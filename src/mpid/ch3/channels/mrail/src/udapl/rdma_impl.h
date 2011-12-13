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

#include "mpidi_ch3i_rdma_conf.h"
#include "udapl_param.h"
#include "udapl_header.h"
#include "mpidi_ch3_impl.h"
#include "udapl_priv.h"
#include "mpidi_ch3_rdma_pre.h"
#include <mv2_arch_hca_detect.h>

#include DAT_HEADER
#include <pthread.h>

typedef struct MPIDI_CH3I_RDMA_Process_t
{
    /* keep all rdma implementation specific global variable in a
       structure like this to avoid name collisions */
    int num_hcas;
    mv2_arch_type arch_type;

    uint8_t                     has_lazy_mem_unregister;
    uint8_t                     has_rdma_fast_path;
    uint8_t                     has_one_sided;

    int maxtransfersize;

    DAT_IA_HANDLE nic[MAX_NUM_HCAS];
    /* NICs */
    DAT_EP_HANDLE hca_port[MAX_SUBCHANNELS];
    /* Port numbers */
    DAT_PZ_HANDLE ptag[MAX_NUM_HCAS];
    /* TO DO:Wil have qpnum. We cud just store qp_num instead of entire pros structure
     * */
    DAT_EVD_HANDLE cq_hndl[MAX_NUM_HCAS];
    /* one cq for both send and recv */
    DAT_EVD_HANDLE conn_cq_hndl[MAX_NUM_HCAS];
    DAT_EVD_HANDLE creq_cq_hndl[MAX_NUM_HCAS];
    DAT_CONN_QUAL service_id[MAX_SUBCHANNELS];
    DAT_PSP_HANDLE psp_hndl[MAX_SUBCHANNELS];

    pthread_t              server_thread;

    /* information for the one-sided communication connection */
    DAT_EVD_HANDLE cq_hndl_1sc;
    DAT_CONN_QUAL service_id_1sc;
    DAT_PSP_HANDLE psp_hndl_1sc;
    DAT_EVD_HANDLE conn_cq_hndl_1sc;
    DAT_EVD_HANDLE creq_cq_hndl_1sc;

    int inline_size_1sc;
} MPIDI_CH3I_RDMA_Process_t;

int MRAILI_Backlog_send(MPIDI_VC_t * vc,
                        const MRAILI_Channel_info * channel);
extern MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
extern rdma_iba_addr_tb_t rdma_iba_addr_table;
extern MPIDI_PG_t *cached_pg;
void MRAILI_RDMA_Put(   MPIDI_VC_t * vc, vbuf *v,
                        char * local_addr, VIP_MEM_HANDLE local_hndl,
                        char * remote_addr, VIP_MEM_HANDLE remote_hndl,
                        int nbytes, MRAILI_Channel_info * subchannel
                    );
int
rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  MPIDI_VC_t * vc, int pg_rank, int pg_size);

int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         MPIDI_VC_t * vc, int pg_rank, int pg_size);

#ifdef USE_MPD_RING
int
rdma_iba_exchange_info(struct MPIDI_CH3I_RDMA_Process_t *proc,
                       MPIDI_VC_t * vc, int pg_rank, int pg_size);
#endif
int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            MPIDI_VC_t * vc, int pg_rank, int pg_size);

int MRAILI_Process_send(void *vbuf_addr);

void rdma_init_parameters (MPIDI_CH3I_RDMA_Process_t *proc);

#endif /* RDMA_IMPL_H */
