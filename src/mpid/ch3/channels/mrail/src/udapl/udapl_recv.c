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

#include "rdma_impl.h"
#include "mpiimpl.h"
#include "udapl_util.h"
#include "udapl_priv.h"

#define SET_CREDIT(header, vc, rail) \
if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path)                 \
{                                                               \
    vc->mrail.rfp.ptail_RDMA_send += header->rdma_credit; \
    if (vc->mrail.rfp.ptail_RDMA_send >= num_rdma_buffer)       \
        vc->mrail.rfp.ptail_RDMA_send -= num_rdma_buffer;       \
    vc->mrail.srp.remote_cc[rail] = header->remote_credit;\
    vc->mrail.srp.remote_credit[rail] += header->vbuf_credit; \
} else {                                                        \
    vc->mrail.srp.remote_cc[rail] = header->remote_credit;\
    vc->mrail.srp.remote_credit[rail] += header->vbuf_credit; \
}

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);  fflush(stderr);                   \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_Pass_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_MRAIL_Parse_header (MPIDI_VC_t * vc,
                               vbuf * v, void **pkt, int *header_size)
{
    void *vstart;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *header;

    DEBUG_PRINT ("[parse header] begin parse header, header %p\n", v);

    vstart = v->pheader;
    header = vstart;

    DEBUG_PRINT ("[parse header] begin, header %d\n", header->type);
    switch (header->type)
      {
#ifndef MV2_DISABLE_HEADER_CACHING 
      case (MPIDI_CH3_PKT_FAST_EAGER_SEND):
      case (MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ):
          {
              MPIDI_CH3I_MRAILI_Pkt_fast_eager *fast_header = vstart;
              MPIDI_CH3_Pkt_eager_send_t *eager_header =
                  (MPIDI_CH3_Pkt_eager_send_t *) vc->mrail.rfp.
                  cached_incoming;

              if (MPIDI_CH3_PKT_FAST_EAGER_SEND == header->type)
                {
                    *header_size = sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager);
                }
              else
                {
                    *header_size =
                        sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req);
                    eager_header->sender_req_id =
                        ((MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req *)
                         vstart)->sender_req_id;
                }

              DEBUG_PRINT ("[receiver side] cached credit %d\n",
                           eager_header->rdma_credit);

              eager_header->data_sz = fast_header->bytes_in_pkt;
              eager_header->seqnum = fast_header->seqnum;

              *pkt = (void *) eager_header;
              DEBUG_PRINT
                  ("[recv: parse header] faster headersize returned %d\n",
                   *header_size);
          }
          break;
#endif
      case (MPIDI_CH3_PKT_EAGER_SEND):
          {
              DEBUG_PRINT ("[recv: parse header] pkt eager send\n");
#ifndef MV2_DISABLE_HEADER_CACHING 
              if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path 
                  && v->padding != NORMAL_VBUF_FLAG)
                {
                    /* Only cache header if the packet is from RdMA path 
                     * XXXX: what is R3_FLAG? 
                     */
                   MPIU_Memcpy ((vc->mrail.rfp.cached_incoming), vstart,
                            sizeof (MPIDI_CH3_Pkt_eager_send_t));
                }
#endif
              *pkt = (MPIDI_CH3_Pkt_t *) vstart;
              *header_size = sizeof (MPIDI_CH3_Pkt_eager_send_t);
              DEBUG_PRINT ("[recv: parse header] headersize returned %d\n",
                           *header_size);
          }
          break;
      case (MPIDI_CH3_PKT_RNDV_REQ_TO_SEND):
      case (MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND):
      case (MPIDI_CH3_PKT_RNDV_CLR_TO_SEND):
      case (MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND):
      case (MPIDI_CH3_PKT_RPUT_FINISH):
      case (MPIDI_CH3_PKT_NOOP):
      case MPIDI_CH3_PKT_EAGER_SYNC_ACK:
      case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
      case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
          {
              *pkt = vstart;
          }
          break;
      case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
          {
              *pkt = vstart;
              *header_size = sizeof (MPIDI_CH3_Pkt_packetized_send_start_t);
              break;
          }
      case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_packetized_send_data_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_RNDV_R3_DATA:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_rndv_r3_data_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
      case MPIDI_CH3_PKT_READY_SEND:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_send_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_PUT:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_put_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_PUT_RNDV:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_put_rndv_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_GET_RNDV:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_get_rndv_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_GET:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_get_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_GET_RESP:     /*15 */
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_get_resp_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_ACCUMULATE:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_accum_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_accum_rndv_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_LOCK:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_lock_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_LOCK_GRANTED:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_lock_granted_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_PT_RMA_DONE:
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_pt_rma_done_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:      /* optimization for single puts */
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_lock_put_unlock_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:      /* optimization for single gets */
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_lock_get_unlock_t);
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:    /* optimization for single accumulates */
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_lock_accum_unlock_t);
              *pkt = vstart;
              break;
          }
    case MPIDI_CH3_PKT_ACCUM_IMMED:
        {   
            *header_size = sizeof(MPIDI_CH3_Pkt_accum_immed_t);
            *pkt = vstart;
            break;
        }   
      case MPIDI_CH3_PKT_FLOW_CNTL_UPDATE:
          {
              *pkt = vstart;
              break;
          }
      case MPIDI_CH3_PKT_CLOSE:        /*24 */
          {
              *header_size = sizeof (MPIDI_CH3_Pkt_close_t);
              *pkt = vstart;
          }
          break;
      default:
          {
              /* Header is corrupted if control has reached here in prototype */
              /* */
              udapl_error_abort (-1,
                                 "Control shouldn't reach here in prototype, header %d\n",
                                 header->type);
          }
      }
    SET_CREDIT ((&(((MPIDI_CH3_Pkt_t *) (*pkt))->eager_send)), vc,
                (v->subchannel.rail_index));

#if 0
    fprintf (stderr,
             "[parse header] after set the credit, remote_cc %d, remote_credit %d, rdma head %d, tail %d\n",
             vc->mrail.srp.remote_cc[v->subchannel.rail_index],
             vc->mrail.srp.remote_credit[v->subchannel.rail_index],
             vc->mrail.rfp.phead_RDMA_send, vc->mrail.rfp.ptail_RDMA_send);
#endif
    if (vc->mrail.srp.remote_credit[v->subchannel.rail_index] > 0 &&
        vc->mrail.srp.backlog.len > 0)
      {
          MRAILI_Backlog_send (vc, &v->subchannel);
      }
    /* if any credits remain, schedule rendezvous progress */
    if ((vc->mrail.srp.remote_credit[v->subchannel.rail_index] > 0
         || (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path
         && vc->mrail.rfp.ptail_RDMA_send != vc->mrail.rfp.phead_RDMA_send)
        ) && (vc->mrail.sreq_head != NULL))
      {
          PUSH_FLOWLIST (vc);
      }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_Fill_Request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_MRAIL_Fill_Request (MPID_Request * req, vbuf * v,
                               int header_size, int *nb)
{
    MPID_IOV *iov;
    int n_iov;
    int len_avail = v->head_flag - header_size;
    void *data_buf;
    int i;

    iov = (req == NULL) ? NULL : req->dev.iov;
    n_iov = (req == NULL) ? 0 : req->dev.iov_count;

    data_buf = (void *) ((aint_t) v->pheader + header_size);

    DEBUG_PRINT
        ("[recv:fill request] total len %d, head len %d, n iov %d\n",
         v->head_flag, header_size, n_iov);
    DEBUG_PRINT ("[recv:fill request] databuf %p: %c%c%c%c\n", data_buf,
                 ((char *) data_buf)[0], ((char *) data_buf)[1],
                 ((char *) data_buf)[2], ((char *) data_buf)[3]);

    *nb = 0;
    for (i = req->dev.iov_offset; i < n_iov; i++)
      {
          DEBUG_PRINT
              ("[recv:fill request] iter %d, len avail %d, iov[i].len %d\n",
               i, len_avail, iov[i].MPID_IOV_LEN);
          if (len_avail >= (int) iov[i].MPID_IOV_LEN
              && iov[i].MPID_IOV_LEN != 0)
            {
            MPIU_Memcpy (iov[i].MPID_IOV_BUF, data_buf, iov[i].MPID_IOV_LEN);
                data_buf = (void *) ((aint_t) data_buf + iov[i].MPID_IOV_LEN);
                len_avail -= iov[i].MPID_IOV_LEN;
                *nb += iov[i].MPID_IOV_LEN;
            }
          else if (len_avail > 0)
            {
            MPIU_Memcpy (iov[i].MPID_IOV_BUF, data_buf, len_avail);
                *nb += len_avail;
                break;
            }
      }

    DEBUG_PRINT ("[recv:fill request] finish filling request\n");
    DEBUG_PRINT
        ("[recv:fill request] about to return form request, nb %d\n", *nb);
    return MPI_SUCCESS;
}

static inline void
MRAILI_Prepost_R3 ()
{

}

void
MPIDI_CH3I_MRAIL_Release_vbuf (vbuf * v)
{
    if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
        if (v->padding == NORMAL_VBUF_FLAG || v->padding == RPUT_VBUF_FLAG)
            MRAILI_Release_vbuf (v);
        else
          {
              MRAILI_Release_recv_rdma (v);
              MRAILI_Send_noop_if_needed ((MPIDI_VC_t *) v->vc, &v->subchannel);
          }
    } else {
        MRAILI_Release_vbuf (v);  
    }
}
