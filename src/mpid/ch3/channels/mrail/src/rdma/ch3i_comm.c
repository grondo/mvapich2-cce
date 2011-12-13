/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"
#include "mpiimpl.h"
#include "coll_shmem.h"

#define NULL_CONTEXT_ID -1

static MPID_Collops collective_functions_osu = {
    0,    /* ref_count */
    MPIR_Barrier_MV2, /* Barrier */ 
    MPIR_Bcast_MV2, /* Bcast intra*/
    MPIR_Gather_MV2, /* Gather */
    MPIR_Gatherv, /* Gatherv */
    MPIR_Scatter_MV2, /* Scatter */
    MPIR_Scatterv, /* Scatterv */
    MPIR_Allgather_MV2, /* Allgather */
    MPIR_Allgatherv_MV2, /* Allgatherv */
    MPIR_Alltoall_MV2, /* Alltoall */
    MPIR_Alltoallv, /* Alltoallv */
    MPIR_Alltoallw, /* Alltoallw */
    MPIR_Reduce_MV2, /* Reduce */
    MPIR_Allreduce_MV2, /* Allreduce */
    MPIR_Reduce_scatter, /* Reduce_scatter */
    MPIR_Scan, /* Scan */
    NULL  /* Exscan */
};

static MPID_Collops collective_functions_anl = {
    0,    /* ref_count */
    MPIR_Barrier, /* Barrier */
    MPIR_Bcast, /* Bcast intra*/
    MPIR_Gather, /* Gather */
    MPIR_Gatherv, /* Gatherv */
    MPIR_Scatter, /* Scatter */
    MPIR_Scatterv, /* Scatterv */
    MPIR_Allgather, /* Allgather */
    MPIR_Allgatherv, /* Allgatherv */
    MPIR_Alltoall, /* Alltoall */
    MPIR_Alltoallv, /* Alltoallv */
    MPIR_Alltoallw, /* Alltoallw */
    MPIR_Reduce, /* Reduce */
    MPIR_Allreduce, /* Allreduce */
	MPIR_Reduce_scatter, /* Reduce_scatter */
    MPIR_Scan, /* Scan */
    NULL  /* Exscan */
};


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_comm_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_comm_create (MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_COMM_CREATE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_COMM_CREATE);

    if(use_osu_collectives == 1 && 
        comm->comm_kind == MPID_INTRACOMM)  { 
        comm->coll_fns = &collective_functions_osu;
    } else { 
        comm->coll_fns = &collective_functions_anl;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_COMM_CREATE);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_comm_destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_comm_destroy (MPID_Comm *comm)
{
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);

    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_COMM_DESTROY);
    return mpi_errno;
}
