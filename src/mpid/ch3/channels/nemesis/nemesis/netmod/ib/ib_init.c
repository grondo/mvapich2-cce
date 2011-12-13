/*! \file */
/*
 *  (C) 2006 by Argonne National Laboratory.
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

#define _GNU_SOURCE

#include "ib_process.h"

#include "pmi.h"
#include "mpidimpl.h"
#include "mpid_nem_impl.h"
#include "ib_device.h"
#include "ib_cm.h"
#include "ib_init.h"

#include "ib_vc.h"
#include "ib_poll.h"
#include "ib_send.h"
#include "ib_finalize.h"
#include "ib_errors.h"
#include "ib_srq.h"
#include "dreg.h"
#include "mem_hooks.h"

/************************************************
 * Using Infiniband Nemesis netmod              *
 ************************************************/

/**
 * Nemesis access function structure.
 */
MPID_nem_netmod_funcs_t MPIDI_nem_ib_funcs = {
    MPID_nem_ib_init,
    MPID_nem_ib_finalize,
    /* MPID_nem_ib_ckpt_shutdown, */
    #ifdef ENABLE_CHECKPOINTING
    NULL,
    NULL,
    NULL,
    #endif
    MPID_nem_ib_poll,
    MPID_nem_ib_get_business_card,
    MPID_nem_ib_connect_to_root,
    MPID_nem_ib_vc_init,
    MPID_nem_ib_vc_destroy,
    MPID_nem_ib_vc_terminate
};

MPID_nem_queue_ptr_t MPID_nem_module_ib_recv_queue = 0;
MPID_nem_queue_ptr_t MPID_nem_module_ib_free_queue = 0;

struct ibv_mr *proc_elements_mr = NULL;
struct ibv_mr *module_elements_mr = NULL;

MPID_nem_ib_dev_param_t *MPID_nem_ib_dev_param_ptr = 0;


/**
 * Process information.
 */
MPID_nem_ib_process_info_t process_info;


#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_get_business_card
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

int MPID_nem_ib_get_business_card (int my_rank, char **bc_val_p,
        int *val_max_sz_p)
{
    int mpi_errno = MPI_SUCCESS;
    return mpi_errno;
}

/* --------------------------------------------------------------------*/
#ifdef EPM_ENABLE_PMI_INIT

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_pmi_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * Initialize Process Manager Interface and update global_info.
 * Called by MPID_nem_ib_init.
 *
 *   -# Initialize the Process Manager Interface;
 *   -# Set the rank;
 *   -# Set the progexx group size;
 *
 * \see MPID_nem_ib_init
 *
    \todo Need to add more stuff here
          Look at InitPG in mvapich2/trunk/src/mpid/ch3/src/mpid_init.c
 */
int MPID_nem_ib_pmi_init()
{
    int pmi_errno   = 0;
    int mpi_errno = MPI_SUCCESS;
    int spawned;
    /* Process group id size*/
    int pg_id_sz,  pg_size;
    char *pg_id;
    MPIDI_PG_t *pg = 0;



    assert( global_info!= NULL );

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_PMI_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_PMI_INIT);

    /* Initialize the Process Manager Interface */
    pmi_errno = PMI_Init(&spawned);
    if (pmi_errno != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**pmi_init",
                 "**pmi_init %d", pmi_errno);
    }

    /* Set the rank */
    pmi_errno = PMI_Get_rank(&global_info->pg_rank);
    if (pmi_errno != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**pmi_get_rank",
                 "**pmi_get_rank %d", pmi_errno);
    }

    /* Set the progexx group size */
    pmi_errno = PMI_Get_size(&global_info->pg_size);
    if (pmi_errno != 0) {
        MPIU_ERR_SETANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**pmi_get_size",
                 "**pmi_get_size %d", pmi_errno);
    }

    /* -------------------------------------- From InitPG in mvapich2/trunk/src/mpid/ch3/src/mpid_init.c
	pmi_errno = PMI_Get_appnum(&appnum);
	if (pmi_errno != PMI_SUCCESS) {
	    MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_get_appnum",
				 "**pmi_get_appnum %d", pmi_errno);
	}

    / * Note that if pmi is not availble, the value of MPI_APPNUM is
       not set * /
    if (appnum != -1) {
        MPIR_Process.attrs.appnum = appnum;
    }
	*/

    /* Now, initialize the process group information with PMI calls */
    /*
     * Get the process group id
     */
    pmi_errno = PMI_KVS_Get_name_length_max(&pg_id_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /*
         * I don't believe that MPICH2 has updated the error message for this
         * yet.
         */
        MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER,
                 "**pmi_get_id_length_max",
                 "**pmi_get_id_length_max %d", pmi_errno);
    }

    /* This memory will be freed by the PG_Destroy if there is an error */
    pg_id = MPIU_Malloc(pg_id_sz + 1);
    if (pg_id == NULL) {
        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**nomem");
    }

    /* Note in the singleton init case, the pg_id is a dummy.
       We'll want to replace this value if we join a
       Process manager */
    pmi_errno = PMI_KVS_Get_my_name(pg_id, pg_id_sz);
    if (pmi_errno != PMI_SUCCESS) {
        /*
         * I don't believe the MPICH2 team has updated the error message for
         * this change yet.
         */
        MPIU_ERR_SETANDJUMP1(mpi_errno,MPI_ERR_OTHER, "**pmi_get_id",
                 "**pmi_get_id %d", pmi_errno);
    }

    /*
     * Create a new structure to track the process group for our MPI_COMM_WORLD
     */
    mpi_errno = MPIDI_PG_Create(pg_size, pg_id, &pg);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER, "**dev|pg_create");
    }

    MPIDI_PG_InitConnKVS( pg );


fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_PMI_INIT);
    return mpi_errno;

fn_fail:
	if (pg) {
		MPIDI_PG_Destroy( pg );
	}
	goto fn_exit;
}

#endif /* EPM_ENABLE_PMI_INIT */
/* --------------------------------------------------------------------*/

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_allocate_memory
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int     
MPID_nem_ib_allocate_memory(int pg_rank, int pg_size)
{   
    int mpi_errno = MPI_SUCCESS;
    process_info.polling_group_size = 0;

    if (rdma_polling_set_limit > 0)
    {
        process_info.polling_set = (MPIDI_VC_t**) MPIU_Malloc(rdma_polling_set_limit * sizeof(MPIDI_VC_t*));
    }
    else
    {
        process_info.polling_set = (MPIDI_VC_t**) MPIU_Malloc(pg_size * sizeof(MPIDI_VC_t*));
    }

    if (!process_info.polling_set)
    {
    fprintf(
            stderr,
            "[%s:%d]: %s\n",
            __FILE__,
            __LINE__,
            "unable to allocate space for polling set\n");
        return 0;
    }

    /* We need to allocate vbufs for send/recv path */
    if ((mpi_errno = allocate_vbufs(rdma_vbuf_pool_size)))
    {
        return mpi_errno;
    }

    return mpi_errno;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * MPID_nem_ib_init - Initialize the Nemesis IB module
 *
 *  -# Allocate and initialize global information;
 *  -# Initialize PMI and get related parameters;
 *  -# Allocate and initialize conn mgmt related info;
 *  -# Set default parameters;
 *  -# Get user defined parameters;
 *  -# Open and init all HCA's for communication;
 *  -# Setup QP's and other things for communication;
 *  -# Exchange conn info between all processes;
 *  -# Exchange conn info between all processes;
 *  -# Free conn mgmt related info.
 *
 * @proc_recv_queue: main recv queue for the process
 * @proc_free_queue: main free queue for the process
 * @proc_elements: pointer to process' queue elements
 * @num_proc_elements: number of process' queue elements
 * @module_elements: pointer to the queue elements used by this module
 * @num_module_elements: number of queue elements for use by this module
 * @module_recv_queue: pointer to recv queue for this
 *  module. The process will add elements to this
 *  queue for the module to send
 * @module_free_queue: pointer to the free queue for
 *  this module. The process will return elements to
 *  this queue
 * @ckpt_restart: true if this is a restart from a
 *  checkpoint. In a restart, the network needs to
 *  be brought up again, but we want to keep things
 *  like sequence numbers.
 * @pg_p: MPICH2 process group pointer
 * @pg_rank: Rank in the process group
 * @bc_val_p: Pointer to business card pointer
 * @val_max_sz_p: Pointer to max. size
 */

int MPID_nem_ib_init (MPIDI_PG_t *pg_p,
        int pg_rank,
        char **bc_val_p,
        int *val_max_sz_p)
{
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPID_IB_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPID_IB_INIT);

    /* Make sure that our private fields in vc fit into the area provided. */
    MPIU_Assert(sizeof(MPID_nem_ib_vc_area) <= MPID_NEM_VC_NETMOD_AREA_LEN);


    /* Allocate and initialize conn mgmt related info  */
    mpi_errno = MPID_nem_ib_init_process_info(pg_rank, pg_p);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init process info");
    }


    mpi_errno = MPID_nem_ib_init_connection(pg_rank, pg_p->size);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init conn info");
    }

    mpi_errno = MPID_nem_ib_get_control_params();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to get control params");
    }

    /* Open and init all HCA's for communication */
    mpi_errno = MPID_nem_ib_init_hca();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init HCA");
    }

    mpi_errno = MPID_nem_ib_get_control_params_after_hcainit();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to get control params after hca_init");
    }

    /* Set default parameters. */
    mpi_errno = MPID_nem_ib_set_default_params();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to set def params");
    }

    /* Get user defined parameters. */
    mpi_errno = MPID_nem_ib_get_user_params();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to get user params");
    }

    /* init process_init_info for communication info exchange */
    mpi_errno = MPID_nem_ib_alloc_process_init_info();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init process_init_info");
    }

    mpi_errno = MPID_nem_ib_setup_startup_ring(pg_p, pg_rank);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to setup startup ring");
    }

    if (process_info.has_srq) {
        mpi_errno = init_vbuf_lock();
        if(mpi_errno) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init vbuf lock");
        }
    }

    mpi_errno = MPID_nem_ib_open_ports();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init HCA");
    }

    /* Setup QP's and other things for communication */
    mpi_errno = MPID_nem_ib_setup_conn(pg_p);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to setup conn");
    }

    /* Exchange conn info between all processes */
    mpi_errno = MPID_nem_ib_exchange_conn(pg_p, pg_rank);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to exchange conn info");

    }

#if !defined(DISABLE_PTMALLOC)
    if (!mvapich2_minit()) {
        process_info.has_lazy_mem_unregister = 1;

        /* Initialize the registration cache */
        mpi_errno = dreg_init();
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to initialize registration cache");
        }
    }
#else /* !defined(DISABLE_PTMALLOC) */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
    process_info.has_lazy_mem_unregister = 0;
#endif /* !defined(DISABLE_PTMALLOC) */


    /* Allocate RDMA Buffers */
    mpi_errno = MPID_nem_ib_allocate_memory(
            pg_rank,
            pg_p->size);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to allocate memory");
    }

    if(process_info.has_srq) {
        mpi_errno = MPID_nem_ib_allocate_srq();
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                       "**fail %s", "Failed to allocate memory for srq");

        }
    }

    /* establish conn info between all processes */
    mpi_errno = MPID_nem_ib_establish_conn();
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to establish conn");
    }

    /* init channel manager */
    /* Defined in ib_channel_manager.c, need to declare in ib_channel_manager.h
     *
     */
    mpi_errno = MPIDI_nem_ib_init_cmanager(pg_rank, pg_p->size);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                   "**fail %s", "Failed to init cmanager");
    }

    /* Free conn mgmt related info */
    mpi_errno = MPID_nem_ib_free_conn_info(pg_p->size);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**fail",
                                    "**fail %s", "Failed to init conn info");
    }


fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPID_IB_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}
