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
#include "ib_device.h"
#include "ib_cm.h"
#include "ib_vc.h"
#include "ib_finalize.h"
#include "ib_process.h"
#include "pmi.h"
#include "ib_srq.h"
#include "mem_hooks.h"
#include "dreg.h"
#include "ib_poll.h"

#undef FUNCNAME
#define FUNCNAME MPIDI_nem_ib_flush
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_nem_ib_flush()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_NEM_IB_FLUSH);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_NEM_IB_FLUSH);

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    MPIDI_CH3I_VC *vc_ch;
    int i, pg_rank, pg_size, rail;
    int mpi_errno = MPI_SUCCESS;

    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    for (i = 0; i < pg_size; i++)
    {
        if (i == pg_rank)
        {
        continue;
        }

        MPIDI_PG_Get_vc_set_active(pg, i, &vc);
        vc_ch = (MPIDI_CH3I_VC *)vc->channel_private;

        /* Skip SMP VCs */
        if (vc_ch->is_local)
        {
            continue;
        }

        if (VC_FIELD(vc, state) != MPIDI_CH3I_VC_STATE_CONNECTED)
        {
            continue;
        }

        for (rail = 0; rail < rdma_num_rails; rail++)
        {
            while (0 != VC_FIELD(vc, connection)->srp.credits[rail].backlog.len)
            {
                if ((mpi_errno = MPID_Progress_test()) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
            }

            while (NULL != VC_FIELD(vc, connection)->rails[rail].ext_sendq_head)
            {
                if ((mpi_errno = MPID_Progress_test()) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        }
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_NEM_IB_FLUSH);

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;

}

#undef FUNCNAME
#define FUNCNAME MPIDI_NEM_IB_FREE_VC
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_nem_ib_free_vc (MPIDI_VC_t *vc)
{
    int mpi_errno = MPI_SUCCESS;
    int rail;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_NEM_IB_FREE_VC);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_NEM_IB_FREE_VC);

    for (rail = 0; rail < rdma_num_rails; rail++)
    {
        while (0 != VC_FIELD(vc, connection)->srp.credits[rail].backlog.len)
        {
            if ((mpi_errno = MPID_Progress_test()) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }

        while (NULL != VC_FIELD(vc, connection)->rails[rail].ext_sendq_head)
        {
            if ((mpi_errno = MPID_Progress_test()) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }

        while((rdma_default_max_send_wqe) != VC_FIELD(vc, connection)->rails[rail].send_wqes_avail) {
            if ((mpi_errno = MPID_Progress_test()) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        } 

    }


    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_NEM_IB_FREE_VC);

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;

}

#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPID_nem_ib_finalize (void)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_NEM_IB_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_NEM_IB_FINALIZE);


    /* No rdma functions will be called after this function */
    int error ATTRIBUTE((unused));
    int pg_rank;
    int pg_size;
    int i;
    int rail_index;
    int hca_index;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    MPIDI_CH3I_VC *vc_ch;
    int err;
    int mpi_errno = MPI_SUCCESS;

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    if (!use_iboeth && (rdma_3dtorus_support || rdma_path_sl_query)) {
        mv2_release_3d_torus_resources();
    }

    /* make sure everything has been sent */
    MPIDI_nem_ib_flush();
    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank) {
            continue;
        }

        MPIDI_PG_Get_vc_set_active(pg, i, &vc);
        vc_ch = (MPIDI_CH3I_VC *)vc->channel_private;

        if (vc_ch->is_local)
        {
            continue;
        }

        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            while((rdma_default_max_send_wqe) != VC_FIELD(vc, connection)->rails[rail_index].send_wqes_avail) {
                    /* only do inter-node polling now. Intra-node fbox has been
                     * finalized. */
                   MPID_nem_ib_poll(FALSE); 
            }
        }

    }

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

    /*
    if (error != PMI_SUCCESS) {
    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
        "**pmi_barrier", "**pmi_barrier %d", error);
    }
    */

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank) {
            continue;
        }

        MPIDI_PG_Get_vc_set_active(pg, i, &vc);
        vc_ch = (MPIDI_CH3I_VC *)vc->channel_private;

        if (vc_ch->is_local)
        {
            continue;
        }
    

        for (hca_index = 0; hca_index < ib_hca_num_hcas; hca_index++) {
            if (VC_FIELD(vc, connection)->rfp.RDMA_send_buf_mr[hca_index]) {
                err = ibv_dereg_mr(VC_FIELD(vc, connection)->rfp.RDMA_send_buf_mr[hca_index]);
                if (err)
                MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
            }
            if (VC_FIELD(vc, connection)->rfp.RDMA_recv_buf_mr[hca_index]) {
                err = ibv_dereg_mr(VC_FIELD(vc, connection)->rfp.RDMA_recv_buf_mr[hca_index]);
                if (err)
                MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
            }
        }

        if (VC_FIELD(vc, connection)->rfp.RDMA_send_buf_DMA)
            MPIU_Free(VC_FIELD(vc, connection)->rfp.RDMA_send_buf_DMA);
        if (VC_FIELD(vc, connection)->rfp.RDMA_recv_buf_DMA)
            MPIU_Free(VC_FIELD(vc, connection)->rfp.RDMA_recv_buf_DMA);
        if (VC_FIELD(vc, connection)->rfp.RDMA_send_buf)
            MPIU_Free(VC_FIELD(vc, connection)->rfp.RDMA_send_buf);
        if (VC_FIELD(vc, connection)->rfp.RDMA_recv_buf)
            MPIU_Free(VC_FIELD(vc, connection)->rfp.RDMA_recv_buf);

#ifndef MV2_DISABLE_HEADER_CACHING 
        if( NULL != VC_FIELD(vc, connection)) {
        MPIU_Free(VC_FIELD(vc, connection)->rfp.cached_incoming);
        MPIU_Free(VC_FIELD(vc, connection)->rfp.cached_outgoing);
        MPIU_Free(VC_FIELD(vc, connection)->rfp.cached_incoming_iheader);
        MPIU_Free(VC_FIELD(vc, connection)->rfp.cached_outgoing_iheader);
        }
#endif

    }


    /* STEP 2: destroy all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i) {
            continue;
        }


        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            err = ibv_destroy_qp(conn_info.connections[i].rails[rail_index].qp_hndl);
            if (err)
            MPIU_Error_printf("Failed to destroy QP (%d)\n", err);
        }

        MPIU_Free(conn_info.connections[i].rails);
        MPIU_Free(cmanagers[i].msg_channels);
        MPIU_Free(conn_info.connections[i].srp.credits);
    }


    /* STEP 3: release all the cq resource, 
     * release all the unpinned buffers, 
     * release the ptag and finally, 
     * release the hca */

    for (i = 0; i < ib_hca_num_hcas; i++) {
        if (process_info.has_srq) {
            pthread_cond_signal(&srq_info.srq_post_cond[i]);
            pthread_mutex_lock(&srq_info.async_mutex_lock[i]);
            pthread_mutex_lock(&srq_info.srq_post_mutex_lock[i]);
            pthread_mutex_unlock(&srq_info.srq_post_mutex_lock[i]);
            pthread_cond_destroy(&srq_info.srq_post_cond[i]);
            pthread_mutex_destroy(&srq_info.srq_post_mutex_lock[i]);
            pthread_cancel(srq_info.async_thread[i]);
            pthread_join(srq_info.async_thread[i], NULL);
            err = ibv_destroy_srq(hca_list[i].srq_hndl);
            pthread_mutex_unlock(&srq_info.async_mutex_lock[i]);
            pthread_mutex_destroy(&srq_info.async_mutex_lock[i]);
            if (err)
                MPIU_Error_printf("Failed to destroy SRQ (%d)\n", err);
        }


        err = ibv_destroy_cq(hca_list[i].cq_hndl);
        if (err)
            MPIU_Error_printf("[%d] Failed to destroy CQ (%d)\n", pg_rank, err);

        if (hca_list[i].send_cq_hndl) {
            err = ibv_destroy_cq(hca_list[i].send_cq_hndl);
            if (err) {
                MPIU_Error_printf("[%d] Failed to destroy send CQ (%d)\n", pg_rank, err);
            }
        }

        if (hca_list[i].recv_cq_hndl) {
            err = ibv_destroy_cq(hca_list[i].recv_cq_hndl);
            if (err) {
                MPIU_Error_printf("[%d] Failed to destroy recv CQ (%d)\n", pg_rank, err);
            }
        }

        if(rdma_use_blocking) {
            err = ibv_destroy_comp_channel(hca_list[i].comp_channel);
            if(err)
            MPIU_Error_printf("[%d] Failed to destroy CQ channel (%d)\n", pg_rank, err);
        }

        deallocate_vbufs(i);
        deallocate_vbuf_region();
        err = dreg_finalize();

        err = ibv_dealloc_pd(hca_list[i].ptag);

        if (err)  {
            MPIU_Error_printf("[%d] Failed to dealloc pd (%s)\n",
                pg_rank, strerror(errno));
        }

        err = ibv_close_device(hca_list[i].nic_context);

        if (err) {
            MPIU_Error_printf("[%d] Failed to close ib device (%s)\n",
                pg_rank, strerror(errno));
        }

    }

    if(process_info.polling_set != NULL) {
      MPIU_Free(process_info.polling_set);
    }

    if(cmanagers != NULL) {
        MPIU_Free(cmanagers);
    }


    if(conn_info.connections != NULL) {
        MPIU_Free(conn_info.connections);
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_NEM_IB_FINALIZE);

    return mpi_errno;
}
