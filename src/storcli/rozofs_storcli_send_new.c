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

#define _XOPEN_SOURCE 500
#define FUSE_USE_VERSION 26

#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <assert.h>

#include <config.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/rpc/stcpproto.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/rozofs_tx_api.h>
#include "rozofs_storcli.h"
#include "rozofs_storcli_rpc.h"

DECLARE_PROFILING(stcpp_profiler_t);

/**
* API for creation a transaction towards an exportd

 The reference of the north load balancing is extracted for the client structure
 fuse_ctx_p:
 That API needs the pointer to the current fuse context. That nformation will be
 saved in the transaction context as userParam. It is intended to be used later when
 the client gets the response from the server
 encoding function;
 For making that API generic, the caller is intended to provide the function that
 will encode the message in XDR format. The source message that is encoded is 
 supposed to be pointed by msg2encode_p.
 Since the service is non-blocking, the caller MUST provide the callback function 
 that will be used for decoding the message
 

 @param lbg_id     : reference of the load balancing group
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @msg2encode_p     : pointer to the message to encode
 @param xmit_buf : pointer to the buffer to send, in case of error that function release the buffer
 @param seqnum     : sequence number associated with the context (store as an opaque parameter in the transaction context
 @param opaque_value_idx1 : opaque value at index 1
 @param extra_len  : extra length to add after encoding RPC (must be 4 bytes aligned !!!)
 @param recv_cbk   : receive callback function

 @param user_ctx_p : pointer to the working context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int rozofs_sorcli_send_rq_common(uint32_t lbg_id,uint32_t prog,uint32_t vers,
                              int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                              void *xmit_buf,
                              uint32_t seqnum,
                              uint32_t opaque_value_idx1,  
                              int      extra_len,                            
                              sys_recv_pf_t recv_cbk,void *user_ctx_p) 
{
    DEBUG_FUNCTION;
   
    uint8_t           *arg_p;
    uint32_t          *header_size_p;
    rozofs_tx_ctx_t   *rozofs_tx_ctx_p = NULL;
    int               bufsize;
    int               ret;
    int               position;
    XDR               xdrs;    
	struct rpc_msg   call_msg;
    uint32_t         null_val = 0;

    /*
    ** allocate a transaction context
    */
    rozofs_tx_ctx_p = rozofs_tx_alloc();  
    if (rozofs_tx_ctx_p == NULL) 
    {
       /*
       ** out of context
       ** --> put a pending list for the future to avoid repluing ENOMEM
       */
       TX_STATS(ROZOFS_TX_NO_CTX_ERROR);
       errno = ENOMEM;
       goto error;
    }    
    /*
    ** store the reference of the xmit buffer in the transaction context: might be useful
    ** in case we want to remove it from a transmit list of the underlying network stacks
    */
    rozofs_tx_save_xmitBuf(rozofs_tx_ctx_p,xmit_buf);
    /*
    ** get the pointer to the payload of the buffer
    */
    header_size_p  = (uint32_t*) ruc_buf_getPayload(xmit_buf);
    arg_p = (uint8_t*)(header_size_p+1);  
    /*
    ** create the xdr_mem structure for encoding the message
    */
    bufsize = (int)ruc_buf_getMaxPayloadLen(xmit_buf);
    xdrmem_create(&xdrs,(char*)arg_p,bufsize,XDR_ENCODE);
    /*
    ** fill in the rpc header
    */
    call_msg.rm_direction = CALL;
    /*
    ** allocate a xid for the transaction 
    */
	call_msg.rm_xid             = rozofs_tx_alloc_xid(rozofs_tx_ctx_p); 
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	/* XXX: prog and vers have been long historically :-( */
	call_msg.rm_call.cb_prog = (uint32_t)prog;
	call_msg.rm_call.cb_vers = (uint32_t)vers;
	if (! xdr_callhdr(&xdrs, &call_msg))
    {
       /*
       ** THIS MUST NOT HAPPEN
       */
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;	
    }
    /*
    ** insert the procedure number, NULL credential and verifier
    */
    XDR_PUTINT32(&xdrs, (int32_t *)&opcode);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
        
    /*
    ** ok now call the procedure to encode the message
    */
    if ((*encode_fct)(&xdrs,msg2encode_p) == FALSE)
    {
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;
    }
    /*
    ** Now get the current length and fill the header of the message
    */
    position = XDR_GETPOS(&xdrs);
    /*
    ** add the extra_len if any
    */
    position +=extra_len;
    /*
    ** update the length of the message : must be in network order
    */
    *header_size_p = htonl(0x80000000 | position);
    /*
    ** set the payload length in the xmit buffer
    */
    int total_len = sizeof(*header_size_p)+ position;
    ruc_buf_setPayloadLen(xmit_buf,total_len);
    /*
    ** store the receive call back and its associated parameter
    */
    rozofs_tx_ctx_p->recv_cbk   = recv_cbk;
    rozofs_tx_ctx_p->user_param = user_ctx_p;    
    /*
    ** store the sequence number in one of the opaque user data array of the transaction
    */
    rozofs_tx_write_opaque_data( rozofs_tx_ctx_p,0,seqnum);  
    rozofs_tx_write_opaque_data( rozofs_tx_ctx_p,1,opaque_value_idx1);  
    /*
    ** now send the message
    */
#ifndef TEST_STORCLI_TEST
    ret = north_lbg_send(lbg_id,xmit_buf);
#else
    ret = test_north_lbg_send(lbg_id,xmit_buf);
#endif
    if (ret < 0)
    {
       TX_STATS(ROZOFS_TX_SEND_ERROR);
       errno = EFAULT;
      goto error;  
    }
    TX_STATS(ROZOFS_TX_SEND);

    /*
    ** OK, so now finish by starting the guard timer
    */
    rozofs_tx_start_timer(rozofs_tx_ctx_p,10);  
    return 0;  
    
  error:
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);
    return -1;    
}




/*
**__________________________________________________________________________
*/
/**
* send a success read reply
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  
  @retval none

*/

void rozofs_storcli_read_reply_success(rozofs_storcli_ctx_t *p)
{
   int ret;
   uint8_t *pbuf;           /* pointer to the part that follows the header length */
   uint32_t *header_len_p;  /* pointer to the array that contains the length of the rpc message*/
   XDR xdrs;
   int len;
   storcli_status_t status = STORCLI_SUCCESS;
   int data_len;
   uint8_t eof_flag = 0;
   
    /*
    ** create xdr structure on top of the buffer that will be used for sending the response
    */
    header_len_p = (uint32_t*)ruc_buf_getPayload(p->xmitBuf); 
    pbuf = (uint8_t*) (header_len_p+1);            
    len = (int)ruc_buf_getMaxPayloadLen(p->xmitBuf);
    len -= sizeof(uint32_t);
    xdrmem_create(&xdrs,(char*)pbuf,len,XDR_ENCODE); 
    if (rozofs_encode_rpc_reply(&xdrs,(xdrproc_t)xdr_storcli_status_t,(caddr_t)&status,p->src_transaction_id) != TRUE)
    {
      severe("rpc reply encoding error");
      goto error;     
    }
    /*
    ** Encode the length of returned data
    */


    data_len = rozofs_storcli_transform_get_read_len_in_bytes(p->block_ctx_table,
                                                              p->effective_number_of_blocks,
                                                              &eof_flag);
    STORCLI_STOP_NORTH_PROF(p,read,data_len);

    XDR_PUTINT32(&xdrs, (int32_t *)&data_len);
    /*
    ** round up data_len to 4 bytes alignment
    */
    if ((data_len%4)!= 0) data_len = (data_len &(~0x3))+4;
       
    /*
    ** compute the total length of the message for the rpc header and add 4 bytes more bytes for
    ** the ruc buffer to take care of the header length of the rpc message.
    */
    int total_len = xdr_getpos(&xdrs)+data_len ;
    *header_len_p = htonl(0x80000000 | total_len);
    total_len +=sizeof(uint32_t);

    ruc_buf_setPayloadLen(p->xmitBuf,total_len);
    /*
    ** Clear the reference of the seqnum to prevent any late response to be processed
    ** by setting seqnum to 0 any late response is ignored and the associated ressources
    ** will released (buffer associated with the response). This typically permits to
    ** avoid sending again the response while this has already been done
    */
    p->read_seqnum = 0;
    /*
    ** Get the callback for sending back the response:
    ** A callback is needed since the request for read might be local or remote
    */
    ret = (*p->response_cbk)(p->xmitBuf,p->socketRef,p->user_param);
    if (ret == 0)
    {
      /**
      * success so remove the reference of the xmit buffer since it is up to the called
      * function to release it
      */
      p->xmitBuf = NULL;
    }
    
error:
//    #warning need to consider the case of a local read triggers by a write request. Without a guard time the write working can be lost!!
    return;
} 
/*
**__________________________________________________________________________
*/
/**
* send a error read reply
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  @param error : error code
  
  @retval none

*/
void rozofs_storcli_read_reply_error(rozofs_storcli_ctx_t *p,int error)
{
   int ret;
   uint8_t *pbuf;           /* pointer to the part that follows the header length */
   uint32_t *header_len_p;  /* pointer to the array that contains the length of the rpc message*/
   XDR xdrs;
   int len;
   storcli_status_ret_t status;

   status.status = STORCLI_FAILURE;
   status.storcli_status_ret_t_u.error = error;
   
    /*
    ** create xdr structure on top of the buffer that will be used for sending the response
    */
    header_len_p = (uint32_t*)ruc_buf_getPayload(p->xmitBuf); 
    pbuf = (uint8_t*) (header_len_p+1);            
    len = (int)ruc_buf_getMaxPayloadLen(p->xmitBuf);
    len -= sizeof(uint32_t);
    xdrmem_create(&xdrs,(char*)pbuf,len,XDR_ENCODE); 
    if (rozofs_encode_rpc_reply(&xdrs,(xdrproc_t)xdr_sp_status_ret_t,(caddr_t)&status,p->src_transaction_id) != TRUE)
    {
      severe("rpc reply encoding error");
      goto error;     
    }       
    /*
    ** compute the total length of the message for the rpc header and add 4 bytes more bytes for
    ** the ruc buffer to take care of the header length of the rpc message.
    */
    int total_len = xdr_getpos(&xdrs) ;
    *header_len_p = htonl(0x80000000 | total_len);
    total_len +=sizeof(uint32_t);
    ruc_buf_setPayloadLen(p->xmitBuf,total_len);
    /*
    ** Clear the reference of the seqnum to prevent any late response to be processed
    ** by setting seqnum to 0 any late response is ignored and the associated ressources
    ** will released (buffer associated with the response). This typically permits to
    ** avoid sending again the response while this has already been done
    */
    p->read_seqnum = 0;
    /*
    ** Get the callback for sending back the response:
    ** A callback is needed since the request for read might be local or remote
    */
    ret = (*p->response_cbk)(p->xmitBuf,p->socketRef,p->user_param);
    if (ret == 0)
    {
      /**
      * success so remove the reference of the xmit buffer since it is up to the called
      * function to release it
      */
      p->xmitBuf = NULL;
    }
    
error:
    return;
}


/*
**__________________________________________________________________________
*/
/**
* send a write success reply
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  
  @retval none

*/
void rozofs_storcli_write_reply_success(rozofs_storcli_ctx_t *p)
{
   int ret;
   uint8_t *pbuf;           /* pointer to the part that follows the header length */
   uint32_t *header_len_p;  /* pointer to the array that contains the length of the rpc message*/
   XDR xdrs;
   int len;
   storcli_status_ret_t status;
   storcli_write_arg_no_data_t *storcli_write_rq_p = NULL;

   status.status = STORCLI_SUCCESS;
   status.storcli_status_ret_t_u.error = 0;  /* NS */

    storcli_write_rq_p = (storcli_write_arg_no_data_t*)&p->storcli_write_arg;
    STORCLI_STOP_NORTH_PROF(p,write,storcli_write_rq_p->len);   
    /*
    ** create xdr structure on top of the buffer that will be used for sending the response
    */
    header_len_p = (uint32_t*)ruc_buf_getPayload(p->xmitBuf); 
    pbuf = (uint8_t*) (header_len_p+1);            
    len = (int)ruc_buf_getMaxPayloadLen(p->xmitBuf);
    len -= sizeof(uint32_t);
    xdrmem_create(&xdrs,(char*)pbuf,len,XDR_ENCODE); 
    if (rozofs_encode_rpc_reply(&xdrs,(xdrproc_t)xdr_sp_status_ret_t,(caddr_t)&status,p->src_transaction_id) != TRUE)
    {
      severe("rpc reply encoding error");
      goto error;     
    }       
    /*
    ** compute the total length of the message for the rpc header and add 4 bytes more bytes for
    ** the ruc buffer to take care of the header length of the rpc message.
    */
    int total_len = xdr_getpos(&xdrs) ;
    *header_len_p = htonl(0x80000000 | total_len);
    total_len +=sizeof(uint32_t);
    ruc_buf_setPayloadLen(p->xmitBuf,total_len);
    /*
    ** Clear the reference of the seqnum to prevent any late response to be processed
    ** by setting seqnum to 0 any late response is ignored and the associated ressources
    ** will released (buffer associated with the response). This typically permits to
    ** avoid sending again the response while this has already been done
    */
    p->read_seqnum = 0;
    /*
    ** Get the callback for sending back the response:
    ** A callback is needed since the request for read might be local or remote
    */
    ret = (*p->response_cbk)(p->xmitBuf,p->socketRef,p->user_param);
    if (ret == 0)
    {
      /**
      * success so remove the reference of the xmit buffer since it is up to the called
      * function to release it
      */
      p->xmitBuf = NULL;
    }
    
error:
    return;
} 


/*
**__________________________________________________________________________
*/
/**
* send a write reply error
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  @param error : error code
  
  @retval none

*/
void rozofs_storcli_write_reply_error(rozofs_storcli_ctx_t *p,int error)
{
   return rozofs_storcli_read_reply_error(p,error);
}


/*
**__________________________________________________________________________
*/
/**
* send a error read reply by using the receiver buffer
 
  @param socket_ctx_idx: index of the TCP connection
  @param recv_buf: pointer to the ruc_buffer that contains the message
  @param rozofs_storcli_remote_rsp_cbk: callback for sending out the response
  @param user_param : pointer to a user opaque parameter (non significant for a remote access)
  @param error : error code
  
  @retval none

*/
void rozofs_storcli_reply_error_with_recv_buf(uint32_t  socket_ctx_idx,
                                              void *recv_buf,
                                              void *user_param,
                                              rozofs_storcli_resp_pf_t rozofs_storcli_remote_rsp_cbk,
                                              int error)
{


   rozofs_rpc_call_hdr_with_sz_t    *com_hdr_p;
   uint32_t  msg_len;  /* length of the rpc messsage including the header length */
   rozofs_rpc_call_hdr_t   hdr;   /* structure that contains the rpc header in host format */
   
   int ret;
   uint8_t *pbuf;           /* pointer to the part that follows the header length */
   uint32_t *header_len_p;  /* pointer to the array that contains the length of the rpc message*/
   XDR xdrs;
   int len;
   storcli_status_ret_t status;

   status.status = STORCLI_FAILURE;
   status.storcli_status_ret_t_u.error = error;
   /*
   ** Get the full length of the message and adjust it the the length of the applicative part (RPC header+application msg)
   */
   msg_len = ruc_buf_getPayloadLen(recv_buf);
   msg_len -=sizeof(uint32_t);
   
   /*
   ** Get the payload of the receive buffer and set the pointer to array that describes the read request
   */
   com_hdr_p  = (rozofs_rpc_call_hdr_with_sz_t*) ruc_buf_getPayload(recv_buf);  
   memcpy(&hdr,&com_hdr_p->hdr,sizeof(rozofs_rpc_call_hdr_t));
   /*
   ** swap the rpc header
   */
   scv_call_hdr_ntoh(&hdr);   
    /*
    ** create xdr structure on top of the buffer that will be used for sending the response
    */
    header_len_p = (uint32_t*)ruc_buf_getPayload(recv_buf); 
    pbuf = (uint8_t*) (header_len_p+1);            
    len = (int)ruc_buf_getMaxPayloadLen(recv_buf);
    len -= sizeof(uint32_t);
    xdrmem_create(&xdrs,(char*)pbuf,len,XDR_ENCODE); 
    if (rozofs_encode_rpc_reply(&xdrs,(xdrproc_t)xdr_sp_status_ret_t,(caddr_t)&status,hdr.hdr.xid) != TRUE)
    {
      severe("rpc reply encoding error");
      goto error;     
    }       
    /*
    ** compute the total length of the message for the rpc header and add 4 bytes more bytes for
    ** the ruc buffer to take care of the header length of the rpc message.
    */
    int total_len = xdr_getpos(&xdrs) ;
    *header_len_p = htonl(0x80000000 | total_len);
    total_len +=sizeof(uint32_t);
    ruc_buf_setPayloadLen(recv_buf,total_len);
    /*
    ** Get the callback for sending back the response:
    ** A callback is needed since the request for read might be local or remote
    */
    ret = (*rozofs_storcli_remote_rsp_cbk)(recv_buf,socket_ctx_idx,user_param);
    if (ret == 0)
    {
      return;
    }
    
error:
    ruc_buf_freeBuffer(recv_buf);
    return;
}


