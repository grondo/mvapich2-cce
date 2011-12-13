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

#ifndef _UDAPL_PRIV_H__
#define _UDAPL_PRIV_H__

#include "mpidi_ch3i_rdma_conf.h"

typedef struct rdma_iba_addr_tb
{
    DAT_SOCK_ADDR **ia_addr;
    DAT_CONN_QUAL **service_id;
    DAT_CONN_QUAL **service_id_1sc;
    int **hostid;
} rdma_iba_addr_tb_t;

#define PACKET_SET_RDMA_CREDIT(_p, _c) \
{                                                                   \
    (_p)->rdma_credit = (_c)->mrail.rfp.rdma_credit;                     \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->vbuf_credit = 0;      \
    (_p)->remote_credit = 0;    \
}

#define PACKET_SET_CREDIT(_p, _c, _rail_index) \
if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) \
{                                                                   \
    (_p)->rdma_credit = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                                        \
    (_p)->vbuf_credit = (_c)->mrail.srp.local_credit[(_rail_index)];      \
    (_p)->remote_credit = (_c)->mrail.srp.remote_credit[(_rail_index)];   \
    (_c)->mrail.srp.local_credit[(_rail_index)] = 0;         \
} else {     \
    (_p)->vbuf_credit = (_c)->mrail.srp.local_credit[(_rail_index)];      \
    (_p)->remote_credit = (_c)->mrail.srp.remote_credit[(_rail_index)];   \
    (_c)->mrail.srp.local_credit[(_rail_index)] = 0;         \
}

#define PREPOST_VBUF_RECV(c, subchannel)  {                         \
    vbuf *v = get_vbuf();                                       \
    vbuf_init_recv(v, VBUF_BUFFER_SIZE, &subchannel);        \
    UDAPL_POST_RR(c, v, subchannel);                     \
    vc->mrail.srp.local_credit[subchannel.rail_index]++;                          \
    vc->mrail.srp.preposts[subchannel.rail_index]++;                              \
}

#define  UDAPL_POST_SR(_v, _c, _channel, err_string) {                 \
    {                                                               \
        DAT_RETURN ret;                                             \
        switch (v->desc.opcode){  \
        case UDAPL_RDMA_WRITE:  \
            {  \
               ret = dat_ep_post_rdma_write(_c->mrail.qp_hndl[(_channel).rail_index], 1, \
                                            &(_v->desc.local_iov), \
                                            _v->desc.cookie, \
                                            &(_v->desc.remote_iov), \
                                            _v->desc.completion_flag); \
               break; \
            } \
        case UDAPL_SEND:  \
            {  \
                ret = dat_ep_post_send (_c->mrail.qp_hndl[(_channel).rail_index], 1, &(_v->desc.local_iov), _v->desc.cookie, _v->desc.completion_flag);  \
                break;  \
            }  \
        case UDAPL_RECV:  \
            {  \
                ret = dat_ep_post_recv (_c->mrail.qp_hndl[(_channel).rail_index], 1, &(_v->desc.local_iov), _v->desc.cookie, v->desc.completion_flag);  \
              break;  \
            }  \
        default:  \
              udapl_error_abort(UDAPL_RETURN_ERR, "unkown op_code");  \
        }  \
        if(ret != DAT_SUCCESS) {  \
            udapl_error_abort(UDAPL_RETURN_ERR, err_string);  \
        }  \
    }                                                               \
}

#define UDAPL_POST_RR(c,vbuf,channel) {   \
    DAT_RETURN ret;   \
    vbuf->vc = (void *)c;                  \
 ret = dat_ep_post_recv (c->mrail.qp_hndl[(channel).rail_index], 1, &(vbuf->desc.local_iov), vbuf->desc.cookie, vbuf->desc.completion_flag);  \
if(ret != DAT_SUCCESS) {  \
            udapl_error_abort(UDAPL_RETURN_ERR, "error in UDAPL_POST_RR");   \
        }  \
}

/*
 * post a descriptor to the send queue
 * all outgoing packets go through this routine.
 * takes a connection rather than a vi because we need to
 * update flow control information on the connection. Also
 * it turns out it is always called in the context of a connection,
 * i.e. it would be post_send(c->vi) otherwise.
 * xxx should be inlined
 */

#define BACKLOG_ENQUEUE(q,v) {                      \
    v->desc.next = NULL;                            \
    if (q->vbuf_tail == NULL) {                     \
         q->vbuf_head = v;                          \
    } else {                                        \
         q->vbuf_tail->desc.next = v;               \
    }                                               \
    q->vbuf_tail = v;                               \
    q->len++;                                       \
}


/*add later */
#define BACKLOG_DEQUEUE(q,v)  {                     \
    v = q->vbuf_head;                               \
    q->vbuf_head = v->desc.next;                    \
    if (v == q->vbuf_tail) {                        \
        q->vbuf_tail = NULL;                        \
    }                                               \
    q->len--;                                       \
    v->desc.next = NULL;                            \
}

#endif
