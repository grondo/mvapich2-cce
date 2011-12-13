/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpidi_ch3_impl.h"
#include "pmi.h"
#include "error_handling.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3_Abort
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3_Abort(int exit_code, char *error_msg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3_ABORT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3_ABORT);

    /* print backtrace */
    if (show_backtrace) print_backtrace();
    
    PMI_Abort(exit_code, error_msg);

    /* if abort returns for some reason, exit here */

    MPIU_Error_printf("%s", error_msg);
    fflush(stderr);

    exit(exit_code);
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3_ABORT);
    return MPI_ERR_INTERN;
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(default, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
}
