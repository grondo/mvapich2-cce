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

#include "mpidimpl.h"
#include "mpidrma.h"

#if defined(_SMP_LIMIC_) && !defined(DAPL_DEFAULT_PROVIDER)
#include "mpiimpl.h"
#include <limic.h>
#endif /* _SMP_LIMIC_ && !DAPL_DEFAULT_PROVIDER*/

#if defined(_OSU_MVAPICH_)
#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
    do {                                                              \
        int rank;                                                     \
        PMI_Get_rank(&rank);                                          \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);    \
        fprintf(stderr, args);                                        \
        fflush(stderr);                                               \
    } while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */
#endif /* defined(_OSU_MVAPICH_) */

//#define PSM_1SC_DEBUG
#ifdef PSM_1SC_DEBUG
    #define PSM_PRINT(args...) \
     do {                                                              \
        int rank;                                                     \
        PMI_Get_rank(&rank);                                          \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);    \
        fprintf(stderr, args);                                        \
        fflush(stderr);                                               \
    } while (0)
#else 
#define PSM_PRINT(args...)
#endif 
static int EnableImmedAcc = 1;
void MPIDI_CH3_RMA_SetAccImmed( int flag )
{
    EnableImmedAcc = flag;
}

#ifdef USE_MPIU_INSTR
MPIU_INSTR_DURATION_DECL(winfence_clearlock);
MPIU_INSTR_DURATION_DECL(winfence_rs);
MPIU_INSTR_DURATION_DECL(winfence_issue);
MPIU_INSTR_DURATION_DECL(winfence_complete);
MPIU_INSTR_DURATION_DECL(winfence_wait);
MPIU_INSTR_DURATION_DECL(winfence_block);
MPIU_INSTR_DURATION_DECL(winpost_clearlock);
MPIU_INSTR_DURATION_DECL(winpost_sendsync);
MPIU_INSTR_DURATION_DECL(winstart_clearlock);
MPIU_INSTR_DURATION_DECL(wincomplete_issue);
MPIU_INSTR_DURATION_DECL(wincomplete_complete);
MPIU_INSTR_DURATION_DECL(wincomplete_recvsync);
MPIU_INSTR_DURATION_DECL(wincomplete_block);
MPIU_INSTR_DURATION_DECL(winwait_wait);
MPIU_INSTR_DURATION_DECL(winlock_getlocallock);
MPIU_INSTR_DURATION_DECL(winunlock_getlock);
MPIU_INSTR_DURATION_DECL(winunlock_issue);
MPIU_INSTR_DURATION_DECL(winunlock_complete);
MPIU_INSTR_DURATION_DECL(winunlock_block);
MPIU_INSTR_DURATION_DECL(lockqueue_alloc);
MPIU_INSTR_DURATION_DECL(rmapkt_acc);
MPIU_INSTR_DURATION_DECL(rmapkt_acc_predef);
MPIU_INSTR_DURATION_DECL(rmapkt_acc_immed);
MPIU_INSTR_DURATION_EXTERN_DECL(rmaqueue_alloc);
void MPIDI_CH3_RMA_InitInstr(void);

void MPIDI_CH3_RMA_InitInstr(void)
{
    MPIU_INSTR_DURATION_INIT(lockqueue_alloc,0,"Allocate Lock Queue element");
    MPIU_INSTR_DURATION_INIT(winfence_clearlock,1,"WIN_FENCE:Clear prior lock");
    MPIU_INSTR_DURATION_INIT(winfence_rs,0,"WIN_FENCE:ReduceScatterBlock");
    MPIU_INSTR_DURATION_INIT(winfence_issue,2,"WIN_FENCE:Issue RMA ops");
    MPIU_INSTR_DURATION_INIT(winfence_complete,1,"WIN_FENCE:Complete RMA ops");
    MPIU_INSTR_DURATION_INIT(winfence_wait,1,"WIN_FENCE:Wait for ops from other processes");
    MPIU_INSTR_DURATION_INIT(winfence_block,0,"WIN_FENCE:Wait for any progress");
    MPIU_INSTR_DURATION_INIT(winpost_clearlock,1,"WIN_POST:Clear prior lock");
    MPIU_INSTR_DURATION_INIT(winpost_sendsync,1,"WIN_POST:Senc sync messages");
    MPIU_INSTR_DURATION_INIT(winstart_clearlock,1,"WIN_START:Clear prior lock");
    MPIU_INSTR_DURATION_INIT(wincomplete_recvsync,1,"WIN_COMPLETE:Recv sync messages");
    MPIU_INSTR_DURATION_INIT(wincomplete_issue,2,"WIN_COMPLETE:Issue RMA ops");
    MPIU_INSTR_DURATION_INIT(wincomplete_complete,1,"WIN_COMPLETE:Complete RMA ops");
    MPIU_INSTR_DURATION_INIT(wincomplete_block,0,"WIN_COMPLETE:Wait for any progress");
    MPIU_INSTR_DURATION_INIT(winwait_wait,1,"WIN_WAIT:Wait for ops from other processes");
    MPIU_INSTR_DURATION_INIT(winlock_getlocallock,0,"WIN_LOCK:Get local lock");
    MPIU_INSTR_DURATION_INIT(winunlock_issue,2,"WIN_UNLOCK:Issue RMA ops");
    MPIU_INSTR_DURATION_INIT(winunlock_complete,1,"WIN_UNLOCK:Complete RMA ops");
    MPIU_INSTR_DURATION_INIT(winunlock_getlock,0,"WIN_UNLOCK:Acquire lock");
    MPIU_INSTR_DURATION_INIT(winunlock_block,0,"WIN_UNLOCK:Wait for any progress");
    MPIU_INSTR_DURATION_INIT(rmapkt_acc,0,"RMA:PKTHANDLER for Accumulate");
    MPIU_INSTR_DURATION_INIT(rmapkt_acc_predef,0,"RMA:PKTHANDLER for Accumulate: predef dtype");
    MPIU_INSTR_DURATION_INIT(rmapkt_acc_immed,0,"RMA:PKTHANDLER for Accum immed");
}
#endif

#if defined(_SMP_LIMIC_) && !defined(DAPL_DEFAULT_PROVIDER)
extern int g_smp_use_limic2;
extern int limic_fd;
#endif /*_SMP_LIMIC_ && !DAPL_DEFAULT_PROVIDER*/

/*
 * These routines provide a default implementation of the MPI RMA operations
 * in terms of the low-level, two-sided channel operations.  A channel
 * may override these functions, on a per-window basis, by defining 
 * USE_CHANNEL_RMA_TABLE and providing the function MPIDI_CH3_RMAWinFnsInit.
 */

/*
 * TODO: 
 * 
 */

#define SYNC_POST_TAG 100

static int MPIDI_CH3I_Send_rma_msg(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, 
				   MPI_Win source_win_handle, 
				   MPI_Win target_win_handle, 
				   MPIDI_RMA_dtype_info * dtype_info, 
				   void ** dataloop, MPID_Request ** request);
static int MPIDI_CH3I_Recv_rma_msg(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, 
				   MPI_Win source_win_handle, 
				   MPI_Win target_win_handle, 
				   MPIDI_RMA_dtype_info * dtype_info, 
				   void ** dataloop, MPID_Request ** request); 
static int MPIDI_CH3I_Send_contig_acc_msg(MPIDI_RMA_ops * rma_op, 
					  MPID_Win * win_ptr, 
					  MPI_Win source_win_handle, 
					  MPI_Win target_win_handle, 
					  MPID_Request ** request);
static int MPIDI_CH3I_Do_passive_target_rma(MPID_Win *win_ptr, 
					    int *wait_for_rma_done_pkt);
static int MPIDI_CH3I_Send_lock_put_or_acc(MPID_Win *win_ptr);
static int MPIDI_CH3I_Send_lock_get(MPID_Win *win_ptr);

#if defined (_OSU_PSM_)
extern int psm_get_rndvtag();
#endif /* _OSU_PSM_ */

static int create_datatype(const MPIDI_RMA_dtype_info *dtype_info,
                           const void *dataloop, MPI_Aint dataloop_sz,
                           const void *o_addr, int o_count, MPI_Datatype o_datatype,
                           MPID_Datatype **combined_dtp);

#undef FUNCNAME
#define FUNCNAME MPIDI_Win_fence
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_fence(int assert, MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size;
    int *rma_target_proc, *nops_to_proc, i, total_op_count, *curr_ops_cnt;
    MPIDI_RMA_ops *curr_ptr;
    MPID_Comm *comm_ptr;
    MPI_Win source_win_handle, target_win_handle;
    MPID_Progress_state progress_state;
#if defined(_OSU_MVAPICH_)
    int newly_finished = 0, index;
    int num_wait_completions;
    int j;
    MPIDI_VC_t* vc = NULL;
#endif /* defined(_OSU_MVAPICH_) */
    int errflag = FALSE;
    MPIU_CHKLMEM_DECL(3);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_FENCE);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_FENCE);

    /* In case this process was previously the target of passive target rma
     * operations, we need to take care of the following...
     * Since we allow MPI_Win_unlock to return without a done ack from
     * the target in the case of multiple rma ops and exclusive lock,
     * we need to check whether there is a lock on the window, and if
     * there is a lock, poke the progress engine until the operartions
     * have completed and the lock is released. */
    if (win_ptr->current_lock_type != MPID_LOCK_NONE)
    {
	MPIU_INSTR_DURATION_START(winfence_clearlock);
	MPID_Progress_start(&progress_state);
	while (win_ptr->current_lock_type != MPID_LOCK_NONE)
	{
	    /* poke the progress engine */
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS) {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
	    }
	    /* --END ERROR HANDLING-- */
	    MPIU_INSTR_DURATION_INCR(winfence_clearlock,0,1);
	}
	MPID_Progress_end(&progress_state);
	MPIU_INSTR_DURATION_END(winfence_clearlock);
    }

    /* Note that the NOPRECEDE and NOSUCCEED must be specified by all processes
       in the window's group if any specify it */
    if (assert & MPI_MODE_NOPRECEDE)
    {
	win_ptr->fence_cnt = (assert & MPI_MODE_NOSUCCEED) ? 0 : 1;
#if defined(_OSU_MVAPICH_)
	if (win_ptr->fence_cnt != 0) {
           comm_ptr = win_ptr->comm_ptr;
           comm_size = comm_ptr->local_size;

           if (win_ptr->fall_back != 1) { 
	      MPIR_Barrier_impl(comm_ptr, &errflag);
              /* All the processes have syncrhonized, we can assume windows at 
                 all the processes to be posted, set post flags appropriately 
                 for check in communication operations */
	      for (i = 0; i < comm_size; i++) {
  		  win_ptr->post_flag[i] = 1; 
	      }
	   } 
#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_)
           else if (!win_ptr->limic_fallback || !win_ptr->shm_fallback) { 
#else
           else if (!win_ptr->shm_fallback) { 
#endif /*_SMP_LIMIC_*/
              MPIR_Barrier_impl(comm_ptr, &errflag);
           }
#endif /* !DAPL_DEFAULT_PROVIDER*/
        }
#endif /* defined(_OSU_MVAPICH_) */
	goto fn_exit;
    }
    
    if (win_ptr->fence_cnt == 0)
    {
	/* win_ptr->fence_cnt == 0 means either this is the very first
	   call to fence or the preceding fence had the
	   MPI_MODE_NOSUCCEED assert. 

           If this fence has MPI_MODE_NOSUCCEED, do nothing and return.
	   Otherwise just increment the fence count and return. */

	if (!(assert & MPI_MODE_NOSUCCEED)) win_ptr->fence_cnt = 1;

#if defined(_OSU_MVAPICH_)
        /* OSU-MPI2 uses extended CH3 interface */
	if (win_ptr->fence_cnt != 0) {
           comm_ptr = win_ptr->comm_ptr;
           comm_size = comm_ptr->local_size;

           if (win_ptr->fall_back != 1) { 
              MPIR_Barrier_impl(comm_ptr, &errflag);
              for (i = 0; i < comm_size; i++) {
                 /* All the processes have syncrhonized, each process can assume windows at 
                    all the processes to be posted */
                 win_ptr->post_flag[i] = 1;
              }
           }
#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_) 
           else if (!win_ptr->limic_fallback || !win_ptr->shm_fallback) { 
#else
           else if (win_ptr->shm_fallback != 1) {
#endif /* _SMP_LIMIC_ */ 
              MPIR_Barrier_impl(comm_ptr, &errflag);
           }
#endif /* !DAPL_DEFAULT_PROVIDER*/
	}
#endif /* defined(_OSU_MVAPICH_) */
    }
    else
    {
	MPIDI_RMA_ops **prevNextPtr, *tmpptr;
	MPIU_INSTR_DURATION_START(winfence_rs);
	/* This is the second or later fence. Do all the preceding RMA ops. */
	comm_ptr = win_ptr->comm_ptr;
	/* First inform every process whether it is a target of RMA
	   ops from this process */
	comm_size = comm_ptr->local_size;
#if defined(_OSU_MVAPICH_)
	if (win_ptr->fall_back != 1) {
	    int i;
	    int *ranks_in_win_grp = (int *) MPIU_Malloc((comm_size - 1) * sizeof(int));

	    for (i = 0; i < comm_size; i++) {
		if (i < win_ptr->my_id)
		    ranks_in_win_grp[i] = i;
		if (i > win_ptr->my_id)
		    ranks_in_win_grp[i - 1] = i;
	    }
	    /* we are sure that all processes have started access epoch as this is the 
               second or later fence */
	    MPIDI_CH3I_RDMA_try_rma(win_ptr, 0);
            if (win_ptr->rma_issued != 0) {
                MPIDI_CH3I_RDMA_finish_rma(win_ptr);
            }
	    MPIDI_CH3I_RDMA_complete(win_ptr, comm_size - 1,
	                                ranks_in_win_grp);

	    MPIU_Free(ranks_in_win_grp);
	}

#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_)
    if (!win_ptr->limic_fallback) { 
        MPIDI_CH3I_LIMIC_try_rma(win_ptr);
    }
#endif
    if (!win_ptr->shm_fallback) {
        mpi_errno = MPIDI_CH3I_SHM_try_rma(win_ptr);     
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno); 
        }
    }
#endif /* DAPL_DEFAULT_PROVIDER */
#endif /* defined(_OSU_MVAPICH_) */

	MPIU_CHKLMEM_MALLOC(rma_target_proc, int *, comm_size*sizeof(int),
			    mpi_errno, "rma_target_proc");
	for (i=0; i<comm_size; i++) rma_target_proc[i] = 0;
	
	/* keep track of no. of ops to each proc. Needed for knowing
	   whether or not to decrement the completion counter. The
	   completion counter is decremented only on the last
	   operation. */
	MPIU_CHKLMEM_MALLOC(nops_to_proc, int *, comm_size*sizeof(int),
			    mpi_errno, "nops_to_proc");
	for (i=0; i<comm_size; i++) nops_to_proc[i] = 0;

	/* set rma_target_proc[i] to 1 if rank i is a target of RMA
	   ops from this process */
	total_op_count = 0;
	curr_ptr = win_ptr->rma_ops_list_head;
	while (curr_ptr != NULL)
	{
	    total_op_count++;
	    rma_target_proc[curr_ptr->target_rank] = 1;
	    nops_to_proc[curr_ptr->target_rank]++;
	    curr_ptr = curr_ptr->next;
	}

#if defined(_OSU_MVAPICH_)
        /* Set all off-node remote processes as possible targets as we donot keep track of 
           which processes operations had been issued to. 
           FIXME: This can be optimized by keeping track of processes to which operations 
           had been issued and marking only them as targets.*/
	if (win_ptr->fall_back != 1) {
	    int j;
	    for (j = 0; j < comm_size; j++) {
                if (SMP_INIT) {
	        	MPIDI_Comm_get_vc(comm_ptr, j, &vc);
        		if (j != win_ptr->my_id && vc->smp.local_nodes == -1)
                        rma_target_proc[j] = 1;
                } else if (j != win_ptr->my_id)
                {
		    rma_target_proc[j] = 1;
                }
	    }
	}
#endif /* defined(_OSU_MVAPICH_) */	

	MPIU_CHKLMEM_MALLOC(curr_ops_cnt, int *, comm_size*sizeof(int),
			    mpi_errno, "curr_ops_cnt");
	for (i=0; i<comm_size; i++) curr_ops_cnt[i] = 0;
	/* do a reduce_scatter_block (with MPI_SUM) on rma_target_proc. 
	   As a result,
	   each process knows how many other processes will be doing
	   RMA ops on its window */  
            
	/* first initialize the completion counter. */
	win_ptr->my_counter = comm_size;
            
	mpi_errno = MPIR_Reduce_scatter_block_impl(MPI_IN_PLACE, rma_target_proc, 1,
                                                   MPI_INT, MPI_SUM, comm_ptr, &errflag);
	MPIU_INSTR_DURATION_END(winfence_rs);
	/* result is stored in rma_target_proc[0] */
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
        MPIU_ERR_CHKANDJUMP(errflag, mpi_errno, MPI_ERR_OTHER, "**coll_fail");

	/* Set the completion counter */
	/* FIXME: MT: this needs to be done atomically because other
	   procs have the address and could decrement it. */
	win_ptr->my_counter = win_ptr->my_counter - comm_size + 
	    rma_target_proc[0];  

	MPIU_INSTR_DURATION_START(winfence_issue);
	MPIU_INSTR_DURATION_INCR(winfence_issue,0,total_op_count);
	MPIU_INSTR_DURATION_MAX(winfence_issue,1,total_op_count);
	i = 0;
	curr_ptr    = win_ptr->rma_ops_list_head;
	prevNextPtr = &win_ptr->rma_ops_list_head;
	while (curr_ptr != NULL)
	{
	    /* The completion counter at the target is decremented only on 
	       the last RMA operation. We indicate the last operation by 
	       passing the source_win_handle only on the last operation. 
	       Otherwise, we pass NULL */
	    if (curr_ops_cnt[curr_ptr->target_rank] ==
		nops_to_proc[curr_ptr->target_rank] - 1) 
		source_win_handle = win_ptr->handle;
	    else 
		source_win_handle = MPI_WIN_NULL;
	    
	    target_win_handle = win_ptr->all_win_handles[curr_ptr->target_rank];

	    curr_ptr->dataloop = 0;
	    switch (curr_ptr->type)
	    {
	    case (MPIDI_RMA_PUT):
	    case (MPIDI_RMA_ACCUMULATE):
		mpi_errno = MPIDI_CH3I_Send_rma_msg(curr_ptr, win_ptr,
					source_win_handle, target_win_handle, 
					&curr_ptr->dtype_info,
					&curr_ptr->dataloop, &curr_ptr->request);
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
		break;
	    case MPIDI_RMA_ACC_CONTIG:
		mpi_errno = MPIDI_CH3I_Send_contig_acc_msg(curr_ptr, win_ptr,
				   source_win_handle, target_win_handle, 
				   &curr_ptr->request );
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
		break;
	    case (MPIDI_RMA_GET):
		mpi_errno = MPIDI_CH3I_Recv_rma_msg(curr_ptr, win_ptr,
					source_win_handle, target_win_handle, 
					&curr_ptr->dtype_info, 
					&curr_ptr->dataloop, &curr_ptr->request);
		if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
		break;
	    default:
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winInvalidOp");
	    }
	    i++;
	    curr_ops_cnt[curr_ptr->target_rank]++;
	    /* If the request is null, we can remove it immediately */
	    if (!curr_ptr->request) {
		if (curr_ptr->dataloop != NULL) {
		    MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
						      recv_rma_msg */
		}
		tmpptr       = curr_ptr->next;
		*prevNextPtr = tmpptr;
		MPIU_Free( curr_ptr );
		curr_ptr     = tmpptr;
	    }
	    else  {
		prevNextPtr = &curr_ptr->next;
		curr_ptr    = curr_ptr->next;
		/* FIXME: We could at least occassionally try to wait
		   on completion of the pending send requests rather than
		   focus on filling the queues.  */
	    }
	}
	MPIU_INSTR_DURATION_END(winfence_issue);

	/* We replaced a loop over an array of requests with a list of the
	   incomplete requests.  The reason to do 
	   that is for long lists - processing the entire list until
	   all are done introduces a potentially n^2 time.  In 
	   testing with test/mpi/perf/manyrma.c , the number of iterations
	   within the "while (total_op_count) was O(total_op_count).
	   
	   Another alternative is to create a more compressed list (storing
	   only the necessary information, reducing the number of cache lines
	   needed while looping through the requests.
	*/
	if (total_op_count)
	{ 
	    int ntimes = 0;
	    MPIU_INSTR_DURATION_START(winfence_complete);
	    MPID_Progress_start(&progress_state);
	    /* Process all operations until they are complete */
	    while (win_ptr->rma_ops_list_head) {
		int loopcount = 0;
		prevNextPtr = &win_ptr->rma_ops_list_head;
		ntimes++;
		curr_ptr = win_ptr->rma_ops_list_head;
		do {
		    if (MPID_Request_is_complete(curr_ptr->request)) {
			/* Once we find a complete request, we complete
			 as many as possible until we find an incomplete
			 or null request */
			do {
			    mpi_errno = curr_ptr->request->status.MPI_ERROR;
			    /* --BEGIN ERROR HANDLING-- */
			    if (mpi_errno != MPI_SUCCESS) {
				MPID_Progress_end(&progress_state);
				MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
			    }
			    /* --END ERROR HANDLING-- */
			    MPID_Request_release(curr_ptr->request);
			    if (curr_ptr->dataloop != NULL) {
				MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
								  recv_rma_msg */
			    }
			    /* We can remove and free this rma op element */
			    tmpptr       = curr_ptr->next;
			    *prevNextPtr = tmpptr;
			    MPIU_Free( curr_ptr );
			    curr_ptr     = tmpptr;
			}
			while (curr_ptr &&
			       MPID_Request_is_complete(curr_ptr->request));
			/* Once a request completes, we wait for another
			   operation to arrive rather than check the
			   rest of the requests.  */
			break;
		    }
		    else {
			/* In many cases, if the list of pending requests
			   is long, there's no point in checking the entire
			   list */
			if (loopcount++ > 4) /* FIXME: threshold as parameter */
			    break;  /* wait for an event */
			prevNextPtr = &curr_ptr->next;
			curr_ptr    = curr_ptr->next;
		    }
		} while (curr_ptr);
         
		/* Wait for something to arrive*/
		/* In some tests, this hung unless the test ensured that 
		   there was an incomplete request. */
		curr_ptr = win_ptr->rma_ops_list_head;
		if (curr_ptr && !MPID_Request_is_complete(curr_ptr->request) ) {
		    MPIU_INSTR_DURATION_START(winfence_block);
		    mpi_errno = MPID_Progress_wait(&progress_state);
		    /* --BEGIN ERROR HANDLING-- */
		    if (mpi_errno != MPI_SUCCESS) {
			MPID_Progress_end(&progress_state);
			MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		    }
		    /* --END ERROR HANDLING-- */
		    MPIU_INSTR_DURATION_END(winfence_block);
		}
	    } /* While list of rma operation is non-empty */
	    MPID_Progress_end(&progress_state);
	    MPIU_INSTR_DURATION_INCR(winfence_complete,0,ntimes);
	    MPIU_INSTR_DURATION_END(winfence_complete);
	}
            
	win_ptr->rma_ops_list_head = NULL;
	win_ptr->rma_ops_list_tail = NULL;
#if defined(_OSU_MVAPICH_) 
        if (win_ptr->fall_back != 1) {
            num_wait_completions = 0;
            while (win_ptr->my_counter || win_ptr->outstanding_rma != 0) {
                newly_finished = 0; 
                for (i = 0; i < win_ptr->comm_size; ++i) {
 
                    for (j = 0; j < rdma_num_rails; ++j) {
            
                        index = i*rdma_num_rails+j;
                        if (win_ptr->completion_counter[index] == 1) {
                            win_ptr->completion_counter[index] = 0;
                            ++num_wait_completions;
                            if (num_wait_completions == rdma_num_rails) {
                                ++newly_finished;
                                num_wait_completions = 0;
                            }
                        }		
                    }
                }
                win_ptr->my_counter -= newly_finished;
                if (win_ptr->my_counter == 0)
                    break;
                mpi_errno = MPID_Progress_test();
                if (mpi_errno != MPI_SUCCESS) {
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        } else 
#endif /* defined(_OSU_MVAPICH_) */
	/* wait for all operations from other processes to finish */
	if (win_ptr->my_counter)
	{
	    MPIU_INSTR_DURATION_START(winfence_wait);
	    MPID_Progress_start(&progress_state);
	    while (win_ptr->my_counter)
	    {
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
		MPIU_INSTR_DURATION_INCR(winfence_wait,0,1);
	    }
	    MPID_Progress_end(&progress_state);
	    MPIU_INSTR_DURATION_END(winfence_wait);
	} 
	
	if (assert & MPI_MODE_NOSUCCEED)
	{
	    win_ptr->fence_cnt = 0;
	}
#if defined(_OSU_MVAPICH_)
	else if (win_ptr->fall_back != 1)
	{
            MPIR_Barrier_impl(comm_ptr, &errflag);
	}
#endif /* defined(_OSU_MVAPICH_) */
    }
 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_FENCE);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

/* create_datatype() creates a new struct datatype for the dtype_info
   and the dataloop of the target datatype together with the user data */
#undef FUNCNAME
#define FUNCNAME create_datatype
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int create_datatype(const MPIDI_RMA_dtype_info *dtype_info,
                           const void *dataloop, MPI_Aint dataloop_sz,
                           const void *o_addr, int o_count, MPI_Datatype o_datatype,
                           MPID_Datatype **combined_dtp)
{
    int mpi_errno = MPI_SUCCESS;
    /* datatype_set_contents wants an array 'ints' which is the
       blocklens array with count prepended to it.  So blocklens
       points to the 2nd element of ints to avoid having to copy
       blocklens into ints later. */
    int ints[4];
    int *blocklens = &ints[1];
    MPI_Aint displaces[3];
    MPI_Datatype datatypes[3];
    const int count = 3;
    MPI_Datatype combined_datatype;
    MPIDI_STATE_DECL(MPID_STATE_CREATE_DATATYPE);

    MPIDI_FUNC_ENTER(MPID_STATE_CREATE_DATATYPE);

    /* create datatype */
    displaces[0] = MPIU_PtrToAint(dtype_info);
    blocklens[0] = sizeof(*dtype_info);
    datatypes[0] = MPI_BYTE;
    
    displaces[1] = MPIU_PtrToAint(dataloop);
    blocklens[1] = dataloop_sz;
    datatypes[1] = MPI_BYTE;
    
    displaces[2] = MPIU_PtrToAint(o_addr);
    blocklens[2] = o_count;
    datatypes[2] = o_datatype;
    
    mpi_errno = MPID_Type_struct(count,
                                 blocklens,
                                 displaces,
                                 datatypes,
                                 &combined_datatype);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);
   
    ints[0] = count;

    MPID_Datatype_get_ptr(combined_datatype, *combined_dtp);    
    mpi_errno = MPID_Datatype_set_contents(*combined_dtp,
				           MPI_COMBINER_STRUCT,
				           count+1, /* ints (cnt,blklen) */
				           count, /* aints (disps) */
				           count, /* types */
				           ints,
				           displaces,
				           datatypes);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* Commit datatype */
    
    MPID_Dataloop_create(combined_datatype,
                         &(*combined_dtp)->dataloop,
                         &(*combined_dtp)->dataloop_size,
                         &(*combined_dtp)->dataloop_depth,
                         MPID_DATALOOP_HOMOGENEOUS);
    
    /* create heterogeneous dataloop */
    MPID_Dataloop_create(combined_datatype,
                         &(*combined_dtp)->hetero_dloop,
                         &(*combined_dtp)->hetero_dloop_size,
                         &(*combined_dtp)->hetero_dloop_depth,
                         MPID_DATALOOP_HETEROGENEOUS);
 
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_CREATE_DATATYPE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_rma_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Send_rma_msg(MPIDI_RMA_ops *rma_op, MPID_Win *win_ptr,
				   MPI_Win source_win_handle, 
				   MPI_Win target_win_handle, 
				   MPIDI_RMA_dtype_info *dtype_info, 
				   void **dataloop, MPID_Request **request) 
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_put_t *put_pkt = &upkt.put;
    MPIDI_CH3_Pkt_accum_t *accum_pkt = &upkt.accum;
    MPID_IOV iov[MPID_IOV_LIMIT];
    int mpi_errno=MPI_SUCCESS, predefined;
    int origin_dt_derived, target_dt_derived, origin_type_size, iovcnt; 
    MPIDI_VC_t * vc;
    MPID_Comm *comm_ptr;
    MPID_Datatype *target_dtp=NULL, *origin_dtp=NULL;
#if defined(_OSU_MVAPICH_)
    int iov_n;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
    MPIDI_msg_sz_t total_length;
#endif /* defined(_OSU_MVAPICH_) */
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
    MPIDI_STATE_DECL(MPID_STATE_MEMCPY);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);

#if defined(_OSU_MVAPICH_)
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &vc);
#endif /* defined(_OSU_MVAPICH_) */
    *request = NULL;

    if (rma_op->type == MPIDI_RMA_PUT)
    {
        MPIDI_Pkt_init(put_pkt, MPIDI_CH3_PKT_PUT);
        put_pkt->addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
            win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

        put_pkt->count = rma_op->target_count;
        put_pkt->datatype = rma_op->target_datatype;
        put_pkt->dataloop_size = 0;
        put_pkt->target_win_handle = target_win_handle;
        put_pkt->source_win_handle = source_win_handle;
        
#if defined (_OSU_PSM_) /* psm uses 2-sided, fill up rank */
        put_pkt->target_rank = rma_op->target_rank;
        put_pkt->source_rank = win_ptr->my_rank;
        put_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
        put_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
#endif

#if defined(_OSU_MVAPICH_)
        MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(put_pkt, seqnum);
        MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, put_pkt);
#endif /* defined(_OSU_MVAPICH_) */

        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) put_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*put_pkt);
    }
    else
    {
        MPIDI_Pkt_init(accum_pkt, MPIDI_CH3_PKT_ACCUMULATE);
        accum_pkt->addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
            win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
        accum_pkt->count = rma_op->target_count;
        accum_pkt->datatype = rma_op->target_datatype;
        accum_pkt->dataloop_size = 0;
        accum_pkt->op = rma_op->op;
        accum_pkt->target_win_handle = target_win_handle;
        accum_pkt->source_win_handle = source_win_handle;

        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) accum_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*accum_pkt);
#if defined(_OSU_MVAPICH_)
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(accum_pkt, seqnum);
	MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, accum_pkt);
	DEBUG_PRINT("[%d]set field for accm %d, vc %p, vc->issued %d, source win %p, win null %p\n",
		win_ptr->my_id, accum_pkt->rma_issued, vc, vc->rma_issued, source_win_handle, MPI_WIN_NULL);
#endif /* defined(_OSU_MVAPICH_) */
#if defined (_OSU_PSM_)
        accum_pkt->target_rank = rma_op->target_rank;
        accum_pkt->source_rank = win_ptr->my_rank;
        accum_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
        accum_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
#endif    
    }

    /*    printf("send pkt: type %d, addr %d, count %d, base %d\n", rma_pkt->type,
          rma_pkt->addr, rma_pkt->count, win_ptr->base_addrs[rma_op->target_rank]);
          fflush(stdout);
    */

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(rma_op->origin_datatype, predefined);
    if (!predefined)
    {
        origin_dt_derived = 1;
        MPID_Datatype_get_ptr(rma_op->origin_datatype, origin_dtp);
#if defined (_OSU_PSM_)
        PSM_PRINT("ORIGIN DERIVED");
        if(origin_dtp->is_contig) 
            PSM_PRINT("\n");
        else
            PSM_PRINT("And non-contig\n");
#endif
    }
    else
    {
        origin_dt_derived = 0;
    }

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(rma_op->target_datatype, predefined);
    if (!predefined)
    {
        target_dt_derived = 1;
        MPID_Datatype_get_ptr(rma_op->target_datatype, target_dtp);
#if defined (_OSU_PSM_)
        PSM_PRINT("TARGET DERIVED\n");
        if(target_dtp->is_contig) 
            PSM_PRINT("\n");
        else
            PSM_PRINT(" And non-contig\n");
#endif
    }
    else
    {
        target_dt_derived = 0;
    }

    if (target_dt_derived)
    {
        /* derived datatype on target. fill derived datatype info */
        dtype_info->is_contig = target_dtp->is_contig;
        dtype_info->max_contig_blocks = target_dtp->max_contig_blocks;
        dtype_info->size = target_dtp->size;
        dtype_info->extent = target_dtp->extent;
        dtype_info->dataloop_size = target_dtp->dataloop_size;
        dtype_info->dataloop_depth = target_dtp->dataloop_depth;
        dtype_info->eltype = target_dtp->eltype;
        dtype_info->dataloop = target_dtp->dataloop;
        dtype_info->ub = target_dtp->ub;
        dtype_info->lb = target_dtp->lb;
        dtype_info->true_ub = target_dtp->true_ub;
        dtype_info->true_lb = target_dtp->true_lb;
        dtype_info->has_sticky_ub = target_dtp->has_sticky_ub;
        dtype_info->has_sticky_lb = target_dtp->has_sticky_lb;

	MPIU_CHKPMEM_MALLOC(*dataloop, void *, target_dtp->dataloop_size, 
			    mpi_errno, "dataloop");

	MPIDI_FUNC_ENTER(MPID_STATE_MEMCPY);
        MPIU_Memcpy(*dataloop, target_dtp->dataloop, target_dtp->dataloop_size);
	MPIDI_FUNC_EXIT(MPID_STATE_MEMCPY);
        /* the dataloop can have undefined padding sections, so we need to let
         * valgrind know that it is OK to pass this data to writev later on */
        MPL_VG_MAKE_MEM_DEFINED(*dataloop, target_dtp->dataloop_size);

        if (rma_op->type == MPIDI_RMA_PUT)
	{
            put_pkt->dataloop_size = target_dtp->dataloop_size;
	}
        else
	{
            accum_pkt->dataloop_size = target_dtp->dataloop_size;
	}
    }

    MPID_Datatype_get_size_macro(rma_op->origin_datatype, origin_type_size);

    if (!target_dt_derived)
    {
        /* basic datatype on target */
        if (!origin_dt_derived)
        {
            /* basic datatype on origin */
            iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)rma_op->origin_addr;
            iov[1].MPID_IOV_LEN = (MPIDI_msg_sz_t) rma_op->origin_count * origin_type_size;
            iovcnt = 2;

#if defined(_OSU_MVAPICH_)
            Calculate_IOV_len(iov, iovcnt, total_length);

            if (total_length > vc->eager_max_msg_sz)
            {
              MPIDI_CH3_Pkt_t pkt_rndv;
              int copy_size;
              void* copy_src = NULL;

              if (rma_op->type == MPIDI_RMA_PUT)
              {
                 copy_size = sizeof(MPIDI_CH3_Pkt_put_rndv_t);
                 copy_src = (void *) put_pkt;
              }
              else
              {
                 copy_size = sizeof(MPIDI_CH3_Pkt_accum_rndv_t);
                 copy_src = (void *) accum_pkt;
              }

              memcpy((void *) &pkt_rndv, copy_src, copy_size);

              if (rma_op->type == MPIDI_RMA_PUT)
              {
                 pkt_rndv.type = MPIDI_CH3_PKT_PUT_RNDV;
                 ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz =
                       (MPIDI_msg_sz_t) rma_op->origin_count * origin_type_size;
              }   
              else
              {
                 pkt_rndv.type = MPIDI_CH3_PKT_ACCUMULATE_RNDV;
                 ((MPIDI_CH3_Pkt_accum_rndv_t *) & pkt_rndv)->data_sz =
                       (MPIDI_msg_sz_t) rma_op->origin_count * origin_type_size;
              }

              *request = MPID_Request_create();

              if (*request == NULL)
              {
                  mpi_errno =
                  MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**nomem", 0);
                  MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
                  return mpi_errno;
               }

               MPIU_Object_set_ref(*request, 2);
               (*request)->kind = MPID_REQUEST_SEND;
               (*request)->dev.iov_count = iovcnt;

               int i = 0;

               for (; i < iovcnt; ++i)
               {
                 (*request)->dev.iov[i].MPID_IOV_BUF = iov[i].MPID_IOV_BUF;
                 (*request)->dev.iov[i].MPID_IOV_LEN = iov[i].MPID_IOV_LEN;
               }

               (*request)->dev.iov[0].MPID_IOV_BUF = (void*) &pkt_rndv;
               (*request)->dev.iov[0].MPID_IOV_LEN = copy_size;
               (*request)->dev.OnFinal = 0;
               (*request)->dev.OnDataAvail = 0;

               DEBUG_PRINT
                ("[win fence]iov number %d, iov[1].len %d, data_sz %d (%d)\n",
                   iovcnt, (*request)->dev.iov[1].MPID_IOV_LEN,
                   ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz,
                   rma_op->origin_count * origin_type_size);

               MPIU_THREAD_CS_ENTER(CH3COMM,vc);
               mpi_errno = MPIDI_CH3_iStartRmaRndv(vc, *request, iovcnt - 1)
               MPIU_THREAD_CS_EXIT(CH3COMM,vc);
               MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
"**ch3|rmarndvmsg"); 
            }
            else
            {
#endif /* defined(_OSU_MVAPICH_) */
	    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
            mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt, request));
	    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
            MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
#if defined(_OSU_MVAPICH_)
            }
#endif /* defined(_OSU_MVAPICH_) */            
        }
        else
        {
#if defined (_OSU_PSM_)
            /* Derived data type at origin and predefined */               
            /*if data is contig/non-contig, it is taken care in psm_do_pack fn*/
              /* do packing and pass packing buffer */
              MPID_Request tmp;
            mpi_errno = psm_do_pack(rma_op->origin_count, rma_op->origin_datatype,
                        win_ptr->comm_ptr, &tmp, rma_op->origin_addr,
                        SEGMENT_IGNORE_LAST);
            if(mpi_errno) MPIU_ERR_POP(mpi_errno);

              iov[1].MPID_IOV_BUF = tmp.pkbuf;
              iov[1].MPID_IOV_LEN = tmp.pksz;
              iovcnt = 2;

            MPIU_THREAD_CS_ENTER(CH3COMM,vc);
            mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt,
                        request));
            MPIU_THREAD_CS_EXIT(CH3COMM,vc);

            if(mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
            }    
            goto fn_low;
#endif /* _OSU_PSM_ */

            /* derived datatype on origin */
            *request = MPID_Request_create();
            MPIU_ERR_CHKANDJUMP(*request == NULL,mpi_errno,MPI_ERR_OTHER,"**nomemreq");
            
            MPIU_Object_set_ref(*request, 2);
            (*request)->kind = MPID_REQUEST_SEND;
            
            (*request)->dev.segment_ptr = MPID_Segment_alloc( );
            MPIU_ERR_CHKANDJUMP1((*request)->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");

            (*request)->dev.datatype_ptr = origin_dtp;
            /* this will cause the datatype to be freed when the request
               is freed. */
            MPID_Segment_init(rma_op->origin_addr, rma_op->origin_count,
                              rma_op->origin_datatype,
                              (*request)->dev.segment_ptr, 0);
            (*request)->dev.segment_first = 0;
            (*request)->dev.segment_size = rma_op->origin_count * origin_type_size;

            (*request)->dev.OnFinal = 0;
            (*request)->dev.OnDataAvail = 0;

#if defined(_OSU_MVAPICH_)

            iovcnt = 1;
            iov_n = MPID_IOV_LIMIT - iovcnt;
            mpi_errno = MPIDI_CH3U_Request_load_send_iov(*request,
                                                     &iov[iovcnt],
                                                     &iov_n);   

            iov_n += iovcnt;

            Calculate_IOV_len(iov, iovcnt, total_length);
            total_length += (*request)->dev.segment_size;

            if (total_length > vc->eager_max_msg_sz)
            {
               MPIDI_CH3_Pkt_t pkt_rndv;
               int copy_size;
               void* copy_src = NULL;

               if (MPIDI_RMA_PUT == rma_op->type) {
                 copy_size = sizeof(MPIDI_CH3_Pkt_put_rndv_t);
                 copy_src = (void *) put_pkt;
               } else {
                 copy_size = sizeof(MPIDI_CH3_Pkt_accum_rndv_t);
                 copy_src = (void *) accum_pkt;
               }

               memcpy((void *) &pkt_rndv, copy_src, copy_size);

              if (MPIDI_RMA_PUT == rma_op->type) {
                pkt_rndv.type = MPIDI_CH3_PKT_PUT_RNDV;
                ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz =
                   rma_op->origin_count * origin_type_size;
              } else {
                pkt_rndv.type = MPIDI_CH3_PKT_ACCUMULATE_RNDV;
                ((MPIDI_CH3_Pkt_accum_rndv_t *) & pkt_rndv)->data_sz =
                    rma_op->origin_count * origin_type_size;
              }

              int i = 0;

              for (; i < iov_n; ++i)
              {
                 (*request)->dev.iov[i].MPID_IOV_BUF =
                    iov[i].MPID_IOV_BUF;
                 (*request)->dev.iov[i].MPID_IOV_LEN =
                    iov[i].MPID_IOV_LEN;
              }
              (*request)->dev.iov_count = iov_n;
              (*request)->dev.iov[0].MPID_IOV_BUF = (void *) &pkt_rndv;
              (*request)->dev.iov[0].MPID_IOV_LEN = copy_size;
               /* (*request)->dev.ca = MPIDI_CH3_CA_COMPLETE; */
               MPIU_THREAD_CS_ENTER(CH3COMM,vc);
               mpi_errno = MPIDI_CH3_iStartRmaRndv(vc, *request, iovcnt);
               MPIU_THREAD_CS_EXIT(CH3COMM,vc);

               if (mpi_errno != MPI_SUCCESS) {
                  MPID_Datatype_release((*request)->dev.datatype_ptr);
                  MPIU_Object_set_ref(*request, 0);
                  MPIDI_CH3_Request_destroy(*request);
                  *request = NULL;
                  mpi_errno =
                     MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER, "**ch3|rmamsg",
                                     0);
                  MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
               }

            } else {
         
               MPIU_THREAD_CS_ENTER(CH3COMM,vc);     
               mpi_errno = MPIU_CALL(MPIDI_CH3,iSendv(vc, *request, iov,
iov_n));      
               MPIU_THREAD_CS_EXIT(CH3COMM,vc);
               MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER,
"**ch3|rmamsg");

            }

            goto fn_exit;
#endif

	    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
            mpi_errno = vc->sendNoncontig_fn(vc, *request, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN);
	    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
            MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
        }
    }
    else
    {
#if defined (_OSU_PSM_)
        if (!origin_dt_derived)
        {
          iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)dtype_info;
          iov[1].MPID_IOV_LEN = sizeof(*dtype_info);
          iov[2].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)*dataloop;
          iov[2].MPID_IOV_LEN = target_dtp->dataloop_size;
          iov[3].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)rma_op->origin_addr;
          iov[3].MPID_IOV_LEN = rma_op->origin_count * origin_type_size;
          iovcnt = 4;

          MPIU_THREAD_CS_ENTER(CH3COMM,vc);
          mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt,
                        request));
          MPIU_THREAD_CS_EXIT(CH3COMM,vc);

          if(mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
          }
        } else {
          MPID_Request tmp;
          /*if data is contig/non-contig, it is taken care in psm_do_pack fn*/
          iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)dtype_info;
          iov[1].MPID_IOV_LEN = sizeof(*dtype_info);
          iov[2].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)*dataloop;
          iov[2].MPID_IOV_LEN = target_dtp->dataloop_size;

          mpi_errno=  psm_do_pack(rma_op->origin_count, rma_op->origin_datatype,
                        win_ptr->comm_ptr, &tmp, rma_op->origin_addr,
                        SEGMENT_IGNORE_LAST);
          if(mpi_errno) MPIU_ERR_POP(mpi_errno);
                iov[3].MPID_IOV_BUF = tmp.pkbuf;
                iov[3].MPID_IOV_LEN = tmp.pksz;

          iovcnt = 4;

          MPIU_THREAD_CS_ENTER(CH3COMM,vc);
          mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt,
                       request));
          MPIU_THREAD_CS_EXIT(CH3COMM,vc);

          if(mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
          }
        }
        goto fn_low;
#endif /* _OSU_PSM_ */

#if defined(_OSU_MVAPICH_)
        if(!origin_dt_derived)
        {
            iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)dtype_info;
            iov[1].MPID_IOV_LEN = sizeof(*dtype_info);
            iov[2].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)*dataloop;
            iov[2].MPID_IOV_LEN = target_dtp->dataloop_size;

            iov[3].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)rma_op->origin_addr;
            iov[3].MPID_IOV_LEN = rma_op->origin_count * origin_type_size;
            iovcnt = 4;
        
            Calculate_IOV_len(iov, iovcnt, total_length);

            if (total_length > vc->eager_max_msg_sz)
            {
               MPIDI_CH3_Pkt_t pkt_rndv;
               int copy_size;
               void* copy_src = NULL;

               if (rma_op->type == MPIDI_RMA_PUT)
               {
                 copy_size = sizeof(MPIDI_CH3_Pkt_put_rndv_t);
                 copy_src = (void *) put_pkt;
               }
               else
               {
                 copy_size = sizeof(MPIDI_CH3_Pkt_accum_rndv_t);
                 copy_src = (void *) accum_pkt;
                 ((MPIDI_CH3_Pkt_accum_rndv_t *) & pkt_rndv)->data_sz =
                      rma_op->origin_count * origin_type_size;
               }

               memcpy((void *) &pkt_rndv, copy_src, copy_size);

               if (rma_op->type == MPIDI_RMA_PUT)
               {
                  pkt_rndv.type = MPIDI_CH3_PKT_PUT_RNDV;
                  ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz =
                     rma_op->origin_count * origin_type_size;
               }
               else
               {
                  pkt_rndv.type = MPIDI_CH3_PKT_ACCUMULATE_RNDV;
                  ((MPIDI_CH3_Pkt_accum_rndv_t *) & pkt_rndv)->data_sz =
                     rma_op->origin_count * origin_type_size;
               }

               *request = MPID_Request_create();

               if (*request == NULL)
               {
                  mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE,
                                     FCNAME, __LINE__, MPI_ERR_OTHER,
                                     "**nomem", 0);
                  MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
                  return mpi_errno;
               }

               MPIU_Object_set_ref(*request, 2);
              (*request)->kind = MPID_REQUEST_SEND;
              (*request)->dev.iov_count = iovcnt;

              int i = 0;

              for (; i < iovcnt; ++i)
              {
                (*request)->dev.iov[i].MPID_IOV_BUF = iov[i].MPID_IOV_BUF;
                (*request)->dev.iov[i].MPID_IOV_LEN = iov[i].MPID_IOV_LEN;
              }

              (*request)->dev.iov[0].MPID_IOV_BUF = (void*) &pkt_rndv;
              (*request)->dev.iov[0].MPID_IOV_LEN = copy_size;
              (*request)->dev.OnFinal = 0;
              (*request)->dev.OnDataAvail = 0;

              DEBUG_PRINT
               ("[win fence]iov number %d, iov[1].len %d, data_sz %d (%d)\n",
                 iovcnt, (*request)->dev.iov[1].MPID_IOV_LEN,
                 ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz,
                 rma_op->origin_count * origin_type_size);

              MPIU_THREAD_CS_ENTER(CH3COMM,vc);   
              mpi_errno = MPIDI_CH3_iStartRmaRndv(vc, *request, iovcnt - 1)
              MPIU_THREAD_CS_EXIT(CH3COMM,vc);

              if (mpi_errno != MPI_SUCCESS) {
                 MPID_Datatype_release((*request)->dev.datatype_ptr);
                 MPIU_Object_set_ref(*request, 0);
                 MPIDI_CH3_Request_destroy(*request);
                 *request = NULL;
                 mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER, "**ch3|rmamsg",
                                     0);
                 MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
              }

           } else 
           {

              MPIU_THREAD_CS_ENTER(CH3COMM,vc);
              mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt,
request));
              MPIU_THREAD_CS_EXIT(CH3COMM,vc);

              if (mpi_errno != MPI_SUCCESS) {
                 MPID_Datatype_release((*request)->dev.datatype_ptr);
                 MPIU_Object_set_ref(*request, 0);
                 MPIDI_CH3_Request_destroy(*request);
                 *request = NULL;
                 mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER, "**ch3|rmamsg",
                                     0);
                 MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
              }
           }
        } else 
        {

            iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)dtype_info;
            iov[1].MPID_IOV_LEN = sizeof(*dtype_info);
            iov[2].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)*dataloop;
            iov[2].MPID_IOV_LEN = target_dtp->dataloop_size;
            iovcnt = 3;
   
           *request = MPID_Request_create();
           if (*request == NULL) {
               MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomem");
           }

           MPIU_Object_set_ref(*request, 2);
           (*request)->kind = MPID_REQUEST_SEND;

           (*request)->dev.datatype_ptr = origin_dtp;
           /* this will cause the datatype to be freed when the request
              is freed. */

           (*request)->dev.segment_ptr = MPID_Segment_alloc( );
           /* if (!*request)->dev.segment_ptr) { MPIU_ERR_POP(); } */
           MPID_Segment_init(rma_op->origin_addr, rma_op->origin_count,
                          rma_op->origin_datatype,
                          (*request)->dev.segment_ptr, 0);
           (*request)->dev.segment_first = 0;
           (*request)->dev.segment_size = rma_op->origin_count * origin_type_size;

           iov_n = MPID_IOV_LIMIT - iovcnt;
           /* On the initial load of a send iov req, set the OnFinal action (null
              for point-to-point) */
           (*request)->dev.OnFinal = 0;
           mpi_errno = MPIDI_CH3U_Request_load_send_iov(*request,
                                                     &iov[iovcnt],
                                                     &iov_n);        
           if (mpi_errno) MPIU_ERR_POP(mpi_errno); 
 
           iov_n += iovcnt;            

           Calculate_IOV_len(iov, iovcnt, total_length);
           total_length += (*request)->dev.segment_size;

           if (total_length > vc->eager_max_msg_sz)
           {
              MPIDI_CH3_Pkt_t pkt_rndv;
              int copy_size;
              void* copy_src = NULL;

              if (MPIDI_RMA_PUT == rma_op->type) {
                 copy_size = sizeof(MPIDI_CH3_Pkt_put_rndv_t);
                 copy_src = (void *) put_pkt;
              } else {
                 copy_size = sizeof(MPIDI_CH3_Pkt_accum_rndv_t);
                 copy_src = (void *) accum_pkt;
              }

              memcpy((void *) &pkt_rndv, copy_src, copy_size);

              if (MPIDI_RMA_PUT == rma_op->type) {
                 pkt_rndv.type = MPIDI_CH3_PKT_PUT_RNDV;
                 ((MPIDI_CH3_Pkt_put_rndv_t *) & pkt_rndv)->data_sz =
                 rma_op->origin_count * origin_type_size;
              } else {
                 pkt_rndv.type = MPIDI_CH3_PKT_ACCUMULATE_RNDV;
                 ((MPIDI_CH3_Pkt_accum_rndv_t *) & pkt_rndv)->data_sz =
                 rma_op->origin_count * origin_type_size;
              }

              int i = 0;

              for (; i < iov_n; ++i)
              {
                 (*request)->dev.iov[i].MPID_IOV_BUF =
                    iov[i].MPID_IOV_BUF;
                 (*request)->dev.iov[i].MPID_IOV_LEN =
                    iov[i].MPID_IOV_LEN;
              }
              (*request)->dev.iov_count = iov_n;
              (*request)->dev.iov[0].MPID_IOV_BUF = (void *) &pkt_rndv;
              (*request)->dev.iov[0].MPID_IOV_LEN = copy_size;
              /* (*request)->dev.ca = MPIDI_CH3_CA_COMPLETE; */

              MPIU_THREAD_CS_ENTER(CH3COMM,vc);
              mpi_errno = MPIDI_CH3_iStartRmaRndv(vc, *request, iovcnt);
              MPIU_THREAD_CS_EXIT(CH3COMM,vc);

              if (mpi_errno != MPI_SUCCESS) {
                 MPID_Datatype_release((*request)->dev.datatype_ptr);
                 MPIU_Object_set_ref(*request, 0);
                 MPIDI_CH3_Request_destroy(*request);
                 *request = NULL;
                 mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL,
                                     FCNAME, __LINE__,
                                     MPI_ERR_OTHER, "**ch3|rmamsg",
                                     0);
                 MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
               }
            }
            else
            {
                 MPIU_THREAD_CS_ENTER(CH3COMM,vc);
                 mpi_errno = MPIU_CALL(MPIDI_CH3,iSendv(vc, *request, iov,
iov_n));  
                 MPIU_THREAD_CS_EXIT(CH3COMM,vc);                 

                 if (mpi_errno != MPI_SUCCESS)
                 {
                    MPID_Datatype_release((*request)->dev.datatype_ptr);
                    MPIU_Object_set_ref(*request, 0);
                    MPIDI_CH3_Request_destroy(*request);
                    *request = NULL;
                    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
                 }      

            }
        }        
        MPID_Datatype_release(target_dtp);   
        goto fn_exit; 
#endif
        /* derived datatype on target */
        MPID_Datatype *combined_dtp = NULL;

        *request = MPID_Request_create();
        if (*request == NULL) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomemreq");
        }

        MPIU_Object_set_ref(*request, 2);
        (*request)->kind = MPID_REQUEST_SEND;

	(*request)->dev.segment_ptr = MPID_Segment_alloc( );
        MPIU_ERR_CHKANDJUMP1((*request)->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");

        /* create a new datatype containing the dtype_info, dataloop, and origin data */

        mpi_errno = create_datatype(dtype_info, *dataloop, target_dtp->dataloop_size, rma_op->origin_addr,
                                    rma_op->origin_count, rma_op->origin_datatype, &combined_dtp);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);

        (*request)->dev.datatype_ptr = combined_dtp;
        /* combined_datatype will be freed when request is freed */

        MPID_Segment_init(MPI_BOTTOM, 1, combined_dtp->handle,
                          (*request)->dev.segment_ptr, 0);
        (*request)->dev.segment_first = 0;
        (*request)->dev.segment_size = combined_dtp->size;

        (*request)->dev.OnFinal = 0;
        (*request)->dev.OnDataAvail = 0;

	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
        mpi_errno = vc->sendNoncontig_fn(vc, *request, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN);
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
        MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");

        /* we're done with the datatypes */
        if (origin_dt_derived)
            MPID_Datatype_release(origin_dtp);
        MPID_Datatype_release(target_dtp);
    }    

#if defined (_OSU_PSM_)
 fn_low:    
    if (target_dt_derived)
    {
        MPID_Datatype_release(target_dtp);
    }
#endif    
 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_RMA_MSG);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (*request)
    {
        MPIU_CHKPMEM_REAP();
        if ((*request)->dev.datatype_ptr)
            MPID_Datatype_release((*request)->dev.datatype_ptr);
        MPIU_Object_set_ref(*request, 0);
        MPIDI_CH3_Request_destroy(*request);
    }
    *request = NULL;
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

/*
 * Use this for contiguous accumulate operations
 */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_contig_acc_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Send_contig_acc_msg(MPIDI_RMA_ops *rma_op, 
					  MPID_Win *win_ptr,
					  MPI_Win source_win_handle, 
					  MPI_Win target_win_handle, 
					  MPID_Request **request) 
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_accum_t *accum_pkt = &upkt.accum;
    MPID_IOV iov[MPID_IOV_LIMIT];
    int mpi_errno=MPI_SUCCESS;
    int origin_type_size, iovcnt; 
    MPIDI_VC_t * vc;
    MPID_Comm *comm_ptr;
    int len;
#if defined(_OSU_MVAPICH_)
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
#endif /* defined(_OSU_MVAPICH_) */ 
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SEND_CONTIG_ACC_MSG);
    MPIDI_STATE_DECL(MPID_STATE_MEMCPY);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SEND_CONTIG_ACC_MSG);

    *request = NULL;

    MPID_Datatype_get_size_macro(rma_op->origin_datatype, origin_type_size);
    /* FIXME: Make this size check efficient and match the packet type */
    len = rma_op->origin_count * origin_type_size;
    if (EnableImmedAcc && len <= MPIDI_RMA_IMMED_INTS*sizeof(int)) {
	MPIDI_CH3_Pkt_accum_immed_t * accumi_pkt = &upkt.accum_immed;
	void *dest = accumi_pkt->data, *src = rma_op->origin_addr;

	MPIDI_Pkt_init(accumi_pkt, MPIDI_CH3_PKT_ACCUM_IMMED);
	accumi_pkt->addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
	    win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
	accumi_pkt->count = rma_op->target_count;
	accumi_pkt->datatype = rma_op->target_datatype;
	accumi_pkt->op = rma_op->op;
	accumi_pkt->target_win_handle = target_win_handle;
	accumi_pkt->source_win_handle = source_win_handle;
	
	switch (len) {
	case 1: *(uint8_t *)dest  = *(uint8_t *)src;  break;
	case 2: *(uint16_t *)dest = *(uint16_t *)src; break;
	case 4: *(uint32_t *)dest = *(uint32_t *)src; break;
	case 8: *(uint64_t *)dest = *(uint64_t *)src; break;
	default:
	    MPIU_Memcpy( accumi_pkt->data, (void *)rma_op->origin_addr, len );
	}
	comm_ptr = win_ptr->comm_ptr;
	MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);

#if defined(_OSU_MVAPICH_)
#if defined(MPID_USE_SEQUENCE_NUMBERS)
        MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(accumi_pkt, seqnum);
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
        MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, accumi_pkt);
#endif /* defined(_OSU_MVAPICH_) */

#if defined (_OSU_PSM_)
        accumi_pkt->target_rank = rma_op->target_rank;
        accumi_pkt->source_rank = win_ptr->my_rank;
        accumi_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
        accumi_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
#endif

	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
	mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, accumi_pkt, sizeof(*accumi_pkt), request));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
	goto fn_exit;
    }

#if defined (_OSU_MVAPICH_)
    mpi_errno = MPIDI_CH3I_Send_rma_msg(rma_op, win_ptr,
                       source_win_handle, target_win_handle,
                       &rma_op->dtype_info,
                       &rma_op->dataloop, request);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
#else

    MPIDI_Pkt_init(accum_pkt, MPIDI_CH3_PKT_ACCUMULATE);
    accum_pkt->addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
	win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
    accum_pkt->count = rma_op->target_count;
    accum_pkt->datatype = rma_op->target_datatype;
    accum_pkt->dataloop_size = 0;
    accum_pkt->op = rma_op->op;
    accum_pkt->target_win_handle = target_win_handle;
    accum_pkt->source_win_handle = source_win_handle;

#if defined (_OSU_PSM_)
    accum_pkt->target_rank = rma_op->target_rank;
    accum_pkt->source_rank = win_ptr->my_rank;
    accum_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
    accum_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
#endif   
 
    iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) accum_pkt;
    iov[0].MPID_IOV_LEN = sizeof(*accum_pkt);

    /*    printf("send pkt: type %d, addr %d, count %d, base %d\n", rma_pkt->type,
          rma_pkt->addr, rma_pkt->count, win_ptr->base_addrs[rma_op->target_rank]);
          fflush(stdout);
    */

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);


    /* basic datatype on target */
    /* basic datatype on origin */
    /* FIXME: This is still very heavyweight for a small message operation,
       such as a single word update */
    /* One possibility is to use iStartMsg with a buffer that is just large 
       enough, though note that nemesis has an optimization for this */
    iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)rma_op->origin_addr;
    iov[1].MPID_IOV_LEN = rma_op->origin_count * origin_type_size;
    iovcnt = 2;
    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
    mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt, request));
    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
    MPIU_ERR_CHKANDJUMP(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|rmamsg");
#endif /* _OSU_MVAPICH_ */
 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_CONTIG_ACC_MSG);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    if (*request)
    {
        MPIU_Object_set_ref(*request, 0);
        MPIDI_CH3_Request_destroy(*request);
    }
    *request = NULL;
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}



#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Recv_rma_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Recv_rma_msg(MPIDI_RMA_ops *rma_op, MPID_Win *win_ptr,
				   MPI_Win source_win_handle, 
				   MPI_Win target_win_handle, 
				   MPIDI_RMA_dtype_info *dtype_info, 
				   void **dataloop, MPID_Request **request) 
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_get_t *get_pkt = &upkt.get;
    int mpi_errno=MPI_SUCCESS, predefined;
    MPIDI_VC_t * vc;
    MPID_Comm *comm_ptr;
    MPID_Request *req = NULL;
    MPID_Datatype *dtp;
    MPID_IOV iov[MPID_IOV_LIMIT];
#if defined(_OSU_MVAPICH_)
    int origin_type_size, type_size;
    MPIDI_msg_sz_t total_size;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
#endif /* defined(_OSU_MVAPICH_) */
#if defined(_OSU_PSM_)
    MPID_Datatype *o_dtp;
    int origin_type_size; 
    MPIDI_msg_sz_t total_size;
#endif 


    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RECV_RMA_MSG);
    MPIDI_STATE_DECL(MPID_STATE_MEMCPY);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RECV_RMA_MSG);

    /* create a request, store the origin buf, cnt, datatype in it,
       and pass a handle to it in the get packet. When the get
       response comes from the target, it will contain the request
       handle. */  
    req = MPID_Request_create();
    if (req == NULL) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomemreq");
    }

    *request = req;

    MPIU_Object_set_ref(req, 2);

    req->dev.user_buf = rma_op->origin_addr;
    req->dev.user_count = rma_op->origin_count;
    req->dev.datatype = rma_op->origin_datatype;
    req->dev.target_win_handle = MPI_WIN_NULL;
    req->dev.source_win_handle = source_win_handle;
    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(req->dev.datatype, predefined);
    if (!predefined)
    {
        MPID_Datatype_get_ptr(req->dev.datatype, dtp);
        req->dev.datatype_ptr = dtp;
        /* this will cause the datatype to be freed when the
           request is freed. */  
    }

#if defined(_OSU_MVAPICH_)
    MPID_Datatype_get_size_macro(req->dev.datatype, type_size);
    req->dev.recv_data_sz = (MPIDI_msg_sz_t) type_size * req->dev.user_count;

    mpi_errno = MPIDI_CH3U_Post_data_receive_found(req);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS) {
	mpi_errno =
	    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
	                         __LINE__, MPI_ERR_OTHER,
	                         "**ch3|postrecv",
	                         "**ch3|postrecv %s",
	                         "MPIDI_CH3_PKT_GET_RESP");
    }
    /* --END ERROR HANDLING-- */
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_Pkt_init(get_pkt, MPIDI_CH3_PKT_GET);
    get_pkt->addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
    get_pkt->count = rma_op->target_count;
    get_pkt->datatype = rma_op->target_datatype;
    get_pkt->request_handle = req->handle;
    get_pkt->target_win_handle = target_win_handle;
    get_pkt->source_win_handle = source_win_handle;

#if defined (_OSU_PSM_)
    get_pkt->source_rank = win_ptr->my_rank;
    get_pkt->target_rank = rma_op->target_rank;
    get_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
    get_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
#endif /* _OSU_PSM_ */


/*    printf("send pkt: type %d, addr %d, count %d, base %d\n", rma_pkt->type,
           rma_pkt->addr, rma_pkt->count, win_ptr->base_addrs[rma_op->target_rank]);
    fflush(stdout);
*/
	    
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);

#if defined(_OSU_MVAPICH_)
    MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, get_pkt);
    DEBUG_PRINT("[%d]set field for get %d, vc %p, vc->issued %d\n",
                win_ptr->my_id, get_pkt->rma_issued, vc, vc->rma_issued);
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(rma_op->target_datatype, predefined);
    if (predefined)
    {
#if defined(_OSU_MVAPICH_)
	MPID_Datatype_get_size_macro(rma_op->origin_datatype,
	                             origin_type_size);
	total_size = (MPIDI_msg_sz_t) origin_type_size * rma_op->origin_count +
	    sizeof(MPIDI_CH3_Pkt_get_resp_t);
	if (total_size <= vc->eager_max_msg_sz) {
	    req->mrail.protocol = VAPI_PROTOCOL_EAGER;
	    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	    MPIDI_Pkt_set_seqnum(get_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */

#if defined (_OSU_PSM_)
    MPID_Datatype_get_size_macro(rma_op->origin_datatype, origin_type_size);
    total_size = (MPIDI_msg_sz_t) origin_type_size * rma_op->origin_count + 
        sizeof(MPIDI_CH3_Pkt_t);
    MPID_Datatype_get_ptr(rma_op->origin_datatype, o_dtp);

    if(total_size < vc->eager_max_msg_sz) {
        get_pkt->rndv_mode = 0;
    } else {
        get_pkt->rndv_mode = 1;
        get_pkt->rndv_tag = psm_get_rndvtag();
        int target_tsz;
        MPID_Datatype_get_size_macro(rma_op->target_datatype, target_tsz);

        if(o_dtp->is_contig) {
            get_pkt->rndv_len = target_tsz*rma_op->target_count;
        } else {
            get_pkt->rndv_len = target_tsz*rma_op->target_count;
            req->dev.real_user_buf = req->dev.user_buf;
            req->dev.user_buf = MPIU_Malloc(get_pkt->rndv_len);
            req->psm_flags |= PSM_RNDVRECV_GET_PACKED;
            PSM_PRINT("GET: ORIGIN is unpack-needed Rlen %d\n", get_pkt->rndv_len);
            //ODOT: needs free
        }
    }

#endif /* _OSU_PSM_ */

        /* basic datatype on target. simply send the get_pkt. */
	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
        mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, get_pkt, sizeof(*get_pkt), &req));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
#if defined(_OSU_MVAPICH_)
	} else {
	    MPIDI_CH3_Pkt_get_rndv_t get_rndv;
	    MPIDI_Pkt_init(&get_rndv, MPIDI_CH3_PKT_GET_RNDV);
	    memcpy((void *) &get_rndv, (void *) get_pkt,
	           sizeof(MPIDI_CH3_Pkt_get_t));
	    /*The packed type should be set again because the memcpy would have 
	      have overwritten it*/ 
	    get_rndv.type = MPIDI_CH3_PKT_GET_RNDV;

	    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	    MPIDI_Pkt_set_seqnum(&get_rndv, seqnum);
        MPIU_THREAD_CS_ENTER(CH3COMM,vc); 
	    mpi_errno = MPIDI_CH3_iStartGetRndv(vc, &get_rndv, req, NULL, 0);
        MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	    req = NULL;
	}
#endif /* defined(_OSU_MVAPICH_) */
    }
    else
    {
        /* derived datatype on target. fill derived datatype info and
           send it along with get_pkt. */

        MPID_Datatype_get_ptr(rma_op->target_datatype, dtp);
        dtype_info->is_contig = dtp->is_contig;
        dtype_info->max_contig_blocks = dtp->max_contig_blocks;
        dtype_info->size = dtp->size;
        dtype_info->extent = dtp->extent;
        dtype_info->dataloop_size = dtp->dataloop_size;
        dtype_info->dataloop_depth = dtp->dataloop_depth;
        dtype_info->eltype = dtp->eltype;
        dtype_info->dataloop = dtp->dataloop;
        dtype_info->ub = dtp->ub;
        dtype_info->lb = dtp->lb;
        dtype_info->true_ub = dtp->true_ub;
        dtype_info->true_lb = dtp->true_lb;
        dtype_info->has_sticky_ub = dtp->has_sticky_ub;
        dtype_info->has_sticky_lb = dtp->has_sticky_lb;

	MPIU_CHKPMEM_MALLOC(*dataloop, void *, dtp->dataloop_size, 
			    mpi_errno, "dataloop");

	MPIDI_FUNC_ENTER(MPID_STATE_MEMCPY);
        MPIU_Memcpy(*dataloop, dtp->dataloop, dtp->dataloop_size);
	MPIDI_FUNC_EXIT(MPID_STATE_MEMCPY);

        /* the dataloop can have undefined padding sections, so we need to let
         * valgrind know that it is OK to pass this data to writev later on */
        MPL_VG_MAKE_MEM_DEFINED(*dataloop, dtp->dataloop_size);

        get_pkt->dataloop_size = dtp->dataloop_size;

        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)get_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*get_pkt);
        iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)dtype_info;
        iov[1].MPID_IOV_LEN = sizeof(*dtype_info);
        iov[2].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)*dataloop;
        iov[2].MPID_IOV_LEN = dtp->dataloop_size;
#if defined (_OSU_PSM_)
    MPID_Datatype_get_size_macro(rma_op->origin_datatype, origin_type_size);
    total_size = (MPIDI_msg_sz_t) origin_type_size * rma_op->origin_count + 
        sizeof(MPIDI_CH3_Pkt_t);
    MPID_Datatype_get_ptr(rma_op->origin_datatype, o_dtp);

    if(total_size < vc->eager_max_msg_sz) {
        get_pkt->rndv_mode = 0;
    } else {
        PSM_PRINT("GET: large packet needed\n");
        get_pkt->rndv_mode = 1;
        get_pkt->rndv_tag = psm_get_rndvtag();

        /* target is contiguous */
        if(dtp->is_contig) {
            PSM_PRINT("GET: target contiguous\n");
            int origin_tsz, target_tsz;
            /* origin is contiguous */
            if(o_dtp->is_contig) {
                MPID_Datatype_get_size_macro(rma_op->origin_datatype,
                        origin_tsz);
                MPID_Datatype_get_size_macro(rma_op->target_datatype,
                        target_tsz);
                assert((origin_tsz*rma_op->origin_count) ==
                        (target_tsz*rma_op->target_count));
                get_pkt->rndv_len = target_tsz * rma_op->target_count;
                PSM_PRINT("GET: origin contig\n");
            } else { /* origin is non-contiguous */
                get_pkt->rndv_len = target_tsz * rma_op->target_count;
                req->dev.real_user_buf = req->dev.user_buf;
                req->dev.user_buf = MPIU_Malloc(get_pkt->rndv_len);
                req->psm_flags |= PSM_RNDVRECV_GET_PACKED;
                PSM_PRINT("GET: origin vector\n");
            }
        }  else { /* target is non-contiguous */
            PSM_PRINT("GET: target non-contiguous\n");
            MPI_Pack_size(rma_op->target_count, rma_op->target_datatype,
                          MPI_COMM_SELF, &(get_pkt->rndv_len));
            req->dev.real_user_buf = req->dev.user_buf;
            req->dev.user_buf = MPIU_Malloc(get_pkt->rndv_len);
            req->psm_flags |= PSM_RNDVRECV_GET_PACKED;
        }
    }
#endif /* _OSU_PSM_ */

        
#if defined(_OSU_MVAPICH_)
	MPID_Datatype_get_size_macro(rma_op->origin_datatype,
	                             origin_type_size);
	total_size = (MPIDI_msg_sz_t) origin_type_size * rma_op->origin_count
	    + sizeof(MPIDI_CH3_Pkt_get_resp_t);

	if (total_size <= vc->eager_max_msg_sz) {
	    /* basic datatype on target. simply send the get_pkt. */
	    req->mrail.protocol = VAPI_PROTOCOL_EAGER;
	    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	    MPIDI_Pkt_set_seqnum(get_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */	

	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
        mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, 3, &req));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);

#if defined(_OSU_MVAPICH_)
	} else {
	    MPIDI_CH3_Pkt_get_rndv_t get_rndv;
            MPIDI_Pkt_init(&get_rndv, MPIDI_CH3_PKT_GET_RNDV);
            MPIU_Memcpy((void *) &get_rndv, (void *) get_pkt,
                   sizeof(MPIDI_CH3_Pkt_get_t));
            /*The packed type should be set again because the memcpy would have 
              have overwritten it*/
            get_rndv.type = MPIDI_CH3_PKT_GET_RNDV;
	    req->mrail.protocol = VAPI_PROTOCOL_RPUT;
	    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	    MPIDI_Pkt_set_seqnum(&get_rndv, seqnum);
        MPIU_THREAD_CS_ENTER(CH3COMM,vc);
	    mpi_errno = MPIDI_CH3_iStartGetRndv(vc, &get_rndv, req, &iov[1], 2);
        MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	    req = NULL;
	}
#endif /* defined(_OSU_MVAPICH_) */

        /* release the target datatype */
        MPID_Datatype_release(dtp);
    }

    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
    }

    /* release the request returned by iStartMsg or iStartMsgv */
    if (req != NULL)
    {
        MPID_Request_release(req);
    }

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RECV_RMA_MSG);
    return mpi_errno;

    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_Win_post
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_post(MPID_Group *post_grp_ptr, int assert, MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS;
    MPID_Group *win_grp_ptr;
    int i, post_grp_size, *ranks_in_post_grp, *ranks_in_win_grp, dst, rank;
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    int local_rank, my_local_rank;
#endif /* !DAPL_DEFAULT_PROVIDER*/
    MPID_Comm *win_comm_ptr;
    MPIU_CHKLMEM_DECL(4);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_POST);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_POST);

    /* Even though we would want to reset the fence counter to keep
     * the user from using the previous fence to mark the beginning of
     * a fence epoch if he switched from fence to lock-unlock
     * synchronization, we cannot do this because fence_cnt must be
     * updated collectively */

    /* In case this process was previously the target of passive target rma
     * operations, we need to take care of the following...
     * Since we allow MPI_Win_unlock to return without a done ack from
     * the target in the case of multiple rma ops and exclusive lock,
     * we need to check whether there is a lock on the window, and if
     * there is a lock, poke the progress engine until the operations
     * have completed and the lock is therefore released. */
    if (win_ptr->current_lock_type != MPID_LOCK_NONE)
    {
	MPID_Progress_state progress_state;
	
	MPIU_INSTR_DURATION_START(winpost_clearlock);
	/* poke the progress engine */
	MPID_Progress_start(&progress_state);
	while (win_ptr->current_lock_type != MPID_LOCK_NONE)
	{
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS) {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
	    }
	    /* --END ERROR HANDLING-- */
	    MPIU_INSTR_DURATION_INCR(winpost_clearlock,0,1);
	}
	MPID_Progress_end(&progress_state);
	MPIU_INSTR_DURATION_END(winpost_clearlock);
    }
        
    post_grp_size = post_grp_ptr->size;
        
    /* initialize the completion counter */
    win_ptr->my_counter = post_grp_size;

#if defined(_OSU_MVAPICH_)
    if (win_ptr->fall_back != 1) {
	MPIU_Memset((void *) win_ptr->completion_counter, 0,
	      sizeof(long long) * win_ptr->comm_size * rdma_num_rails);
    }

#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback) 
#else
    if (!win_ptr->shm_fallback) 
#endif
    {
       MPIU_Memset(win_ptr->shm_cmpl_counter_me, 0,
                 sizeof(long long) * win_ptr->shm_l_ranks);
    }
#endif /* !DAPL_DEFAULT_PROVIDER*/
#endif /* defined(_OSU_MVAPICH_) */

    if ((assert & MPI_MODE_NOCHECK) == 0)
    {
        MPI_Request *req;
        MPI_Status *status;

	MPIU_INSTR_DURATION_START(winpost_sendsync);
 
	/* NOCHECK not specified. We need to notify the source
	   processes that Post has been called. */  
	
	/* We need to translate the ranks of the processes in
	   post_group to ranks in win_ptr->comm_ptr, so that we
	   can do communication */
            
	MPIU_CHKLMEM_MALLOC(ranks_in_post_grp, int *, 
			    post_grp_size * sizeof(int),
			    mpi_errno, "ranks_in_post_grp");
	MPIU_CHKLMEM_MALLOC(ranks_in_win_grp, int *, 
			    post_grp_size * sizeof(int),
			    mpi_errno, "ranks_in_win_grp");
        
	for (i=0; i<post_grp_size; i++)
	{
	    ranks_in_post_grp[i] = i;
	}
        
	win_comm_ptr = win_ptr->comm_ptr;

        mpi_errno = MPIR_Comm_group_impl(win_comm_ptr, &win_grp_ptr);
	if (mpi_errno) MPIU_ERR_POP(mpi_errno);
	

        mpi_errno = MPIR_Group_translate_ranks_impl(post_grp_ptr, post_grp_size, ranks_in_post_grp,
                                                    win_grp_ptr, ranks_in_win_grp);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
	
        rank = win_ptr->myrank;
	
	MPIU_CHKLMEM_MALLOC(req, MPI_Request *, post_grp_size * sizeof(MPI_Request), mpi_errno, "req");
        MPIU_CHKLMEM_MALLOC(status, MPI_Status *, post_grp_size*sizeof(MPI_Status), mpi_errno, "status");

#if defined(_OSU_MVAPICH_)
        MPIDI_VC_t* vc = 0;
#endif /* defined(_OSU_MVAPICH_) */
	/* Send a 0-byte message to the source processes */
	MPIU_INSTR_DURATION_INCR(winpost_sendsync,0,post_grp_size);
	for (i = 0; i < post_grp_size; i++) {
	    dst = ranks_in_win_grp[i];
#if defined(_OSU_MVAPICH_)
            if (SMP_INIT) {
	            MPIDI_Comm_get_vc(win_ptr->comm_ptr, dst, &vc);
            }
#endif /* defined(_OSU_MVAPICH_) */

	    /* FIXME: Short messages like this shouldn't normally need a 
	       request - this should consider using the ch3 call to send
	       a short message and return a request only if the message is
	       not delivered. */
	    if (dst != rank) {
#if defined(_OSU_MVAPICH_)
               req[i] = MPI_REQUEST_NULL;
#if !defined (DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
               if (!win_ptr->limic_fallback || !win_ptr->shm_fallback) 
#else 
               if (!win_ptr->shm_fallback)
#endif
               {
                   if (SMP_INIT && vc->smp.local_nodes != -1)
                   {
                       local_rank = win_ptr->shm_g2l_rank[dst];
                       my_local_rank = win_ptr->shm_g2l_rank[rank];
                       *(win_ptr->shm_post_flag_all[local_rank] 
                                + my_local_rank) = 1;
                   } else if (win_ptr->fall_back != 1) {
                       MPIDI_CH3I_RDMA_post(win_ptr, dst);
                   } else {
                       MPID_Request *req_ptr;
                       mpi_errno = MPID_Isend(&i, 0, MPI_INT, dst, SYNC_POST_TAG, win_comm_ptr,
                                       MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
                       if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                       req[i] = req_ptr->handle;
                   } 
               }
               else 
               {
#endif /* !DAPL_DEFAULT_PROVIDER*/
                 if (win_ptr->fall_back != 1
                       && (!SMP_INIT || vc->smp.local_nodes == -1))
                 {
                     MPIDI_CH3I_RDMA_post(win_ptr, dst);
                 } 
                 else
                 {
                     MPID_Request *req_ptr;
		         mpi_errno = MPID_Isend(&i, 0, MPI_INT, dst, SYNC_POST_TAG, win_comm_ptr,
                                       MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
		        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                     req[i] = req_ptr->handle;
                 } 
#if !defined(DAPL_DEFAULT_PROVIDER)
               }
#endif /* !DAPL_DEFAULT_PROVIDER*/
#else  /* defined(_OSU_MVAPICH_)*/
               MPID_Request *req_ptr;
                   mpi_errno = MPID_Isend(&i, 0, MPI_INT, dst, SYNC_POST_TAG, win_comm_ptr,
                                MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
               if (mpi_errno) MPIU_ERR_POP(mpi_errno);
               req[i] = req_ptr->handle;
#endif /* defined(_OSU_MVAPICH_) */
	        } else {
                req[i] = MPI_REQUEST_NULL;
            }
        }

        mpi_errno = MPIR_Waitall_impl(post_grp_size, req, status);
        if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) MPIU_ERR_POP(mpi_errno);

        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < post_grp_size; i++) {
                if (status[i].MPI_ERROR != MPI_SUCCESS) {
                    mpi_errno = status[i].MPI_ERROR;
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        }
        /* --END ERROR HANDLING-- */

        mpi_errno = MPIR_Group_free_impl(win_grp_ptr);
	if (mpi_errno) MPIU_ERR_POP(mpi_errno);
	MPIU_INSTR_DURATION_END(winpost_sendsync);
    }

 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_POST);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Win_start
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_start(MPID_Group *group_ptr, int assert, MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_START);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_START);

    /* Even though we would want to reset the fence counter to keep
     * the user from using the previous fence to mark the beginning of
     * a fence epoch if he switched from fence to lock-unlock
     * synchronization, we cannot do this because fence_cnt must be
     * updated collectively */

    /* In case this process was previously the target of passive target rma
     * operations, we need to take care of the following...
     * Since we allow MPI_Win_unlock to return without a done ack from
     * the target in the case of multiple rma ops and exclusive lock,
     * we need to check whether there is a lock on the window, and if
     * there is a lock, poke the progress engine until the operations
     * have completed and the lock is therefore released. */
    if (win_ptr->current_lock_type != MPID_LOCK_NONE)
    {
	MPID_Progress_state progress_state;
	
	MPIU_INSTR_DURATION_START(winstart_clearlock);
	/* poke the progress engine */
	MPID_Progress_start(&progress_state);
	while (win_ptr->current_lock_type != MPID_LOCK_NONE)
	{
	    mpi_errno = MPID_Progress_wait(&progress_state);
	    /* --BEGIN ERROR HANDLING-- */
	    if (mpi_errno != MPI_SUCCESS) {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
	    }
	    /* --END ERROR HANDLING-- */
	    MPIU_INSTR_DURATION_INCR(winstart_clearlock,0,1);
	}
	MPID_Progress_end(&progress_state);
	MPIU_INSTR_DURATION_END(winstart_clearlock);
    }
    
    win_ptr->start_group_ptr = group_ptr;
    MPIR_Group_add_ref( group_ptr );
    win_ptr->start_assert = assert;

 fn_fail:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_START);
    return mpi_errno;
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Win_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_complete(MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int comm_size, *nops_to_proc, src, new_total_op_count;
    int i, j, dst, done, total_op_count, *curr_ops_cnt;
    MPIDI_RMA_ops *curr_ptr, *tmpptr, **prevNextPtr;
    MPID_Comm *comm_ptr;
    MPI_Win source_win_handle, target_win_handle;
    MPID_Group *win_grp_ptr;
    int start_grp_size, *ranks_in_start_grp, *ranks_in_win_grp, rank;
    MPIU_CHKLMEM_DECL(9);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_COMPLETE);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_COMPLETE);

    comm_ptr = win_ptr->comm_ptr;
    comm_size = comm_ptr->local_size;
        
    /* Translate the ranks of the processes in
       start_group to ranks in win_ptr->comm_ptr */
    
    start_grp_size = win_ptr->start_group_ptr->size;

    MPIU_INSTR_DURATION_START(wincomplete_recvsync);
    MPIU_CHKLMEM_MALLOC(ranks_in_start_grp, int *, start_grp_size*sizeof(int), 
			mpi_errno, "ranks_in_start_grp");
        
    MPIU_CHKLMEM_MALLOC(ranks_in_win_grp, int *, start_grp_size*sizeof(int), 
			mpi_errno, "ranks_in_win_grp");
        
    for (i=0; i<start_grp_size; i++)
    {
	ranks_in_start_grp[i] = i;
    }
        
    mpi_errno = MPIR_Comm_group_impl(comm_ptr, &win_grp_ptr);
    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }

    mpi_errno = MPIR_Group_translate_ranks_impl(win_ptr->start_group_ptr, start_grp_size,
                                                ranks_in_start_grp,
                                                win_grp_ptr, ranks_in_win_grp);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    rank = win_ptr->myrank;

#if defined(_OSU_MVAPICH_)
    if (win_ptr->fall_back != 1) {
        /* If 1 sided implementation is defined, finish all pending RDMA
        * operations */
        if ((win_ptr->start_assert & MPI_MODE_NOCHECK) == 0)
        {
            MPIDI_CH3I_RDMA_start(win_ptr, start_grp_size, ranks_in_win_grp);
        }
        MPIDI_CH3I_RDMA_try_rma(win_ptr, 0);
        if (win_ptr->rma_issued != 0) {
            MPIDI_CH3I_RDMA_finish_rma(win_ptr);
        }
        MPIDI_CH3I_RDMA_complete(win_ptr, start_grp_size,
	                             ranks_in_win_grp);
    }

#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_) 
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback) 
#endif
    {        
        MPIDI_VC_t* vc = NULL;
 
        /* If MPI_MODE_NOCHECK was not specified, we need to check if
           Win_post was called on the target processes. Wait for a 0-byte sync
           message from each target process */
        if ((win_ptr->start_assert & MPI_MODE_NOCHECK) == 0)
        {
            MPI_Request *req;
            MPI_Status *status;
            MPID_Request *req_ptr;
           
            MPIU_CHKLMEM_MALLOC(req, MPI_Request *, start_grp_size*sizeof(MPI_Request), mpi_errno, "req");
            MPIU_CHKLMEM_MALLOC(status, MPI_Status *, start_grp_size*sizeof(MPI_Status), mpi_errno, "status");

            MPIU_INSTR_DURATION_INCR(wincomplete_recvsync,0,start_grp_size);

            /*check for post from intranode peers*/ 
            MPIDI_CH3I_INTRANODE_start(win_ptr, start_grp_size, ranks_in_win_grp);

            for (i=0; i<start_grp_size; i++)
            {
               src = ranks_in_win_grp[i];
               MPIDI_Comm_get_vc(comm_ptr, src, &vc);
               if (src != rank &&
                    vc->smp.local_nodes == -1 && win_ptr->fall_back == 1) 
               {
                   mpi_errno = MPID_Irecv(NULL, 0, MPI_INT, src, SYNC_POST_TAG,
                                       comm_ptr, MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
                   if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                   req[i] = req_ptr->handle;
                } else {
                    req[i] = MPI_REQUEST_NULL;
                }
            }

            mpi_errno = MPIR_Waitall_impl(start_grp_size, req, status);
            if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) MPIU_ERR_POP(mpi_errno);
   
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno == MPI_ERR_IN_STATUS) {
                for (i = 0; i < start_grp_size; i++) {
                    if (status[i].MPI_ERROR != MPI_SUCCESS) {
                        mpi_errno = status[i].MPI_ERROR;
                        MPIU_ERR_POP(mpi_errno);
                    }
                }
            }
            /* --END ERROR HANDLING-- */ 
        }

        win_ptr->rma_issued = 0;

#if defined(_SMP_LIMIC_)
        if (!win_ptr->limic_fallback) {
            MPIDI_CH3I_LIMIC_try_rma(win_ptr);
        }
#endif
        if (!win_ptr->shm_fallback) {
            mpi_errno = MPIDI_CH3I_SHM_try_rma(win_ptr);
            if (mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_POP(mpi_errno);
            }
        }

        if (win_ptr->rma_issued != 0 || win_ptr->rma_ops_list_head == NULL) {
            MPIDI_CH3I_INTRANODE_complete(win_ptr, start_grp_size, 
                    ranks_in_win_grp);
        }
    }
    else 
    {
#endif /* !DAPL_DEFAULT_PROVIDER*/

       if (SMP_INIT || (!SMP_INIT && win_ptr->fall_back == 1))
       {
          MPIDI_VC_t* vc = NULL;
 
       /* If MPI_MODE_NOCHECK was not specified, we need to check if
          Win_post was called on the target processes. Wait for a 0-byte sync
          message from each target process */
 
          if ((win_ptr->start_assert & MPI_MODE_NOCHECK) == 0)
          {
             MPI_Request *req;
             MPI_Status *status;
             MPID_Request *req_ptr;
             
             MPIU_CHKLMEM_MALLOC(req, MPI_Request *, start_grp_size*sizeof(MPI_Request), mpi_errno, "req");
             MPIU_CHKLMEM_MALLOC(status, MPI_Status *, start_grp_size*sizeof(MPI_Status), mpi_errno, "status");

             MPIU_INSTR_DURATION_INCR(wincomplete_recvsync,0,start_grp_size);
             for (i=0; i<start_grp_size; i++)
             {
                 src = ranks_in_win_grp[i];
 
                 if (SMP_INIT) 
                 {
                     MPIDI_Comm_get_vc(comm_ptr, src, &vc);
                     if (src != rank &&
                         (vc->smp.local_nodes != -1 || win_ptr->fall_back == 1))
                     {
                         mpi_errno = MPID_Irecv(NULL, 0, MPI_INT, src, SYNC_POST_TAG,
                                             comm_ptr, MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
                         if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                         req[i] = req_ptr->handle;
                     } else { 
                         req[i] = MPI_REQUEST_NULL;
                     }
                  }
                  else if (src != rank && win_ptr->fall_back == 1)
                  {
                         mpi_errno = MPID_Irecv(NULL, 0, MPI_INT, src, SYNC_POST_TAG,
                                             comm_ptr, MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
                         if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                          req[i] = req_ptr->handle;
                  } else {
                      req[i] = MPI_REQUEST_NULL;
                  }
              }

              mpi_errno = MPIR_Waitall_impl(start_grp_size, req, status);
              if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) MPIU_ERR_POP(mpi_errno);
   
              /* --BEGIN ERROR HANDLING-- */
              if (mpi_errno == MPI_ERR_IN_STATUS) {
                  for (i = 0; i < start_grp_size; i++) {
                      if (status[i].MPI_ERROR != MPI_SUCCESS) {
                          mpi_errno = status[i].MPI_ERROR;
                          MPIU_ERR_POP(mpi_errno);
                      }
                  }
              }
              /* --END ERROR HANDLING-- */ 
          }
       }
#if !defined(DAPL_DEFAULT_PROVIDER)
    }
#endif /* !DAPL_DEFAULT_PROVIDER */
#else  /* defined(_OSU_MVAPICH_) */

    /* If MPI_MODE_NOCHECK was not specified, we need to check if
       Win_post was called on the target processes. Wait for a 0-byte sync
       message from each target process */
    if ((win_ptr->start_assert & MPI_MODE_NOCHECK) == 0)
    {
        MPI_Request *req;
        MPI_Status *status;

        MPIU_CHKLMEM_MALLOC(req, MPI_Request *, start_grp_size*sizeof(MPI_Request), mpi_errno, "req");
        MPIU_CHKLMEM_MALLOC(status, MPI_Status *, start_grp_size*sizeof(MPI_Status), mpi_errno, "status");

	MPIU_INSTR_DURATION_INCR(wincomplete_recvsync,0,start_grp_size);
	for (i = 0; i < start_grp_size; i++) {
	    src = ranks_in_win_grp[i];
	    if (src != rank) {
                MPID_Request *req_ptr;
		/* FIXME: This is a heavyweight way to process these sync 
		   messages - this should be handled with a special packet
		   type and callback function.
		*/
                mpi_errno = MPID_Irecv(NULL, 0, MPI_INT, src, SYNC_POST_TAG,
                                       comm_ptr, MPID_CONTEXT_INTRA_PT2PT, &req_ptr);
		if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                req[i] = req_ptr->handle;
	    } else {
                req[i] = MPI_REQUEST_NULL;
            }

	}
        mpi_errno = MPIR_Waitall_impl(start_grp_size, req, status);
        if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) MPIU_ERR_POP(mpi_errno);

        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < start_grp_size; i++) {
                if (status[i].MPI_ERROR != MPI_SUCCESS) {
                    mpi_errno = status[i].MPI_ERROR;
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        }
        /* --END ERROR HANDLING-- */
    }
    MPIU_INSTR_DURATION_END(wincomplete_recvsync);
#endif /* defined(_OSU_MVAPICH_) */

    /* keep track of no. of ops to each proc. Needed for knowing
       whether or not to decrement the completion counter. The
       completion counter is decremented only on the last
       operation. */

    MPIU_INSTR_DURATION_START(wincomplete_issue);

    MPIU_CHKLMEM_MALLOC(nops_to_proc, int *, comm_size*sizeof(int), 
			mpi_errno, "nops_to_proc");
    for (i=0; i<comm_size; i++) nops_to_proc[i] = 0;

    total_op_count = 0;
    curr_ptr = win_ptr->rma_ops_list_head;
    while (curr_ptr != NULL)
    {
	nops_to_proc[curr_ptr->target_rank]++;
	total_op_count++;
	curr_ptr = curr_ptr->next;
    }

    MPIU_INSTR_DURATION_INCR(wincomplete_issue,0,total_op_count);
    MPIU_INSTR_DURATION_MAX(wincomplete_issue,1,total_op_count);

    /* We allocate a few extra requests because if there are no RMA
       ops to a target process, we need to send a 0-byte message just
       to decrement the completion counter. */
        
    MPIU_CHKLMEM_MALLOC(curr_ops_cnt, int *, comm_size*sizeof(int),
			mpi_errno, "curr_ops_cnt");
    for (i=0; i<comm_size; i++) curr_ops_cnt[i] = 0;
        
    i = 0;
    prevNextPtr = &win_ptr->rma_ops_list_head;
    curr_ptr    = win_ptr->rma_ops_list_head;
    while (curr_ptr != NULL)
    {
	/* The completion counter at the target is decremented only on 
	   the last RMA operation. We indicate the last operation by 
	   passing the source_win_handle only on the last operation. 
	   Otherwise, we pass NULL */
	if (curr_ops_cnt[curr_ptr->target_rank] ==
	    nops_to_proc[curr_ptr->target_rank] - 1) 
	    source_win_handle = win_ptr->handle;
	else 
	    source_win_handle = MPI_WIN_NULL;
	
	target_win_handle = win_ptr->all_win_handles[curr_ptr->target_rank];

	curr_ptr->dataloop = 0;
	switch (curr_ptr->type)
	{
	case (MPIDI_RMA_PUT):
	case (MPIDI_RMA_ACCUMULATE):
	    mpi_errno = MPIDI_CH3I_Send_rma_msg(curr_ptr, win_ptr,
				source_win_handle, target_win_handle, 
				&curr_ptr->dtype_info,
				&curr_ptr->dataloop, &curr_ptr->request); 
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    break;
	case MPIDI_RMA_ACC_CONTIG:
	    mpi_errno = MPIDI_CH3I_Send_contig_acc_msg(curr_ptr, win_ptr,
				       source_win_handle, target_win_handle, 
				       &curr_ptr->request );
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    break;
	case (MPIDI_RMA_GET):
	    mpi_errno = MPIDI_CH3I_Recv_rma_msg(curr_ptr, win_ptr,
				source_win_handle, target_win_handle, 
				&curr_ptr->dtype_info, 
				&curr_ptr->dataloop, &curr_ptr->request);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    break;
	default:
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winInvalidOp");
	}
	i++;
	curr_ops_cnt[curr_ptr->target_rank]++;
	/* If the request is null, we can remove it immediately */
	if (!curr_ptr->request) {
	    if (curr_ptr->dataloop != NULL) {
		MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
						  recv_rma_msg */
	    }
	    tmpptr       = curr_ptr->next;
	    *prevNextPtr = tmpptr;
	    MPIU_Free( curr_ptr );
	    curr_ptr     = tmpptr;
	}
	else  {
	    prevNextPtr = &curr_ptr->next;
	    curr_ptr    = curr_ptr->next;
	}
    }
    MPIU_INSTR_DURATION_END(wincomplete_issue);
        
    /* If the start_group included some processes that did not end up
       becoming targets of  RMA operations from this process, we need
       to send a dummy message to those processes just to decrement
       the completion counter */
        
    j = i;
    new_total_op_count = total_op_count;
    for (i=0; i<start_grp_size; i++)
    {
	dst = ranks_in_win_grp[i];
	if (dst == rank) {
	    /* FIXME: MT: this has to be done atomically */
	    win_ptr->my_counter -= 1;
	}
	else if (nops_to_proc[dst] == 0)
	{
	    MPIDI_CH3_Pkt_t upkt;
	    MPIDI_CH3_Pkt_put_t *put_pkt = &upkt.put;
	    MPIDI_VC_t * vc;
	    MPID_Request *request;
	    
#if defined(_OSU_MVAPICH_) 
#if defined(MPID_USE_SEQUENCE_NUMBERS)
            MPID_Seqnum_t seqnum;
#endif
	        MPIDI_Comm_get_vc(comm_ptr, dst, &vc);
            if ((!SMP_INIT || vc->smp.local_nodes == -1) 
                && win_ptr->fall_back != 1) {
                continue;
            }
#if !defined (DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
            if (vc->smp.local_nodes != -1 
                && win_ptr->limic_fallback != 1) {
                continue;
            }
#endif
            if (vc->smp.local_nodes != -1 
                && win_ptr->shm_fallback != 1) {
                continue;
            }        
#endif /* !DAPL_DEFAULT_PROVIDER */
#endif /* defined(_OSU_MVAPICH_) */
	    MPIDI_Pkt_init(put_pkt, MPIDI_CH3_PKT_PUT);
	    put_pkt->addr = NULL;
	    put_pkt->count = 0;
#if defined (_OSU_PSM_)
            put_pkt->rndv_mode = 0;
            put_pkt->source_rank = win_ptr->my_rank;
            put_pkt->target_rank = dst;
            put_pkt->mapped_srank = win_ptr->rank_mapping[win_ptr->my_rank];
            put_pkt->mapped_trank = win_ptr->rank_mapping[dst];
#endif    
	    put_pkt->datatype = MPI_INT;
	    put_pkt->target_win_handle = win_ptr->all_win_handles[dst];
	    put_pkt->source_win_handle = win_ptr->handle;
	    
#if defined(_OSU_MVAPICH_)
            MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, put_pkt);
	    
            MPIDI_VC_FAI_send_seqnum(vc, seqnum);
            MPIDI_Pkt_set_seqnum(put_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */	    

	    MPIDI_Comm_get_vc_set_active(comm_ptr, dst, &vc);
	    
	    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
	    mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, put_pkt,
						      sizeof(*put_pkt),
						      &request));
	    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	    if (mpi_errno != MPI_SUCCESS) {
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg" );
	    }
	    /* In the unlikely event that a request is returned (the message
	       is not sent yet), add it to the list of pending operations */
	    if (request) {
		/* Its hard to use the automatic allocator here, as those 
		   macros are optimized for a known maximum number of items. */
		MPIDI_RMA_ops *new_ptr;
		MPIU_INSTR_DURATION_START(rmaqueue_alloc);
		new_ptr = (MPIDI_RMA_ops *)MPIU_Malloc(sizeof(MPIDI_RMA_ops) );
		MPIU_INSTR_DURATION_END(rmaqueue_alloc);
		/* --BEGIN ERROR HANDLING-- */
		if (!new_ptr) {
		    MPIU_CHKMEM_SETERR(mpi_errno,sizeof(MPIDI_RMA_ops),
					"RMA operation entry");
		    goto fn_fail;
		}
		/* --END ERROR HANDLING-- */
		if (win_ptr->rma_ops_list_tail) 
		    win_ptr->rma_ops_list_tail->next = new_ptr;
		else
		    win_ptr->rma_ops_list_head = new_ptr;
		win_ptr->rma_ops_list_tail = new_ptr;
		new_ptr->next     = NULL;
		new_ptr->request  = request;
		new_ptr->dataloop = 0;
	    }
	    j++;
	    new_total_op_count++;
	}
    }

    if (new_total_op_count)
    {
	MPID_Progress_state progress_state;
	
	done = 1;
	MPIU_INSTR_DURATION_START(wincomplete_complete);
	MPID_Progress_start(&progress_state);
	while (win_ptr->rma_ops_list_head) {
	    prevNextPtr = &win_ptr->rma_ops_list_head;
	    curr_ptr    = win_ptr->rma_ops_list_head;
	    do {
		if (MPID_Request_is_complete(curr_ptr->request)) {
		    /* Once we find a complete request, we complete
		       as many as possible until we find an incomplete
		       or null request */
		    do {
			mpi_errno = curr_ptr->request->status.MPI_ERROR;
			/* --BEGIN ERROR HANDLING-- */
			if (mpi_errno != MPI_SUCCESS) {
			    MPID_Progress_end(&progress_state);
			    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
			}
			/* --END ERROR HANDLING-- */
			MPID_Request_release(curr_ptr->request);
			if (curr_ptr->dataloop != NULL) {
			    MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
							      recv_rma_msg */
			}
			/* We can remove and free this rma op element */
			tmpptr       = curr_ptr->next;
			*prevNextPtr = tmpptr;
			MPIU_Free( curr_ptr );
			curr_ptr     = tmpptr;
		    }
		    while (curr_ptr &&
			   MPID_Request_is_complete(curr_ptr->request));
		    /* Once a request completes, we wait for another
		       operation to arrive rather than check the
		       rest of the requests.  */
		    break;
		}
		else {
		    prevNextPtr = &curr_ptr->next;
		    curr_ptr    = curr_ptr->next;
		    break;
		}
	    } while (curr_ptr);

	    /* Wait for something to arrive*/
	    /* In some tests, this hung unless the test ensured that 
	       there was an incomplete request. */
	    curr_ptr = win_ptr->rma_ops_list_head;
	    if (curr_ptr && !MPID_Request_is_complete(curr_ptr->request) ) {
		MPIU_INSTR_DURATION_START(wincomplete_block);
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
		MPIU_INSTR_DURATION_END(wincomplete_block);
	    }
	} /* While list of rma operation is non-empty */
	    
	    
	MPID_Progress_end(&progress_state);
    }

    MPIU_Assert( !win_ptr->rma_ops_list_head );
    win_ptr->rma_ops_list_head = NULL;
    win_ptr->rma_ops_list_tail = NULL;
    
    mpi_errno = MPIR_Group_free_impl(win_grp_ptr);
    if (mpi_errno) MPIU_ERR_POP(mpi_errno);

    /* free the group stored in window */
    MPIR_Group_release(win_ptr->start_group_ptr);
    win_ptr->start_group_ptr = NULL; 
    
 fn_exit:
    MPIU_CHKLMEM_FREEALL();
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_COMPLETE);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}



#undef FUNCNAME
#define FUNCNAME MPIDI_Win_wait
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_wait(MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS;
#if defined(_OSU_MVAPICH_)
    int newly_finished, num_wait_completions, index;
    int i,j;
#endif /* defined(_OSU_MVAPICH_) */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_WAIT);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_WAIT);

    /* wait for all operations from other processes to finish */
#if defined(_OSU_MVAPICH_)
    while (win_ptr->outstanding_rma != 0) { 
        mpi_errno = MPID_Progress_test();
        if (mpi_errno != MPI_SUCCESS) {
            MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_WAIT);
            return mpi_errno;
        }
    }

    num_wait_completions = 0;
    while (win_ptr->my_counter) {
        newly_finished = 0; 
        if (win_ptr->fall_back != 1) {
            for (i = 0; i < win_ptr->comm_size; ++i) {
                for (j = 0; j < rdma_num_rails; ++j) {
                    index = i*rdma_num_rails+j;
                    if (win_ptr->completion_counter[index] == 1){
                        win_ptr->completion_counter[index] = 0;
                        ++num_wait_completions;
                        if (num_wait_completions == rdma_num_rails) {
                            ++newly_finished;
                            num_wait_completions = 0;
                         }
                    }
                }
            }
        }
#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_)
        if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)  
#else
        if (!win_ptr->shm_fallback) 
#endif 
        {
            for (i = 0; i < win_ptr->shm_l_ranks; ++i) {
                if(*((volatile long long *) 
                    &win_ptr->shm_cmpl_counter_me[i]) == 1)
                {
                    ++newly_finished;
                    *((volatile long long *)
                        &win_ptr->shm_cmpl_counter_me[i]) = 0;
                }
            }
        }
#endif /* !DAPL_DEFAULT_PROVIDER */ 
        win_ptr->my_counter -= newly_finished;
        if (win_ptr->my_counter == 0)
      	    break;
	    mpi_errno = MPID_Progress_test();
        if (mpi_errno != MPI_SUCCESS) {
            MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_WAIT);
            return mpi_errno;
        }
    }
#else /* _OSU_MVAPICH_ */
    if (win_ptr->my_counter)
    {
    	MPID_Progress_state progress_state;
	
    	MPIU_INSTR_DURATION_START(winwait_wait);
    	MPID_Progress_start(&progress_state);
    	while (win_ptr->my_counter)
    	{
    	    mpi_errno = MPID_Progress_wait(&progress_state);
    	    /* --BEGIN ERROR HANDLING-- */
    	    if (mpi_errno != MPI_SUCCESS)
    	    {
        		MPID_Progress_end(&progress_state);
        		MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_WAIT);
        		return mpi_errno;
    	    }
    	    /* --END ERROR HANDLING-- */
    	    MPIU_INSTR_DURATION_INCR(winwait_wait,0,1)
    	}
    	MPID_Progress_end(&progress_state);
    	MPIU_INSTR_DURATION_END(winwait_wait);
    } 
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_WAIT);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_Win_test
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_test(MPID_Win *win_ptr, int *flag)
{
    int mpi_errno=MPI_SUCCESS;
#if defined(_OSU_MVAPICH_)
    int i,j,index;
    static int num_wait_completions = 0;
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_TEST);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_TEST);

#if defined(_OSU_MVAPICH_)
    if (!win_ptr->fall_back) {
        for (i = 0; i < win_ptr->comm_size; ++i) {
           for (j = 0; j < rdma_num_rails; ++j) {
                index = i*rdma_num_rails+j;
                if (win_ptr->completion_counter[index] == 1){
                    win_ptr->completion_counter[index] = 0;
                    ++num_wait_completions;
                    if (num_wait_completions == rdma_num_rails) {
                        --win_ptr->my_counter;
                        num_wait_completions = 0;
                    } 
                }
           }
        } 
    }
#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined(_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback) 
#else
    if (!win_ptr->shm_fallback) 
#endif
    {
       for (i = 0; i < win_ptr->comm_size; ++i) {
           if(win_ptr->shm_cmpl_counter_me[i] == 1) {
               --win_ptr->my_counter;
               win_ptr->shm_cmpl_counter_me[i] = 0;
           }
       }
    }
#endif /* !DAPL_DEFAULT_PROVIDER */ 
#endif /* _OSU_MVAPICH_ */

    mpi_errno = MPID_Progress_test();
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_POP(mpi_errno);
    }

    *flag = (win_ptr->my_counter) ? 0 : 1;

 fn_fail:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_TEST);
    return mpi_errno;
}




#undef FUNCNAME
#define FUNCNAME MPIDI_Win_lock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_lock(int lock_type, int dest, int assert, MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_RMA_ops *new_ptr;
    MPID_Comm *comm_ptr;
    MPIU_CHKPMEM_DECL(1);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_LOCK);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_LOCK);

    MPIU_UNREFERENCED_ARG(assert);

    /* Even though we would want to reset the fence counter to keep
     * the user from using the previous fence to mark the beginning of
     * a fence epoch if he switched from fence to lock-unlock
     * synchronization, we cannot do this because fence_cnt must be
     * updated collectively */

    if (dest == MPI_PROC_NULL) goto fn_exit;
        
    comm_ptr = win_ptr->comm_ptr;

    if (dest == win_ptr->myrank) {
	/* The target is this process itself. We must block until the lock
	 * is acquired. */
            
	/* poke the progress engine until lock is granted */
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    /* if SHM lock are being used, one should not block in 
     * progress engine for a completeion as the lock can be 
     * released directly by an intra-node peer */
#if defined (_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback)
#endif
    {
        if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, lock_type) == 0)
        {
    	    MPIU_INSTR_DURATION_START(winlock_getlocallock);
            while (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, lock_type) == 0) 
            {
                mpi_errno = MPID_Progress_test();
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno != MPI_SUCCESS) {
                    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
                }
                /* --END ERROR HANDLING-- */
            }
            MPIU_INSTR_DURATION_END(winlock_getlocallock);
        }
    } else {
#endif
	if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, lock_type) == 0)
	{
	    MPID_Progress_state progress_state;
	    
	    MPIU_INSTR_DURATION_START(winlock_getlocallock);
	    MPID_Progress_start(&progress_state);
	    while (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, lock_type) == 0) 
	    {
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
	    }
	    MPID_Progress_end(&progress_state);
	    MPIU_INSTR_DURATION_END(winlock_getlocallock);
	}
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    }
#endif

	/* local lock acquired. local puts, gets, accumulates will be done 
	   directly without queueing. */
    }
    else {
	/* target is some other process. add the lock request to rma_ops_list */
	MPIU_INSTR_DURATION_START(rmaqueue_alloc);
	MPIU_CHKPMEM_MALLOC(new_ptr, MPIDI_RMA_ops *, sizeof(MPIDI_RMA_ops), 
			    mpi_errno, "RMA operation entry");
	MPIU_INSTR_DURATION_END(rmaqueue_alloc);
            
	MPIU_Assert( !win_ptr->rma_ops_list_head );
	win_ptr->rma_ops_list_head = new_ptr;
	win_ptr->rma_ops_list_tail = new_ptr;
        
	new_ptr->next = NULL;  
	new_ptr->type = MPIDI_RMA_LOCK;
	new_ptr->target_rank = dest;
	new_ptr->lock_type = lock_type;
    }
#if defined(_OSU_MVAPICH_)
    win_ptr->using_lock = 1;
#endif /* defined(_OSU_MVAPICH_) */

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_LOCK);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    MPIU_CHKPMEM_REAP();
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}

#undef FUNCNAME
#define FUNCNAME MPIDI_Win_unlock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_Win_unlock(int dest, MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS;
    int single_op_opt, type_size;
    MPIDI_RMA_ops *rma_op, *curr_op;
    MPID_Comm *comm_ptr;
    MPID_Request *req=NULL; 
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_lock_t *lock_pkt = &upkt.lock;
    MPIDI_VC_t * vc;
    int wait_for_rma_done_pkt = 0, predefined;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_WIN_UNLOCK);
    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_WIN_UNLOCK);
#if defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS) */

    if (dest == MPI_PROC_NULL) goto fn_exit;
        
    if (dest == win_ptr->myrank) {
	/* local lock. release the lock on the window, grant the next one
	 * in the queue, and return. */
	MPIU_Assert(!win_ptr->rma_ops_list_head);
	mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
	if (mpi_errno != MPI_SUCCESS) goto fn_exit;
	mpi_errno = MPID_Progress_poke();
	goto fn_exit;
    }
        
    comm_ptr = win_ptr->comm_ptr;

    rma_op = win_ptr->rma_ops_list_head;
    
    /* win_lock was not called. return error */
    if ( (rma_op == NULL) || (rma_op->type != MPIDI_RMA_LOCK) ) { 
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**rmasync");
    }
        
    if (rma_op->target_rank != dest) {
	/* The target rank is different from the one passed to win_lock! */
	MPIU_ERR_SETANDJUMP2(mpi_errno,MPI_ERR_OTHER,"**winunlockrank", 
		     "**winunlockrank %d %d", dest, rma_op->target_rank);
    }
        
    if (rma_op->next == NULL) {
	/* only win_lock called, no put/get/acc. Do nothing and return. */
	MPIU_Free(rma_op);
	win_ptr->rma_ops_list_head = NULL;
	win_ptr->rma_ops_list_tail = NULL;
	goto fn_exit;
    }

#if defined (_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER) 
    MPIDI_Comm_get_vc(comm_ptr, dest, &vc);
#if defined (_SMP_LIMIC_)
    if ((!win_ptr->limic_fallback || !win_ptr->shm_fallback)
        && vc->smp.local_nodes != -1)
#else
    if (!win_ptr->shm_fallback && vc->smp.local_nodes != -1)
#endif
    {
        /*Acquire the lock*/
        win_ptr->lock_granted = MPIDI_CH3I_SHM_win_lock(dest, 
                                    rma_op->lock_type, win_ptr, 1);
        if(!win_ptr->lock_granted) {
            MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**rmasync");
        } 

        /*Try and complete operations through SHM backed window*/
        win_ptr->rma_ops_list_head = win_ptr->rma_ops_list_head->next;
        if (!win_ptr->shm_fallback) {
            mpi_errno = MPIDI_CH3I_SHM_try_rma(win_ptr);
#if defined(_SMP_LIMIC_)
        } else {
            mpi_errno = MPIDI_CH3I_LIMIC_try_rma(win_ptr);
#endif
        }
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }

        if (win_ptr->rma_ops_list_head != NULL) {
            /* If some oprations could not be completed through SHM backed window, do 
             * do them using send/recv. Re-insert lock op as it is used by 
             * do_passive_target_rma.        
             * Window will be unlocked by the remote process when the last op 
             * completes */
            rma_op->next = win_ptr->rma_ops_list_head;
            win_ptr->rma_ops_list_head = rma_op;
            mpi_errno = MPIDI_CH3I_Do_passive_target_rma(win_ptr,
                                 &wait_for_rma_done_pkt);
            if (mpi_errno) { MPIU_ERR_POP(mpi_errno); } 

            if (wait_for_rma_done_pkt == 1) {
                /* poke the progress engine until lock_granted flag is reset to 0 */
                if (win_ptr->lock_granted != 0) {
                    MPID_Progress_state progress_state;

                    MPID_Progress_start(&progress_state);
                    while (win_ptr->lock_granted != 0) {
                        mpi_errno = MPID_Progress_wait(&progress_state);
                        /* --BEGIN ERROR HANDLING-- */
                        if (mpi_errno != MPI_SUCCESS) {
                        MPID_Progress_end(&progress_state);
                        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
                        }
                    /* --END ERROR HANDLING-- */
                    }
                    MPID_Progress_end(&progress_state);
                }
            } else { 
                win_ptr->lock_granted = 0;
            }
        } else {
            /*If all operations completed, free the lock op and unlock window*/  
            MPIU_Free(rma_op);
            MPIDI_CH3I_SHM_win_unlock(dest, win_ptr);
        }

        win_ptr->using_lock = 0;
        goto fn_exit;
    }
#endif /*_OSU_MVAPICH_ && && !DAPL_DEFAULT_PROVIDER*/       
 
    single_op_opt = 0;
 
#if !defined (_OSU_PSM_) 
    MPIDI_Comm_get_vc_set_active(comm_ptr, dest, &vc);

    if (rma_op->next->next == NULL) {
	/* Single put, get, or accumulate between the lock and unlock. If it
	 * is of small size and predefined datatype at the target, we
	 * do an optimization where the lock and the RMA operation are
	 * sent in a single packet. Otherwise, we send a separate lock
	 * request first. */

	curr_op = rma_op->next;
	
	MPID_Datatype_get_size_macro(curr_op->origin_datatype, type_size);
	
	MPIDI_CH3I_DATATYPE_IS_PREDEFINED(curr_op->target_datatype, predefined);

	/* msg_sz typically = 65480 */
	if ( predefined &&
	     (type_size * curr_op->origin_count <= vc->eager_max_msg_sz) ) {
	    single_op_opt = 1;
	    /* Set the lock granted flag to 1 */
	    win_ptr->lock_granted = 1;
	    if (curr_op->type == MPIDI_RMA_GET) {
		mpi_errno = MPIDI_CH3I_Send_lock_get(win_ptr);
		wait_for_rma_done_pkt = 0;
	    }
	    else {
		mpi_errno = MPIDI_CH3I_Send_lock_put_or_acc(win_ptr);
		wait_for_rma_done_pkt = 1;
	    }
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	}
    }
#endif
        
    if (single_op_opt == 0) {

	/* Send a lock packet over to the target. wait for the lock_granted
	 * reply. Then do all the RMA ops. */ 
	
	MPIDI_Pkt_init(lock_pkt, MPIDI_CH3_PKT_LOCK);
	lock_pkt->target_win_handle = win_ptr->all_win_handles[dest];
	lock_pkt->source_win_handle = win_ptr->handle;
	lock_pkt->lock_type = rma_op->lock_type;
#if defined (_OSU_PSM_)
    lock_pkt->source_rank = comm_ptr->rank;
    lock_pkt->target_rank = rma_op->target_rank;
    lock_pkt->mapped_srank = win_ptr->rank_mapping[comm_ptr->rank];
    lock_pkt->mapped_trank = win_ptr->rank_mapping[rma_op->target_rank];
#endif /* _OSU_PSM_ */
	
	/* Set the lock granted flag to 0 */
	win_ptr->lock_granted = 0;
	
#if defined(_OSU_MVAPICH_)
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(lock_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */

	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
	mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, lock_pkt, sizeof(*lock_pkt), &req));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
	}
	
	/* release the request returned by iStartMsg */
	if (req != NULL)
	{
	    MPID_Request_release(req);
	}
	
	/* After the target grants the lock, it sends a lock_granted
	 * packet. This packet is received in ch3u_handle_recv_pkt.c.
	 * The handler for the packet sets the win_ptr->lock_granted flag to 1.
	 */
	
	/* poke the progress engine until lock_granted flag is set to 1 */
	if (win_ptr->lock_granted == 0)
	{
	    MPID_Progress_state progress_state;
	    
	    MPIU_INSTR_DURATION_START(winunlock_getlock);
	    MPID_Progress_start(&progress_state);
	    while (win_ptr->lock_granted == 0)
	    {
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
	    }
	    MPID_Progress_end(&progress_state);
	    MPIU_INSTR_DURATION_END(winunlock_getlock);
	}
	
	/* Now do all the RMA operations */
	mpi_errno = MPIDI_CH3I_Do_passive_target_rma(win_ptr, 
						     &wait_for_rma_done_pkt);
	if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    }
        
    /* If the lock is a shared lock or we have done the single op
       optimization, we need to wait until the target informs us that
       all operations are done on the target. */ 
    if (wait_for_rma_done_pkt == 1) {
	/* wait until the "pt rma done" packet is received from the 
	   target. This packet resets the win_ptr->lock_granted flag back to 
	   0. */
	
	/* poke the progress engine until lock_granted flag is reset to 0 */
	if (win_ptr->lock_granted != 0)
	{
	    MPID_Progress_state progress_state;
	    
	    MPID_Progress_start(&progress_state);
	    while (win_ptr->lock_granted != 0)
	    {
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
	    }
	    MPID_Progress_end(&progress_state);
	}
    }
    else
	win_ptr->lock_granted = 0; 
    
#if defined(_OSU_MVAPICH_)
    win_ptr->using_lock = 0;
#endif /* defined(_OSU_MVAPICH_) */

 fn_exit:
    MPIU_Assert( !win_ptr->rma_ops_list_head );
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_WIN_UNLOCK);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Do_passive_target_rma
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Do_passive_target_rma(MPID_Win *win_ptr, 
					    int *wait_for_rma_done_pkt)
{
    int mpi_errno = MPI_SUCCESS, done, i, nops;
    MPIDI_RMA_ops *curr_ptr;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops **prevNextPtr, *tmpptr;
    MPI_Win source_win_handle, target_win_handle;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_DO_PASSIVE_TARGET_RMA);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_DO_PASSIVE_TARGET_RMA);

    if (win_ptr->rma_ops_list_head->lock_type == MPI_LOCK_EXCLUSIVE) {
        /* exclusive lock. no need to wait for rma done pkt at the end */
        *wait_for_rma_done_pkt = 0;
    }
    else {
        /* shared lock. check if any of the rma ops is a get. If so, move it 
           to the end of the list and do it last, in which case an rma done 
           pkt is not needed. If there is no get, rma done pkt is needed */

        if (win_ptr->rma_ops_list_tail->type == MPIDI_RMA_GET) {
            /* last operation is a get. no need to wait for rma done pkt */
            *wait_for_rma_done_pkt = 0;
        }
        else {
            /* go through the list and move the first get operation 
               (if there is one) to the end.  Note that the first
	       operation must be a lock, so we can skip it */
            
            curr_ptr = win_ptr->rma_ops_list_head->next;
            prevNextPtr = &(win_ptr->rma_ops_list_head->next);
            
            *wait_for_rma_done_pkt = 1;
            
            while (curr_ptr != NULL) {
                if (curr_ptr->type == MPIDI_RMA_GET) {
		    /* Found a GET, move it to the end */
                    *wait_for_rma_done_pkt = 0;
		    win_ptr->rma_ops_list_tail->next = curr_ptr;
		    *prevNextPtr = curr_ptr->next;
		    curr_ptr->next = NULL;
		    win_ptr->rma_ops_list_tail = curr_ptr;
                    break;
                }
                else {
                    prevNextPtr = &(curr_ptr->next);
                    curr_ptr    = curr_ptr->next;
                }
            }
        }
    }

    comm_ptr = win_ptr->comm_ptr;

    /* Ignore the first op in the list because it is a win_lock and do
       the rest */

    /* 
       This list has a head (lock) (but no tail (unlock)) that is not 
       processed, so we must skip over that head 
    */

    curr_ptr = win_ptr->rma_ops_list_head->next;
    nops = 0;
    while (curr_ptr != NULL) {
        nops++;
        curr_ptr = curr_ptr->next;
    }
    
    MPIU_INSTR_DURATION_START(winunlock_issue);
    i = 0;

    /* Remove the lock entry */
    curr_ptr = win_ptr->rma_ops_list_head;
    tmpptr       = curr_ptr->next;
    win_ptr->rma_ops_list_head = tmpptr;
    MPIU_Free( curr_ptr );

    prevNextPtr = &win_ptr->rma_ops_list_head;
    curr_ptr    = win_ptr->rma_ops_list_head;
    target_win_handle = win_ptr->all_win_handles[curr_ptr->target_rank];
    while (curr_ptr != NULL)
    {
        /* To indicate the last RMA operation, we pass the
           source_win_handle only on the last operation. Otherwise, 
           we pass MPI_WIN_NULL. */
	/* Could also be curr_ptr->next == NULL */
        if (/*i == nops - 1*/!curr_ptr->next)
            source_win_handle = win_ptr->handle;
        else 
            source_win_handle = MPI_WIN_NULL;
        
	curr_ptr->dataloop = 0;
        switch (curr_ptr->type)
        {
        case (MPIDI_RMA_PUT):  /* same as accumulate */
        case (MPIDI_RMA_ACCUMULATE):
            win_ptr->pt_rma_puts_accs[curr_ptr->target_rank]++;
            mpi_errno = MPIDI_CH3I_Send_rma_msg(curr_ptr, win_ptr,
				source_win_handle, target_win_handle, 
				&curr_ptr->dtype_info,
                                &curr_ptr->dataloop, &curr_ptr->request);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
            break;
	case MPIDI_RMA_ACC_CONTIG:
        win_ptr->pt_rma_puts_accs[curr_ptr->target_rank]++;
	    mpi_errno = MPIDI_CH3I_Send_contig_acc_msg(curr_ptr, win_ptr,
				       source_win_handle, target_win_handle, 
				       &curr_ptr->request );
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
	    break;
        case (MPIDI_RMA_GET):
            mpi_errno = MPIDI_CH3I_Recv_rma_msg(curr_ptr, win_ptr,
                         source_win_handle, target_win_handle, 
				&curr_ptr->dtype_info,
                                &curr_ptr->dataloop, &curr_ptr->request);
	    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
            break;
        default:
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winInvalidOp");
        }
        i++;
	/* If the request is null, we can remove it immediately */
	if (!curr_ptr->request) {
	    if (curr_ptr->dataloop != NULL) {
		MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
						  recv_rma_msg */
	    }
	    tmpptr       = curr_ptr->next;
	    *prevNextPtr = tmpptr;
	    MPIU_Free( curr_ptr );
	    curr_ptr     = tmpptr;
	}
	else  {
	    prevNextPtr = &curr_ptr->next;
	    curr_ptr    = curr_ptr->next;
	}
    }
    MPIU_INSTR_DURATION_END(winunlock_issue);
    
    if (nops)
    {
	MPID_Progress_state progress_state;
	
	done = 1;
	MPIU_INSTR_DURATION_START(winunlock_complete);
	MPID_Progress_start(&progress_state);
	while (win_ptr->rma_ops_list_head) {
	    prevNextPtr = &win_ptr->rma_ops_list_head;
	    curr_ptr    = win_ptr->rma_ops_list_head;
	    do {
		if (MPID_Request_is_complete(curr_ptr->request)) {
		    /* Once we find a complete request, we complete
		       as many as possible until we find an incomplete
		       or null request */
		    do {
			mpi_errno = curr_ptr->request->status.MPI_ERROR;
			/* --BEGIN ERROR HANDLING-- */
			if (mpi_errno != MPI_SUCCESS) {
			    MPID_Progress_end(&progress_state);
			    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
			}
			/* --END ERROR HANDLING-- */
			MPID_Request_release(curr_ptr->request);
			if (curr_ptr->dataloop != NULL) {
			    MPIU_Free(curr_ptr->dataloop); /* allocated in send_rma_msg or 
							      recv_rma_msg */
			}
			/* We can remove and free this rma op element */
			tmpptr       = curr_ptr->next;
			*prevNextPtr = tmpptr;
			MPIU_Free( curr_ptr );
			curr_ptr     = tmpptr;
		    }
		    while (curr_ptr &&
			   MPID_Request_is_complete(curr_ptr->request));
		    /* Once a request completes, we wait for another
		       operation to arrive rather than check the
		       rest of the requests.  */
		    break;
		}
		else {
		    prevNextPtr = &curr_ptr->next;
		    curr_ptr    = curr_ptr->next;
		    break;
		}
	    } while (curr_ptr);
	    
	    /* Wait for something to arrive*/
	    /* In some tests, this hung unless the test ensured that 
	       there was an incomplete request. */
	    curr_ptr = win_ptr->rma_ops_list_head;
	    if (curr_ptr && !MPID_Request_is_complete(curr_ptr->request) ) {
		MPIU_INSTR_DURATION_START(winunlock_block);
		mpi_errno = MPID_Progress_wait(&progress_state);
		/* --BEGIN ERROR HANDLING-- */
		if (mpi_errno != MPI_SUCCESS) {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winnoprogress");
		}
		/* --END ERROR HANDLING-- */
		MPIU_INSTR_DURATION_END(winunlock_block);
	    }
	} /* While list of rma operation is non-empty */
	    
	MPID_Progress_end(&progress_state);
    } 
    

    MPIU_Assert( !win_ptr->rma_ops_list_head );

    win_ptr->rma_ops_list_head = NULL;
    win_ptr->rma_ops_list_tail = NULL;

 fn_exit:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_DO_PASSIVE_TARGET_RMA);
    return mpi_errno;
    /* --BEGIN ERROR HANDLING-- */
 fn_fail:
    goto fn_exit;
    /* --END ERROR HANDLING-- */
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_lock_put_or_acc
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Send_lock_put_or_acc(MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS, lock_type, origin_dt_derived, iovcnt;
    MPIDI_RMA_ops *rma_op;
    MPID_Request *request=NULL;
    MPIDI_VC_t * vc;
    MPID_IOV iov[MPID_IOV_LIMIT];
    MPID_Comm *comm_ptr;
    MPID_Datatype *origin_dtp=NULL;
    int origin_type_size, predefined;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_lock_put_unlock_t *lock_put_unlock_pkt = 
	&upkt.lock_put_unlock;
    MPIDI_CH3_Pkt_lock_accum_unlock_t *lock_accum_unlock_pkt = 
	&upkt.lock_accum_unlock;
#if defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS) */
        
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SEND_LOCK_PUT_OR_ACC);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SEND_LOCK_PUT_OR_ACC);

    lock_type = win_ptr->rma_ops_list_head->lock_type;

    rma_op = win_ptr->rma_ops_list_head->next;

    win_ptr->pt_rma_puts_accs[rma_op->target_rank]++;

#if defined(_OSU_MVAPICH_)
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &vc);
#endif /* defined(_OSU_MVAPICH_) */

    if (rma_op->type == MPIDI_RMA_PUT) {
        MPIDI_Pkt_init(lock_put_unlock_pkt, MPIDI_CH3_PKT_LOCK_PUT_UNLOCK);
        lock_put_unlock_pkt->target_win_handle = 
            win_ptr->all_win_handles[rma_op->target_rank];
        lock_put_unlock_pkt->source_win_handle = win_ptr->handle;
        lock_put_unlock_pkt->lock_type = lock_type;
 
        lock_put_unlock_pkt->addr = 
            (char *) win_ptr->base_addrs[rma_op->target_rank] +
            win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
        
        lock_put_unlock_pkt->count = rma_op->target_count;
        lock_put_unlock_pkt->datatype = rma_op->target_datatype;

#if defined(_OSU_MVAPICH_)
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(lock_put_unlock_pkt, seqnum);
        MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, lock_put_unlock_pkt);
#endif /* defined(_OSU_MVAPICH_) */

        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) lock_put_unlock_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*lock_put_unlock_pkt);
    }
    
    else if (rma_op->type == MPIDI_RMA_ACCUMULATE) {        
        MPIDI_Pkt_init(lock_accum_unlock_pkt, MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK);
        lock_accum_unlock_pkt->target_win_handle = 
            win_ptr->all_win_handles[rma_op->target_rank];
        lock_accum_unlock_pkt->source_win_handle = win_ptr->handle;
        lock_accum_unlock_pkt->lock_type = lock_type;

        lock_accum_unlock_pkt->addr = 
            (char *) win_ptr->base_addrs[rma_op->target_rank] +
            win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
        
        lock_accum_unlock_pkt->count = rma_op->target_count;
        lock_accum_unlock_pkt->datatype = rma_op->target_datatype;
        lock_accum_unlock_pkt->op = rma_op->op;

#if defined(_OSU_MVAPICH_)
	MPIDI_VC_FAI_send_seqnum(vc, seqnum);
	MPIDI_Pkt_set_seqnum(lock_accum_unlock_pkt, seqnum);
	MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, lock_accum_unlock_pkt);
#endif /* defined(_OSU_MVAPICH_) */
        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) lock_accum_unlock_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*lock_accum_unlock_pkt);
    }
    else if (rma_op->type == MPIDI_RMA_ACC_CONTIG) {
        MPIDI_Pkt_init(lock_accum_unlock_pkt, MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK);
        lock_accum_unlock_pkt->target_win_handle = 
            win_ptr->all_win_handles[rma_op->target_rank];
        lock_accum_unlock_pkt->source_win_handle = win_ptr->handle;
        lock_accum_unlock_pkt->lock_type = lock_type;

        lock_accum_unlock_pkt->addr = 
            (char *) win_ptr->base_addrs[rma_op->target_rank] +
            win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
        
        lock_accum_unlock_pkt->count = rma_op->target_count;
        lock_accum_unlock_pkt->datatype = rma_op->target_datatype;
        lock_accum_unlock_pkt->op = rma_op->op;

#if defined(_OSU_MVAPICH_)
        MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(lock_accum_unlock_pkt, seqnum);
        MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, lock_accum_unlock_pkt);
#endif /* defined(_OSU_MVAPICH_) */

        iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) lock_accum_unlock_pkt;
        iov[0].MPID_IOV_LEN = sizeof(*lock_accum_unlock_pkt);
    }
    else {
	/* FIXME: Error return */
	printf( "expected short accumulate...\n" );
	/* */
    }

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(rma_op->origin_datatype, predefined);
    if (!predefined)
    {
        origin_dt_derived = 1;
        MPID_Datatype_get_ptr(rma_op->origin_datatype, origin_dtp);
    }
    else
    {
        origin_dt_derived = 0;
    }

    MPID_Datatype_get_size_macro(rma_op->origin_datatype, origin_type_size);

    if (!origin_dt_derived)
    {
	/* basic datatype on origin */

        iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)rma_op->origin_addr;
        iov[1].MPID_IOV_LEN = rma_op->origin_count * origin_type_size;
        iovcnt = 2;

	MPIU_THREAD_CS_ENTER(CH3COMM,vc);
        mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, iovcnt, &request));
	MPIU_THREAD_CS_EXIT(CH3COMM,vc);
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
        }
    }
    else
    {
	/* derived datatype on origin */

        iovcnt = 1;

        request = MPID_Request_create();
        if (request == NULL) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomemreq");
        }

        MPIU_Object_set_ref(request, 2);
        request->kind = MPID_REQUEST_SEND;
	    
        request->dev.datatype_ptr = origin_dtp;
        /* this will cause the datatype to be freed when the request
           is freed. */ 

	request->dev.segment_ptr = MPID_Segment_alloc( );
        MPIU_ERR_CHKANDJUMP1(request->dev.segment_ptr == NULL, mpi_errno, MPI_ERR_OTHER, "**nomem", "**nomem %s", "MPID_Segment_alloc");

        MPID_Segment_init(rma_op->origin_addr, rma_op->origin_count,
                          rma_op->origin_datatype,
                          request->dev.segment_ptr, 0);
        request->dev.segment_first = 0;
        request->dev.segment_size = rma_op->origin_count * origin_type_size;
	    
        request->dev.OnFinal = 0;
        request->dev.OnDataAvail = 0;

        mpi_errno = vc->sendNoncontig_fn(vc, request, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
        {
            MPID_Datatype_release(request->dev.datatype_ptr);
            MPIU_Object_set_ref(request, 0);
            MPIDI_CH3_Request_destroy(request);
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|loadsendiov");
        }
        /* --END ERROR HANDLING-- */        
    }

    if (request != NULL) {
	if (!MPID_Request_is_complete(request))
        {
	    MPID_Progress_state progress_state;
	    
            MPID_Progress_start(&progress_state);
	    while (!MPID_Request_is_complete(request))
            {
                mpi_errno = MPID_Progress_wait(&progress_state);
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno != MPI_SUCCESS)
                {
		    MPID_Progress_end(&progress_state);
		    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
                }
                /* --END ERROR HANDLING-- */
            }
	    MPID_Progress_end(&progress_state);
        }
        
        mpi_errno = request->status.MPI_ERROR;
        if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
        }
                
        MPID_Request_release(request);
    }

    /* free MPIDI_RMA_ops_list */
    MPIU_Free(win_ptr->rma_ops_list_head->next);
    MPIU_Free(win_ptr->rma_ops_list_head);
    win_ptr->rma_ops_list_head = NULL;
    win_ptr->rma_ops_list_tail = NULL;

 fn_fail:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_LOCK_PUT_OR_ACC);
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_lock_get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int MPIDI_CH3I_Send_lock_get(MPID_Win *win_ptr)
{
    int mpi_errno=MPI_SUCCESS, lock_type, predefined;
    MPIDI_RMA_ops *rma_op;
    MPID_Request *rreq=NULL, *sreq=NULL;
    MPIDI_VC_t * vc;
    MPID_Comm *comm_ptr;
    MPID_Datatype *dtp;
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_lock_get_unlock_t *lock_get_unlock_pkt = 
	&upkt.lock_get_unlock;
#if defined(_OSU_MVAPICH_)
    int type_size;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
#endif /* defined(_OSU_MVAPICH_) */


    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GET);

    MPIDI_RMA_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GET);

    lock_type = win_ptr->rma_ops_list_head->lock_type;

    rma_op = win_ptr->rma_ops_list_head->next;

    /* create a request, store the origin buf, cnt, datatype in it,
       and pass a handle to it in the get packet. When the get
       response comes from the target, it will contain the request
       handle. */  
    rreq = MPID_Request_create();
    if (rreq == NULL) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomemreq");
    }

    MPIU_Object_set_ref(rreq, 2);

    rreq->dev.user_buf = rma_op->origin_addr;
    rreq->dev.user_count = rma_op->origin_count;
    rreq->dev.datatype = rma_op->origin_datatype;
    rreq->dev.target_win_handle = MPI_WIN_NULL;
    rreq->dev.source_win_handle = win_ptr->handle;
#if defined(_OSU_MVAPICH_)
    rreq->mrail.protocol = VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED;
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(rreq->dev.datatype, predefined);
    if (!predefined)
    {
        MPID_Datatype_get_ptr(rreq->dev.datatype, dtp);
        rreq->dev.datatype_ptr = dtp;
        /* this will cause the datatype to be freed when the
           request is freed. */  
    }

#if defined(_OSU_MVAPICH_)
    /* For OSU-MPI2, the post of receiving vectors is done when get pkt is sent
     * out */
    MPID_Datatype_get_size_macro(rreq->dev.datatype, type_size);
    rreq->dev.recv_data_sz = type_size * rreq->dev.user_count;

    mpi_errno = MPIDI_CH3U_Post_data_receive_found(rreq);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno != MPI_SUCCESS) {
	mpi_errno =
	    MPIR_Err_create_code(mpi_errno, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER,
                                 "**ch3|postrecv",
                                 "**ch3|postrecv %s",
                                 "MPIDI_CH3_PKT_GET_RESP");
    }
    /* --END ERROR HANDLING-- */
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_Pkt_init(lock_get_unlock_pkt, MPIDI_CH3_PKT_LOCK_GET_UNLOCK);
    lock_get_unlock_pkt->target_win_handle = 
        win_ptr->all_win_handles[rma_op->target_rank];
    lock_get_unlock_pkt->source_win_handle = win_ptr->handle;
    lock_get_unlock_pkt->lock_type = lock_type;
 
    lock_get_unlock_pkt->addr = 
        (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
        
    lock_get_unlock_pkt->count = rma_op->target_count;
    lock_get_unlock_pkt->datatype = rma_op->target_datatype;
    lock_get_unlock_pkt->request_handle = rreq->handle;

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc_set_active(comm_ptr, rma_op->target_rank, &vc);

    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
#if defined(_OSU_MVAPICH_)
    MPIDI_CH3_SET_RMA_ISSUED_NUM(vc, lock_get_unlock_pkt);
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(lock_get_unlock_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */
    mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, lock_get_unlock_pkt, 
				      sizeof(*lock_get_unlock_pkt), &sreq));
    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
    }

    /* release the request returned by iStartMsg */
    if (sreq != NULL)
    {
        MPID_Request_release(sreq);
    }

    /* now wait for the data to arrive */
    if (!MPID_Request_is_complete(rreq))
    {
	MPID_Progress_state progress_state;
	
	MPID_Progress_start(&progress_state);
	while (!MPID_Request_is_complete(rreq))
        {
            mpi_errno = MPID_Progress_wait(&progress_state);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS)
            {
		MPID_Progress_end(&progress_state);
		MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
            }
            /* --END ERROR HANDLING-- */
        }
	MPID_Progress_end(&progress_state);
    }
    
    mpi_errno = rreq->status.MPI_ERROR;
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**winRMAmessage");
    }
            
    /* if origin datatype was a derived datatype, it will get freed when the 
       rreq gets freed. */ 
    MPID_Request_release(rreq);

    /* free MPIDI_RMA_ops_list */
    MPIU_Free(win_ptr->rma_ops_list_head->next);
    MPIU_Free(win_ptr->rma_ops_list_head);
    win_ptr->rma_ops_list_head = NULL;
    win_ptr->rma_ops_list_tail = NULL;


 fn_fail:
    MPIDI_RMA_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GET);
    return mpi_errno;
}

/* ------------------------------------------------------------------------ */
/*
 * Utility routines
 */
/* ------------------------------------------------------------------------ */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Send_lock_granted_pkt
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_Send_lock_granted_pkt(MPIDI_VC_t *vc, MPI_Win source_win_handle)
{
    MPIDI_CH3_Pkt_t upkt;
    MPIDI_CH3_Pkt_lock_granted_t *lock_granted_pkt = &upkt.lock_granted;
    MPID_Request *req = NULL;
    int mpi_errno;
#if defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS) */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GRANTED_PKT);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GRANTED_PKT);

    /* send lock granted packet */
    MPIDI_Pkt_init(lock_granted_pkt, MPIDI_CH3_PKT_LOCK_GRANTED);
    lock_granted_pkt->source_win_handle = source_win_handle;
#if defined (_OSU_PSM_)
    MPID_Win *winptr;
    MPID_Comm *commptr;
    MPID_Win_get_ptr(source_win_handle, winptr);
    commptr = winptr->comm_ptr;
    lock_granted_pkt->source_rank = commptr->rank;
    lock_granted_pkt->mapped_srank = winptr->rank_mapping[commptr->rank];
    lock_granted_pkt->target_rank = vc->pg_rank;
    lock_granted_pkt->mapped_trank = vc->pg_rank;
#endif

    MPIU_THREAD_CS_ENTER(CH3COMM,vc);
#if defined(_OSU_MVAPICH_)
    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(lock_granted_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */
    mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsg(vc, lock_granted_pkt,
				      sizeof(*lock_granted_pkt), &req));
    MPIU_THREAD_CS_EXIT(CH3COMM,vc);
    if (mpi_errno) {
	MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
    }

    if (req != NULL)
    {
        MPID_Request_release(req);
    }

 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SEND_LOCK_GRANTED_PKT);

    return mpi_errno;
}

/* ------------------------------------------------------------------------ */
/* 
 * The following routines are the packet handlers for the packet types 
 * used above in the implementation of the RMA operations in terms
 * of messages.
 */
/* ------------------------------------------------------------------------ */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_Put
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_Put( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, 
			      MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_put_t * put_pkt = &pkt->put;
    MPID_Request *req = NULL;
    int predefined;
    int type_size;
    int complete = 0;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
#if defined(_OSU_PSM_)
	MPIDI_msg_sz_t orig_len = *buflen;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_PUT);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_PUT);

    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received put pkt");

    if (put_pkt->count == 0)
    {
	MPID_Win *win_ptr;
	
	/* it's a 0-byte message sent just to decrement the
	   completion counter. This happens only in
	   post/start/complete/wait sync model; therefore, no need
	   to check lock queue. */
#if defined(_OSU_MVAPICH_)
        MPID_Win_get_ptr(put_pkt->target_win_handle, win_ptr);
        --win_ptr->outstanding_rma;
#endif /* defined(_OSU_MVAPICH_) */
	if (put_pkt->target_win_handle != MPI_WIN_NULL) {
#if defined(_OSU_MVAPICH_)
            win_ptr->outstanding_rma += put_pkt->rma_issued;
#else /* defined(_OSU_MVAPICH_) */
	    MPID_Win_get_ptr(put_pkt->target_win_handle, win_ptr);
#endif /* defined(_OSU_MVAPICH_) */
	    /* FIXME: MT: this has to be done atomically */
	    win_ptr->my_counter -= 1;
	}
        *buflen = sizeof(MPIDI_CH3_Pkt_t);
	MPIDI_CH3_Progress_signal_completion();	
	*rreqp = NULL;
        goto fn_exit;
    }
        
    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);

    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);
                
    req->dev.user_buf = put_pkt->addr;
    req->dev.user_count = put_pkt->count;
    req->dev.target_win_handle = put_pkt->target_win_handle;
    req->dev.source_win_handle = put_pkt->source_win_handle;
	
#if defined(_OSU_MVAPICH_)
    if (put_pkt->source_win_handle != MPI_WIN_NULL)
    {
        MPID_Win *win_ptr;
        MPID_Win_get_ptr(put_pkt->target_win_handle, win_ptr);
        win_ptr->outstanding_rma += put_pkt->rma_issued;
    }
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(put_pkt->datatype, predefined);
    if (predefined)
    {
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PUT_RESP);
        req->dev.datatype = put_pkt->datatype;

        MPID_Datatype_get_size_macro(put_pkt->datatype,
                                     type_size);
        req->dev.recv_data_sz = (MPIDI_msg_sz_t) type_size * put_pkt->count;

        if (req->dev.recv_data_sz == 0) {
            MPIDI_CH3U_Request_complete( req );
            *buflen = sizeof(MPIDI_CH3_Pkt_t);
            *rreqp = NULL;
            goto fn_exit;
        }

#if defined (_OSU_PSM_)
        MPIU_Assert((*rreqp));
        if((*rreqp)->psm_flags & PSM_RNDVPUT_COMPLETED) {
            complete = TRUE;
            goto rndv_complete;
        }
#endif
#if defined(_OSU_MVAPICH_)
        switch(pkt->type)
        {
        case MPIDI_CH3_PKT_PUT_RNDV:
            *rreqp = NULL;
            MPIDI_CH3_Pkt_put_rndv_t *rts_pkt = (void *) put_pkt;
            MPID_Request *cts_req = NULL;
            MPIDI_CH3_Pkt_t upkt;
            MPIDI_CH3_Pkt_rndv_clr_to_send_t *cts_pkt = &upkt.rndv_clr_to_send;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
            MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
            req->dev.sender_req_id = rts_pkt->sender_req_id;
            req->dev.recv_data_sz = rts_pkt->data_sz;

            MPIDI_CH3_RNDV_SET_REQ_INFO(req, rts_pkt);
            MPIDI_CH3U_Post_data_receive_found(req);
            MPIDI_Pkt_init(cts_pkt, MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND);
            MPIDI_VC_FAI_send_seqnum(vc, seqnum);
            MPIDI_Pkt_set_seqnum(cts_pkt, seqnum);

            cts_pkt->sender_req_id = rts_pkt->sender_req_id;
            cts_pkt->receiver_req_id = req->handle;
 
            mpi_errno = MPIDI_CH3_Prepare_rndv_cts(vc, cts_pkt, req);
            if (mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rndv");
            }

            mpi_errno = MPIDI_CH3_iStartMsg(vc, cts_pkt, sizeof(*cts_pkt),&cts_req);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|ctspkt");
            }
            /* --END ERROR HANDLING-- */
            if (cts_req != NULL) {
                MPID_Request_release(cts_req);
            }

            break;

        default:
            *rreqp = req;
#endif /* defined(_OSU_MVAPICH_) */

#if defined (_OSU_PSM_)
        /* if its a large PUT, we will receive it out-of-band directly to the
         * destination buffer. */ 
        if(put_pkt->rndv_mode) {
            *rreqp = req;
            goto fn_exit;
        }
#endif        

        mpi_errno = MPIDI_CH3U_Receive_data_found(req, data_buf, &data_len,
                                                  &complete);
        MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                             "**ch3|postrecv %s", "MPIDI_CH3_PKT_PUT");
#if defined(_OSU_MVAPICH_)
            break;
        }
#endif /* defined(_OSU_MVAPICH_) */
        /* FIXME:  Only change the handling of completion if
           post_data_receive reset the handler.  There should
           be a cleaner way to do this */
        if (!req->dev.OnDataAvail) {
            req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_PutAccumRespComplete;
        }
        
        /* return the number of bytes processed in this function */
        *buflen = sizeof(MPIDI_CH3_Pkt_t) + data_len;
#if defined (_OSU_PSM_)
rndv_complete:
#endif
        if (complete) 
        {
            mpi_errno = MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req, &complete);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
            if (complete)
            {
                *rreqp = NULL;
                goto fn_exit;
            }
        }
    }
    else
    {
        /* derived datatype */
        MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PUT_RESP_DERIVED_DT);
        req->dev.datatype = MPI_DATATYPE_NULL;
	    
        req->dev.dtype_info = (MPIDI_RMA_dtype_info *) 
            MPIU_Malloc(sizeof(MPIDI_RMA_dtype_info));
        if (! req->dev.dtype_info) {
            MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_RMA_dtype_info");
        }

        req->dev.dataloop = MPIU_Malloc(put_pkt->dataloop_size);
        if (! req->dev.dataloop) {
            MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
				 put_pkt->dataloop_size);
        }

        /* if we received all of the dtype_info and dataloop, copy it
           now and call the handler, otherwise set the iov and let the
           channel copy it */
        if (data_len >= sizeof(MPIDI_RMA_dtype_info) + put_pkt->dataloop_size)
        {
            /* copy all of dtype_info and dataloop */
            MPIU_Memcpy(req->dev.dtype_info, data_buf, sizeof(MPIDI_RMA_dtype_info));
            MPIU_Memcpy(req->dev.dataloop, data_buf + sizeof(MPIDI_RMA_dtype_info), put_pkt->dataloop_size);

            *buflen = sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_RMA_dtype_info) + put_pkt->dataloop_size;
          
            /* All dtype data has been received, call req handler */
            mpi_errno = MPIDI_CH3_ReqHandler_PutRespDerivedDTComplete(vc, req, &complete);
            MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                 "**ch3|postrecv %s", "MPIDI_CH3_PKT_PUT"); 
#if defined (_OSU_PSM_)
        /* notes: for PSM channel the packet header is sent in single send
         * followed by data. If data is available then we can copy it to
         * destination at this time and call put_completion. If data is not
         * available we will post a RNDV recv (on  a tmp_buf if non-contig) and
         * then complete the PUT when RNDV completes */
            if(put_pkt->rndv_mode) {
                if((*rreqp)->psm_flags & PSM_RNDVPUT_COMPLETED) {
                    MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req,
                            &complete);
                }
                PSM_PRINT("Rendezvous put with DERIVED target\n");
            } else {
                PSM_PRINT("Eager put with DERIVED target\n");
                assert((req->dev.recv_data_sz) == (data_len -
                            (sizeof(MPIDI_RMA_dtype_info) +
                             put_pkt->dataloop_size)));
                PSM_PRINT("We have %d bytes of data\n", req->dev.recv_data_sz);
                data_buf += sizeof(MPIDI_RMA_dtype_info) + put_pkt->dataloop_size;
                assert(data_buf <= ((char *) pkt+orig_len));
                mpi_errno = psm_dt_1scop(req, data_buf, (data_len -
                            (sizeof(MPIDI_RMA_dtype_info) +
                             put_pkt->dataloop_size)));
                MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req, &complete);
            }

#endif /* _OSU_PSM_ */
            if (complete)
            {
                *rreqp = NULL;
                goto fn_exit;
            }
        }
        else
        {
#if defined (_OSU_PSM_)
            /* will we ever have a packet header (pkt+datatype+dataloop) greater
             * than 16k */
            assert(0);
#endif /* _OSU_PSM_ */
            req->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)((char *)req->dev.dtype_info);
            req->dev.iov[0].MPID_IOV_LEN = sizeof(MPIDI_RMA_dtype_info);
            req->dev.iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)req->dev.dataloop;
            req->dev.iov[1].MPID_IOV_LEN = put_pkt->dataloop_size;
            req->dev.iov_count = 2;

            *buflen = sizeof(MPIDI_CH3_Pkt_t);
            
            req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_PutRespDerivedDTComplete;
#if defined(_OSU_MVAPICH_)
            if (MPIDI_CH3_PKT_PUT_RNDV == pkt->type) {
                req->mrail.protocol = VAPI_PROTOCOL_RPUT;
                MPIDI_CH3_RNDV_SET_REQ_INFO(req,((MPIDI_CH3_Pkt_put_rndv_t * )(put_pkt)));
                req->dev.sender_req_id = ((MPIDI_CH3_Pkt_put_rndv_t *)pkt)->sender_req_id;
                req->dev.recv_data_sz = ((MPIDI_CH3_Pkt_put_rndv_t *)pkt)->data_sz;
            } else {
                req->mrail.protocol = VAPI_PROTOCOL_EAGER;
        }
            DEBUG_PRINT("put_rndv_t size %d, buf0 size %d, buf1 size %d, "
                "sender_req_id %p, data_sz %d\n",
                sizeof(MPIDI_CH3_Pkt_put_rndv_t), req->dev.iov[0].MPID_IOV_LEN,
                req->dev.iov[1].MPID_IOV_LEN, req->dev.sender_req_id,
                req->dev.recv_data_sz);
#endif /* defined(_OSU_MVAPICH_) */
    }
	
#if defined(_OSU_MVAPICH_)
    *rreqp = req;
#endif /* defined(_OSU_MVAPICH_) */
    }
#if !defined(_OSU_MVAPICH_)
    *rreqp = req;
#endif /* !defined(_OSU_MVAPICH_) */
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SET1(mpi_errno,MPI_ERR_OTHER,"**ch3|postrecv",
                      "**ch3|postrecv %s", "MPIDI_CH3_PKT_PUT");
    }
    

 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_PUT);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_Get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_Get( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, 
			      MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_get_t * get_pkt = &pkt->get;
    MPID_Request *req = NULL;
    MPID_IOV iov[MPID_IOV_LIMIT];
    int predefined;
    int complete;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
    int type_size;
#if defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS) */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_GET);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_GET);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received get pkt");
    
    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);
    
    req = MPID_Request_create();
    req->dev.target_win_handle = get_pkt->target_win_handle;
    req->dev.source_win_handle = get_pkt->source_win_handle;
    
#if defined(_OSU_MVAPICH_)
    if (req->dev.source_win_handle != MPI_WIN_NULL) {
        MPID_Win *win_ptr;
        MPID_Win_get_ptr(req->dev.target_win_handle, win_ptr);
        DEBUG_PRINT("get pkt, win handle %d, WINNULL %d, outstanding %d, get rma %d\n",
                req->dev.source_win_handle, MPI_WIN_NULL, win_ptr->outstanding_rma,
                get_pkt->rma_issued);

        win_ptr->outstanding_rma += get_pkt->rma_issued;
    }
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(get_pkt->datatype, predefined);
    if (predefined)
    {
	/* basic datatype. send the data. */
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt = &upkt.get_resp;
	
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_GET_RESP); 
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GetSendRespComplete;
	req->dev.OnFinal     = MPIDI_CH3_ReqHandler_GetSendRespComplete;
	req->kind = MPID_REQUEST_SEND;
	
	MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
	get_resp_pkt->request_handle = get_pkt->request_handle;
	
	iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) get_resp_pkt;
	iov[0].MPID_IOV_LEN = sizeof(*get_resp_pkt);
	
	iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)get_pkt->addr;
	MPID_Datatype_get_size_macro(get_pkt->datatype, type_size);
	iov[1].MPID_IOV_LEN = (MPIDI_msg_sz_t) get_pkt->count * type_size;

        MPIU_THREAD_CS_ENTER(CH3COMM,vc);
#if defined (_OSU_PSM_)
    get_resp_pkt->target_rank = get_pkt->source_rank;
    get_resp_pkt->source_rank = get_pkt->target_rank;
    get_resp_pkt->source_win_handle = get_pkt->source_win_handle;
    get_resp_pkt->target_win_handle = get_pkt->target_win_handle;
    get_resp_pkt->rndv_mode = get_pkt->rndv_mode;
    get_resp_pkt->rndv_tag = get_pkt->rndv_tag;
    get_resp_pkt->rndv_len = get_pkt->rndv_len;
    get_resp_pkt->mapped_srank = get_pkt->mapped_trank;
    get_resp_pkt->mapped_trank = get_pkt->mapped_srank;
#endif /* _OSU_PSM_ */
        
#if defined(_OSU_MVAPICH_)
        MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(get_resp_pkt, seqnum);

        if (MPIDI_CH3_PKT_GET == pkt->type ) {
            get_resp_pkt->protocol = VAPI_PROTOCOL_EAGER;
#endif /* defined(_OSU_MVAPICH_) */	

#if defined (_OSU_PSM_)
    mpi_errno = MPIU_CALL(MPIDI_CH3,iStartMsgv(vc, iov, 2, &req));
#else /* _OSU_PSM_ */
	mpi_errno = MPIU_CALL(MPIDI_CH3,iSendv(vc, req, iov, 2));
#endif
        MPIU_THREAD_CS_EXIT(CH3COMM,vc);
#if defined(_OSU_MVAPICH_)      
            req->mrail.protocol = VAPI_PROTOCOL_EAGER;
#endif /* defined(_OSU_MVAPICH_) */
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    MPIU_Object_set_ref(req, 0);
	    MPIDI_CH3_Request_destroy(req);
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
	}
	/* --END ERROR HANDLING-- */
#if defined(_OSU_MVAPICH_)
        } else {
            req->dev.iov[0].MPID_IOV_BUF = iov[1].MPID_IOV_BUF;
            req->dev.iov[0].MPID_IOV_LEN = iov[1].MPID_IOV_LEN;
            req->dev.iov_count = 1;

            MPIDI_CH3I_MRAIL_SET_REQ_REMOTE_RNDV(req,(MPIDI_CH3_Pkt_get_rndv_t *)pkt);
            req->dev.recv_data_sz = iov[1].MPID_IOV_LEN;
            DEBUG_PRINT("Process get rndv, buf %p, len %d\n",
                   req->dev.iov[0].MPID_IOV_BUF, req->dev.iov[0].MPID_IOV_LEN);
            MPIDI_CH3_Get_rndv_push(vc, get_resp_pkt, req);
        }
#endif /* defined(_OSU_MVAPICH_) */
	
        *buflen = sizeof(MPIDI_CH3_Pkt_t);
	*rreqp = NULL;
    }
    else
    {
	/* derived datatype. first get the dtype_info and dataloop. */
	
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_GET_RESP_DERIVED_DT);
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GetRespDerivedDTComplete;
	req->dev.OnFinal     = 0;
	req->dev.user_buf = get_pkt->addr;
	req->dev.user_count = get_pkt->count;
	req->dev.datatype = MPI_DATATYPE_NULL;
	req->dev.request_handle = get_pkt->request_handle;
	
	req->dev.dtype_info = (MPIDI_RMA_dtype_info *) 
	    MPIU_Malloc(sizeof(MPIDI_RMA_dtype_info));
	if (! req->dev.dtype_info) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_RMA_dtype_info");
	}
	
	req->dev.dataloop = MPIU_Malloc(get_pkt->dataloop_size);
	if (! req->dev.dataloop) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
				 get_pkt->dataloop_size);
	}
	
        /* if we received all of the dtype_info and dataloop, copy it
           now and call the handler, otherwise set the iov and let the
           channel copy it */
        if (data_len >= sizeof(MPIDI_RMA_dtype_info) + get_pkt->dataloop_size)
        {
            /* copy all of dtype_info and dataloop */
            MPIU_Memcpy(req->dev.dtype_info, data_buf, sizeof(MPIDI_RMA_dtype_info));
            MPIU_Memcpy(req->dev.dataloop, data_buf + sizeof(MPIDI_RMA_dtype_info), get_pkt->dataloop_size);

            *buflen = sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_RMA_dtype_info) + get_pkt->dataloop_size;
          

#if defined (_OSU_PSM_)
            vc->ch.pkt_active = get_pkt;
#endif
            /* All dtype data has been received, call req handler */
            mpi_errno = MPIDI_CH3_ReqHandler_GetRespDerivedDTComplete(vc, req, &complete);
            MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                 "**ch3|postrecv %s", "MPIDI_CH3_PKT_GET"); 
            if (complete)
                *rreqp = NULL;
        }
        else
        {
#if defined (_OSU_PSM_)
            assert(0);
#endif
            req->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)req->dev.dtype_info;
            req->dev.iov[0].MPID_IOV_LEN = sizeof(MPIDI_RMA_dtype_info);
            req->dev.iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)req->dev.dataloop;
            req->dev.iov[1].MPID_IOV_LEN = get_pkt->dataloop_size;
            req->dev.iov_count = 2;
	
#if defined(_OSU_MVAPICH_)
            if (MPIDI_CH3_PKT_GET_RNDV == pkt->type) {
                MPIDI_CH3I_MRAIL_SET_REQ_REMOTE_RNDV(req, (MPIDI_CH3_Pkt_get_rndv_t *)pkt);
            } else {
                req->mrail.protocol = VAPI_PROTOCOL_EAGER;
            }
#endif /* defined(_OSU_MVAPICH_) */
            *buflen = sizeof(MPIDI_CH3_Pkt_t);
            *rreqp = req;
        }
        
    }
 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_GET);
    return mpi_errno;

}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_Accumulate
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_Accumulate( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
				     MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_accum_t * accum_pkt = &pkt->accum;
    MPID_Request *req = NULL;
    MPI_Aint true_lb, true_extent, extent;
    void *tmp_buf = NULL;
    int predefined;
    int complete = 0;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
    int type_size;
#if defined (_OSU_PSM_)
	MPIDI_msg_sz_t orig_len = *buflen;
    MPID_Request *savereq = (*rreqp);
#endif /* _OSU_PSM_ */

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received accumulate pkt");
#if defined (_OSU_PSM_)
    if((*rreqp)->psm_flags & PSM_RNDVPUT_COMPLETED) {
        complete = TRUE;
        goto do_accumulate;
    }
#endif /* _OSU_PSM_ */
            

    MPIU_INSTR_DURATION_START(rmapkt_acc);
    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);
    
    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);
    *rreqp = req;
    
    req->dev.user_count = accum_pkt->count;
    req->dev.op = accum_pkt->op;
    req->dev.real_user_buf = accum_pkt->addr;
    req->dev.target_win_handle = accum_pkt->target_win_handle;
    req->dev.source_win_handle = accum_pkt->source_win_handle;

#if defined(_OSU_MVAPICH_)
    if (req->dev.source_win_handle != MPI_WIN_NULL) {
        MPID_Win *win_ptr = NULL;
        MPID_Win_get_ptr(req->dev.target_win_handle, win_ptr);
        win_ptr->outstanding_rma += accum_pkt->rma_issued;
    }
#endif /* defined(_OSU_MVAPICH_) */

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(accum_pkt->datatype, predefined);
    if (predefined)
    {
	MPIU_INSTR_DURATION_START(rmapkt_acc_predef);
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_ACCUM_RESP);
	req->dev.datatype = accum_pkt->datatype;

	MPIR_Type_get_true_extent_impl(accum_pkt->datatype, &true_lb, &true_extent);
	MPID_Datatype_get_extent_macro(accum_pkt->datatype, extent); 
	tmp_buf = MPIU_Malloc(accum_pkt->count * 
			      (MPIR_MAX(extent,true_extent)));
	if (!tmp_buf) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
			 accum_pkt->count * MPIR_MAX(extent,true_extent));
	}
	
	/* adjust for potential negative lower bound in datatype */
	tmp_buf = (void *)((char*)tmp_buf - true_lb);
	
	req->dev.user_buf = tmp_buf;
	
	MPID_Datatype_get_size_macro(accum_pkt->datatype, type_size);
	req->dev.recv_data_sz = type_size * accum_pkt->count;
              
#if defined (_OSU_PSM_)
    if(savereq->psm_flags & PSM_RNDV_ACCUM_REQ) {
        goto fn_exit;
    }
#endif /* _OSU_PSM_ */    
	if (req->dev.recv_data_sz == 0) {
	    MPIDI_CH3U_Request_complete(req);
            *buflen = sizeof(MPIDI_CH3_Pkt_t);
	    *rreqp = NULL;
	}
	else {
#if defined(_OSU_MVAPICH_)
            if (pkt->type == MPIDI_CH3_PKT_ACCUMULATE_RNDV)
            {
                MPIDI_CH3_Pkt_accum_rndv_t * rts_pkt = (void *)accum_pkt;
                MPID_Request *cts_req = NULL;
                MPIDI_CH3_Pkt_t upkt;
                MPIDI_CH3_Pkt_rndv_clr_to_send_t *cts_pkt = &upkt.rndv_clr_to_send;
#if defined(MPID_USE_SEQUENCE_NUMBERS)
                MPID_Seqnum_t seqnum;
#endif /* defined(MPID_USE_SEQUENCE_NUMBERS) */
                req->dev.sender_req_id = rts_pkt->sender_req_id;

                DEBUG_PRINT("sending bytes in pkt is %d\n", rts_pkt->data_sz);
                MPIDI_CH3_RNDV_SET_REQ_INFO(req, rts_pkt);

                mpi_errno = MPIDI_CH3U_Post_data_receive_found(req);
                MPIU_ERR_CHKANDJUMP1(
                    mpi_errno,
                    mpi_errno,
                    MPI_ERR_OTHER,
                    "**ch3|postrecv",
                    "**ch3|postrecv %s",
                    "MPIDI_CH3_PKT_ACCUMULATE");

                MPIDI_Pkt_init(cts_pkt, MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND);
                MPIDI_VC_FAI_send_seqnum(vc, seqnum);
                MPIDI_Pkt_set_seqnum(cts_pkt, seqnum);

                cts_pkt->sender_req_id = rts_pkt->sender_req_id;
                cts_pkt->receiver_req_id = req->handle;

                mpi_errno = MPIDI_CH3_Prepare_rndv_cts(vc, cts_pkt, req);

                if (mpi_errno != MPI_SUCCESS)
                {
                    MPIU_ERR_SETANDJUMP(mpi_errno, MPIR_ERR_FATAL,"**ch3|rndv");
                }

                mpi_errno = MPIDI_CH3_iStartMsg(vc, cts_pkt, sizeof(*cts_pkt), &cts_req);

                if (mpi_errno != MPI_SUCCESS)
                {
                    MPIU_ERR_SETANDJUMP(mpi_errno,MPIR_ERR_FATAL,"**ch3|ctspkt");
                }

                DEBUG_PRINT("Successfully sending cts_pkt\n");

                if (cts_req != NULL)
                {
                    MPID_Request_release(cts_req);
                }

                *rreqp = NULL;
            }
            else
            {
                mpi_errno = MPIDI_CH3U_Post_data_receive_found(req);
                MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                 "**ch3|postrecv %s", "MPIDI_CH3_PKT_ACCUMULATE");
            }
#else /* defined(_OSU_MVAPICH_) */
            mpi_errno = MPIDI_CH3U_Receive_data_found(req, data_buf, &data_len,
                                                      &complete);
            MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                 "**ch3|postrecv %s", "MPIDI_CH3_PKT_ACCUMULATE");
#endif /* defined(_OSU_MVAPICH_) */
	    /* FIXME:  Only change the handling of completion if
	       post_data_receive reset the handler.  There should
	       be a cleaner way to do this */
	    if (!req->dev.OnDataAvail) {
		req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_PutAccumRespComplete;
	    }
            /* return the number of bytes processed in this function */
            *buflen = data_len + sizeof(MPIDI_CH3_Pkt_t);
#if !defined(_OSU_MVAPICH_)
            if (complete) 
            {
#if defined(_OSU_PSM_)
do_accumulate:
#endif
                mpi_errno = MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req, &complete);
                if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                if (complete)
                {
                    *rreqp = NULL;
		    MPIU_INSTR_DURATION_END(rmapkt_acc_predef);
                    goto fn_exit;
                }
            }
	    MPIU_INSTR_DURATION_END(rmapkt_acc_predef);
#endif /* !defined(_OSU_MVAPICH_) */
	}
    }
    else
    {
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_ACCUM_RESP_DERIVED_DT);
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_AccumRespDerivedDTComplete;
	req->dev.datatype = MPI_DATATYPE_NULL;
                
	req->dev.dtype_info = (MPIDI_RMA_dtype_info *) 
	    MPIU_Malloc(sizeof(MPIDI_RMA_dtype_info));
	if (! req->dev.dtype_info) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_RMA_dtype_info");
	}
	
	req->dev.dataloop = MPIU_Malloc(accum_pkt->dataloop_size);
	if (! req->dev.dataloop) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
				 accum_pkt->dataloop_size);
	}
	
        if (data_len >= sizeof(MPIDI_RMA_dtype_info) + accum_pkt->dataloop_size)
        {
            /* copy all of dtype_info and dataloop */
            MPIU_Memcpy(req->dev.dtype_info, data_buf, sizeof(MPIDI_RMA_dtype_info));
            MPIU_Memcpy(req->dev.dataloop, data_buf + sizeof(MPIDI_RMA_dtype_info), accum_pkt->dataloop_size);

            *buflen = sizeof(MPIDI_CH3_Pkt_t) + sizeof(MPIDI_RMA_dtype_info) + accum_pkt->dataloop_size;
          
            /* All dtype data has been received, call req handler */
            mpi_errno = MPIDI_CH3_ReqHandler_AccumRespDerivedDTComplete(vc, req, &complete);
            MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv",
                                 "**ch3|postrecv %s", "MPIDI_CH3_ACCUMULATE"); 

#if defined (_OSU_PSM_)
        /* notes: for PSM channel the packet header is sent in single send
         * followed by data. If data is available then we can copy it to
         * destination at this time and call put_completion. If data is not
         * available we will post a RNDV recv (on  a tmp_buf if non-contig) and
         * then complete the PUT when RNDV completes */
            if(accum_pkt->rndv_mode) {
                if((*rreqp)->psm_flags & PSM_RNDVPUT_COMPLETED) {
                    MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req,
                            &complete);
                }
                PSM_PRINT("Rendezvous accum with DERIVED target\n");
            } else {
                PSM_PRINT("Eager accum with DERIVED target\n");
                assert((req->dev.recv_data_sz) == (data_len -
                            (sizeof(MPIDI_RMA_dtype_info) +
                             accum_pkt->dataloop_size)));
                PSM_PRINT("We have %d bytes of data\n", req->dev.recv_data_sz);
                data_buf += sizeof(MPIDI_RMA_dtype_info) + accum_pkt->dataloop_size;
                assert(data_buf <= ((char *) pkt+orig_len));
                mpi_errno = psm_dt_1scop(req, data_buf, (data_len -
                            (sizeof(MPIDI_RMA_dtype_info) +
                             accum_pkt->dataloop_size)));
                MPIDI_CH3_ReqHandler_PutAccumRespComplete(vc, req, &complete);
            }

#endif /* _OSU_PSM_ */

            if (complete)
            {
                *rreqp = NULL;
                goto fn_exit;
            }
        }
        else
        {
#if defined (_OSU_PSM_)
            /* will we ever have a packet header (pkt+datatype+dataloop) greater
             * than 16k */
            assert(0);
#endif /* _OSU_PSM_ */

            req->dev.iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)req->dev.dtype_info;
            req->dev.iov[0].MPID_IOV_LEN = sizeof(MPIDI_RMA_dtype_info);
            req->dev.iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)req->dev.dataloop;
            req->dev.iov[1].MPID_IOV_LEN = accum_pkt->dataloop_size;
            req->dev.iov_count = 2;
            *buflen = sizeof(MPIDI_CH3_Pkt_t);
#if defined(_OSU_MVAPICH_)
            if (MPIDI_CH3_PKT_ACCUMULATE_RNDV == pkt->type) {
                req->mrail.protocol = VAPI_PROTOCOL_RPUT;
                MPIDI_CH3_RNDV_SET_REQ_INFO(req,
                    ((MPIDI_CH3_Pkt_accum_rndv_t * )(accum_pkt)));
                req->dev.sender_req_id =
                    ((MPIDI_CH3_Pkt_accum_rndv_t *)pkt)->sender_req_id;
                req->dev.recv_data_sz = ((MPIDI_CH3_Pkt_accum_rndv_t *)pkt)->data_sz;
            } else {
                req->mrail.protocol = VAPI_PROTOCOL_EAGER;
            }
            DEBUG_PRINT("accum_rndv_t size %d, ender_req_id %p\n",
                sizeof(MPIDI_CH3_Pkt_accum_rndv_t),
                req->dev.sender_req_id);
#endif /* defined(_OSU_MVAPICH_) */
        }
        
    }

    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**ch3|postrecv",
			     "**ch3|postrecv %s", "MPIDI_CH3_PKT_ACCUMULATE");
    }

 fn_exit:
    MPIU_INSTR_DURATION_END(rmapkt_acc);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE);
    return mpi_errno;
 fn_fail:
    goto fn_exit;

}

/* Special accumulate for short data items entirely within the packet */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_Accumulate_Immed
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_Accumulate_Immed( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
					   MPIDI_msg_sz_t *buflen, 
					   MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_accum_immed_t * accum_pkt = &pkt->accum_immed;
    MPID_Win *win_ptr;
    MPI_Aint extent;
    int mpi_errno = MPI_SUCCESS;
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    int l_rank = -1;
#endif

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE_IMMED);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE_IMMED);

    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received accumulate immedidate pkt");

    MPIU_INSTR_DURATION_START(rmapkt_acc_immed);

#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    MPID_Win_get_ptr(accum_pkt->target_win_handle, win_ptr);
#if defined (_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback)
#endif
    {
        l_rank = win_ptr->shm_g2l_rank[win_ptr->my_id];
    }
#endif

    /* return the number of bytes processed in this function */
    /* data_len == 0 (all within packet) */
    *buflen = sizeof(MPIDI_CH3_Pkt_t);
    *rreqp  = NULL;

    MPID_Datatype_get_extent_macro(accum_pkt->datatype, extent); 
    
    /* size == 0 should never happen */
    if (accum_pkt->count == 0 || extent == 0) {
    }
    else {
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    if (!win_ptr->shm_fallback) {
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_rank);
        if (mpi_errno != MPI_SUCCESS) {
              MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "mutex lock error");
        }
    }
#endif
	/* Data is already present */
	if (accum_pkt->op == MPI_REPLACE) {
	    /* no datatypes required */
	    int len = accum_pkt->count * extent;
	    /* FIXME: use immediate copy because this is short */
	    MPIUI_Memcpy( accum_pkt->addr, accum_pkt->data, len );
	}
	else {
	    if (HANDLE_GET_KIND(accum_pkt->op) == HANDLE_KIND_BUILTIN) {
		MPI_User_function *uop;
		/* get the function by indexing into the op table */
		uop = MPIR_Op_table[((accum_pkt->op)&0xf) - 1];
		(*uop)(accum_pkt->data, accum_pkt->addr,
		       &(accum_pkt->count), &(accum_pkt->datatype));
	    }
	    else {
		MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OP, "**opnotpredefined",
				     "**opnotpredefined %d", accum_pkt->op );
	    }
	}
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
    if (!win_ptr->shm_fallback) {
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_rank);
        if (mpi_errno != MPI_SUCCESS) {
              MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "mutex unlock error");
        }
    }
#endif

	/* There are additional steps to take if this is a passive 
	   target RMA or the last operation from the source */
	
	/* Here is the code executed in PutAccumRespComplete after the
	   accumulation operation */
	MPID_Win_get_ptr(accum_pkt->target_win_handle, win_ptr);
#if defined(_OSU_MVAPICH_)
    --win_ptr->outstanding_rma;

    /* if passive target RMA, increment pt_rma_puts_accs counter */
#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
    if ((!win_ptr->limic_fallback || !win_ptr->shm_fallback)
        && vc->smp.local_nodes != -1)
#else
    if (!win_ptr->shm_fallback && vc->smp.local_nodes != -1)
#endif
    {
        if (*((volatile int *) &win_ptr->shm_lock[l_rank]) != MPID_LOCK_NONE) {
            win_ptr->my_pt_rma_puts_accs++;
        }
    }
#endif /* !defined(DAPL_DEFAULT_PROVIDER) */
#endif /* defined(_OSU_MVAPICH_) */
	
	/* if passive target RMA, increment counter */
	if (win_ptr->current_lock_type != MPID_LOCK_NONE)
	    win_ptr->my_pt_rma_puts_accs++;
	
	if (accum_pkt->source_win_handle != MPI_WIN_NULL) {
	    /* Last RMA operation from source. If active
	       target RMA, decrement window counter. If
	       passive target RMA, release lock on window and
	       grant next lock in the lock queue if there is
	       any. If it's a shared lock or a lock-put-unlock
	       type of optimization, we also need to send an
	       ack to the source. */ 
#if defined(_OSU_MVAPICH_)
        win_ptr->outstanding_rma += accum_pkt->rma_issued;

#if !defined(DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
        if ((!win_ptr->limic_fallback || !win_ptr->shm_fallback)
            && vc->smp.local_nodes != -1)
#else
        if (!win_ptr->shm_fallback && vc->smp.local_nodes != -1)
#endif
        {
            if (*((volatile int *) &win_ptr->shm_lock[l_rank]) != MPID_LOCK_NONE) {
                if(*((volatile int *) &win_ptr->shm_lock[l_rank]) == MPI_LOCK_SHARED) {
                    mpi_errno = MPIDI_CH3I_Send_pt_rma_done_pkt(vc,
                                accum_pkt->source_win_handle);
                    if (mpi_errno) {
                        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**rmasync");
                    }
                }
                MPIDI_CH3I_SHM_win_unlock(win_ptr->my_id, win_ptr);
                MPIDI_CH3_Progress_signal_completion();
                goto fn_exit;
            }
        }
#endif /* !defined(DAPL_DEFAULT_PROVIDER) */
#endif /* defined(_OSU_MVAPICH_) */ 

	    if (win_ptr->current_lock_type == MPID_LOCK_NONE) {
		/* FIXME: MT: this has to be done atomically */
		win_ptr->my_counter -= 1;
		MPIDI_CH3_Progress_signal_completion();
	    }
	    else {
		if ((win_ptr->current_lock_type == MPI_LOCK_SHARED) ||
		    (/*rreq->dev.single_op_opt*/ 0 == 1)) {
		    mpi_errno = MPIDI_CH3I_Send_pt_rma_done_pkt(vc, 
					accum_pkt->source_win_handle);
		    if (mpi_errno) {
			    MPIU_ERR_POP(mpi_errno);
		    }
		}
		mpi_errno = MPIDI_CH3I_Release_lock(win_ptr);
		/* Without the following signal_completion call, we 
		   sometimes hang */
		MPIDI_CH3_Progress_signal_completion();
	    }
	}

	goto fn_exit;
    }

 fn_exit:
    MPIU_INSTR_DURATION_END(rmapkt_acc_immed);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_ACCUMULATE_IMMED);
    return mpi_errno;
 fn_fail:
    goto fn_exit;

}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_Lock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_Lock( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, 
			       MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_lock_t * lock_pkt = &pkt->lock;
    MPID_Win *win_ptr = NULL;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCK);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCK);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received lock pkt");
    
    *buflen = sizeof(MPIDI_CH3_Pkt_t);

    MPID_Win_get_ptr(lock_pkt->target_win_handle, win_ptr);
    
    if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, 
					lock_pkt->lock_type) == 1)
    {
	/* send lock granted packet. */
	mpi_errno = MPIDI_CH3I_Send_lock_granted_pkt(vc,
					     lock_pkt->source_win_handle);
    }

    else {
	/* queue the lock information */
	MPIDI_Win_lock_queue *curr_ptr, *prev_ptr, *new_ptr;
	
	/* Note: This code is reached by the fechandadd rma tests */
	/* FIXME: MT: This may need to be done atomically. */
	
	/* FIXME: Since we need to add to the tail of the list,
	   we should maintain a tail pointer rather than traversing the 
	   list each time to find the tail. */
	curr_ptr = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}
	
	MPIU_INSTR_DURATION_START(lockqueue_alloc);
	new_ptr = (MPIDI_Win_lock_queue *) MPIU_Malloc(sizeof(MPIDI_Win_lock_queue));
	MPIU_INSTR_DURATION_END(lockqueue_alloc);
	if (!new_ptr) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_Win_lock_queue");
	}
	if (prev_ptr != NULL)
	    prev_ptr->next = new_ptr;
	else 
	    win_ptr->lock_queue = new_ptr;
        
	new_ptr->next = NULL;  
	new_ptr->lock_type = lock_pkt->lock_type;
	new_ptr->source_win_handle = lock_pkt->source_win_handle;
	new_ptr->vc = vc;
	new_ptr->pt_single_op = NULL;

    /*enqueue window to detect SHM unlocks in progress engine*/
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback)
#endif
    {
        MPIDI_CH3I_SHM_win_lock_enqueue(win_ptr);
    }
#endif
    }
    
    *rreqp = NULL;
 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCK);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_LockPutUnlock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_LockPutUnlock( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt, 
					MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_lock_put_unlock_t * lock_put_unlock_pkt = 
	&pkt->lock_put_unlock;
    MPID_Win *win_ptr = NULL;
    MPID_Request *req = NULL;
    int type_size;
    int complete;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKPUTUNLOCK);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKPUTUNLOCK);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received lock_put_unlock pkt");
    
    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);

    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);
    
    req->dev.datatype = lock_put_unlock_pkt->datatype;
    MPID_Datatype_get_size_macro(lock_put_unlock_pkt->datatype, type_size);
    req->dev.recv_data_sz = type_size * lock_put_unlock_pkt->count;
    req->dev.user_count = lock_put_unlock_pkt->count;
    req->dev.target_win_handle = lock_put_unlock_pkt->target_win_handle;
    
    MPID_Win_get_ptr(lock_put_unlock_pkt->target_win_handle, win_ptr);
    
    if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, 
                                        lock_put_unlock_pkt->lock_type) == 1)
    {
	/* do the put. for this optimization, only basic datatypes supported. */
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PUT_RESP);
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_PutAccumRespComplete;
	req->dev.user_buf = lock_put_unlock_pkt->addr;
	req->dev.source_win_handle = lock_put_unlock_pkt->source_win_handle;
	req->dev.single_op_opt = 1;
#if defined(_OSU_MVAPICH_)
        if (req->dev.source_win_handle != MPI_WIN_NULL)
        {
            ++win_ptr->outstanding_rma;
        }
#endif /* defined(_OSU_MVAPICH_) */
    }
    
    else {
	/* queue the information */
	MPIDI_Win_lock_queue *curr_ptr, *prev_ptr, *new_ptr;
	
	MPIU_INSTR_DURATION_START(lockqueue_alloc);
	new_ptr = (MPIDI_Win_lock_queue *) MPIU_Malloc(sizeof(MPIDI_Win_lock_queue));
	MPIU_INSTR_DURATION_END(lockqueue_alloc);
	if (!new_ptr) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_Win_lock_queue");
	}
	
	new_ptr->pt_single_op = (MPIDI_PT_single_op *) MPIU_Malloc(sizeof(MPIDI_PT_single_op));
	if (new_ptr->pt_single_op == NULL) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_PT_single_op");
	}
	
	/* FIXME: MT: The queuing may need to be done atomically. */

	curr_ptr = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}
	
	if (prev_ptr != NULL)
	    prev_ptr->next = new_ptr;
	else 
	    win_ptr->lock_queue = new_ptr;
        
	new_ptr->next = NULL;  
	new_ptr->lock_type = lock_put_unlock_pkt->lock_type;
	new_ptr->source_win_handle = lock_put_unlock_pkt->source_win_handle;
	new_ptr->vc = vc;
	
	new_ptr->pt_single_op->type = MPIDI_RMA_PUT;
	new_ptr->pt_single_op->addr = lock_put_unlock_pkt->addr;
	new_ptr->pt_single_op->count = lock_put_unlock_pkt->count;
	new_ptr->pt_single_op->datatype = lock_put_unlock_pkt->datatype;
	/* allocate memory to receive the data */
	new_ptr->pt_single_op->data = MPIU_Malloc(req->dev.recv_data_sz);
	if (new_ptr->pt_single_op->data == NULL) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
				 req->dev.recv_data_sz);
	}

	new_ptr->pt_single_op->data_recd = 0;

	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PT_SINGLE_PUT);
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_SinglePutAccumComplete;
	req->dev.user_buf = new_ptr->pt_single_op->data;
	req->dev.lock_queue_entry = new_ptr;

    /*enqueue window to detect SHM unlocks in progress engine*/
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback)
#endif
    {
        MPIDI_CH3I_SHM_win_lock_enqueue(win_ptr);
    }
#endif
    }
    
    if (req->dev.recv_data_sz == 0) {
        *buflen = sizeof(MPIDI_CH3_Pkt_t);
	MPIDI_CH3U_Request_complete(req);
	*rreqp = NULL;
    }
    else {
	int (*fcn)( MPIDI_VC_t *, struct MPID_Request *, int * );
	fcn = req->dev.OnDataAvail;
        mpi_errno = MPIDI_CH3U_Receive_data_found(req, data_buf, &data_len,
                                                  &complete);
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
                                      "**ch3|postrecv", "**ch3|postrecv %s", 
                                      "MPIDI_CH3_PKT_LOCK_PUT_UNLOCK");
        }
	req->dev.OnDataAvail = fcn; 
	*rreqp = req;

        if (complete) 
        {
            mpi_errno = fcn(vc, req, &complete);
            if (complete)
            {
                *rreqp = NULL;
            }
        }
        
         /* return the number of bytes processed in this function */
        *buflen = data_len + sizeof(MPIDI_CH3_Pkt_t);
   }
    
    
    if (mpi_errno != MPI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER, 
				  "**ch3|postrecv", "**ch3|postrecv %s", 
				  "MPIDI_CH3_PKT_LOCK_PUT_UNLOCK");
    }

 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKPUTUNLOCK);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_LockGetUnlock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_LockGetUnlock( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
					MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_lock_get_unlock_t * lock_get_unlock_pkt = 
	&pkt->lock_get_unlock;
    MPID_Win *win_ptr = NULL;
    int type_size;
    int mpi_errno = MPI_SUCCESS;
#if defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif /* defined(_OSU_MVAPICH_) && defined(MPID_USE_SEQUENCE_NUMBERS) */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGETUNLOCK);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGETUNLOCK);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received lock_get_unlock pkt");
    
    *buflen = sizeof(MPIDI_CH3_Pkt_t);

    MPID_Win_get_ptr(lock_get_unlock_pkt->target_win_handle, win_ptr);
    
#if defined(_OSU_MVAPICH_) && defined(DEBUG)
    if (lock_get_unlock_pkt->source_win_handle != MPI_WIN_NULL)
    {
        MPIU_Assert(lock_get_unlock_pkt->rma_issued == 1);
    }
#endif /* defined(_OSU_MVAPICH_) && defined(DEBUG) */
    if (MPIDI_CH3I_Try_acquire_win_lock(win_ptr, 
                                        lock_get_unlock_pkt->lock_type) == 1)
    {
	/* do the get. for this optimization, only basic datatypes supported. */
	MPIDI_CH3_Pkt_t upkt;
	MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt = &upkt.get_resp;
	MPID_Request *req;
	MPID_IOV iov[MPID_IOV_LIMIT];
	
	req = MPID_Request_create();
	req->dev.target_win_handle = lock_get_unlock_pkt->target_win_handle;
	req->dev.source_win_handle = lock_get_unlock_pkt->source_win_handle;
	req->dev.single_op_opt = 1;
	
	MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_GET_RESP); 
	req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_GetSendRespComplete;
	req->dev.OnFinal     = MPIDI_CH3_ReqHandler_GetSendRespComplete;
	req->kind = MPID_REQUEST_SEND;
	
#if defined(_OSU_MVAPICH_)
        if (lock_get_unlock_pkt->source_win_handle != MPI_WIN_NULL)
        {
            MPID_Win *win_ptr = NULL;
            MPID_Win_get_ptr(lock_get_unlock_pkt->target_win_handle,win_ptr);
            ++win_ptr->outstanding_rma;
        }
#endif /* defined(_OSU_MVAPICH_) */
	MPIDI_Pkt_init(get_resp_pkt, MPIDI_CH3_PKT_GET_RESP);
	get_resp_pkt->request_handle = lock_get_unlock_pkt->request_handle;
	
	iov[0].MPID_IOV_BUF = (MPID_IOV_BUF_CAST) get_resp_pkt;
	iov[0].MPID_IOV_LEN = sizeof(*get_resp_pkt);
	
	iov[1].MPID_IOV_BUF = (MPID_IOV_BUF_CAST)lock_get_unlock_pkt->addr;
	MPID_Datatype_get_size_macro(lock_get_unlock_pkt->datatype, type_size);
	iov[1].MPID_IOV_LEN = lock_get_unlock_pkt->count * type_size;
	
#if defined(_OSU_MVAPICH_)
        MPIDI_VC_FAI_send_seqnum(vc, seqnum);
        MPIDI_Pkt_set_seqnum(get_resp_pkt, seqnum);
#endif /* defined(_OSU_MVAPICH_) */
	mpi_errno = MPIU_CALL(MPIDI_CH3,iSendv(vc, req, iov, 2));
	/* --BEGIN ERROR HANDLING-- */
	if (mpi_errno != MPI_SUCCESS)
	{
	    MPIU_Object_set_ref(req, 0);
	    MPIDI_CH3_Request_destroy(req);
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**ch3|rmamsg");
	}
	/* --END ERROR HANDLING-- */
    }

    else {
	/* queue the information */
	MPIDI_Win_lock_queue *curr_ptr, *prev_ptr, *new_ptr;
	
	/* FIXME: MT: This may need to be done atomically. */

	curr_ptr = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
	prev_ptr = curr_ptr;
	while (curr_ptr != NULL)
	{
	    prev_ptr = curr_ptr;
	    curr_ptr = curr_ptr->next;
	}
	
	MPIU_INSTR_DURATION_START(lockqueue_alloc);
	new_ptr = (MPIDI_Win_lock_queue *) MPIU_Malloc(sizeof(MPIDI_Win_lock_queue));
	MPIU_INSTR_DURATION_END(lockqueue_alloc);
	if (!new_ptr) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_Win_lock_queue");
	}
	new_ptr->pt_single_op = (MPIDI_PT_single_op *) MPIU_Malloc(sizeof(MPIDI_PT_single_op));
	if (new_ptr->pt_single_op == NULL) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
				 "MPIDI_PT_Single_op");
	}
	
	if (prev_ptr != NULL)
	    prev_ptr->next = new_ptr;
	else 
	    win_ptr->lock_queue = new_ptr;
        
	new_ptr->next = NULL;  
	new_ptr->lock_type = lock_get_unlock_pkt->lock_type;
	new_ptr->source_win_handle = lock_get_unlock_pkt->source_win_handle;
	new_ptr->vc = vc;
	
	new_ptr->pt_single_op->type = MPIDI_RMA_GET;
	new_ptr->pt_single_op->addr = lock_get_unlock_pkt->addr;
	new_ptr->pt_single_op->count = lock_get_unlock_pkt->count;
	new_ptr->pt_single_op->datatype = lock_get_unlock_pkt->datatype;
	new_ptr->pt_single_op->data = NULL;
	new_ptr->pt_single_op->request_handle = lock_get_unlock_pkt->request_handle;
	new_ptr->pt_single_op->data_recd = 1;

    /*enqueue window to detect SHM based unlocks in progress engine*/
#if defined(_OSU_MVAPICH_) && !defined(DAPL_DEFAULT_PROVIDER)
#if defined (_SMP_LIMIC_)
    if (!win_ptr->limic_fallback || !win_ptr->shm_fallback)
#else
    if (!win_ptr->shm_fallback)
#endif
    {
        MPIDI_CH3I_SHM_win_lock_enqueue(win_ptr);
    }
#endif
    }
    
    *rreqp = NULL;

 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGETUNLOCK);
    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_LockAccumUnlock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_LockAccumUnlock( MPIDI_VC_t *vc, MPIDI_CH3_Pkt_t *pkt,
					  MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_lock_accum_unlock_t * lock_accum_unlock_pkt = 
	&pkt->lock_accum_unlock;
    MPID_Request *req = NULL;
    MPID_Win *win_ptr = NULL;
    MPIDI_Win_lock_queue *curr_ptr = NULL, *prev_ptr = NULL, *new_ptr = NULL;
    int type_size;
    int complete;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKACCUMUNLOCK);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKACCUMUNLOCK);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received lock_accum_unlock pkt");
    
    /* no need to acquire the lock here because we need to receive the 
       data into a temporary buffer first */
    
    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);

    req = MPID_Request_create();
    MPIU_Object_set_ref(req, 1);
    
    req->dev.datatype = lock_accum_unlock_pkt->datatype;
    MPID_Datatype_get_size_macro(lock_accum_unlock_pkt->datatype, type_size);
    req->dev.recv_data_sz = type_size * lock_accum_unlock_pkt->count;
    req->dev.user_count = lock_accum_unlock_pkt->count;
    req->dev.target_win_handle = lock_accum_unlock_pkt->target_win_handle;
    
    /* queue the information */
    
    MPIU_INSTR_DURATION_START(lockqueue_alloc);
    new_ptr = (MPIDI_Win_lock_queue *) MPIU_Malloc(sizeof(MPIDI_Win_lock_queue));
    MPIU_INSTR_DURATION_END(lockqueue_alloc);
    if (!new_ptr) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
			     "MPIDI_Win_lock_queue");
    }
    
    new_ptr->pt_single_op = (MPIDI_PT_single_op *) MPIU_Malloc(sizeof(MPIDI_PT_single_op));
    if (new_ptr->pt_single_op == NULL) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %s",
			     "MPIDI_PT_single_op");
    }
    
    MPID_Win_get_ptr(lock_accum_unlock_pkt->target_win_handle, win_ptr);
    
    /* FIXME: MT: The queuing may need to be done atomically. */
    
    curr_ptr = (MPIDI_Win_lock_queue *) win_ptr->lock_queue;
    prev_ptr = curr_ptr;
    while (curr_ptr != NULL)
    {
	prev_ptr = curr_ptr;
	curr_ptr = curr_ptr->next;
    }
    
    if (prev_ptr != NULL)
	prev_ptr->next = new_ptr;
    else 
	win_ptr->lock_queue = new_ptr;
    
    new_ptr->next = NULL;  
    new_ptr->lock_type = lock_accum_unlock_pkt->lock_type;
    new_ptr->source_win_handle = lock_accum_unlock_pkt->source_win_handle;
    new_ptr->vc = vc;
    
    new_ptr->pt_single_op->type = MPIDI_RMA_ACCUMULATE;
    new_ptr->pt_single_op->addr = lock_accum_unlock_pkt->addr;
    new_ptr->pt_single_op->count = lock_accum_unlock_pkt->count;
    new_ptr->pt_single_op->datatype = lock_accum_unlock_pkt->datatype;
    new_ptr->pt_single_op->op = lock_accum_unlock_pkt->op;

    /* allocate memory to receive the data */
    new_ptr->pt_single_op->data = MPIU_Malloc(req->dev.recv_data_sz);
    if (new_ptr->pt_single_op->data == NULL) {
	MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem","**nomem %d",
			     req->dev.recv_data_sz);
    }
    
    new_ptr->pt_single_op->data_recd = 0;
    
    MPIDI_Request_set_type(req, MPIDI_REQUEST_TYPE_PT_SINGLE_ACCUM);
    req->dev.user_buf = new_ptr->pt_single_op->data;
    req->dev.lock_queue_entry = new_ptr;
    
    *rreqp = req;
    if (req->dev.recv_data_sz == 0) {
        *buflen = sizeof(MPIDI_CH3_Pkt_t);
	MPIDI_CH3U_Request_complete(req);
	*rreqp = NULL;
    }
    else {
        mpi_errno = MPIDI_CH3U_Receive_data_found(req, data_buf, &data_len,
                                                  &complete);
	/* FIXME:  Only change the handling of completion if
	   post_data_receive reset the handler.  There should
	   be a cleaner way to do this */
	if (!req->dev.OnDataAvail) {
	    req->dev.OnDataAvail = MPIDI_CH3_ReqHandler_SinglePutAccumComplete;
	}
	if (mpi_errno != MPI_SUCCESS) {
	    MPIU_ERR_SET1(mpi_errno,MPI_ERR_OTHER,"**ch3|postrecv", 
		  "**ch3|postrecv %s", "MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK");
	}
        /* return the number of bytes processed in this function */
        *buflen = data_len + sizeof(MPIDI_CH3_Pkt_t);

        if (complete) 
        {
            mpi_errno = MPIDI_CH3_ReqHandler_SinglePutAccumComplete(vc, req, &complete);
            if (complete)
            {
                *rreqp = NULL;
            }
        }
    }
 fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKACCUMUNLOCK);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_GetResp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_GetResp( MPIDI_VC_t *vc ATTRIBUTE((unused)), 
				  MPIDI_CH3_Pkt_t *pkt,
				  MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_get_resp_t * get_resp_pkt = &pkt->get_resp;
    MPID_Request *req;
    int complete;
    char *data_buf = NULL;
    MPIDI_msg_sz_t data_len;
    int mpi_errno = MPI_SUCCESS;
    int type_size;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_GETRESP);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_GETRESP);
    
    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received get response pkt");

    data_len = *buflen - sizeof(MPIDI_CH3_Pkt_t);
    data_buf = (char *)pkt + sizeof(MPIDI_CH3_Pkt_t);
    
    MPID_Request_get_ptr(get_resp_pkt->request_handle, req);
    
    MPID_Datatype_get_size_macro(req->dev.datatype, type_size);
    req->dev.recv_data_sz = type_size * req->dev.user_count;
    
    /* FIXME: It is likely that this cannot happen (never perform
       a get with a 0-sized item).  In that case, change this
       to an MPIU_Assert (and do the same for accumulate and put) */
    if (req->dev.recv_data_sz == 0) {
	MPIDI_CH3U_Request_complete( req );
        *buflen = sizeof(MPIDI_CH3_Pkt_t);
	*rreqp = NULL;
    }
    else {
#if defined(_OSU_MVAPICH_)
/* FIXME: I believe the problem is the change from
 * MPIDI_CH3U_Post_data_receive_found (1.0) to
 * MPIDI_CH3U_Receive_data_found (1.1). For now, I think we can comment out
 * 1076 - 1083 (exp2) so that make things are consistent with 1.0.
 * */
    if (VAPI_PROTOCOL_RPUT == req->mrail.protocol)
    {
        MPIDI_CH3_Get_rndv_recv(vc, req);
        vc->ch.recv_active = NULL;
        *rreqp = NULL;
    }
    else
    {
        *rreqp = req;
    }
#else /* defined(_OSU_MVAPICH_) */
	*rreqp = req;
        mpi_errno = MPIDI_CH3U_Receive_data_found(req, data_buf,
                                                  &data_len, &complete);
        MPIU_ERR_CHKANDJUMP1(mpi_errno, mpi_errno, MPI_ERR_OTHER, "**ch3|postrecv", "**ch3|postrecv %s", "MPIDI_CH3_PKT_GET_RESP");
        if (complete) 
        {
            MPIDI_CH3U_Request_complete(req);
            *rreqp = NULL;
        }
#endif /* defined(_OSU_MVAPICH_) */
        /* return the number of bytes processed in this function */
        *buflen = data_len + sizeof(MPIDI_CH3_Pkt_t);
    }
 fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_GETRESP);
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_LockGranted
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_LockGranted( MPIDI_VC_t *vc ATTRIBUTE((unused)), 
				      MPIDI_CH3_Pkt_t *pkt,
				      MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_lock_granted_t * lock_granted_pkt = &pkt->lock_granted;
    MPID_Win *win_ptr = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGRANTED);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGRANTED);

    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received lock granted pkt");
    
    *buflen = sizeof(MPIDI_CH3_Pkt_t);

    MPID_Win_get_ptr(lock_granted_pkt->source_win_handle, win_ptr);
    /* set the lock_granted flag in the window */
    win_ptr->lock_granted = 1;
    
    *rreqp = NULL;
    MPIDI_CH3_Progress_signal_completion();	

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_LOCKGRANTED);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_PktHandler_PtRMADone
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_PktHandler_PtRMADone( MPIDI_VC_t *vc ATTRIBUTE((unused)), 
				    MPIDI_CH3_Pkt_t *pkt, 
				    MPIDI_msg_sz_t *buflen, MPID_Request **rreqp )
{
    MPIDI_CH3_Pkt_pt_rma_done_t * pt_rma_done_pkt = &pkt->pt_rma_done;
    MPID_Win *win_ptr = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_PKTHANDLER_PTRMADONE);
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_PKTHANDLER_PTRMADONE);

    MPIU_DBG_MSG(CH3_OTHER,VERBOSE,"received shared lock ops done pkt");

    *buflen = sizeof(MPIDI_CH3_Pkt_t);

    MPID_Win_get_ptr(pt_rma_done_pkt->source_win_handle, win_ptr);
    /* reset the lock_granted flag in the window */
    win_ptr->lock_granted = 0;

    *rreqp = NULL;
    MPIDI_CH3_Progress_signal_completion();	

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_PKTHANDLER_PTRMADONE);
    return MPI_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* 
 * For debugging, we provide the following functions for printing the 
 * contents of an RMA packet
 */
/* ------------------------------------------------------------------------ */
#ifdef MPICH_DBG_OUTPUT
int MPIDI_CH3_PktPrint_Put( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_PUT\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->put.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->put.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->put.datatype));
    MPIU_DBG_PRINTF((" dataloop_size. 0x%08X\n", pkt->put.dataloop_size));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->put.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->put.source_win_handle));
    /*MPIU_DBG_PRINTF((" win_ptr ...... 0x%08X\n", pkt->put.win_ptr));*/
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_Get( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_GET\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->get.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->get.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->get.datatype));
    MPIU_DBG_PRINTF((" dataloop_size. %d\n", pkt->get.dataloop_size));
    MPIU_DBG_PRINTF((" request ...... 0x%08X\n", pkt->get.request_handle));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->get.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->get.source_win_handle));
    /*
      MPIU_DBG_PRINTF((" request ...... 0x%08X\n", pkt->get.request));
      MPIU_DBG_PRINTF((" win_ptr ...... 0x%08X\n", pkt->get.win_ptr));
    */
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_GetResp( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_GET_RESP\n"));
    MPIU_DBG_PRINTF((" request ...... 0x%08X\n", pkt->get_resp.request_handle));
    /*MPIU_DBG_PRINTF((" request ...... 0x%08X\n", pkt->get_resp.request));*/
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_Accumulate( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_ACCUMULATE\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->accum.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->accum.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->accum.datatype));
    MPIU_DBG_PRINTF((" dataloop_size. %d\n", pkt->accum.dataloop_size));
    MPIU_DBG_PRINTF((" op ........... 0x%08X\n", pkt->accum.op));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->accum.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->accum.source_win_handle));
    /*MPIU_DBG_PRINTF((" win_ptr ...... 0x%08X\n", pkt->accum.win_ptr));*/
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_Accum_Immed( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_ACCUM_IMMED\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->accum_immed.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->accum_immed.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->accum_immed.datatype));
    MPIU_DBG_PRINTF((" op ........... 0x%08X\n", pkt->accum_immed.op));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->accum_immed.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->accum_immed.source_win_handle));
    /*MPIU_DBG_PRINTF((" win_ptr ...... 0x%08X\n", pkt->accum.win_ptr));*/
    fflush(stdout);
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_Lock( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_LOCK\n"));
    MPIU_DBG_PRINTF((" lock_type .... %d\n", pkt->lock.lock_type));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->lock.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock.source_win_handle));
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_LockPutUnlock( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_LOCK_PUT_UNLOCK\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->lock_put_unlock.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->lock_put_unlock.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->lock_put_unlock.datatype));
    MPIU_DBG_PRINTF((" lock_type .... %d\n", pkt->lock_put_unlock.lock_type));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->lock_put_unlock.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock_put_unlock.source_win_handle));
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_LockAccumUnlock( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->lock_accum_unlock.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->lock_accum_unlock.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->lock_accum_unlock.datatype));
    MPIU_DBG_PRINTF((" lock_type .... %d\n", pkt->lock_accum_unlock.lock_type));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->lock_accum_unlock.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock_accum_unlock.source_win_handle));
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_LockGetUnlock( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_LOCK_GET_UNLOCK\n"));
    MPIU_DBG_PRINTF((" addr ......... %p\n", pkt->lock_get_unlock.addr));
    MPIU_DBG_PRINTF((" count ........ %d\n", pkt->lock_get_unlock.count));
    MPIU_DBG_PRINTF((" datatype ..... 0x%08X\n", pkt->lock_get_unlock.datatype));
    MPIU_DBG_PRINTF((" lock_type .... %d\n", pkt->lock_get_unlock.lock_type));
    MPIU_DBG_PRINTF((" target ....... 0x%08X\n", pkt->lock_get_unlock.target_win_handle));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock_get_unlock.source_win_handle));
    MPIU_DBG_PRINTF((" request ...... 0x%08X\n", pkt->lock_get_unlock.request_handle));
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_PtRMADone( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_PT_RMA_DONE\n"));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock_accum_unlock.source_win_handle));
    return MPI_SUCCESS;
}
int MPIDI_CH3_PktPrint_LockGranted( FILE *fp, MPIDI_CH3_Pkt_t *pkt )
{
    MPIU_DBG_PRINTF((" type ......... MPIDI_CH3_PKT_LOCK_GRANTED\n"));
    MPIU_DBG_PRINTF((" source ....... 0x%08X\n", pkt->lock_granted.source_win_handle));
    return MPI_SUCCESS;
}
#endif

#if defined (_OSU_PSM_)
int psm_dt_1scop(MPID_Request *req, char *buf, int len)
{
    int predefined;
    int size;

    MPIDI_CH3I_DATATYPE_IS_PREDEFINED(req->dev.datatype, predefined);
    if(predefined) {
        memcpy(req->dev.user_buf, buf, len);
    /* } else if(req->dev.datatype_ptr->is_contig) {
        memcpy(req->dev.user_buf, buf, len); */
    } else {
        PSM_PRINT("small unpack\n");
        MPID_Datatype_get_size_macro(req->dev.datatype, size);
        size = size * req->dev.user_count;
        psm_do_unpack(req->dev.user_count, req->dev.datatype, NULL, buf, size,
                req->dev.user_buf, len);
    }

    return MPI_SUCCESS;
}
#endif /* _OSU_PSM_ */
