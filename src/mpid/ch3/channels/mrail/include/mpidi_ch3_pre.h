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

#if !defined(MPICH_MPIDI_CH3_PRE_H_INCLUDED)
#define MPICH_MPIDI_CH3_PRE_H_INCLUDED

#include "mpidi_ch3i_rdma_conf.h"
#include "mpidi_ch3_rdma_pre.h"
#include "smp_smpi.h"

/*#define MPICH_DBG_OUTPUT*/

typedef struct MPIDI_CH3I_Process_group_s
{
    char * kvs_name;
    struct MPIDI_VC * unex_finished_list;
    int nEagerLimit;
    int local_process_id;
    int num_local_processes;
    int nRDMAWaitSpinCount;
    int nRDMAWaitYieldCount;
# if defined(MPIDI_CH3I_RDMA_PG_DECL)
    MPIDI_CH3I_RDMA_PG_DECL
# endif
}
MPIDI_CH3I_Process_group_t;

#define MPIDI_CH3_PG_DECL MPIDI_CH3I_Process_group_t ch;

#define MPIDI_CH3_PKT_DECL 
#define MPIDI_CH3_PKT_DEFS

#define MPIDI_DEV_IMPLEMENTS_KVS

#define MPIDI_CH3_IMPLEMENTS_ABORT

typedef enum MPIDI_CH3I_VC_state
{
    MPIDI_CH3I_VC_STATE_INVALID,
    MPIDI_CH3I_VC_STATE_UNCONNECTED,
    MPIDI_CH3I_VC_STATE_CONNECTING_CLI,
    MPIDI_CH3I_VC_STATE_CONNECTING_SRV,
#ifdef CKPT
    MPIDI_CH3I_VC_STATE_SUSPENDING,
    MPIDI_CH3I_VC_STATE_SUSPENDED,
    MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_1,
    MPIDI_CH3I_VC_STATE_REACTIVATING_CLI_2,
    MPIDI_CH3I_VC_STATE_REACTIVATING_SRV,
#endif
#ifdef RDMA_CM
    MPIDI_CH3I_VC_STATE_IWARP_SRV_WAITING,
    MPIDI_CH3I_VC_STATE_IWARP_CLI_WAITING,
#endif 
    MPIDI_CH3I_VC_STATE_IDLE,
    MPIDI_CH3I_VC_STATE_FAILED
}
MPIDI_CH3I_VC_state_t;

#define IS_RC_CONN_ESTABLISHED(vc) \
    (vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE && \
        vc->mrail.state & MRAILI_RC_CONNECTED)

/* This structure requires the iovec structure macros to be defined */
typedef struct MPIDI_CH3I_Buffer_t
{
    int use_iov;
    unsigned int num_bytes;
    void *buffer;
    unsigned int bufflen;
    MPID_IOV *iov;
    int iovlen;
    int index;
    int total;
} MPIDI_CH3I_Buffer_t;

typedef struct MPIDI_CH3I_RDMA_Unex_read_s
{
    struct MPIDI_CH3I_RDMA_Packet_t *pkt_ptr;
    unsigned char *buf;
    unsigned int length;
    int src;
    struct MPIDI_CH3I_RDMA_Unex_read_s *next;
} MPIDI_CH3I_RDMA_Unex_read_t;
#ifdef _ENABLE_XRC_
struct _xrc_pending_conn;
#endif
typedef struct MPIDI_CH3I_VC
{
    struct MPID_Request * sendq_head;
    struct MPID_Request * sendq_tail;
    struct MPID_Request * send_active;
    struct MPID_Request * recv_active;
    struct MPID_Request * req;
    volatile MPIDI_CH3I_VC_state_t state;
    MPIDI_CH3I_Buffer_t read;
    int read_state;
    int port_name_tag;
    /* Connection management */
    struct MPID_Request * cm_sendq_head;
    struct MPID_Request * cm_sendq_tail;
    struct vbuf         * cm_1sc_sendq_head;
    struct vbuf         * cm_1sc_sendq_tail;
#ifdef CKPT
    volatile int rput_stop; /*Stop rput message and wait for rkey update*/
#endif
#ifdef _ENABLE_XRC_
    uint32_t                    xrc_flags;
    struct MPIDI_VC             *orig_vc;
    struct _xrc_pending_conn    *xrc_conn_queue;
    uint32_t                    xrc_srqn[MAX_NUM_HCAS];
    uint32_t                    xrc_rqpn[MAX_NUM_SUBRAILS];
    uint32_t                    xrc_my_rqpn[MAX_NUM_SUBRAILS];
#endif
    MPIDI_msg_sz_t              pending_r3_data;
    MPIDI_msg_sz_t              received_r3_data;
} MPIDI_CH3I_VC;

#ifdef _ENABLE_XRC_
typedef struct _xrc_pending_conn {
    struct _xrc_pending_conn    *next;
    struct MPIDI_VC             *vc;
} xrc_pending_conn_t;
#define xrc_pending_conn_s (sizeof (xrc_pending_conn_t))

#define VC_XST_ISSET(vc, st)      ((vc)->ch.xrc_flags & (st))
#define VC_XST_ISUNSET(vc, st)    (!((vc)->ch.xrc_flags & (st)))
#define VC_XSTS_ISSET(vc, sts)    (((vc)->ch.xrc_flags & (sts)) == (sts))
#define VC_XSTS_ISUNSET(vc, sts)  (((vc)->ch.xrc_flags & (sts)) == 0)

#define VC_XST_SET(vc, st) vc->ch.xrc_flags |= (st);
#define VC_XST_CLR(vc, st) vc->ch.xrc_flags &= ~(st);

#define     XF_NONE             0x00000000 
#define     XF_START_RDMAFP     0x00000001
#define     XF_DIRECT_CONN      0x00000002
#define     XF_INDIRECT_CONN    0x00000004 
#define     XF_NEW_QP           0x00000008
#define     XF_SEND_IDLE        0x00000010
#define     XF_RECV_IDLE        0x00000020
#define     XF_NEW_RECV         0x00000040
#define     XF_CONN_CLOSING     0x00000080
#define     XF_INIT_DONE        0x00000100
#define     XF_SEND_CONNECTING  0x00000200
#define     XF_REUSE_WAIT       0x00000400
#define     XF_SMP_VC           0x00000800
#define     XF_DPM_INI          0x00001000
#define     XF_TERMINATED       0x00002000
#define     XF_UD_CONNECTED     0x00004000
#endif
/* SMP Channel is added by OSU-MPI2 */
typedef enum SMP_pkt_type
{
    SMP_EAGER_MSG,
    SMP_RNDV_MSG,
    SMP_RNDV_MSG_CONT
} SMP_pkt_type_t;

typedef struct MPIDI_CH3I_SMP_VC
{
    struct MPID_Request * sendq_head;
    struct MPID_Request * sendq_tail;
    struct MPID_Request * send_active;
    struct MPID_Request * recv_active;
    int local_nodes;
    int local_rank;
    SMP_pkt_type_t send_current_pkt_type;
    SMP_pkt_type_t recv_current_pkt_type;
    int hostid;
    int read_index;
    int read_off;
#if defined(_SMP_LIMIC_)
    struct limic_header current_l_header;
    int current_nb;
    int use_limic;
#endif
} MPIDI_CH3I_SMP_VC;

#ifndef MPIDI_CH3I_VC_RDMA_DECL
#define MPIDI_CH3I_VC_RDMA_DECL
#endif

#define MPIDI_CH3_VC_DECL \
MPIDI_CH3I_VC ch; \
MPIDI_CH3I_SMP_VC smp; \
MPIDI_CH3I_VC_RDMA_DECL
/* end of OSU-MPI2 */

/*
 * MPIDI_CH3_CA_ENUM (additions to MPIDI_CA_t)
 *
 * MPIDI_CH3I_CA_HANDLE_PKT - The completion of a packet request (send or
 * receive) needs to be handled.
 */
#define MPIDI_CH3_CA_ENUM			\
MPIDI_CH3I_CA_HANDLE_PKT,			\
MPIDI_CH3I_CA_END_RDMA

enum REQ_TYPE {
    REQUEST_NORMAL,
    REQUEST_RNDV_R3_HEADER,
    REQUEST_RNDV_R3_DATA
};

/*
 * MPIDI_CH3_REQUEST_DECL (additions to MPID_Request)
 */
#define MPIDI_CH3_REQUEST_DECL						\
struct MPIDI_CH3I_Request						\
{									\
    /*  pkt is used to temporarily store a packet header associated	\
       with this request */						\
    MPIDI_CH3_PktGeneric_t pkt;                        \
    enum REQ_TYPE   reqtype;						\
    /* For CKPT, hard to put in ifdef because it's in macro define*/    \
    struct MPID_Request *cr_queue_next;                                 \
    struct MPIDI_VC *vc;                                                \
} ch;

#define MPIDI_CH3_REQUEST_INIT(_rreq)   \
    (_rreq)->mrail.rndv_buf_alloc = 0;   \
    (_rreq)->mrail.rndv_buf = NULL;      \
    (_rreq)->mrail.rndv_buf_sz = 0;      \
    (_rreq)->mrail.rndv_buf_off = 0;     \
    (_rreq)->mrail.protocol = 0;         \
    (_rreq)->mrail.d_entry = NULL;       \
    (_rreq)->mrail.remote_addr = NULL;   \
    (_rreq)->mrail.nearly_complete = 0;

typedef struct MPIDI_CH3I_Progress_state
{
    int completion_count;
}
MPIDI_CH3I_Progress_state;

/* This variable is used in the definitions of the MPID_Progress_xxx macros,
   and must be available to the routines in src/mpi */
extern volatile unsigned int MPIDI_CH3I_progress_completion_count;

#define MPIDI_CH3_PROGRESS_STATE_DECL MPIDI_CH3I_Progress_state ch;

#define HAVE_DEV_COMM_HOOK
#define MPID_Dev_comm_create_hook(comm_) do {           \
        int _mpi_errno;                                 \
        _mpi_errno = MPIDI_CH3I_comm_create (comm_);    \
        if (_mpi_errno) MPIU_ERR_POP (_mpi_errno);      \
    } while(0)


#define MPID_Dev_comm_destroy_hook(comm_) do {          \
        int _mpi_errno;                                 \
        _mpi_errno = MPIDI_CH3I_comm_destroy (comm_);   \
        if (_mpi_errno) MPIU_ERR_POP (_mpi_errno);      \
    } while(0)


typedef struct MPIDI_CH3I_comm
{
    MPI_Comm     leader_comm;
    MPI_Comm     shmem_comm;
    MPI_Comm     allgather_comm;
    int*    leader_map;
    int*    leader_rank;
    int*    node_sizes; 
    int*    allgather_new_ranks;
    int     is_uniform; 
    int     shmem_comm_rank;
    int     shmem_coll_ok;
    int     allgather_comm_ok; 
    int     leader_group_size;
    int     is_global_block; 
} MPIDI_CH3I_comm_t;

#define MPID_DEV_COMM_DECL MPIDI_CH3I_comm_t ch;

#endif /* !defined(MPICH_MPIDI_CH3_PRE_H_INCLUDED) */
