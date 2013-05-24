/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation, version 2.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>

#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>

#include "config.h"
#include "monitor.h"
#include "econfig.h"

DECLARE_PROFILING(epp_profiler_t);

#define HEADER "\
# This file was generated by exportd(8) version: %s.\n\
# All changes to this file will be lost.\n\n"

int monitor_initialize() {
    int status = -1;
    char path[FILENAME_MAX];
    DEBUG_FUNCTION;

    sprintf(path, "%s%s", DAEMON_PID_DIRECTORY, "exportd");
    if (access(path, X_OK) != 0) {
        if (mkdir(path, S_IRWXU) != 0) {
            severe("can't create %s", path);
            goto out;
        }
    }
    status = 0;
out:
    return status;
}

void monitor_release() {
    //XXX should clean MONITOR_DIRECTORY
    return;
}

int monitor_volume(volume_t *volume) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];
    volume_stat_t vstat;
    list_t *p, *q;
    volume_t clone;
    uint32_t nb_storages = 0;

    volume_initialize(&clone, 0);
    if (volume_safe_copy(&clone, volume) != 0) {
        severe("can't clone volume: %d", volume->vid);
        goto out;
    }

    sprintf(path, "%s%s%d", DAEMON_PID_DIRECTORY, "exportd/volume_", clone.vid);
    if ((fd = open(path, O_WRONLY | O_CREAT, S_IRWXU | S_IROTH)) < 0) {
        severe("can't open %s", path);
        goto out;
    }

    gprofiler.vstats[gprofiler.nb_volumes].vid = clone.vid;

    dprintf(fd, HEADER, VERSION);
    dprintf(fd, "volume: %u\n", clone.vid);

    //XXX TO CHANGE
    volume_stat(&clone, 0, &vstat);
    gprofiler.vstats[gprofiler.nb_volumes].bsize = vstat.bsize;
    gprofiler.vstats[gprofiler.nb_volumes].bfree = vstat.bfree;
    gprofiler.vstats[gprofiler.nb_volumes].blocks = vstat.blocks;

    dprintf(fd, "bsize: %u\n", vstat.bsize);
    dprintf(fd, "bfree: %"PRIu64"\n", vstat.bfree);
    dprintf(fd, "blocks: %"PRIu64"\n", vstat.blocks);
    dprintf(fd, "nb_clusters: %d\n", list_size(&clone.clusters));

    list_for_each_forward(p, &clone.clusters) {
        cluster_t *cluster = list_entry(p, cluster_t, list);
        dprintf(fd, "cluster: %u\n", cluster->cid);
        dprintf(fd, "nb_storages: %d\n", list_size(&cluster->storages));
        dprintf(fd, "size: %"PRIu64"\n", cluster->size);
        dprintf(fd, "free: %"PRIu64"\n", cluster->free);

        list_for_each_forward(q, &cluster->storages) {
            volume_storage_t *storage = list_entry(q, volume_storage_t, list);

            gprofiler.vstats[gprofiler.nb_volumes].sstats[nb_storages].sid = storage->sid;
            gprofiler.vstats[gprofiler.nb_volumes].sstats[nb_storages].status = storage->status;
            gprofiler.vstats[gprofiler.nb_volumes].sstats[nb_storages].size = storage->stat.size;
            gprofiler.vstats[gprofiler.nb_volumes].sstats[nb_storages].free = storage->stat.free;
            nb_storages++;

            dprintf(fd, "storage: %u\n", storage->sid);
            dprintf(fd, "host: %s\n", storage->host);
            dprintf(fd, "status: %u\n", storage->status);
            dprintf(fd, "size: %"PRIu64"\n", storage->stat.size);
            dprintf(fd, "free: %"PRIu64"\n", storage->stat.free);
        }
    }
    gprofiler.vstats[gprofiler.nb_volumes].nb_storages = nb_storages;

    // Free the clone volume
    p = NULL;
    q = NULL;

    list_for_each_forward_safe(p, q, &clone.clusters) {
        cluster_t *entry = list_entry(p, cluster_t, list);
        list_remove(p);
        cluster_release(entry);
        free(entry);
    }

    status = 0;
out:
    if (fd > 0) close(fd);
    return status;
}

int monitor_export(export_t *export) {
    int status = -1;
    int fd = -1;
    char path[FILENAME_MAX];
    estat_t estat;
    uint64_t exceed = 0;
    DEBUG_FUNCTION;

    sprintf(path, "%s%s%d", DAEMON_PID_DIRECTORY, "exportd/export_", export->eid);
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU | S_IROTH)) < 0) {
        severe("can't open %s", path);
        goto out;
    }

    if (export_stat(export, &estat) != 0) {
        severe("can't stat export: %"PRIu32"", export->eid);
        goto out;
    }

    gprofiler.estats[gprofiler.nb_exports].eid = export->eid;
    gprofiler.estats[gprofiler.nb_exports].vid = export->volume->vid;
    gprofiler.estats[gprofiler.nb_exports].bsize = estat.bsize;
    gprofiler.estats[gprofiler.nb_exports].blocks = estat.blocks;
    gprofiler.estats[gprofiler.nb_exports].bfree = estat.bfree;
    gprofiler.estats[gprofiler.nb_exports].files = estat.files;
    gprofiler.estats[gprofiler.nb_exports].ffree = estat.ffree;

    dprintf(fd, HEADER, VERSION);
    dprintf(fd, "export: %"PRIu32"\n", export->eid);
    dprintf(fd, "volume: %u\n", export->volume->vid);
    dprintf(fd, "root: %s\n", export->root);
    dprintf(fd, "squota: %"PRIu64"\n", export->squota);
    dprintf(fd, "hquota: %"PRIu64"\n", export->hquota);
    dprintf(fd, "bsize: %u\n", estat.bsize);
    dprintf(fd, "blocks: %"PRIu64"\n", estat.blocks);
    dprintf(fd, "bfree: %"PRIu64"\n", estat.bfree);
    dprintf(fd, "files: %"PRIu64"\n", estat.files);
    dprintf(fd, "ffree: %"PRIu64"\n", estat.ffree);
    if (export->squota > 0) {
        exceed = estat.blocks - estat.bfree > export->squota ?
                estat.blocks - estat.bfree - export->squota : 0;
    }
    dprintf(fd, "squota_exceed: %"PRIu64"\n", exceed);

    status = 0;
out:
    if (fd > 0) close(fd);
    return status;
}
