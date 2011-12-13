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

extern int rts_send;
extern int cts_recv;

int
MPIDI_CH3I_MRAIL_Prepare_rndv (MPIDI_VC_t * vc, MPID_Request * req)
{
    dreg_entry *reg_entry;
    DEBUG_PRINT ("[prepare cts] rput protocol, recv size %d, segsize %d, io count %d\n",
        rreq->dev.recv_data_sz, req->dev.segment_size, req->dev.iov_count);

    req->mrail.protocol = VAPI_PROTOCOL_RPUT;
    /* Step 1: ready for user space (user buffer or pack) */
    if (1 == req->dev.iov_count && (req->dev.OnDataAvail == NULL ||
       (req->dev.OnDataAvail == req->dev.OnFinal) ||
       (req->dev.OnDataAvail == MPIDI_CH3_ReqHandler_UnpackSRBufComplete))) 
      {
          req->mrail.rndv_buf = req->dev.iov[0].MPID_IOV_BUF;
          req->mrail.rndv_buf_sz = req->dev.iov[0].MPID_IOV_LEN;
          req->mrail.rndv_buf_alloc = 0;
      }
    else
      {
          req->mrail.rndv_buf_sz = req->dev.segment_size;
          req->mrail.rndv_buf = MPIU_Malloc (req->mrail.rndv_buf_sz);

          if (req->mrail.rndv_buf == NULL)
            {
                /* fall back to r3 if cannot allocate tmp buf */
                DEBUG_PRINT ("[rndv sent] set info: cannot allocate space\n");
                req->mrail.protocol = VAPI_PROTOCOL_R3;
                req->mrail.rndv_buf_sz = 0;
            }
          else
            {
                req->mrail.rndv_buf_alloc = 1;
            }
      }
    req->mrail.rndv_buf_off = 0;

    /* Step 2: try register and decide the protocol */
    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol)
      {
          DEBUG_PRINT ("[cts] size registered %d, addr %p\n",
                       req->mrail.rndv_buf_sz, req->mrail.rndv_buf);
          reg_entry =
              dreg_register (req->mrail.rndv_buf, req->mrail.rndv_buf_sz);
          if (NULL == reg_entry)
            {
                req->mrail.protocol = VAPI_PROTOCOL_R3;
                if (1 == req->mrail.rndv_buf_alloc)
                  {
                      MPIU_Free (req->mrail.rndv_buf);
                      req->mrail.rndv_buf_alloc = 0;
                      req->mrail.rndv_buf_sz = 0;
                      req->mrail.rndv_buf = NULL;
                  }
                req->mrail.rndv_buf_alloc = 0;
                /*MRAILI_Prepost_R3(); */
            }
          DEBUG_PRINT ("[prepare cts] register success\n");
      }

    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol)
      {
          req->mrail.d_entry = reg_entry;
          return 1;
      }
    else
        return 0;
}

int
MPIDI_CH3I_MRAIL_Prepare_rndv_transfer (MPID_Request * sreq,    /* contains local info */
                                        MPIDI_CH3I_MRAILI_Rndv_info_t * rndv)
{
    if (rndv->protocol == VAPI_PROTOCOL_R3)
      {
          if (sreq->mrail.d_entry != NULL)
            {
                dreg_unregister (sreq->mrail.d_entry);
                sreq->mrail.d_entry = NULL;
            }
          if (1 == sreq->mrail.rndv_buf_alloc && NULL != sreq->mrail.rndv_buf)
            {
                MPIU_Free (sreq->mrail.rndv_buf);
                sreq->mrail.rndv_buf_alloc = 0;
                sreq->mrail.rndv_buf = NULL;
            }
          sreq->mrail.remote_addr = NULL;
          sreq->mrail.remote_handle.hndl = DAT_HANDLE_NULL;
          sreq->mrail.protocol = VAPI_PROTOCOL_R3;
      }
    else
      {
          sreq->mrail.remote_addr = rndv->buf_addr;
          sreq->mrail.remote_handle = rndv->memhandle;

          DEBUG_PRINT ("[add rndv list] addr %p, key %p\n",
                       sreq->mrail.remote_addr,
                       sreq->mrail.remote_handle.rkey);
          if (1 == sreq->mrail.rndv_buf_alloc)
            {
                int mpi_errno = MPI_SUCCESS;
                int i;
                aint_t buf;

                buf = (aint_t) sreq->mrail.rndv_buf;
                for (i = 0; i < sreq->dev.iov_count; i++)
                  {
                  MPIU_Memcpy ((void *) buf, sreq->dev.iov[i].MPID_IOV_BUF,
                              sreq->dev.iov[i].MPID_IOV_LEN);
                      buf += sreq->dev.iov[i].MPID_IOV_LEN;
                  }
                /* TODO: Following part is a workaround to deal with datatype with large number
                 * of segments. We check if the datatype has finished loading and reload if not.
                 * May be better interface with upper layer should be considered*/
                while (sreq->dev.OnDataAvail == MPIDI_CH3_ReqHandler_SendReloadIOV) 
                  {
                      sreq->dev.iov_count = MPID_IOV_LIMIT;
                      mpi_errno =
                          MPIDI_CH3U_Request_load_send_iov (sreq,
                                                            sreq->dev.iov,
                                                            &sreq->dev.
                                                            iov_count);
                      /* --BEGIN ERROR HANDLING-- */
                      if (mpi_errno != MPI_SUCCESS)
                        {
                            udapl_error_abort (UDAPL_STATUS_ERR,
                                               "Reload iov error");
                        }
                      for (i = 0; i < sreq->dev.iov_count; i++)
                        {
                        MPIU_Memcpy ((void *) buf,
                                    sreq->dev.iov[i].MPID_IOV_BUF,
                                    sreq->dev.iov[i].MPID_IOV_LEN);
                            buf += sreq->dev.iov[i].MPID_IOV_LEN;
                        }
                  }
            }
      }
    return MPI_SUCCESS;
}

void
MRAILI_RDMA_Put_finish (MPIDI_VC_t * vc, MPID_Request * sreq,
                        MRAILI_Channel_info * subchannel)
{
    MPIDI_CH3_Pkt_rput_finish_t rput_pkt;
    MPID_IOV iov;
    int n_iov = 1;
    int nb, rdma_ok;
    int mpi_errno = MPI_SUCCESS;
    int seqnum;

    vbuf *buf;

    rput_pkt.type = MPIDI_CH3_PKT_RPUT_FINISH;
    rput_pkt.receiver_req_id = sreq->mrail.partner_id;
    iov.MPID_IOV_BUF = (void *)&rput_pkt;
    iov.MPID_IOV_LEN = sizeof (MPIDI_CH3_Pkt_rput_finish_t);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(&rput_pkt, seqnum);

    if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
        rdma_ok =
            MPIDI_CH3I_MRAILI_Fast_rdma_ok (vc,
                                            sizeof (MPIDI_CH3_Pkt_rput_finish_t));
        if (rdma_ok)
          {
              /* the packet header and the data now is in rdma fast buffer */
              mpi_errno =
                  MPIDI_CH3I_MRAILI_Fast_rdma_send_complete (vc, &iov, n_iov, &nb,
                                                             &buf);
              if (mpi_errno != MPI_SUCCESS && mpi_errno != MPI_MRAIL_MSG_QUEUED)
                {
                    udapl_error_abort (UDAPL_STATUS_ERR,
                                       "Cannot send rput through rdma fast path");
                }
         } else {
                  mpi_errno =
                      MPIDI_CH3I_MRAILI_Eager_send (vc, &iov, n_iov,
                                                    sizeof (MPIDI_CH3_Pkt_rput_finish_t),
                                                    &nb, &buf);
                  if (mpi_errno != MPI_SUCCESS && mpi_errno != MPI_MRAIL_MSG_QUEUED)
                    {
                        udapl_error_abort (UDAPL_STATUS_ERR,
                                           "Cannot send rput through send/recv path");
                    }
         }
    
    } else
      {
          mpi_errno =
              MPIDI_CH3I_MRAILI_Eager_send (vc, &iov, n_iov, 
                                            sizeof (MPIDI_CH3_Pkt_rput_finish_t), 
                                            &nb, &buf);
          if (mpi_errno != MPI_SUCCESS && mpi_errno != MPI_MRAIL_MSG_QUEUED)
            {
                udapl_error_abort (UDAPL_STATUS_ERR,
                                   "Cannot send rput through send/recv path");
            }

      }
    /* mark MPI send complete when VIA send completes */
    buf->sreq = (void *) sreq;
}

void
MPIDI_CH3I_MRAILI_Rendezvous_rget_push (MPIDI_VC_t * vc, MPID_Request * rreq)
{
    /* RDMA Read support for uDAPL not implemented */
    udapl_error_abort (UDAPL_STATUS_ERR,
            "RDMA Read not supported for uDAPL");
}

void
MPIDI_CH3I_MRAILI_Rendezvous_rput_push (MPIDI_VC_t * vc, MPID_Request * sreq)
{
    vbuf *v;
    int i = 0;
    MRAILI_Channel_info channel;

    channel.hca_index = 0;
    channel.rail_index = 0;
    int nbytes;

    if (sreq->mrail.rndv_buf_off != 0)
      {
          udapl_error_abort (GEN_ASSERT_ERR,
                             "s->bytes_sent != 0 Rendezvous Push, %d",
                             sreq->mrail.nearly_complete);
      }

#if defined(DEBUG)
    if (sreq->mrail.rndv_buf_sz > 0)
    {
          MPIU_Assert (sreq->mrail.d_entry != NULL);
          MPIU_Assert (sreq->mrail.remote_addr != NULL);
    }
#endif /* defined(DEBUG) */

    while (sreq->mrail.rndv_buf_off < sreq->mrail.rndv_buf_sz)
      {
          v = get_vbuf ();
          nbytes = sreq->mrail.rndv_buf_sz - sreq->mrail.rndv_buf_off;
          if (nbytes > MPIDI_CH3I_RDMA_Process.maxtransfersize)
            {
                nbytes = MPIDI_CH3I_RDMA_Process.maxtransfersize;
            }
            DEBUG_PRINT("[buffer content]: %02x,%02x,%02x, offset %d, remote buf %p\n",
                       ((char *) sreq->mrail.rndv_buf)[0],
                       ((char *) sreq->mrail.rndv_buf)[1],
                       ((char *) sreq->mrail.rndv_buf)[2],
                       sreq->mrail.rndv_buf_off, sreq->mrail.remote_addr);
          MRAILI_RDMA_Put (vc, v,
                           (char *) (sreq->mrail.rndv_buf) +
                           sreq->mrail.rndv_buf_off,
                           ((dreg_entry *) sreq->mrail.d_entry)->memhandle,
                           (char *) (sreq->mrail.remote_addr) +
                           sreq->mrail.rndv_buf_off,
                           sreq->mrail.remote_handle, nbytes, &channel);
          sreq->mrail.rndv_buf_off += nbytes;
      }

    MPIU_Assert (sreq->mrail.rndv_buf_off == sreq->mrail.rndv_buf_sz);

    for (; i < vc->mrail.num_total_subrails; ++i)
      {
          /*Fix ME: only support one channel */
          channel.rail_index = i;
          MRAILI_RDMA_Put_finish (vc, sreq, &channel);
          break;
      }
    sreq->mrail.nearly_complete = 1;
}

int MPIDI_CH3I_MRAIL_Finish_request(MPID_Request *rreq)
{
    return 1;
}

