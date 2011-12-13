/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

#include "mpidi_ch3_impl.h"
#include "mpiutil.h"
#include <stdio.h>

#ifdef DAPL_DEFAULT_PROVIDER
#include "rdma_impl.h"
extern MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
extern struct smpi_var g_smpi;
extern int od_server_thread;
extern int cached_pg_size;
#endif

#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_read_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_read_progress(MPIDI_VC_t ** vc_pptr, vbuf ** v_ptr, int *rdmafp_found, int is_blocking)
{
    static MPIDI_VC_t 	*pending_vc = NULL;
    int 	type;
    MPIDI_VC_t 	*recv_vc_ptr;
#ifdef DAPL_DEFAULT_PROVIDER
    static int 		local_vc_index = 0;
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_READ_PROGRESS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_READ_PROGRESS);

#ifdef DAPL_DEFAULT_PROVIDER
    if (od_server_thread &&
        MPIDI_CH3I_Process.num_conn >= cached_pg_size - g_smpi.num_local_nodes) {
	int ret;
        ret = pthread_cancel(MPIDI_CH3I_RDMA_Process.server_thread);
	MPIU_Assert(ret == 0);

        ret = pthread_join(MPIDI_CH3I_RDMA_Process.server_thread, NULL);
	MPIU_Assert(ret == 0);

        od_server_thread = 0;
    }
#endif

    *vc_pptr = NULL;
    *v_ptr = NULL;

    if (pending_vc != NULL) {
        type = MPIDI_CH3I_MRAILI_Waiting_msg(pending_vc, v_ptr, 1);
        if (type == T_CHANNEL_CONTROL_MSG_ARRIVE) {
            if((void *) pending_vc != (*v_ptr)->vc) {
                fprintf(stderr, "mismatch %p %p\n", pending_vc,
                        (*v_ptr)->vc);
            }
            MPIU_Assert((void *) pending_vc == (*v_ptr)->vc);
            *vc_pptr = pending_vc;
        } else if(type == T_CHANNEL_EXACT_ARRIVE) {
            *vc_pptr = pending_vc;
            pending_vc = NULL;
            DEBUG_PRINT("will return seqnum %d\n",
                        ((MPIDI_CH3_Pkt_rndv_req_to_send_t *) (*v_ptr)->
                         pheader)->seqnum);
        } else if (type == T_CHANNEL_OUT_OF_ORDER_ARRIVE) {
            /* Reqd pkt has not arrived yet. Poll CQ for it */
        } else {
            /* T_CHANNEL_NO_ARRIVE - no packets left  in queue */
        }
        goto fn_exit;
    }

#ifdef DAPL_DEFAULT_PROVIDER
  int i;
  MPIDI_PG_t 	*pg = MPIDI_Process.my_pg;
  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    for (i = 0; i < pg->size; i++) {
        MPIDI_PG_Get_vc(MPIDI_Process.my_pg, local_vc_index,
                         &recv_vc_ptr);
        /* skip over the vc to myself */
        if (MPIDI_CH3I_Process.vc == recv_vc_ptr) {
            if (++local_vc_index == pg->size)
                local_vc_index = 0;
            continue;
        }
        type =
            MPIDI_CH3I_MRAILI_Get_next_vbuf_local(recv_vc_ptr, v_ptr);
        if (++local_vc_index == pg->size)
            local_vc_index = 0;
        if (type != T_CHANNEL_NO_ARRIVE) {
            *vc_pptr = recv_vc_ptr;
            DEBUG_PRINT("[read_progress] find one\n");
            goto fn_exit;
        }
    }
  } else {
       type = MPIDI_CH3I_MRAILI_Get_next_vbuf(vc_pptr, v_ptr);
       if (type != T_CHANNEL_NO_ARRIVE) {
            goto fn_exit;
       }
  }
#else
    type = MPIDI_CH3I_MRAILI_Get_next_vbuf(vc_pptr, v_ptr);
    if (type != T_CHANNEL_NO_ARRIVE) {
        if (rdmafp_found != NULL ) {
            *rdmafp_found = 1;
        }
	    goto fn_exit;
    } 
#endif

    /* local polling has finished, now we need to start global subchannel polling 
     * For convenience, at this stage, we by default refer to the global polling channel 
     * as the send recv channel on each of the queue pair
     * TODO: we may extend this interface to support other format of channel polling */
    /* Interface cq_poll requires that if *v_ptr is exactly the next packet 
     * don't enqueue * v_ptr. 
     * if *v_ptr is not initialized as NULL, Cq_poll will return exactly only the
     * packet on *v_ptr
     * TODO: Is this the optimal way?
     */
    type = MPIDI_CH3I_MRAILI_Cq_poll(v_ptr, NULL, 0, is_blocking);
    if (type != T_CHANNEL_NO_ARRIVE) {
        recv_vc_ptr = (*v_ptr)->vc;
        *vc_pptr = recv_vc_ptr;
        switch (type) {
        case (T_CHANNEL_EXACT_ARRIVE):
            DEBUG_PRINT("Get one packet with exact seq num\n");
            break;
        case (T_CHANNEL_OUT_OF_ORDER_ARRIVE):
            /* It is possible that *v_ptr points to a vbuf that contains later pkts send/recv 
             * may return vbuf from any connection */
            DEBUG_PRINT("get out of order progress seqnum %d, expect %d\n",
                        ((MPIDI_CH3_Pkt_rndv_req_to_send_t *) *
                         v_ptr)->seqnum, recv_vc_ptr->seqnum_recv);

            type =
                MPIDI_CH3I_MRAILI_Waiting_msg(recv_vc_ptr, v_ptr, 1);
            if (type == T_CHANNEL_CONTROL_MSG_ARRIVE) {
                pending_vc = recv_vc_ptr;
            } else if (T_CHANNEL_EXACT_ARRIVE == type) {
		DEBUG_PRINT("Get out of order delivered msg\n");
            } else {
                fprintf(stderr, "Error recving run return type\n");
                exit(1);
            }
            break;
        case (T_CHANNEL_CONTROL_MSG_ARRIVE):
            DEBUG_PRINT("Get one control msg\n");
            break;
        default:
            /* Error here */
            break;
        }
        goto fn_exit;
    } 
  fn_exit:
    return MPI_SUCCESS;
}

/* non-blocking functions */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_post_read
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_post_read(MPIDI_VC_t * vc, void *buf, int len)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST_READ);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST_READ);
    MPIDI_DBG_PRINTF((60, FCNAME, "entering"));
    vc->ch.read.total = 0;
    vc->ch.read.buffer = buf;
    vc->ch.read.bufflen = len;
    vc->ch.read.use_iov = FALSE;
    vc->ch.read_state = MPIDI_CH3I_READ_STATE_READING;
#ifdef USE_RDMA_UNEX
    if (vc->ch.unex_list)
        shmi_read_unex(vc);
#endif
    MPIU_DBG_PRINTF(("post_read: len = %d\n", len));
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST_READ);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_post_readv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_post_readv(MPIDI_VC_t * vc, MPID_IOV * iov, int n)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST_READV);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST_READV);
    MPIDI_DBG_PRINTF((60, FCNAME, "entering"));
    /* strip any trailing empty buffers */
    while (n && iov[n - 1].MPID_IOV_LEN == 0)
        n--;
    vc->ch.read.total = 0;
    vc->ch.read.iov = iov;
    vc->ch.read.iovlen = n;
    vc->ch.read.index = 0;
    vc->ch.read.use_iov = TRUE;
    vc->ch.read_state = MPIDI_CH3I_READ_STATE_READING;
#ifdef USE_RDMA_UNEX
    if (vc->ch.unex_list)
        shmi_readv_unex(vc);
#endif
#ifdef MPICH_DBG_OUTPUT
    while (n) {
        MPIU_DBG_PRINTF(("post_readv: iov[%d].len = %d\n", n - 1,
                         iov[n - 1].MPID_IOV_LEN));
        n--;
    }
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST_READV);
    return MPI_SUCCESS;
}
