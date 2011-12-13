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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "rdma_impl.h"
#include "mpidi_ch3i_rdma_conf.h"
#include "mem_hooks.h"
#include "pmi.h"
#include "vbuf.h"
#include "ibv_param.h"
#include "rdma_cm.h"
#include "mpiutil.h"
#include "cm.h"
#include "dreg.h"

/* For mv2_system_report */
#include "sysreport.h"

/* global rdma structure for the local process */
MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;
char ufile[512];
int ring_setup_done = 0;

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_PG_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAIL_PG_Init(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;

    pg->ch.mrail.cm_ah = NULL;
    pg->ch.mrail.cm_ud_qpn = NULL;
    pg->ch.mrail.cm_lid = NULL;
    pg->ch.mrail.cm_gid = NULL;
#ifdef _ENABLE_XRC_
    pg->ch.mrail.xrc_hostid = NULL;
#endif

    pg->ch.mrail.cm_ah = MPIU_Malloc(pg->size * sizeof(struct ibv_ah *));
    if (!pg->ch.mrail.cm_ah) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**nomem",
                                  "**nomem %s", "cm_ah");
    }
    MPIU_Memset(pg->ch.mrail.cm_ah, 0, pg->size * sizeof(struct ibv_ah *));

    pg->ch.mrail.cm_ud_qpn = MPIU_Malloc(pg->size * sizeof(uint32_t));
    if (!pg->ch.mrail.cm_ud_qpn) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**nomem",
                                  "**nomem %s", "cm_ud_qpn");
    }
    MPIU_Memset(pg->ch.mrail.cm_ud_qpn, 0, pg->size * sizeof(uint32_t));

    pg->ch.mrail.cm_lid = MPIU_Malloc(pg->size * sizeof(uint16_t));
    if (!pg->ch.mrail.cm_lid) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**nomem",
                                  "**nomem %s", "cm_lid");
    }
    MPIU_Memset(pg->ch.mrail.cm_lid, 0, pg->size * sizeof(uint16_t));

	pg->ch.mrail.cm_gid = MPIU_Malloc(pg->size * sizeof(union ibv_gid));
	if (!pg->ch.mrail.cm_gid) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**nomem",
	                              "**nomem %s", "cm_gid");
	}
	MPIU_Memset(pg->ch.mrail.cm_gid, 0, pg->size * sizeof(union ibv_gid));

#ifdef _ENABLE_XRC_
    pg->ch.mrail.xrc_hostid = MPIU_Malloc (pg->size * sizeof(uint32_t));
    if (!pg->ch.mrail.xrc_hostid) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_INTERN, "**nomem",
                                  "**nomem %s", "xrc_hostid");
    }
    memset(pg->ch.mrail.xrc_hostid, 0, pg->size * sizeof(uint32_t));
#endif

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAIL_PG_Destroy
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAIL_PG_Destroy(MPIDI_PG_t *pg)
{
    if (pg->ch.mrail.cm_ah)
        MPIU_Free(pg->ch.mrail.cm_ah);
    if (pg->ch.mrail.cm_ud_qpn)
        MPIU_Free(pg->ch.mrail.cm_ud_qpn);
    if (pg->ch.mrail.cm_lid)
        MPIU_Free(pg->ch.mrail.cm_lid);
    if (pg->ch.mrail.cm_gid)
        MPIU_Free(pg->ch.mrail.cm_gid);
#ifdef _ENABLE_XRC_
    if (pg->ch.mrail.xrc_hostid)
        MPIU_Free (pg->ch.mrail.xrc_hostid);
#endif
    return MPI_SUCCESS;
}

static inline uint16_t get_local_lid(struct ibv_context *ctx, int port)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(ctx, port, &attr)) {
	return -1;
    }

    MPIDI_CH3I_RDMA_Process.lmc = attr.lmc;

    return attr.lid;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_init(MPIDI_PG_t * pg, int pg_rank)
{

    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */

    MPIDI_VC_t* vc = NULL;
    int i = 0;
    int error;
    int rail_index;
    char* key = NULL;
    char* val = NULL;
    int key_max_sz;
    int val_max_sz;

    char rdmakey[512];
    char rdmavalue[512];
    char* buf = NULL;
    int mpi_errno = MPI_SUCCESS;
    struct process_init_info *init_info = NULL;
    uint32_t my_hca_type;
    int pg_size;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_INIT);

    pg_size = MPIDI_PG_Get_size(pg);

    rdma_get_pm_parameters(&MPIDI_CH3I_RDMA_Process);
    
    if((pg_size > 1) && ((!using_mpirun_rsh) || MPIDI_CH3I_RDMA_Process.has_ring_startup)) {
        mpi_errno = rdma_setup_startup_ring(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        /* Host ids exchanges through PMI in MPIDI_Get_local_host for
        ** mpirun_rsh. for all other launchers do it here */
        if(!using_mpirun_rsh) {
            rdma_ring_exchange_host_id(pg, pg_rank, pg_size);
        }
        ring_setup_done = 1;
    }

    rdma_local_id = MPIDI_Get_local_process_id(pg);
    rdma_num_local_procs = MPIDI_Num_local_processes(pg);

    /* Reading the values from user first and 
     * then allocating the memory */
    if ((mpi_errno = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);

#if !defined(DISABLE_PTMALLOC)
    if (mvapich2_minit()) {
        MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister = 0;
    }
#else /* !defined(DISABLE_PTMALLOC) */
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
    MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister = 0;
#endif /* !defined(DISABLE_PTMALLOC) */

    if(!MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
        rdma_r3_threshold = rdma_r3_threshold_nocache;
    }

    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
    rdma_num_rails_per_hca = rdma_num_ports * rdma_num_qp_per_port;

    if (rdma_multirail_usage_policy == MV2_MRAIL_SHARING) {
        if (mrail_use_default_mapping) {
            rdma_process_binding_rail_offset = rdma_num_rails_per_hca*
                                                (rdma_local_id % rdma_num_hcas);
        } else {
            rdma_process_binding_rail_offset = rdma_num_rails_per_hca *
                                                mrail_user_defined_p2r_mapping;
        }
    }

    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d, "
                "rdma_num_rails_per_hca = %d, "
                "rdma_process_binding_rail_offset = %d\n", rdma_num_qp_per_port,
	            rdma_num_rails, rdma_num_rails_per_hca,
                rdma_process_binding_rail_offset);

    init_info = alloc_process_init_info(pg_size, rdma_num_rails);
    if (!init_info) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
                        "**nomem %s", "init_info");
    }

    if (pg_size > 1) {
        if (MPIDI_CH3I_RDMA_Process.has_ring_startup) {
            my_hca_type = MPIDI_CH3I_RDMA_Process.hca_type;
            mpi_errno = rdma_ring_based_allgather(&my_hca_type, sizeof my_hca_type,
                                        pg_rank, init_info->hca_type, pg_size, 
                                        &MPIDI_CH3I_RDMA_Process);

	        if (mpi_errno) {
	            MPIU_ERR_POP(mpi_errno);
	        }
            /* Check heterogenity */
            rdma_param_handle_heterogenity(init_info->hca_type, pg_size);
        }
    }

    if (MPIDI_CH3I_RDMA_Process.has_apm) {
        init_apm_lock(); 
    }

    if (MPIDI_CH3I_RDMA_Process.has_srq) {
	    mpi_errno = init_vbuf_lock();
	    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    /* the vc structure has to be initialized */

    for (i = 0; i < pg_size; ++i) {

	    MPIDI_PG_Get_vc(pg, i, &vc);
	    MPIU_Memset(&(vc->mrail), 0, sizeof(vc->mrail));

	    /* This assmuption will soon be done with */
	    vc->mrail.num_rails = rdma_num_rails;
    }

    /* Open the device and create cq and qp's */
    if ((mpi_errno = rdma_iba_hca_init(
        &MPIDI_CH3I_RDMA_Process,
        pg_rank,
        pg,
        init_info)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

    if (pg_size > 1) {

	/* Exchange the information about HCA_lid / HCA_gid and qp_num */
	if (!MPIDI_CH3I_RDMA_Process.has_ring_startup) {
	    /* Allocate space for pmi keys and values */
	    error = PMI_KVS_Get_key_length_max(&key_max_sz);
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			    "**fail %s", "Error getting max key length");
	    }

	    ++key_max_sz;
	    key = MPIU_Malloc(key_max_sz);

	    if (key == NULL) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
			    "**nomem %s", "pmi key");
	    }

	    error = PMI_KVS_Get_value_length_max(&val_max_sz);
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			    "**fail %s", "Error getting max value length");
	    }

	    ++val_max_sz;
	    val = MPIU_Malloc(val_max_sz);

	    if (val == NULL) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,"**nomem",
			    "**nomem %s", "pmi value");
	    }

	    /* For now, here exchange the information of each LID separately */
	    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i) {
            continue;
        }
	    /* Generate the key and value pair */
	    MPIU_Snprintf(rdmakey, 512, "%08x-%08x", pg_rank, i);
	    buf = rdmavalue;

	    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            if (use_iboeth) {
	            sprintf(buf, "%016"SCNx64,
                      init_info->gid[i][rail_index].global.subnet_prefix);
	            buf += 16;
	            sprintf(buf, "%016"SCNx64,
                       init_info->gid[i][rail_index].global.interface_id);
	            buf += 16;
	            DEBUG_PRINT("[%d] put subnet prefix = %"PRIx64
                            " interface id = %"PRIx64"\r\n", pg_rank,
                            init_info->gid[i][rail_index].global.subnet_prefix,
                            init_info->gid[i][rail_index].global.interface_id);
            } else {
	            sprintf(buf, "%08x", init_info->lid[i][rail_index]);
	            DEBUG_PRINT("put my hca %d lid %d\n", rail_index,
		                init_info->lid[i][rail_index]);
	            buf += 8;
            }
	    }

        sprintf(buf, "%08x", init_info->hca_type[i]);
        buf += 8;
        sprintf(buf, "%016"SCNx64, init_info->vc_addr[i]);
        buf += 16;
        DEBUG_PRINT("Put hca type %d, vc addr %"PRIx64", max val%d\n", 
                init_info->hca_type[i], init_info->vc_addr[i],
                val_max_sz);

	    /* put the kvs into PMI */
	    MPIU_Strncpy(key, rdmakey, key_max_sz);
	    MPIU_Strncpy(val, rdmavalue, val_max_sz);
        DEBUG_PRINT("rdmavalue %s len:%lu \n", val,strlen(val));

	    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
	    if (error != PMI_SUCCESS) {
	        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		        "**pmi_kvs_put", "**pmi_kvs_put %d", error);
	    }

	    DEBUG_PRINT("after put, before barrier\n");

	    error = PMI_KVS_Commit(pg->ch.kvs_name);

	    if (error != PMI_SUCCESS) {
	        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		        "**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
	    }
        
        /* 
        ** This barrer is placed here because PMI is not allowing to put
        ** multiple key-value pairs. otherwise this berrier is not required.
        ** This should be moved out of this loop if PMI allows multiple pairs.
        */
	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
	    }
        }


	    /* Here, all the key and value pairs are put, now we can get them */
	    for (i = 0; i < pg_size; i++) {
            rail_index = 0;
		    if (pg_rank == i) {
                if (!use_iboeth) {
		            init_info->lid[i][0] =
			               get_local_lid(MPIDI_CH3I_RDMA_Process.nic_context[0],
                                            rdma_default_port);
                    init_info->hca_type[i] = MPIDI_CH3I_RDMA_Process.hca_type;
                }
	            DEBUG_PRINT("[%d] get subnet prefix = %"PRIx64", interface id = %"PRIx64
                       " from proc %d \n", pg_rank,
                       init_info->gid[i][rail_index].global.subnet_prefix,
                       init_info->gid[i][rail_index].global.interface_id, i);
		        continue;
		    }

		    /* Generate the key */
		    MPIU_Snprintf(rdmakey, 512, "%08x-%08x", i, pg_rank);
		    MPIU_Strncpy(key, rdmakey, key_max_sz);

		    error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
		    if (error != PMI_SUCCESS) {
		        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			        "**pmi_kvs_get", "**pmi_kvs_get %d", error);
		    }

		    MPIU_Strncpy(rdmavalue, val, val_max_sz);
		    buf = rdmavalue;

		    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
                if (use_iboeth) {
	                sscanf(buf, "%016"SCNx64, (uint64_t*)
                           &init_info->gid[i][rail_index].global.subnet_prefix);
	                buf += 16;
	                sscanf(buf, "%016"SCNx64, (uint64_t*)
                           &init_info->gid[i][rail_index].global.interface_id);
	                buf += 16;
	                DEBUG_PRINT("[%d] get subnet prefix = %"PRIx64
                            "interface id = %"PRIx64" from proc %d\n", pg_rank,
                        init_info->gid[i][rail_index].global.subnet_prefix,
                        init_info->gid[i][rail_index].global.interface_id, i);
                } else {
		            sscanf(buf, "%08x", 
                            (unsigned int *)&init_info->lid[i][rail_index]);
		            buf += 8;
		            DEBUG_PRINT("get rail %d, lid %08d\n", rail_index,
			                    (int)init_info->lid[i][rail_index]);
                }
		    }

            sscanf(buf, "%08x", &init_info->hca_type[i]);
            buf += 8;
            sscanf(buf, "%016"SCNx64, &init_info->vc_addr[i]);
            buf += 16;
            DEBUG_PRINT("Get vc addr %"PRIx64"\n", init_info->vc_addr[i]);
	    }

	    /* This barrier is to prevent some process from
	     * overwriting values that has not been get yet
         */
	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
	    }

	    /* STEP 2: Exchange qp_num and vc addr */
	    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i) {
            continue;
        }
	        /* Generate the key and value pair */
	        MPIU_Snprintf(rdmakey, 512, "1-%08x-%08x", pg_rank, i);
	   	buf = rdmavalue;

		for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
		    sprintf(buf, "%08X", init_info->qp_num_rdma[i][rail_index]);
		    buf += 8;
		    DEBUG_PRINT("target %d, put qp %d, num %08X \n", i,
			    rail_index, init_info->qp_num_rdma[i][rail_index]);
		    DEBUG_PRINT("[%d] %s(%d) put qp %08X \n", pg_rank, __FUNCTION__,
                         __LINE__, init_info->qp_num_rdma[i][rail_index]);
		}

		DEBUG_PRINT("put rdma value %s\n", rdmavalue);
		/* Put the kvs into PMI */
		MPIU_Strncpy(key, rdmakey, key_max_sz);
		MPIU_Strncpy(val, rdmavalue, val_max_sz);

		error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_put", "**pmi_kvs_put %d", error);
		}

		error = PMI_KVS_Commit(pg->ch.kvs_name);
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
		}

        /* 
        ** This barrer is placed here because PMI is not allowing to put
        ** multiple key-value pairs. otherwise this berrier is not required.
        ** This should be moved out of this loop if PMI allows multiple pairs.
        */
            
	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
	    }
        }


	    /* Here, all the key and value pairs are put, now we can get them */
	    for (i = 0; i < pg_size; i++) {
		    if (pg_rank == i) {
		        continue;
		    }

		    /* Generate the key */
		    MPIU_Snprintf(rdmakey, 512, "1-%08x-%08x", i, pg_rank);
		    MPIU_Strncpy(key, rdmakey, key_max_sz);
		    error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);

		    if (error != PMI_SUCCESS) {
		        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			        "**pmi_kvs_get", "**pmi_kvs_get %d", error);
		    }
		    MPIU_Strncpy(rdmavalue, val, val_max_sz);

		    buf = rdmavalue;
		    DEBUG_PRINT("get rdmavalue %s\n", rdmavalue);
		    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
		        sscanf(buf, "%08X", &init_info->qp_num_rdma[i][rail_index]);
		        buf += 8;
		        DEBUG_PRINT("[%d] %s(%d) get qp %08X from %d\n", pg_rank,
                             __FUNCTION__, __LINE__,
			                init_info->qp_num_rdma[i][rail_index], i);
		    }
	    }

	    error = PMI_Barrier();
	    if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", error);
	    }

	    DEBUG_PRINT("After barrier\n");

	} else {
	    /* Exchange the information about HCA_lid, qp_num, and memory,
	     * With the ring-based queue pair */
            mpi_errno = rdma_ring_boot_exchange(&MPIDI_CH3I_RDMA_Process, pg,
                                             pg_rank, init_info);
           if(mpi_errno) {
              MPIU_ERR_POP(mpi_errno)
           }
	}
    }

    /* Initialize the registration cache */
    mpi_errno = dreg_init();

    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }

    /* Allocate RDMA Buffers */
    mpi_errno = rdma_iba_allocate_memory(
            &MPIDI_CH3I_RDMA_Process,
            pg_rank,
            pg_size);

    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }

    /* Enable all the queue pair connections */
    DEBUG_PRINT ("Address exchange finished, proceed to enabling connection\n");
    rdma_iba_enable_connections(&MPIDI_CH3I_RDMA_Process, pg_rank,
	                            pg, init_info);
    DEBUG_PRINT("Finishing enabling connection\n");

    /*barrier to make sure queues are initialized before continuing */
    /*  error = bootstrap_barrier(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
     *  */
    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		    "**pmi_barrier", "**pmi_barrier %d", error);
    }

    /* Prefill post descriptors */
    for (i = 0; i < pg_size; i++) {
	    MPIDI_PG_Get_vc(pg, i, &vc);

 	    if (i == pg_rank) {
	        continue;
	    }

	    if (!qp_required(vc, pg_rank, i)) {
 	        vc->state = MPIDI_VC_STATE_ACTIVE;
  	        MPIDI_CH3I_SMP_Init_VC(vc);
   	        continue;
 	    }

	    vc->state = MPIDI_VC_STATE_ACTIVE;
	    MRAILI_Init_vc(vc);
    }

    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		    "**pmi_barrier", "**pmi_barrier %d", error);
    }

    if (ring_setup_done) {
        /* clean up the bootstrap qps and free memory */
        if ((mpi_errno = rdma_cleanup_startup_ring(&MPIDI_CH3I_RDMA_Process))
             != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }
    }

    /* If requested produces a report on the system.  See sysreport.h and sysreport.c */
    if (enable_sysreport) {
        mv2_system_report();
    }


    DEBUG_PRINT("Finished MPIDI_CH3I_RDMA_init()\n");

fn_exit:
    if (init_info)
    {
        free_process_init_info(init_info, pg_size);
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Flush
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Flush(void)
{
    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int i, pg_rank, pg_size, rail;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);

    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);

    for (i = 0; i < pg_size; i++)
    {
        if (i == pg_rank)
        {
	    continue;
	}

	MPIDI_PG_Get_vc(pg, i, &vc);

	/* Skip SMP VCs */
	if (SMP_INIT && (vc->smp.local_nodes >= 0))
        {
	    continue;
        }

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE)
        {
	    continue;
	}

	for (rail = 0; rail < vc->mrail.num_rails; rail++)
        {
	    while (0 != vc->mrail.srp.credits[rail].backlog.len)
            {
                if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
	    }

	    while (NULL != vc->mrail.rails[rail].ext_sendq_head)
            {
                if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
                {
		    MPIU_ERR_POP(mpi_errno);
                }
	    }
	}
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_MRAILI_FLUSH);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_RDMA_finalize(void)
{
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    int error;
    int pg_rank;
    int pg_size;
    int i;
    int rail_index;
    int hca_index;

    MPIDI_PG_t *pg;
    MPIDI_VC_t *vc;
    int err;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);

    /* Insert implementation here */
    pg = MPIDI_Process.my_pg;
    pg_rank = MPIDI_Process.my_pg_rank;
    pg_size = MPIDI_PG_Get_size(pg);
    
    /* Show memory usage statistics */
    if (DEBUG_MEM_verbose) {
        if(pg_rank == 0 || DEBUG_MEM_verbose > 1) {
            print_mem_usage();
            print_vbuf_usage();
        }
    }

    /* make sure everything has been sent */
    if (!use_iboeth && (rdma_3dtorus_support || rdma_path_sl_query)) {
        mv2_release_3d_torus_resources();
    }

    if (ring_setup_done) {
        ring_rdma_close_hca(&MPIDI_CH3I_RDMA_Process);
    }

    MPIDI_CH3I_MRAILI_Flush();
    for (i = 0; i < pg_size; i++) {
	MPIDI_PG_Get_vc(pg, i, &vc);

	if (i == pg_rank || !qp_required(vc, pg_rank, i)) {
	    continue;
	}

	for (rail_index = 0; rail_index < vc->mrail.num_rails;
		rail_index++) {
	    while((rdma_default_max_send_wqe) != 
		    vc->mrail.rails[rail_index].send_wqes_avail) {
		MPIDI_CH3I_Progress_test();
	    } 
	}
    }

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

    /*barrier to make sure queues are initialized before continuing */
    error = PMI_Barrier();

    if (error != PMI_SUCCESS) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", error);
    }

    for (i = 0; i < pg_size; i++) {
		MPIDI_PG_Get_vc(pg, i, &vc);

		if (i == pg_rank || !qp_required(vc, pg_rank, i)) {
		    continue;
		}
	
		for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
		    if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index]) {
			err = ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
			if (err)
			    MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
		    }
		    if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]) {
			err = ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
			if (err)
			    MPIU_Error_printf("Failed to deregister mr (%d)\n", err);
		    }
		}
	
		if (vc->mrail.rfp.RDMA_send_buf_DMA)
		    MPIU_Free(vc->mrail.rfp.RDMA_send_buf_DMA);
		if (vc->mrail.rfp.RDMA_recv_buf_DMA)
		    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf_DMA);
		if (vc->mrail.rfp.RDMA_send_buf)
		    MPIU_Free(vc->mrail.rfp.RDMA_send_buf);
		if (vc->mrail.rfp.RDMA_recv_buf)
		    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf);
    }

    /* STEP 2: destroy all the qps, tears down all connections */
    for (i = 0; i < pg_size; i++) {
		MPIDI_PG_Get_vc(pg, i, &vc);

        if (!qp_required(vc, pg_rank, i)) {
            continue;
        }

#ifndef MV2_DISABLE_HEADER_CACHING
		MPIU_Free(vc->mrail.rfp.cached_incoming);
		MPIU_Free(vc->mrail.rfp.cached_outgoing);
#endif

		for (rail_index = 0; rail_index < vc->mrail.num_rails; rail_index++) {
	    	err = ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
	    	if (err) {
				MPIU_Error_printf("Failed to destroy QP (%d)\n", err);
			}
		}
    }


    /* STEP 3: release all the cq resource, 
     * release all the unpinned buffers, 
     * release the ptag and finally, 
     * release the hca */

    for (i = 0; i < rdma_num_hcas; i++) {
	if (MPIDI_CH3I_RDMA_Process.has_srq) {
        
        /* Signal thread if waiting */
        pthread_mutex_lock(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i]);
        *((volatile int*)&MPIDI_CH3I_RDMA_Process.is_finalizing ) = 1;
        pthread_cond_signal(&MPIDI_CH3I_RDMA_Process.srq_post_cond[i]);
        pthread_mutex_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i]);
        
        /* wait for async thread to finish processing */
        pthread_mutex_lock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
        
        /* destroy mutex and cond and cancel thread */
        pthread_cond_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_cond[i]);
        pthread_mutex_destroy(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[i]);
        pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);

        pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i], NULL);

        err = ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
        pthread_mutex_unlock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
        pthread_mutex_destroy(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i]);
        if (err)
            MPIU_Error_printf("Failed to destroy SRQ (%d)\n", err);
    }

	err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);
	if (err)
	    MPIU_Error_printf("Failed to destroy CQ (%d)\n", err);

    if (MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]) {
	    err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]);
	    if (err) {
	        MPIU_Error_printf("Failed to destroy CQ (%d)\n", err);
        }
    }

    if (MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]) {
	    err = ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]);
	    if (err) {
	        MPIU_Error_printf("Failed to destroy CQ (%d)\n", err);
        }
    }

	if(rdma_use_blocking) {
	    err = ibv_destroy_comp_channel(MPIDI_CH3I_RDMA_Process.comp_channel[i]);
	    if(err)
		MPIU_Error_printf("Failed to destroy CQ channel (%d)\n", err);
	    }

	deallocate_vbufs(i);
    } 

    deallocate_vbuf_region();
    dreg_finalize();

    for (i = 0; i < rdma_num_hcas; i++) {
        err = ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
        if (err)  {
            MPIU_Error_printf("[%d] Failed to dealloc pd (%s)\n", 
                pg_rank, strerror(errno));
        }
        err = ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
        if (err) {
            MPIU_Error_printf("[%d] Failed to close ib device (%s)\n", 
                pg_rank, strerror(errno));
        }
    }

    if(MPIDI_CH3I_RDMA_Process.polling_set != NULL) {
      MPIU_Free(MPIDI_CH3I_RDMA_Process.polling_set);
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_FINALIZE);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#ifdef _ENABLE_XRC_
#undef FUNCNAME
#define FUNCNAME mv2_xrc_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static int mv2_xrc_init (MPIDI_PG_t * pg) 
{
    int i, mpi_errno = MPI_SUCCESS;
    char xrc_file[512];
    PRINT_DEBUG(DEBUG_XRC_verbose>0, "Init XRC Start\n");
    MPIDI_CH3I_RDMA_Process.xrc_rdmafp = 1;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;
    MPIDI_STATE_DECL(MPID_STATE_CH3I_MV2_XRC_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_CH3I_MV2_XRC_INIT);

    if (!MPIDI_CH3I_Process.has_dpm) {
        memset(ufile, 0, sizeof(ufile));
        sprintf(ufile, "mv2_xrc_%s_%d", pg->ch.kvs_name, getuid());
    }

    if (ufile == NULL) {
        ibv_error_abort (GEN_EXIT_ERR, "Can't get unique filename");
    }

    for(i = 0; i < rdma_num_hcas; i++) {
        sprintf (xrc_file, "/dev/shm/%s-%d", ufile, i);
        PRINT_DEBUG(DEBUG_XRC_verbose>0, "Opening xrc file: %s\n", xrc_file);
        proc->xrc_fd[i] = open (xrc_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
        if (proc->xrc_fd[i] < 0) {
            MPIU_ERR_SETFATALANDJUMP2(
                    mpi_errno,
                    MPI_ERR_INTERN,
                    "**fail",
                    "%s: %s",
                    "open",
                    strerror(errno));
        }

        proc->xrc_domain[i] = ibv_open_xrc_domain (proc->nic_context[i], 
                proc->xrc_fd[i], O_CREAT);

        if (NULL == proc->xrc_domain[i]) {
            perror ("xrc_domain");
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_INTERN,"**fail",
                    "**fail %s","Can't open XRC domain");
        }
    }
    if (!MPIDI_CH3I_Process.has_dpm) {
        if (PMI_Barrier () != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**pmi_barrier", "**pmi_barrier %d", mpi_errno);
		}
        unlink (xrc_file);
    }

    PRINT_DEBUG(DEBUG_XRC_verbose>0, "Init XRC DONE\n");
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_CH3I_MV2_XRC_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#endif /* _ENABLE_XRC_ */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Init(MPIDI_PG_t * pg, int pg_rank, char **conn_info_ptr)
{
    /* Initialize the rdma implemenation. */
    /* This function is called after the RDMA channel has initialized its 
       structures - like the vc_table. */
    MPIDI_VC_t *vc;
    int pg_size;
    int i, error;
    int key_max_sz;
    int val_max_sz;
    int *hosts = NULL;
    uint32_t *ud_qpn_all;
    uint32_t ud_qpn_self;
    uint16_t *lid_all;
    union ibv_gid *gid_all;
    uint32_t *hca_type_all;
    uint32_t my_hca_type;
    int mpi_errno = MPI_SUCCESS;
    ud_addr_info_t self_info;
    ud_addr_info_t *all_info;
    char hostname[HOSTNAME_LEN + 1];
    int hostid;
    int result;
    struct hostent *hostent;

    MPIDI_STATE_DECL(MPID_STATE_CH3I_CM_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_CH3I_CM_INIT);

#ifndef DISABLE_PTMALLOC
    if (mvapich2_minit()) {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		    "**fail %s", "Error initializing MVAPICH2 ptmalloc2 library");
    }
#else
    mallopt(M_TRIM_THRESHOLD, -1);
    mallopt(M_MMAP_MAX, 0);
#endif

    pg_size = MPIDI_PG_Get_size(pg);

    rdma_get_pm_parameters(&MPIDI_CH3I_RDMA_Process);

#if defined(RDMA_CM)
    /* We cann't setup UD Ring for iWARP devices. Use PMI here for hostid exchange
    ** in the case of hydra. Consider using hydra process mapping in the next
    ** release as it is not correct in the current release.
    */
    if(MPIDI_CH3I_RDMA_Process.use_rdma_cm && !using_mpirun_rsh ) {
        rdma_cm_exchange_hostid(pg, pg_rank, pg_size);
    } else 
#endif 
    if((pg_size > 1) && ((!using_mpirun_rsh) || MPIDI_CH3I_RDMA_Process.has_ring_startup)) {
        mpi_errno = rdma_setup_startup_ring(&MPIDI_CH3I_RDMA_Process, pg_rank, pg_size);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        /* Host ids exchanges through PMI in MPIDI_Get_local_host for
        ** mpirun_rsh. for all other launchers do it here */
        if(!using_mpirun_rsh) {
            rdma_ring_exchange_host_id(pg, pg_rank, pg_size);
        }
        ring_setup_done = 1;
    }

    rdma_local_id = MPIDI_Get_local_process_id(pg);
    rdma_num_local_procs = MPIDI_Num_local_processes(pg);

    /* Reading the values from user first and 
     * then allocating the memory */
    mpi_errno = rdma_get_control_parameters(&MPIDI_CH3I_RDMA_Process);
    if (mpi_errno) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "rdma_get_control_parameters");
    }

    rdma_set_default_parameters(&MPIDI_CH3I_RDMA_Process);
    rdma_get_user_parameters(pg_size, pg_rank);

    /*
     * TODO Local Memory, must free if error occurs
     */
    lid_all        = (uint16_t *) MPIU_Malloc(pg_size * sizeof(uint16_t));
    gid_all        = (union ibv_gid *)
                     MPIU_Malloc(pg_size * sizeof(union ibv_gid));
    ud_qpn_all     = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));
    hca_type_all   = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));

    if (!ud_qpn_all || !lid_all || !hca_type_all || !gid_all) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
                "**nomem %s", "structure to exchange information");
    }

    for (i = 0; i < rdma_num_hcas; i++) {
        pthread_mutex_init(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[i], 0); 
    }

    if (pg_size > 1
#if defined(RDMA_CM)
        && !MPIDI_CH3I_RDMA_Process.use_rdma_cm
#endif /* defined(RDMA_CM) */
    ) {
        if (MPIDI_CH3I_RDMA_Process.has_ring_startup) {
            my_hca_type = MPIDI_CH3I_RDMA_Process.hca_type;

            mpi_errno = rdma_ring_based_allgather(&my_hca_type,
								sizeof(my_hca_type), pg_rank, hca_type_all,
								pg_size, &MPIDI_CH3I_RDMA_Process);
            if (mpi_errno) {
                MPIU_ERR_POP(mpi_errno);
            }
            rdma_param_handle_heterogenity(hca_type_all, pg_size);
        }
    }

    rdma_num_rails = rdma_num_hcas * rdma_num_ports * rdma_num_qp_per_port;
    rdma_num_rails_per_hca = rdma_num_ports * rdma_num_qp_per_port;

    if (rdma_multirail_usage_policy == MV2_MRAIL_SHARING) {
        if (mrail_use_default_mapping) {
            rdma_process_binding_rail_offset = rdma_num_rails_per_hca*
                                                (rdma_local_id % rdma_num_hcas);
        } else {
            rdma_process_binding_rail_offset = rdma_num_rails_per_hca *
                                                mrail_user_defined_p2r_mapping;
        }
    }

    DEBUG_PRINT("num_qp_per_port %d, num_rails = %d, "
                "rdma_num_rails_per_hca = %d, "
                "rdma_process_binding_rail_offset = %d\n", rdma_num_qp_per_port,
	            rdma_num_rails, rdma_num_rails_per_hca,
                rdma_process_binding_rail_offset);

    if (MPIDI_CH3I_RDMA_Process.has_apm) {
        init_apm_lock();
    }

    mpi_errno = init_vbuf_lock();
    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	}

    /* the vc structure has to be initialized */
    for (i = 0; i < pg_size; i++) {
		MPIDI_PG_Get_vc(pg, i, &vc);
		MPIU_Memset(&(vc->mrail), 0, sizeof(vc->mrail));
		/* This assmuption will soon be done with */
		vc->mrail.num_rails = rdma_num_rails;
    }

#ifdef RDMA_CM
    if (MPIDI_CH3I_RDMA_Process.use_rdma_cm) {
        mpi_errno = rdma_iba_hca_init(
                &MPIDI_CH3I_RDMA_Process,
                pg_rank,
                pg,
                NULL);
        
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }

		hosts = rdma_cm_get_hostnames(pg_rank, pg);
		if (!hosts) {
            MPIU_Error_printf("Error obtaining hostnames\n");
        }

    	if (MV2_IS_CHELSIO_IWARP_CARD(MPIDI_CH3I_RDMA_Process.hca_type)) {
	        /* TRAC Ticket #455 */
	        if (g_num_smp_peers + 1 < pg_size) {
	  	        int avail_cq_entries = 0;
	  	        avail_cq_entries = rdma_default_max_cq_size /
	  	                ((pg_size - g_num_smp_peers - 1)*rdma_num_rails);
	  	        avail_cq_entries = avail_cq_entries -
	  			  					rdma_initial_prepost_depth - 1;
	  	        if(avail_cq_entries < rdma_prepost_depth) {  
	  	            rdma_prepost_depth = avail_cq_entries;
	  	        }
	  	        MPIDI_CH3I_RDMA_Process.global_used_recv_cq = 
	  	             (rdma_prepost_depth + rdma_initial_prepost_depth + 1)
	  	             *(pg_size - g_num_smp_peers - 1);
	        }
        }

        if (g_num_smp_peers + 1 < pg_size) {
            /* Initialize the registration cache. */
            mpi_errno = dreg_init();

            if (mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_POP(mpi_errno);
            }

            mpi_errno = rdma_iba_allocate_memory(
                    &MPIDI_CH3I_RDMA_Process,
                    pg_rank,
                    pg_size);

            if (mpi_errno != MPI_SUCCESS) {
                MPIU_ERR_POP(mpi_errno);
            }
        }
    }
#endif

    /* Open the device and create cq and qp's */
#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm) {
#endif /* defined(RDMA_CM) */

#ifdef _ENABLE_XRC_
        if (USE_XRC) {
            mpi_errno = mv2_xrc_init (pg);
	        if (mpi_errno) {
	            MPIU_ERR_POP(mpi_errno);
	        }
        }
#endif /* _ENABLE_XRC_ */
        if ((mpi_errno = rdma_iba_hca_init_noqp(
                &MPIDI_CH3I_RDMA_Process,
                pg_rank,
                pg_size)) != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
	    }
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

    MPIDI_CH3I_RDMA_Process.maxtransfersize = RDMA_MAX_RDMA_SIZE;

#ifdef RDMA_CM
    {
	if (MPIDI_CH3I_RDMA_Process.use_rdma_cm) {
	    {
		error = PMI_Barrier();
		if (error != PMI_SUCCESS) {
		    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
			    "**fail", "**fail %s", "PMI Barrier failed");
		}
	    }

	    if ((mpi_errno = rdma_cm_connect_all(hosts, pg_rank, pg)) != MPI_SUCCESS)
        {
             MPIU_ERR_POP(mpi_errno);
	    }

	    if (g_num_smp_peers + 1 == pg_size) {
			MPIDI_CH3I_RDMA_Process.has_one_sided = 0;
        }

	    DEBUG_PRINT("Done RDMA_CM based MPIDI_CH3I_CM_Init()\n");
	    goto fn_exit;
	}
    }
#endif /* RDMA_CM */

    if ((mpi_errno = MPICM_Init_UD(&ud_qpn_self)) != MPI_SUCCESS) {
        MPIU_ERR_POP(mpi_errno);
    }

	result = gethostname(hostname, HOSTNAME_LEN);
	hostent = gethostbyname(hostname);
	hostid = (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;

    if (pg_size > 1)
    {
        if (MPIDI_CH3I_RDMA_Process.has_ring_startup)
        {
		    if (result != 0) {
		        MPIU_ERR_SETFATALANDJUMP1(
	                    mpi_errno,
	                    MPI_ERR_OTHER,
		            	"**fail",
	                    "**fail %s",
	                    "Could not get hostname"
	                );
		    }
	
		    self_info.hostid = hostid;
		    memcpy(&self_info.lid, &MPIDI_CH3I_RDMA_Process.lids,
	                   sizeof(uint16_t)*MAX_NUM_HCAS*MAX_NUM_PORTS);
		    memcpy(&self_info.gid, &MPIDI_CH3I_RDMA_Process.gids,
	                   sizeof(union ibv_gid)*MAX_NUM_HCAS*MAX_NUM_PORTS);
		    self_info.qpn = ud_qpn_self;
		    all_info = (ud_addr_info_t *) MPIU_Malloc(sizeof(ud_addr_info_t)*
														pg_size); 
		    /*will be freed in rdma_cleanup_startup_ring */
	
	        mpi_errno = rdma_ring_based_allgather(&self_info,
	                sizeof(self_info),
	                pg_rank,
	                all_info,
	                pg_size,
	                &MPIDI_CH3I_RDMA_Process);
	
	        if (mpi_errno){
	            MPIU_ERR_POP(mpi_errno);
	        }

		    for (i = 0; i < pg_size; ++i) {
		        MPIDI_PG_Get_vc(pg, i, &vc);
		        vc->smp.hostid = all_info[i].hostid;
		        memcpy(&vc->mrail.lid, &all_info[i].lid,
	                       sizeof(uint16_t)*MAX_NUM_HCAS*MAX_NUM_PORTS);
		        memcpy(&vc->mrail.gid, &all_info[i].gid,
	                       sizeof(union ibv_gid)*MAX_NUM_HCAS*MAX_NUM_PORTS);
#ifdef _ENABLE_XRC_
	            if (USE_XRC) {
	                pg->ch.mrail.xrc_hostid[i] = all_info[i].hostid;
	            }
#endif
				ud_qpn_all[i] = all_info[i].qpn;
				lid_all[i] = all_info[i].lid[0][0];
	            gid_all[i] = all_info[i].gid[0][0];
		    }
		} else {
		    char *key;
		    char *val;
		    /*Exchange the information about HCA_lid and qp_num */
		    /* Allocate space for pmi keys and values */
		    error = PMI_KVS_Get_key_length_max(&key_max_sz);
		    if (error != PMI_SUCCESS) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
				"**fail %s", "Error getting max key length");
		    }
	
		    ++key_max_sz;
		    key = MPIU_Malloc(key_max_sz);
		    if (key == NULL) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
				"**nomem %s", "PMI key");
		    }
	
		    error = PMI_KVS_Get_value_length_max(&val_max_sz);
		    if (error != PMI_SUCCESS) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
				"**fail %s", "Error getting max value length");
		    }
	
		    ++val_max_sz;
		    val = MPIU_Malloc(val_max_sz);
		    if (val == NULL) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
				"**nomem %s", "PMI value");
		    }
	
		    if (key_max_sz < 20 || val_max_sz < 30) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**fail", "**fail %s", "PMI value too small");
		    }
	
		    /*Just put lid for default port and ud_qpn is sufficient*/
		    MPIU_Snprintf(key, key_max_sz, "ud_info_%08d", pg_rank);
	
	        if (!use_iboeth) {
		        MPIU_Snprintf(val, val_max_sz, "%08hx:%08x:%02x:%08x",
			        MPIDI_CH3I_RDMA_Process.lids[0][0], ud_qpn_self,
					MPIDI_CH3I_RDMA_Process.hca_type, hostid);
	        } else {
	            MPIU_Snprintf(val, val_max_sz,
					"%08hx:%08x:%02x:%08x:%016"SCNx64":%016"SCNx64,
	                MPIDI_CH3I_RDMA_Process.lids[0][0], ud_qpn_self,
					MPIDI_CH3I_RDMA_Process.hca_type, hostid,
	                MPIDI_CH3I_RDMA_Process.gids[0][0].global.subnet_prefix, 
	                MPIDI_CH3I_RDMA_Process.gids[0][0].global.interface_id);
	        }

	        DEBUG_PRINT("[%d] Put lids: %08hx ud_qp: %08x hca_type: %02x"
						" hostid: %08x\n", pg_rank,
						MPIDI_CH3I_RDMA_Process.lids[0][0], ud_qpn_self,
						MPIDI_CH3I_RDMA_Process.hca_type,hostid);

		    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
		    if (error != PMI_SUCCESS) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_kvs_put", "**pmi_kvs_put %d", error);
		    }

		    error = PMI_KVS_Commit(pg->ch.kvs_name);
		    if (error != PMI_SUCCESS) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
		    }
	
			error = PMI_Barrier();
			if (error != PMI_SUCCESS) {
				MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				"**pmi_barrier", "**pmi_barrier %d", error);
			}
		
			if (!mv2_on_demand_ud_info_exchange) {

			    for (i = 0; i < pg_size; i++)
		        {
				    if (pg_rank == i) {
		                lid_all[i] = MPIDI_CH3I_RDMA_Process.lids[0][0];
		                ud_qpn_all[i] = ud_qpn_self;
		                hca_type_all[i] = MPIDI_CH3I_RDMA_Process.hca_type;
		                gid_all[i] = MPIDI_CH3I_RDMA_Process.gids[0][0];
		                continue;
				    }

				    MPIU_Snprintf(key, key_max_sz, "ud_info_%08d", i);

				    error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
				    if (error != PMI_SUCCESS) {
				    	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
					    "**pmi_kvs_get", "**pmi_kvs_get %d", error);
				    }

		            if (!use_iboeth) {
				        sscanf(val,"%08hx:%08x:%02x:%08x",
								(uint16_t *)&(lid_all[i]), &(ud_qpn_all[i]),
								&(hca_type_all[i]), &hostid);
		            } else {
				        sscanf(val,"%08hx:%08x:%02x:%08x:%016"SCNx64":%016"SCNx64,
								(uint16_t *)&(lid_all[i]), &(ud_qpn_all[i]),
								&(hca_type_all[i]), &hostid, 
								&(gid_all[i].global.subnet_prefix),
								&(gid_all[i].global.interface_id));
		            }
#ifdef _ENABLE_XRC_
		            if (USE_XRC) {
		                pg->ch.mrail.xrc_hostid[i] = hostid;
		            }
#endif
		            DEBUG_PRINT("[%d<-%d]Get: lid: %08hx, qpn: %08x hca_type:%02x"
								" hostid: %08x\n",pg_rank,i, lid_all[i],
								ud_qpn_all[i], hca_type_all[i], hostid);
			    }
			}
	        MPIU_Free(key); 
	        MPIU_Free(val);
		}
    }
    else
    {
#ifdef _ENABLE_XRC_
        if (USE_XRC) {
	        struct hostent *hostent;
	        char hostname[HOSTNAME_LEN + 1];
	        
            if (gethostname(hostname, HOSTNAME_LEN)) {
	            MPIU_ERR_SETFATALANDJUMP1 (mpi_errno, MPI_ERR_OTHER, "**fail",
                        "**fail %s", "Could not get hostname");
	        }
            hostent = gethostbyname(hostname);
            pg->ch.mrail.xrc_hostid[0] = 
                    (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;
	        MPIDI_PG_Get_vc(pg, 0, &vc);
	        vc->smp.hostid = pg->ch.mrail.xrc_hostid[0];
        }
#endif
        lid_all[0] = MPIDI_CH3I_RDMA_Process.lids[0][0];
        gid_all[0] = MPIDI_CH3I_RDMA_Process.gids[0][0];
        ud_qpn_all[0] = ud_qpn_self;
    }

    if (pg_size > 1
#if defined(RDMA_CM)
        && !MPIDI_CH3I_RDMA_Process.use_rdma_cm
#endif /* defined(RDMA_CM) */
    )
    {
        if (ring_setup_done) {
            rdma_cleanup_startup_ring(&MPIDI_CH3I_RDMA_Process);
        }
    }

#if defined(RDMA_CM)
    if (g_num_smp_peers + 1 < pg_size || !MPIDI_CH3I_RDMA_Process.use_rdma_cm) {
#endif /* defined(RDMA_CM) */
	/* Initialize the registration cache */
        mpi_errno = dreg_init();

        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }

        /* Allocate RDMA Buffers */
        /*
         * Add Error Handling in Function
         */
        mpi_errno = rdma_iba_allocate_memory(
                &MPIDI_CH3I_RDMA_Process,
                pg_rank,
                pg_size);
        
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

	if (mv2_on_demand_ud_info_exchange) {
	    mpi_errno = MPICM_Init_Local_UD_struct(pg, ud_qpn_self,
											MPIDI_CH3I_RDMA_Process.lids[0][0],
											&MPIDI_CH3I_RDMA_Process.gids[0][0],
											hostid);
	    if (mpi_errno) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "rdma_init_ud_struct");
	    }
	} else {
	    mpi_errno = MPICM_Init_UD_struct(pg, ud_qpn_all, lid_all, gid_all);
	    if (mpi_errno) {
			MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
			"**fail %s", "rdma_init_ud_struct");
	    }
	}

    MPICM_Create_UD_threads();

    *conn_info_ptr = MPIU_Malloc(128);
    if (!*conn_info_ptr) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**nomem %s", "conn_info_str");
    }
    MPIU_Memset(*conn_info_ptr, 0, 128);

    MPIDI_CH3I_CM_Get_port_info(*conn_info_ptr, 128);

#ifdef _ENABLE_UD_
    if (rdma_enable_hybrid) {
        if ((mpi_errno = rdma_init_ud(&MPIDI_CH3I_RDMA_Process)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if (rdma_use_ud_zcopy) {
            if ((mpi_errno = mv2_ud_setup_zcopy_rndv(&MPIDI_CH3I_RDMA_Process)) != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }
        }
        
        if ((mpi_errno = mv2_ud_get_remote_info(pg, pg_rank, pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if ((mpi_errno = MPIDI_CH3I_UD_Generate_addr_handles(pg, pg_rank, pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        mv2_init_timers();
    }
#endif /* _ENABLE_UD_ */

    /*
     * Freeing local memory
     */
    MPIU_Free(ud_qpn_all);
    MPIU_Free(lid_all);
    MPIU_Free(hca_type_all);
    MPIU_Free(gid_all);

    DEBUG_PRINT("Done MPIDI_CH3I_CM_Init()\n");

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_CH3I_CM_INIT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_CM_Finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_CM_Finalize(void)
{
    /* Finalize the rdma implementation */
    /* No rdma functions will be called after this function */
    int retval;
    int i = 0;
    int rail_index;
    int hca_index;
    int mpi_errno = MPI_SUCCESS;

    MPIDI_VC_t *vc = NULL;

    /* Insert implementation here */
    MPIDI_PG_t* pg = MPIDI_Process.my_pg;
    int pg_rank = MPIDI_Process.my_pg_rank;
    int pg_size = MPIDI_PG_Get_size(pg);

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);

#ifndef DISABLE_PTMALLOC
    mvapich2_mfin();
#endif

#ifdef _ENABLE_UD_
    if (rdma_enable_hybrid && DEBUG_UDSTAT_verbose) {
        MPIDI_CH3I_UD_Stats(pg);
    }
#endif

    /* Show memory usage statistics */
    if (DEBUG_MEM_verbose) {
        if(pg_rank == 0 || DEBUG_MEM_verbose > 1) {
            print_mem_usage();
            print_vbuf_usage();
        }
    }

    /*barrier to make sure queues are initialized before continuing */
    if ((retval = PMI_Barrier()) != 0)
    {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**pmi_barrier", "**pmi_barrier %d", retval);
    }

#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
#endif /* defined(RDMA_CM) */
	if ((retval = MPICM_Finalize_UD()) != 0)
        {
	    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
		"**fail", "**fail %d", retval);
        }
#if defined(RDMA_CM)
    }
#endif /* defined(RDMA_CM) */

    if (!use_iboeth && (rdma_3dtorus_support || rdma_path_sl_query)) {
        mv2_release_3d_torus_resources();
    }

    if (ring_setup_done) {
        ring_rdma_close_hca(&MPIDI_CH3I_RDMA_Process);
    }

    for (; i < pg_size; ++i)
    {

		MPIDI_PG_Get_vc(pg, i, &vc);

#ifndef MV2_DISABLE_HEADER_CACHING
        MPIU_Free(vc->mrail.rfp.cached_incoming);
        MPIU_Free(vc->mrail.rfp.cached_outgoing);
#endif /* !MV2_DISABLE_HEADER_CACHING */

        if (!qp_required(vc, pg_rank, i)) {
            continue;
        }

	if (vc->ch.state != MPIDI_CH3I_VC_STATE_IDLE 
#ifdef _ENABLE_XRC_
            && VC_XSTS_ISUNSET (vc, (XF_SEND_IDLE | XF_SEND_CONNECTING |
                XF_RECV_IDLE))
#endif
            )
        {
	    continue;
        }

	for (hca_index = 0; hca_index < rdma_num_hcas; ++hca_index)
        {
	    if (vc->mrail.rfp.RDMA_send_buf_mr[hca_index])
            {
                ibv_dereg_mr(vc->mrail.rfp.RDMA_send_buf_mr[hca_index]);
            }

	    if (vc->mrail.rfp.RDMA_recv_buf_mr[hca_index])
            {
                ibv_dereg_mr(vc->mrail.rfp.RDMA_recv_buf_mr[hca_index]);
            }
	}

	for (rail_index = 0; rail_index < vc->mrail.num_rails; ++rail_index) {
#if defined(RDMA_CM)
	    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
            {
#endif /* defined(RDMA_CM) */
#ifdef _ENABLE_XRC_
                hca_index = rail_index / (rdma_num_ports * 
                        rdma_num_qp_per_port);
                if (USE_XRC && vc->ch.xrc_my_rqpn[rail_index] != 0) {
                    /*  Unregister recv QP */
                    PRINT_DEBUG(DEBUG_XRC_verbose>0, "unreg %d", vc->ch.xrc_my_rqpn[rail_index]);
                    if ((retval = ibv_unreg_xrc_rcv_qp (
                                MPIDI_CH3I_RDMA_Process.xrc_domain[hca_index], 
                                vc->ch.xrc_my_rqpn[rail_index])) != 0) {
                        PRINT_DEBUG(DEBUG_XRC_verbose>0, "unreg failed %d %d", 
                                vc->ch.xrc_rqpn[rail_index], retval);
                        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,
                                MPI_ERR_INTERN,
                                "**fail",
	                        "**fail %s",
                                "Can't unreg RCV QP");
                    }
                }
                if (!USE_XRC || (VC_XST_ISUNSET(vc, XF_INDIRECT_CONN) &&
                        VC_XST_ISSET (vc, XF_SEND_IDLE)))
                    /* Destroy SEND QP */
#endif
                {
                    if  (vc->mrail.rails[rail_index].qp_hndl) {
                        ibv_destroy_qp(vc->mrail.rails[rail_index].qp_hndl);
                    }
                }
#if defined(RDMA_CM)
            }
#endif /* defined(RDMA_CM) */
	}

        if (vc->mrail.rfp.RDMA_send_buf_DMA)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_send_buf_DMA);
        }

        if (vc->mrail.rfp.RDMA_recv_buf_DMA)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_recv_buf_DMA);
        }

        if (vc->mrail.rfp.RDMA_send_buf)
        {
            MPIU_Free(vc->mrail.rfp.RDMA_send_buf);
        }

        if (vc->mrail.rfp.RDMA_recv_buf)
        {
	    MPIU_Free(vc->mrail.rfp.RDMA_recv_buf);
        }
    }

#ifdef _ENABLE_UD_
    /* destroy ud context*/
    if (rdma_enable_hybrid) {
		for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
            mv2_ud_destroy_ctx(MPIDI_CH3I_RDMA_Process.ud_rails[hca_index]);
        }
        MPIU_Free(MPIDI_CH3I_RDMA_Process.remote_ud_info);

        if (rdma_use_ud_zcopy) {
            mv2_ud_zcopy_info_t *zcopy_info  = &MPIDI_CH3I_RDMA_Process.zcopy_info;
            dreg_unregister(zcopy_info->grh_mr);

		    for (hca_index = 0; hca_index < rdma_num_hcas; hca_index++) {
                mv2_ud_destroy_ctx(zcopy_info->rndv_ud_qps[hca_index]);
                ibv_destroy_cq(zcopy_info->rndv_ud_cqs[hca_index]);
            }

            for (i=0; i<rdma_ud_num_rndv_qps; i++) {
                ibv_destroy_qp(zcopy_info->rndv_qp_pool[i].ud_qp);
                ibv_destroy_cq(zcopy_info->rndv_qp_pool[i].ud_cq);
            }

            MPIU_Free(zcopy_info->rndv_ud_qps);
            MPIU_Free(zcopy_info->rndv_ud_cqs);
            MPIU_Free(zcopy_info->rndv_qp_pool);
            MPIU_Free(zcopy_info->grh_buf);
        }
    }
#endif

    /* STEP 3: release all the cq resource, relaes all the unpinned buffers, release the
     * ptag 
     *   and finally, release the hca */
#if defined(RDMA_CM)
    if (!MPIDI_CH3I_RDMA_Process.use_rdma_cm)
    {
#endif /* defined(RDMA_CM) */
        for (i = 0; i < rdma_num_hcas; ++i)
        {
	    ibv_destroy_cq(MPIDI_CH3I_RDMA_Process.cq_hndl[i]);

	    if (MPIDI_CH3I_RDMA_Process.has_srq) {
		    pthread_cancel(MPIDI_CH3I_RDMA_Process.async_thread[i]);
		    pthread_join(MPIDI_CH3I_RDMA_Process.async_thread[i], NULL);
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "destroyed SRQ: %d\n", i);
		    ibv_destroy_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[i]);
#ifdef _ENABLE_XRC_
            if (USE_XRC) {
                int err;
                if (MPIDI_CH3I_Process.has_dpm) {
                    char xrc_file[512]; 
                    hca_index = i / (rdma_num_ports * 
                            rdma_num_qp_per_port);
                    sprintf (xrc_file, "/dev/shm/%s-%d", 
                            ufile, hca_index);
                    unlink (xrc_file);
                }
                ibv_close_xrc_domain (
                            MPIDI_CH3I_RDMA_Process.xrc_domain[i]);
                if ((err = close (MPIDI_CH3I_RDMA_Process.xrc_fd[i]))) {
                    MPIU_ERR_SETFATALANDJUMP2(
                            mpi_errno,
                            MPI_ERR_INTERN,
                            "**fail",
                            "%s: %s",
                            "close",
                            strerror(err));
                }
            }
#endif
	    }

	    deallocate_vbufs(i);
	  }

      deallocate_vbuf_region();
      dreg_finalize();
      
      for (i = 0; i < rdma_num_hcas; ++i)
      {
	    ibv_dealloc_pd(MPIDI_CH3I_RDMA_Process.ptag[i]);
	    ibv_close_device(MPIDI_CH3I_RDMA_Process.nic_context[i]);
      } 
#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        clear_xrc_hash ();
    }
#endif
#if defined(RDMA_CM)
    }
    else 
    {
	ib_finalize_rdma_cm(pg_rank, pg);
	MPIU_Free(rdma_cm_host_list);
    }
#endif /* defined(RDMA_CM) */

    if(MPIDI_CH3I_RDMA_Process.polling_set != NULL) { 
      MPIU_Free(MPIDI_CH3I_RDMA_Process.polling_set);
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_CM_FINALIZE);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/* vi:set sw=4 tw=80: */
