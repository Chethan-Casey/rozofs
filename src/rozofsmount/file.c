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

#include <errno.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rozofs_srv.h>
#include <rozofs/rpc/sproto.h>

#include "file.h"


void buf_file_write(rozo_buf_rw_status_t *status_p,
                   file_t * p,
                   uint64_t off, 
                   const char *buf, 
                   uint32_t len) ;

// For compute the timestamp
#define MICROLONG(time) ((uint64_t )time.tv_sec * 1000000 + time.tv_usec)

/** Get one storage connection for a given couple (cid;sid) and given 
 *  random value
 *
 * @param *e: pointer to exportclt_t
 * @param cid: cluster ID for this storage
 * @param sid: storage ID
 * @param rand_value: random value
 *
 * @return: storage connection on success, NULL otherwise (errno: EINVAL is set)
 */
static sclient_t *get_storage_cnt(exportclt_t * e, cid_t cid, sid_t sid,
        uint8_t rand_value) {
    list_t *iterator;
    int i = 0;
    DEBUG_FUNCTION;

    list_for_each_forward(iterator, &e->storages) {

        mstorage_t *entry = list_entry(iterator, mstorage_t, list);

        for (i = 0; i < entry->sids_nb; i++) {

            if ((sid == entry->sids[i]) && (cid == entry->cids[i])) {
                // The good storage node is find
                // modulo between all connections for this node
                if (entry->sclients_nb > 0) {
                    rand_value %= entry->sclients_nb;
                    return &entry->sclients[rand_value];
                } else {
                    // We find a storage node for this sid but
                    // it don't have connection.
                    // It's only possible when all connections for one storage
                    // node are down when we mount the filesystem or we don't
                    // have get ports for this storage when we mount the
                    // the filesystem
                    severe("No connection found for storage (cid: %u, sid: %u)",
                            cid, sid);
                    return NULL;
                }
            }
        }
    }
    severe("Storage point (cid: %u ; sid: %u) is unknow,"
            " the volume has been modified",
            cid, sid);
    // XXX: We must send a request to the export server
    // for reload the configuration
    return NULL;
}

/** Get connections to storage servers for a given file and
 *  verifies that the number of connections to storage servers is sufficient
 *
 *  To verify that number of connections is sufficient from a given distribution
 *  then we must pass the distribution as parameter
 *
 * This function computes a pseudo-random integer in the range 0 to RAND_MAX
 * inclusive to select a connection among all those available for
 * one storage server (one SID).
 * That allows you to load balancing connections between proccess
 * for one storage server
 *
 * @param *f: pointer to the file structure
 * @param nb_required: nb. of connections required
 * @param *dist_p: pointer to the distribution if necessary or NULL
 *
 * @return: 0 on success -1 otherwise (errno: EIO is set)
 */
int file_get_cnts(file_t * f, uint8_t nb_required, dist_t * dist_p) {
    int i = 0;
    int connected = 0;
    struct timespec ts = {2, 0}; /// XXX static
    uint8_t rand_value = 0;
    uint8_t rozofs_safe = 0;

    DEBUG_FUNCTION;

    rozofs_safe = rozofs_get_rozofs_safe(f->export->layout);

    // Get a pseudo-random integer in the range 0 to RAND_MAX inclusive
    rand_value = rand();

    for (i = 0; i < rozofs_safe; i++) {
        // Not necessary to check the distribution
        if (dist_p == NULL) {

            // Get connection for this storage
            if ((f->storages[i] = get_storage_cnt(f->export, f->attrs.cid,
                    f->attrs.sids[i], rand_value)) != NULL) {

                // Check connection status
                if (f->storages[i]->status == 1 &&
                        f->storages[i]->rpcclt.client != 0)
                    connected++; // This storage seems to be OK
            }

        } else {
            // Check if the storage server has data
            if (dist_is_set(*dist_p, i)) {
                // Get connection for this storage
                if ((f->storages[i] = get_storage_cnt(f->export,
                        f->attrs.cid, f->attrs.sids[i], rand_value)) != NULL) {

                    // Check connection status
                    if (f->storages[i]->status == 1 &&
                            f->storages[i]->rpcclt.client != 0)
                        connected++; // This storage seems to be OK
                }
            }
        }
    }

    // Is it sufficient?
    if (connected < nb_required) {
        // Wait a little time for the thread try to reconnect one storage
        nanosleep(&ts, NULL);
        errno = EIO;
        return -1;
    }

    return 0;
}

/** Reads the data on the storage servers and perform the inverse transform
 *
 * If the distribution is identical for several consecutive blocks then
 * it sends only one request to each server (request cluster)
 *
 * @param *f: pointer to the file structure
 * @param bid: first block address (from the start of the file)
 * @param nb_blocks: number of blocks to read
 * @param *data: pointer where the data will be stored
 * @param *dist: pointer to distributions of different blocks to read
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: 0 on success -1 otherwise (errno is set)
 */
static int read_blocks(file_t * f, bid_t bid, uint32_t nb_blocks, char *data,
        dist_t * dist, uint16_t * last_block_size_p) {
    int status = -1, i, j;
    dist_t *dist_iterator = NULL;
    uint8_t proj_id = 0;
    bin_t **bins;
    projection_t *projections = NULL;
    angle_t *angles = NULL;
    uint16_t *psizes = NULL;
    int receive = 0;
    uint8_t spare = 0;

    DEBUG_FUNCTION;

    // Get the parameters relative to this layout
    uint8_t rozofs_layout = f->export->layout;
    uint8_t rozofs_forward = rozofs_get_rozofs_forward(rozofs_layout);
    uint8_t rozofs_inverse = rozofs_get_rozofs_inverse(rozofs_layout);
    uint8_t rozofs_safe = rozofs_get_rozofs_safe(rozofs_layout);

    bins = xcalloc(rozofs_inverse, sizeof (bin_t *));
    projections = xmalloc(rozofs_inverse * sizeof (projection_t));
    angles = xmalloc(rozofs_inverse * sizeof (angle_t));
    psizes = xmalloc(rozofs_inverse * sizeof (uint16_t));
    memset(data, 0, nb_blocks * ROZOFS_BSIZE);

    /* Until we don't decode all data blocks (nmbs blocks) */
    i = 0; // Nb. of blocks decoded (at begin = 0)
    dist_iterator = dist;
    while (i < nb_blocks) {
        if (*dist_iterator == 0) {
            i++;
            dist_iterator++;
            continue;
        }

        /* We calculate the number of blocks with identical distributions */
        uint32_t nb_blocks_identical_dist = 1;
        while ((i + nb_blocks_identical_dist) < nb_blocks && *dist_iterator == *(dist_iterator + 1)) {
            nb_blocks_identical_dist++;
            dist_iterator++;
        }
        // I don't know if it's possible
        if (i + nb_blocks_identical_dist > nb_blocks)
            goto out;

        int retry = 0;
        do {
            // Nb. of received requests (at begin=0)
            uint8_t nb_proj_recv = 0;
            // Nb. of spare projection encountered
            uint8_t current_proj_spare_nb = 0;

            // For each existent projection
            for (proj_id = 0; proj_id < rozofs_forward; proj_id++) {

                uint8_t proj_stor_idx = 0;
                bin_t *b = NULL;

                // Find the projection server for projection proj_id
                // Two cases possible: 
                // This projection was stored on a no-spare server
                // This projection was stored on a spare server
                if (dist_is_set(*dist_iterator, proj_id)) {
                    // The proj. is store on a no-spare server
                    proj_stor_idx = proj_id;
                    spare = 0;
                } else {

                    // This projection is store on a spare server                    
                    uint8_t stor_idx = 0;
                    uint8_t local_spare_cnt = 0;

                    // Find the storage for this proj.
                    // For each spare storage
                    for (stor_idx = rozofs_forward; stor_idx < rozofs_safe; stor_idx++) {

                        // Verify if this spare storage have this projection
                        if (dist_is_set(*dist_iterator, stor_idx) && local_spare_cnt == current_proj_spare_nb) {
                            proj_stor_idx = stor_idx;
                            current_proj_spare_nb++;
                            spare = 1;
                            break;
                        } else {
                            // Try with the next spare storage
                            local_spare_cnt += dist_is_set(*dist_iterator, stor_idx);
                        }
                    }
                }

                if (!f->storages[proj_stor_idx]->rpcclt.client || f->storages[proj_stor_idx]->status != 1)
                    continue; // Try with the next projection

                b = xmalloc(nb_blocks_identical_dist * ((rozofs_get_max_psize(rozofs_layout) * sizeof (bin_t)) + sizeof (rozofs_stor_bins_hdr_t)));

                if (sclient_read(f->storages[proj_stor_idx], f->attrs.cid, f->attrs.sids[proj_stor_idx], f->export->layout, spare, f->attrs.sids, f->fid, bid + i, nb_blocks_identical_dist, b) != 0) {
                    free(b);
                    continue; // Try with the next projection
                }

                // If we read the last block, get the last_block_size
                if ((i + nb_blocks_identical_dist) == nb_blocks) {
                    // Get pointer on last projection header
                    rozofs_stor_bins_hdr_t *rozofs_bins_hdr_p = (rozofs_stor_bins_hdr_t*) (b
                            + ((rozofs_get_max_psize(rozofs_layout)+(sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t))) * (nb_blocks_identical_dist - 1)));
                    // Get last_block_size
                    *last_block_size_p = rozofs_bins_hdr_p->s.effective_length;
                }

                bins[nb_proj_recv] = b;

                angles[nb_proj_recv].p = rozofs_get_angles_p(rozofs_layout, proj_id);
                angles[nb_proj_recv].q = rozofs_get_angles_q(rozofs_layout, proj_id);
                psizes[nb_proj_recv] = rozofs_get_psizes(rozofs_layout, proj_id);

                // Increment the number of received requests
                if (++nb_proj_recv == rozofs_inverse)
                    break;
            }

            if (nb_proj_recv == rozofs_inverse) {
                // All data were received
                receive = 1;
                break;
            }

            // If file_get_cnts fail
            // It's not necessary to retry to read on storage servers
            while (file_get_cnts(f, rozofs_inverse, dist_iterator) != 0 && retry++ < f->export->retries);

        } while (retry++ < f->export->retries);

        // Not enough server storage response to retrieve the file
        if (receive != 1) {
            errno = EIO;
            goto out;
        }

        // Proceed the inverse data transform for the n blocks.
        for (j = 0; j < nb_blocks_identical_dist; j++) {
            // Fill the table of projections for the block j
            // For each meta-projection
            for (proj_id = 0; proj_id < rozofs_inverse; proj_id++) {
                // It's really important to specify the angles and sizes here
                // because the data inverse function sorts the projections.
                projections[proj_id].angle.p = angles[proj_id].p;
                projections[proj_id].angle.q = angles[proj_id].q;
                projections[proj_id].size = psizes[proj_id];
                projections[proj_id].bins = bins[proj_id] + ((rozofs_get_max_psize(rozofs_layout) + (sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t))) * j) + (sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t));
            }

            // Inverse data for the block j
            transform_inverse((pxl_t *) (data + (ROZOFS_BSIZE * (i + j))),
                    rozofs_inverse,
                    ROZOFS_BSIZE / rozofs_inverse / sizeof (pxl_t),
                    rozofs_inverse, projections);
        }
        // Free the memory area where are stored the bins.
        for (proj_id = 0; proj_id < rozofs_inverse; proj_id++) {
            if (bins[proj_id])
                free(bins[proj_id]);
            bins[proj_id] = 0;
        }
        // Increment the nb. of blocks decoded
        i += nb_blocks_identical_dist;
        // Shift to the next distribution
        dist_iterator++;
    }
    // If everything is OK, the status is set to 0
    status = 0;
out:
    // Free the memory area where are stored the bins used by the inverse transform
    if (bins) {
        for (proj_id = 0; proj_id < rozofs_inverse; proj_id++)
            if (bins[proj_id])
                free(bins[proj_id]);
        free(bins);
    }
    if (projections)
        free(projections);
    if (angles)
        free(angles);
    if (psizes)
        free(psizes);

    return status;
}

/** Perform the transform, write the data on storage servers and write
 *  the distribution on the export server
 *
 * If the distribution is identical for several consecutive blocks then
 * it sends only one request to each server (request cluster)
 *
 * @param *f: pointer to the file structure
 * @param bid: first block address (from the start of the file)
 * @param nb_blocks: number of blocks/projections to write
 * @param *data: pointer where the data are be stored
 * @param off: offset to write from
 * @param len: length to write
 * @param last_block_size: size of the last block to write
 *
 * @return: the length written on success, -1 otherwise (errno is set)
 */
static int64_t write_blocks(file_t * f, bid_t bid, uint32_t nb_blocks,
        const char *data, uint64_t off, uint32_t len,
        uint16_t last_block_size) {
    projection_t *projections; // Table of projections used to transform data
    bin_t **bins;
    dist_t dist = 0; // Important
    uint8_t proj_id = 0;
    uint8_t proj_stor_idx = 0;
    uint32_t i = 0;
    int j = 0;
    int retry = 0;
    int send = 0;
    uint8_t nb_proj_send = 0;
    int64_t length = -1;
    uint8_t spare = 0;

    DEBUG_FUNCTION;

    // Get rozofs layout parameters
    uint8_t rozofs_layout = f->export->layout;
    uint8_t rozofs_forward = rozofs_get_rozofs_forward(rozofs_layout);
    uint8_t rozofs_inverse = rozofs_get_rozofs_inverse(rozofs_layout);
    uint8_t rozofs_safe = rozofs_get_rozofs_safe(rozofs_layout);
    uint16_t rozofs_max_psize = rozofs_get_max_psize(rozofs_layout);
    uint8_t proj_idx_send[rozofs_forward];

    projections = xmalloc(rozofs_forward * sizeof (projection_t));
    bins = xcalloc(rozofs_forward, sizeof (bin_t *));

    // For each projection
    for (proj_id = 0; proj_id < rozofs_forward; proj_id++) {
        bins[proj_id] = xmalloc((rozofs_max_psize * sizeof (bin_t) +
                sizeof (rozofs_stor_bins_hdr_t)) * nb_blocks);
        memset(bins[proj_id], 0, (rozofs_max_psize * sizeof (bin_t) +
                sizeof (rozofs_stor_bins_hdr_t)) * nb_blocks);
        projections[proj_id].angle.p = rozofs_get_angles_p(rozofs_layout, proj_id);
        projections[proj_id].angle.q = rozofs_get_angles_q(rozofs_layout, proj_id);
        projections[proj_id].size = rozofs_get_psizes(rozofs_layout, proj_id);
    }

    /* Transform the data */
    // For each block to send
    for (i = 0; i < nb_blocks; i++) {

        // Compute the timestamp
        struct timeval timeDay;
        gettimeofday(&timeDay, (struct timezone *) 0);
        uint64_t ts = MICROLONG(timeDay);

        // seek bins for each projection
        for (proj_id = 0; proj_id < rozofs_forward; proj_id++) {

            // Indicates the memory area where the transformed data must be stored
            projections[proj_id].bins = bins[proj_id] + ((rozofs_max_psize + (sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t))) * i);

            rozofs_stor_bins_hdr_t *rozofs_stor_bins_hdr_p = (rozofs_stor_bins_hdr_t*) projections[proj_id].bins;

            // Fill the header of the projection
            rozofs_stor_bins_hdr_p->s.projection_id = proj_id;
            rozofs_stor_bins_hdr_p->s.timestamp = ts;
            // Set the effective size of the block.
            // It is always ROZOFS_BSIZE except for the last block
            if (i == (nb_blocks - 1)) {
                rozofs_stor_bins_hdr_p->s.effective_length = last_block_size;
            } else {
                rozofs_stor_bins_hdr_p->s.effective_length = ROZOFS_BSIZE;
            }
            // XXX: TO CHANGE
            rozofs_stor_bins_hdr_p->s.version = 0;
            rozofs_stor_bins_hdr_p->s.filler = 0;

            // update the pointer to point out the first bins
            projections[proj_id].bins += sizeof (rozofs_stor_bins_hdr_t) / sizeof (bin_t);
        }

        // Apply the erasure code transform for the block i
        transform_forward((pxl_t *) (data + (i * ROZOFS_BSIZE)),
                rozofs_inverse,
                ROZOFS_BSIZE / rozofs_inverse / sizeof (pxl_t),
                rozofs_forward, projections);
    }

    do {
        /* Send requests to the storage servers */
        proj_id = 0;
        dist = 0;
        nb_proj_send = 0;
        memset(&proj_idx_send, 0, rozofs_forward * sizeof (uint8_t));

        // For each projection server
        for (proj_stor_idx = 0; proj_stor_idx < rozofs_safe; proj_stor_idx++) {

            // Warning: the server can be disconnected
            // but f->storages[ps].rpcclt->client != NULL
            // the disconnection will be detected when the request will be sent
            if (!f->storages[proj_stor_idx]->rpcclt.client || f->storages[proj_stor_idx]->status != 1) {
                proj_id++;
                continue;
            }

            // If (proj_stor_idx >= rozofs_forward) :
            // this means that one or more projections could not be stored
            // on their no-spare storage respective
            if (proj_stor_idx >= rozofs_forward) {

                // Find the first projection not yet sent
                for (j = 0; j < rozofs_forward; j++) {
                    if (proj_idx_send[j] == 0) {
                        proj_id = j;
                        spare = 1;
                        break;
                    }
                }
            } else {
                spare = 0;
            }

            if (sclient_write(f->storages[proj_stor_idx], f->attrs.cid, f->attrs.sids[proj_stor_idx], rozofs_layout, spare, f->attrs.sids, f->fid, bid,
                    nb_blocks, bins[proj_id]) != 0) {
                proj_id++;
                continue;
            }

            dist_set_true(dist, proj_stor_idx);
            proj_idx_send[proj_id] = 1;

            proj_id++;

            if (++nb_proj_send == rozofs_forward)
                break;
        }

        if (nb_proj_send == rozofs_forward) {
            // All data were sent to storage servers
            send = 1;
            break;
        }
        // If file_check_connections don't return 0
        // It's not necessary to retry to write on storage servers
        while (file_get_cnts(f, rozofs_forward, NULL) != 0 && retry++ < f->export->retries);

    } while (retry++ < f->export->retries);

    if (send != 1) {
        // It's necessary to goto out here otherwise
        // we will write something on export server
        errno = EIO;
        goto out;
    }

    if ((length = exportclt_write_block(f->export, f->fid, bid, nb_blocks, dist, off, len)) == -1) {
        // XXX data has already been written on the storage servers
        severe("exportclt_write_block failed: %s", strerror(errno));
        errno = EIO;
        goto out;
    }

out:
    if (bins) {
        for (proj_id = 0; proj_id < rozofs_forward; proj_id++)
            if (bins[proj_id])
                free(bins[proj_id]);
        free(bins);
    }
    if (projections)
        free(projections);

    return length;
}

/** Reads the distributions on the export server,
 *  adjust the read buffer to read only whole data blocks
 *  and uses the function read_blocks to read data
 *
 * @param *f: pointer to the file structure
 * @param off: offset to read from
 * @param *buf: pointer where the data will be stored
 * @param len: length to read
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: the length read on success, -1 otherwise (errno is set)
 */
static int64_t read_buf(file_t * f, uint64_t off, char *buf, uint32_t len,
        uint16_t * last_block_size) {
    int64_t length = -1;
    uint64_t first = 0;
    uint16_t foffset = 0;
    uint64_t last = 0;
    uint16_t loffset = 0;
    dist_t * dist_p = NULL;

    DEBUG_FUNCTION;

    // Sends request to the metadata server
    // to know the size of the file and block data distributions
    if ((dist_p = exportclt_read_block(f->export, f->fid, off, len, &length)) == NULL) {
        severe("exportclt_read_block failed: %s", strerror(errno));
        goto out;
    }

    // Nb. of the first block to read
    first = off / ROZOFS_BSIZE;
    // Offset (in bytes) for the first block
    foffset = off % ROZOFS_BSIZE;
    // Nb. of the last block to read
    last = (off + length) / ROZOFS_BSIZE + ((off + length) % ROZOFS_BSIZE == 0 ? -1 : 0);
    // Offset (in bytes) for the last block
    loffset = (off + length) - last * ROZOFS_BSIZE;

    char * buff_p = NULL;

    if (foffset != 0 || loffset != ROZOFS_BSIZE) {

        // Readjust the buffer
        buff_p = xmalloc(((last - first) + 1) * ROZOFS_BSIZE * sizeof (char));

        // Read blocks
        if (read_blocks(f, first, (last - first) + 1, buff_p, dist_p, last_block_size) != 0) {
            length = -1;
            goto out;
        }

        // Copy blocks read into the buffer
        memcpy(buf, buff_p + foffset, length);

        // Free adjusted buffer
        free(buff_p);

    } else {
        // No need for readjusting the buffer
        buff_p = buf;

        // Read blocks
        if (read_blocks(f, first, (last - first) + 1, buff_p, dist_p, last_block_size) != 0) {
            length = -1;
            goto out;
        }
    }

out:
    if (dist_p)
        free(dist_p);

    return length;
}

/** Send a request to the export server to know the file size
 *  adjust the write buffer to write only whole data blocks,
 *  reads blocks if necessary (incomplete blocks)
 *  and uses the function write_blocks to write data
 *
 * @param *f: pointer to the file structure
 * @param off: offset to write from
 * @param *buf: pointer where the data are be stored
 * @param len: length to write
 *
 * @return: the length written on success, -1 otherwise (errno is set)
 */
static int64_t write_buf(file_t * f, uint64_t off, const char *buf, uint32_t len) {
    int64_t length = -1;
    uint64_t first = 0;
    uint64_t last = 0;
    int fread = 0;
    int lread = 0;
    uint16_t foffset = 0;
    uint16_t loffset = 0;
    uint16_t last_block_buffer_size = 0;
    uint16_t last_block_size_to_write = 0;
    uint16_t last_block_size_read = 0;

    // Get attr just for get size of file
    if (exportclt_getattr(f->export, f->attrs.fid, &f->attrs) != 0) {
        severe("exportclt_getattr failed: %s", strerror(errno));
        goto out;
    }

    length = len;
    // Nb. of the first block to write
    first = off / ROZOFS_BSIZE;
    // Offset (in bytes) for the first block
    foffset = off % ROZOFS_BSIZE;
    // Nb. of the last block to write
    last = (off + length) / ROZOFS_BSIZE + ((off + length) % ROZOFS_BSIZE == 0 ? -1 : 0);
    // Offset (in bytes) for the last block
    loffset = (off + length) - last * ROZOFS_BSIZE;

    // Is it neccesary to read the first block ?
    if (first <= (f->attrs.size / ROZOFS_BSIZE) && foffset != 0)
        fread = 1;

    // Is it necesary to read the last block ?
    if (last < (f->attrs.size / ROZOFS_BSIZE) && loffset != ROZOFS_BSIZE)
        lread = 1;

    // Compute the size of the the last block of buffer to write
    last_block_buffer_size = (((off + len) % ROZOFS_BSIZE) == 0 ? ROZOFS_BSIZE : ((off + len) % ROZOFS_BSIZE));
    // By default the size of the last block to write is the size of last block
    // of buffer
    last_block_size_to_write = last_block_buffer_size;

    // If we must write only one block
    if (first == last) {

        // Reading block if necessary
        if (fread == 1 || lread == 1) {
            char block[ROZOFS_BSIZE];
            memset(block, 0, ROZOFS_BSIZE);

            if (read_buf(f, first * ROZOFS_BSIZE, block, ROZOFS_BSIZE, &last_block_size_read) == -1) {
                length = -1;
                goto out;
            }

            // Copy data to be written in full block
            memcpy(&block[foffset], buf, len);

            last_block_size_to_write = (last_block_buffer_size > last_block_size_read ? last_block_buffer_size : last_block_size_read);

            // Write the full block
            if (write_blocks(f, first, 1, block, off, len, last_block_size_to_write) == -1) {
                length = -1;
                goto out;
            }
        } else {

            // Write the full block
            if (write_blocks(f, first, 1, buf, off, len, last_block_size_to_write) == -1) {
                length = -1;
                goto out;
            }
        }

    } else { // If we must write more than one block

        if (fread || lread) {

            // Readjust the buffer
            char * buff_p = xmalloc(((last - first) + 1) * ROZOFS_BSIZE * sizeof (char));

            // Read the first block if necessary
            if (fread == 1) {
                if (read_buf(f, first * ROZOFS_BSIZE, buff_p, ROZOFS_BSIZE, &last_block_size_read) == -1) {
                    length = -1;
                    goto out;
                }
            }

            // Read the last block if necessary
            if (lread == 1) {
                if (read_buf(f, last * ROZOFS_BSIZE, buff_p + ((last - first) * ROZOFS_BSIZE * sizeof (char)), ROZOFS_BSIZE, &last_block_size_read) == -1) {
                    length = -1;
                    goto out;
                }
                last_block_size_to_write = (last_block_buffer_size > last_block_size_read ? last_block_buffer_size : last_block_size_read);
            }

            // Copy data to be written into the buffer
            memcpy(buff_p + foffset, buf, len);

            // Write complete blocks
            if (write_blocks(f, first, (last - first) + 1, buff_p, off, len, last_block_size_to_write) == -1) {
                length = -1;
                goto out;
            }

            // Free adjusted buffer if necessary
            free(buff_p);

        } else {
            // No need for readjusting the buffer
            // Write complete blocks
            if (write_blocks(f, first, (last - first) + 1, buf, off, len, last_block_size_to_write) == -1) {
                length = -1;
                goto out;
            }
        }
    }

out:

    return length;
}

file_t * file_open(exportclt_t * e, fid_t fid, mode_t mode) {
    file_t *f = 0;
    DEBUG_FUNCTION;

    f = xmalloc(sizeof (file_t));

    uint8_t rozofs_safe = rozofs_get_rozofs_safe(e->layout);
    uint8_t rozofs_forward = rozofs_get_rozofs_forward(e->layout);

    memcpy(f->fid, fid, sizeof (fid_t));
    f->storages = xmalloc(rozofs_safe * sizeof (sclient_t *));
    if (exportclt_getattr(e, fid, &f->attrs) != 0) {
        severe("exportclt_getattr failed: %s", strerror(errno));
        goto error;
    }

    f->buffer = xmalloc(e->bufsize * sizeof (char));

    // Open the file descriptor in the export server
    /* no need anymore
    if (exportclt_open(e, fid) != 0) {
        severe("exportclt_open failed: %s", strerror(errno));
        goto error;
    }
     */

    f->export = e;

    // XXX use the mode because if we open the file in read-only,
    // it is not always necessary to have as many connections
    if (file_get_cnts(f, rozofs_forward, NULL) != 0)
        goto error;

    f->read_from  = 0;
    f->read_pos   = 0;
    f->write_from = 0;
    f->write_pos  = 0;
    f->buf_write_wait = 0;
    f->buf_read_wait = 0;

    goto out;
error:
    if (f) {
        int xerrno = errno;
        free(f);
        errno = xerrno;
    }
    f = 0;
out:

    return f;
}
#if 0 // FDL
int64_t file_write(file_t * f, uint64_t off, const char *buf, uint32_t len) {
    int done = 1;
    int64_t len_write = -1;
    DEBUG_FUNCTION;

    while (done) {

        if (len > (f->export->bufsize - f->buf_pos) ||
                (off != (f->buf_from + f->buf_pos) && f->buf_write_wait != 0)) {

            if ((len_write = write_buf(f, f->buf_from, f->buffer, f->buf_pos)) < 0) {
                goto out;
            }

            f->buf_from = 0;
            f->buf_pos = 0;
            f->buf_write_wait = 0;

        } else {

            memcpy(f->buffer + f->buf_pos, buf, len);

            if (f->buf_write_wait == 0) {
                f->buf_from = off;
                f->buf_write_wait = 1;
            }
            f->buf_pos += len;
            len_write = len;
            done = 0;
        }
    }

out:

    return len_write;
}
#endif

/**
*  FDL new read/write function
*/

int64_t file_write(file_t * f, uint64_t off, const char *buf, uint32_t len) 
{
    int64_t len_write = -1;
    
    rozo_buf_rw_status_t status;

    DEBUG_FUNCTION;
    buf_file_write(&status,f,off,buf,len);
    if (status.status != BUF_STATUS_DONE)
    {
      goto out;
    }
    len_write = (int64_t)len;
out:
    return len_write;
}


int file_flush(file_t * f) {
    int status = -1;
    int64_t length;
    DEBUG_FUNCTION;

    if (f->buf_write_wait != 0) {

        if ((length = write_buf(f, f->write_from, f->buffer, (uint32_t)(f->write_pos -f->write_from))) < 0)
            goto out;

        f->read_from  = 0;
        f->read_pos   = 0;
        f->write_from = 0;
        f->write_pos  = 0;
        f->buf_write_wait = 0;
    }
    status = 0;
out:

    return status;
}

int64_t file_read(file_t * f, uint64_t off, char **buf, uint32_t len) {
    int64_t len_rec = 0;
    int64_t length = 0;
    uint16_t last_block_size = 0;
    DEBUG_FUNCTION;

    if ((off < f->read_from) || (off > f->read_pos) ||
            ((off+len) >  f->read_pos )) {
        /*
        ** Check for pending write
        */
        
        if ((len_rec = read_buf(f, off, f->buffer, f->export->bufsize,
                &last_block_size)) <= 0) {
            length = len_rec;
            goto out;
        }

        length = (len_rec > len) ? len : len_rec;
        *buf = f->buffer;

        f->read_from = off;
        f->read_pos = f->read_from+ len_rec;
        f->buf_read_wait = 1;
    } else {
        length =
                (len <=
                (f->read_pos - off )) ? len : (f->read_pos - off);
        *buf = f->buffer + (off - f->read_from);
    }

out:

    return length;
}

int file_close(exportclt_t * e, file_t * f) {

    if (f) {
        f->read_from  = 0;
        f->read_pos   = 0;
        f->write_from = 0;
        f->write_pos  = 0;
        f->buf_write_wait = 0;
        f->buf_read_wait = 0;
        if (f->buf_write_pending)
        {
           severe("file_close:buf_write_pending is not null %d",f->buf_write_pending);
        }

        // Close the file descriptor in the export server
        /* no need anymore
        if (exportclt_close(e, f->fid) != 0) {
            severe("exportclt_close failed: %s", strerror(errno));
            goto out;
        }
         */

        free(f->storages);
        free(f->buffer);
        free(f);

    }
    return 0;
}






#define CLEAR_WRITE(p) \
{ \
  p->write_pos = 0;\
  p->write_from = 0;\
  p->buf_write_wait = 0;\
}


#define CLEAR_READ(p) \
{ \
  p->read_pos = 0;\
  p->read_from = 0;\
  p->buf_read_wait = 0;\
}

/*
**______________________________________
*/
static inline int read_section_empty(file_t *p)
{
   return (p->read_from == p->read_pos)?1:0;
}
/*
**______________________________________
*/
static inline int write_section_empty(file_t *p)
{
   return (p->write_from == p->write_pos)?1:0;
}

/*
**______________________________________
*/
static inline int check_empty(file_t *p)
{
 
 if (read_section_empty(p)!= 1)
 {
   return 0;
 }
 return write_section_empty(p);
}

/**
*  flush the content of the buffer to the disk
*/
static inline int buf_flush(file_t *p)
{
  int64_t len_write = -1;
  if (p->buf_write_wait == 0) return 0;
  
  uint64_t flush_off = p->write_from;
  uint32_t flush_len = (uint32_t)(p->write_pos - p->write_from);
  uint32_t flush_off_buf = (uint32_t)(p->write_from - p->read_from);
  char *buffer;
  
  buffer = p->buffer+flush_off_buf;
  /*
  ** trigger the write
  */
  if ((len_write = write_buf(p, flush_off, buffer, flush_len)) < 0) {
    return -1;
  }
  return 0;

}
static inline void buf_align_write_from(file_t *p)
{
   uint64_t off2start_w;
   uint64_t off2start;

   off2start_w = (p->write_from/ROZOFS_BSIZE);
   off2start = off2start_w*ROZOFS_BSIZE;
   if ( off2start >= p->read_from)
   {
     p->write_from = off2start;
   }      
}

/*
**______________________________________
*/
void buf_file_write(rozo_buf_rw_status_t *status_p,
                   file_t * p,
                   uint64_t off, 
                   const char *buf, 
                   uint32_t len) 
{

uint64_t off_requested = off;
uint64_t pos_requested = off+len;
int state;
int action;
uint64_t off2end;
uint32_t buf_off;
int ret;
char *dest_p;

/*
** check the start point 
*/
  while(1)
  {
    if (!read_section_empty(p))
    {
      /*
      ** we have either a read or write section, need to check read section first
      */
      if (off_requested >= p->read_from) 
      {
         if (off_requested <= p->read_pos)
         {
           state = BUF_ST_READ_INSIDE;  
           break;   
         } 
         state = BUF_ST_READ_AFTER;
         break;    
      }
      /*
      ** the write start off is out side of the cache buffer
      */
      state = BUF_ST_READ_BEFORE;
      break;
    }
    /*
    ** the buffer is empty (neither read nor write occurs
    */
    state = BUF_ST_EMPTY;
    break;
  }
  /*
  ** ok, now check the end
  */
//  printf("state %s\n",rozofs_buf_read_write_state_e2String(state));
  switch (state)
  {
    case BUF_ST_EMPTY:
      action = BUF_ACT_COPY_EMPTY;
      break;
    case BUF_ST_READ_BEFORE:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;    

    case BUF_ST_READ_INSIDE:
      /*
      ** the start is inside the write buffer check if the len does not exceed the buffer length
      */
      if ((pos_requested - p->read_from) >= p->export->bufsize) 
      {
        if (write_section_empty(p)== 0)
        {        
          action = BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW;
          break;      
        }
      }
      action = BUF_ACT_COPY;
      break;
      
    case BUF_ST_READ_AFTER:
        action = BUF_ACT_FLUSH_THEN_COPY_NEW;
        break;      
  }
//  printf("action %s\n",rozofs_buf_read_write_action_e2String(action));
  /*
  ** OK now perform the required action
  */
  switch (action)
  {
    case BUF_ACT_COPY_EMPTY:
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->buf_write_wait = 1;
      p->read_from = off_requested; 
      p->read_pos  = pos_requested; 
      /*
      ** copy the buffer
      */
      memcpy(p->buffer,buf,len);
      /*
      ** fill the status section
      */
      status_p->status = BUF_STATUS_DONE;
      status_p->errcode = 0;
      break;

    case BUF_ACT_COPY:
      if (write_section_empty(p))
      {
        p->write_from = off_requested; 
        p->write_pos  = pos_requested; 
      }
      else
      {
        if (p->write_from > off_requested) p->write_from = off_requested;      
        if (p->write_pos  < pos_requested) p->write_pos  = pos_requested;      
      }
      buf_align_write_from(p);
      if (p->write_pos > p->read_pos) p->read_pos = p->write_pos;
      else
      {
        /*
        ** attempt to align write_pos on a block boundary
        */
        if (p->write_pos%ROZOFS_BSIZE)
        {
           /*
           ** not on a block boundary
           */
           off2end = (p->read_pos/ROZOFS_BSIZE)*ROZOFS_BSIZE;
           if (off2end > p->write_pos)
           {
             /*
             ** align on block boundary
             */
             p->write_pos = (p->write_pos/ROZOFS_BSIZE)*ROZOFS_BSIZE;           
           }
        }
      }
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      dest_p = p->buffer + (off_requested - p->read_from);
      memcpy(dest_p,buf,len);
      /*
      ** fill the status section
      */
      status_p->status = BUF_STATUS_DONE;
      status_p->errcode = 0;
      break;  
  
    case BUF_ACT_FLUSH_THEN_COPY_NEW:
      /*
      ** flush
      */
      ret = buf_flush(p);
      if (ret < 0)
      {
        status_p->status = BUF_STATUS_FAILURE;
        status_p->errcode = errno;
        break;                  
      }
      p->write_from = off_requested; 
      p->write_pos  = pos_requested; 
      p->read_from = off_requested; 
      p->read_pos  = pos_requested; 
      p->buf_write_wait = 1;
      /*
      ** copy the buffer
      */
      memcpy(p->buffer,buf,len);
      /*
      ** fill the status section
      */
      status_p->status = BUF_STATUS_DONE;
      status_p->errcode = 0;
      break;    
  
    case BUF_ACT_FLUSH_ALIGN_THEN_COPY_NEW:
      /*
      ** partial flush
      */
      if (write_section_empty(p))
      {
        p->write_from = off_requested; 
      }
      else
      {
        if (p->write_from > off_requested) p->write_from = off_requested;      
      }
      buf_align_write_from(p);
      off2end = p->read_from + p->export->bufsize;
      
      if (off2end < p->write_from)
      {
       severe ("Bug!!!! off2start %llu off2end: %llu\n",(long long unsigned int)p->write_from,
                                                        (long long unsigned int)off2end);      
      }
      else
      {
//        printf("offset start %llu len %u\n",p->write_from,(off2end-p->write_from));            
      }
      /*
      ** partial copy of the buffer in the cache
      */
      buf_off = (uint32_t)(off_requested - p->read_from);
      memcpy(p->buffer+buf_off,buf,(size_t)(off2end - off_requested));
            
      p->write_pos  = off2end; 
      ret = buf_flush(p);
      if (ret < 0)
      {
        status_p->status = BUF_STATUS_FAILURE;
        status_p->errcode = errno;
        break;                  
      }      
      /*
      ** copy the buffer
      */
      buf_off = (uint32_t)(p->write_pos - off_requested);
      memcpy(p->buffer,buf+buf_off,(len-buf_off));

      p->write_from = off2end; 
      p->write_pos  = pos_requested; 
      p->read_from = off2end; 
      p->read_pos  = pos_requested; 
      if (write_section_empty(p) == 0) p->buf_write_wait = 1;      
      /*
      ** fill the status section
      */
      status_p->status = BUF_STATUS_DONE;
      status_p->errcode = 0;
      break;      
  
  }

} 

