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

#include "mpidi_ch3i_rdma_conf.h"
#include <sched.h>
#include <mpimem.h>
#include "rdma_impl.h"
#include "pmi.h"
#include "udapl_priv.h"
#include "udapl_util.h"
#include "mem_hooks.h"
#include "mpiutil.h"

#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

static int MPIDI_CH3I_PG_Compare_ids (void *id1, void *id2);
static int MPIDI_CH3I_PG_Destroy (MPIDI_PG_t * pg, void *id);

int cached_pg_size;
int cached_pg_rank;

pthread_mutex_t cm_conn_state_lock;
pthread_mutex_t cm_conn_state_lock_udapl;

int od_server_thread = 0;

extern DAT_EP_HANDLE temp_ep_handle;
extern void
MPIDI_CH3I_RDMA_util_atos (char *str, DAT_SOCK_ADDR * addr);
extern void
MPIDI_CH3I_RDMA_util_stoa (DAT_SOCK_ADDR * addr, char *str);

extern int
rdma_iba_hca_init_noep (struct MPIDI_CH3I_RDMA_Process_t *proc,
                   MPIDI_VC_t * vc, int pg_rank, int pg_size);
extern void cm_ep_create(MPIDI_VC_t *vc);

#ifdef MAC_OSX
extern int mvapich_need_malloc_init;
extern void *vt_dylib;
extern void *(*vt_malloc_ptr) (size_t);
extern void (*vt_free_ptr) (void *);
#endif

MPIDI_PG_t *cached_pg;
rdma_iba_addr_tb_t rdma_iba_addr_table;

static void *od_conn_server( void * arg );

static inline void MPICM_lock();
static inline void MPICM_unlock();
static inline void MPICM_lock_udapl();
static inline void MPICM_unlock_udapl();

/*Interface to lock/unlock connection manager*/
static inline void MPICM_lock()
{
/*    printf("[Rank %d],CM lock\n",cm_ib_context.rank); */
    pthread_mutex_lock(&cm_conn_state_lock);
}

static inline void MPICM_unlock()
{
/*    printf("[Rank %d],CM unlock\n", cm_ib_context.rank); */
    pthread_mutex_unlock(&cm_conn_state_lock);
}

/*Interface to lock/unlock connection manager*/
static inline void MPICM_lock_udapl()
{
/*    printf("[Rank %d],CM lock\n",cm_ib_context.rank); */
    pthread_mutex_lock(&cm_conn_state_lock_udapl);
}

static inline void MPICM_unlock_udapl()
{
/*    printf("[Rank %d],CM unlock\n", cm_ib_context.rank); */
    pthread_mutex_unlock(&cm_conn_state_lock_udapl);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_init(MPIDI_PG_t *pg, int pg_rank)
{
    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    int ret;
    int act_num_cqe;
    DAT_EP_ATTR ep_attr;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;
    DAT_EVENT event;
    int count;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN dummy_registered_size;
    DAT_VADDR dummy_registered_addr;
    DAT_IA_ATTR ia_attr;
    int tmp1;
    int step;
    int num_connected = 0;
    int num_connected_1sc = 0;

    DAT_CONN_QUAL local_service_id;
    DAT_CONN_QUAL local_service_id_1sc;
    DAT_SOCK_ADDR local_ia_addr;

    MPIDI_VC_t *vc;
    int         pg_size;
    int i, error;
    char *key;
    char *val;
    int key_max_sz;
    int val_max_sz;
    int *hostid_all;

    char rdmakey[512];
    char rdmavalue[512];
    char tmp[512];

#ifndef DISABLE_PTMALLOC    
    if(mvapich2_minit()) {
        fprintf(stderr,
                "[%s:%d] Error initializing MVAPICH2 malloc library\n",
                __FILE__, __LINE__);
        return MPI_ERR_OTHER;
    }
#elif !defined(SOLARIS) 
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size (pg);
    cached_pg_size = pg_size;

    /* Currently only single nic per pg is allowed */
    rdma_iba_addr_table.ia_addr =
        (DAT_SOCK_ADDR **) MPIU_Malloc (pg_size * sizeof (DAT_SOCK_ADDR *));
    rdma_iba_addr_table.hostid = (int **) MPIU_Malloc (pg_size * sizeof (int *));
    rdma_iba_addr_table.service_id =
        (DAT_CONN_QUAL **) MPIU_Malloc (pg_size * sizeof (DAT_CONN_QUAL *));
    rdma_iba_addr_table.service_id_1sc =
        (DAT_CONN_QUAL **) MPIU_Malloc (pg_size * sizeof (DAT_CONN_QUAL *));
    hostid_all = MPIU_Malloc (pg_size * sizeof(int));

    if (!rdma_iba_addr_table.ia_addr
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.service_id
        || !rdma_iba_addr_table.service_id_1sc)
      {
          fprintf (stderr, "Error %s:%d out of memory\n", __FILE__, __LINE__);
          exit (1);
      }


    for (i = 0; i < pg_size; ++i)
      {
          rdma_iba_addr_table.ia_addr[i] =
              (DAT_SOCK_ADDR *) MPIU_Malloc (MAX_NUM_HCAS *
                                        sizeof (DAT_SOCK_ADDR));

          rdma_iba_addr_table.hostid[i] =
              (int *) MPIU_Malloc (MAX_NUM_HCAS * sizeof (int));

          rdma_iba_addr_table.service_id[i] =
              (DAT_CONN_QUAL *) MPIU_Malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));

          rdma_iba_addr_table.service_id_1sc[i] =
              (DAT_CONN_QUAL *) MPIU_Malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));
          if (!rdma_iba_addr_table.ia_addr[i]
              || !rdma_iba_addr_table.hostid[i]
              || !rdma_iba_addr_table.service_id[i]
              || !rdma_iba_addr_table.service_id_1sc[i])
            {
                fprintf (stderr, "Error %s:%d out of memory\n",
                         __FILE__, __LINE__);
                exit (1);
            }
      }

    /* enable rdma_fast_path */
    MPIDI_CH3I_RDMA_Process.has_rdma_fast_path = 1;
    MPIDI_CH3I_RDMA_Process.has_one_sided = 1;

    rdma_init_parameters (&MPIDI_CH3I_RDMA_Process);

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; ++i)
      {
          MPIDI_PG_Get_vc (pg, i, &vc);
          MPIU_Memset (&(vc->mrail), 0, sizeof (vc->mrail));
          vc->mrail.num_total_subrails = 1;
          vc->mrail.subrail_per_hca = 1;
      }

    /* Open the device and create cq and qp's */
    MPIDI_CH3I_RDMA_Process.num_hcas = 1;
    rdma_iba_hca_init (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);
    MPIDI_CH3I_RDMA_Process.maxtransfersize = UDAPL_MAX_RDMA_SIZE;
    /* init dreg entry */
    dreg_init();
    DEBUG_PRINT ("vc->num_subrails %d\n", vc->mrail.num_subrails);
    /* Allocate memory and handlers */
    rdma_iba_allocate_memory (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);

    if (pg_size > 1)
      {
#if !defined(USE_MPD_RING)
          /*Exchange the information about HCA_lid and qp_num */
          /* Allocate space for pmi keys and values */
          error = PMI_KVS_Get_key_length_max (&key_max_sz);
          MPIU_Assert(error == PMI_SUCCESS);
          ++key_max_sz;
          key = MPIU_Malloc (key_max_sz);

          if (key == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi key");
                return error;
            }

          PMI_KVS_Get_value_length_max (&val_max_sz);
          MPIU_Assert (error == PMI_SUCCESS);
          ++val_max_sz;
          val = MPIU_Malloc (val_max_sz);

          if (val == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi value");
                return error;
            }
          /* STEP 1: Exchange HCA_lid */

          sprintf (rdmakey, "MV2HCA%08d", pg_rank);

          MPIDI_CH3I_RDMA_util_atos (rdmavalue,
                                     &(rdma_iba_addr_table.
                                       ia_addr[pg_rank][0]));

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }

          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                  {
                      continue;
                  }
                /* generate the key */
                sprintf (rdmakey, "MV2HCA%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                MPIDI_CH3I_RDMA_util_stoa (&
                                           (rdma_iba_addr_table.
                                            ia_addr[i][0]), rdmavalue);
            }

          /* this barrier is to prevent some process from
             overwriting values that has not been get yet */
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* STEP 2: Exchange qp_num and host id*/
                /* generate the key and value pair for each connection */
          hostid_all[pg_rank] =  gethostid();
          sprintf (rdmakey, "MV2QP%08d", pg_rank);
          sprintf (rdmavalue, "%08d-%016d",
                   (int) rdma_iba_addr_table.service_id[pg_rank][0],
                    hostid_all[pg_rank]);

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }
          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }


          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "MV2QP%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);

                strncpy (tmp, rdmavalue, 8);
                tmp[8] = '\0';
                rdma_iba_addr_table.service_id[i][0] = atoll (tmp);
                strncpy (tmp, rdmavalue + 8 + 1, 16);
                tmp[16] = '\0';
                hostid_all[i] = atoi(tmp);
            }

            rdma_process_hostid(pg, hostid_all, pg_rank, pg_size);
            MPIU_Free(hostid_all);
            

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
          DEBUG_PRINT ("After barrier\n");

      if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
          /* STEP 3: exchange the information about remote buffer */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;

                MPIDI_PG_Get_vc (pg, i, &vc);

                DEBUG_PRINT ("vc: %p, %p, i %d, pg_rank %d\n", vc,
                             vc->mrail.rfp.RDMA_recv_buf, i, pg_rank);

                /* generate the key and value pair for each connection */
                sprintf (rdmakey, "MV2BUF%08d-%08d", pg_rank, i);
                sprintf (rdmavalue, "%032ld-%016ld",
                         (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                         (DAT_RMR_CONTEXT) vc->mrail.rfp.
                         RDMA_recv_buf_hndl[0].rkey);

                DEBUG_PRINT ("Put %d recv_buf %016lX, key %08X\n",
                             i, (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                             (DAT_RMR_CONTEXT) vc->mrail.rfp.
                             RDMA_recv_buf_hndl[0].rkey);

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }
                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                MPIDI_PG_Get_vc (pg, i, &vc);

                /* generate the key */
                sprintf (rdmakey, "MV2BUF%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);

                /* we get the remote addresses, now the SendBufferTail.buffer
                   is the address for remote SendBuffer.tail;
                   the RecvBufferHead.buffer is the address for
                   the remote RecvBuffer.head */
                /*format : "%032d-%016d-%032d-%016d" */
                vc->mrail.rfp.remote_RDMA_buf_hndl =
                    MPIU_Malloc (sizeof (VIP_MEM_HANDLE) *
                            MPIDI_CH3I_RDMA_Process.num_hcas);
                strncpy (tmp, rdmavalue, 32);
                tmp[32] = '\0';
                vc->mrail.rfp.remote_RDMA_buf = (void *) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1, 16);
                tmp[16] = '\0';
                vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey =
                    (DAT_RMR_CONTEXT) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1 + 16 + 1, 32);
                tmp[32] = '\0';

                DEBUG_PRINT ("Get %d recv_buf %016lX, key %08X, local_credit %016lX, credit \
                    key %08X\n",
                             i, (aint_t) vc->mrail.rfp.remote_RDMA_buf, (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey,
                             (aint_t) (vc->mrail.rfp.remote_credit_array), (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_credit_update_hndl.
                             rkey);
            }
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
      }

      if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
          /* Exchange qp_num */
          /* generate the key and value pair for each connection */
          sprintf (rdmakey, "MV2OS%08d", pg_rank);
          sprintf (rdmavalue, "%08d",
                   (int) rdma_iba_addr_table.
                   service_id_1sc[pg_rank][0]);

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }
          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }


          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "MV2OS%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                rdma_iba_addr_table.service_id_1sc[i][0] = atoll (rdmavalue);

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          MPIU_Free (val);
          MPIU_Free (key);
      }
#else /*  end  f !defined (USE_MPD_RING) */

          /* Exchange the information about HCA_lid, qp_num, and memory,
           * With the ring-based queue pair */
          rdma_iba_exchange_info (&MPIDI_CH3I_RDMA_Process,
                                  vc, pg_rank, pg_size);
#endif
      }

    /* Enable all the queue pair connections */
    rdma_iba_enable_connections (&MPIDI_CH3I_RDMA_Process,
                                 vc, pg_rank, pg_size);

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier ();

    if (error != 0)
      {
          error =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                    "**pmi_barrier %d", error);
          return error;
      }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);

          MRAILI_Init_vc (vc, pg_rank);
      }

    error = PMI_Barrier ();
    if (error != 0)
      {
          error =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                    "**pmi_barrier %d", error);
          return error;
      }

    DEBUG_PRINT ("Done MPIDI_CH3I_RDMA_init()\n");
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int
MPIDI_CH3I_RDMA_finalize ()
{
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    DAT_RETURN error;
    int pg_rank;
    int pg_size;
    int i, j;
    int num_disconnected = 0;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int ret;
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size (pg);

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier ();
    if (error != 0)
      {
          error = MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                        __LINE__, MPI_ERR_OTHER,
                                        "**pmi_barrier", "**pmi_barrier %d",
                                        error);
          return error;
      }

    if (MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND &&
	od_server_thread) {
        ret = pthread_cancel(MPIDI_CH3I_RDMA_Process.server_thread);
        CHECK_RETURN (ret, "could not cancel server thread");
  
        ret = pthread_join(MPIDI_CH3I_RDMA_Process.server_thread, NULL);
        CHECK_RETURN(ret, "pthread_join failed in finalization");
    }

  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);
          error = dat_lmr_free (vc->mrail.rfp.RDMA_send_buf_hndl[0].hndl);
          CHECK_RETURN (error, "could not unpin send rdma buffer");
          error = dat_lmr_free (vc->mrail.rfp.RDMA_recv_buf_hndl[0].hndl);

          CHECK_RETURN (error, "could not unpin recv rdma buffer");
          /* free the buffers */
          MPIU_Free (vc->mrail.rfp.RDMA_send_buf_orig);
          MPIU_Free (vc->mrail.rfp.RDMA_recv_buf_orig);
          MPIU_Free (vc->mrail.rfp.RDMA_send_buf_hndl);
          MPIU_Free (vc->mrail.rfp.RDMA_recv_buf_hndl);
      }
  }

    deallocate_vbufs (MPIDI_CH3I_RDMA_Process.nic);
    while (dreg_evict ());

    /* STEP 2: destry all the eps, tears down all connections */
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (pg, i, &vc);

	  /*
	   * FIXME: When SMP + All-to-All Connection Management is used, the QPs are
	   * not supposed to be setup. However, since SMP initialization takes place
	   * after RDMA Init, the QPs are setup, even though they are not used. Hence,
	   * we have to tear down the QPs on an SMP VC *only* when All-to-All is used.
	   */
	  if (	SMP_INIT && (vc->smp.local_nodes >= 0) &&
		(MPIDI_CH3I_Process.cm_type != MPIDI_CH3I_CM_BASIC_ALL2ALL) )
	      continue;

          if(vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
              for (j = 0; j < vc->mrail.num_total_subrails; ++j) {
                error =
                    dat_ep_disconnect (vc->mrail.qp_hndl[j],
                                       DAT_CLOSE_GRACEFUL_FLAG);
                CHECK_RETURN (error, "fail to disconnect EP");
                ++num_disconnected;
              }
          }
      }

    PMI_Barrier ();

    /* Be the server or the client, there will be a disconnection event */
    {
        DAT_EVENT event;
        DAT_COUNT count;
        while (num_disconnected > 0)
          {
              if ((DAT_SUCCESS ==
                   dat_evd_wait (MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0],
                                 DAT_TIMEOUT_INFINITE, 1, &event, &count))
                  && (event.event_number ==
                      DAT_CONNECTION_EVENT_DISCONNECTED))
                {
                    --num_disconnected;
                }
          }                     /* while */
    }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    PMI_Barrier ();
    /* Disconnect all the EPs for 1sc  now */

    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (pg, i, &vc);
          error =
              dat_ep_disconnect (vc->mrail.qp_hndl_1sc,
                                 DAT_CLOSE_GRACEFUL_FLAG);
          CHECK_RETURN (error, "fail to disconnect EP");
      }
    /* Be the server or the client, there will be a disconnection event */

    PMI_Barrier ();

    {
        int num_disconnected_1sc = 0;
        DAT_EVENT event;
        DAT_COUNT count;
        while (num_disconnected_1sc < pg_size - 1)
          {
              if ((DAT_SUCCESS ==
                   dat_evd_wait (MPIDI_CH3I_RDMA_Process.conn_cq_hndl_1sc,
                                 DAT_TIMEOUT_INFINITE, 1, &event, &count))
                  && (event.event_number ==
                      DAT_CONNECTION_EVENT_DISCONNECTED))
                {
                    ++num_disconnected_1sc;
                }
          }                     /* while */
    }                           /* disconnect event block */
  }

#ifndef MV2_DISABLE_HEADER_CACHING 
  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);

          MPIU_Free (vc->mrail.rfp.cached_incoming);
          MPIU_Free (vc->mrail.rfp.cached_outgoing);
      }
  }
#endif

    /* free all the spaces */
    for (i = 0; i < pg_size; ++i)
      {
          if (rdma_iba_addr_table.ia_addr[i])
              MPIU_Free (rdma_iba_addr_table.ia_addr[i]);
          if (rdma_iba_addr_table.service_id[i])
              MPIU_Free (rdma_iba_addr_table.service_id[i]);
          if (rdma_iba_addr_table.hostid[i])
              MPIU_Free (rdma_iba_addr_table.hostid[i]);
          if (rdma_iba_addr_table.service_id_1sc[i])
              MPIU_Free (rdma_iba_addr_table.service_id_1sc[i]);
      }
    MPIU_Free (rdma_iba_addr_table.ia_addr);
    MPIU_Free (rdma_iba_addr_table.hostid);
    MPIU_Free (rdma_iba_addr_table.service_id);
    MPIU_Free (rdma_iba_addr_table.service_id_1sc);
    /* STEP 3: release all the cq resource, relaes all the unpinned buffers, release the
     * ptag
     *   and finally, release the hca */
    if (strcmp (dapl_provider, "ccil") == 0)
      {
          dat_ep_free (temp_ep_handle);
      }
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);

	  /*
	   * FIXME: When SMP + All-to-All Connection Management is used, the QPs are
	   * not supposed to be setup. However, since SMP initialization takes place
	   * after RDMA Init, the QPs are setup, even though they are not used. Hence,
	   * we have to tear down the QPs on an SMP VC *only* when All-to-All is used.
	   */
	  if (	SMP_INIT && (vc->smp.local_nodes >= 0) &&
		(MPIDI_CH3I_Process.cm_type != MPIDI_CH3I_CM_BASIC_ALL2ALL) )
	      continue;

          if(vc->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {
              error = dat_ep_free (vc->mrail.qp_hndl[0]);
              CHECK_RETURN (error, "error freeing ep");
              vc->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;
          }
      }

    for (i = 0; i < MPIDI_CH3I_RDMA_Process.num_hcas; ++i)
      {

          error = dat_psp_free (MPIDI_CH3I_RDMA_Process.psp_hndl[i]);
          CHECK_RETURN (error, "error freeing psp");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.conn_cq_hndl[i]);
          CHECK_RETURN (error, "error freeing creq evd");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.creq_cq_hndl[i]);
          CHECK_RETURN (error, "error freeing conn evd");

          error = dat_evd_free (MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
          CHECK_RETURN (error, "error freeing evd");
      }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    for (i = 0; i < pg_size; ++i)
      {
          if (i == pg_rank)
              continue;
          MPIDI_PG_Get_vc (pg, i, &vc);
          error = dat_ep_free (vc->mrail.qp_hndl_1sc);
          CHECK_RETURN (error, "error freeing ep for 1SC");
      }

    error = dat_psp_free (MPIDI_CH3I_RDMA_Process.psp_hndl_1sc);
    CHECK_RETURN (error, "error freeing psp 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.conn_cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing creq evd 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.creq_cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing conn evd 1SC");

    error = dat_evd_free (MPIDI_CH3I_RDMA_Process.cq_hndl_1sc);
    CHECK_RETURN (error, "error freeing evd 1SC");
  }

    error = dat_pz_free (MPIDI_CH3I_RDMA_Process.ptag[0]);
    error =
        dat_ia_close (MPIDI_CH3I_RDMA_Process.nic[0],
                      DAT_CLOSE_GRACEFUL_FLAG);
    if (error != DAT_SUCCESS)
      {
          error =
              dat_ia_close (MPIDI_CH3I_RDMA_Process.nic[0],
                            DAT_CLOSE_ABRUPT_FLAG);
          CHECK_RETURN (error, "fail to close IA");
      }


    return MPI_SUCCESS;
}


static int
MPIDI_CH3I_PG_Compare_ids (void *id1, void *id2)
{
    return (strcmp ((char *) id1, (char *) id2) == 0) ? TRUE : FALSE;
}

static int
MPIDI_CH3I_PG_Destroy (MPIDI_PG_t * pg, void *id)
{
    if (pg->ch.kvs_name != NULL)
      {
          MPIU_Free (pg->ch.kvs_name);
      }

    if (id != NULL)
      {
          MPIU_Free (id);
      }

    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Init(MPIDI_PG_t * pg, int pg_rank, char **str)
{
    int ret;
    MPIDI_VC_t *vc;
    int act_num_cqe;
    DAT_EP_ATTR ep_attr;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;
    DAT_EVENT event;
    int count;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN dummy_registered_size;
    DAT_VADDR dummy_registered_addr;
    DAT_IA_ATTR ia_attr;
    int tmp1;
    int step;
    int num_connected = 0;
    int num_connected_1sc = 0;

    DAT_CONN_QUAL local_service_id;
    DAT_CONN_QUAL local_service_id_1sc;
    DAT_SOCK_ADDR local_ia_addr;

    int pg_size;
    int i, error;
    char *key;
    char *val;
    int key_max_sz;
    int val_max_sz;
    int *hostid_all;

    char rdmakey[512];
    char rdmavalue[512];
    char tmp[512];

    *str = NULL; /* We do not support dynamic process management */
#ifndef DISABLE_PTMALLOC
    if(mvapich2_minit()) {
        fprintf(stderr,
                "[%s:%d] Error initializing MVAPICH2 malloc library\n",
                __FILE__, __LINE__);
        return MPI_ERR_OTHER;
    }
#elif !defined(SOLARIS)
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    cached_pg = pg;
    cached_pg_rank = pg_rank;
    pg_size = MPIDI_PG_Get_size (pg);
    cached_pg_size = pg_size;

    /* Currently only single nic per pg is allowed */
    rdma_iba_addr_table.ia_addr =
        (DAT_SOCK_ADDR **) MPIU_Malloc (pg_size * sizeof (DAT_SOCK_ADDR *));
    rdma_iba_addr_table.hostid = (int **) MPIU_Malloc (pg_size * sizeof (int *));
    rdma_iba_addr_table.service_id =
        (DAT_CONN_QUAL **) MPIU_Malloc (pg_size * sizeof (DAT_CONN_QUAL *));
    rdma_iba_addr_table.service_id_1sc =
        (DAT_CONN_QUAL **) MPIU_Malloc (pg_size * sizeof (DAT_CONN_QUAL *));
    hostid_all = MPIU_Malloc (pg_size * sizeof(int));

    if (!rdma_iba_addr_table.ia_addr
        || !rdma_iba_addr_table.hostid
        || !rdma_iba_addr_table.service_id
        || !rdma_iba_addr_table.service_id_1sc)
      {
          fprintf (stderr, "Error %s:%d out of memory\n", __FILE__, __LINE__);
          exit (1);
      }


    for (i = 0; i < pg_size; ++i)
      {
          rdma_iba_addr_table.ia_addr[i] =
              (DAT_SOCK_ADDR *) MPIU_Malloc (MAX_NUM_HCAS *
                                        sizeof (DAT_SOCK_ADDR));

          rdma_iba_addr_table.hostid[i] =
              (int *) MPIU_Malloc (MAX_NUM_HCAS * sizeof (int));

          rdma_iba_addr_table.service_id[i] =
              (DAT_CONN_QUAL *) MPIU_Malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));

          rdma_iba_addr_table.service_id_1sc[i] =
              (DAT_CONN_QUAL *) MPIU_Malloc (MAX_SUBCHANNELS *
                                        sizeof (DAT_CONN_QUAL));
          if (!rdma_iba_addr_table.ia_addr[i]
              || !rdma_iba_addr_table.hostid[i]
              || !rdma_iba_addr_table.service_id[i]
              || !rdma_iba_addr_table.service_id_1sc[i])
            {
                fprintf (stderr, "Error %s:%d out of memory\n",
                         __FILE__, __LINE__);
                exit (1);
            }
      }

    rdma_init_parameters (&MPIDI_CH3I_RDMA_Process);

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; ++i)
      {
          MPIDI_PG_Get_vc (pg, i, &vc);
          MPIU_Memset (&(vc->mrail), 0, sizeof (vc->mrail));
          vc->mrail.num_total_subrails = 1;
          vc->mrail.subrail_per_hca = 1;
      }

    /* disable rdma_fast_path if using on demand connection management */
    MPIDI_CH3I_RDMA_Process.has_rdma_fast_path = 0;
    MPIDI_CH3I_RDMA_Process.has_one_sided = 0;
 
    /* Open the device and create cq and qp's */
    MPIDI_CH3I_RDMA_Process.num_hcas = 1;
    rdma_iba_hca_init_noep (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);

    MPIDI_CH3I_RDMA_Process.maxtransfersize = UDAPL_MAX_RDMA_SIZE;
    /* init dreg entry */
    dreg_init();
    DEBUG_PRINT ("vc->num_subrails %d\n", vc->mrail.num_subrails);
    /* Allocate memory and handlers */
    rdma_iba_allocate_memory (&MPIDI_CH3I_RDMA_Process, vc, pg_rank, pg_size);

    if (pg_size > 1)
      {

#if !defined(USE_MPD_RING)
          /*Exchange the information about HCA_lid and qp_num */
          /* Allocate space for pmi keys and values */
          error = PMI_KVS_Get_key_length_max (&key_max_sz);
          MPIU_Assert (error == PMI_SUCCESS);
          ++key_max_sz;
          key = MPIU_Malloc (key_max_sz);

          if (key == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi key");
                return error;
            }

          PMI_KVS_Get_value_length_max (&val_max_sz);
          MPIU_Assert (error == PMI_SUCCESS);
          ++val_max_sz;
          val = MPIU_Malloc (val_max_sz);

          if (val == NULL)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER, "**nomem",
                                          "**nomem %s", "pmi value");
                return error;
            }
          /* STEP 1: Exchange HCA_lid */
          sprintf (rdmakey, "MV2HCAR%08d", pg_rank);

          MPIDI_CH3I_RDMA_util_atos (rdmavalue,
                                     &(rdma_iba_addr_table.
                                       ia_addr[pg_rank][0]));

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }

          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                  {
                      continue;
                  }
                /* generate the key */
                sprintf (rdmakey, "MV2HCAR%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                MPIDI_CH3I_RDMA_util_stoa (&
                                           (rdma_iba_addr_table.
                                            ia_addr[i][0]), rdmavalue);
            }

          /* this barrier is to prevent some process from
             overwriting values that has not been get yet */
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* STEP 2: Exchange qp_num and host id*/
          /* generate the key and value pair for each connection */
          hostid_all[pg_rank] = gethostid();
          sprintf (rdmakey, "MV2QPR%08d", pg_rank);
          sprintf (rdmavalue, "%08d-%016d",
                   (int) rdma_iba_addr_table.service_id[pg_rank][0],hostid_all[pg_rank]);

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }
          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "MV2QPR%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }

                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                strncpy (tmp, rdmavalue, 8);
                tmp[8] = '\0';
                rdma_iba_addr_table.service_id[i][0] = atoll (tmp);
                strncpy (tmp, rdmavalue + 8 + 1, 16);
                tmp[16] = '\0';
                hostid_all[i] = atoi(tmp);
            }
            rdma_process_hostid(pg, hostid_all, pg_rank, pg_size);
            MPIU_Free(hostid_all);

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
          DEBUG_PRINT ("After barrier\n");

      if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
          /* STEP 3: exchange the information about remote buffer */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;

                MPIDI_PG_Get_vc (pg, i, &vc);

                DEBUG_PRINT ("vc: %p, %p, i %d, pg_rank %d\n", vc,
                             vc->mrail.rfp.RDMA_recv_buf, i, pg_rank);

                /* generate the key and value pair for each connection */
                sprintf (rdmakey, "MV2BUFR%08d-%08d", pg_rank, i);
                sprintf (rdmavalue, "%032ld-%016ld",
                         (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                         (DAT_RMR_CONTEXT) vc->mrail.rfp.
                         RDMA_recv_buf_hndl[0].rkey);

                DEBUG_PRINT ("Put %d recv_buf %016lX, key %08X\n",
                             i, (aint_t) vc->mrail.rfp.RDMA_recv_buf,
                             (DAT_RMR_CONTEXT) vc->mrail.rfp.
                             RDMA_recv_buf_hndl[0].rkey);

                /* put the kvs into PMI */
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                MPIU_Strncpy (val, rdmavalue, val_max_sz);
                error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_put",
                                                "**pmi_kvs_put %d", error);
                      return error;
                  }
                error = PMI_KVS_Commit (pg->ch.kvs_name);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_commit",
                                                "**pmi_kvs_commit %d", error);
                      return error;
                  }
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                MPIDI_PG_Get_vc (pg, i, &vc);

                /* generate the key */
                sprintf (rdmakey, "MV2BUFR%08d-%08d", i, pg_rank);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);

                /* we get the remote addresses, now the SendBufferTail.buffer
                   is the address for remote SendBuffer.tail;
                   the RecvBufferHead.buffer is the address for
                   the remote RecvBuffer.head */
                /*format : "%032d-%016d-%032d-%016d" */
                vc->mrail.rfp.remote_RDMA_buf_hndl =
                    MPIU_Malloc (sizeof (VIP_MEM_HANDLE) *
                            MPIDI_CH3I_RDMA_Process.num_hcas);
                strncpy (tmp, rdmavalue, 32);
                tmp[32] = '\0';
                vc->mrail.rfp.remote_RDMA_buf = (void *) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1, 16);
                tmp[16] = '\0';
                vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey =
                    (DAT_RMR_CONTEXT) atol (tmp);
                strncpy (tmp, rdmavalue + 32 + 1 + 16 + 1, 32);
                tmp[32] = '\0';

                DEBUG_PRINT ("Get %d recv_buf %016lX, key %08X, local_credit %016lX, credit \
                    key %08X\n",
                             i, (aint_t) vc->mrail.rfp.remote_RDMA_buf, (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_RDMA_buf_hndl[0].rkey,
                             (aint_t) (vc->mrail.rfp.remote_credit_array), (DAT_RMR_CONTEXT) vc->mrail.rfp.remote_credit_update_hndl.
                             rkey);
            }
          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }
      }

      if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
          /* Exchange qp_num */
          /* generate the key and value pair for each connection */
          sprintf (rdmakey, "MV2OSR%08d", pg_rank);
          sprintf (rdmavalue, "%08d",
                   (int) rdma_iba_addr_table.
                   service_id_1sc[pg_rank][0]);

          /* put the kvs into PMI */
          MPIU_Strncpy (key, rdmakey, key_max_sz);
          MPIU_Strncpy (val, rdmavalue, val_max_sz);
          error = PMI_KVS_Put (pg->ch.kvs_name, key, val);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_put",
                                          "**pmi_kvs_put %d", error);
                return error;
            }
          error = PMI_KVS_Commit (pg->ch.kvs_name);
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                          FCNAME, __LINE__,
                                          MPI_ERR_OTHER,
                                          "**pmi_kvs_commit",
                                          "**pmi_kvs_commit %d", error);
                return error;
            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }

          /* Here, all the key and value pairs are put, now we can get them */
          for (i = 0; i < pg_size; ++i)
            {
                if (pg_rank == i)
                    continue;
                /* generate the key */
                sprintf (rdmakey, "MV2OSR%08d", i);
                MPIU_Strncpy (key, rdmakey, key_max_sz);
                error = PMI_KVS_Get (pg->ch.kvs_name, key, val, val_max_sz);
                if (error != 0)
                  {
                      error =
                          MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL,
                                                FCNAME, __LINE__,
                                                MPI_ERR_OTHER,
                                                "**pmi_kvs_get",
                                                "**pmi_kvs_get %d", error);
                      return error;
                  }
                MPIU_Strncpy (rdmavalue, val, val_max_sz);
                rdma_iba_addr_table.service_id_1sc[i][0] = atoll (rdmavalue);

            }

          error = PMI_Barrier ();
          if (error != 0)
            {
                error =
                    MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                          __LINE__, MPI_ERR_OTHER,
                                          "**pmi_barrier", "**pmi_barrier %d",
                                          error);
                return error;
            }


          MPIU_Free (val);
          MPIU_Free (key);
      }
#else /*  end  f !defined (USE_MPD_RING) */

          /* Exchange the information about HCA_lid, qp_num, and memory,
           * With the ring-based queue pair */
          rdma_iba_exchange_info (&MPIDI_CH3I_RDMA_Process,
                                  vc, pg_rank, pg_size);
#endif
      }

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier ();

    if (error != 0)
      {
          error =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**pmi_barrier",
                                    "**pmi_barrier %d", error);
          return error;
      }

    /* Start this thread after the EPs have been created and before any connection request has been made */
    pthread_create(&MPIDI_CH3I_RDMA_Process.server_thread, NULL, od_conn_server, (void*)pg);
    od_server_thread = 1;
    error = PMI_Barrier ();

    pthread_mutex_init(&cm_conn_state_lock, NULL);
    error = PMI_Barrier ();

    pthread_mutex_init(&cm_conn_state_lock_udapl, NULL);
    error = PMI_Barrier ();

    DEBUG_PRINT ("Done MPIDI_CH3I_RDMA_init()\n");

    return MPI_SUCCESS;
}

/* Waiting for connection request and give the reply */

static void *od_conn_server( void * arg )
{
     DAT_RETURN status;
     DAT_EVENT event;
     DAT_COUNT count;
     DAT_CR_ARRIVAL_EVENT_DATA cr_stat;
     DAT_CR_PARAM cr_param;
     DAT_EP_PARAM_MASK param_mask = 0xFFFF;
     DAT_EP_PARAM param;
     MPIDI_VC_t * vc;
     MPIDI_VC_t * peer_vc;
     MPIDI_PG_t * pg = (MPIDI_PG_t *)arg;
     int i, size, peer, nmore, ret;

     ret = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
     if(ret != 0)
         MPIU_Internal_error_printf("Error: cannot set server thread cancel state.\n");

     ret = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
     if(ret != 0)
         MPIU_Internal_error_printf("Error: cannot set server thread cancel type.\n");

     while (1) {

         status = dat_evd_wait(MPIDI_CH3I_RDMA_Process.creq_cq_hndl[0], DAT_TIMEOUT_INFINITE,
                               1, &event, &nmore);
         if (status != DAT_SUCCESS)
             continue;

         MPICM_lock();
         if (event.event_number == DAT_CONNECTION_REQUEST_EVENT) {
            cr_stat = event.event_data.cr_arrival_event_data;

            status = dat_cr_query( cr_stat.cr_handle, DAT_CR_FIELD_PRIVATE_DATA, &cr_param );
            if (status != DAT_SUCCESS) {
                MPIU_Internal_error_printf("Error: fail to get private data from the connection request.\n");
                MPICM_unlock();
                continue;
            }

            i = *(int *)cr_param.private_data;

            MPIDI_PG_Get_vc(pg,i,&vc);

            if (vc->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {
                cm_ep_create(vc);
                status = dat_cr_accept( cr_stat.cr_handle, vc->mrail.qp_hndl[0], 
                             sizeof(int), (DAT_PVOID)&cached_pg_rank);
                if (status != DAT_SUCCESS) {
                    MPIU_Internal_error_printf("Error: fail to send REP. "
                                               "vc->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED. "
                                               "status=%x\n", status);
                }

#ifdef SOLARIS
                do {
                    size = -1;

                    status = dat_evd_wait(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], 
                                          DAT_TIMEOUT_INFINITE,
                                          1, &event, &nmore);

                    if (status != DAT_SUCCESS) continue;

                    if (event.event_number != DAT_CONNECTION_EVENT_ESTABLISHED)
                        continue;

                    peer = *((int *)event.event_data.connect_event_data.private_data);

                    if (peer == i) {

                    /* the event generated by accept above */
                        MRAILI_Init_vc (vc, cached_pg_rank);
                        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                        MPIDI_CH3I_Process.new_conn_complete = 1;
			MPIDI_CH3I_Process.num_conn++;

                    } else {
                    /* someone just accepted my other request */
                        MPIDI_PG_Get_vc (cached_pg, peer, &peer_vc);
                        MRAILI_Init_vc (peer_vc, cached_pg_rank);
                        peer_vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                        MPIDI_CH3I_Process.new_conn_complete = 1;
			MPIDI_CH3I_Process.num_conn++;
                    }
                } while (peer != i);
#else
                do {
                    size = -1;

                    status = dat_evd_dequeue(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], &event);

                    if (status != DAT_SUCCESS) continue;

                    if (event.event_number != DAT_CONNECTION_EVENT_ESTABLISHED)
                        continue;

                    size = event.event_data.connect_event_data.private_data_size;

                    if (size == 0) { 
                    /* the event generated by accept above */
                        MRAILI_Init_vc (vc, cached_pg_rank);
                        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                        MPIDI_CH3I_Process.new_conn_complete = 1;
			MPIDI_CH3I_Process.num_conn++;

                    } else {
                    /* someone just accepted my other request */
                        peer = *((int *)event.event_data.connect_event_data.private_data);
                        MPIDI_PG_Get_vc (cached_pg, peer, &peer_vc);
                        MRAILI_Init_vc (peer_vc, cached_pg_rank);
                        peer_vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                        MPIDI_CH3I_Process.new_conn_complete = 1;
			MPIDI_CH3I_Process.num_conn++;
                    }                    
                } while (size != 0);
#endif

            } else {
                MPIU_Assert (vc->ch.state == MPIDI_CH3I_VC_STATE_CONNECTING_CLI);
                if ( cached_pg_rank < i ) {
                    /* I'm the server */

                    MPICM_lock_udapl();
                    status = dat_cr_accept( cr_stat.cr_handle, vc->mrail.qp_hndl[0], 
                                        sizeof(int), (DAT_PVOID)&cached_pg_rank);
                    MPICM_unlock_udapl();

                    if (status != DAT_SUCCESS) {
                        dat_ep_query(vc->mrail.qp_hndl[0], param_mask, &param);
                        if (param.ep_state == DAT_EP_STATE_ACTIVE_CONNECTION_PENDING) {
                            while(param.ep_state !=  DAT_EP_STATE_UNCONNECTED) {
                                MPICM_unlock();
                                sched_yield();
                                MPICM_lock();
                                dat_ep_query(vc->mrail.qp_hndl[0], param_mask, &param);
                            }

                           status = dat_cr_accept( cr_stat.cr_handle, vc->mrail.qp_hndl[0],
                                        sizeof(int), (DAT_PVOID)&cached_pg_rank);
                            if (status != DAT_SUCCESS) {
                                 MPIU_Internal_error_printf("Error: fail to accept cr after ep_reset 1");
                            }

                        } else if (param.ep_state == DAT_EP_STATE_DISCONNECTED) { 
                            while(param.ep_state !=  DAT_EP_STATE_UNCONNECTED) {
                                MPICM_unlock();
                                sched_yield();
                                MPICM_lock();
                                dat_ep_query(vc->mrail.qp_hndl[0], param_mask, &param);
                            }
                            status = dat_cr_accept( cr_stat.cr_handle, vc->mrail.qp_hndl[0],
                                        sizeof(int), (DAT_PVOID)&cached_pg_rank);
                            if (status != DAT_SUCCESS) {
                                 MPIU_Internal_error_printf("Error: fail to accept cr after ep_reset");    
                            }
                        } else{
                            printf("Error in connection establishment: "
                                   "vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI, "
                                   "ep_state=%d\n", param.ep_state);
                        }
                    }

#ifdef SOLARIS
                    do {
                        status = dat_evd_wait(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], 
                                              DAT_TIMEOUT_INFINITE,
                                              1, &event, &nmore);


                        if (status != DAT_SUCCESS) continue;

                        if (event.event_number != DAT_CONNECTION_EVENT_ESTABLISHED)
                            continue;

                        peer = *((int *)event.event_data.connect_event_data.private_data);

                        if (peer == i) {

                        /* the event generated by accept above */
                            MRAILI_Init_vc (vc, cached_pg_rank);
                            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                            MPIDI_CH3I_Process.new_conn_complete = 1;
			    MPIDI_CH3I_Process.num_conn++;

                        } else {
                        /* someone just accepted my request */
                            MPIDI_PG_Get_vc (cached_pg, peer, &peer_vc);
                            MRAILI_Init_vc (peer_vc, cached_pg_rank);
                            peer_vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                            MPIDI_CH3I_Process.new_conn_complete = 1;
			    MPIDI_CH3I_Process.num_conn++;
                        }
                    } while (peer != i);
#else
                    do {
                        size = -1;

                        status = dat_evd_dequeue(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], &event);

                        if (status != DAT_SUCCESS) continue;

                        if (event.event_number != DAT_CONNECTION_EVENT_ESTABLISHED)
                            continue;

                        size = event.event_data.connect_event_data.private_data_size;

                        if (size == 0) {
                        /* the event generated by accept above */
                            MRAILI_Init_vc (vc, cached_pg_rank);
                            vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                            MPIDI_CH3I_Process.new_conn_complete = 1;
			    MPIDI_CH3I_Process.num_conn++;

                        } else {
                        /* someone just accepted my request */
                            peer = *((int *)event.event_data.connect_event_data.private_data);
                            MPIDI_PG_Get_vc (cached_pg, peer, &peer_vc);
                            MRAILI_Init_vc (peer_vc, cached_pg_rank);
                            peer_vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                            MPIDI_CH3I_Process.new_conn_complete = 1;
			    MPIDI_CH3I_Process.num_conn++;
                        }
                    } while (size != 0);
#endif

                } else {
                    /* I'm the client, reject the duplicated request */
                    status = dat_cr_reject(
                        cr_stat.cr_handle
#if DAT_VERSION_MAJOR > 1 
                        , 0,
                        NULL
#endif /* if DAT_VERSION_MAJOR > 1 */
                    );

                    if (status != DAT_SUCCESS) {
                        MPIU_Internal_error_printf("Error: fail to reject connection req.\n");
                    } 
                }
            }    

            MPICM_unlock();

          } /* if */

      }/* while */
}

int MPIDI_CH3I_CM_Finalize()
{
    return MPIDI_CH3I_RDMA_finalize();
}

int MPIDI_CH3I_CM_Connect(MPIDI_VC_t * vc)
{
    int size, peer, count=0, limit=1000, nmore;
    DAT_RETURN ret, ret_conn; 
    DAT_EVENT event;
    
    MPICM_lock();

    if (vc->ch.state != MPIDI_CH3I_VC_STATE_UNCONNECTED) {
        MPICM_unlock();
        return MPI_SUCCESS;
    }
    if (vc->pg_rank == cached_pg_rank) {
        MPICM_unlock();
        return MPI_SUCCESS;
    }

    /*Create qps*/
    cm_ep_create(vc);

    vc->ch.state = MPIDI_CH3I_VC_STATE_CONNECTING_CLI;

    MPICM_unlock();

    MPICM_lock_udapl();
    ret_conn = dat_ep_connect (vc->mrail.qp_hndl[0], 
                          &rdma_iba_addr_table.ia_addr[vc->pg_rank][0], 
                          rdma_iba_addr_table.service_id[vc->pg_rank][0], 
                          DAT_TIMEOUT_INFINITE, 
                          sizeof (int),  /* private_data_size */
                          (DAT_PVOID) & cached_pg_rank,      /* private_data */
                          DAT_QOS_BEST_EFFORT,        /* QoS */
                          DAT_CONNECT_DEFAULT_FLAG);

    MPICM_unlock_udapl();

    MPICM_lock();

    if(ret_conn == DAT_SUCCESS) {
        while (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {

#ifdef SOLARIS
            ret = dat_evd_wait(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], 
                               10, 1, &event, &nmore);
#else
            ret = dat_evd_dequeue(MPIDI_CH3I_RDMA_Process.conn_cq_hndl[0], &event);
#endif

            if (ret != DAT_SUCCESS) {
                if(++count > limit) {
                    MPICM_unlock();
                    sched_yield();
                    MPICM_lock();
                    count = 0;
                }
                continue;
            }

            if (event.event_number == DAT_CONNECTION_EVENT_ESTABLISHED) {

                size = event.event_data.connect_event_data.private_data_size;

                MPIU_Assert (size != 0);
                /* someone just accepted my request */
                peer = *((int *)event.event_data.connect_event_data.private_data);
                MPIU_Assert (peer == vc->pg_rank);
                MRAILI_Init_vc (vc, cached_pg_rank);
                vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
                MPIDI_CH3I_Process.new_conn_complete = 1;
		MPIDI_CH3I_Process.num_conn++;

            } else {
                ret = dat_ep_reset (vc->mrail.qp_hndl[0]);
                CHECK_RETURN (ret, "Could not reset ep");
                while (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
                    MPICM_unlock();
                    sched_yield();
                    MPICM_lock();
                }
                break;
           }
       } 
    } else {
        while (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE) {
            MPICM_unlock();
            sched_yield();
            MPICM_lock();
        }
    }

    MPICM_unlock();

    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Establish(MPIDI_VC_t * vc)
{
    return MPI_SUCCESS;
}

int MPIDI_CH3I_MRAIL_PG_Destroy(MPIDI_PG_t *pg)
{
    return MPI_SUCCESS;   
}

int MPIDI_CH3I_MRAIL_PG_Init(MPIDI_PG_t *pg)
{
    return MPI_SUCCESS;
}

int MPIDI_CH3I_CM_Get_port_info(char *ifname, int max_len)
{
    return MPI_ERR_SPAWN;
}

int MPIDI_CH3I_CM_Connect_raw_vc(MPIDI_VC_t *vc, char *ifname)
{
    return MPI_ERR_SPAWN;
}
