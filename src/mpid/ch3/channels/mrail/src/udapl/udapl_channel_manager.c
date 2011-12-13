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
#include "udapl_util.h"
#include "udapl_priv.h"
#include "mpiutil.h"

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
    fflush(stderr); \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

static inline int
CMANAGER_SOLVE_GLOBAL (MRAILI_Channel_manager * cmanager, int global_index)
{
    return (global_index + cmanager->num_local_pollings);
}

const char *MPIDI_CH3_VC_GetStateString(MPIDI_VC_t *vc)
{
	return NULL;
}
static inline void
VQUEUE_ENQUEUE (MRAILI_Channel_manager * cmanager, int index, vbuf * v)
{
    v->desc.next = NULL;
    if (cmanager->v_queue_tail[index] == NULL)
      {
          cmanager->v_queue_head[index] = v;
      }
    else
      {
          cmanager->v_queue_tail[index]->desc.next = v;
      }
    cmanager->v_queue_tail[index] = v;
    cmanager->len[index]++;
}


/*add later */
static inline vbuf *
VQUEUE_DEQUEUE (MRAILI_Channel_manager * cmanager, int index)
{
    vbuf *v;
    v = cmanager->v_queue_head[index];
    cmanager->v_queue_head[index] = v->desc.next;
    if (v == cmanager->v_queue_tail[index])
      {
          cmanager->v_queue_tail[index] = NULL;
      }
    cmanager->len[index]--;
    v->desc.next = NULL;
    return v;
}

static inline int
PKT_IS_NOOP (void *v)
{
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p = ((vbuf *) v)->pheader;
    return ((p->type == MPIDI_CH3_PKT_NOOP) ? 1 : 0);
}

static inline int
GetSeqNumVbuf (vbuf * buf)
{
    if (NULL == buf)
        return PKT_IS_NULL;

    switch (((MPIDI_CH3I_MRAILI_Pkt_comm_header *) buf->pheader)->type)
      {
      case MPIDI_CH3_PKT_EAGER_SEND:
      case MPIDI_CH3_PKT_READY_SEND:
      case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
      case MPIDI_CH3_PKT_RNDV_REQ_TO_SEND:
      case MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND:
          {
              return ((MPIDI_CH3_Pkt_send_t *) (buf->pheader))->seqnum;
          }
      case MPIDI_CH3_PKT_RNDV_CLR_TO_SEND:
          {
              return ((MPIDI_CH3_Pkt_rndv_clr_to_send_t *) (buf->pheader))->
                  seqnum;
          }
      case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
          {
              return ((MPIDI_CH3_Pkt_packetized_send_start_t *) (buf->
                                                                 pheader))->
                  seqnum;
          }
      case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
          {
              return ((MPIDI_CH3_Pkt_packetized_send_data_t *) (buf->
                                                                pheader))->
                  seqnum;
          }
      case MPIDI_CH3_PKT_RNDV_R3_DATA:
          {
              return ((MPIDI_CH3_Pkt_rndv_r3_data_t *) (buf->pheader))->
                  seqnum;
          }
#ifndef MV2_DISABLE_HEADER_CACHING 
      case MPIDI_CH3_PKT_FAST_EAGER_SEND:
      case MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ:
          {
              return ((MPIDI_CH3I_MRAILI_Pkt_fast_eager *) (buf->pheader))->
                  seqnum;
          }
#endif
      case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
          {
              return ((MPIDI_CH3_Pkt_cancel_send_req_t *)(buf->pheader))->seqnum;
          }
      case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
          {
              return ((MPIDI_CH3_Pkt_cancel_send_resp_t *)(buf->pheader))->seqnum;
          }
      case MPIDI_CH3_PKT_PUT:
      case MPIDI_CH3_PKT_GET:
      case MPIDI_CH3_PKT_GET_RESP:
      case MPIDI_CH3_PKT_ACCUMULATE:
      case MPIDI_CH3_PKT_ACCUM_IMMED:
      case MPIDI_CH3_PKT_LOCK:
      case MPIDI_CH3_PKT_LOCK_GRANTED:
      case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK:
      case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:
      case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK:
      case MPIDI_CH3_PKT_PT_RMA_DONE:
      case MPIDI_CH3_PKT_PUT_RNDV:
      case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
      case MPIDI_CH3_PKT_GET_RNDV:
          {
              return ((MPIDI_CH3_Pkt_put_t *)(buf->pheader))->seqnum;
          }
      case MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND:
          {
              return ((MPIDI_CH3_Pkt_rndv_clr_to_send_t *)
                      (buf->pheader))->seqnum;
          }
      case MPIDI_CH3_PKT_RPUT_FINISH:
         {
               return ((MPIDI_CH3_Pkt_rput_finish_t *)(buf->pheader))->seqnum;
         }

      default:
          return PKT_NO_SEQ_NUM;
      }
}

static inline vbuf *
MPIDI_CH3I_RDMA_poll (MPIDI_VC_t * vc)
{
    vbuf *v = NULL;
    volatile VBUF_FLAG_TYPE *tail;

    if (num_rdma_buffer == 0)
        return NULL;

    v = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
    tail = &v->head_flag;

    if (*tail && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail)
      {
          /* advance receive pointer */
          DEBUG_PRINT ("[in side rdma poll] vc %p\n", vc);
          if (++(vc->mrail.rfp.p_RDMA_recv) >= num_rdma_buffer)
              vc->mrail.rfp.p_RDMA_recv = 0;
          DEBUG_PRINT ("[send: rdma_send] lkey %d%d, rkey %d%d\n",
                       vc->mrail.rfp.RDMA_send_buf_hndl[0].lkey,
                       vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey);

          MRAILI_FAST_RDMA_VBUF_START (v, *tail, v->pheader)
              DEBUG_PRINT
              ("rank %d,[recv: poll rdma] recv %d, tail %d, size %d\n",
               vc->pg_rank, vc->mrail.rfp.p_RDMA_recv,
               vc->mrail.rfp.p_RDMA_recv_tail, *tail);
      }
    else
      {
          v = NULL;
      }
    return v;
}

int
MPIDI_CH3I_MRAILI_Register_channels (MPIDI_VC_t * vc, int num,
                                     vbuf * (*func[])(void *))
{
    return MPI_SUCCESS;
}

int
MPIDI_CH3I_MRAILI_Get_next_vbuf_local (MPIDI_VC_t * vc, vbuf ** vbuf_handle)
{
    int i, seq;
    MRAILI_Channel_manager *cmanager = &vc->mrail.cmanager;
    int seq_expected = vc->seqnum_recv;
    int type;

    *vbuf_handle = NULL;

 if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) { 
    /*First loop over all queues to see if there is any pkt already there */
    for (i = 0; i < cmanager->total_subrails; i++)
      {
          seq = GetSeqNumVbuf (cmanager->v_queue_head[i]);
          if (seq == seq_expected)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                vc->seqnum_recv++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            }
          else if (seq == PKT_NO_SEQ_NUM)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
          else if (seq != seq_expected && seq != -1)
            {
            }
      }
    /* no pkt has arrived yet, so we will poll the local channels for the pkt */
    for (i = 0; i < cmanager->num_local_pollings; i++)
      {
          seq = GetSeqNumVbuf (cmanager->v_queue_head[i]);
          if (seq == seq_expected)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                vc->seqnum_recv++;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            }
          else if (seq == PKT_NO_SEQ_NUM)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
          else
            {
                *vbuf_handle = MPIDI_CH3I_RDMA_poll (vc);
                seq = GetSeqNumVbuf (*vbuf_handle);
                if (seq == seq_expected)
                  {
                      type = T_CHANNEL_EXACT_ARRIVE;
                      vc->seqnum_recv++;
                      DEBUG_PRINT ("[vbuf_local]:find one, now seqnum %d\n",
                                   vc->seqnum_recv);
                      goto fn_exit;
                  }
                else if (seq == PKT_NO_SEQ_NUM)
                  {
                      type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                      DEBUG_PRINT ("[vbuf_local]: get control msg\n");
                      goto fn_exit;
                  }
                else if (*vbuf_handle != NULL)
                  {
                      VQUEUE_ENQUEUE (cmanager, i, *vbuf_handle);
                      DEBUG_PRINT
                          ("[vbuf_local]: get out of order msg, seq %d, expecting %d\n",
                           seq, seq_expected);
                      *vbuf_handle = NULL;
                  }
            }
      }
  }

    type = T_CHANNEL_NO_ARRIVE;
    *vbuf_handle = NULL;
  fn_exit:

    for (i = 0; i < vc->mrail.num_total_subrails; i++)
      {
          if (vc->mrail.send_wqes_avail[i] < UDAPL_LOW_WQE_THRESHOLD)
            {
                break;
            }
      }

    if (i != vc->mrail.num_total_subrails)
      {
          vbuf *vbuffer;
          MPIDI_CH3I_MRAILI_Cq_poll (&vbuffer, vc, 1, 0);
      }

    return type;
}

int MPIDI_CH3I_MRAILI_Get_next_vbuf(MPIDI_VC_t **vc_pptr, vbuf **v_ptr)
{
    *vc_pptr = NULL;
    *v_ptr   = NULL;
     return T_CHANNEL_NO_ARRIVE;
}

int MPIDI_CH3I_MRAILI_Waiting_msg(MPIDI_VC_t * vc, vbuf ** vbuf_handle, int blocking)
{
    int i, seq;
    MRAILI_Channel_manager *cmanager = &vc->mrail.cmanager;
    int seq_expected = vc->seqnum_recv;
    int type = T_CHANNEL_NO_ARRIVE;
    int is_blocking = 1;

    *vbuf_handle = NULL;

    for (i = 0; i < cmanager->total_subrails; i++)
      {
          seq = GetSeqNumVbuf (cmanager->v_queue_head[i]);
          if (seq == seq_expected)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                type = T_CHANNEL_EXACT_ARRIVE;
                vc->seqnum_recv++;
                goto fn_exit;
            }
          else if (PKT_NO_SEQ_NUM == seq)
            {
                *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
          else if (PKT_IS_NULL == seq)
            {
            }
      }

    /* Obviously the packet with correct sequence hasn't arrived */
    while (1)
      {
          /* poll local subrails */
          for (i = 0; i < cmanager->num_local_pollings; i++)
            {
                seq = GetSeqNumVbuf (cmanager->v_queue_head[i]);
                if (seq == seq_expected)
                  {
                      *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                      vc->seqnum_recv++;
                      type = T_CHANNEL_EXACT_ARRIVE;
                      goto fn_exit;
                  }
                else if (seq == PKT_NO_SEQ_NUM)
                  {
                      *vbuf_handle = VQUEUE_DEQUEUE (cmanager, i);
                      type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                      goto fn_exit;
                  }
                else
                  {
                      if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
                          *vbuf_handle = MPIDI_CH3I_RDMA_poll (vc);
                          seq = GetSeqNumVbuf (*vbuf_handle);
                          if (seq == seq_expected)
                            {
                                type = T_CHANNEL_EXACT_ARRIVE;
                                vc->seqnum_recv++;
                                goto fn_exit;
                            }
                          else if (seq == PKT_NO_SEQ_NUM)
                            {
                                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                                goto fn_exit;
                            }
                          else if (*vbuf_handle != NULL)
                            {
                                VQUEUE_ENQUEUE (cmanager, i, *vbuf_handle);
                                *vbuf_handle = NULL;
                            }
                      }
                  }
            }

          type = MPIDI_CH3I_MRAILI_Cq_poll (vbuf_handle, vc, 0, 0);
          if (type != T_CHANNEL_NO_ARRIVE)
            {
                switch (type)
                  {
                  case (T_CHANNEL_EXACT_ARRIVE):
                      goto fn_exit;
                  case (T_CHANNEL_OUT_OF_ORDER_ARRIVE):
                      continue;
                  case (T_CHANNEL_CONTROL_MSG_ARRIVE):
                      goto fn_exit;
                  default:
                      /* Error here */
                      MPIU_Assert (0);
                      break;
                  }
            }
          else
            {

            }

          if (is_blocking != 1)
              goto fn_exit;
      }
  fn_exit:
    DEBUG_PRINT
        ("now return from solve out of order, type %d, next expected %d\n",
         type, vc->seqnum_recv);
    return type;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_cq_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* Behavior of RDMA cq poll:
 *  1, of vc_req is not null, then function return vbuf only on this vc_req
 *  2, else function return vbuf on any incomming channel
 *  3, if vbuf is exactly the next expected vbuf on the channel, increase
 *  vc->seqnum_recv, and vbuf is not enqueued
 *  4, possible return values:
 *      #define T_CHANNEL_NO_ARRIVE 0
 *      #define T_CHANNEL_EXACT_ARRIVE 1
 *      #define T_CHANNEL_OUT_OF_ORDER_ARRIVE 2
 *      #define T_CHANNEL_CONTROL_MSG_ARRIVE 3
 *      #define T_CHANNEL_ERROR -1
 */
int MPIDI_CH3I_MRAILI_Cq_poll(vbuf **vbuf_handle, MPIDI_VC_t * vc_req, 
                              int receiving, int is_blocking)
{
    DAT_RETURN ret1;
    MPIDI_VC_t *vc;
    UDAPL_DESCRIPTOR sc;
    vbuf *v;
    DAT_EVENT event;

    int i, j, needed;
    static int last_poll = 0;
    int type = T_CHANNEL_NO_ARRIVE;

    *vbuf_handle = NULL;
    for (i = last_poll, j = 0;
         j < MPIDI_CH3I_RDMA_Process.num_hcas;
         i = ((i + 1) % MPIDI_CH3I_RDMA_Process.num_hcas), j++)
      {
          last_poll = i;
          ret1 = dat_evd_dequeue (MPIDI_CH3I_RDMA_Process.cq_hndl[i], &event);
          if (ret1 == DAT_SUCCESS)
            {
                DEBUG_PRINT ("[poll cq]: get complete queue entry\n");
                MPIU_Assert (event.event_number == DAT_DTO_COMPLETION_EVENT);

                /* Handling fatal error like
                 * DAT_DTO_ERR_TRANSPORT (occures when network disfunction)
                 */
                if (event.event_data.dto_completion_event_data.status != DAT_DTO_SUCCESS)
                  {
                      int rank;
                      PMI_Get_rank(&rank);
                      udapl_error_abort(UDAPL_STATUS_ERR,
                              "[%d] DAT_EVD_ERROR in Consume_signals %x\n", rank,
                              event.event_data.dto_completion_event_data.status);
                  }

                sc = ((struct vbuf *) event.event_data.
                      dto_completion_event_data.user_cookie.as_ptr)->desc;
                v = (vbuf *) ((aint_t) sc.cookie.as_ptr);
                vc = (MPIDI_VC_t *) (v->vc);

                /* get the VC and increase its wqe */
                if (sc.opcode == UDAPL_SEND ||
                    sc.opcode == UDAPL_RDMA_WRITE ||
                    sc.opcode == UDAPL_RDMA_READ)
                  {
                      DEBUG_PRINT ("[device_Check] process send\n");
                      MRAILI_Process_send (v);

                      type = T_CHANNEL_NO_ARRIVE;
                      *vbuf_handle = NULL;
                  }
                  else if ((NULL == vc_req || vc_req == vc) && 0 == receiving )
                  {
                      /* In this case, we should return the vbuf any way if it is next expected */
                      int seqnum = GetSeqNumVbuf (v);

                      *vbuf_handle = v;
                      v->head_flag =
                          (VBUF_FLAG_TYPE) event.event_data.
                          dto_completion_event_data.transfered_length;
                      vc->mrail.srp.preposts[v->subchannel.rail_index]--;

                      needed = udapl_prepost_depth + udapl_prepost_noop_extra
                          + MIN (udapl_prepost_rendezvous_extra,
                                 vc->mrail.srp.rendezvous_packets_expected[v->
                                                                           subchannel.
                                                                           rail_index]);

                      if (seqnum == PKT_NO_SEQ_NUM)
                        {
                            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                        }
                      else if (seqnum == vc->seqnum_recv)
                        {
                            vc->seqnum_recv++;
                            type = T_CHANNEL_EXACT_ARRIVE;
                            DEBUG_PRINT
                                ("[channel manager] get one with exact seqnum\n");
                        }
                      else
                        {
                            type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                            VQUEUE_ENQUEUE (&vc->mrail.cmanager,
                                            CMANAGER_SOLVE_GLOBAL (&vc->mrail.
                                                                   cmanager,
                                                                   i), v);
                            DEBUG_PRINT ("get recv %d (%d)\n", seqnum,
                                         vc->seqnum_recv);
                        }

                      DEBUG_PRINT ("[channel manager] needed %d\n", needed);
                      if (PKT_IS_NOOP (v))
                        {
                            PREPOST_VBUF_RECV (vc, v->subchannel);
                            /* noops don't count for credits */
                            vc->mrail.srp.local_credit[v->subchannel.
                                                       rail_index]--;
                        }
                      else if (vc->mrail.srp.
                               preposts[v->subchannel.rail_index] <
                               udapl_rq_size
                               && vc->mrail.srp.preposts[v->subchannel.
                                                         rail_index] +
                               udapl_prepost_threshold < needed)
                        {
                            do
                              {
                                  PREPOST_VBUF_RECV (vc, v->subchannel);
                              }
                            while (vc->mrail.srp.
                                   preposts[v->subchannel.rail_index] <
                                   udapl_rq_size
                                   && vc->mrail.srp.preposts[v->subchannel.
                                                             rail_index] <
                                   needed);
                        }

                      MRAILI_Send_noop_if_needed (vc, &v->subchannel);
                      if (type == T_CHANNEL_CONTROL_MSG_ARRIVE ||
                          type == T_CHANNEL_EXACT_ARRIVE ||
                          type == T_CHANNEL_OUT_OF_ORDER_ARRIVE)
                        {
                            goto fn_exit;
                        }
                  }
                else
                  {
                      /* Now since this is not the packet we want, we have to enqueue it */
                      type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                      *vbuf_handle = NULL;
                      VQUEUE_ENQUEUE (&vc->mrail.cmanager,
                                      CMANAGER_SOLVE_GLOBAL (&vc->mrail.
                                                             cmanager, i), v);
                      vc->mrail.srp.preposts[v->subchannel.rail_index]--;

                      needed = udapl_prepost_depth + udapl_prepost_noop_extra
                          + MIN (udapl_prepost_rendezvous_extra,
                                 vc->mrail.srp.rendezvous_packets_expected[v->
                                                                           subchannel.
                                                                           rail_index]);
                      v->head_flag =
                          (VBUF_FLAG_TYPE) event.event_data.
                          dto_completion_event_data.transfered_length;
                      if (PKT_IS_NOOP (v))
                        {
                            PREPOST_VBUF_RECV (vc, v->subchannel);
                            /* noops don't count for credits */
                            vc->mrail.srp.local_credit[v->subchannel.
                                                       rail_index]--;
                        }
                      else if (vc->mrail.srp.
                               preposts[v->subchannel.rail_index] <
                               udapl_rq_size
                               && vc->mrail.srp.preposts[v->subchannel.
                                                         rail_index] +
                               udapl_prepost_threshold < needed)
                        {
                            do
                              {
                                  PREPOST_VBUF_RECV (vc, v->subchannel);
                              }
                            while (vc->mrail.srp.
                                   preposts[v->subchannel.rail_index] <
                                   udapl_rq_size
                                   && vc->mrail.srp.preposts[v->subchannel.
                                                             rail_index] <
                                   needed);
                        }
                      MRAILI_Send_noop_if_needed (vc, &v->subchannel);
                  }
            }
          else
            {
                *vbuf_handle = NULL;
                type = T_CHANNEL_NO_ARRIVE;
            }
      }
  fn_exit:

    return type;
}

void
MPIDI_CH3I_Cleanup_after_connection(MPIDI_VC_t *vc)
{
    /* nothing to be cleaned up in udapl */  
}

void
MPIDI_CH3I_Check_pending_send(MPIDI_VC_t *vc) 
{
    return;
}
