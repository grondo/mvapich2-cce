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

#ifndef RDMA_IMPL_H
#define RDMA_IMPL_H

#include "mpidi_ch3_impl.h"
#include "mpidi_ch3_rdma_pre.h"
#include "pmi.h"

#include <infiniband/verbs.h>
#include "ibv_param.h"
#include "mv2_arch_hca_detect.h"
#include "rdma_3dtorus.h"

#ifdef RDMA_CM
#include <rdma/rdma_cma.h>
#include <semaphore.h>
#include <pthread.h>
#endif /* RDMA_CM */

#include <errno.h>

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    MPIU_Error_printf("[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    MPIU_Error_printf(args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#ifndef ERROR
#define ERROR   -1
#endif

#define ERROR_EPSILON (0.00000001)

/* cluster size */
enum {VERY_SMALL_CLUSTER, SMALL_CLUSTER, MEDIUM_CLUSTER, LARGE_CLUSTER};

typedef struct MPIDI_CH3I_RDMA_Process_t {
    /* keep all rdma implementation specific global variable in a
       structure like this to avoid name collisions */
    mv2_hca_type                 hca_type;
    mv2_arch_type                arch_type;
    mv2_arch_hca_type            arch_hca_type;
    int                         cluster_size;
    uint8_t                     has_srq;
    uint8_t                     has_hsam;
    uint8_t                     has_apm;
    uint8_t                     has_adaptive_fast_path;
    uint8_t                     has_ring_startup;
    uint8_t                     has_lazy_mem_unregister;
    uint8_t                     has_one_sided;
    uint8_t                     has_limic_one_sided;
    uint8_t                     has_shm_one_sided;
    int                         maxtransfersize;
    int                         global_used_send_cq;
    int                         global_used_recv_cq;
    uint8_t                     lmc;

    struct ibv_context          *nic_context[MAX_NUM_HCAS];
    struct ibv_device           *ib_dev[MAX_NUM_HCAS];
    struct ibv_pd               *ptag[MAX_NUM_HCAS];
    struct ibv_cq               *cq_hndl[MAX_NUM_HCAS];
    struct ibv_cq               *send_cq_hndl[MAX_NUM_HCAS];
    struct ibv_cq               *recv_cq_hndl[MAX_NUM_HCAS];
    struct ibv_comp_channel     *comp_channel[MAX_NUM_HCAS];

    /*record lid and port information for connection establish later*/
    int ports[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int lids[MAX_NUM_HCAS][MAX_NUM_PORTS];
    union ibv_gid gids[MAX_NUM_HCAS][MAX_NUM_PORTS];

    int    (*post_send)(MPIDI_VC_t * vc, vbuf * v, int rail);

    uint32_t                    pending_r3_sends[MAX_NUM_SUBRAILS];
    struct ibv_srq              *srq_hndl[MAX_NUM_HCAS];
    pthread_spinlock_t          srq_post_spin_lock;
    pthread_mutex_t             srq_post_mutex_lock[MAX_NUM_HCAS];
    pthread_mutex_t             async_mutex_lock[MAX_NUM_HCAS];
    pthread_cond_t              srq_post_cond[MAX_NUM_HCAS];
    uint32_t                    srq_zero_post_counter[MAX_NUM_HCAS];
    pthread_t                   async_thread[MAX_NUM_HCAS];
    uint32_t                    posted_bufs[MAX_NUM_HCAS];
    int                         is_finalizing;

    /* data structure for ring based startup */
    struct ibv_context          *boot_context;
    struct ibv_device           *boot_device;
    struct ibv_pd               *boot_ptag;
    struct ibv_cq               *boot_cq_hndl;
    struct ibv_qp               *boot_qp_hndl[2];
    int                         boot_tb[2][2];

    int                         polling_group_size;
    MPIDI_VC_t                  **polling_set;

#if defined(RDMA_CM)
    pthread_t                   cmthread;
    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *cm_listen_id;
    sem_t                       rdma_cm;
    uint8_t                     use_rdma_cm;
    uint8_t                     use_iwarp_mode;
    uint8_t                     use_rdma_cm_on_demand;
#endif /* defined(RDMA_CM) */

#ifdef _ENABLE_XRC_
    /* XRC parameters specific to a process */
    uint32_t                    xrc_srqn[MAX_NUM_HCAS];
    int                         xrc_fd[MAX_NUM_HCAS];
    struct ibv_xrc_domain       *xrc_domain[MAX_NUM_HCAS];
    uint8_t                     has_xrc;
    uint8_t                     xrc_rdmafp;
#endif /* _ENABLE_XRC_ */

#ifdef _ENABLE_UD_
    /* UD specific parameters */
    mv2_ud_ctx_t                *ud_rails[MAX_NUM_HCAS];
    mv2_ud_exch_info_t          *remote_ud_info;
    message_queue_t             unack_queue;
    mv2_ud_zcopy_info_t         zcopy_info;
    uint32_t                    rc_connections;
#endif /*_ENABLE_UD_ */
} MPIDI_CH3I_RDMA_Process_t;

#ifdef _ENABLE_XRC_
#define USE_XRC (MPIDI_CH3I_RDMA_Process.has_xrc)
#endif  /* _ENABLE_XRC_ */

struct process_init_info {
    int         **hostid;
    uint16_t    **lid;
    uint32_t    **qp_num_rdma;
    union ibv_gid    **gid;
    uint64_t    *vc_addr;
    uint32_t    *hca_type;
};

typedef struct ud_addr_info {
    int hostid;
    uint16_t lid[MAX_NUM_HCAS][MAX_NUM_PORTS];
    uint32_t qpn;
    union ibv_gid gid[MAX_NUM_HCAS][MAX_NUM_PORTS];
}ud_addr_info_t;

struct MPIDI_PG;

extern MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define IBV_RETURN_ERR   -3     /* gen2 function return error */
#define IBV_STATUS_ERR   -4     /*  gen2 function status error */

#define ibv_va_error_abort(code, message, args...)  {           \
    int my_rank;                                                \
    PMI_Get_rank(&my_rank);                                     \
    fprintf(stderr, "[%d] Abort: ", my_rank);                   \
    fprintf(stderr, message, ##args);                           \
    fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
            __FILE__);                                          \
    fflush (stderr);                                            \
    exit(code);                                                 \
}

#define ibv_error_abort(code, message)                          \
{                                                               \
	int my_rank;                                                \
	PMI_Get_rank(&my_rank);                                     \
	fprintf(stderr, "[%d] Abort: ", my_rank);                   \
	fprintf(stderr, message);                                   \
	fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
	    __FILE__);                                              \
    fflush (stderr);                                            \
	exit(code);                                                 \
}

#define PACKET_SET_RDMA_CREDIT(_p, _c)                          \
{                                                               \
    (_p)->rdma_credit     = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                            \
    (_p)->vbuf_credit     = 0;                            \
    (_p)->remote_credit   = 0;                            \
}

#define PACKET_SET_CREDIT(_p, _c, _rail_index)                  \
{                                                               \
    (_p)->rdma_credit     = (_c)->mrail.rfp.rdma_credit;  \
    (_c)->mrail.rfp.rdma_credit = 0;                            \
    (_p)->vbuf_credit     =                               \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit;        \
    (_p)->remote_credit   =                               \
    (_c)->mrail.srp.credits[(_rail_index)].remote_credit;       \
    (_c)->mrail.srp.credits[(_rail_index)].local_credit = 0;    \
}

#define PREPOST_VBUF_RECV(_c, _subrail)  {                      \
    vbuf *__v = get_vbuf();                                     \
    vbuf_init_recv(__v, VBUF_BUFFER_SIZE, _subrail);            \
    IBV_POST_RR(_c, __v, (_subrail));                           \
    (_c)->mrail.srp.credits[(_subrail)].local_credit++;         \
    (_c)->mrail.srp.credits[(_subrail)].preposts++;             \
}

#ifdef _ENABLE_XRC_
#define  XRC_FILL_SRQN_FIX_CONN(_v, _vc, _rail)\
do {                                                                    \
    if (USE_XRC && VC_XST_ISUNSET ((_vc), XF_DPM_INI)) {                \
        int hca_index = _rail / (rdma_num_ports                         \
                * rdma_num_qp_per_port);                                \
        (_v)->desc.u.sr.xrc_remote_srq_num =                            \
                (_vc)->ch.xrc_srqn[hca_index];                          \
        PRINT_DEBUG(DEBUG_XRC_verbose>1, "Msg for %d. Fixed SRQN: %d (WQE: %d) (%s:%d)\n",      \
                (_vc)->pg_rank,                                         \
                (_v)->desc.u.sr.xrc_remote_srq_num,                     \
                (_vc)->mrail.rails[(_rail)].send_wqes_avail,            \
                __FILE__, __LINE__);                                    \
        if (VC_XST_ISSET ((_vc), XF_INDIRECT_CONN)) {                   \
            PRINT_DEBUG(DEBUG_XRC_verbose>1, "Switched vc from %d to %d\n",                     \
                    (_vc)->pg_rank,                                     \
                    (_vc)->ch.orig_vc->pg_rank);                        \
            (_vc) = (_vc)->ch.orig_vc;                                  \
        }                                                               \
    }                                                                   \
} while (0);
#define  IBV_POST_SR(_v, _c, _rail, err_string) {                           \
    {                                                                       \
        PRINT_DEBUG(DEBUG_XRC_verbose>1, "POST_SR: to %d (qpn: %d) (state: %d %d %d) (%s:%d)\n",    \
                (_c)->pg_rank, (_c)->mrail.rails[(_rail)].qp_hndl->qp_num,  \
                (_c)->mrail.rails[(_rail)].qp_hndl->state, (_c)->ch.state,  \
                (_c)->state, __FILE__, __LINE__);                           \
        MPIU_Assert ((_c)->mrail.rails[(_rail)].send_wqes_avail >= 0);      \
        MPIU_Assert (!USE_XRC || VC_XST_ISUNSET ((_c), XF_INDIRECT_CONN));  \
        int __ret;                                                          \
        if(((_v)->desc.sg_entry.length <= rdma_max_inline_size)       \
                && ((_v)->desc.u.sr.opcode != IBV_WR_RDMA_READ))      \
        {                                                             \
           (_v)->desc.u.sr.send_flags = (enum ibv_send_flags)         \
                                        (IBV_SEND_SIGNALED |          \
                                         IBV_SEND_INLINE);            \
        } else {                                                      \
            (_v)->desc.u.sr.send_flags = IBV_SEND_SIGNALED ;          \
        }                                                             \
        if ((_rail) != (_v)->rail)                                    \
        {                                                             \
                DEBUG_PRINT("[%s:%d] rail %d, vrail %d\n",            \
                        __FILE__, __LINE__,(_rail), (_v)->rail);      \
                MPIU_Assert((_rail) == (_v)->rail);                   \
        }                                                             \
        MPIDI_CH3I_RDMA_Process.global_used_send_cq++;                \
        __ret = ibv_post_send((_c)->mrail.rails[(_rail)].qp_hndl,     \
                  &((_v)->desc.u.sr),&((_v)->desc.y.bad_sr));         \
        if(__ret) {                                                   \
            fprintf(stderr, "failed while avail wqe is %d, "          \
                    "rail %d\n",                                      \
                    (_c)->mrail.rails[(_rail)].send_wqes_avail,       \
                    (_rail));                                         \
            ibv_error_abort(-1, err_string);                          \
        }                                                             \
    }                                                                 \
}
#else
#define  XRC_FILL_SRQN_FIX_CONN(_v, _vc, _rail)

#if 0
inline static void print_info(vbuf* v, char* title, int err)
{
    if( !err )   return;
	
    static int cnt = 0;
    MPIDI_VC_t* vc = v->vc;
    struct ibv_wr_descriptor *desc = &(v->desc);
    int myrank = MPIDI_Process.my_pg_rank;
    char* msg;
    if( err) msg = "Error!!";
    else  msg = "";

    printf("[%d -> %d] %s:%s: sr.opcode=%d, phead_type=%d(%s), loc:%p:%x:len=%d, rmt:%p:%x\n", 
            myrank, vc->pg_rank, title, msg, 
            desc->u.sr.opcode, ((MPIDI_CH3I_MRAILI_Pkt_comm_header *)v->pheader)->type,
                       MPIDI_CH3_Pkt_type_to_string[((MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader)->type],
            desc->sg_entry.addr, desc->sg_entry.lkey, desc->sg_entry.length,
            desc->u.sr.wr.rdma.remote_addr, desc->u.sr.wr.rdma.rkey);
	if(err){
		struct ibv_qp_attr attr;
		struct ibv_qp_init_attr init_attr;
		enum ibv_qp_attr_mask attr_mask = 0;
		memset(&attr, 0, sizeof(attr));
		memset(&init_attr, 0, sizeof(init_attr) );
	
		int rv = ibv_query_qp( vc->mrail.rails[0].qp_hndl, &attr,
			0xffffffff, &init_attr ); 
		/* sleep(1000000); */
	}
    cnt++;
}
#endif

#define  IBV_POST_SR(_v, _c, _rail, err_string) {                     \
    {                                                                 \
        int __ret;                                                    \
        if(((_v)->desc.sg_entry.length <= rdma_max_inline_size)       \
                && ((_v)->desc.u.sr.opcode != IBV_WR_RDMA_READ))      \
        {                                                             \
           (_v)->desc.u.sr.send_flags = (enum ibv_send_flags)         \
                                        (IBV_SEND_SIGNALED |          \
                                         IBV_SEND_INLINE);            \
        } else {                                                      \
            (_v)->desc.u.sr.send_flags = IBV_SEND_SIGNALED ;          \
        }                                                             \
        if ((_rail) != (_v)->rail)                                    \
        {                                                             \
                DEBUG_PRINT("[%s:%d] rail %d, vrail %d\n",            \
                        __FILE__, __LINE__,(_rail), (_v)->rail);      \
                MPIU_Assert((_rail) == (_v)->rail);                   \
        }                                                             \
        MPIDI_CH3I_RDMA_Process.global_used_send_cq++;                \
        __ret = ibv_post_send((_c)->mrail.rails[(_rail)].qp_hndl,     \
                  &((_v)->desc.u.sr),&((_v)->desc.y.bad_sr));         \
        if(__ret) {                                                   \
		printf("[%d => %d]: %s(%s): ret=%d, errno=%d: failed while avail wqe is %d, "  \
                    "rail %d\n",  MPIDI_Process.my_pg_rank, _c->pg_rank, \
                                       __func__, err_string, __ret, errno,    \
                     (_c)->mrail.rails[(_rail)].send_wqes_avail,      \
                     (_rail));                                        \
                       perror("IBV_POST_SR err::  "); 		          \
            ibv_error_abort(-1, err_string);                          \
        }							     \
    }                                                                 \
}
#endif /* _ENABLE_XRC_ */

#define IBV_POST_RR(_c,_vbuf,_rail) {                           \
    int __ret;                                                  \
    _vbuf->vc = (void *)_c;                                     \
    __ret = ibv_post_recv(_c->mrail.rails[(_rail)].qp_hndl,     \
                          &((_vbuf)->desc.u.rr),                \
            &((_vbuf)->desc.y.bad_rr));                         \
    if (__ret) {                                                \
        ibv_va_error_abort(IBV_RETURN_ERR,                      \
            "ibv_post_recv err with %d",          \
                __ret);                                         \
    }                                                           \
}

#define BACKLOG_ENQUEUE(q,v) {                      \
    v->desc.next = NULL;                            \
    if (q->vbuf_tail == NULL) {                     \
         q->vbuf_head = v;                          \
    } else {                                        \
         q->vbuf_tail->desc.next = v;               \
    }                                               \
    q->vbuf_tail = v;                               \
    q->len++;                                       \
}

#define BACKLOG_DEQUEUE(q,v)  {                     \
    v = q->vbuf_head;                               \
    q->vbuf_head = v->desc.next;                    \
    if (v == q->vbuf_tail) {                        \
        q->vbuf_tail = NULL;                        \
    }                                               \
    q->len--;                                       \
    v->desc.next = NULL;                            \
}

#define CHECK_UNEXP(ret, s)                           \
do {                                                  \
    if (ret) {                                        \
        fprintf(stderr, "[%s:%d]: %s\n",              \
                __FILE__,__LINE__, s);                \
    exit(1);                                          \
    }                                                 \
} while (0)

#define CHECK_RETURN(ret, s)                            \
do {                                                    \
    if (ret) {                                          \
    fprintf(stderr, "[%s:%d] error(%d): %s\n",          \
        __FILE__,__LINE__, ret, s);                     \
    exit(1);                                            \
    }                                                   \
}                                                       \
while (0)

#ifdef _ENABLE_UD_
#define MV2_HYBRID_SET_RC_CONN_INITIATED(vc)            \
do {                                                    \
    if (!(vc->mrail.state & MRAILI_RC_CONNECTING)) {    \
        rdma_hybrid_pending_rc_conn++;                  \
        vc->mrail.state |= MRAILI_RC_CONNECTING;        \
    }                                                   \
}while(0)
#else
#define MV2_HYBRID_SET_RC_CONN_INITIATED(vc)
#endif

#ifdef CKPT
#define MSG_LOG_ENQUEUE(vc, entry) { \
    entry->next = NULL; \
    if (vc->mrail.msg_log_queue_tail!=NULL) { \
        vc->mrail.msg_log_queue_tail->next = entry; \
    } \
    vc->mrail.msg_log_queue_tail = entry; \
    if (vc->mrail.msg_log_queue_head==NULL) { \
        vc->mrail.msg_log_queue_head = entry; \
    }\
}

#define MSG_LOG_DEQUEUE(vc, entry) { \
    entry = vc->mrail.msg_log_queue_head; \
    if (vc->mrail.msg_log_queue_head!=NULL) {\
        vc->mrail.msg_log_queue_head = vc->mrail.msg_log_queue_head->next; \
    }\
    if (entry == vc->mrail.msg_log_queue_tail) { \
        vc->mrail.msg_log_queue_tail = NULL; \
    }\
}

#define MSG_LOG_QUEUE_TAIL(vc) (vc->mrail.msg_log_queue_tail)

#define MSG_LOG_EMPTY(vc) (vc->mrail.msg_log_queue_head == NULL)

void MRAILI_Init_vc_network(MPIDI_VC_t * vc);

#endif

#define INVAL_HNDL (0xffffffff)

#define SIGNAL_FOR_PUT        (1)
#define SIGNAL_FOR_GET        (2)
#define SIGNAL_FOR_LOCK_ACT   (3)
#define SIGNAL_FOR_DECR_CC    (4)

/* Prototype for ring based startup */
int rdma_ring_boot_exchange(struct MPIDI_CH3I_RDMA_Process_t *proc,
                        MPIDI_PG_t *pg, int pg_size, struct process_init_info *);
int rdma_setup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *, int pg_rank, int pg_size);
int rdma_ring_exchange_host_id(MPIDI_PG_t * pg, int pg_rank, int pg_size);
int ring_rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc);
void ring_rdma_close_hca(struct MPIDI_CH3I_RDMA_Process_t *proc);
int rdma_cm_get_hca_type (struct MPIDI_CH3I_RDMA_Process_t *proc);
void rdma_process_hostid(MPIDI_PG_t * pg, int *host_ids, int my_rank, int pg_size);
int rdma_cleanup_startup_ring(struct MPIDI_CH3I_RDMA_Process_t *proc);
int rdma_cm_exchange_hostid(MPIDI_PG_t *pg, int pg_rank, int pg_size);
int rdma_ring_based_allgather(void *sbuf, int data_size,
        int proc_rank, void *rbuf, int job_size,
        struct MPIDI_CH3I_RDMA_Process_t *proc);

/* Other prototype */
struct process_init_info *alloc_process_init_info(int pg_size, int num_rails);
void free_process_init_info(struct process_init_info *, int pg_size);
struct ibv_mr * register_memory(void *, int len, int hca_num);
int deregister_memory(struct ibv_mr * mr);
int MRAILI_Backlog_send(MPIDI_VC_t * vc, int subrail);
int rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc);
int rdma_find_active_port(struct ibv_context *context, struct ibv_device *ib_dev);
int rdma_get_process_to_rail_mapping(int mrail_user_defined_p2r_type);
int  rdma_get_control_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc);
void  rdma_set_default_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc);
void rdma_get_user_parameters(int num_proc, int me);
void rdma_get_pm_parameters(MPIDI_CH3I_RDMA_Process_t *proc);
int rdma_iba_hca_init_noqp(struct MPIDI_CH3I_RDMA_Process_t *proc,
              int pg_rank, int pg_size);
int rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
              int pg_rank, MPIDI_PG_t *pg, struct process_init_info *);
int rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                 int pg_rank, int pg_size);
int rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                int pg_rank, MPIDI_PG_t *pg, struct process_init_info *);
void rdma_param_handle_heterogenity(uint32_t hca_type[], int pg_size);
int MRAILI_Process_send(void *vbuf_addr);
void MRAILI_Process_recv(vbuf *v); 
int post_send(MPIDI_VC_t *vc, vbuf *v, int rail);
int post_srq_send(MPIDI_VC_t *vc, vbuf *v, int rail);
#ifdef _ENABLE_UD_
int post_hybrid_send(MPIDI_VC_t *vc, vbuf *v, int rail);
int post_ud_send(MPIDI_VC_t* vc, vbuf* v, int rail, mv2_ud_ctx_t *);
int mv2_post_ud_recv_buffers(int num_bufs, mv2_ud_ctx_t *ud_ctx);
void mv2_ud_update_send_credits(vbuf *v);
int rdma_init_ud(struct MPIDI_CH3I_RDMA_Process_t *proc);
int mv2_ud_setup_zcopy_rndv(struct MPIDI_CH3I_RDMA_Process_t *proc);
int mv2_ud_get_remote_info(MPIDI_PG_t *pg, int pg_rank, int pg_size);
void mv2_check_resend();
void mv2_send_explicit_ack(MPIDI_VC_t *vc);
int MPIDI_CH3I_UD_Generate_addr_handles(MPIDI_PG_t *pg, int pg_rank, int pg_size);
void MRAILI_RC_Enable(MPIDI_VC_t *vc);
void MPIDI_CH3I_UD_Stats(MPIDI_PG_t *pg);
#endif /* _ENABLE_UD_ */
int MRAILI_Fill_start_buffer(vbuf *v, MPID_IOV *iov, int n_iov);
int MPIDI_CH3I_MRAILI_Recv_addr(MPIDI_VC_t * vc, void *vstart);
int MPIDI_CH3I_MRAILI_Recv_addr_reply(MPIDI_VC_t * vc, void *vstart);
void MRAILI_RDMA_Put(MPIDI_VC_t * vc, vbuf *v,
                     char * local_addr, uint32_t lkey,
                     char * remote_addr, uint32_t rkey,
                     int nbytes, int subrail);
void MRAILI_RDMA_Get(MPIDI_VC_t * vc, vbuf *v,
                     char * local_addr, uint32_t lkey,
                     char * remote_addr, uint32_t rkey,
                     int nbytes, int subrail);
int MRAILI_Send_select_rail(MPIDI_VC_t * vc);
void vbuf_address_send(MPIDI_VC_t *vc);
void vbuf_address_reply_send(MPIDI_VC_t *vc, uint8_t);
int vbuf_fast_rdma_alloc (struct MPIDI_VC *, int dir);
int MPIDI_CH3I_MRAILI_rput_complete(MPIDI_VC_t *, MPID_IOV *,
                                    int, int *num_bytes_ptr, 
                                    vbuf **, int rail);
int MPIDI_CH3I_MRAILI_rget_finish(MPIDI_VC_t *, MPID_IOV *,
                                    int, int *num_bytes_ptr, 
                                    vbuf **, int rail);
int MRAILI_Handle_one_sided_completions(vbuf * v);                            
int MRAILI_Flush_wqe(MPIDI_VC_t *vc, vbuf *v , int rail);
struct ibv_srq *create_srq(struct MPIDI_CH3I_RDMA_Process_t *proc,
				  int hca_num);

/*function to create qps for the connection and move them to INIT state*/
int cm_qp_create(MPIDI_VC_t *vc, int force, int qptype);

/*function to move qps to rtr and prepost buffers*/
int cm_qp_move_to_rtr(MPIDI_VC_t *vc, uint16_t *lids, union ibv_gid *gids, 
                        uint32_t *qpns, int flag, uint32_t * rqpn, int is_dpm);

/*function to move qps to rts and mark the connection available*/
int cm_qp_move_to_rts(MPIDI_VC_t *vc);

int get_pkey_index(uint16_t pkey, int hca_num, int port_num, uint16_t* index);
void set_pkey_index(uint16_t * pkey_index, int hca_num, int port_num);

void init_apm_lock(void);

void MRAILI_RDMA_Get_finish(MPIDI_VC_t * vc, 
        MPID_Request * rreq, int rail);
        
int reload_alternate_path(struct ibv_qp *qp);

int power_two(int x);
int qp_required(MPIDI_VC_t* vc, int my_rank, int dst_rank);

#endif                          /* RDMA_IMPL_H */
