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

#ifndef MPIDI_CH3_RDMA_POST_H
#define MPIDI_CH3_RDMA_POST_H

#include "mpidi_ch3i_rdma_conf.h"
#include "vbuf.h"

/* Use this header to add implementation specific structures
   that cannot be defined until after the mpich2 header files
   have been included.
*/

#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)

/* structure MPIDI_CH3I_RDMA_put_get_list is the queue pool to record every
 * issued signaled RDMA write and RDMA read operation. The address of
 * the entries are assigned to the id field of the descriptors when they
 * are posted. So it will be easy to find the corresponding operators of
 * the RDMA operations when a completion queue entry is polled.
 */

struct MPIDI_CH3I_RDMA_put_get_list_t
{
    int op_type;
    /* op_type, SIGNAL_FOR_PUT or
     * SIGNAL_FOR_GET */
    int data_size;
    dreg_entry *mem_entry;      /* mem region registered on the fly */
    void *target_addr;          /* get use only */
    void *origin_addr;          /* get use only, tmp buffer for small msg,
                                 * NULL if big msg, do need to do mem cpy
                                 */
    MPID_Win *win_ptr;
    struct MPIDI_VC *vc_ptr;
};

#define Calculate_IOV_len(iov, n_iov, len) \
{   int i; (len) = 0;                         \
    for (i = 0; i < (n_iov); i ++) {          \
        (len) += (iov)[i].MPID_IOV_LEN;         \
    }                                       \
}

#define MPIDI_CH3I_MRAILI_RREQ_RNDV_FINISH(rreq) \
{       \
    if (rreq != NULL) {         \
        if (rreq->mrail.d_entry != NULL) {               \
            dreg_unregister(rreq->mrail.d_entry);           \
            rreq->mrail.d_entry = NULL;                     \
        }       \
        if (1 == rreq->mrail.rndv_buf_alloc \
            && rreq->mrail.rndv_buf != NULL) { \
            MPIU_Free(rreq->mrail.rndv_buf);                    \
            rreq->mrail.rndv_buf = NULL;                        \
            rreq->mrail.rndv_buf_off = rreq->mrail.rndv_buf_sz = 0; \
            rreq->mrail.rndv_buf_alloc = 0;       \
        }  else { \
            rreq->mrail.rndv_buf_off = rreq->mrail.rndv_buf_sz = 0; \
        } \
        rreq->mrail.d_entry = NULL;                         \
	rreq->mrail.protocol = VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED; \
    }   \
}

#define PUSH_FLOWLIST(c) {                                          \
    if (0 == c->mrail.inflow) {                                           \
        c->mrail.inflow = 1;                                              \
        c->mrail.nextflow = flowlist;                                     \
        flowlist = c;                                               \
    }                                                               \
}

#define POP_FLOWLIST() {                                            \
    if (flowlist != NULL) {                                         \
        MPIDI_VC_t *_c;                                    \
        _c = flowlist;                                              \
        flowlist = _c->mrail.nextflow;                                    \
        _c->mrail.inflow = 0;                                             \
        _c->mrail.nextflow = NULL;                                        \
    }                                                               \
}

/*
 * Attached to each connection is a list of send handles that
 * represent rendezvous sends that have been started and acked but not
 * finished. When the ack is received, the send is placed on the list;
 * when the send is complete, it is removed from the list.  The list
 * is an "in progress sends" queue for this connection.  We need it to
 * remember what sends are pending and to remember the order sends
 * were acked so that we complete them in that order. This
 * prevents a situation where we receive an ack for message 1, block
 * because of flow control, receive an ack for message 2, and are able
 * to complete message 2 based on the new piggybacked credits.
 *
 * The list head and tail are given by the shandle_head and
 * shandle_tail entries on viadev_connection_t and the list is linked
 * through the nexthandle entry on a send handle.
 *
 * The queue is FIFO because we must preserve order, so we maintain
 * both a head and a tail.
 *
 */
#define RENDEZVOUS_IN_PROGRESS(c, s) {                              \
    if (NULL == (c)->mrail.sreq_tail) {                               \
        (c)->mrail.sreq_head = (void *)(s);                                     \
    } else {                                                        \
        ((MPID_Request *)(c)->mrail.sreq_tail)->mrail.next_inflow = (void *)(s);  \
    }                                                               \
    (c)->mrail.sreq_tail = (void *)(s);                                         \
    ((MPID_Request *)(s))->mrail.next_inflow = NULL; \
}

#define RENDEZVOUS_DONE(c) {                                        \
    (c)->mrail.sreq_head = ((MPID_Request *)(c)->mrail.sreq_head)->mrail.next_inflow; \
        if (NULL == (c)->mrail.sreq_head) {                              \
            (c)->mrail.sreq_tail = NULL;                                 \
        }                                                           \
}

#define MPIDI_CH3I_MRAIL_REVERT_RPUT(_sreq)                     \
{                                                               \
    if (VAPI_PROTOCOL_RGET == (_sreq)->mrail.protocol)          \
        (_sreq)->mrail.protocol = VAPI_PROTOCOL_RPUT;           \
} 

#define MPIDI_CH3I_MRAIL_SET_PKT_RNDV(_pkt, _req) \
{   \
    (_pkt)->rndv.protocol = (_req)->mrail.protocol;  \
    if (VAPI_PROTOCOL_RPUT == (_pkt)->rndv.protocol){   \
        (_pkt)->rndv.memhandle = ((_req)->mrail.d_entry)->memhandle; \
        (_pkt)->rndv.buf_addr = (_req)->mrail.rndv_buf;  \
    }   \
}

#define MPIDI_CH3I_MRAIL_SET_REMOTE_RNDV_INFO(_rndv,_req)  \
{   \
    (_rndv)->protocol = (_req)->mrail.protocol;  \
    (_rndv)->memhandle = (_req)->mrail.remote_handle;    \
    (_rndv)->buf_addr = (_req)->mrail.remote_addr;  \
}

#define MPIDI_CH3I_MRAIL_SET_REQ_REMOTE_RNDV(_req,_pkt) \
{   \
    (_req)->mrail.protocol = (_pkt)->rndv.protocol;     \
    (_req)->mrail.remote_addr = (_pkt)->rndv.buf_addr;  \
    (_req)->mrail.remote_handle = (_pkt)->rndv.memhandle;         \
}

/* Return type of the sending interfaces */
#define MPI_MRAIL_MSG_QUEUED (-1)

int MPIDI_CH3I_MRAILI_Fast_rdma_ok (struct MPIDI_VC * vc, int len);

int MPIDI_CH3I_MRAILI_Fast_rdma_send_complete (struct MPIDI_VC * vc,
                                               MPID_IOV * iov,
                                               int n_iov, int *nb, vbuf ** v);
int MPIDI_CH3I_RDMA_cq_poll ();

void MRAILI_Init_vc (struct MPIDI_VC * vc, int pg_rank);

int MPIDI_CH3I_MRAILI_Eager_send (struct MPIDI_VC * vc,
                                  MPID_IOV * iov,
                                  int n_iov,
                                  int pkt_len,
                                  int *num_bytes_ptr, vbuf ** buf_handle);

int MRAILI_Post_send (struct MPIDI_VC * vc, vbuf * v,
                      const MRAILI_Channel_info * channel);

int MRAILI_Fill_start_buffer (vbuf * v, MPID_IOV * iov, int n_iov);

/* Following functions are defined in udapl_channel_manager.c */

/* return type predefinition */
#define T_CHANNEL_NO_ARRIVE 0
#define T_CHANNEL_EXACT_ARRIVE 1
#define T_CHANNEL_OUT_OF_ORDER_ARRIVE 2
#define T_CHANNEL_CONTROL_MSG_ARRIVE 3
#define T_CHANNEL_ERROR -1

int MPIDI_CH3I_MRAILI_Get_next_vbuf_local (struct MPIDI_VC * vc,
                                           vbuf ** vbuf_handle);
                                           
int MPIDI_CH3I_MRAILI_Get_next_vbuf(struct MPIDI_VC **vc_pptr, vbuf **v_ptr);

int MPIDI_CH3I_MRAILI_Waiting_msg(struct MPIDI_VC * vc, vbuf **, int);

int MPIDI_CH3I_MRAILI_Cq_poll (vbuf **, struct MPIDI_VC *, int, int);

void MRAILI_Send_noop(struct MPIDI_VC * c, const MRAILI_Channel_info * channel);

int MRAILI_Send_noop_if_needed (struct MPIDI_VC * vc,
                                const MRAILI_Channel_info * channel);

int MRAILI_Send_rdma_credit_if_needed (struct MPIDI_VC * vc);

int MPIDI_CH3I_MRAILI_rput_complete(struct MPIDI_VC *, MPID_IOV *, 
                                    int, int *, vbuf **, int); 

/* Following interface for rndv msgs */
void MPIDI_CH3I_MRAILI_Rendezvous_rput_push (struct MPIDI_VC * vc,
                                             MPID_Request * sreq);

void MPIDI_CH3I_MRAILI_Rendezvous_rget_push (struct MPIDI_VC * vc,
                                             MPID_Request * rreq);
void MRAILI_Release_recv_rdma (vbuf * v);

int MPIDI_CH3I_MRAIL_Finish_request(MPID_Request *rreq);

extern struct MPIDI_VC *flowlist;

#endif /* MPIDI_CH3_RDMA_POST_H */
