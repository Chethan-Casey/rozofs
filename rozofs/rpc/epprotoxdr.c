/*
 * Please do not edit this file.
 * It was generated using rpcgen.
 */

#include "epproto.h"
#include <rozofs/rozofs.h>

bool_t
xdr_epp_status_t (XDR *xdrs, epp_status_t *objp)
{
	//register int32_t *buf;

	 if (!xdr_enum (xdrs, (enum_t *) objp))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_epp_status_ret_t (XDR *xdrs, epp_status_ret_t *objp)
{
	//register int32_t *buf;

	 if (!xdr_epp_status_t (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case EPP_FAILURE:
		 if (!xdr_int (xdrs, &objp->epp_status_ret_t_u.error))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

bool_t
xdr_epp_profiler_t (XDR *xdrs, epp_profiler_t *objp)
{
	//register int32_t *buf;

	//int i;
	 if (!xdr_uint64_t (xdrs, &objp->uptime))
		 return FALSE;
	 if (!xdr_uint64_t (xdrs, &objp->now))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->vers, 20,
		sizeof (uint8_t), (xdrproc_t) xdr_uint8_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_mount, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_umount, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_statfs, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_lookup, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_getattr, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_setattr, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_readlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_mknod, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_mkdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_unlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_rmdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_symlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_rename, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_readdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_read_block, 3,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_write_block, 3,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->ep_link, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_lv1_resolve_entry, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_lv2_resolve_path, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_lookup_fid, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_update_files, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_update_blocks, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_stat, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_lookup, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_getattr, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_setattr, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_link, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_mknod, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_mkdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_unlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_rmdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_symlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_readlink, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_rename, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_read, 3,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_read_block, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_write_block, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->export_readdir, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->lv2_cache_put, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->lv2_cache_get, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->lv2_cache_del, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->volume_balance, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->volume_distribute, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->volume_stat, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mdir_open, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mdir_close, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mdir_read_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mdir_write_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_open, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_close, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_read_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_write_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_read_dist, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mreg_write_dist, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_open, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_close, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_read_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_write_attributes, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_read_link, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	 if (!xdr_vector (xdrs, (char *)objp->mslnk_write_link, 2,
		sizeof (uint64_t), (xdrproc_t) xdr_uint64_t))
		 return FALSE;
	return TRUE;
}

bool_t
xdr_epp_profiler_ret_t (XDR *xdrs, epp_profiler_ret_t *objp)
{
	//register int32_t *buf;

	 if (!xdr_epp_status_t (xdrs, &objp->status))
		 return FALSE;
	switch (objp->status) {
	case EPP_SUCCESS:
		 if (!xdr_epp_profiler_t (xdrs, &objp->epp_profiler_ret_t_u.profiler))
			 return FALSE;
		break;
	case EPP_FAILURE:
		 if (!xdr_int (xdrs, &objp->epp_profiler_ret_t_u.error))
			 return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}
