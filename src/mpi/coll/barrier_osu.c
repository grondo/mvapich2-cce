/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
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

#include "mpiimpl.h"
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "coll_shmem.h"

/*
Comm_N2_prev - retrieve greatest power of two < size of Comm.
*/

static int Comm_N2_prev(int x){ 
    int v=1, old_v=1;
    if(!(x & (x-1))) return x;
    while(v < x) {
        old_v = v;
        v = v << 1;
    }
    return old_v;
} 

static int MPIR_Pairwise_Barrier_MV2(MPID_Comm *comm_ptr, int *errflag){
    
    int size, rank;
    int d, dst, src;
    int mpi_errno=MPI_SUCCESS;
    MPI_Comm comm;
    
    size = comm_ptr->local_size;
    /* Trivial barriers return immediately */
    if (size == 1) return MPI_SUCCESS;

    rank = comm_ptr->rank;
    comm = comm_ptr->handle;

    int N2_prev=Comm_N2_prev(size);
    int surfeit = size - N2_prev;

    /* Perform a combine-like operation */
    if ( rank < N2_prev ) {
        if( rank < surfeit ) {
            /* get the fanin letter from the upper "half" process: */
            dst = N2_prev + rank;
            mpi_errno = MPIC_Recv_ft(NULL, 0, MPI_BYTE, dst, MPIR_BARRIER_TAG,
                     comm, MPI_STATUS_IGNORE, errflag);
        }

        /* combine on embedded N2_prev power-of-two processes */
        for (d = 1; d < N2_prev; d <<= 1) {
            dst = (rank ^ d);
            mpi_errno = MPIC_Sendrecv_ft(NULL, 0, MPI_BYTE, dst, MPIR_BARRIER_TAG,
                     NULL , 0, MPI_BYTE, dst, MPIR_BARRIER_TAG,
                     comm, MPI_STATUS_IGNORE, errflag);
        }

        /* fanout data to nodes above N2_prev... */
        if ( rank < surfeit ) {
            dst = N2_prev + rank;
            mpi_errno = MPIC_Send_ft(NULL, 0, MPI_BYTE, dst, MPIR_BARRIER_TAG,
                 comm, errflag);
        }
    } else {
        /* fanin data to power of 2 subset */
        src = rank - N2_prev;
        mpi_errno = MPIC_Sendrecv_ft(NULL, 0, MPI_BYTE, src, MPIR_BARRIER_TAG,
               NULL, 0, MPI_BYTE, src, MPIR_BARRIER_TAG,
               comm, MPI_STATUS_IGNORE, errflag);
    }

    return mpi_errno;

}

static int MPIR_shmem_barrier_MV2(MPID_Comm *comm_ptr, int *errflag){
    
    int mpi_errno=MPI_SUCCESS;
	
    MPI_Comm shmem_comm = MPI_COMM_NULL, leader_comm = MPI_COMM_NULL;
    MPID_Comm *shmem_commptr = NULL, *leader_commptr = NULL;
    int local_rank = -1, local_size=0;
    int total_size, shmem_comm_rank;

    shmem_comm = comm_ptr->ch.shmem_comm;
    leader_comm = comm_ptr->ch.leader_comm;

    total_size = comm_ptr->local_size;
    shmem_comm = comm_ptr->ch.shmem_comm;

    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank = shmem_commptr->rank;
    local_size = shmem_commptr->local_size;
    shmem_comm_rank = shmem_commptr->ch.shmem_comm_rank;
    leader_comm = comm_ptr->ch.leader_comm;
    MPID_Comm_get_ptr(leader_comm, leader_commptr);

    if (local_size > 1) {
        MPIDI_CH3I_SHMEM_COLL_Barrier_gather(local_size, local_rank, shmem_comm_rank);
    }

    if ((local_rank == 0) && (local_size != total_size)) {
        mpi_errno = MPIR_Pairwise_Barrier_MV2(leader_commptr, errflag );
    }

    if (local_size > 1) {
        MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(local_size, local_rank, shmem_comm_rank);
    }

    return mpi_errno;

}
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

/* This is the default implementation of the barrier operation.  The
   algorithm is:
   
   Algorithm: MPI_Barrier

   We use pairwise exchange with recursive doubling algorithm 
   described in:
   R. Gupta, V. Tipparaju, J. Nieplocha and D.K. Panda,
   "Efficient Barrier using Remote Memory Operations on VIA-Based Clusters",
   IEEE Cluster Computing, 2002

   Possible improvements: 

   End Algorithm: MPI_Barrier

   This is an intracommunicator barrier only!
*/

/* not declared static because it is called in ch3_comm_connect/accept */
#undef FUNCNAME
#define FUNCNAME MPIR_Barrier_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Barrier_intra_MV2( MPID_Comm *comm_ptr, int *errflag )
{
    int size;
    int mpi_errno=MPI_SUCCESS;
    int mpi_errno_ret=MPI_SUCCESS;

	size = comm_ptr->local_size;
    /* Trivial barriers return immediately */
    if (size == 1) return MPI_SUCCESS;
              
    /* Only one collective operation per communicator can be active at any
       time */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)

#if defined(CKPT)
    MPIDI_CH3I_CR_lock();
#endif

    if (enable_shmem_collectives && disable_shmem_barrier == 0
          && comm_ptr->ch.shmem_coll_ok == 1 ){

        mpi_errno = MPIR_shmem_barrier_MV2(comm_ptr, errflag);

    } else {

        mpi_errno = MPIR_Pairwise_Barrier_MV2(comm_ptr, errflag);
    }

    
#if defined(CKPT)
    MPIDI_CH3I_CR_unlock();
#endif

#else

    mpi_errno = MPIR_Barrier_intra( comm_ptr, errflag );
    
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }   
    
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);
    
    return mpi_errno;
}

/* not declared static because a machine-specific function may call this one 
   in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Barrier_inter
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Barrier_inter_MV2( MPID_Comm *comm_ptr, int *errflag )
{
    int rank, mpi_errno, i, root;
    MPID_Comm *newcomm_ptr = NULL;

    rank = comm_ptr->rank;

    /* Get the local intracommunicator */
    if (!comm_ptr->local_comm)
	MPIR_Setup_intercomm_localcomm( comm_ptr );

    newcomm_ptr = comm_ptr->local_comm;

    /* do a barrier on the local intracommunicator */
    mpi_errno = MPIR_Barrier(newcomm_ptr, errflag);
    /* --BEGIN ERROR HANDLING-- */
    if (mpi_errno)
    {
	mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	return mpi_errno;
    }
    /* --END ERROR HANDLING-- */

    /* rank 0 on each group does an intercommunicator broadcast to the
       remote group to indicate that all processes in the local group
       have reached the barrier. We do a 1-byte bcast because a 0-byte
       bcast will just return without doing anything. */
    
    /* first broadcast from left to right group, then from right to
       left group */
    if (comm_ptr->is_low_group) {
        /* bcast to right*/
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr, errflag); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        /* receive bcast from right */
        root = 0;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr, errflag); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }
    else {
        /* receive bcast from left */
        root = 0;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr, errflag); 
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
        /* bcast to left */
        root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
        mpi_errno = MPIR_Bcast_inter(&i, 1, MPI_BYTE, root, comm_ptr, errflag);  
	/* --BEGIN ERROR HANDLING-- */
        if (mpi_errno)
	{
	    mpi_errno = MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 0);
	    return mpi_errno;
	}
	/* --END ERROR HANDLING-- */
    }

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Barrier_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Barrier_MV2(MPID_Comm *comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
        /* intracommunicator */
        mpi_errno = MPIR_Barrier_intra_MV2( comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    } else {
        /* intercommunicator */
        mpi_errno = MPIR_Barrier_inter_MV2( comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

