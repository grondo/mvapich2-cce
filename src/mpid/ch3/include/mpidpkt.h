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

#ifndef HAVE_MPIDPKT_H
#define HAVE_MPIDPKT_H

/* Enable the use of data within the message packet for small messages */
#if 0
#define USE_EAGER_SHORT
#define MPIDI_EAGER_SHORT_INTS 4
/* FIXME: This appears to assume that sizeof(int) == 4 (or at least >= 4) */
#define MPIDI_EAGER_SHORT_SIZE 16
#endif

/* This is the number of ints that can be carried within an RMA packet */
#define MPIDI_RMA_IMMED_INTS 1

/*
 * MPIDI_CH3_Pkt_type_t
 *
 * Predefined packet types.  This simplifies some of the code.
 */
/* FIXME: Having predefined names makes it harder to add new message types,
   such as different RMA types. */
typedef enum MPIDI_CH3_Pkt_type
{
    MPIDI_CH3_PKT_EAGER_SEND = 0,
#if defined(_OSU_MVAPICH_)
    MPIDI_CH3_PKT_EAGER_SEND_CONTIG,
#ifndef MV2_DISABLE_HEADER_CACHING 
    MPIDI_CH3_PKT_FAST_EAGER_SEND,
    MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ,
#endif /* !MV2_DISABLE_HEADER_CACHING */
    MPIDI_CH3_PKT_RPUT_FINISH,
    MPIDI_CH3_PKT_RGET_FINISH,
    MPIDI_CH3_PKT_ZCOPY_FINISH,
    MPIDI_CH3_PKT_ZCOPY_ACK,
    MPIDI_CH3_PKT_NOOP,
    MPIDI_CH3_PKT_RMA_RNDV_CLR_TO_SEND,
    MPIDI_CH3_PKT_PUT_RNDV,
    MPIDI_CH3_PKT_ACCUMULATE_RNDV,  /*8*/
    MPIDI_CH3_PKT_GET_RNDV,         /*9*/
    MPIDI_CH3_PKT_RNDV_READY_REQ_TO_SEND,
    MPIDI_CH3_PKT_PACKETIZED_SEND_START,
    MPIDI_CH3_PKT_PACKETIZED_SEND_DATA,
    MPIDI_CH3_PKT_RNDV_R3_DATA,
    MPIDI_CH3_PKT_RNDV_R3_ACK,
    MPIDI_CH3_PKT_ADDRESS,
    MPIDI_CH3_PKT_ADDRESS_REPLY,
    MPIDI_CH3_PKT_CM_ESTABLISH,
#if defined(CKPT)
    MPIDI_CH3_PKT_CM_SUSPEND,
    MPIDI_CH3_PKT_CM_REACTIVATION_DONE,
    MPIDI_CH3_PKT_CR_REMOTE_UPDATE,
#endif /* defined(CKPT) */
#if defined(_SMP_LIMIC_)
    MPIDI_CH3_PKT_LIMIC_COMP,
#endif
#endif /* defined(_OSU_MVAPICH_) */
#if defined(USE_EAGER_SHORT)
    MPIDI_CH3_PKT_EAGERSHORT_SEND,
#endif /* defined(USE_EAGER_SHORT) */
    MPIDI_CH3_PKT_EAGER_SYNC_SEND,    /* FIXME: no sync eager */
    MPIDI_CH3_PKT_EAGER_SYNC_ACK,
    MPIDI_CH3_PKT_READY_SEND,
    MPIDI_CH3_PKT_RNDV_REQ_TO_SEND,
    MPIDI_CH3_PKT_RNDV_CLR_TO_SEND,
    MPIDI_CH3_PKT_RNDV_SEND,          /* FIXME: should be stream put */
    MPIDI_CH3_PKT_CANCEL_SEND_REQ,
    MPIDI_CH3_PKT_CANCEL_SEND_RESP,
    MPIDI_CH3_PKT_PUT,                /* RMA Packets begin here */
    MPIDI_CH3_PKT_GET,
    MPIDI_CH3_PKT_GET_RESP,
    MPIDI_CH3_PKT_ACCUMULATE,
    MPIDI_CH3_PKT_LOCK,
    MPIDI_CH3_PKT_LOCK_GRANTED,
    MPIDI_CH3_PKT_PT_RMA_DONE,
    MPIDI_CH3_PKT_LOCK_PUT_UNLOCK, /* optimization for single puts */
    MPIDI_CH3_PKT_LOCK_GET_UNLOCK, /* optimization for single gets */
    MPIDI_CH3_PKT_LOCK_ACCUM_UNLOCK, /* optimization for single accumulates */
                                     /* RMA Packets end here */
    MPIDI_CH3_PKT_ACCUM_IMMED,     /* optimization for short accumulate */
    /* FIXME: Add PUT, GET_IMMED packet types */
    MPIDI_CH3_PKT_FLOW_CNTL_UPDATE,
    MPIDI_CH3_PKT_CLOSE,
    MPIDI_CH3_PKT_END_CH3
    /* The channel can define additional types by defining the value
       MPIDI_CH3_PKT_ENUM */
# if defined(MPIDI_CH3_PKT_ENUM)
    , MPIDI_CH3_PKT_ENUM
# endif    
    , MPIDI_CH3_PKT_END_ALL
}
MPIDI_CH3_Pkt_type_t;

#if defined(_OSU_MVAPICH_)
extern char *MPIDI_CH3_Pkt_type_to_string[MPIDI_CH3_PKT_END_ALL+1];
#endif

typedef struct MPIDI_CH3_Pkt_send
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;  /* XXX - uint8_t to conserve space ??? */
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request sender_req_id;	/* needed for ssend and send cancel */
    MPIDI_Message_match match;
    MPIDI_msg_sz_t data_sz;
}
MPIDI_CH3_Pkt_send_t;

#if defined(_OSU_MVAPICH_)
#if defined(_SMP_LIMIC_)
typedef struct MPIDI_CH3_Pkt_limic_comp
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    size_t nb;
    MPI_Request *send_req_id;
} MPIDI_CH3_Pkt_limic_comp_t;
#endif
#endif

/* NOTE: Normal and synchronous eager sends, as well as all ready-mode sends, 
   use the same structure but have a different type value. */
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_eager_send_t;
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_eager_sync_send_t;
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_ready_send_t;
 #if defined(_OSU_MVAPICH_)
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_eager_send_contig_t;
#endif /* defined(_OSU_MVAPICH_) */

#if defined(USE_EAGER_SHORT)
typedef struct MPIDI_CH3_Pkt_eagershort_send
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;  /* XXX - uint8_t to conserve space ??? */
#endif /* defined(_OSU_MVAPICH_) */
    MPIDI_Message_match match;
    MPIDI_msg_sz_t data_sz;
    int  data[MPIDI_EAGER_SHORT_INTS];    /* FIXME: Experimental for now */
}
MPIDI_CH3_Pkt_eagershort_send_t;
#endif /* defined(USE_EAGER_SHORT) */

typedef struct MPIDI_CH3_Pkt_eager_sync_ack
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request sender_req_id;
}
MPIDI_CH3_Pkt_eager_sync_ack_t;

#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3_Pkt_rndv_req_to_send
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request sender_req_id;  /* needed for ssend and send cancel */
    MPIDI_Message_match match;
    MPIDI_msg_sz_t data_sz;
    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
} MPIDI_CH3_Pkt_rndv_req_to_send_t;

typedef struct MPIDI_CH3I_Pkt_address {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rdma_hndl[MAX_NUM_HCAS];
    unsigned long rdma_address;
} MPIDI_CH3_Pkt_address_t;

typedef struct MPIDI_CH3I_Pkt_address_reply {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t reply_data;
} MPIDI_CH3_Pkt_address_reply_t;
/* data values for reply_data field*/
#define RDMA_FP_SUCCESS                 111
#define RDMA_FP_SENDBUFF_ALLOC_FAILED   121
#define RDMA_FP_MAX_SEND_CONN_REACHED   131

#else /* defined(_OSU_MVAPICH_) */
typedef MPIDI_CH3_Pkt_send_t MPIDI_CH3_Pkt_rndv_req_to_send_t;
#endif /* defined(_OSU_MVAPICH_) */

#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3I_Pkt_rndv_r3_Ack
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t ack_data;
} MPIDI_CH3_Pkt_rndv_r3_ack_t;
#endif /* defined(_OSU_MVAPICH_) */


typedef struct MPIDI_CH3_Pkt_rndv_clr_to_send
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPIDI_msg_sz_t recv_sz;
    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request sender_req_id;
    MPI_Request receiver_req_id;
}
MPIDI_CH3_Pkt_rndv_clr_to_send_t;

typedef struct MPIDI_CH3_Pkt_rndv_send
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request receiver_req_id;
}
MPIDI_CH3_Pkt_rndv_send_t;

#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3_Pkt_packetized_send_start {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPIDI_msg_sz_t origin_head_size;
} MPIDI_CH3_Pkt_packetized_send_start_t;

typedef struct MPIDI_CH3_Pkt_packetized_send_data {
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request receiver_req_id;
#if defined(_SMP_LIMIC_)
    MPID_Request *send_req_id;
#endif
} MPIDI_CH3_Pkt_packetized_send_data_t;

typedef MPIDI_CH3_Pkt_packetized_send_data_t MPIDI_CH3_Pkt_rndv_r3_data_t;

typedef struct MPIDI_CH3_Pkt_rput_finish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request receiver_req_id; /* echoed*/
} MPIDI_CH3_Pkt_rput_finish_t;

typedef struct MPIDI_CH3_Pkt_zcopy_finish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    int hca_index;
    MPI_Request receiver_req_id;
} MPIDI_CH3_Pkt_zcopy_finish_t;

typedef struct MPIDI_CH3_Pkt_zcopy_ack_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request sender_req_id; 
} MPIDI_CH3_Pkt_zcopy_ack_t;

typedef struct MPIDI_CH3_Pkt_rget_finish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    MPI_Request sender_req_id;
} MPIDI_CH3_Pkt_rget_finish_t;

typedef struct MPIDI_CH3_Pkt_cm_establish_t
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    int port_name_tag;
    uint64_t vc_addr; /* The VC that is newly created */
} MPIDI_CH3_Pkt_cm_establish_t;

#endif /* defined(_OSU_MVAPICH_) */

typedef struct MPIDI_CH3_Pkt_cancel_send_req
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request sender_req_id;
    MPIDI_Message_match match;
}
MPIDI_CH3_Pkt_cancel_send_req_t;

typedef struct MPIDI_CH3_Pkt_cancel_send_resp
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request sender_req_id;
    int ack;
}
MPIDI_CH3_Pkt_cancel_send_resp_t;

#if defined(MPIDI_CH3_PKT_DEFS)
MPIDI_CH3_PKT_DEFS
#endif

typedef struct MPIDI_CH3_Pkt_put
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
#if defined (_OSU_PSM_)
    uint32_t rndv_len;
    int source_rank;
    int target_rank;
    int rndv_tag;
    int rndv_mode;
    int mapped_trank;
    int mapped_srank;
#endif       
}
MPIDI_CH3_Pkt_put_t;

#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3_Pkt_put_rndv
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
    MPI_Request sender_req_id;
    MPIDI_msg_sz_t data_sz;
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;
}
MPIDI_CH3_Pkt_put_rndv_t;
#endif /* defined(_OSU_MVAPICH_) */

typedef struct MPIDI_CH3_Pkt_get
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Request request_handle;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
#if defined (_OSU_PSM_)
    int rndv_mode;
    int target_rank;
    int source_rank;
    int rndv_len;
    int rndv_tag;
    int mapped_srank;
    int mapped_trank;
#endif
}
MPIDI_CH3_Pkt_get_t;

#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3_Pkt_get_rndv
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Request request_handle;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
    MPIDI_msg_sz_t data_sz;
    MPIDI_CH3I_MRAILI_Rndv_info_t rndv;
}
MPIDI_CH3_Pkt_get_rndv_t;
#endif /* defined(_OSU_MVAPICH_) */

typedef struct MPIDI_CH3_Pkt_get_resp
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    int protocol;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Request request_handle;
#if defined (_OSU_PSM_)
    int target_rank;
    int source_rank;
    int rndv_tag;
    int rndv_mode;
    int rndv_len;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int mapped_trank;
    int mapped_srank;
#endif /* _OSU_PSM_ */
}
MPIDI_CH3_Pkt_get_resp_t;


#if defined(_OSU_MVAPICH_)
typedef struct MPIDI_CH3_Pkt_accum_rndv
{
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Op op;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
    MPI_Request sender_req_id;
    MPIDI_msg_sz_t data_sz;

    MPIDI_CH3I_MRAILI_RNDV_INFO_DECL
}
MPIDI_CH3_Pkt_accum_rndv_t;
#endif /* defined(_OSU_MVAPICH_) */

typedef struct MPIDI_CH3_Pkt_accum
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    void *addr;
    int count;
    MPI_Datatype datatype;
    int dataloop_size;   /* for derived datatypes */
    MPI_Op op;
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
#if defined (_OSU_PSM_)
    uint32_t rndv_len;
    int source_rank;
    int target_rank;
    int rndv_tag;
    int rndv_mode;
    int mapped_srank;
    int mapped_trank;
#endif    
}
MPIDI_CH3_Pkt_accum_t;

typedef struct MPIDI_CH3_Pkt_accum_immed
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */ 
    void *addr;
    int count;
    /* FIXME: Compress datatype/op into a single word (immedate mode) */
    MPI_Datatype datatype;
    MPI_Op op;
    /* FIXME: do we need these (use a regular accum packet if we do?) */
    MPI_Win target_win_handle; /* Used in the last RMA operation in each
                               * epoch for decrementing rma op counter in
                               * active target rma and for unlocking window 
                               * in passive target rma. Otherwise set to NULL*/
    MPI_Win source_win_handle; /* Used in the last RMA operation in an
                               * epoch in the case of passive target rma
                               * with shared locks. Otherwise set to NULL*/
    int data[MPIDI_RMA_IMMED_INTS];
#if defined (_OSU_PSM_)
    int source_rank;
    int target_rank;
    int mapped_srank;
    int mapped_trank;
#endif
}
MPIDI_CH3_Pkt_accum_immed_t;

typedef struct MPIDI_CH3_Pkt_lock
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    int lock_type;
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
#if defined (_OSU_PSM_)
    int source_rank;
    int target_rank;
    int mapped_srank;
    int mapped_trank;
#endif
}
MPIDI_CH3_Pkt_lock_t;

typedef struct MPIDI_CH3_Pkt_lock_granted
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Win source_win_handle;
#if defined (_OSU_PSM_)
    int source_rank;
    int target_rank;
    MPI_Win target_win_handle;
    int mapped_srank;
    int mapped_trank;
#endif
}
MPIDI_CH3_Pkt_lock_granted_t;

typedef MPIDI_CH3_Pkt_lock_granted_t MPIDI_CH3_Pkt_pt_rma_done_t;

typedef struct MPIDI_CH3_Pkt_lock_put_unlock
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    void *addr;
    int count;
    MPI_Datatype datatype;
}
MPIDI_CH3_Pkt_lock_put_unlock_t;

typedef struct MPIDI_CH3_Pkt_lock_get_unlock
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    int count;
    MPI_Datatype datatype;
    MPI_Request request_handle;
    void *addr;
}
MPIDI_CH3_Pkt_lock_get_unlock_t;

typedef struct MPIDI_CH3_Pkt_lock_accum_unlock
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
    uint32_t rma_issued;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPI_Win target_win_handle;
    MPI_Win source_win_handle;
    int lock_type;
    int count;
    MPI_Datatype datatype;
    MPI_Op op;
    void *addr;
}
MPIDI_CH3_Pkt_lock_accum_unlock_t;


typedef struct MPIDI_CH3_Pkt_close
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3I_MRAILI_IBA_PKT_DECL
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    int ack;
}
MPIDI_CH3_Pkt_close_t;

typedef union MPIDI_CH3_Pkt
{
#if defined(_OSU_MVAPICH_)
    uint8_t type;
    MPIDI_CH3_Pkt_address_t address;
    MPIDI_CH3_Pkt_address_reply_t addr_reply;
    MPIDI_CH3_Pkt_rput_finish_t rput_finish;
    MPIDI_CH3_Pkt_put_rndv_t put_rndv;
    MPIDI_CH3_Pkt_get_rndv_t get_rndv;
    MPIDI_CH3_Pkt_accum_rndv_t accum_rndv;
    MPIDI_CH3_Pkt_rndv_r3_ack_t rndv_r3_ack;
#else /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_type_t type;
#endif /* defined(_OSU_MVAPICH_) */
    MPIDI_CH3_Pkt_eager_send_t eager_send;
#if defined(USE_EAGER_SHORT)
    MPIDI_CH3_Pkt_eagershort_send_t eagershort_send;
#endif /* defined(USE_EAGER_SHORT) */
    MPIDI_CH3_Pkt_eager_sync_send_t eager_sync_send;
    MPIDI_CH3_Pkt_eager_sync_ack_t eager_sync_ack;
    MPIDI_CH3_Pkt_eager_send_t ready_send;
    MPIDI_CH3_Pkt_rndv_req_to_send_t rndv_req_to_send;
    MPIDI_CH3_Pkt_rndv_clr_to_send_t rndv_clr_to_send;
    MPIDI_CH3_Pkt_rndv_send_t rndv_send;
    MPIDI_CH3_Pkt_cancel_send_req_t cancel_send_req;
    MPIDI_CH3_Pkt_cancel_send_resp_t cancel_send_resp;
    MPIDI_CH3_Pkt_put_t put;
    MPIDI_CH3_Pkt_get_t get;
    MPIDI_CH3_Pkt_get_resp_t get_resp;
    MPIDI_CH3_Pkt_accum_t accum;
    MPIDI_CH3_Pkt_accum_immed_t accum_immed;
    MPIDI_CH3_Pkt_lock_t lock;
    MPIDI_CH3_Pkt_lock_granted_t lock_granted;
    MPIDI_CH3_Pkt_pt_rma_done_t pt_rma_done;    
    MPIDI_CH3_Pkt_lock_put_unlock_t lock_put_unlock;
    MPIDI_CH3_Pkt_lock_get_unlock_t lock_get_unlock;
    MPIDI_CH3_Pkt_lock_accum_unlock_t lock_accum_unlock;
    MPIDI_CH3_Pkt_close_t close;
# if defined(MPIDI_CH3_PKT_DECL)
    MPIDI_CH3_PKT_DECL
# endif
}
MPIDI_CH3_Pkt_t;

#if defined(_OSU_MVAPICH_)
extern int MPIDI_CH3_Pkt_size_index[];
#endif /* defined(_OSU_MVAPICH_) */

#if defined(MPID_USE_SEQUENCE_NUMBERS)
typedef struct MPIDI_CH3_Pkt_send_container
{
    MPIDI_CH3_Pkt_send_t pkt;
    struct MPIDI_CH3_Pkt_send_container_s * next;
}
MPIDI_CH3_Pkt_send_container_t;
#endif

#endif
