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
#include <pthread.h>

/* this file has all the MPIU_CALL functions called from CH3 layer */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Init(int has_parent, MPIDI_PG_t *pg, int pg_rank) 
{
    return (psm_doinit(has_parent, pg, pg_rank));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSendv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_iSendv(MPIDI_VC_t *vc, MPID_Request *req, MPID_IOV *iov, int iov_n)
{
    return (psm_isendv(vc, iov, iov_n, req));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iStartMsgv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_iStartMsgv(MPIDI_VC_t *vc, MPID_IOV *iov, int iov_n, MPID_Request **req)
{
   return (psm_istartmsgv(vc, iov, iov_n, req));     
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iStartMsg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_iStartMsg(MPIDI_VC_t *vc, void *pkt, MPIDI_msg_sz_t pkt_sz, MPID_Request **req)
{
    return (psm_istartmsg(vc, pkt, pkt_sz, req));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iSend
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_iSend(MPIDI_VC_t *vc, MPID_Request *req, void *pkt, MPIDI_msg_sz_t pkt_sz)
{
    return MPI_SUCCESS; 
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connection_terminate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_Connection_terminate(MPIDI_VC_t *vc)
{
    return MPI_SUCCESS; 
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Connect_to_root
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_Connect_to_root(const char *port, MPIDI_VC_t **vc)
{
    return MPI_SUCCESS; 
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_VC_GetStateString
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

const char * MPIDI_CH3_VC_GetStateString(MPIDI_VC_t *vc)
{
    return NULL;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_PG_Init(MPIDI_PG_t *pg)
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PG_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_PG_Destroy(MPIDI_PG_t *pg)
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_InitCompleted
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_InitCompleted()
{
    
    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        psm_lock_fn = pthread_spin_lock;
        psm_unlock_fn = pthread_spin_unlock;
    } else {
        psm_lock_fn = psm_unlock_fn = psm_no_lock;
    }
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PortFnsInit  
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_PortFnsInit(MPIDI_PortFns *fns)
{
    return MPI_SUCCESS; 
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Get_business_card
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_Get_business_card(int myRank, char *port, int len)
{
    return MPI_SUCCESS; 
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_VC_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_VC_Destroy(MPIDI_VC_t *vc)    
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_VC_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPIDI_CH3_VC_Init(MPIDI_VC_t *vc)
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_start
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline void MPIDI_CH3_Progress_start(MPID_Progress_state *pstate)
{
  _psm_enter_;
  psm_poll(psmdev_cw.ep);
  _psm_exit_;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_end
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

void MPIDI_CH3_Progress_end(MPID_Progress_state *pstate)
{

}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Progress_wakeup
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

void MPIDI_CH3I_Progress_wakeup(void)
{

}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_wait
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Progress_wait(MPID_Progress_state *state)
{
    return(psm_progress_wait(TRUE));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Finalize(void)
{
    return(psm_dofinalize());
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Progress_test(void)
{
    return (psm_progress_wait(FALSE));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Progress_poke
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Progress_poke()
{
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_Recv(int rank, int tag, int cid, void *buf, int buflen,
                          MPI_Status *stat, MPID_Request **req)
{
    return (psm_recv(rank, tag, cid, buf, buflen, stat, req));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_iRecv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

inline int MPIDI_CH3_iRecv(int rank, int tag, int cid, void *buf, int buflen,
        MPID_Request *req)
{
    return (psm_irecv(rank, tag, cid, buf, buflen, req));
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_SendNonContig
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_SendNonContig(MPIDI_VC_t *vc, MPID_Request *sreq,
                            MPIDI_Message_match match, int blocking)
{
    fprintf(stderr, "SHOULD NOT BE USED\n");
    fflush(stderr);
    return MPI_SUCCESS; 
}
    
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Probe
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Probe(int source, int tag, int context, MPI_Status *stat,
                    int *complete, int blk)
{
    int mpi_errno = MPI_SUCCESS, i;
    psm_error_t psmerr;
    uint32_t ipath_spinlimit = 
      (MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) ? 100 : 1000;
   
    /* if not blocking, do probe once */
    if(blk == PSM_NONBLOCKING) {
        psmerr = psm_probe(source, tag, context, stat);
        if(psmerr == PSM_OK) {
            *complete = TRUE;
        } else if(psmerr == PSM_MQ_NO_COMPLETIONS) {
            *complete = FALSE;
        } else {
            MPIU_ERR_POP(mpi_errno);
        }
        goto fn_exit;
    }
 
    /* if blocking, probe SPINLIMIT times */
spin:
    for(i = 0; i < ipath_spinlimit; i++) {
        psmerr = psm_probe(source, tag, context, stat);
        if(psmerr == PSM_OK) {
            *complete = TRUE;
            goto fn_exit;
        } else if(psmerr != PSM_MQ_NO_COMPLETIONS) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    /* if we're MT yield global lock */
    
    if(MPIR_ThreadInfo.thread_provided == MPI_THREAD_MULTIPLE) {
        psm_pe_yield();
    }
    goto spin;
    
fn_fail:    
fn_exit:
    return mpi_errno;
}
