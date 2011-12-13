/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"
#include "pmi.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Comm_connect
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Comm_connect(char *port_name, int root, MPID_Comm * comm_ptr,
                           MPID_Comm ** newcomm)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_COMM_CONNECT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_COMM_CONNECT);
    int mpi_errno =
        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                             __LINE__, MPI_ERR_OTHER, "**notimpl", 0);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_COMM_CONNECT);
    return mpi_errno;
}
