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

#undef FUNCNAME
#define FUNCNAME psm_recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_recv(int src, int tag, int context_id, void *buf, int buflen,
             MPI_Status *stat, MPID_Request **request)
{
    uint64_t rtag, rtagsel;
    psm_error_t psmerr;
    MPID_Request *req = *request;
    int mpi_errno = MPI_SUCCESS;

    if(unlikely(buf == NULL && buflen > 0)) {
        MPIU_ERR_SET(mpi_errno, MPI_ERR_BUFFER, "**fail");
        goto fn_fail;
    }

    rtag = 0;
    rtagsel = MQ_TAGSEL_ALL;
    MAKE_PSM_SELECTOR(rtag, context_id, tag, src);
    if(unlikely(src == MPI_ANY_SOURCE))
        rtagsel = MQ_TAGSEL_ANY_SOURCE;
    if(unlikely(tag == MPI_ANY_TAG))
        rtagsel = rtagsel & MQ_TAGSEL_ANY_TAG;
       
    if(!req) {
        req = psm_create_req();
        if(unlikely(req == NULL)) {
            MPIU_ERR_SET(mpi_errno, MPI_ERR_NO_MEM, "**nomem");
            goto fn_fail;
         }
        *request = req;
    }

    req->kind = MPID_REQUEST_RECV;

    DBG("psm_irecv: expecting data from %d, tag = %d\n", src, tag);
    _psm_enter_;
    psmerr = psm_mq_irecv(psmdev_cw.mq, rtag, rtagsel, MQ_FLAGS_NONE, buf,
            buflen, req, &(req->mqreq));
    _psm_exit_;
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        goto fn_fail;
    }
   
    /* we cannot do a blocking recv, poke prog-engine till this req completes */
    mpi_errno = psm_try_complete(req);
    if(unlikely(mpi_errno != MPI_SUCCESS)) {
        MPIU_ERR_POP(mpi_errno);
    }
    if(stat != MPI_STATUS_IGNORE) {
        stat->MPI_TAG = req->status.MPI_TAG;
        stat->MPI_ERROR = req->status.MPI_ERROR;
        stat->MPI_SOURCE = req->status.MPI_SOURCE;
        stat->count = req->status.count;
        mpi_errno = stat->MPI_ERROR;
    }    

    ++psm_tot_recvs;
    req->psm_flags |= PSM_NON_BLOCKING_RECV;

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME psm_irecv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_irecv(int src, int tag, int context_id, void *buf, int buflen,
        MPID_Request *req)
{
    uint64_t rtag, rtagsel;
    psm_error_t psmerr;
    int mpi_errno = MPI_SUCCESS;

    if(unlikely(buf == NULL && buflen > 0)) {
        MPIU_ERR_SET(mpi_errno, MPI_ERR_BUFFER, "**fail");
        goto fn_fail;
    }

    rtag = 0;
    rtagsel = MQ_TAGSEL_ALL;
    MAKE_PSM_SELECTOR(rtag, context_id, tag, src);
    if(unlikely(src == MPI_ANY_SOURCE))
        rtagsel = MQ_TAGSEL_ANY_SOURCE;
    if(unlikely(tag == MPI_ANY_TAG))
        rtagsel = rtagsel & MQ_TAGSEL_ANY_TAG;
   
    DBG("psm_irecv: non-blocking\n");
    _psm_enter_;
    psmerr = psm_mq_irecv(psmdev_cw.mq, rtag, rtagsel, MQ_FLAGS_NONE, buf,
                buflen, req, &(req->mqreq));
    _psm_exit_;
    if(unlikely(psmerr != PSM_OK)) {
        mpi_errno = psm_map_error(psmerr);
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        goto fn_fail;
    }
            
    DBG("irecv enqueue\n");
    ++psm_tot_recvs;

fn_fail:
    return mpi_errno;
}
