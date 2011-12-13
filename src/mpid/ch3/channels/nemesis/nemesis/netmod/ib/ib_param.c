/*!\file */
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
#include "mpidimpl.h"
#include "pmi.h"

#include "ib_device.h"
#include "ib_cm.h"
#include "ib_param.h"
#include "ib_process.h"
#include "ib_hca.h"
#include "ib_send.h"
#include "ib_lmt.h"
#include "ib_vbuf.h"
#include "mv2_utils.h"
#include <mv2_arch_hca_detect.h>

#define EAGER_THRESHOLD_ADJUST    0
#define INLINE_THRESHOLD_ADJUST  (20)

/* Global variables */
int           rdma_num_qp_per_port = 1;
int           rdma_num_rails = 1;
int           rdma_pin_pool_size = RDMA_PIN_POOL_SIZE;
unsigned long rdma_default_max_cq_size = RDMA_DEFAULT_MAX_CQ_SIZE;
int           rdma_default_port = RDMA_DEFAULT_PORT;
unsigned long rdma_default_max_send_wqe = RDMA_DEFAULT_MAX_SEND_WQE;
unsigned long rdma_default_max_recv_wqe = RDMA_DEFAULT_MAX_RECV_WQE;
uint32_t      rdma_default_max_sg_list = RDMA_DEFAULT_MAX_SG_LIST;
uint16_t      rdma_default_pkey_ix = RDMA_DEFAULT_PKEY_IX;
uint16_t      rdma_default_pkey = RDMA_DEFAULT_PKEY;
uint8_t       rdma_default_qp_ous_rd_atom;
uint8_t       rdma_default_max_rdma_dst_ops = RDMA_DEFAULT_MAX_RDMA_DST_OPS;
enum ibv_mtu  rdma_default_mtu;
uint32_t      rdma_default_psn = RDMA_DEFAULT_PSN;
uint8_t       rdma_default_min_rnr_timer = RDMA_DEFAULT_MIN_RNR_TIMER;
uint8_t       rdma_default_service_level = RDMA_DEFAULT_SERVICE_LEVEL;
uint8_t       rdma_default_static_rate = RDMA_DEFAULT_STATIC_RATE;
uint8_t       rdma_default_src_path_bits = RDMA_DEFAULT_SRC_PATH_BITS;
uint8_t       rdma_default_time_out = RDMA_DEFAULT_TIME_OUT;
uint8_t       rdma_default_retry_count = RDMA_DEFAULT_RETRY_COUNT;
uint8_t       rdma_default_rnr_retry = RDMA_DEFAULT_RNR_RETRY;
int           rdma_default_put_get_list_size = RDMA_DEFAULT_PUT_GET_LIST_SIZE;
int           rdma_read_reserve = RDMA_READ_RESERVE;
long          rdma_eagersize_1sc;
int           rdma_put_fallback_threshold;
int           rdma_get_fallback_threshold;
int           rdma_integer_pool_size = RDMA_INTEGER_POOL_SIZE;
int           rdma_polling_set_limit = -1;
int           rdma_polling_set_threshold = 10;
int           rdma_fp_buffer_size = RDMA_FP_DEFAULT_BUF_SIZE;
int           rdma_fp_sendconn_accepted = 0;
int           rdma_pending_conn_request = 0;
int	          rdma_eager_limit = 32;
int           rdma_iba_eager_threshold;
char          rdma_iba_hca[32];
int           rdma_max_inline_size;
unsigned int  rdma_ndreg_entries = RDMA_NDREG_ENTRIES;
int           rdma_rndv_protocol = MV2_LMT_PROTOCOL_RPUT;
int           rdma_r3_threshold = 4096;
int           rdma_r3_threshold_nocache = 8192 * 4;
int           rdma_max_r3_pending_data = 512 * 1024;
int           num_rdma_buffer;
int           rdma_use_smp = 1;
int           rdma_qos_num_sls = RDMA_QOS_DEFAULT_NUM_SLS;
int           rdma_use_qos = 0;
#ifdef ENABLE_3DTORUS_SUPPORT
int           rdma_3dtorus_support = 1;
#else
int           rdma_3dtorus_support = 0;
#endif /* ENABLE_3DTORUS_SUPPORT */
int           rdma_num_sa_query_retries = RDMA_DEFAULT_NUM_SA_QUERY_RETRIES;
int           rdma_path_sl_query = 0;
/* Threshold of job size beyond which we want to use 2-cq approach */
int           rdma_iwarp_multiple_cq_threshold = RDMA_IWARP_DEFAULT_MULTIPLE_CQ_THRESHOLD;
int           rdma_iwarp_use_multiple_cq = 1;
/* Force to use rendezvous if extended sendq size exceeds this value */
int           rdma_rndv_ext_sendq_size = 5;
/* Whether coalescing of messages should be attempted */
int           rdma_use_coalesce = 1;

/* If this number of eager sends are already outstanding
 * the message can be coalesced with other messages (and
 * will not be sent until a previous message completes)
 */
int           rdma_coalesce_threshold = 6;

/* max (total) number of vbufs to allocate, after which process
 * terminates with a fatal error.
 * -1 means no limit.
 */
int rdma_vbuf_max = -1;
/* number of vbufs to allocate in a secondary region if we should
 * run out of the initial allocation.  This is re-computed (below)
 * once other parameters are known.
 */
int rdma_vbuf_secondary_pool_size = RDMA_VBUF_SECONDARY_POOL_SIZE;

/* number of vbufs to allocate initially.
 * This will be re-defined after reading the parameters below
 * to scale to the number of VIs and other factors.
 */
int rdma_vbuf_pool_size         = RDMA_VBUF_POOL_SIZE;
int rdma_prepost_depth          = RDMA_PREPOST_DEPTH;
int rdma_initial_prepost_depth  = RDMA_INITIAL_PREPOST_DEPTH;

/* allow some extra buffers for non-credited packets (eg. NOOP) */
int rdma_prepost_noop_extra     = 6;
int rdma_credit_preserve;
int rdma_initial_credits        = 0;

/* Max number of entries on the RecvQ of QPs per connection.
 * computed to be:
 * prepost_depth + rdma_prepost_rendezvous_extra + viadev_prepost_noop_extra
 * Must be within NIC MaxQpEntries limit.
 */
int rdma_rq_size;

uint32_t viadev_srq_alloc_size = 4096;
uint32_t viadev_srq_fill_size = 512;
uint32_t viadev_srq_limit = 30;
uint32_t viadev_max_r3_oust_send = 32;


/* The number of "extra" vbufs that will be posted as receives
 * on a connection in anticipation of an R3 rendezvous message.
 * The TOTAL number of VBUFs posted on a receive queue at any
 * time is rdma_prepost_depth + viadev_prepost_rendezvous_extra
 * regardless of the number of outstanding R3 sends active on
 * a connection.
 */
int rdma_prepost_rendezvous_extra   = 10;
int rdma_dynamic_credit_threshold   = 10;
int rdma_credit_notify_threshold    = 10;
int rdma_prepost_threshold          = 5;

unsigned long rdma_max_registered_pages = RDMA_MAX_REGISTERED_PAGES;
unsigned long rdma_dreg_cache_limit = 0;

/* Blocking mode progress */
int rdma_use_blocking = 0;
unsigned long rdma_blocking_spin_count_threshold = 5000;

/* The total size of each vbuf. Used to be the eager threshold, but
 * it can be smaller, so that each eager message will span over few
 * vbufs
 */
int rdma_vbuf_total_size;

/* Small message scheduling policy
 * Was earlier set to USE_FIRST, optimized for minimal QP cache misses
 * Now setting it to ROUND_ROBIN as we get better performance.
 */
int sm_scheduling = USE_FIRST;

/* This value should increase with the increase in number
 * of rails */
int striping_threshold = STRIPING_THRESHOLD;

/* Used IBoEth mode */
int use_iboeth = 0;

/* Linear update factor for HSAM */
int alpha = 0.9;
int stripe_factor = 1;
int apm_tester = 0;
int apm_count;


static inline int log_2(int np)
{
    int lgN, t;
    for (lgN = 0, t = 1; t < np; lgN++, t += t);
    return lgN;
}


/*
 * User parameters for HCA module. The supported parameters are:
 * - MV2_NUM_HCAS
 * - MV2_NUM_PORTS
 */
static void get_hca_user_parameters() {
	char *value;

	value = getenv("MV2_NUM_HCAS");
    if (value != NULL) {
    	ib_hca_num_hcas = (int)atoi(value);
        if (ib_hca_num_hcas > MAX_NUM_HCAS) {
        	ib_hca_num_hcas = MAX_NUM_HCAS;
            MPIU_Msg_printf("Warning, max hca is %d, change %s in ib_hca.h "
            		"to overide the option\n", MAX_NUM_HCAS, "MAX_NUM_HCAS");
        }
    }

    /* Get number of ports/HCA used by a process */
    value = getenv("MV2_NUM_PORTS");
    if (value != NULL) {
    	ib_hca_num_ports = (int)atoi(value);
        if (ib_hca_num_ports > MAX_NUM_PORTS) {
        	ib_hca_num_ports = MAX_NUM_PORTS;
	    MPIU_Usage_printf("Warning, max ports per hca is %d, change %s in "
		    "ibv_hca.h to overide the option\n", MAX_NUM_PORTS,
		    "MAX_NUM_PORTS");
        }
    }
}


/**
 * This function is specifically written to make sure that HSAM
 * parameters are configured correctly
 */
static int check_hsam_parameters()
{
    char *value;
    int size;

    /* Get the number of processes */
    PMI_Get_size(&size);

    /* If the number of processes is less than 64, we can afford * to
     * have more RC QPs and hence a value of 4 is chosen, for * other
     * cases, a value of 2 is chosen */

    /* (rdma_num_qp_per_port/ stripe factor) represents the number
     * of QPs which will be chosen for data transfer at a given point */

    /* If the user has not specified any value, then perform
     * this tuning */

    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {
        rdma_num_qp_per_port = atoi(value);
        if(rdma_num_qp_per_port <= 2) {
            stripe_factor = 1;
        } else {
            stripe_factor = (rdma_num_qp_per_port / 2);
        }
    } else {
        /* Speculated value */

        /* The congestion is actually never seen for less
         * than 8 nodes */
        if((size > 8) && (size < 64)) {
            rdma_num_qp_per_port = 4;
            stripe_factor = (rdma_num_qp_per_port / 2);
        } else {
            rdma_num_qp_per_port = 2;
            stripe_factor = 1;
        }
    }

    return MPI_SUCCESS;
}



#undef FUNCNAME
#define FUNCNAME rdma_get_control_params_after_hcainit
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_get_control_params_after_hcainit()
{
    MPIDI_STATE_DECL(MPID_STATE_RDMA_GET_CONTROL_PARAMS_AFTER);
    MPIDI_FUNC_ENTER(MPID_STATE_RDMA_GET_CONTROL_PARAMS_AFTER);
    char* value = NULL;
    int mpi_errno = MPI_SUCCESS;
    int my_rank = -1;

    PMI_Get_rank(&my_rank);

    int size;
    PMI_Get_size(&size);
    process_info.has_srq = (value = getenv("MV2_USE_SRQ")) != NULL ? !!atoi(value) : 1;
#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        process_info.has_srq = 1;
        MPIU_Assert (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND);
        MPIDI_CH3I_Process.cm_type = MPIDI_CH3I_CM_ON_DEMAND;
        rdma_use_coalesce = 0;
        rdma_use_blocking = 0;
    }
#endif /* _ENABLE_XRC_ */

    /* VBUF */
    if (process_info.has_srq
        && (hca_list[0].hca_type != PATH_HT)
        && (hca_list[0].hca_type != MLX_PCI_X)
        && (hca_list[0].hca_type != IBM_EHCA)
#if defined(RDMA_CM)
        && !process_info.use_iwarp_mode
#endif /* defined(RDMA_CM) */
    )
    {
        process_info.post_send = MPIDI_nem_ib_post_srq_send;
    }
    else
    {
        process_info.has_srq = 0;
        process_info.post_send = MPIDI_nem_ib_post_send;
    }

#if defined(CKPT)
    process_info.has_adaptive_fast_path = 0;
    rdma_polling_set_limit = 0;
#else /* defined(CKPT) */
    if ((value = getenv("MV2_USE_RDMA_FAST_PATH")) != NULL)
    {
        process_info.has_adaptive_fast_path = !!atoi(value);

	if (!process_info.has_adaptive_fast_path)
        {
	    rdma_polling_set_limit = 0;
        }
    }
    else
    { 
        process_info.has_adaptive_fast_path = 1;
    }
#endif /* defined(CKPT) */

    process_info.has_ring_startup = (value = getenv("MV2_USE_RING_STARTUP")) != NULL ? !!atoi(value) : 1;

#if !defined(DISABLE_PTMALLOC)
    process_info.has_lazy_mem_unregister = (value = getenv("MV2_USE_LAZY_MEM_UNREGISTER")) != NULL ? !!atoi(value) : 1;
#endif /* !defined(DISABLE_PTMALLOC) */

#if defined(CKPT)
    process_info.has_one_sided = 0;
#else /* defined(CKPT) */
    process_info.has_one_sided = (value = getenv("MV2_USE_RDMA_ONE_SIDED")) != NULL ? !!atoi(value) : 1;

#endif /* defined(CKPT) */

    if ((value = getenv("MV2_RNDV_EXT_SENDQ_SIZE")) != NULL) {
        rdma_rndv_ext_sendq_size = atoi(value);
        if (rdma_rndv_ext_sendq_size <= 1) {
            MPIU_Usage_printf("Setting MV2_RNDV_EXT_SENDQ_SIZE smaller than 1 "
                              "will severely limit the MPI bandwidth.\n");
        }
    }

    if ((value = getenv("MV2_COALESCE_THRESHOLD")) != NULL) {
        rdma_coalesce_threshold = atoi(value);
        if(rdma_coalesce_threshold < 1) {
            MPIU_Usage_printf("MV2_COALESCE_THRESHOLD must be >= 1\n");
            rdma_coalesce_threshold = 1;
        }
    }

    if ((value = getenv("MV2_USE_COALESCE")) != NULL) {
        rdma_use_coalesce = !!atoi(value);
    }

    if (hca_list[0].hca_type == MLX_CX_DDR ||
    		hca_list[0].hca_type == MLX_CX_SDR ||
    		hca_list[0].hca_type == MLX_CX_QDR) {
    	rdma_use_coalesce = 0;
    }

    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {
        rdma_use_smp = !!atoi(value);
    }

    if ((value = getenv("MV2_USE_IBOETH")) != NULL) {
        use_iboeth = !!atoi(value);
        if (1 == process_info.has_ring_startup) {
            if (0 == my_rank) {
                MPIU_Usage_printf("Ring start up cannot be used in IBoEth mode."
                                "Falling back to PMI exchange.\r\n"
                                "You can also set MV2_USE_RING_STARTUP=0.\r\n");
            }
            process_info.has_ring_startup = 0;
        }
        if (!rdma_use_smp) {
            if (0 == my_rank) {
                MPIU_Usage_printf("IBoEth mode cannot function without SHMEM."
                                "Falling back to use SHMEM.\r\n"
                                "Please do NOT set MV2_USE_SHARED_MEM=0.\r\n");
            }
            rdma_use_smp = 1;
        }
    }

#ifdef _ENABLE_XRC_
    if (!USE_XRC) {
#endif
    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        rdma_use_blocking = !!atoi(value);

        /* Automatically turn off RDMA fast path */
        if(rdma_use_blocking) {
            rdma_use_smp = 0;
            process_info.has_adaptive_fast_path = 0;
        }
    }
#ifdef _ENABLE_XRC_
    }
#endif

    if ((value = getenv("MV2_SPIN_COUNT")) != NULL) {
        rdma_blocking_spin_count_threshold = atol(value);
    }

    if ((value = getenv("MV2_RNDV_PROTOCOL")) != NULL) {
        if (strncmp(value,"R3", 2) == 0) {
            rdma_rndv_protocol = MV2_LMT_PROTOCOL_R3;
        } 
    }

#if FALSE
    if ((value = getenv("MV2_RNDV_PROTOCOL")) != NULL) {
        if (strncmp(value,"RPUT", 4) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        } else if (strncmp(value,"RGET", 4) == 0
#ifdef _ENABLE_XRC_
                && !USE_XRC
#endif
                ) {
#if defined(CKPT)
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\" or \"R3\" when checkpoint is enabled\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
#else /* defined(CKPT) */
            rdma_rndv_protocol = VAPI_PROTOCOL_RGET;
#endif /* defined(CKPT) */
        } else if (strncmp(value,"R3", 2) == 0) {
            rdma_rndv_protocol = VAPI_PROTOCOL_R3;
        } else {
#ifdef _ENABLE_XRC_
            if(!USE_XRC)
#endif
            MPIU_Usage_printf("MV2_RNDV_PROTOCOL "
                    "must be either \"RPUT\", \"RGET\", or \"R3\"\n");
            rdma_rndv_protocol = VAPI_PROTOCOL_RPUT;
        }
    }
#endif

    if ((value = getenv("MV2_R3_THRESHOLD")) != NULL) {
        rdma_r3_threshold = atoi(value);
        if(rdma_r3_threshold < 0) {
            rdma_r3_threshold = 0;
        }
    }

    if ((value = getenv("MV2_R3_NOCACHE_THRESHOLD")) != NULL) {
        rdma_r3_threshold_nocache = atoi(value);
        if(rdma_r3_threshold_nocache < 0) {
            rdma_r3_threshold_nocache = 0;
        }
    }

    if ((value = getenv("MV2_MAX_R3_PENDING_DATA")) !=NULL) {
        rdma_max_r3_pending_data = atoi(value);
        if (rdma_max_r3_pending_data < 0) {
            rdma_max_r3_pending_data = 0;
        }
    }

#if defined(RDMA_CM)
    if (process_info.use_rdma_cm_on_demand){
	    process_info.use_iwarp_mode = 1;
    }
#endif /* defined(RDMA_CM) */

/* fn_exit: */
    MPIDI_FUNC_EXIT(MPID_STATE_RDMA_GET_CONTROL_PARAMS_AFTER);
    return mpi_errno;

/* fn_fail:
    goto fn_exit; */
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_set_default_params
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 *  Look at rdma_set_default_parameters() in
 * "mvapich2/trunk/src/mpid/ch3/channels/mrail/src/gen2/ibv_param.c"
 */
int MPID_nem_ib_set_default_params()
{
    int mpi_errno   = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_SET_DEFAULT_PARAMS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_SET_DEFAULT_PARAMS);

    rdma_fp_buffer_size = RDMA_FP_DEFAULT_BUF_SIZE;
    mv2_arch_hca_type arch_hca_type = mv2_get_arch_hca_type ( hca_list[0].ib_dev );

    switch( arch_hca_type  ) {

        case MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 5 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0; 
            break;

        case MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 5 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 8 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0; 
            break;

        case MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 5 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 4 * 1024;
            rdma_get_fallback_threshold  = 0; 
            break;

        case MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 9 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 128;
            rdma_get_fallback_threshold  = 0;
            break;

        case MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_DDR:
            rdma_vbuf_total_size = 17 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 9 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_QDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 5 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_CX_DDR:
            rdma_vbuf_total_size = 16 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_fp_buffer_size = 9 * 1024;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 0;
            break;

        CASE_MV2_ANY_ARCH_WITH_MLX_PCI_X:
        CASE_MV2_ANY_ARCH_WITH_IBM_EHCA:
            rdma_vbuf_total_size     = 12*1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T3:
        CASE_MV2_ANY_ARCH_WITH_CHELSIO_T4:
            rdma_vbuf_total_size     = 9 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        CASE_MV2_ANY_ARCH_WITH_INTEL_NE020:
            rdma_vbuf_total_size     = 9 * 1024 + EAGER_THRESHOLD_ADJUST;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc           = 4 * 1024;
            rdma_put_fallback_threshold  = 8 * 1024;
            rdma_get_fallback_threshold  = 394 * 1024;
            break;

        default:
            rdma_vbuf_total_size     = 12 * 1024;
            rdma_fp_buffer_size      = VBUF_BUFFER_SIZE;
            rdma_iba_eager_threshold = VBUF_BUFFER_SIZE;
            rdma_eagersize_1sc               = 4 * 1024;
            rdma_put_fallback_threshold      = 8 * 1024;
            rdma_get_fallback_threshold      = 256 * 1024;
            break;
    }

    num_rdma_buffer          = 16;
    if (hca_list[0].hca_type == PATH_HT) {
        rdma_default_qp_ous_rd_atom = 1;
    } else {
        rdma_default_qp_ous_rd_atom = 4;
    }

    if (hca_list[0].hca_type == IBM_EHCA) {
        rdma_max_inline_size = -1;
    } else if ((hca_list[0].hca_type == CHELSIO_T3) ||
            (hca_list[0].hca_type == CHELSIO_T4)) {
        rdma_max_inline_size = 64;
    } else {
        rdma_max_inline_size = 128 + INLINE_THRESHOLD_ADJUST;
    }

    if (hca_list[0].hca_type == MLX_PCI_EX_DDR) {
        rdma_default_mtu = IBV_MTU_2048;
    } else if(hca_list[0].hca_type == MLX_CX_QDR) {
        rdma_default_mtu = IBV_MTU_2048;
    } else {
        rdma_default_mtu = IBV_MTU_1024;
    }

    if ((hca_list[0].hca_type == CHELSIO_T3) ||
            (hca_list[0].hca_type == CHELSIO_T4)) {
        /* Trac #423 */
        struct ibv_device_attr dev_attr;
        int mpi_errno = MPI_SUCCESS;

        /*quering device for cq depth*/
        mpi_errno = ibv_query_device(hca_list[0].nic_context, &dev_attr);

        if(!mpi_errno) {
            if(dev_attr.max_cqe < rdma_default_max_cq_size) {
                rdma_default_max_cq_size = dev_attr.max_cqe;
            } 
        } else {
            rdma_default_max_cq_size = RDMA_DEFAULT_IWARP_CQ_SIZE;
        } 

        rdma_prepost_noop_extra = 8;
    }

    if (process_info.has_srq) {
        rdma_credit_preserve = 100;
    } else {
        rdma_credit_preserve = 3;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_SET_DEFAULT_PARAMS);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_get_user_params
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

/**
 *  Look at rdma_get_user_parameters() & rdma_get_control_parameters() in
 * "mvapich2/trunk/src/mpid/ch3/channels/mrail/src/gen2/ibv_param.c"
 *
 */
int MPID_nem_ib_get_user_params()
{
    int mpi_errno   = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_GET_USER_PARAMS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_GET_USER_PARAMS);


    char *value;
    int pg_size;
    PMI_Get_size(&pg_size);


    if ((value = getenv("MV2_DEFAULT_MTU")) != NULL) {

        if (strncmp(value,"IBV_MTU_256",11)==0) {
            rdma_default_mtu = IBV_MTU_256;
        } else if (strncmp(value,"IBV_MTU_512",11)==0) {
            rdma_default_mtu = IBV_MTU_512;
        } else if (strncmp(value,"IBV_MTU_1024",12)==0) {
            rdma_default_mtu = IBV_MTU_1024;
        } else if (strncmp(value,"IBV_MTU_2048",12)==0) {
            rdma_default_mtu = IBV_MTU_2048;
        } else if (strncmp(value,"IBV_MTU_4096",12)==0) {
            rdma_default_mtu = IBV_MTU_4096;
        } else {
            rdma_default_mtu = IBV_MTU_1024;
        }
    }

    /* Get number of qps/port used by a process */
    if ((value = getenv("MV2_NUM_QP_PER_PORT")) != NULL) {

        rdma_num_qp_per_port = (int)atoi(value);

        if (rdma_num_qp_per_port > MAX_NUM_QP_PER_PORT) {
            rdma_num_qp_per_port = MAX_NUM_QP_PER_PORT;
            MPIU_Usage_printf("Warning, max qps per port is %d, change %s in "
		    "ibv_param.h to overide the option\n", MAX_NUM_QP_PER_PORT,
		    "MAX_NUM_QP_PER_PORT");
        }
    }

    if ((value = getenv("MV2_PIN_POOL_SIZE")) != NULL) {
        rdma_pin_pool_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_MAX_INLINE_SIZE")) != NULL) {
        rdma_max_inline_size = (int)atoi(value);
    } 
    /* else if(num_proc > 256) {
        rdma_max_inline_size = 0;
    } */

    if ((value = getenv("MV2_DEFAULT_MAX_CQ_SIZE")) != NULL) {
        rdma_default_max_cq_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_READ_RESERVE")) != NULL) {
        rdma_read_reserve = (int)atoi(value);
    }
    if ((value = getenv("MV2_NUM_RDMA_BUFFER")) != NULL) {
        num_rdma_buffer = (int)atoi(value);
    }
    if ((value = getenv("MV2_POLLING_SET_THRESHOLD")) != NULL
        && process_info.has_adaptive_fast_path) {
        rdma_polling_set_threshold = atoi(value);
    }
    if ((value = getenv("MV2_RDMA_EAGER_LIMIT")) != NULL
        && process_info.has_adaptive_fast_path) {
        rdma_eager_limit = atoi(value);
	if (rdma_eager_limit < 0)
	    rdma_eager_limit = 0;
    }
    if ((value = getenv("MV2_POLLING_SET_LIMIT")) != NULL
        && process_info.has_adaptive_fast_path) {
        rdma_polling_set_limit = atoi(value);
        if (rdma_polling_set_limit == -1) {
            rdma_polling_set_limit = log_2(pg_size);
        }
    } else if (process_info.has_adaptive_fast_path) {
        rdma_polling_set_limit = RDMA_DEFAULT_POLLING_SET_LIMIT;
    }
    if ((value = getenv("MV2_VBUF_TOTAL_SIZE")) != NULL) {
            rdma_vbuf_total_size = user_val_to_bytes(value,"MV2_VBUF_TOTAL_SIZE"); 
            if (rdma_vbuf_total_size <= 2 * sizeof(int))
              rdma_vbuf_total_size = 2 * sizeof(int);
    }

    if ((value = getenv("MV2_RDMA_FAST_PATH_BUF_SIZE")) != NULL
        && process_info.has_adaptive_fast_path) {
        rdma_fp_buffer_size = atoi(value);
    }

    /* We have read the value of the rendezvous threshold, and the number of
     * rails used for communication, increase the striping threshold
     * accordingly */

    /* Messages in between will use the rendezvous protocol, however will
     * not be striped.
     * Call get_hca_parameters() before next line to set ib_hca_num_ports
     * and ib_hca_num_hcas to proper values */

    striping_threshold = rdma_vbuf_total_size * ib_hca_num_ports *
        rdma_num_qp_per_port * ib_hca_num_hcas;

    if ((value = getenv("MV2_SRQ_MAX_SIZE")) != NULL) {
        viadev_srq_alloc_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("MV2_SRQ_SIZE")) != NULL) {
        viadev_srq_fill_size = (uint32_t) atoi(value);
    }

    if ((value = getenv("MV2_SRQ_LIMIT")) != NULL) {
        viadev_srq_limit = (uint32_t) atoi(value);

        if(viadev_srq_limit > viadev_srq_fill_size) {
	    MPIU_Usage_printf("SRQ limit shouldn't be greater than SRQ size\n");
        }
    }

    if (process_info.has_srq) {
        rdma_credit_preserve = (viadev_srq_fill_size > 200) ?
            (viadev_srq_fill_size - 100) : (viadev_srq_fill_size / 2);
    }

    if ((value = getenv("MV2_IBA_EAGER_THRESHOLD")) != NULL) {
        rdma_iba_eager_threshold =  user_val_to_bytes(value,"MV2_IBA_EAGER_THRESHOLD");
    }

    if ((value = getenv("MV2_STRIPING_THRESHOLD")) != NULL) {
        striping_threshold = atoi(value);
        if (striping_threshold <= 0) {
            /* Invalid value - set to computed value */
            striping_threshold = rdma_vbuf_total_size * ib_hca_num_ports *
                                    rdma_num_qp_per_port * ib_hca_num_hcas;
        }
        if (striping_threshold < rdma_iba_eager_threshold) {
            /* checking to make sure that the striping threshold is not less
             * than the RNDV threshold since it won't work as expected.
             */
            striping_threshold = rdma_iba_eager_threshold;
        }
    }

    if ((value = getenv("MV2_INTEGER_POOL_SIZE")) != NULL) {
        rdma_integer_pool_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PUT_GET_LIST_SIZE")) != NULL) {
        rdma_default_put_get_list_size = (int)atoi(value);
    }
    if ((value = getenv("MV2_EAGERSIZE_1SC")) != NULL) {
        rdma_eagersize_1sc = (int)atoi(value);
    }
    if ((value = getenv("MV2_PUT_FALLBACK_THRESHOLD")) != NULL) {
        rdma_put_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("MV2_GET_FALLBACK_THRESHOLD")) != NULL) {
        rdma_get_fallback_threshold = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PORT")) != NULL) {
        rdma_default_port = (int)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_QP_OUS_RD_ATOM")) != NULL) {
        rdma_default_qp_ous_rd_atom = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_RDMA_DST_OPS")) != NULL) {
        rdma_default_max_rdma_dst_ops = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_PSN")) != NULL) {
        rdma_default_psn = (uint32_t)atoi(value);
    }

    if ((value = getenv("MV2_DEFAULT_PKEY")) != NULL) {
        rdma_default_pkey = (uint16_t)strtol(value, (char **) NULL,0) & PKEY_MASK;
    }

    if ((value = getenv("MV2_DEFAULT_MIN_RNR_TIMER")) != NULL) {
        rdma_default_min_rnr_timer = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_SERVICE_LEVEL")) != NULL) {
        rdma_default_service_level = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_TIME_OUT")) != NULL) {
        rdma_default_time_out = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_STATIC_RATE")) != NULL) {
        rdma_default_static_rate = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_SRC_PATH_BITS")) != NULL) {
        rdma_default_src_path_bits = (uint8_t)atoi(value);
    }
    if ((value = getenv("MV2_DEFAULT_RETRY_COUNT")) != NULL) {
        rdma_default_retry_count = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_RNR_RETRY")) != NULL) {
        rdma_default_rnr_retry = (uint8_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SG_LIST")) != NULL) {
        rdma_default_max_sg_list = (uint32_t)atol(value);
    }
    if ((value = getenv("MV2_DEFAULT_MAX_SEND_WQE")) != NULL) {
        rdma_default_max_send_wqe = atol(value);
    } 
    /* else if(num_proc > 256) {
        rdma_default_max_send_wqe = 16;
    } */
    if ((value = getenv("MV2_DEFAULT_MAX_RECV_WQE")) != NULL) {
        rdma_default_max_recv_wqe = atol(value);
    }
    if ((value = getenv("MV2_NDREG_ENTRIES")) != NULL) {
        rdma_ndreg_entries = (unsigned int)atoi(value);
    }
    if ((value = getenv("MV2_VBUF_MAX")) != NULL) {
        rdma_vbuf_max = atoi(value);
    }
    if ((value = getenv("MV2_INITIAL_PREPOST_DEPTH")) != NULL) {
        rdma_initial_prepost_depth = atoi(value);
    }
    if ((value = getenv("MV2_PREPOST_DEPTH")) != NULL) {
        rdma_prepost_depth = atoi(value);
    }
    if ((value = getenv("MV2_MAX_REGISTERED_PAGES")) != NULL) {
        rdma_max_registered_pages = atol(value);
    }
    if ((value = getenv("MV2_VBUF_POOL_SIZE")) != NULL) {
        rdma_vbuf_pool_size = atoi(value);
    }
    if ((value = getenv("MV2_DREG_CACHE_LIMIT")) != NULL) {
        rdma_dreg_cache_limit = atol(value);
    }
    if (rdma_vbuf_pool_size <= 10) {
        rdma_vbuf_pool_size = 10;
        MPIU_Usage_printf("Warning! Too small vbuf pool size (%d).  "
		"Reset to %d\n", rdma_vbuf_pool_size, 10);
    }
    if ((value = getenv("MV2_VBUF_SECONDARY_POOL_SIZE")) != NULL) {
        rdma_vbuf_secondary_pool_size = atoi(value);
    }
    if (rdma_vbuf_secondary_pool_size <= 0) {
        rdma_vbuf_secondary_pool_size = 1;
        MPIU_Usage_printf("Warning! Too small secondary vbuf pool size (%d).  "
                "Reset to %d\n", rdma_vbuf_secondary_pool_size, 1);
    }
    if (rdma_initial_prepost_depth <= rdma_prepost_noop_extra) {
        rdma_initial_credits = rdma_initial_prepost_depth;
    } else {
        rdma_initial_credits =
            rdma_initial_prepost_depth - rdma_prepost_noop_extra;
    }

    rdma_rq_size = rdma_prepost_depth +
        rdma_prepost_rendezvous_extra + rdma_prepost_noop_extra;

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_GET_USER_PARAMS);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_get_control_params
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

/**
 *  Look at rdma_get_control_parameters() in
 * "mvapich2/trunk/src/mpid/ch3/channels/mrail/src/gen2/ibv_param.c"
 *
 */
int MPID_nem_ib_get_control_params()
{
    int mpi_errno   = 0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_GET_CONTROL_PARAMS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_GET_CONTROL_PARAMS);

    char* value = NULL;
    int my_rank = -1;

    PMI_Get_rank(&my_rank);
    get_hca_user_parameters();

    /* Start HSAM Parameters */
    if ((value = getenv("MV2_USE_HSAM")) != NULL) {
        process_info.has_hsam = (int)atoi(value);
        if(process_info.has_hsam) {
            check_hsam_parameters();
        }
    } else {
        /* By default disable the HSAM, due to problem with
         * multi-pathing with current version of opensm and
         * up/down */
        process_info.has_hsam = 0;
    }

#ifdef ENABLE_QOS_SUPPORT
    if ((value = getenv("MV2_USE_QOS")) != NULL) {
        rdma_use_qos = !!atoi(value);
    }

    if ((value = getenv("MV2_3DTORUS_SUPPORT")) != NULL) {
        rdma_3dtorus_support = !!atoi(value);
    }

    if ((value = getenv("MV2_PATH_SL_QUERY")) != NULL) {
        rdma_path_sl_query = !!atoi(value);
    }

    if ((value = getenv("MV2_NUM_SLS")) != NULL) {
        rdma_qos_num_sls = atoi(value);
        if (rdma_qos_num_sls <= 0 && rdma_qos_num_sls > RDMA_QOS_MAX_NUM_SLS) {
            rdma_qos_num_sls = RDMA_QOS_DEFAULT_NUM_SLS;
        }
        /* User asked us to use multiple SL's without enabling QoS globally. */
        if (rdma_use_qos == 0) {
            rdma_use_qos = 1;
        }
    }
#endif /* ENABLE_QOS_SUPPORT */

    if ((value = getenv("MV2_NUM_SA_QUERY_RETRIES")) != NULL) {
        rdma_num_sa_query_retries = !!atoi(value);
        if (rdma_num_sa_query_retries < RDMA_DEFAULT_NUM_SA_QUERY_RETRIES) {
            rdma_num_sa_query_retries = RDMA_DEFAULT_NUM_SA_QUERY_RETRIES;
        }
    }

    process_info.has_apm = (value = getenv("MV2_USE_APM")) != NULL ? (int) atoi(value) : 0;
    apm_tester = (value = getenv("MV2_USE_APM_TEST")) != NULL ? (int) atoi(value) : 0;
    apm_count = (value = getenv("MV2_APM_COUNT")) != NULL ? (int) atoi(value) : APM_COUNT;

    /* Scheduling Parameters */
    if ( (value = getenv("MV2_SM_SCHEDULING")) != NULL) {
        if (!strcmp(value, "USE_FIRST")) {
            sm_scheduling = USE_FIRST;
        } else if (!strcmp(value, "ROUND_ROBIN")) {
            sm_scheduling = ROUND_ROBIN;
        } else if (!strcmp(value, "PROCESS_BINDING")) {
            sm_scheduling = PROCESS_BINDING;
        } else {
            MPIU_Usage_printf("Invalid small message scheduling\n");
        }
    }

    /* End : HSAM Parameters */

#if defined(RDMA_CM)
    if ((value = getenv("MV2_USE_IWARP_MODE")) != NULL) {
            process_info.use_rdma_cm = !!atoi(value);
            process_info.use_iwarp_mode = !!atoi(value);
    }

    if (!process_info.use_rdma_cm){
        if ((value = getenv("MV2_USE_RDMA_CM")) != NULL) {
            process_info.use_rdma_cm = !!atoi(value);
        }
        else {
            process_info.use_rdma_cm = 0;
            process_info.use_iwarp_mode = 0;
        }
    }

    if ((value = getenv("MV2_SUPPORT_DPM")) && !!atoi(value)) {
        process_info.use_rdma_cm = 0;
        process_info.use_iwarp_mode = 0;
    }

    if (process_info.use_rdma_cm) {
        int rank = ERROR;
        int pg_size = ERROR;
        int threshold = ERROR;

        if (process_info.use_iwarp_mode) {
            /* Trac #423 */
            threshold = MPIDI_CH3I_CM_DEFAULT_IWARP_ON_DEMAND_THRESHOLD;
        } else {
            threshold = MPIDI_CH3I_CM_DEFAULT_ON_DEMAND_THRESHOLD;
        }

        PMI_Get_size(&pg_size);
        PMI_Get_rank(&rank);

        if ((value = getenv("MV2_ON_DEMAND_THRESHOLD")) != NULL){
            threshold = atoi(value);
        }
        if (pg_size > threshold) {
            process_info.use_rdma_cm_on_demand = 1;
        }
    }
#endif
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_GET_CONTROL_PARAMS);
    return mpi_errno;
}
                                                       
