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

#include "psmpriv.h"

/* 
   iov[0] contains the packet information which includes tag/destrank/context_id
   to use. Extract iov[0], cast it to a packet type and use it */

#undef FUNCNAME
#define FUNCNAME psm_istartmsgv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_istartmsgv(MPIDI_VC_t *vc, MPID_IOV *iov, int iov_n, MPID_Request **rptr)
{
    MPIDI_CH3_Pkt_t *genpkt;
    void *buf;
    int buflen, psmerr, mpi_errno = MPI_SUCCESS;
    
    assert(iov_n > 0);
    genpkt = (MPIDI_CH3_Pkt_t *) iov[0].MPID_IOV_BUF;

    switch(genpkt->type) {
        case MPIDI_CH3_PKT_PUT: {
            /* packet handlers expect generic packet size */
            MPIDI_CH3_Pkt_put_t *putpkt = (MPIDI_CH3_Pkt_put_t *) genpkt;
            iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
            DBG("mpi put to %d\n", putpkt->mapped_trank);
            mpi_errno = psm_1sided_putpkt(putpkt, iov, iov_n, rptr); //ODOT: err
            goto fn_exit;
        }
        case MPIDI_CH3_PKT_ACCUMULATE: {
            MPIDI_CH3_Pkt_accum_t *acpkt = (MPIDI_CH3_Pkt_accum_t *) genpkt;
            iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
            DBG("mpi accum to %d\n", acpkt->mapped_trank);
            mpi_errno = psm_1sided_accumpkt(acpkt, iov, iov_n, rptr); //ODOT: error handle
            goto fn_exit;
        }
        case MPIDI_CH3_PKT_GET_RESP: {
            MPIDI_CH3_Pkt_get_resp_t *resppkt = (MPIDI_CH3_Pkt_get_resp_t *) genpkt;
            iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
            DBG("mpi response for get from %d\n", resppkt->mapped_trank);
            (*rptr)->pkbuf = vc;
            mpi_errno = psm_1sided_getresppkt(resppkt, iov, iov_n, rptr);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            goto fn_exit;
        }
        case MPIDI_CH3_PKT_GET: {
            MPIDI_CH3_Pkt_get_t *getpkt = (MPIDI_CH3_Pkt_get_t *) genpkt;
            iov[0].MPID_IOV_LEN = sizeof(MPIDI_CH3_Pkt_t);
            DBG("mpi get to %d\n", getpkt->mapped_trank);
            mpi_errno = psm_1sided_getpkt(getpkt, iov, iov_n, rptr);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            if(getpkt->rndv_mode) {
                mpi_errno = psm_1sc_get_rndvrecv((*rptr), (MPIDI_CH3_Pkt_t *)getpkt,
                        getpkt->mapped_trank);
            }
            goto fn_exit;
        }

        default: {
            DBG("sending packet type %d\n", genpkt->type);
            if(genpkt->type != MPIDI_CH3_PKT_EAGER_SEND) {
                PSM_ERR_ABORT("unknown control packet type %d\n", genpkt->type);
            }                
            assert(genpkt->type == MPIDI_CH3_PKT_EAGER_SEND);                                     
            break;                                        
        }
    }

    /* 2-sided packet handling */
    MPIDI_CH3_Pkt_send_t *pkt;

    pkt = (MPIDI_CH3_Pkt_send_t *) iov[0].MPID_IOV_BUF;
    buf = (void *) iov[1].MPID_IOV_BUF;
    buflen = iov[1].MPID_IOV_LEN;
    psmerr = psm_send_pkt(rptr, pkt->match, vc->pg_rank, buf, buflen);
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
    }

fn_exit:
    if(unlikely(mpi_errno != MPI_SUCCESS)) {
        MPIU_ERR_SET(mpi_errno, MPI_ERR_INTERN, "**fail");        
    }

fn_fail:    
    return mpi_errno;

}

#undef FUNCNAME
#define FUNCNAME psm_istartmsg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_istartmsg(MPIDI_VC_t *vc, void *upkt, MPIDI_msg_sz_t pkt_sz, MPID_Request **rptr)
{
    MPIDI_CH3_Pkt_t* genpkt = upkt;
    void *buf;
    int buflen, psmerr, src, trank;
    int mpi_errno = MPI_SUCCESS;

    buf = upkt;
    buflen = pkt_sz;

    switch(genpkt->type) {
        case MPIDI_CH3_PKT_GET:
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_get_t *) genpkt)->mapped_srank;
            (*rptr)->psm_flags |= PSM_GETPKT_REQ;
            MPIU_Object_add_ref((*rptr));
            trank = ((MPIDI_CH3_Pkt_get_t *) genpkt)->mapped_trank;
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, trank, buf, buflen, src, 0);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            if(((MPIDI_CH3_Pkt_get_t *) genpkt)->rndv_mode) {
                DBG("rndv msg length %d\n", ((MPIDI_CH3_Pkt_get_t *)
                                              genpkt)->rndv_len);
                mpi_errno = psm_1sc_get_rndvrecv((*rptr), genpkt, trank);
            } else {
                ++psm_tot_eager_gets;
            }
            break;

        case MPIDI_CH3_PKT_PUT:
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_put_t *) genpkt)->mapped_srank;
            trank = ((MPIDI_CH3_Pkt_put_t *) genpkt)->mapped_trank;
            ++psm_tot_eager_puts;
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, trank, buf, buflen, src, 1);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            break;

        case MPIDI_CH3_PKT_ACCUM_IMMED: 
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_accum_immed_t *) genpkt)->mapped_srank;
            trank = ((MPIDI_CH3_Pkt_accum_immed_t *) genpkt)->mapped_trank;
            ++psm_tot_eager_accs;
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, trank, buf, buflen, src, 1);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            break;

        case MPIDI_CH3_PKT_LOCK:
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_lock_t *) genpkt)->mapped_srank;
            trank = ((MPIDI_CH3_Pkt_lock_t *) genpkt)->mapped_trank;
            DBG("Sending LOCK packet to %d from %d\n", trank, src);
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, trank, buf, buflen, src, 1);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            break;

        case MPIDI_CH3_PKT_LOCK_GRANTED:
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_lock_granted_t *) genpkt)->mapped_srank;
            trank = ((MPIDI_CH3_Pkt_lock_granted_t *) genpkt)->mapped_trank;
            DBG("Sending LOCK_GRANTED packet to %d from %d\n", trank, src);
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, trank, buf, buflen, src, 1);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            break;

        case MPIDI_CH3_PKT_PT_RMA_DONE:
            buflen = sizeof(MPIDI_CH3_Pkt_t);
            src = ((MPIDI_CH3_Pkt_pt_rma_done_t *) genpkt)->source_rank;
            mpi_errno = psm_send_1sided_ctrlpkt(rptr, vc->pg_rank, buf, buflen, src, 1);
            if(unlikely(mpi_errno != MPI_SUCCESS)) {
                MPIU_ERR_POP(mpi_errno);
            }
            break;

        default: {
            DBG("sending packet type %d\n", genpkt->type);
            if(genpkt->type != MPIDI_CH3_PKT_EAGER_SEND) {
                PSM_ERR_ABORT("unknown control packet type %d\n", genpkt->type);
            }                
            assert(genpkt->type == MPIDI_CH3_PKT_EAGER_SEND);                                     

            MPIDI_CH3_Pkt_send_t *pkt = (MPIDI_CH3_Pkt_send_t *) genpkt;
            psmerr = psm_send_pkt(rptr, pkt->match, vc->pg_rank, buf, buflen);
            if(unlikely(psmerr != PSM_OK)) {
                mpi_errno = psm_map_error(psmerr);
            }
            break;
        }
    }
    
fn_exit:
    if(unlikely(mpi_errno != MPI_SUCCESS)) {
        MPIU_ERR_POP(mpi_errno);
    }
    return mpi_errno;
fn_fail:    
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME psm_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_send(MPIDI_VC_t *vc, MPIDI_Message_match match, MPID_Request *sreq)
{
    int psmerr;
    int mpi_errno = MPI_SUCCESS;

    DBG("simple PSM_Send issued\n");
    psmerr = psm_send_pkt(&sreq, match, vc->pg_rank, sreq->pkbuf, sreq->pksz);
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME psm_isend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_isend(MPIDI_VC_t *vc, MPIDI_Message_match match, MPID_Request *sreq)
{
    int psmerr;
    int mpi_errno = MPI_SUCCESS;

    DBG("simple non-blocking PSM_Send issued\n");
    psmerr = psm_isend_pkt(sreq, match, vc->pg_rank, sreq->pkbuf, 
                           sreq->pksz);
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME psm_isendv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_isendv(MPIDI_VC_t *vc, MPID_IOV *iov, int iov_n, MPID_Request *rptr)
{
    MPIDI_CH3_Pkt_send_t *pkt;
    void *buf;
    int buflen, psmerr, mpi_errno = MPI_SUCCESS;

    assert(iov_n > 0);
    pkt = (MPIDI_CH3_Pkt_send_t *) iov[0].MPID_IOV_BUF;
    buf = (void *) iov[1].MPID_IOV_BUF;
    buflen = iov[1].MPID_IOV_LEN;

    psmerr = psm_isend_pkt(rptr, pkt->match, vc->pg_rank, buf, buflen);
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:    
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME psm_send_noncontig
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_send_noncontig(MPIDI_VC_t *vc, MPID_Request *sreq, 
                       MPIDI_Message_match match)
{
    int mpi_errno, inuse;

    if(sreq->psm_flags & PSM_NON_BLOCKING_SEND) {
        mpi_errno = psm_isend(vc, match, sreq);
        if(unlikely(mpi_errno != MPI_SUCCESS)) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    else {
        mpi_errno = psm_send(vc, match, sreq);
        if(unlikely(mpi_errno != MPI_SUCCESS)) {
            MPIU_ERR_POP(mpi_errno);
        }

        if(!(sreq->psm_flags & PSM_NON_BLOCKING_SEND)) {
            *(sreq->cc_ptr) = 0;
            MPIU_Object_release_ref(sreq, &inuse);
        }
    }

fn_fail:
    return mpi_errno;
}
#undef FUNCNAME
#define FUNCNAME psm_map_error
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_map_error(psm_error_t psmerr) 
{
    if(psmerr == PSM_OK) {
        return MPI_SUCCESS;
    }

    fprintf(stderr, "psm error: %s\n", psm_error_get_string(psmerr));
    fflush(stderr);
    return MPI_ERR_INTERN;
}
