/* -*- Mode: C; c-basic-offset:4 ; -*- */
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
#include "rdma_impl.h"

#undef FUNCNAME
#define FUNCNAME create_eagercontig_request
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline MPID_Request * create_eagercontig_request(MPIDI_VC_t * vc,
                         MPIDI_CH3_Pkt_type_t reqtype,
                         const void * buf, MPIDI_msg_sz_t data_sz, int rank,
                         int tag, MPID_Comm * comm, int context_offset)
{
    MPID_Request * sreq;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_eager_send_t * const eager_pkt = &upkt.eager_send;
    MPIDI_STATE_DECL(MPID_STATE_CREATE_EAGERCONTIG_REQUEST);
    MPIDI_FUNC_ENTER(MPID_STATE_CREATE_EAGERCONTIG_REQUEST);
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */

    MPIDI_Pkt_init(eager_pkt, reqtype);
    eager_pkt->match.parts.rank = comm->rank;
    eager_pkt->match.parts.tag  = tag;
    eager_pkt->match.parts.context_id   = comm->context_id + context_offset;
    eager_pkt->sender_req_id    = MPI_REQUEST_NULL;
    eager_pkt->data_sz      = data_sz;

    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);
    MPIU_DBG_MSGPKT(vc,tag,eager_pkt->match.parts.context_id,rank,data_sz,"EagerContig");
    sreq = MPID_Request_create();
    /* --BEGIN ERROR HANDLING-- */
    if (sreq == NULL)
        return NULL;
    /* --END ERROR HANDLING-- */
    MPIU_Object_set_ref(sreq, 2);
    sreq->kind = MPID_REQUEST_SEND;

    sreq->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)eager_pkt;
    sreq->dev.iov[0].MPID_IOV_LEN = sizeof(*eager_pkt);
    MPIU_DBG_MSG_FMT(CH3_OTHER,VERBOSE,(MPIU_DBG_FDEST,
                "sending smp contiguous eager message, data_sz=" 
                MPIDI_MSG_SZ_FMT, data_sz));
    sreq->dev.iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) buf;
    sreq->dev.iov[1].MPID_IOV_LEN = data_sz;
    sreq->dev.pending_pkt = *(MPIDI_CH3_PktGeneric_t *) sreq->dev.iov[0].MPID_IOV_BUF;
    sreq->dev.iov[0].MPID_IOV_BUF = (void *)&sreq->dev.pending_pkt;
    sreq->ch.reqtype = REQUEST_NORMAL;
    sreq->dev.iov_offset = 0;
    sreq->dev.iov_count = 2;
    sreq->dev.OnDataAvail = 0;

    MPIDI_Request_set_seqnum(sreq, seqnum);
    MPIDI_Request_set_type(sreq, MPIDI_REQUEST_TYPE_SEND);
    
    MPIDI_FUNC_EXIT(MPID_STATE_CREATE_EAGERCONTIG_REQUEST);
    return sreq;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SMP_ContigSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3_SMP_ContigSend(MPIDI_VC_t * vc,
                MPID_Request **sreq_p, MPIDI_CH3_Pkt_type_t reqtype,
                const void * buf, MPIDI_msg_sz_t data_sz, int rank,
                int tag, MPID_Comm * comm, int context_offset)
{
    MPID_Request *sreq = NULL;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_SMP_CONTIGSEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_SMP_CONTIGSEND);

    /* If send queue is empty attempt to send
       data, queuing any unsent data. */
    if (MPIDI_CH3I_SMP_SendQ_empty(vc)) {
        int nb;
        /* MT - need some signalling to lock down our right to use the
           channel, thus insuring that the progress engine does also try to
           write */
        MPIDI_CH3I_SMP_write_contig(vc, reqtype, buf, data_sz, rank, 
                tag, comm, context_offset, &nb);
        DEBUG_PRINT("ch3_smp_contigsend: writev returned %d bytes\n", nb);

        /* send all or NULL */
        if( !nb ) {
            /* no available shared memory buffer, enqueue request, fallback to
             * MPIDI_CH3_PKT_EAGER_SEND */
            sreq = create_eagercontig_request(vc, MPIDI_CH3_PKT_EAGER_SEND, buf, data_sz, rank, tag,
                    comm, context_offset);
            if (sreq == NULL) {
                MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|contigsend");
            }
            MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        }
    } else {
        /* sendQ not empty, enqueue request, fallback MPIDI_CH3_PKT_EAGER_SEND */
        sreq = create_eagercontig_request(vc, MPIDI_CH3_PKT_EAGER_SEND, buf, data_sz, rank, tag,
                comm, context_offset);
        if (sreq == NULL) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|contigsend");
        }
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
    }

    *sreq_p = sreq;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_SMP_CONTIGSEND);
fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_ContigSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_ContigSend(MPID_Request **sreq_p,
                         MPIDI_CH3_Pkt_type_t reqtype,
                         const void * buf, MPIDI_msg_sz_t data_sz, int rank,
                         int tag, MPID_Comm * comm, int context_offset)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_CONTIGSEND);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_CONTIGSEND);

    MPIDI_VC_t * vc;
    MPIDI_Comm_get_vc_set_active(comm, rank, &vc);

#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    if (SMP_INIT && vc->smp.local_nodes >= 0 &&
            vc->smp.local_nodes != g_smpi.my_local_id)
    {
        MPIU_THREAD_CS_ENTER(CH3COMM,vc);
        if(MPIDI_CH3_SMP_ContigSend(vc, sreq_p, reqtype, 
                    buf, data_sz, rank, tag, comm, context_offset)) {
            MPIU_THREAD_CS_EXIT(CH3COMM,vc);
#ifdef CKPT
            MPIDI_CH3I_CR_unlock();
#endif
            return 1;
        }
        MPIU_THREAD_CS_EXIT(CH3COMM,vc);
#ifdef CKPT
        MPIDI_CH3I_CR_unlock();
#endif
        return 0;
    }

#ifdef CKPT
    MPIDI_CH3I_CR_unlock();
#endif
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_CONTIGSEND);
    return 1;
}
