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

#ifndef MPIDI_CH3_RDMA_PRE_H
#define MPIDI_CH3_RDMA_PRE_H

#include "dreg.h"
#include "vbuf.h"

#include "mpiimpl.h"

/* Use this header to append implementation specific fields
   to mpich2 structures.
   The most common use is to add fields to the vc structure
   that are specific to the rdma implementation */

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;

#define MPIDI_CH3I_MRAILI_PKT_ADDRESS_DECL

#define MPIDI_CH3I_MRAILI_IBA_PKT_DEFS 1

#define _SCHEDULE 1

#define MPIDI_CH3I_MRAILI_IBA_PKT_DECL                              \
    uint16_t seqnum;                                                \
    unsigned int vbuf_credit;   /* piggybacked vbuf credit   */     \
    unsigned int remote_credit; /* our current credit count */      \
    unsigned int rdma_credit;                                       \
    union {                                                         \
        int smp_index;                                              \
        uint64_t vc_addr;                                           \
    } src;

typedef enum
{
    /* rendezvous with RDMA read from receiver */
    VAPI_PROTOCOL_RENDEZVOUS_UNSPECIFIED = 0,
    /*    VIADEV_PROTOCOL_SHORT=1, */
    VAPI_PROTOCOL_EAGER,
    /* eager in several vbufs. short is special case */
    VAPI_PROTOCOL_R3,
    /* rendezvous through receiver vbufs */
    VAPI_PROTOCOL_RPUT,
    /* rendezvous with RDMA write from sender */
    VAPI_PROTOCOL_RGET,
} MRAILI_Protocol_t;

typedef struct MPIDI_CH3I_MRAILI_Rndv_info
{
    MRAILI_Protocol_t protocol;
    /* Are the following three really needed for rdma write protocol? */
    void *buf_addr;
    VIP_MEM_HANDLE memhandle;
    /*int len;    */
} MPIDI_CH3I_MRAILI_Rndv_info_t;

#define MPIDI_CH3I_MRAILI_RNDV_INFO_DECL \
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;

#define MPIDI_CH3I_MRAILI_REQUEST_DECL \
struct MPIDI_CH3I_MRAILI_Request {  \
    MPI_Request partner_id;         \
    u_int8_t rndv_buf_alloc;              \
    void * rndv_buf;    \
    int rndv_buf_sz;    \
    int rndv_buf_off;   \
    MRAILI_Protocol_t protocol;               \
    dreg_entry *d_entry;  \
    void * remote_addr;         \
    VIP_MEM_HANDLE remote_handle;   \
    u_int8_t nearly_complete;       \
    struct MPID_Request *next_inflow;  \
} mrail;

#ifndef MV2_DISABLE_HEADER_CACHING 

#define MAX_SIZE_WITH_HEADER_CACHING 255
typedef struct MPIDI_CH3I_MRAILI_Pkt_fast_eager_t
{
    u_int8_t type;
    u_int8_t bytes_in_pkt;
    u_int16_t seqnum;
} MPIDI_CH3I_MRAILI_Pkt_fast_eager;

typedef struct MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req_t
{
    u_int8_t type;
    u_int8_t bytes_in_pkt;
    u_int16_t seqnum;
    int sender_req_id;
} MPIDI_CH3I_MRAILI_Pkt_fast_eager_with_req;
#endif

typedef struct MPIDI_CH3I_MRAILI_Pkt_comm_header_t
{
    u_int8_t type;              /* XXX - uint8_t to conserve space ??? */
#if defined(MPIDI_CH3I_MRAILI_IBA_PKT_DECL)
      MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#endif
} MPIDI_CH3I_MRAILI_Pkt_comm_header;

#define MPIDI_CH3I_MRAILI_Pkt_noop MPIDI_CH3I_MRAILI_Pkt_comm_header
#define MPIDI_CH3I_MRAILI_Pkt_flow_cntl MPIDI_CH3I_MRAILI_Pkt_comm_header

typedef struct MRAILI_Channel_manager_t
{
    int total_subrails;

    vbuf **v_queue_head;        /* put vbufs from each channel */
    vbuf **v_queue_tail;
    int *len;

    int num_local_pollings;
    vbuf *(**poll_channel) (void *vc);
} MRAILI_Channel_manager;


typedef struct MPIDI_CH3I_MRAILI_RDMAPATH_VC
{
    /* Though using multi-rail, only one set of rdma is used */
    /**********************************************************
     * Following part of the structure is shared by all rails *
     **********************************************************/
    void *RDMA_send_buf_orig;
    void *RDMA_recv_buf_orig;
    /* RDMA buffers for sending */
    struct vbuf *RDMA_send_buf;
    /* RDMA buffers for receive */
    struct vbuf *RDMA_recv_buf;
    /* RDMA buffer on the remote side, no actual space allocated */
    struct vbuf *remote_RDMA_buf;
    /* current flow control credit accumulated for remote side */
    u_int8_t rdma_credit;
    /* pointer to the head of free send buffers */
    /* this pointer advances when packets are sent */
    int phead_RDMA_send;
    /* pointer to the tail of free send buffers
     * no buffers available if head == tail */
    /* this pointer advances when we receive more credits */
    int ptail_RDMA_send;
    /* pointer to the head of free receive buffers
     * this is also where we should poll for incoming
     * rdma write messages */
    /* this pointer advances when we receive packets */
    int p_RDMA_recv;
    /* tail of recv free buffer ??? */
    /* last free buffer at the receive side */
    int p_RDMA_recv_tail;
    /* counter indicating we have received remote RDMA memory
     * address and handle for this connection */

    /* credit array, peer will RDMA_write to it to inform 
     * the newest progress */

    /* cache part of the packet header to reduce eager message size */
#ifndef MV2_DISABLE_HEADER_CACHING
    void *cached_outgoing;
    void *cached_incoming;
    int cached_hit;
    int cached_miss;
#endif

    /**********************************************************
     * Following part of the structure is array, one entry    *
     * corresponds to one rail                                *
     * array size is num_subrails                             *
     **********************************************************/
    /* RDMA buffers needs to be registered for each subrail */
    VIP_MEM_HANDLE *RDMA_send_buf_hndl;
    VIP_MEM_HANDLE *RDMA_recv_buf_hndl;
    /* RDMA buffer on the remote side */
    VIP_MEM_HANDLE *remote_RDMA_buf_hndl;
    /* we must make sure that a completion entry
     * is generated once in a while
     * this number of be less than max outstanding WQE */
    /* array of handles, size num_subchannel */
} MPIDI_CH3I_MRAILI_RDMAPATH_VC;

typedef struct _udapl_backlog_queue_t
{
    int len;                    /* length of backlog queue */
    vbuf *vbuf_head;            /* head of backlog queue */
    vbuf *vbuf_tail;            /* tail of backlog queue */
} udapl_backlog_queue_t;


typedef struct MPIDI_CH3I_MRAILI_SR_VC
{
    /**********************************************************
     * Following part of the structure is array, one entry    *
     * corresponds to one rail                                *
     * array size is num_subrails                             *
     **********************************************************/
    u_int8_t remote_credit[MAX_SUBCHANNELS];    /* how many vbufs I can consume on remote end. */
    u_int8_t local_credit[MAX_SUBCHANNELS];     /* accumulate vbuf credit locally here */
    u_int8_t preposts[MAX_SUBCHANNELS]; /* number of vbufs currently preposted */

    u_int8_t remote_cc[MAX_SUBCHANNELS];
    u_int8_t initialized[MAX_SUBCHANNELS];

    /* the backlog queue for this connection. */
    udapl_backlog_queue_t backlog;
    /* This field is used for managing preposted receives. It is the
     * number of rendezvous packets (r3/rput) expected to arrive for
     * receives we've ACK'd but have not been completed. In general we
     * can't prepost all of these vbufs, but we do prepost extra ones
     * to allow sender to keep the pipe full. As packets come in this
     * field is decremented.  We know when to stop preposting extra
     * buffers when this number goes to zero.
     */

    int rendezvous_packets_expected[MAX_SUBCHANNELS];
} MPIDI_CH3I_MRAILI_SR_VC;

/* sample implemenation structure */
typedef struct MPIDI_CH3I_MRAIL_VC_t
{
    int num_total_subrails;
    int subrail_per_hca;
    /* qp handle for each of the sub-rails */
    DAT_EP_HANDLE *qp_hndl;
    DAT_EP_HANDLE qp_hndl_1sc;
    int postsend_times_1sc;
    UDAPL_DESCRIPTOR ddesc1sc[RDMA_DEFAULT_PUT_GET_LIST_SIZE];

    /* number of send wqes available */
    /* Following three pointers are allocated in MPIDI_Init_vc function */
    int *send_wqes_avail;       /* send Q WQES available */
    struct vbuf **ext_sendq_head;       /* queue of sends which didn't fit on send Q */
    struct vbuf **ext_sendq_tail;       /* queue of sends which didn't fit on send Q */

    u_int16_t next_packet_expected;     /* for sequencing (starts at 1) */
    u_int16_t next_packet_tosend;       /* for sequencing (starts at 1) */
    MPIDI_CH3I_MRAILI_RDMAPATH_VC rfp;

    /* data structure needed for s/r path */
    /* It is commented out currently */
    MPIDI_CH3I_MRAILI_SR_VC srp;
    UDAPL_DESCRIPTOR put_desc;

    /* Buffered receiving request for packetized transfer */
    void *packetized_recv;

    MRAILI_Channel_manager cmanager;

    /* these fields are used to remember data transfer operations
     * that are currently in progress on this connection. The
     * send handle list is a queue of send handles representing
     * in-progress rendezvous transfers. It is processed in FIFO
     * order (because of MPI ordering rules) so there is both a head
     * and a tail.
     *
     * The receive handle is a pointer to a single
     * in-progress eager receive. We require that an eager sender
     * send *all* packets associated with an eager receive before
     * sending any others, so when we receive the first packet of
     * an eager series, we remember it by caching the rhandle
     * on the connection.
     *
     */
    void *sreq_head;            /* "queue" of send handles to process */
    void *sreq_tail;
    /* these two fields are used *only* by MPID_DeviceCheck to
     * build up a list of connections that have received new
     * flow control credit so that pending operations should be
     * pushed. nextflow is a pointer to the next connection on the
     * list, and inflow is 1 (true) or 0 (false) to indicate whether
     * the connection is currently on the flowlist. This is needed
     * to prevent a circular list.
     */
    void *nextflow;
    int inflow;
    /* used to distinguish which VIA barrier synchronozations have
     * completed on this connection.  Currently, only used during
     * process teardown.
     *
     int barrier_id; */
     uint64_t remote_vc_addr; /* Used to find vc at remote side */
} MPIDI_CH3I_MRAIL_VC;

/* add this structure to the implemenation specific macro */
#define MPIDI_CH3I_VC_RDMA_DECL MPIDI_CH3I_MRAIL_VC mrail;

/* structure MPIDI_CH3I_RDMA_Ops_list is the queue pool to record every
 * issued signaled RDMA write and RDMA read operation. The address of
 * the entries are assigned to the id field of the descriptors when they
 * are posted. So it will be easy to find the corresponding operators of
 * the RDMA operations when a completion queue entry is polled.
 */
struct MPIDI_CH3I_RDMA_put_get_list_t;
typedef struct MPIDI_CH3I_RDMA_put_get_list_t MPIDI_CH3I_RDMA_put_get_list;

#endif /* MPIDI_CH3_RDMA_PRE_H */
