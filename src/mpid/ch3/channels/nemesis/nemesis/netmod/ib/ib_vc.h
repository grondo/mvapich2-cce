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

#ifndef IB_VC_H
#define IB_VC_H

#define _GNU_SOURCE

#include <infiniband/verbs.h>

#include "mpid_nem_impl.h"
#include "ib_cm.h"


/**
 *  The vc provides a generic buffer in which network modules can store
 *  private fields This removes all dependencies from the VC struction
 *  on the network module, facilitating dynamic module loading.
 */
typedef struct
{
    uint32_t  ud_qpn;
    uint16_t  ud_dlid;
    uint64_t  node_guid;

    struct
    {
        struct MPID_Request *head;
        struct MPID_Request *tail;
    } send_queue;

    struct MPID_Request * send_active;
    struct MPID_Request * recv_active;
    /** Address handler */
    struct ibv_ah *ud_ah;

    volatile MPIDI_CH3I_VC_state_t state;
    MPID_nem_ib_cm_conn_type_t conn_status;
    struct ibv_qp *qp;
    struct MPID_nem_ib_queue_t *ib_send_queue;
    struct MPID_nem_ib_queue_t *ib_recv_queue;
    char   in_queue;

    unsigned char free_vc;

    uint16_t seqnum_send;
    /**
     * The sequential number of received packets.
     * It is the expected sequence packet nunmber.
     */
    uint16_t seqnum_recv;

    /**
     * Link to connection structure.
     * All connections are stored in conn_info.
     *
     * \see conn_info
     */
    MPID_nem_ib_connection_t *connection;
    MPID_nem_ib_channel_manager *cmanager;

    int force_rndv;

    int pending_r3_data;
    int received_r3_data;
} MPID_nem_ib_vc_area;

/**
 *  accessor macro to private fields in VC
 */
#define VC_FIELD(vc, field) (((MPID_nem_ib_vc_area *)(&((MPIDI_CH3I_VC *)(vc)->channel_private)->netmod_area))->field)

#define MPID_NEM_IB_UD_QPN_KEY      "ud_qp_key"
#define MPID_NEM_IB_LID_KEY         "lid_key"
#define MPID_NEM_IB_GUID_KEY        "guid_key"

int MPID_nem_ib_vc_init (MPIDI_VC_t *vc);
int MPID_nem_ib_vc_destroy(MPIDI_VC_t *vc);
int MPID_nem_ib_vc_terminate (MPIDI_VC_t *vc);

#endif /* IB_VC_H */
