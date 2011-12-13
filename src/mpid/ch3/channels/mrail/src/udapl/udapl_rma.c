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

int
MPIDI_CH3I_MRAILI_Get_rndv_rput (MPIDI_VC_t * vc,
                                 MPID_Request * req,
                                 MPIDI_CH3I_MRAILI_Rndv_info_t * rndv,
                                 MPID_IOV *iov)
{
    /* This function will register the local buf, send rdma write to target, and send
     * get_resp_kt as rput finsh. Currently, we assume the local buffer is contiguous,
     * datatype cases will be considered later */
    int nbytes;
    dreg_entry *reg_entry;
    MRAILI_Channel_info channel;
    vbuf *v;
    int mpi_errno = MPI_SUCCESS;

    channel.hca_index = 0;
    channel.rail_index = 0;

    MPIDI_CH3I_MRAIL_Prepare_rndv (vc, req);

    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol)
      {
          MPIDI_CH3I_MRAIL_Prepare_rndv_transfer (req, rndv);
      }

    /* STEP 2: Push RDMA write */
    while ((req->mrail.rndv_buf_off < req->mrail.rndv_buf_sz)
           && VAPI_PROTOCOL_RPUT == req->mrail.protocol)
      {
          v = get_vbuf ();
          MPIU_Assert (v != NULL);
          nbytes = req->mrail.rndv_buf_sz - req->mrail.rndv_buf_off;
          if (nbytes > MPIDI_CH3I_RDMA_Process.maxtransfersize)
            {
                nbytes = MPIDI_CH3I_RDMA_Process.maxtransfersize;
            }
          DEBUG_PRINT ("[buffer content]: offset %d\n",
                       sreq->mrail.rndv_buf_off);
          MRAILI_RDMA_Put (vc, v,
                           (char *) (req->mrail.rndv_buf) +
                           req->mrail.rndv_buf_off,
                           ((dreg_entry *) req->mrail.d_entry)->memhandle,
                           (char *) (req->mrail.remote_addr) +
                           req->mrail.rndv_buf_off, req->mrail.remote_handle,
                           nbytes, &channel);
          req->mrail.rndv_buf_off += nbytes;
      }

    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol) {
        MPIDI_CH3I_MRAILI_rput_complete(vc, iov, 1, &nbytes, &v, channel.rail_index);
        v->sreq = req;
    }

    return MPI_SUCCESS;
}
