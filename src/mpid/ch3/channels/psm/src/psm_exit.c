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
#include "coll_shmem.h"
#include "psm_vbuf.h"

#undef FUNCNAME
#define FUNCNAME psm_dofinalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_dofinalize()
{
    MPIDI_VC_t *vc = NULL;
    int mpi_errno = MPI_ERR_INTERN;
    psm_error_t psmerr;

    if((psmerr = psm_mq_finalize(psmdev_cw.mq)) != PSM_OK) {
        PSM_ERR_ABORT("psm_mq_finalize failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }

    if((psmerr = psm_ep_close(psmdev_cw.ep, PSM_EP_CLOSE_GRACEFUL, 
                     5 * SEC_IN_NS)) != PSM_OK) {
        PSM_ERR_ABORT("psm_ep_close failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }

    if((psmerr = psm_finalize() != PSM_OK)) {
        PSM_ERR_ABORT("psm_finalize failed: %s\n",
                psm_error_get_string(psmerr));
        MPIU_ERR_POP(mpi_errno);
    }
    MPIU_Free(psmdev_cw.epaddrs);

    psm_deallocate_vbuf();

    if (enable_shmem_collectives){
        MPIDI_PG_Get_vc(MPIDI_Process.my_pg, MPIDI_Process.my_pg_rank, &vc);
	    /* Freeing up shared memory collective resources*/
     	MPIDI_CH3I_SHMEM_COLL_finalize(vc->smp.local_rank, MPIDI_Process.my_pg->ch.num_local_processes);
    }

    mpi_errno = MPI_SUCCESS;
fn_fail:
    return mpi_errno;
}
