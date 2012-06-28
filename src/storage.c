/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 3 of the License,
 or (at your option) any later version.

 Rozofs is distributed in the hope that it will be useful, but
 WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see
 <http://www.gnu.org/licenses/>.
 */

#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <inttypes.h>
#include <glob.h>

#include "rozofs.h"
#include "log.h"
#include "list.h"
#include "xmalloc.h"
#include "storage.h"

#define STORAGE_CSIZE	32
#define STORAGE_HSIZE	8

static char *storage_map(storage_t * st, fid_t fid, tid_t pid, char *path) {
    char str[37];
    DEBUG_FUNCTION;

    strcpy(path, st->root);
    strcat(path, "/");
    uuid_unparse(fid, str);
    strcat(path, str);
    sprintf(str, "-%d.bins", pid);
    strcat(path, str);
    return path;
}

typedef struct pfentry {
    fid_t fid;
    tid_t pid;
    int fd;
    list_t list;
} pfentry_t;

static int pfentry_initialize(pfentry_t * pfe, const char *path, fid_t fid,
        tid_t pid) {
    int status = -1;
    DEBUG_FUNCTION;

    uuid_copy(pfe->fid, fid);
    pfe->pid = pid;
    if ((pfe->fd = open(path,
            O_RDWR | O_CREAT, S_IFREG | S_IRUSR | S_IWUSR)) < 0) {
        severe("pfentry_initialize failed: open for file %s failed: %s", path,
                strerror(errno));
        goto out;
    }
    list_init(&pfe->list);

    status = 0;
out:
    return status;
}

static void pfentry_release(pfentry_t * pfe) {
    DEBUG_FUNCTION;
    if (pfe)
        close(pfe->fd);
}

static uint32_t pfentry_hash(void *key) {
    pfentry_t *pfe = (pfentry_t *) key;
    uint32_t hash = 0;
    uint8_t *c;
    DEBUG_FUNCTION;

    for (c = pfe->fid; c != pfe->fid + 16; c++)
        hash = *c + (hash << 6) + (hash << 16) - hash;
    hash = pfe->pid + (hash << 6) + (hash << 16) - hash;
    return hash;
}

static int pfentry_cmp(void *k1, void *k2) {
    DEBUG_FUNCTION;
    pfentry_t *sk1 = (pfentry_t *) k1;
    pfentry_t *sk2 = (pfentry_t *) k2;
    if ((uuid_compare(sk1->fid, sk2->fid) == 0) && (sk1->pid == sk2->pid)) {
        return 0;
    } else {
        return 1;
    }
}

static void storage_put_pfentry(storage_t * st, pfentry_t * pfe) {
    DEBUG_FUNCTION;
    htable_put(&st->htable, pfe, pfe);
    list_push_front(&st->pfiles, &pfe->list);
    st->csize++;
}

static void storage_del_pfentry(storage_t * st, pfentry_t * pfe) {
    DEBUG_FUNCTION;
    htable_del(&st->htable, pfe);
    list_remove(&pfe->list);
    st->csize--;
}

static pfentry_t *storage_find_pfentry(storage_t * st, fid_t fid, tid_t pid) {
    pfentry_t key;
    pfentry_t *pfe = 0;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    uuid_copy(key.fid, fid);
    key.pid = pid;

    if (!(pfe = htable_get(&st->htable, &key))) {
        pfe = xmalloc(sizeof (pfentry_t));
        if (pfentry_initialize(pfe, storage_map(st, fid, pid, path), fid, pid)
                != 0) {
            free(pfe);
            pfe = 0;
            warning("storage_find_pfentry failed");
            goto out;
        }
        // if cache is full delete the tail of the list which should be the lru.
        if (st->csize == STORAGE_CSIZE) {
            pfentry_t *lru = list_entry(st->pfiles.prev, pfentry_t, list);
            storage_del_pfentry(st, lru);
            pfentry_release(lru);
            free(lru);
        }
        storage_put_pfentry(st, pfe);
    } else {
        // push the lru.
        list_remove(&pfe->list);
        list_push_front(&st->pfiles, &pfe->list);
    }
out:
    return pfe;
}

int storage_initialize(storage_t * st, sid_t sid, const char *root) {
    int status = -1;
    struct stat s;
    DEBUG_FUNCTION;

    if (!realpath(root, st->root))
        goto out;
    // sanity checks
    if (stat(st->root, &s) != 0)
        goto out;
    if (!S_ISDIR(s.st_mode)) {
        errno = ENOTDIR;
        goto out;
    }
    st->sid = sid;
    st->csize = 0;
    htable_initialize(&st->htable, STORAGE_HSIZE, pfentry_hash, pfentry_cmp);
    list_init(&st->pfiles);

    status = 0;
out:
    return status;
}

void storage_release(storage_t * st) {
    list_t *p, *q;
    DEBUG_FUNCTION;

    list_for_each_forward_safe(p, q, &st->pfiles) {
        pfentry_t *pfe = list_entry(p, pfentry_t, list);
        storage_del_pfentry(st, pfe);
        pfentry_release(pfe);
        free(pfe);
    }
    htable_release(&st->htable);
}

int storage_write(storage_t * st, fid_t fid, tid_t pid, bid_t bid, uint32_t n,
        size_t len, const bin_t * bins) {
    int status = -1;
    pfentry_t *pfe = 0;
    size_t count = 0;
    size_t nb_write = 0;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    if (!(pfe = storage_find_pfentry(st, fid, pid)))
        goto out;

    count = n * rozofs_psizes[pid] * sizeof (bin_t);
    if ((nb_write =
            pwrite(pfe->fd, bins, len,
            (off_t) bid * (off_t) rozofs_psizes[pid] *
            (off_t) sizeof (bin_t))) != count) {
        severe("storage_write failed: pwrite in file %s failed: %s",
                storage_map(st, fid, pid, path), strerror(errno));
        if (nb_write != -1) {
            severe("pwrite failed: %zu bytes written instead of %zu",
                    nb_write, count);
            errno = EIO;
        }
        goto out;
    }

    status = 0;
out:
    return status;
}

int storage_read(storage_t * st, fid_t fid, tid_t pid, bid_t bid, uint32_t n,
        bin_t * bins) {
    int status = -1;
    pfentry_t *pfe = 0;
    size_t count;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    if (!(pfe = storage_find_pfentry(st, fid, pid)))
        goto out;

    count = n * rozofs_psizes[pid] * sizeof (bin_t);

    if (pread
            (pfe->fd, bins, count,
            (off_t) bid * (off_t) rozofs_psizes[pid] * (off_t) sizeof (bin_t)) !=
            count) {
        severe("storage_read failed: pread in file %s failed: %s",
                storage_map(st, fid, pid, path), strerror(errno));
        goto out;
    }

    status = 0;
out:
    return status;
}

int storage_truncate(storage_t * st, fid_t fid, tid_t pid, bid_t bid) {
    int status = -1;
    pfentry_t *pfe = 0;
    DEBUG_FUNCTION;

    if (!(pfe = storage_find_pfentry(st, fid, pid)))
        goto out;
    status =
            ftruncate(pfe->fd, (bid + 1) * rozofs_psizes[pid] * sizeof (bin_t));
out:
    return status;
}

int storage_rm_file(storage_t * st, fid_t fid) {
    int status = -1;
    char bins_filename[FILENAME_MAX];
    char fid_str[37];
    pfentry_t key;
    pfentry_t *pfe = 0;
    pid_t pid;
    
    DEBUG_FUNCTION;

    if (chdir(st->root) != 0) {
        goto out;
    }

    uuid_unparse(fid, fid_str);

    for (pid = 0; pid < rozofs_forward; pid++) {

        if (sprintf(bins_filename, "%36s-%u.bins", fid_str, pid) < 0) {
            severe("storage_rm_file failed: sprintf for bins %s failed: %s", fid_str, strerror(errno));
            goto out;
        }

        if (unlink(bins_filename) == -1) {
            if (errno != ENOENT) {
                severe("storage_rm_file failed: unlink file %s failed: %s", bins_filename, strerror(errno));
                goto out;
            }
        }

        uuid_copy(key.fid, fid);
        key.pid = pid;
        if ((pfe = htable_get(&st->htable, &key))) {
            storage_del_pfentry(st, pfe);
            pfentry_release(pfe);
            free(pfe);
        }
    }

    status = 0;
out:
    return status;
}

int storage_stat(storage_t * st, sstat_t * sstat) {
    int status = -1;
    struct statfs sfs;
    DEBUG_FUNCTION;

    if (statfs(st->root, &sfs) == -1)
        goto out;
    sstat->size = (uint64_t) sfs.f_blocks * (uint64_t) sfs.f_bsize;
    sstat->free = (uint64_t) sfs.f_bfree * (uint64_t) sfs.f_bsize;
    status = 0;
out:
    return status;
}
