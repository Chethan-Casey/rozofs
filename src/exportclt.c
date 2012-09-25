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

/* need for crypt */
#define _XOPEN_SOURCE 500

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "rozofs.h"
#include "log.h"
#include "xmalloc.h"
#include "eproto.h"
#include "exportclt.h"

int exportclt_initialize(exportclt_t * clt, const char *host, char *root,
        const char *passwd, uint32_t bufsize,
        uint32_t retries) {
    int status = -1;
    ep_mount_ret_t *ret = 0;
    char *md5pass = 0;
    int i = 0;
    int j = 0;
    DEBUG_FUNCTION;

    strcpy(clt->host, host);
    clt->root = strdup(root);
    clt->passwd = strdup(passwd);
    clt->retries = retries;
    clt->bufsize = bufsize;

    if (rpcclt_initialize
            (&clt->rpcclt, host, EXPORT_PROGRAM, EXPORT_VERSION,
            ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0)
        goto out;

    ret = ep_mount_1(&root, clt->rpcclt.client);
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mount_ret_t_u.error;
        goto out;
    }
    // check passwd
    if (memcmp
            (ret->ep_mount_ret_t_u.volume.md5, ROZOFS_MD5_NONE,
            ROZOFS_MD5_SIZE) != 0) {
        md5pass = crypt(passwd, "$1$rozofs$");
        if (memcmp
                (md5pass + 10, ret->ep_mount_ret_t_u.volume.md5,
                ROZOFS_MD5_SIZE) != 0) {
            errno = EACCES;
            goto out;
        }
    }

    clt->eid = ret->ep_mount_ret_t_u.volume.eid;
    clt->rl = ret->ep_mount_ret_t_u.volume.rl;
    memcpy(clt->rfid, ret->ep_mount_ret_t_u.volume.rfid, sizeof (fid_t));

    // Initialize the list of clusters
    list_init(&clt->mcs);

    // For each cluster
    for (i = 0; i < ret->ep_mount_ret_t_u.volume.clusters_nb; i++) {

        ep_cluster_t ep_cluster = ret->ep_mount_ret_t_u.volume.clusters[i];

        mcluster_t *cluster = (mcluster_t *) xmalloc(sizeof (mcluster_t));

        DEBUG("cluster (cid: %d)", ep_cluster.cid);

        cluster->cid = ep_cluster.cid;
        cluster->nb_ms = ep_cluster.storages_nb;

        cluster->ms = xmalloc(ep_cluster.storages_nb * sizeof (storageclt_t));

        for (j = 0; j < ep_cluster.storages_nb; j++) {

            DEBUG("storage (sid: %d, host: %s)", ep_cluster.storages[j].sid,
                    ep_cluster.storages[j].host);

            strcpy(cluster->ms[j].host, ep_cluster.storages[j].host);
            cluster->ms[j].sid = ep_cluster.storages[j].sid;
            cluster->ms[j].status = 0;

            //Initialize the connection with the storage
            if (storageclt_initialize(&cluster->ms[j]) != 0) {
                fprintf(stderr,
                        "warning failed to join storage (SID: %d): %s, %s\n",
                        ep_cluster.storages[j].sid,
                        ep_cluster.storages[j].host, strerror(errno));
            }

        }
        // Add to the list
        list_push_back(&clt->mcs, &cluster->list);
    }

    // Initialize rozofs
    if (rozofs_initialize(clt->rl) != 0) {
        fatal("can't initialise rozofs %s", strerror(errno));
        goto out;
    }

    status = 0;
out:
    if (md5pass)
        free(md5pass);
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mount_ret_t, (char *) ret);
    return status;
}

int exportclt_reload(exportclt_t * clt) {
    int status = -1;
    ep_mount_ret_t *ret = 0;
    char *md5pass = 0;
    int i = 0;
    int j = 0;
    list_t *p, *q;
    DEBUG_FUNCTION;

    ret = ep_mount_1(&clt->root, clt->rpcclt.client);
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mount_ret_t_u.error;
        goto out;
    }

    list_for_each_forward_safe(p, q, &clt->mcs) {
        mcluster_t *entry = list_entry(p, mcluster_t, list);
        free(entry->ms);
        list_remove(p);
        free(entry);
    }

    if (memcmp
            (ret->ep_mount_ret_t_u.volume.md5, ROZOFS_MD5_NONE,
            ROZOFS_MD5_SIZE) != 0) {
        md5pass = crypt(clt->passwd, "$1$rozofs$");
        if (memcmp
                (md5pass + 10, ret->ep_mount_ret_t_u.volume.md5,
                ROZOFS_MD5_SIZE) != 0) {
            errno = EACCES;
            goto out;
        }
    }

    clt->eid = ret->ep_mount_ret_t_u.volume.eid;
    clt->rl = ret->ep_mount_ret_t_u.volume.rl;
    memcpy(clt->rfid, ret->ep_mount_ret_t_u.volume.rfid, sizeof (fid_t));

    // Initialize the list of clusters
    list_init(&clt->mcs);

    // For each cluster
    for (i = 0; i < ret->ep_mount_ret_t_u.volume.clusters_nb; i++) {

        ep_cluster_t ep_cluster = ret->ep_mount_ret_t_u.volume.clusters[i];

        mcluster_t *cluster = (mcluster_t *) xmalloc(sizeof (mcluster_t));

        cluster->cid = ep_cluster.cid;
        cluster->nb_ms = ep_cluster.storages_nb;

        cluster->ms = xmalloc(ep_cluster.storages_nb * sizeof (storageclt_t));

        for (j = 0; j < ep_cluster.storages_nb; j++) {

            strcpy(cluster->ms[j].host, ep_cluster.storages[j].host);
            cluster->ms[j].sid = ep_cluster.storages[j].sid;

            //Initialize the connection with the storage
            if (storageclt_initialize(&cluster->ms[j]) != 0) {
                fprintf(stderr,
                        "warning failed to join storage (SID: %d): %s, %s\n",
                        ep_cluster.storages[j].sid,
                        ep_cluster.storages[j].host, strerror(errno));
            }

        }
        // Add to the list
        list_push_back(&clt->mcs, &cluster->list);
    }

    // Initialize rozofs
    if (rozofs_initialize(clt->rl) != 0) {
        fatal("can't initialise rozofs %s", strerror(errno));
        goto out;
    }

    status = 0;
out:
    if (md5pass)
        free(md5pass);
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mount_ret_t, (char *) ret);
    return status;
}

void exportclt_release(exportclt_t * clt) {
    list_t *p, *q;
    int i;

    DEBUG_FUNCTION;

    list_for_each_forward_safe(p, q, &clt->mcs) {
        mcluster_t *entry = list_entry(p, mcluster_t, list);

        for (i = 0; i < entry->nb_ms; i++) {
            storageclt_release(&entry->ms[i]);
        }

        free(entry->ms);
        list_remove(p);
        free(entry);
    }

    free(clt->passwd);
    free(clt->root);

    rpcclt_release(&clt->rpcclt);
}

int exportclt_stat(exportclt_t * clt, estat_t * st) {
    int status = -1;
    ep_statfs_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_statfs_1(&clt->eid, clt->rpcclt.client)))) {
        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_statfs_ret_t_u.error;
        goto out;
    }
    memcpy(st, &ret->ep_statfs_ret_t_u.stat, sizeof (estat_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_statfs_ret_t, (char *) ret);
    return status;
}

int exportclt_lookup(exportclt_t * clt, fid_t parent, char *name,
        mattr_t * attrs) {
    int status = -1;
    ep_lookup_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.parent, parent, sizeof (uuid_t));
    arg.name = name;
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_lookup_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_getattr(exportclt_t * clt, fid_t fid, mattr_t * attrs) {
    int status = -1;
    ep_mfile_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (uuid_t));
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_getattr_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_setattr(exportclt_t * clt, fid_t fid, mattr_t * attrs, int to_set) {
    int status = -1;
    ep_setattr_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(&arg.attrs, attrs, sizeof (mattr_t));
    memcpy(arg.attrs.fid, fid, sizeof (fid_t));
    arg.to_set = to_set;
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_setattr_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_readlink(exportclt_t * clt, fid_t fid, char *link) {
    int status = -1;
    ep_mfile_arg_t arg;
    ep_readlink_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(&arg.fid, fid, sizeof (uuid_t));

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_readlink_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_readlink_ret_t_u.error;
        goto out;
    }
    strcpy(link, ret->ep_readlink_ret_t_u.link);
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_readlink_ret_t, (char *) ret);
    return status;
}

int exportclt_link(exportclt_t * clt, fid_t inode, fid_t newparent, char *newname, mattr_t * attrs) {
    int status = -1;
    ep_link_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.inode, inode, sizeof (uuid_t));
    memcpy(arg.newparent, newparent, sizeof (uuid_t));
    arg.newname = newname;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_link_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_mknod(exportclt_t * clt, fid_t parent, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t * attrs) {
    int status = -1;
    ep_mknod_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.parent, parent, sizeof (uuid_t));
    arg.name = name;
    arg.uid = uid;
    arg.gid = gid;
    arg.mode = mode;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_mknod_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_mkdir(exportclt_t * clt, fid_t parent, char *name, uint32_t uid,
        uint32_t gid, mode_t mode, mattr_t * attrs) {
    int status = -1;
    ep_mkdir_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.parent, parent, sizeof (uuid_t));
    arg.name = name;
    arg.uid = uid;
    arg.gid = gid;
    arg.mode = mode;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_mkdir_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_unlink(exportclt_t * clt, fid_t pfid, char *name, fid_t * fid) {
    int status = -1;
    ep_unlink_arg_t arg;
    ep_fid_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.pfid, pfid, sizeof (uuid_t));
    arg.name = name;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_unlink_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_fid_ret_t_u.error;
        goto out;
    }
    memcpy(fid, &ret->ep_fid_ret_t_u.fid, sizeof (fid_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_fid_ret_t, (char *) ret);
    return status;
}

int exportclt_rmdir(exportclt_t * clt, fid_t pfid, char *name, fid_t * fid) {
    int status = -1;
    ep_rmdir_arg_t arg;
    ep_fid_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.pfid, pfid, sizeof (uuid_t));
    arg.name = name;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_rmdir_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_fid_ret_t_u.error;
        goto out;
    }
    memcpy(fid, &ret->ep_fid_ret_t_u.fid, sizeof (fid_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_fid_ret_t, (char *) ret);
    return status;
}

int exportclt_symlink(exportclt_t * clt, char *link, fid_t parent, char *name,
        mattr_t * attrs) {
    int status = -1;
    ep_symlink_arg_t arg;
    ep_mattr_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    arg.link = link;
    arg.name = name;
    memcpy(arg.parent, parent, sizeof (fid_t));

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_symlink_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_mattr_ret_t_u.error;
        goto out;
    }
    memcpy(attrs, &ret->ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_mattr_ret_t, (char *) ret);
    return status;
}

int exportclt_rename(exportclt_t * clt, fid_t parent, char *name, fid_t newparent, char *newname, fid_t * fid) {
    int status = -1;
    ep_rename_arg_t arg;
    ep_fid_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.pfid, parent, sizeof (fid_t));
    arg.name = name;
    memcpy(arg.npfid, newparent, sizeof (fid_t));
    arg.newname = newname;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_rename_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_fid_ret_t_u.error;
        goto out;
    }
    memcpy(fid, &ret->ep_fid_ret_t_u.fid, sizeof (fid_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_fid_ret_t, (char *) ret);
    return status;
}

/*
int64_t exportclt_read(exportclt_t * clt, fid_t fid, uint64_t off,
        uint32_t len) {
    int64_t lenght = -1;
    ep_io_arg_t arg;
    ep_io_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));
    arg.offset = off;
    arg.length = len;

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_read_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_io_ret_t_u.error;
        goto out;
    }
    lenght = ret->ep_io_ret_t_u.length;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_io_ret_t, (char *) ret);
    return lenght;
}
 */

dist_t * exportclt_read_block(exportclt_t * clt, fid_t fid, uint64_t off, uint32_t len, int64_t * length) {
    dist_t * dist = NULL;
    ep_io_arg_t arg;
    ep_read_block_ret_t *ret = 0;
    int retry = 0;

    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));
    arg.offset = off;
    arg.length = len;

    while ((retry++ < clt->retries) && (!(clt->rpcclt.client) || !(ret = ep_read_block_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize(&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }

    if (ret->status == EP_FAILURE) {
        errno = ret->ep_read_block_ret_t_u.error;
        goto out;
    }

    dist = xmalloc(ret->ep_read_block_ret_t_u.ret.dist.dist_len * sizeof (dist_t));
    memcpy(dist, ret->ep_read_block_ret_t_u.ret.dist.dist_val, ret->ep_read_block_ret_t_u.ret.dist.dist_len * sizeof (dist_t));
    *length = ret->ep_read_block_ret_t_u.ret.length;

out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_read_block_ret_t, (char *) ret);
    return dist;
}

/*
int64_t exportclt_write(exportclt_t * clt, fid_t fid, uint64_t off,
        uint32_t len) {
    int64_t lenght = -1;
    ep_io_arg_t arg;
    ep_io_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));
    arg.offset = off;
    arg.length = len;
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_write_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_io_ret_t_u.error;
        goto out;
    }
    lenght = ret->ep_io_ret_t_u.length;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_io_ret_t, (char *) ret);
    return lenght;
}
 */

int64_t exportclt_write_block(exportclt_t * clt, fid_t fid, bid_t bid, uint32_t n, dist_t d, uint64_t off, uint32_t len) {
    int64_t length = -1;
    ep_write_block_arg_t arg;
    ep_io_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));
    arg.bid = bid;
    arg.nrb = n;
    arg.length = len;
    arg.offset = off;

    arg.dist = d;
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_write_block_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_io_ret_t_u.error;
        goto out;
    }
    length = ret->ep_io_ret_t_u.length;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_io_ret_t, (char *) ret);
    return length;
}

int exportclt_readdir(exportclt_t * clt, fid_t fid, uint64_t * cookie, child_t ** children, uint8_t * eof) {
    int status = -1;
    ep_readdir_arg_t arg;
    ep_readdir_ret_t *ret = 0;
    int retry = 0;
    ep_children_t it1;
    child_t **it2;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));
    arg.cookie = *cookie;

    // Send readdir request to export
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_readdir_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }
    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_readdir_ret_t_u.error;
        goto out;
    }

    // Copy list of children
    it2 = children;
    it1 = ret->ep_readdir_ret_t_u.reply.children;
    while (it1 != NULL) {
        *it2 = xmalloc(sizeof (child_t));
        memcpy((*it2)->fid, it1->fid, sizeof (fid_t));
        (*it2)->name = strdup(it1->name);
        it2 = &(*it2)->next;
        it1 = it1->next;
    }
    *it2 = NULL;
    
    // End of readdir?
    *eof = ret->ep_readdir_ret_t_u.reply.eof;
    *cookie = ret->ep_readdir_ret_t_u.reply.cookie;
    
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_readdir_ret_t, (char *) ret);
    return status;
}

/* not used anymore
int exportclt_open(exportclt_t * clt, fid_t fid) {

    int status = -1;
    ep_mfile_arg_t arg;
    ep_status_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_open_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_status_ret_t_u.error;
        goto out;
    }
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_status_ret_t, (char *) ret);
    return status;
}

int exportclt_close(exportclt_t * clt, fid_t fid) {

    int status = -1;
    ep_mfile_arg_t arg;
    ep_status_ret_t *ret = 0;
    int retry = 0;
    DEBUG_FUNCTION;

    arg.eid = clt->eid;
    memcpy(arg.fid, fid, sizeof (fid_t));

    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret = ep_close_1(&arg, clt->rpcclt.client)))) {

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE) != 0) {
            rpcclt_release(&clt->rpcclt);
            errno = EPROTO;
        }
    }

    if (ret == 0) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status == EP_FAILURE) {
        errno = ret->ep_status_ret_t_u.error;
        goto out;
    }
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_ep_status_ret_t, (char *) ret);
    return status;
}
 */
