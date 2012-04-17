/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "../src/sproto.h"
#include "rozofs.h"

bool_t xdr_sp_uuid_t(XDR * xdrs, sp_uuid_t objp) {
    //register int32_t *buf;

    if (!xdr_vector
        (xdrs, (char *) objp, ROZOFS_UUID_SIZE, sizeof (u_char),
         (xdrproc_t) xdr_u_char))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_status_t(XDR * xdrs, sp_status_t * objp) {
    //register int32_t *buf;

    if (!xdr_enum(xdrs, (enum_t *) objp))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_status_ret_t(XDR * xdrs, sp_status_ret_t * objp) {
    //register int32_t *buf;

    if (!xdr_sp_status_t(xdrs, &objp->status))
        return FALSE;
    switch (objp->status) {
    case SP_FAILURE:
        if (!xdr_int(xdrs, &objp->sp_status_ret_t_u.error))
            return FALSE;
        break;
    default:
        break;
    }
    return TRUE;
}

bool_t xdr_sp_remove_arg_t(XDR * xdrs, sp_remove_arg_t * objp) {
    //register int32_t *buf;

    if (!xdr_uint16_t(xdrs, &objp->sid))
        return FALSE;
    if (!xdr_sp_uuid_t(xdrs, objp->fid))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_write_arg_t(XDR * xdrs, sp_write_arg_t * objp) {
    //register int32_t *buf;

    if (!xdr_uint16_t(xdrs, &objp->sid))
        return FALSE;
    if (!xdr_sp_uuid_t(xdrs, objp->fid))
        return FALSE;
    if (!xdr_uint8_t(xdrs, &objp->tid))
        return FALSE;
    if (!xdr_uint64_t(xdrs, &objp->bid))
        return FALSE;
    if (!xdr_uint32_t(xdrs, &objp->nrb))
        return FALSE;
    if (!xdr_bytes
        (xdrs, (char **) &objp->bins.bins_val,
         (u_int *) & objp->bins.bins_len, ~0))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_read_arg_t(XDR * xdrs, sp_read_arg_t * objp) {
    //register int32_t *buf;

    if (!xdr_uint16_t(xdrs, &objp->sid))
        return FALSE;
    if (!xdr_sp_uuid_t(xdrs, objp->fid))
        return FALSE;
    if (!xdr_uint8_t(xdrs, &objp->tid))
        return FALSE;
    if (!xdr_uint64_t(xdrs, &objp->bid))
        return FALSE;
    if (!xdr_uint32_t(xdrs, &objp->nrb))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_truncate_arg_t(XDR * xdrs, sp_truncate_arg_t * objp) {
    //register int32_t *buf;

    if (!xdr_uint16_t(xdrs, &objp->sid))
        return FALSE;
    if (!xdr_sp_uuid_t(xdrs, objp->fid))
        return FALSE;
    if (!xdr_uint8_t(xdrs, &objp->tid))
        return FALSE;
    if (!xdr_uint64_t(xdrs, &objp->bid))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_read_ret_t(XDR * xdrs, sp_read_ret_t * objp) {
    //register int32_t *buf;

    if (!xdr_sp_status_t(xdrs, &objp->status))
        return FALSE;
    switch (objp->status) {
    case SP_SUCCESS:
        if (!xdr_bytes
            (xdrs, (char **) &objp->sp_read_ret_t_u.bins.bins_val,
             (u_int *) & objp->sp_read_ret_t_u.bins.bins_len, ~0))
            return FALSE;
        break;
    case SP_FAILURE:
        if (!xdr_int(xdrs, &objp->sp_read_ret_t_u.error))
            return FALSE;
        break;
    default:
        break;
    }
    return TRUE;
}

bool_t xdr_sp_sstat_t(XDR * xdrs, sp_sstat_t * objp) {
    //register int32_t *buf;

    if (!xdr_uint64_t(xdrs, &objp->size))
        return FALSE;
    if (!xdr_uint64_t(xdrs, &objp->free))
        return FALSE;
    return TRUE;
}

bool_t xdr_sp_stat_ret_t(XDR * xdrs, sp_stat_ret_t * objp) {
    //register int32_t *buf;

    if (!xdr_sp_status_t(xdrs, &objp->status))
        return FALSE;
    switch (objp->status) {
    case SP_SUCCESS:
        if (!xdr_sp_sstat_t(xdrs, &objp->sp_stat_ret_t_u.sstat))
            return FALSE;
        break;
    case SP_FAILURE:
        if (!xdr_int(xdrs, &objp->sp_stat_ret_t_u.error))
            return FALSE;
        break;
    default:
        break;
    }
    return TRUE;
}
