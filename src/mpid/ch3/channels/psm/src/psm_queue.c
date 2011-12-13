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

#define _GNU_SOURCE

#include <pthread.h>
#include "mpiimpl.h"
#include "psmpriv.h"
#include "psm_vbuf.h"

MPID_Request psmcomphead;
pthread_spinlock_t reqlock;
pthread_spinlock_t psmlock;

static void psm_dump_debug();

#undef FUNCNAME
#define FUNCNAME psm_queue_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void psm_queue_init()
{
    pthread_spin_init(&reqlock, 0);
    pthread_spin_init(&psmlock, 0);
    psmcomphead.psmcompnext = &psmcomphead;
    psmcomphead.psmcompprev = &psmcomphead;
}
        
/* request complete:  add to completed entry queue,
                      update MPI_Status
                      set cc_ptr
                      release ref count on req
                      update pending send/recv ops
*/
#undef FUNCNAME
#define FUNCNAME psm_complete_req
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void psm_complete_req(MPID_Request *req, psm_mq_status_t psmstat)
{
    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        pthread_spin_lock(&reqlock);
    }

    req->psm_flags |= PSM_COMPQ_PENDING;
    req->psmcompnext = &psmcomphead;
    req->psmcompprev = psmcomphead.psmcompprev;
    req->psmcompnext->psmcompprev = req;
    req->psmcompprev->psmcompnext = req;

    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        pthread_spin_unlock(&reqlock);
    }

    assert(psmstat.context == req);
    if((&req->status) != MPI_STATUS_IGNORE) {
        psm_update_mpistatus(&(req->status), psmstat);
    }
    *(req->cc_ptr) = 0;         //TODO: should i set to 0 or decrement ?
    MPID_Request_release(req);
//    MPIU_Object_release_ref(req, &inuse);
}

#undef FUNCNAME
#define FUNCNAME psm_do_cancel
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_do_cancel(MPID_Request *req)
{
    psm_error_t psmerr;
    int mpi_errno = MPI_SUCCESS;

    if(req->psm_flags & PSM_SEND_CANCEL) {
        printf("send cancel unsupported\n");
        req->psm_flags &= ~PSM_SEND_CANCEL;
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**psmsendcancel");
    }

    if(req->psm_flags & PSM_RECV_CANCEL) {
        DBG("recv cancel\n");
        req->psm_flags &= ~PSM_RECV_CANCEL;
        _psm_enter_;
        psmerr = psm_mq_cancel(&(req->mqreq));
        _psm_exit_;
        if(unlikely(psmerr != PSM_OK)) {
            MPIU_ERR_POP(mpi_errno);
        }

        req->status.cancelled = TRUE;
        req->status.count = 0;
    }

fn_fail:
    return mpi_errno;    
}

#undef FUNCNAME
#define FUNCNAME psm_process_completion
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_process_completion(MPID_Request *req, psm_mq_status_t gblstatus)
{
    int mpi_errno = MPI_SUCCESS;

    /* request is a 1-sided pre-posted receive */
    if(req->psm_flags & PSM_1SIDED_PREPOST) {
        --psm_tot_pposted_recvs;
        mpi_errno = psm_1sided_input(req, gblstatus.nbytes);
        if(mpi_errno)   MPIU_ERR_POP(mpi_errno);
        DBG("1-sided pre-posted completed\n");
        goto fn_exit;
    }

    /* request is a RNDV receive for a GET */
    if(req->psm_flags & PSM_RNDVRECV_GET_REQ) {
        //MPIDI_CH3U_Request_complete(req->savedreq);
        mpi_errno = psm_getresp_rndv_complete(req, gblstatus.nbytes);
        goto fn_exit;
    }

    /* request is a GET-Response */
    if(req->psm_flags & PSM_GETRESP_REQ) {
        mpi_errno = psm_getresp_complete(req);
        if(mpi_errno)   MPIU_ERR_POP(mpi_errno);
        goto fn_exit;
    }

    /* request is a RNDV send */
    if(req->psm_flags & PSM_RNDVSEND_REQ) {
        psm_complete_req(req, gblstatus);
        goto fn_exit;
    }

    /* request was a GET packet */
    if(req->psm_flags & PSM_GETPKT_REQ) {
        /* completion of GET req is not complete until the
           GET-RESP comes back. */
        goto fn_exit;
    }

    /* request was a PUT/ACCUM RNDV receive */
    if(req->psm_flags & (PSM_RNDVRECV_ACCUM_REQ | PSM_RNDVRECV_PUT_REQ)) {
        DBG("1-sided PUT comp on %x\n", req);
        mpi_errno = psm_complete_rndvrecv(req, gblstatus.nbytes);
        if(mpi_errno)   MPIU_ERR_POP(mpi_errno);
        goto fn_exit;
    }

    psm_complete_req(req, gblstatus);

fn_exit:
fn_fail:    
    return mpi_errno;
}

/* try to complete a specified request */
#undef FUNCNAME
#define FUNCNAME psm_try_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_try_complete(MPID_Request *req)
{
    int mpi_errno = MPI_SUCCESS;

    while(*(req->cc_ptr) != 0)
      mpi_errno = psm_progress_wait(TRUE);

    return mpi_errno;
}

/* progress engine:
        peek into PSM. If no completion for 10 spins, get out.
            if MT yield CPU and release global lock
        if we got completion, do mq_test, release PSM lock,
        run completion handler, re-acquire PSM lock and
        back into ipeek.
*/    

#undef FUNCNAME
#define FUNCNAME psm_progress_wait
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_progress_wait(int blocking)
{
    psm_error_t psmerr;
    psm_mq_status_t gblstatus;
    psm_mq_req_t gblpsmreq;
    register MPID_Request *req;
    int mpi_errno = MPI_SUCCESS;
    int yield_count = 3;

    _psm_enter_;
    do {
      psmerr = psm_mq_ipeek(psmdev_cw.mq, &gblpsmreq, NULL);
    
      if(psmerr == PSM_OK) {
	psmerr = psm_mq_test(&gblpsmreq, &gblstatus);
	_psm_exit_;
	req = (MPID_Request *) gblstatus.context;
	DBG("got bytes from %d\n", (gblstatus.msg_tag & SRC_RANK_MASK));
	mpi_errno = psm_process_completion(req, gblstatus);
	if(mpi_errno != MPI_SUCCESS) {
	  MPIU_ERR_POP(mpi_errno);
	}
	goto out_2;
      }
      else if ((MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) &&
	       (--yield_count == 0))
	goto out;
    } while (blocking);
    
 out:
    _psm_exit_;
    if(unlikely(ipath_debug_enable)) {
      psm_dump_debug();
    }

    if (MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
      psm_pe_yield();
    }

 out_2:
 fn_fail:
    return mpi_errno;
}

/* remove request from completed queue. called from request release
   in CH3. lock needed for MT */
#undef FUNCNAME
#define FUNCNAME psm_dequeue_compreq
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void psm_dequeue_compreq(MPID_Request *req)
{
    DBG("Request release\n");
    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        pthread_spin_lock(&reqlock);
    }
       
    assert(req != (&psmcomphead));
    assert(req->psmcompnext);
    assert(req->psmcompprev);
    req->psmcompnext->psmcompprev = req->psmcompprev;
    req->psmcompprev->psmcompnext = req->psmcompnext;    
    
    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        pthread_spin_unlock(&reqlock);
    }
}

#undef FUNCNAME
#define FUNCNAME psm_probe
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
psm_error_t psm_probe(int src, int tag, int context, MPI_Status *stat)
{
    uint64_t rtag, rtagsel;
    psm_error_t psmerr;
    psm_mq_status_t gblstatus;

    rtag = 0;
    rtagsel = MQ_TAGSEL_ALL;
    MAKE_PSM_SELECTOR(rtag, context, tag, src);
    if(unlikely(src == MPI_ANY_SOURCE))
        rtagsel = MQ_TAGSEL_ANY_SOURCE;
    if(unlikely(tag == MPI_ANY_TAG))
        rtagsel = rtagsel & MQ_TAGSEL_ANY_TAG;
    
    _psm_enter_;
    psmerr = psm_mq_iprobe(psmdev_cw.mq, rtag, rtagsel, &gblstatus);
    _psm_exit_;
    if(psmerr == PSM_OK) {
        DBG("one psm probe completed\n");
        if(stat != MPI_STATUS_IGNORE) {
            psm_update_mpistatus(stat, gblstatus);
        }
    }

    return psmerr;    
}

int psm_no_lock(pthread_spinlock_t *lock) 
{  
    /* no-lock needed */
    return 0;
}

/* in multi-threaded mode, the mpich2 global lock has to be released
   by the progress engine for other threads to enter MPID layer */
#undef FUNCNAME
#define FUNCNAME psm_pe_yield
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void psm_pe_yield()
{
    pthread_mutex_unlock(&(MPIR_ThreadInfo.global_mutex));
    sched_yield();
    pthread_mutex_lock(&(MPIR_ThreadInfo.global_mutex));
}

#undef FUNCNAME
#define FUNCNAME psm_update_mpistatus
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void psm_update_mpistatus(MPI_Status *stat, psm_mq_status_t psmst)
{
    stat->MPI_TAG = (psmst.msg_tag >> SRC_RANK_BITS) & TAG_MASK;
    switch(psmst.error_code) {
        case PSM_OK:    
            stat->MPI_ERROR = MPI_SUCCESS;
            break;
        case PSM_MQ_TRUNCATION:
            stat->MPI_ERROR = MPI_ERR_TRUNCATE;
            break;
        default:
            break;
    }           
    stat->MPI_SOURCE = psmst.msg_tag & SRC_RANK_MASK;
    stat->count = psmst.nbytes;
}

/* if PSM_DEBUG is enabled, we will dump some counters */

static void psm_dump_debug()
{
    static time_t  timedump;
    struct tm *ts;
    char buf[80];
    time_t last;
    int rank;
    extern uint32_t ipath_dump_frequency;

    last = time(NULL);
    
    if((last - timedump) < ipath_dump_frequency) 
        return;
    PMI_Get_rank(&rank);

    fprintf(stderr, "[%d]------- PSM COUNTERS---------\n", rank);
    fprintf(stderr, "[%d] Total SENDS\t\t%d\n", rank, psm_tot_sends);
    fprintf(stderr, "[%d] Total RECVS\t\t%d\n", rank, psm_tot_recvs);
    fprintf(stderr, "[%d] Total pre-posted receives\t\t%d\n", rank, psm_tot_pposted_recvs);
    fprintf(stderr, "[%d] Total eager PUTS\t\t%d\n", rank, psm_tot_eager_puts);
    fprintf(stderr, "[%d] Total eager GETS\t\t%d\n", rank, psm_tot_eager_gets);
    fprintf(stderr, "[%d] Total rendezvous PUTS\t\t%d\n", rank, psm_tot_rndv_puts);
    fprintf(stderr, "[%d] Total rendezvous GETS\t\t%d\n", rank, psm_tot_rndv_gets);
    fprintf(stderr, "[%d] Total ACCUMULATES\t\t%d\n", rank, psm_tot_accs);
    ts = localtime(&last);
    strftime(buf, sizeof(buf), "%a %Y-%m-%d %H:%M:%S %Z", ts);
    fprintf(stderr, "[%d] ------Time of dump %s-----\n", rank, buf);
    timedump = last;
}
