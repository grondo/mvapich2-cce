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

#ifndef IB_SEND_H
#define IB_SEND_H

#define _GNU_SOURCE
#include "mpid_nem_impl.h"
#include <infiniband/verbs.h>

#define MPI_MRAIL_MSG_QUEUED (-1)

typedef enum MPIDI_nem_ib_Pkt_type
{
    MPIDI_CH3_PKT_NOOP = MPIDI_NEM_PKT_END + 1,
    MPIDI_CH3_PKT_ADDRESS,
    MPIDI_CH3_PKT_ADDRESS_REPLY,
    MPIDI_CH3_PKT_FAST_EAGER_SEND,
    MPIDI_CH3_PKT_FAST_EAGER_SEND_WITH_REQ,
    MPIDI_CH3_PKT_PACKETIZED_SEND_START,
    MPIDI_CH3_PKT_PACKETIZED_SEND_DATA,
    MPIDI_CH3_PKT_RNDV_R3_DATA, 
    MPIDI_CH3_PKT_RNDV_R3_ACK,
    MPIDI_NEM_IB_PKT_END
}
MPIDI_nem_ib_Pkt_type_t;

/* move to ib_channel_manager? */
typedef struct MPIDI_nem_ib_pkt_comm_header_t {
    uint8_t type;
    uint16_t seqnum;

    /* store the info need to be delivered to remote */
    uint8_t  vbuf_credit;
    uint8_t  remote_credit;
    uint8_t  rdma_credit;
    uint8_t  rail;
    uint64_t vc_addr;
} MPIDI_nem_ib_pkt_comm_header;

typedef struct MPIDI_nem_ib_pkt_address_t {
    uint8_t type;
    uint32_t rdma_hndl[MAX_NUM_HCAS];
    unsigned long rdma_address;
} MPIDI_nem_ib_pkt_address;

typedef struct MPIDI_nem_ib_pkt_address_reply_t {
    uint8_t type;
    uint8_t reply_data;
} MPIDI_nem_ib_pkt_address_reply;

typedef struct MPIDI_CH3_Pkt_rndv_r3_ack{
    uint8_t type;
    uint32_t ack_data;
} MPIDI_CH3_Pkt_rndv_r3_ack_t;
/* data values for reply_data field*/
#define RDMA_FP_SUCCESS                 111
#define RDMA_FP_SENDBUFF_ALLOC_FAILED   121
#define RDMA_FP_MAX_SEND_CONN_REACHED   131
#ifndef MV2_DISABLE_HEADER_CACHING
#define MAX_SIZE_WITH_HEADER_CACHING 255

typedef struct MPIDI_nem_ib_pkt_fast_eager_t {
    uint8_t type;
    uint8_t     bytes_in_pkt;
    uint16_t    seqnum;
} MPIDI_nem_ib_pkt_fast_eager;

typedef struct MPIDI_nem_ib_pkt_fast_eager_with_req_t {
    uint8_t type;
    uint8_t     bytes_in_pkt;
    uint16_t    seqnum;
    int         sender_req_id;
} MPIDI_nem_ib_pkt_fast_eager_with_req;
#endif

typedef struct MPIDI_CH3_Pkt_packetized_send_start {
    uint8_t type;
    uint16_t seqnum;
    MPIDI_msg_sz_t origin_head_size;
} MPIDI_CH3_Pkt_packetized_send_start_t;

typedef struct MPIDI_CH3_Pkt_packetized_send_data {
    uint8_t type;
    uint16_t seqnum;
    MPI_Request receiver_req_id;
} MPIDI_CH3_Pkt_packetized_send_data_t;


#define MPIDI_CH3_Pkt_rndv_r3_data_t MPIDI_CH3_Pkt_packetized_send_data_t

#define MPIDI_nem_ib_pkt_noop MPIDI_nem_ib_pkt_comm_header

#   define MPIDI_nem_ib_request_set_seqnum(req_, seqnum_)  \
    {                           \
        (req_)->dev.seqnum = (seqnum_);         \
    }
#   define MPIDI_nem_ib_get_send_seqnum(vc_, seqnum_out_)   \
    {                           \
    (seqnum_out_) = VC_FIELD(vc_, seqnum_send)++;       \
    }
#   define MPIDI_nem_ib_set_seqnum(pkt_, seqnum_)  \
    {                       \
        (pkt_)->seqnum = (seqnum_);     \
    }
#   define MPIDI_nem_ib_init_seqnum_send(vc_)   \
    {                       \
        VC_FIELD(vc_, seqnum_send) = 0;         \
    }


int MRAILI_Process_send(void *vbuf_addr);
int MRAILI_Send_noop_if_needed(MPIDI_VC_t * vc, int rail);
void MRAILI_Send_noop(MPIDI_VC_t * c, int rail);

int MPID_nem_ib_send (MPIDI_VC_t *vc, MPID_nem_cell_ptr_t cell, int datalen);
int MPID_nem_ib_iSendContig(MPIDI_VC_t *vc, MPID_Request *sreq, void *hdr, 
                MPIDI_msg_sz_t hdr_sz, void *data, MPIDI_msg_sz_t data_sz);
int MPID_nem_ib_iStartContigMsg(MPIDI_VC_t *vc, void *hdr, MPIDI_msg_sz_t hdr_sz, 
                void *data, MPIDI_msg_sz_t data_sz, MPID_Request **sreq_ptr);
int MPID_nem_ib_iSendNoncontig (MPIDI_VC_t *vc, MPID_Request *sreq, void *header, 
		MPIDI_msg_sz_t hdr_sz);
int MRAILI_Backlog_send(MPIDI_VC_t * vc, int rail);

void MRAILI_Ext_sendq_enqueue(MPIDI_VC_t *c, int rail, vbuf * v);
void vbuf_address_send(MPIDI_VC_t *vc);
void vbuf_address_reply_send(MPIDI_VC_t *vc, uint8_t data);
int MPIDI_nem_ib_fast_rdma_ok(MPIDI_VC_t * vc, int len);
int MPIDI_nem_ib_fast_rdma_send_complete(MPIDI_VC_t * vc,
                                              MPID_IOV * iov,
                                              int n_iov,
                                              int *num_bytes_ptr,
                                              vbuf ** vbuf_handle);
int MPIDI_nem_ib_post_send(MPIDI_VC_t * vc, vbuf * v, int rail);
int MPIDI_nem_ib_post_srq_send(MPIDI_VC_t* vc, vbuf* v, int rail);
int MPIDI_nem_ib_eager_send(MPIDI_VC_t * vc,
                        MPID_IOV * iov,
                        int n_iov,
                        int pkt_len,
                        int *num_bytes_ptr,
                        vbuf **buf_handle);
int MPIDI_nem_ib_lmt_r3_ack_send(MPIDI_VC_t *vc);
#endif
