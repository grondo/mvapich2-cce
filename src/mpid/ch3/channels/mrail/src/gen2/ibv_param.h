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

#ifndef _IBV_PARAM_H
#define _IBV_PARAM_H

#include <infiniband/verbs.h>
#include "debug_utils.h"

/* Support multiple QPs/port, multiple ports, multiple HCAs and combinations */
extern int                  rdma_num_hcas;
extern int                  rdma_num_req_hcas;
extern int                  rdma_num_ports;
extern int                  rdma_num_qp_per_port;
extern int                  rdma_num_rails;

extern unsigned long        rdma_default_max_cq_size;
extern int                  rdma_default_port;
extern int                  rdma_default_max_send_wqe;
extern int                  rdma_default_max_recv_wqe;
extern uint32_t             rdma_default_max_sg_list;
extern uint16_t             rdma_default_pkey_ix;
extern uint16_t             rdma_default_pkey;
extern uint8_t              rdma_default_qp_ous_rd_atom;
extern uint8_t              rdma_default_max_rdma_dst_ops;
extern enum ibv_mtu         rdma_default_mtu;
extern uint32_t             rdma_default_psn;
extern uint8_t              rdma_default_min_rnr_timer;
extern uint8_t              rdma_default_service_level;
extern uint8_t              rdma_default_static_rate;
extern uint8_t              rdma_default_src_path_bits;
extern uint8_t              rdma_default_time_out;
extern uint8_t              rdma_default_retry_count;
extern uint8_t              rdma_default_rnr_retry;
extern int                  rdma_default_put_get_list_size;
extern int                  rdma_read_reserve;
extern float                rdma_credit_update_threshold;   
extern int                  num_rdma_buffer;
extern int                  rdma_iba_eager_threshold;
extern unsigned int         rdma_ndreg_entries;
extern int                  rdma_vbuf_max;
extern int                  rdma_vbuf_pool_size;
extern int                  rdma_vbuf_secondary_pool_size;
extern int                  rdma_initial_prepost_depth;
extern int                  rdma_prepost_depth;
extern int                  rdma_prepost_threshold;
extern int                  rdma_prepost_noop_extra;
extern int                  rdma_initial_credits;
extern int                  rdma_prepost_rendezvous_extra;
extern int                  rdma_dynamic_credit_threshold;
extern int                  rdma_credit_notify_threshold;
extern int                  rdma_credit_preserve;
extern int                  rdma_rq_size;
extern unsigned long        rdma_dreg_cache_limit;
extern int                  rdma_rndv_protocol;
extern int                  rdma_r3_threshold;
extern int                  rdma_r3_threshold_nocache;
extern int                  rdma_max_r3_pending_data;
extern int                  rdma_vbuf_total_size;
extern int                  rdma_max_inline_size;
extern int                  rdma_local_id;
extern int                  rdma_num_local_procs; 

extern uint32_t             viadev_srq_alloc_size;
extern uint32_t             viadev_srq_fill_size;
extern uint32_t             viadev_srq_limit;
extern uint32_t             viadev_max_r3_oust_send;

extern int                  rdma_polling_set_threshold;
extern int                  rdma_polling_set_limit;
extern int                  rdma_fp_buffer_size;
extern int                  rdma_fp_sendconn_accepted;
extern int                  rdma_pending_conn_request;
extern int                  rdma_eager_limit;
extern int                  rdma_rndv_ext_sendq_size;
extern int                  rdma_global_ext_sendq_size;
extern int                  rdma_num_extra_polls;

extern int                  rdma_pin_pool_size;
extern int                  rdma_put_fallback_threshold;
extern int                  rdma_get_fallback_threshold; 
extern int                  rdma_integer_pool_size;
extern int                  rdma_iba_eager_threshold;
extern long                 rdma_eagersize_1sc;
extern int                  rdma_qos_num_sls;
extern int                  rdma_use_qos;
extern int                  rdma_3dtorus_support;
extern int                  rdma_path_sl_query;
extern int                  rdma_num_sa_query_retries;
extern int                  rdma_multirail_usage_policy;
extern int                  rdma_small_msg_rail_sharing_policy;
extern int                  rdma_med_msg_rail_sharing_policy;
extern int                  rdma_med_msg_rail_sharing_threshold;
extern int                  rdma_large_msg_rail_sharing_threshold;


extern int                  mv2_on_demand_ud_info_exchange;
/* HSAM Definitions */

extern  int                 striping_threshold;
extern  int                 rdma_rail_sharing_policy;
extern  int                 alpha;
extern  int                 stripe_factor;
extern  int                 apm_tester;
extern  int                 apm_count;

extern int                  rdma_coalesce_threshold;
extern int                  rdma_use_coalesce;

extern int                  rdma_use_blocking;
extern unsigned long        rdma_blocking_spin_count_threshold;
extern unsigned long        rdma_polling_spin_count_threshold;
extern int                  use_thread_yield; 
extern int                  spins_before_lock; 
extern int                  rdma_use_smp;
extern int                  use_iboeth;
extern int                  rdma_iwarp_multiple_cq_threshold;
extern int                  rdma_iwarp_use_multiple_cq;
extern int                  using_mpirun_rsh;

extern int                  use_hwloc_cpu_binding; 
extern int                  max_rdma_connect_attempts;
extern int                  rdma_cm_connect_retry_interval;
extern int                  rdma_num_rails_per_hca;
extern int                  rdma_process_binding_rail_offset;

/* Use of LIMIC of RMA Communication */
extern int                  limic_put_threshold;
extern int                  limic_get_threshold;

extern int                  rdma_enable_hugepage;
#ifdef _ENABLE_UD_
extern uint8_t              rdma_enable_hybrid;
extern uint8_t              rdma_use_ud_zcopy;
extern uint32_t             rdma_hybrid_enable_threshold;
extern uint32_t             rdma_default_max_ud_send_wqe;
extern uint32_t             rdma_default_max_ud_recv_wqe;
extern uint32_t             rdma_default_ud_sendwin_size;
extern uint32_t             rdma_default_ud_recvwin_size;
extern long                 rdma_ud_progress_timeout;
extern long                 rdma_ud_retry_timeout;
extern long                 rdma_ud_max_retry_timeout;
extern long                 rdma_ud_last_check;
extern uint16_t             rdma_ud_max_retry_count;
extern uint16_t             rdma_ud_progress_spin;
extern uint16_t             rdma_ud_max_ack_pending;
extern uint16_t             rdma_default_ud_mtu;
extern uint16_t             rdma_ud_num_rndv_qps;
extern uint32_t             rdma_ud_num_msg_limit;
extern uint32_t             rdma_ud_vbuf_pool_size;
extern uint32_t             rdma_ud_zcopy_threshold;
extern uint32_t             rdma_ud_zcopy_rq_size;
extern uint16_t             rdma_hybrid_max_rc_conn;
extern uint16_t             rdma_hybrid_pending_rc_conn;
#ifdef _MV2_UD_DROP_PACKET_RATE_
extern uint32_t             ud_drop_packet_rate;
#endif
#endif /* _ENABLE_UD_ */

extern int                  rdma_default_async_thread_stack_size;

#define PKEY_MASK 0x7fff /* the last bit is reserved */
#define RDMA_PIN_POOL_SIZE              (2*1024*1024)
#define RDMA_DEFAULT_MAX_CQ_SIZE        (40000)
#define RDMA_DEFAULT_IWARP_CQ_SIZE      (8192)
#define RDMA_DEFAULT_PORT               (-1)
#define RDMA_DEFAULT_MAX_PORTS          (2)
#define RDMA_DEFAULT_MAX_SEND_WQE       (64)
#define RDMA_DEFAULT_MAX_RECV_WQE       (128)
#define RDMA_DEFAULT_MAX_UD_SEND_WQE    (2048)
#define RDMA_DEFAULT_MAX_UD_RECV_WQE    (4096)
#define RDMA_UD_NUM_MSG_LIMIT           (4096)
#define RDMA_READ_RESERVE               (10)
#define RDMA_DEFAULT_MAX_SG_LIST        (1)
#define RDMA_DEFAULT_PKEY_IX            (0)
#define RDMA_DEFAULT_PKEY               (0x0)
#define RDMA_DEFAULT_MAX_RDMA_DST_OPS   (4)
#define RDMA_DEFAULT_PSN                (0)
#define RDMA_DEFAULT_MIN_RNR_TIMER      (12)
#define RDMA_DEFAULT_SERVICE_LEVEL      (0)
#define RDMA_DEFAULT_STATIC_RATE        (0)
#define RDMA_DEFAULT_SRC_PATH_BITS      (0)
#define RDMA_DEFAULT_TIME_OUT          (20)
#define RDMA_DEFAULT_RETRY_COUNT        (7)  
#define RDMA_DEFAULT_RNR_RETRY          (7)
#define RDMA_DEFAULT_PUT_GET_LIST_SIZE  (200)
#define RDMA_INTEGER_POOL_SIZE          (1024)
#define RDMA_IBA_NULL_HCA               "nohca"
#define RDMA_DEFAULT_POLLING_SET_LIMIT  (64)
#define RDMA_FP_DEFAULT_BUF_SIZE        (4096)
#define MAX_NUM_HCAS                    (4)
#define MAX_NUM_PORTS                   (2)
#define MAX_NUM_QP_PER_PORT             (4)
#define RDMA_QOS_MAX_NUM_SLS	        (15)
#define RDMA_QOS_DEFAULT_NUM_SLS	    (8)
#define RDMA_DEFAULT_NUM_SA_QUERY_RETRIES   (20)
#define RDMA_DEFAULT_MED_MSG_RAIL_SHARING_THRESHOLD (2048)
#define RDMA_DEFAULT_LARGE_MSG_RAIL_SHARING_THRESHOLD (16384)

/* This is a overprovision of resource, do not use in critical structures */
#define MAX_NUM_SUBRAILS                (MAX_NUM_HCAS*  \
                                         MAX_NUM_PORTS* \
                                         MAX_NUM_QP_PER_PORT)

#define RDMA_NDREG_ENTRIES              (1100)
#define RDMA_VBUF_POOL_SIZE             (2048)
#define RDMA_UD_VBUF_POOL_SIZE          (8192)
#define RDMA_MIN_VBUF_POOL_SIZE         (512)
#define RDMA_VBUF_SECONDARY_POOL_SIZE   (256)
#define RDMA_PREPOST_DEPTH              (64)
#define RDMA_INITIAL_PREPOST_DEPTH      (10)
#define RDMA_LOW_WQE_THRESHOLD          (10)
#define RDMA_MAX_RDMA_SIZE              (4194304)
#define DEFAULT_RDMA_CONNECT_ATTEMPTS   (10)
#define RDMA_DEFAULT_CONNECT_INTERVAL   (100)

#define RDMA_IWARP_DEFAULT_MULTIPLE_CQ_THRESHOLD  (32)
#define RDMA_DEFAULT_ASYNC_THREAD_STACK_SIZE  (1<<20)

/* Inline not supported for PPC */
#define HOSTNAME_LEN                    (255)
#define RDMA_MAX_REGISTERED_PAGES       (0)

/* #define MIN(a,b) ((a)<(b)?(a):(b)) */

#define NUM_BOOTSTRAP_BARRIERS  2

/* Statistically sending a stripe below this may not lead
 * to benefit */                               
#define STRIPING_THRESHOLD              8 * 1024
extern char                 rdma_iba_hcas[MAX_NUM_HCAS][32];
                               
typedef enum _mv2_iba_network_classes {
    MV2_NETWORK_CLASS_UNKNOWN = 0,
    MV2_NETWORK_CLASS_IB = 1,
    MV2_NETWORK_CLASS_IWARP,
} mv2_iba_network_classes;

/* Below ROUND_ROBIN refers to the rails where the rails are alternately
 * given to any process asking for it. Where as FIXED_MAPPING refers
 * to a scheduling policy where processes are bound to rails in a round
 * robin manner. So once a process is bound to a rail it will use only 
 * that rail to send out messages */

typedef enum _mv2_multirail_policies {
    MV2_MRAIL_BINDING = 0,
    MV2_MRAIL_SHARING,
} mv2_multirail_policies;

typedef enum _mv2_rail_sharing_policies {
    ROUND_ROBIN = 0,
    USE_FIRST,
    EVEN_STRIPING,
    ADAPTIVE_STRIPING,
    FIXED_MAPPING,
    PARTIAL_ADAPTIVE,
    BEST_ADAPTIVE,
} mv2_rail_sharing_policies;

/* This is to allow users to specify rail mapping at run time */
extern int                  mrail_use_default_mapping;
extern int                  mrail_user_defined_p2r_mapping;
extern char*                mrail_p2r_string;
extern int                  mrail_p2r_length;

#define APM_COUNT                       2

#define DYNAMIC_TOTAL_WEIGHT            (3* 1024)

#define CHELSIO_RNIC                    "cxgb"
#define INTEL_NE020_RNIC                "nes0"

/* MV2_POLLING_LEVEL
Level 1 : Exit on finding a message on any channel 
Level 2 : Exit on finding a message on RDMA_FP or SMP channel.
          Continue on ibv_poll_cq success.
Level 3 : Exit on finding a message on RDMA_FP channel.
          Continue polling on SMP and ibv_poll_cq channels
          until no more messages.
Level 4 : Exit only after processing all the messages on 
          all the channels
*/
typedef enum mv2_polling_level {
    MV2_POLLING_LEVEL_1 = 1, 
    MV2_POLLING_LEVEL_2, 
    MV2_POLLING_LEVEL_3, 
    MV2_POLLING_LEVEL_4,
} mv2_polling_level; 
                               
extern mv2_polling_level    rdma_polling_level;
#endif /* _RDMA_PARAM_H */
