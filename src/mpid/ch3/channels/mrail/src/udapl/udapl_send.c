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
#include "vbuf.h"
#include "mpiutil.h"

#include DAT_HEADER

#undef DEBUG_PRINT
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

static inline int
MRAILI_Fast_rdma_select_channel (MPIDI_VC_t * vc,
                                 MRAILI_Channel_info * const channel)
{
    channel->rail_index = 0;
    channel->hca_index = 0;
    channel->port_index = 1;
    return MPI_SUCCESS;
}

static inline int
MRAILI_Send_select_channel (MPIDI_VC_t * vc,
                            MRAILI_Channel_info * const channel)
{
    /* we are supposed to consider both the scheduling policy and credit infor */
    /* We are supposed to return rail_index = -1 if no rail has available credit */
    channel->rail_index = 0;
    channel->hca_index = 0;
    channel->port_index = 0;
    return MPI_SUCCESS;
}

/* to handle Send Q overflow, we maintain an extended send queue
 * above the HCA.  This permits use to have a virtually unlimited send Q depth
 * (limited by number of vbufs available for send)
 */
#undef FUNCNAME
#define FUNCNAME MRAILI_Ext_sendq_enqueue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void
MRAILI_Ext_sendq_enqueue (MPIDI_VC_t * c,
                          const MRAILI_Channel_info * channel, vbuf * v)
{
    MPIDI_STATE_DECL(MRAILI_EXT_SENDQ_ENQUEUE);
    MPIDI_FUNC_ENTER(MRAILI_EXT_SENDQ_ENQUEUE);

    v->desc.next = NULL;
    if (c->mrail.ext_sendq_head[channel->rail_index] == NULL)
      {
          c->mrail.ext_sendq_head[channel->rail_index] = v;
      }
    else
      {
          c->mrail.ext_sendq_tail[channel->rail_index]->desc.next = v;
      }
    c->mrail.ext_sendq_tail[channel->rail_index] = v;
    c->force_rndv = 1;

    MPIDI_FUNC_EXIT(MRAILI_EXT_SENDQ_ENQUEUE);
}

/* dequeue and send as many as we can from the extended send queue
 * this is called in each function which may post send prior to it attempting
 * its send, hence ordering of sends is maintained
 */
#undef FUNCNAME
#define FUNCNAME MRAILI_Ext_sendq_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void
MRAILI_Ext_sendq_send (MPIDI_VC_t * c, const MRAILI_Channel_info * channel)
{
    MPIDI_STATE_DECL(MRAILI_EXT_SENDQ_SEND);
    MPIDI_FUNC_ENTER(MRAILI_EXT_SENDQ_SEND);

    vbuf *v;
    while (c->mrail.send_wqes_avail[channel->rail_index]
           && c->mrail.ext_sendq_head[channel->rail_index])
      {
          v = c->mrail.ext_sendq_head[channel->rail_index];
          c->mrail.ext_sendq_head[channel->rail_index] = v->desc.next;
          if (v == c->mrail.ext_sendq_tail[channel->rail_index])
            {
                c->mrail.ext_sendq_tail[channel->rail_index] = NULL;
            }
          v->desc.next = NULL;
          c->mrail.send_wqes_avail[channel->rail_index]--;
          UDAPL_POST_SR (v, c, (*channel),
                         "Mrail_post_sr (viadev_ext_sendq_send)");
      }

    if (c->mrail.send_wqes_avail[channel->rail_index] > 0) {
        c->force_rndv = 0;
    }

    MPIDI_FUNC_EXIT(MRAILI_EXT_SENDQ_SEND);
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_put_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_RDMA_put_datav (MPIDI_VC_t * vc, MPID_IOV * iov, int n,
                           int *num_bytes_ptr)
{
    /* all variable must be declared before the state declarations */
    MPIDI_STATE_DECL (MPID_STATE_MPIDI_CH3I_PUT_DATAV);
    MPIDI_FUNC_ENTER (MPID_STATE_MPIDI_CH3I_PUT_DATAV);

    /* Insert implementation here */
    MPIU_Assert (0);

    MPIDI_FUNC_EXIT (MPID_STATE_MPIDI_CH3I_PUT_DATAV);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_read_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_RDMA_read_datav (MPIDI_VC_t * recv_vc_ptr, MPID_IOV * iov,
                            int iovlen, int *num_bytes_ptr)
{
    /* all variable must be declared before the state declarations */
    MPIDI_STATE_DECL (MPID_STATE_MPIDI_CH3I_RDMA_READ_DATAV);
    MPIDI_FUNC_ENTER (MPID_STATE_MPIDI_CH3I_RDMA_READ_DATAV);

    /* Insert implementation here */
    MPIU_Assert (0);
    MPIDI_FUNC_EXIT (MPID_STATE_MPIDI_CH3I_RDMA_READ_DATAV);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Fast_rdma_fill_start_buf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MRAILI_Fast_rdma_fill_start_buf (MPIDI_VC_t * vc,
                                 MPID_IOV * iov, int n_iov,
                                 int *num_bytes_ptr)
{
    MPIDI_CH3_Pkt_send_t *cached;

    /* Here we assume that iov holds a packet header, 
       ATTN!: it is a must!! */
#ifndef MV2_DISABLE_HEADER_CACHING 
  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    cached =
        (NULL == vc) ? NULL : vc->mrail.rfp.cached_outgoing;
  }
#endif
    MPIDI_CH3_Pkt_send_t *header;
    vbuf *v =
        (NULL ==
         vc) ? NULL : &(vc->mrail.rfp.RDMA_send_buf[vc->mrail.rfp.
                                                    phead_RDMA_send]);
    void *vstart;
    void *data_buf;

    int len, avail = 0;
    int seq_num;

    int i;

    MPIDI_STATE_DECL(MPIDI_CH3I_MRAILI_FAST_RDMA_FILL_START_BUF);
    MPIDI_FUNC_ENTER(MPIDI_CH3I_MRAILI_FAST_RDMA_FILL_START_BUF);

    header = iov[0].MPID_IOV_BUF;
    seq_num = header->seqnum;

    Calculate_IOV_len (iov, n_iov, len);
    if (len > VBUF_BUFFER_SIZE)
        len = VBUF_BUFFER_SIZE;
    avail = len;

    DEBUG_PRINT ("[send: fill buffer] !!!!!!!!!!!!! index %d\n",
                 vc->mrail.rfp.phead_RDMA_send);

    PACKET_SET_RDMA_CREDIT (header, vc);
    DEBUG_PRINT ("header credit %d cached credit %d\n",
                 header->rdma_credit, cached->rdma_credit);

    *num_bytes_ptr = 0;

#ifndef MV2_DISABLE_HEADER_CACHING 
  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    if ((header->type == MPIDI_CH3_PKT_EAGER_SEND) &&
	(len - sizeof(MPIDI_CH3_Pkt_eager_send_t) <= MAX_SIZE_WITH_HEADER_CACHING) &&
        (header->match.parts.tag == cached->match.parts.tag) &&
        (header->match.parts.rank == cached->match.parts.rank) &&
        (header->match.parts.context_id == cached->match.parts.context_id) &&
        /*(header->sender_req_id == cached->sender_req_id) && */
        (header->vbuf_credit == cached->vbuf_credit) &&
        (header->remote_credit == cached->remote_credit) &&
        (header->rdma_credit == cached->rdma_credit))
      {
          /* change the header contents */
          vc->mrail.rfp.cached_hit++;

          if (header->sender_req_id == cached->sender_req_id)
            {
                MPIDI_CH3I_MRAILI_Pkt_fast_eager *fast_header;
		MRAILI_FAST_RDMA_VBUF_START(v, len - sizeof(MPIDI_CH3_Pkt_eager_send_t) +
                                    sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager), vstart);


                DEBUG_PRINT
                    ("[send: fill buf], head cached, head_flag %p, vstart %p, length %d",
                     &v->head_flag, vstart,
 		     len - sizeof(MPIDI_CH3_Pkt_eager_send_t) +
		     sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager));

                fast_header = vstart;
                fast_header->type = MPIDI_CH3_PKT_FAST_EAGER_SEND;
                fast_header->bytes_in_pkt = len - sizeof(MPIDI_CH3_Pkt_eager_send_t);
                fast_header->seqnum = seq_num;
                v->pheader = fast_header;
                data_buf =
                    (void *) ((aint_t) vstart +
                              sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager));

		if (iov[0].MPID_IOV_LEN - sizeof(MPIDI_CH3_Pkt_eager_send_t))
		  MPIU_Memcpy(data_buf, (void *)((uintptr_t)iov[0].MPID_IOV_BUF +
			   sizeof(MPIDI_CH3_Pkt_eager_send_t)), 
			   iov[0].MPID_IOV_LEN - sizeof(MPIDI_CH3_Pkt_eager_send_t));

		data_buf = (void *)((uintptr_t)data_buf + iov[0].MPID_IOV_LEN -
			sizeof(MPIDI_CH3_Pkt_eager_send_t));

                *num_bytes_ptr += sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager);
                avail -= sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager);
            }
          else
            {
                MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req *fast_header;
		MRAILI_FAST_RDMA_VBUF_START(v, len - sizeof(MPIDI_CH3_Pkt_eager_send_t) +
			sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req), vstart);

                DEBUG_PRINT
                    ("[send: fill buf], head cached, head_flag %p, vstart %p, length %d",
                     &v->head_flag, vstart,
		     len - sizeof(MPIDI_CH3_Pkt_eager_send_t) + 
		     sizeof(MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req));

                fast_header = vstart;
                fast_header->type = MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ;
                fast_header->bytes_in_pkt = len - sizeof(MPIDI_CH3_Pkt_eager_send_t);
                fast_header->seqnum = seq_num;
                fast_header->sender_req_id = header->sender_req_id;
                cached->sender_req_id = header->sender_req_id;
                v->pheader = fast_header;
                data_buf =
                    (void *) ((aint_t) vstart +
                              sizeof
                              (MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req));

		if (iov[0].MPID_IOV_LEN - sizeof(MPIDI_CH3_Pkt_eager_send_t))
		  MPIU_Memcpy(data_buf, (void *)((uintptr_t)iov[0].MPID_IOV_BUF +
			       sizeof(MPIDI_CH3_Pkt_eager_send_t)),
			       iov[0].MPID_IOV_LEN - sizeof(MPIDI_CH3_Pkt_eager_send_t));

		data_buf = (void *)((uintptr_t)data_buf + iov[0].MPID_IOV_LEN -
			   sizeof(MPIDI_CH3_Pkt_eager_send_t));

                *num_bytes_ptr +=
                    sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req);
                avail -= sizeof (MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req);
            }
      }
    else {
                  MRAILI_FAST_RDMA_VBUF_START (v, len, vstart);
          DEBUG_PRINT
              ("[send: fill buf], head not cached, v %p, vstart %p, length %d, header size %d\n",
               v, vstart, len, iov[0].MPID_IOV_LEN);
          MPIU_Memcpy (vstart, header, iov[0].MPID_IOV_LEN);
          if (header->type == MPIDI_CH3_PKT_EAGER_SEND)
            MPIU_Memcpy (cached, header, sizeof (MPIDI_CH3_Pkt_eager_send_t));
          vc->mrail.rfp.cached_miss++;
          data_buf = (void *) ((aint_t) vstart + iov[0].MPID_IOV_LEN);
          *num_bytes_ptr += iov[0].MPID_IOV_LEN;
          avail -= iov[0].MPID_IOV_LEN;
          v->pheader = vstart;
    }
  } else
#endif
      {
          MRAILI_FAST_RDMA_VBUF_START (v, len, vstart);
          DEBUG_PRINT
              ("[send: fill buf], head not cached, v %p, vstart %p, length %d, header size %d\n",
               v, vstart, len, iov[0].MPID_IOV_LEN);
          MPIU_Memcpy (vstart, header, iov[0].MPID_IOV_LEN);
#ifndef MV2_DISABLE_HEADER_CACHING 
          if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
              if (header->type == MPIDI_CH3_PKT_EAGER_SEND)
                MPIU_Memcpy (cached, header, sizeof (MPIDI_CH3_Pkt_eager_send_t));
              vc->mrail.rfp.cached_miss++;
          }
#endif
          data_buf = (void *) ((aint_t) vstart + iov[0].MPID_IOV_LEN);
          *num_bytes_ptr += iov[0].MPID_IOV_LEN;
          avail -= iov[0].MPID_IOV_LEN;
          v->pheader = vstart;
      }

    /* We have filled the header, it is time to fit in the actual data */
    for (i = 1; i < n_iov; i++)
      {
          if (avail >= iov[i].MPID_IOV_LEN)
            {
            MPIU_Memcpy (data_buf, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
                data_buf = (void *) ((aint_t) data_buf + iov[i].MPID_IOV_LEN);
                *num_bytes_ptr += iov[i].MPID_IOV_LEN;
                avail -= iov[i].MPID_IOV_LEN;
            }
          else if (avail > 0)
            {
            MPIU_Memcpy (data_buf, iov[i].MPID_IOV_BUF, avail);
                data_buf = (void *) ((aint_t) data_buf + avail);
                *num_bytes_ptr += avail;
                avail = 0;
                break;
            }
          else
              break;
      }

    DEBUG_PRINT ("[send: fill buf], num bytes copied %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPIDI_CH3I_MRAILI_FAST_RDMA_FILL_START_BUF);

    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Fast_rdma_send_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_MRAILI_Fast_rdma_send_complete (MPIDI_VC_t * vc,
                                           MPID_IOV * iov,
                                           int n_iov,
                                           int *num_bytes_ptr,
                                           vbuf ** vbuf_handle)
{
    if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path == 0) {
        return -1;
    }

    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
    MRAILI_Channel_info channel;
    int  align_len;

    vbuf *v = &(vc->mrail.rfp.RDMA_send_buf[vc->mrail.rfp.phead_RDMA_send]);
    vbuf *remote =
        &(vc->mrail.rfp.remote_RDMA_buf[vc->mrail.rfp.phead_RDMA_send]);
    vbuf *rstart;

    MPIDI_STATE_DECL(MPIDI_CH3I_MRAILI_FAST_RDMA_SEND_COMPLETE);
    MPIDI_FUNC_ENTER(MPIDI_CH3I_MRAILI_FAST_RDMA_SEND_COMPLETE);

    MRAILI_Fast_rdma_select_channel (vc, &channel);

    MRAILI_Fast_rdma_fill_start_buf (vc, iov, n_iov, num_bytes_ptr);

    p = v->pheader;
    /*
       MRAILI_FAST_RDMA_VBUF_START(v, len, p);
     */
    MRAILI_FAST_RDMA_VBUF_START (remote, (*num_bytes_ptr), rstart);

    DEBUG_PRINT ("[send: rdma_send] local vbuf %p, remote vbuf %p\n", v,
                 remote);
    DEBUG_PRINT ("[send: rdma_send] local start %p, remote start %p\n", p,
                 rstart);

    if (++(vc->mrail.rfp.phead_RDMA_send) >= num_rdma_buffer)
        vc->mrail.rfp.phead_RDMA_send = 0;

    v->head_flag = (VBUF_FLAG_TYPE) (*num_bytes_ptr);
    v->subchannel = channel;
    v->padding = BUSY_FLAG;

    /* generate a completion, following statements should have been executed during
     * initialization */
    MRAILI_ALIGN_LEN (*num_bytes_ptr, align_len);
    align_len += VBUF_FAST_RDMA_EXTRA_BYTES;

    DEBUG_PRINT ("[send: rdma_send] lkey %p, rkey %p, len %d, flag %d\n",
                 vc->mrail.rfp.RDMA_send_buf_hndl[channel.hca_index].lkey,
                 vc->mrail.rfp.remote_RDMA_buf_hndl[channel.hca_index].
                 rkey, align_len, v->head_flag);

    VBUF_SET_RDMA_ADDR_KEY (v, align_len,
                            p,
                            vc->mrail.rfp.RDMA_send_buf_hndl[channel.
                                                             hca_index].
                            lkey, rstart,
                            vc->mrail.rfp.remote_RDMA_buf_hndl[channel.
                                                               hca_index].
                            rkey);

    if (!vc->mrail.send_wqes_avail[channel.rail_index])
      {
          DEBUG_PRINT ("[send: rdma_send] Warning! no send wqe available\n");
          MRAILI_Ext_sendq_enqueue (vc, &channel, v);
          *vbuf_handle = v;
          return MPI_MRAIL_MSG_QUEUED;
      }
    else
      {
          vc->mrail.send_wqes_avail[channel.rail_index]--;
          *vbuf_handle = v;
          UDAPL_POST_SR (v, vc, channel, "UDAPL_post_sr (post_fast_rdma)");
          DEBUG_PRINT ("[send:post rdma] desc posted\n");
      }
    MPIDI_FUNC_EXIT(MPIDI_CH3I_MRAILI_FAST_RDMA_SEND_COMPLETE);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Fast_rdma_ok
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Fast_rdma_ok (MPIDI_VC_t * vc, int len)
{
    if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path == 0) {
        return 0;
    }

    MPIDI_STATE_DECL(MPIDI_CH3I_MRAILI_FAST_RDMA_OK);
    MPIDI_FUNC_ENTER(MPIDI_CH3I_MRAILI_FAST_RDMA_OK);

    MPIDI_FUNC_EXIT(MPIDI_CH3I_MRAILI_FAST_RDMA_OK);

    if (num_rdma_buffer < 2)
        return 0;
    if (vc->mrail.rfp.phead_RDMA_send == vc->mrail.rfp.ptail_RDMA_send)
      {
          /* collect the credit in credit array */
          return 0;
      }
    if (vc->mrail.rfp.RDMA_send_buf[vc->mrail.rfp.phead_RDMA_send].padding ==
        BUSY_FLAG)
        return 0;
    if (vc->mrail.srp.backlog.len > 0)
        return 0;
    DEBUG_PRINT ("[send:rdma_ok] return 1\n");
    return 1;
}


#undef FUNCNAME
#define FUNCNAME MRAILI_Post_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MRAILI_Post_send (MPIDI_VC_t * vc, vbuf * v,
                  const MRAILI_Channel_info * channel)
{
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p = v->pheader;

    MPIDI_STATE_DECL(MRAILI_POST_SEND);
    MPIDI_FUNC_ENTER(MRAILI_POST_SEND);

    DEBUG_PRINT
        ("[post send] credit %d,type noop %d, backlog %d, wqe %d, nb will be %d\n",
         vc->mrail.srp.remote_credit[channel->rail_index],
         p->type == MPIDI_CH3_PKT_NOOP, vc->mrail.srp.backlog.len,
         vc->mrail.send_wqes_avail[channel->rail_index],
         v->desc.sg_entry.len);

    if (vc->mrail.srp.remote_credit[channel->rail_index] > 0
        || p->type == MPIDI_CH3_PKT_NOOP)
      {

          /* if we got here, the backlog queue better be  empty 
             MPIU_Assert(vc->mrail.srp.backlog.len == 0
             || p->type == MPIDI_CH3_PKT_NOOP); */

	  PACKET_SET_CREDIT(p, vc, channel->rail_index);
          if (p->type != MPIDI_CH3_PKT_NOOP)
              vc->mrail.srp.remote_credit[channel->rail_index]--;

          v->vc = (void *) vc;

          if (!vc->mrail.send_wqes_avail[channel->rail_index])
            {
                MRAILI_Ext_sendq_enqueue (vc, channel, v);
                MPIDI_FUNC_EXIT(MRAILI_POST_SEND);
                return MPI_MRAIL_MSG_QUEUED;
            }
          vc->mrail.send_wqes_avail[channel->rail_index]--;

          UDAPL_POST_SR (v, vc, (*channel), "UDAPL_post_sr (post_send_desc)");
      }
    else
      {
          udapl_backlog_queue_t *q = &(vc->mrail.srp.backlog);
          BACKLOG_ENQUEUE (q, v);
          MPIDI_FUNC_EXIT(MRAILI_POST_SEND);
          return MPI_MRAIL_MSG_QUEUED;
      }
    MPIDI_FUNC_EXIT(MRAILI_POST_SEND);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MRAILI_Fill_start_buffer
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MRAILI_Fill_start_buffer (vbuf * v, MPID_IOV * iov, int n_iov)
{
    int i;
    int avail = VBUF_BUFFER_SIZE;
    void *ptr = v->buffer;
    int len = 0;

    MPIDI_STATE_DECL(MRAILI_FILL_START_BUFFER);
    MPIDI_FUNC_ENTER(MRAILI_FILL_START_BUFFER);

    for (i = 0; i < n_iov; i++)
      {
          DEBUG_PRINT ("[fill buf]avail %d, len %d\n", avail,
                       iov[i].MPID_IOV_LEN);
          if (avail >= iov[i].MPID_IOV_LEN)
            {
                DEBUG_PRINT ("[fill buf] cpy ptr %p\n", ptr);
                MPIU_Memcpy (ptr, iov[i].MPID_IOV_BUF, (iov[i].MPID_IOV_LEN));
                len += (iov[i].MPID_IOV_LEN);
                avail -= (iov[i].MPID_IOV_LEN);
                ptr = (void *) ((aint_t) ptr + iov[i].MPID_IOV_LEN);
            }
          else
            {
            MPIU_Memcpy (ptr, iov[i].MPID_IOV_BUF, avail);
                len += avail;
                avail = 0;
                break;
            }
      }

    MPIDI_FUNC_EXIT(MRAILI_FILL_START_BUFFER);
    return len;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Eager_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Eager_send (MPIDI_VC_t * vc,
                              MPID_IOV * iov,
                              int n_iov,
                              int pkt_len,
                              int *num_bytes_ptr, vbuf ** buf_handle)
{
    MPIDI_CH3I_MRAILI_Pkt_comm_header *pheader;
    MRAILI_Channel_info channel;
    vbuf *v;
    int  mpi_errno;

    MPIDI_STATE_DECL(MPIDI_CH3I_MRAILI_EAGER_SEND);
    MPIDI_FUNC_ENTER(MPIDI_CH3I_MRAILI_EAGER_SEND);

    /* first we check if we can take the RDMA FP */
    if(MPIDI_CH3I_MRAILI_Fast_rdma_ok(vc, pkt_len)) {
        return MPIDI_CH3I_MRAILI_Fast_rdma_send_complete(vc, iov,
                n_iov, num_bytes_ptr, buf_handle);
    } 

    /* otherwise we can take the send/recv path */
    v = get_vbuf ();
    *buf_handle = v;
    DEBUG_PRINT ("[eager send]vbuf addr %p\n", v);
    *num_bytes_ptr = MRAILI_Fill_start_buffer (v, iov, n_iov);

    /* select channel and send it out */
    MRAILI_Send_select_channel (vc, &channel);
    DEBUG_PRINT ("[eager send] len %d, selected channel hca %d, rail %d\n",
                 *num_bytes_ptr, channel.hca_index, channel.rail_index);
    pheader = v->pheader;

    vbuf_init_send (v, *num_bytes_ptr, &channel);
    /* PACKET_SET_CREDIT (pheader, vc, channel.rail_index); */
    mpi_errno = MRAILI_Post_send (vc, v, &channel);

    MPIDI_FUNC_EXIT(MPIDI_CH3I_MRAILI_EAGER_SEND);
    return mpi_errno;
}

int MPIDI_CH3I_MRAILI_rput_complete(MPIDI_VC_t * vc,
                                 MPID_IOV * iov,
                                 int n_iov,
                                 int *num_bytes_ptr, vbuf ** buf_handle,
                                 int rail)
{
    MPIDI_CH3I_MRAILI_Pkt_comm_header *pheader;
    vbuf *v;
    int  mpi_errno;
    MRAILI_Channel_info channel;

    MPIDI_STATE_DECL(MPIDI_CH3I_MRAILI_EAGER_SEND);
    MPIDI_FUNC_ENTER(MPIDI_CH3I_MRAILI_EAGER_SEND);

    MPIU_Memset(&channel, 0, sizeof(MRAILI_Channel_info));
    channel.rail_index = rail;
    v = get_vbuf();
    *buf_handle = v;
    DEBUG_PRINT("[eager send]vbuf addr %p\n", v);
    *num_bytes_ptr = MRAILI_Fill_start_buffer(v, iov, n_iov);

    DEBUG_PRINT("[eager send] len %d, selected rail hca %d, rail %d\n",
                *num_bytes_ptr, vc->mrail.rails[rail].hca_index, rail);
    pheader = v->pheader;

    vbuf_init_send(v, *num_bytes_ptr, &channel);
    /*PACKET_SET_CREDIT(pheader, vc, channel.rail_index);*/
    mpi_errno = MRAILI_Post_send(vc, v, &channel);

    MPIDI_FUNC_EXIT(MPIDI_CH3I_MRAILI_EAGER_SEND);

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MRAILI_Backlog_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MRAILI_Backlog_send (MPIDI_VC_t * vc, const MRAILI_Channel_info * channel)
{
    MPIDI_STATE_DECL(MRAILI_BACKLOG_SEND);
    MPIDI_FUNC_ENTER(MRAILI_BACKLOG_SEND);

    udapl_backlog_queue_t *q = &vc->mrail.srp.backlog;

    while ((q->len > 0)
           && (vc->mrail.srp.remote_credit[channel->rail_index] > 0))
      {
          vbuf *v = NULL;
          MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
          MPIU_Assert (q->vbuf_head != NULL);
          BACKLOG_DEQUEUE (q, v);

          /* Assumes packet header is at beginning of packet structure */
          p = (MPIDI_CH3I_MRAILI_Pkt_comm_header *) v->pheader;

          PACKET_SET_CREDIT (p, vc, channel->rail_index);
          vc->mrail.srp.remote_credit[channel->rail_index]--;

          v->vc = vc;

          if (!vc->mrail.send_wqes_avail[channel->rail_index])
            {
                MRAILI_Ext_sendq_enqueue (vc, channel, v);
                continue;
            }
          vc->mrail.send_wqes_avail[channel->rail_index]--;

          UDAPL_POST_SR (v, vc, (*channel),
                         "UDAPL_post_sr (viadev_backlog_push)");
      }

    MPIDI_FUNC_EXIT(MRAILI_BACKLOG_SEND);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MRAILI_Process_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MRAILI_Process_send (void *vbuf_addr)
{
    vbuf *v = vbuf_addr;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
    MPIDI_VC_t *vc;
    MPID_Request *req;
    int complete;

    MPIDI_STATE_DECL(MRAILI_PROCESS_SEND);
    MPIDI_FUNC_ENTER(MRAILI_PROCESS_SEND);

    p = v->pheader;
    vc = v->vc;
    vc->mrail.send_wqes_avail[v->subchannel.rail_index]++;

    if (vc->mrail.ext_sendq_head[v->subchannel.rail_index])
      {
          MRAILI_Ext_sendq_send (vc, &v->subchannel);
      }
    DEBUG_PRINT ("after increase 2, %d\n",
                 v->desc.sr.opcode == UDAPL_RDMA_WRITE);

    if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
        if (v->padding == RPUT_VBUF_FLAG)
          {
              MRAILI_Release_vbuf (v);
              MPIDI_FUNC_EXIT(MRAILI_PROCESS_SEND);
              return MPI_SUCCESS;
          }
        if (v->padding == CREDIT_VBUF_FLAG)
          {
              vc->mrail.send_wqes_avail[v->subchannel.rail_index]--;
              MPIDI_FUNC_EXIT(MRAILI_PROCESS_SEND);
              return MPI_SUCCESS;
          }
    } else {
        if (v->desc.opcode == UDAPL_RDMA_WRITE)
          {
              MRAILI_Release_vbuf (v);
              MPIDI_FUNC_EXIT(MRAILI_PROCESS_SEND);
              return MPI_SUCCESS;
          }
    }

    switch (p->type)
      {
#ifndef MV2_DISABLE_HEADER_CACHING 
      case MPIDI_CH3_PKT_FAST_EAGER_SEND:
      case MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ:
#endif
      case MPIDI_CH3_PKT_EAGER_SEND:
      case MPIDI_CH3_PKT_EAGER_SYNC_SEND:
      case MPIDI_CH3_PKT_PACKETIZED_SEND_DATA:
      case MPIDI_CH3_PKT_RNDV_R3_DATA:
      case MPIDI_CH3_PKT_READY_SEND:
      case MPIDI_CH3_PKT_PUT:
      case MPIDI_CH3_PKT_ACCUMULATE:
          req = v->sreq;
          v->sreq = NULL;
          DEBUG_PRINT ("[process send] complete for eager msg, req %p\n",
                       req);
          if (req != NULL)
            {
                MPIDI_CH3U_Handle_send_req (vc, req, &complete);

                DEBUG_PRINT ("[process send] req not null\n");
                if (complete != TRUE)
                  {
                      sleep (10);
                      udapl_error_abort (UDAPL_STATUS_ERR,
                                         "Get incomplete eager send request\n");
                  }
            }

          if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
              if (v->padding == NORMAL_VBUF_FLAG)
                {
                    DEBUG_PRINT ("[process send] normal flag, free vbuf\n");
                    MRAILI_Release_vbuf (v);
                }
              else
                {
                    v->padding = FREE_FLAG;
                }
          } else {
              MRAILI_Release_vbuf (v);   
          }

          break;
      case MPIDI_CH3_PKT_RPUT_FINISH:
          DEBUG_PRINT ("[process send] get rput finish\n");
          req = (MPID_Request *) (v->sreq);
          v->sreq = NULL;
          if (req == NULL)
            {
                udapl_error_abort (GEN_ASSERT_ERR,
                                   "s == NULL, s is the send "
                                   "handler of the rput finish");
            }
          DEBUG_PRINT ("req pointer %p\n", req);
          if (req->mrail.d_entry != NULL)
            {
                dreg_unregister (req->mrail.d_entry);
                req->mrail.d_entry = NULL;
            }
          DEBUG_PRINT ("req pointer1 %p\n", req);
          if (1 == req->mrail.rndv_buf_alloc && NULL != req->mrail.rndv_buf)
            {
                /* we allocated a tmp buf to do rput */
                DEBUG_PRINT ("req pointer2 %p\n", req);
                MPIU_Free (req->mrail.rndv_buf);
                DEBUG_PRINT ("after free %p\n", req);
                req->mrail.rndv_buf = NULL;
                req->mrail.rndv_buf_off = req->mrail.rndv_buf_sz = 0;
                req->mrail.rndv_buf_alloc = 0;
            }
          DEBUG_PRINT ("req pointer3 %p\n", req);
          req->mrail.d_entry = NULL;

          MPIDI_CH3U_Handle_send_req (vc, req, &complete);

          if (complete != TRUE)
            {
                udapl_error_abort (UDAPL_STATUS_ERR,
                                   "Get incomplete eager send request\n");
            }

          if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
              if (v->padding == NORMAL_VBUF_FLAG)
                  MRAILI_Release_vbuf (v);
              else
                  v->padding = FREE_FLAG;
          } else {
              MRAILI_Release_vbuf (v);
          }

          break;
      case MPIDI_CH3_PKT_GET_RESP:
          DEBUG_PRINT ("[process send] get get respond finish\n");
          req = (MPID_Request *) (v->sreq);
          v->sreq = NULL;
          if (NULL != req)
            {
                if (VAPI_PROTOCOL_RPUT == req->mrail.protocol)
                  {
                      if (req->mrail.d_entry != NULL)
                        {
                            dreg_unregister (req->mrail.d_entry);
                            req->mrail.d_entry = NULL;
                        }
                      if (1 == req->mrail.rndv_buf_alloc)
                        {
                            /* we allocated a tmp buf to do rput */
                            MPIU_Free (req->mrail.rndv_buf);
                            req->mrail.rndv_buf = NULL;
                            req->mrail.rndv_buf_off = req->mrail.rndv_buf_sz =
                                0;
                            req->mrail.rndv_buf_alloc = 0;
                        }
                      req->mrail.d_entry = NULL;
                  }
                MPIDI_CH3U_Handle_send_req (vc, req, &complete);
                if (complete != TRUE)
                  {
                      udapl_error_abort (UDAPL_STATUS_ERR,
                                         "Get incomplete eager send request\n");
                  }
            }

          if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
              if (v->padding == NORMAL_VBUF_FLAG)
                  MRAILI_Release_vbuf (v);
              else
                  v->padding = FREE_FLAG;
          } else {
              MRAILI_Release_vbuf (v); 
          }

          break;
      case MPIDI_CH3_PKT_NOOP:
      case MPIDI_CH3_PKT_PACKETIZED_SEND_START:
      case MPIDI_CH3_PKT_RNDV_REQ_TO_SEND:
      case MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND:
      case MPIDI_CH3_PKT_RNDV_CLR_TO_SEND:
      case MPIDI_CH3_PKT_EAGER_SYNC_ACK:
      case MPIDI_CH3_PKT_CANCEL_SEND_REQ:
      case MPIDI_CH3_PKT_CANCEL_SEND_RESP:
      case MPIDI_CH3_PKT_PUT_RNDV:
      case MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND:
      case MPIDI_CH3_PKT_GET:
      case MPIDI_CH3_PKT_GET_RNDV:
      case MPIDI_CH3_PKT_ACCUMULATE_RNDV:
      case MPIDI_CH3_PKT_LOCK:
      case MPIDI_CH3_PKT_LOCK_GRANTED:
      case MPIDI_CH3_PKT_PT_RMA_DONE:
      case MPIDI_CH3_PKT_LOCK_GET_UNLOCK:      /* optimization for single gets */
      case MPIDI_CH3_PKT_ACCUM_IMMED:
      case MPIDI_CH3_PKT_FLOW_CNTL_UPDATE:
      case MPIDI_CH3_PKT_CLOSE:        /*24 */
          DEBUG_PRINT ("[process send] get %d\n", p->type);

          if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
              if (v->padding == NORMAL_VBUF_FLAG)
                {
                    MRAILI_Release_vbuf (v);
                }
              else
                  v->padding = FREE_FLAG;
          } else {
              MRAILI_Release_vbuf (v);
          }

          break;
   case MPIDI_CH3_PKT_LOCK_PUT_UNLOCK: /* optimization for single puts */
   case MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK: /* optimization for single accumulates */
        req = (MPID_Request *) v->sreq;
        if (NULL != req) { 
          MPID_Request_set_completed(req);
        }
        if (v->padding == NORMAL_VBUF_FLAG) {
            MRAILI_Release_vbuf(v);
        }
        else v->padding = FREE_FLAG;
        break;

      default:
          dump_vbuf ("unknown packet (send finished)", v);
          udapl_error_abort (UDAPL_STATUS_ERR,
                             "Unknown packet type %d in "
                             "viadev_process_send", p->type);
      }
    DEBUG_PRINT ("return from process send\n");

    MPIDI_FUNC_EXIT(MRAILI_PROCESS_SEND);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MRAILI_Send_noop
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MRAILI_Send_noop (MPIDI_VC_t * c, const MRAILI_Channel_info * channel)
{
    /* always send a noop when it is needed even if there is a backlog.
     * noops do not consume credits.
     * this is necessary to avoid credit deadlock.
     * RNR NAK will protect us if receiver is low on buffers.
     * by doing this we can force a noop ahead of any other queued packets.
     */

    vbuf *v;
    MPIDI_CH3I_MRAILI_Pkt_noop *p;

    MPIDI_STATE_DECL(MRAILI_SEND_NOOP);
    MPIDI_FUNC_ENTER(MRAILI_SEND_NOOP);


    v = get_vbuf ();
    p = (MPIDI_CH3I_MRAILI_Pkt_noop *) v->pheader;

    p->type = MPIDI_CH3_PKT_NOOP;
    /* PACKET_SET_CREDIT (p, c, channel->rail_index); */
    vbuf_init_send (v, sizeof (MPIDI_CH3I_MRAILI_Pkt_noop), channel);
    MRAILI_Post_send (c, v, channel);

    MPIDI_FUNC_EXIT(MRAILI_SEND_NOOP);
}

#undef FUNCNAME
#define FUNCNAME MRAILI_Send_noop_if_needed
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MRAILI_Send_noop_if_needed (MPIDI_VC_t * vc,
                            const MRAILI_Channel_info * channel)
{
    MPIDI_STATE_DECL(MRAILI_SEND_NOOP_IF_NEEDED);
    MPIDI_FUNC_ENTER(MRAILI_SEND_NOOP_IF_NEEDED);

    DEBUG_PRINT ("local credit %d, rdma redit %d\n",
                 vc->mrail.srp.local_credit[channel->rail_index],
                 vc->mrail.rfp.rdma_credit);
    if (vc->mrail.srp.local_credit[channel->rail_index] >=
        udapl_dynamic_credit_threshold
        || (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path 
            && vc->mrail.rfp.rdma_credit > num_rdma_buffer / 2)
        || (vc->mrail.srp.remote_cc[channel->rail_index] <=
            udapl_credit_preserve
            && vc->mrail.srp.local_credit[channel->rail_index] >=
            udapl_credit_notify_threshold))
      {
          MRAILI_Send_noop (vc, channel);
      }

    MPIDI_FUNC_EXIT(MRAILI_SEND_NOOP_IF_NEEDED);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MRAILI_RDMA_Put
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MRAILI_RDMA_Put (MPIDI_VC_t * vc, vbuf * v,
                 char *local_addr, VIP_MEM_HANDLE local_hndl,
                 char *remote_addr, VIP_MEM_HANDLE remote_hndl,
                 int nbytes, MRAILI_Channel_info * subchannel)
{
    MPIDI_STATE_DECL(MRAILI_RDMA_PUT);
    MPIDI_FUNC_ENTER(MRAILI_RDMA_PUT);

#ifdef UDAPL_HAVE_RDMA_LIMIT
    while (viadev.outstanding_rdmas >= viadev_rdma_limit)
        MPID_DeviceCheck (MPID_BLOCKING);
    viadev.outstanding_rdmas++;
#endif
    DEBUG_PRINT("viadev_rput: RDMA write, remote addr %p, rkey %p, nbytes %d, hca %d\n",
                remote_addr, rkey, nbytes, subchannel->hca_index);

    vbuf_init_rput (v, (void *) local_addr, local_hndl, remote_addr,
                    remote_hndl, nbytes, subchannel);

    v->vc = (void *) vc;

    if (!vc->mrail.send_wqes_avail[subchannel->rail_index])
      {
          MRAILI_Ext_sendq_enqueue (vc, subchannel, v);
          return;
      }
    vc->mrail.send_wqes_avail[subchannel->rail_index]--;

    UDAPL_POST_SR (v, vc, (*subchannel), "viadev_post_rdmawrite");

    MPIDI_FUNC_EXIT(MRAILI_RDMA_PUT);
}
