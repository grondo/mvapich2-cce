/*
 * This source file was derived from code in the MPICH-GM implementation
 * of MPI, which was developed by Myricom, Inc.
 * Myricom MPICH-GM ch_gm backend
 * Copyright (c) 2001 by Myricom, Inc.
 * All rights reserved.
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

#define _GNU_SOURCE 1

#include <mpimem.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include "pmi.h"

#include <sched.h>

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "coll_shmem.h"
#include "coll_shmem_internal.h"

typedef unsigned long addrint_t;

/* Shared memory collectives mgmt*/
struct shmem_coll_mgmt{
    void *mmap_ptr;
    int fd;
};
struct shmem_coll_mgmt shmem_coll_obj = {NULL, -1};

int           enable_knomial_2level_bcast=1;
int           inter_node_knomial_factor=4;
int           knomial_2level_bcast_message_size_threshold=2048;
int           knomial_2level_bcast_system_size_threshold=64; 

int shmem_coll_size = 0;
char *shmem_coll_file = NULL;

char hostname[SHMEM_COLL_HOSTNAME_LEN];
int my_rank;

int g_shmem_coll_blocks = 8;
int g_shmem_coll_max_msg_size = (1 << 17); 

int tuning_table[COLL_COUNT][COLL_SIZE] = {{2048, 1024, 512},
                                         {-1, -1, -1},
                                         {-1, -1, -1}
                                         };
/* array used to tune scatter*/
int size_scatter_tuning_table=4;
struct scatter_tuning scatter_tuning_table[] = {{64, 4096, 8192},{128, 8192, 16384},{256, 4096, 8192},{512, 4096, 8192}};

/*array used to tune gather */
int size_gather_tuning_table=8;
struct gather_tuning gather_tuning_table[] = {{32, 256},{64, 512},{128, 2048},{256, 2048},{384, 8196},{512, 8196},{768,8196},{1024,8196}};

/* array used to tune allgatherv */
int size_allgatherv_tuning_table=4;
struct allgatherv_tuning allgatherv_tuning_table[] = {{64, 32768},{128, 65536},{256, 131072},{512, 262144}};

int enable_shmem_collectives = 1;
int allgather_ranking=1;
int disable_shmem_allreduce=0;
int disable_shmem_reduce=0;
int disable_shmem_barrier=0;
int use_two_level_gather=1;
int use_direct_gather=1; 
int use_two_level_scatter=1;
int use_direct_scatter=1; 
int gather_direct_system_size_small = GATHER_DIRECT_SYSTEM_SIZE_SMALL;
int gather_direct_system_size_medium = GATHER_DIRECT_SYSTEM_SIZE_MEDIUM;
int use_xor_alltoall=1; 
int enable_shmem_bcast=1;
int scatter_rd_inter_leader_bcast=1;
int scatter_ring_inter_leader_bcast=1;
int knomial_inter_leader_bcast=1; 
int knomial_intra_node_threshold=256*1024;
int knomial_inter_leader_threshold=64*1024;
int bcast_two_level_system_size=64;
int intra_node_knomial_factor=4;

int tune_parameter=0;
/* Runtime threshold for scatter */
int user_scatter_small_msg = 0;
int user_scatter_medium_msg = 0;

/* Runtime threshold for gather */
int user_gather_switch_point = 0;

/* Runtime threshold for allgatherv */
int user_allgatherv_switch_point = 0;

int bcast_short_msg = MPIR_BCAST_SHORT_MSG; 

char* kvs_name;

int  use_osu_collectives = 1;
int  use_anl_collectives = 0;

#if defined(_OSU_MVAPICH_) || defined (_OSU_PSM_)
struct coll_runtime coll_param = { MPIR_ALLGATHER_SHORT_MSG, 
                                   MPIR_ALLGATHER_LONG_MSG,
                                   MPIR_ALLREDUCE_SHORT_MSG,
                                   MPIR_ALLREDUCE_2LEVEL_THRESHOLD,
                                   MPIR_REDUCE_SHORT_MSG,
                                   MPIR_REDUCE_2LEVEL_THRESHOLD,
                                   SHMEM_ALLREDUCE_THRESHOLD,
                                   SHMEM_REDUCE_THRESHOLD, 
                                   SHMEM_INTRA_REDUCE_THRESHOLD, 
                                   MPIR_ALLTOALL_SHORT_MSG, 
                                   MPIR_ALLTOALL_MEDIUM_MSG, 
                                   MPIR_ALLTOALL_THROTTLE, 
};
#endif

#if defined(CKPT)
extern void Wait_for_CR_Completion();
void *smc_store;
int smc_store_set;
#endif

/* Change the values set inside the array by the one define by the user */
int tuning_init(){

    int i;

    /* If MV2_SCATTER_SMALL_MSG is define*/
    if(user_scatter_small_msg>0){
        for(i=0; i <= size_scatter_tuning_table; i++){
            scatter_tuning_table[i].small = user_scatter_small_msg;
        }
    }

    /* If MV2_SCATTER_MEDIUM_MSG is define */
    if(user_scatter_medium_msg>0){
        for(i=0; i <= size_scatter_tuning_table; i++){
            if(scatter_tuning_table[i].small < user_scatter_medium_msg){ 
                scatter_tuning_table[i].medium = user_scatter_medium_msg;
            }
        }
    }

    /* If MV2_GATHER_SWITCH_POINT is define  */
    if(user_gather_switch_point>0){
        for(i=0; i <= size_gather_tuning_table; i++){
            gather_tuning_table[i].switchp = user_gather_switch_point;
        }
    }
    
    /* If MV2_ALLGATHERV_RD_THRESHOLD is define  */
    if(user_allgatherv_switch_point>0){
        for(i=0; i <= size_allgatherv_tuning_table; i++){
            allgatherv_tuning_table[i].switchp = user_allgatherv_switch_point;
        }
    }

    return 0;
} 

void MPIDI_CH3I_SHMEM_COLL_Cleanup()
{
    /*unmap*/
    if (shmem_coll_obj.mmap_ptr != NULL) { 
        munmap(shmem_coll_obj.mmap_ptr, shmem_coll_size);
    }
    /*unlink and close*/
    if (shmem_coll_obj.fd != -1) {
        close(shmem_coll_obj.fd);
        unlink(shmem_coll_file);
    }
    /*free filename variable*/
    if (shmem_coll_file != NULL) {
        MPIU_Free(shmem_coll_file);
    }
    shmem_coll_obj.mmap_ptr = NULL;
    shmem_coll_obj.fd = -1;
    shmem_coll_file = NULL;
}

void MPIDI_CH3I_SHMEM_COLL_Unlink()
{
    if (shmem_coll_obj.fd != -1) {
        unlink(shmem_coll_file);
    }
    if (shmem_coll_file != NULL) {
       MPIU_Free(shmem_coll_file);
    }
    shmem_coll_file = NULL;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_init(MPIDI_PG_t *pg, int local_id)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t* vc = NULL;
#if defined(SOLARIS)
    char *setdir="/tmp";
#else
    char *setdir="/dev/shm";
#endif
    char *shmem_dir, *shmdir;
    size_t pathlen;

    if ((shmdir = getenv("MV2_SHMEM_DIR")) != NULL) {
        shmem_dir = shmdir;
    } else {
        shmem_dir = setdir;
    }
    pathlen = strlen(shmem_dir);

    if (gethostname(hostname, sizeof(char) * SHMEM_COLL_HOSTNAME_LEN) < 0) {
	MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
		"gethostname", strerror(errno));
    }

    PMI_Get_rank(&my_rank);
    MPIDI_PG_Get_vc(pg, my_rank, &vc);

    /* add pid for unique file name */
    shmem_coll_file = (char *) MPIU_Malloc(pathlen + 
             sizeof(char) * (SHMEM_COLL_HOSTNAME_LEN + 26 + PID_CHAR_LEN));
    if (!shmem_coll_file) {
	MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
		"**nomem %s", "shmem_coll_file");
    }

    MPIDI_PG_GetConnKVSname(&kvs_name);
    /* unique shared file name */
    sprintf(shmem_coll_file, "%s/ib_shmem_coll-%s-%s-%d.tmp",
            shmem_dir, kvs_name, hostname, getuid());

    /* open the shared memory file */
    shmem_coll_obj.fd = open(shmem_coll_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (shmem_coll_obj.fd < 0) {
        /* Fallback */
        sprintf(shmem_coll_file, "/tmp/ib_shmem_coll-%s-%s-%d.tmp",
                kvs_name, hostname, getuid());

        shmem_coll_obj.fd = open(shmem_coll_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (shmem_coll_obj.fd < 0) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail", "%s: %s",
                "open", strerror(errno));
        }
    }

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    shmem_coll_size = SHMEM_ALIGN (SHMEM_COLL_BUF_SIZE + getpagesize()) + SHMEM_CACHE_LINE_SIZE;
#endif

   if (local_id == 0) {
        if (ftruncate(shmem_coll_obj.fd, 0)) {
            /* to clean up tmp shared file */
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s: %s",
                      "ftruncate", strerror(errno)); 
            goto cleanup_files;
        }

        /* set file size, without touching pages */
        if (ftruncate(shmem_coll_obj.fd, shmem_coll_size)) {
            /* to clean up tmp shared file */
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                      "%s: %s", "ftruncate",
                      strerror(errno));
            goto cleanup_files;
        }

/* Ignoring optimal memory allocation for now */
#if !defined(_X86_64_)
        {
            char *buf = (char *) MPIU_Calloc(shmem_coll_size + 1, sizeof(char));
            
            if (write(shmem_coll_obj.fd, buf, shmem_coll_size) != shmem_coll_size) {
                mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                      "%s: %s", "write",
                      strerror(errno));
                MPIU_Free(buf);
                goto cleanup_files;
            }
            MPIU_Free(buf);
        }
#endif /* !defined(_X86_64_) */

        if (lseek(shmem_coll_obj.fd, 0, SEEK_SET) != 0) {
            /* to clean up tmp shared file */
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                      "%s: %s", "lseek",
                      strerror(errno));
            goto cleanup_files;
        }

    }

    if(tune_parameter==1){
        tuning_init();
    }
    
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_INIT);
    return mpi_errno;

cleanup_files:
    MPIDI_CH3I_SHMEM_COLL_Cleanup();
fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Mmap
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_Mmap(MPIDI_PG_t *pg, int local_id)
{
    int i = 0;
    int j = 0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t* vc = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);

    MPIDI_PG_Get_vc(pg, my_rank, &vc);

    shmem_coll_obj.mmap_ptr = mmap(0, shmem_coll_size,
                         (PROT_READ | PROT_WRITE), (MAP_SHARED), shmem_coll_obj.fd,
                         0);
    if (shmem_coll_obj.mmap_ptr == (void *) -1) {
        /* to clean up tmp shared file */
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                   FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s: %s",
                   "mmap", strerror(errno));
        goto cleanup_files;
    }

#if defined(CKPT)
    if (smc_store_set) {
        MPIU_Memcpy(shmem_coll_obj.mmap_ptr, smc_store, shmem_coll_size);
	MPIU_Free(smc_store);
	smc_store_set = 0;
    }
#endif

    shmem_coll = (shmem_coll_region *) shmem_coll_obj.mmap_ptr;

    if (local_id == 0) {
        MPIU_Memset(shmem_coll_obj.mmap_ptr, 0, shmem_coll_size);

        for (j=0; j < SHMEM_COLL_NUM_COMM; ++j) {
            for (i = 0; i < SHMEM_COLL_NUM_PROCS; ++i) {
                shmem_coll->child_complete_bcast[j][i] = 0;
            }

            for (i = 0; i < SHMEM_COLL_NUM_PROCS; ++i) { 
                shmem_coll->root_complete_gather[j][i] = 1;
            }
        }
        pthread_spin_init(&shmem_coll->shmem_coll_lock, 0);

#if defined(CKPT)
	/*
	 * FIXME: The second argument to pthread_spin_init() indicates whether the
	 * Lock can be accessed by a process other than the one that initialized
	 * it. So, it should actually be PTHREAD_PROCESS_SHARED. However, the
	 * "shmem_coll_lock" above sets this to 0. Hence, I am doing the same.
	 */
	pthread_spin_init(&shmem_coll->cr_smc_spinlock, PTHREAD_PROCESS_SHARED);
	shmem_coll->cr_smc_cnt = 0;
#endif
    }
    
fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLLMMAP);
    return mpi_errno;

cleanup_files:
    MPIDI_CH3I_SHMEM_COLL_Cleanup();
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHMEM_COLL_finalize(int local_id, int num_local_nodes)
{
  MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);
  MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);


#if defined(CKPT)

    extern int g_cr_in_progress;

    if (g_cr_in_progress) {
            /* Wait for other local processes to check-in */
            pthread_spin_lock(&shmem_coll->cr_smc_spinlock);
            ++(shmem_coll->cr_smc_cnt);
            pthread_spin_unlock(&shmem_coll->cr_smc_spinlock);
            while(shmem_coll->cr_smc_cnt < num_local_nodes);

            if (local_id == 0) {
                smc_store = MPIU_Malloc(shmem_coll_size);
                MPIU_Memcpy(smc_store, shmem_coll_obj.mmap_ptr, shmem_coll_size);
                smc_store_set = 1;
            }
    }

#endif

    MPIDI_CH3I_SHMEM_COLL_Cleanup();

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_FINALIZE);
    return MPI_SUCCESS;
}

/* Shared memory gather: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_GetShmemBuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i = 1, cnt=0;
    char* shmem_coll_buf = (char*)(&(shmem_coll->shmem_coll_buf));
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->child_complete_gather[shmem_comm_rank][i] == 0) {
#if defined(CKPT)
  		Wait_for_CR_Completion();
#endif
                MPID_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

            }
        }
        /* Set the completion flags back to zero */
        for (i = 1; i < size; ++i) { 
            shmem_coll->child_complete_gather[shmem_comm_rank][i] = 0;
        }

        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    } else {
        while (shmem_coll->root_complete_gather[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
   	    Wait_for_CR_Completion();
#endif
            MPID_Progress_test(); 
                /* Yield once in a while */
            MPIU_THREAD_CHECK_BEGIN
            ++cnt;
            if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END

        }

        shmem_coll->root_complete_gather[shmem_comm_rank][rank] = 0;
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
}


/* Shared memory bcast: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_Bcast_GetBuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_Bcast_GetBuf(int size, int rank, int shmem_comm_rank, void** output_buf)
{
    int i = 1, cnt=0;
    char* shmem_coll_buf = (char*)(&(shmem_coll->shmem_coll_buf) +
                               g_shmem_coll_blocks*SHMEM_COLL_BLOCK_SIZE);
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->child_complete_bcast[shmem_comm_rank][i] == 1) {
#if defined(CKPT)
  		Wait_for_CR_Completion();
#endif
                MPID_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

            }
        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    } else {
        while (shmem_coll->child_complete_bcast[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
   	    Wait_for_CR_Completion();
#endif
            MPID_Progress_test(); 
                /* Yield once in a while */
            MPIU_THREAD_CHECK_BEGIN
            ++cnt;
            if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
            }
            MPIU_THREAD_CHECK_END

        }
        *output_buf = (char*)shmem_coll_buf + shmem_comm_rank * SHMEM_COLL_BLOCK_SIZE;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_BCAST_GETBUF);
}

/* Shared memory bcast: rank zero is the root always*/
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_Bcast_Complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_Bcast_Complete(int size, int rank, int shmem_comm_rank)
{
    int i = 1;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETBCASTCOMPLETE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETBCASTCOMPLETE);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->child_complete_bcast[shmem_comm_rank][i] = 1;
        } 
    } else {
            shmem_coll->child_complete_bcast[shmem_comm_rank][rank] = 0;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_GETSHMEMBUF);
}



#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_SetGatherComplete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(int size, int rank, int shmem_comm_rank)
{
    int i = 1;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->root_complete_gather[shmem_comm_rank][i] = 1;
        }
    } else {
        shmem_coll->child_complete_gather[shmem_comm_rank][rank] = 1;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_SETGATHERCOMPLETE);
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Barrier_gather
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_Barrier_gather(int size, int rank, int shmem_comm_rank)
{
    int i = 1, cnt=0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);

    if (rank == 0) {
        for (; i < size; ++i) { 
            while (shmem_coll->barrier_gather[shmem_comm_rank][i] == 0) {
#if defined(CKPT)
                Wait_for_CR_Completion();
#endif
                MPID_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END
            }
        }
        for (i = 1; i < size; ++i) { 
            shmem_coll->barrier_gather[shmem_comm_rank][i] = 0; 
        }
    } else {
        shmem_coll->barrier_gather[shmem_comm_rank][rank] = 1;
    }
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_GATHER);
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHMEM_COLL_Barrier_bcast
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SHMEM_COLL_Barrier_bcast(int size, int rank, int shmem_comm_rank)
{
    int i = 1, cnt=0;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);

    if (rank == 0) {
        for (; i < size; ++i) { 
            shmem_coll->barrier_bcast[shmem_comm_rank][i] = 1;
        }
    } else {
        while (shmem_coll->barrier_bcast[shmem_comm_rank][rank] == 0) {
#if defined(CKPT)
	        Wait_for_CR_Completion();
#endif
                MPID_Progress_test();
                /* Yield once in a while */
                MPIU_THREAD_CHECK_BEGIN
                ++cnt;
                if (cnt >= 20) {
                    cnt = 0;
#if defined(CKPT)
                    MPIDI_CH3I_CR_unlock();
#endif
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
                    do { } while(0);
#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
                    MPIU_THREAD_CHECK_BEGIN
                    MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
                    MPIU_THREAD_CHECK_END
#endif
#if defined(CKPT)
                    MPIDI_CH3I_CR_lock();
#endif
                }
                MPIU_THREAD_CHECK_END

        }
        shmem_coll->barrier_bcast[shmem_comm_rank][rank] = 0;
    }

    MPID_Progress_test();
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SHMEM_COLL_BARRIER_BCAST);
}


void lock_shmem_region()
{
    pthread_spin_lock(&shmem_coll->shmem_coll_lock);
}

void unlock_shmem_region()
{
    pthread_spin_unlock(&shmem_coll->shmem_coll_lock);
}

void increment_shmem_comm_count()
{
    ++ shmem_coll->shmem_comm_count;
}
int get_shmem_comm_count()
{
    return shmem_coll->shmem_comm_count;
}

int is_shmem_collectives_enabled()
{
    return enable_shmem_collectives;
}

void MV2_Read_env_vars(void){
    char *value;
    int flag;

    if ((value = getenv("MV2_USE_OSU_COLLECTIVES")) != NULL) {
        if( atoi(value) == 1) {
            use_osu_collectives = 1;
        } else {
            use_osu_collectives = 0;
            use_anl_collectives = 1;
        }
    }

    if ((value = getenv("MV2_USE_SHMEM_COLL")) != NULL){
        flag = (int)atoi(value); 
        if (flag > 0) enable_shmem_collectives = 1;
        else enable_shmem_collectives = 0;
    }
    if ((value = getenv("MV2_USE_SHMEM_ALLREDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_allreduce = 0;
        else disable_shmem_allreduce = 1;
    }
    if ((value = getenv("MV2_USE_SHMEM_REDUCE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_reduce = 0;
        else disable_shmem_reduce = 1;
    }
    if ((value = getenv("MV2_USE_SHMEM_BARRIER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) disable_shmem_barrier = 0;
        else disable_shmem_barrier = 1;
    }
    if ((value = getenv("MV2_SHMEM_COLL_NUM_COMM")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) g_shmem_coll_blocks = flag;
    }
    if ((value = getenv("MV2_SHMEM_COLL_MAX_MSG_SIZE")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) g_shmem_coll_max_msg_size = flag;
    }
    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL){
	    flag = (int)atoi(value);
	    if (flag <= 0) enable_shmem_collectives = 0;
    }
    if ((value = getenv("MV2_USE_BLOCKING")) != NULL){
	    flag = (int)atoi(value);
	    if (flag > 0) enable_shmem_collectives = 0;
    }

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLREDUCE_SHORT_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.allreduce_short_msg = flag;
    }
#endif
    if ((value = getenv("MV2_ALLGATHER_REVERSE_RANKING")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) allgather_ranking = 1;
        else allgather_ranking = 0;
    }
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLGATHER_RD_THRESHOLD")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.allgather_rd_threshold = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLGATHERV_RD_THRESHOLD")) != NULL){
        flag = (int)atoi(value);
        user_allgatherv_switch_point = atoi(value);
        tune_parameter=1;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLGATHER_BRUCK_THRESHOLD")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.allgather_bruck_threshold = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLREDUCE_2LEVEL_MSG")) != NULL){
        flag = (int)atoi(value);
        if (flag >= 0) { 
            coll_param.allreduce_2level_threshold = flag;
        }
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_REDUCE_SHORT_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.reduce_short_msg = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_SHMEM_ALLREDUCE_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.shmem_allreduce_msg = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_REDUCE_2LEVEL_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) { 
                  coll_param.reduce_2level_threshold = flag;
            } 
    }
#endif
    if ((value = getenv("MV2_SCATTER_SMALL_MSG")) != NULL) {
        user_scatter_small_msg = atoi(value);
        tune_parameter=1;
    }
    if ((value = getenv("MV2_SCATTER_MEDIUM_MSG")) != NULL) {
        user_scatter_medium_msg = atoi(value);
        tune_parameter=1;
    }
    if ((value = getenv("MV2_GATHER_SWITCH_PT")) != NULL) {
        user_gather_switch_point = atoi(value);
        tune_parameter=1;
    }
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_SHMEM_REDUCE_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.shmem_reduce_msg = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_INTRA_SHMEM_REDUCE_MSG")) != NULL){
	    flag = (int)atoi(value);
	    if (flag >= 0) coll_param.shmem_intra_reduce_msg = flag;
    }
#endif
    if ((value = getenv("MV2_USE_SHMEM_BCAST")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) enable_shmem_bcast = 1;
        else enable_shmem_bcast = 0;
    }
    if ((value = getenv("MV2_USE_TWO_LEVEL_GATHER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) use_two_level_gather = 1;
        else use_two_level_gather = 0;
    }
    if ((value = getenv("MV2_USE_DIRECT_GATHER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) use_direct_gather = 1;
        else use_direct_gather = 0;
    }
    if ((value = getenv("MV2_USE_TWO_LEVEL_SCATTER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) use_two_level_scatter = 1;
        else use_two_level_scatter = 0;
    }
    if ((value = getenv("MV2_USE_DIRECT_SCATTER")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) use_direct_scatter = 1;
        else use_direct_scatter = 0;
    }
    if ((value = getenv("MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_SMALL")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) gather_direct_system_size_small = flag;
        else use_direct_gather = GATHER_DIRECT_SYSTEM_SIZE_SMALL;
    }
    if ((value = getenv("MV2_USE_DIRECT_GATHER_SYSTEM_SIZE_MEDIUM")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) gather_direct_system_size_medium = flag;
        else use_direct_gather = GATHER_DIRECT_SYSTEM_SIZE_MEDIUM;
    }
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLTOALL_SMALL_MSG")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) coll_param.alltoall_small_msg = flag;
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLTOALL_THROTTLE_FACTOR")) != NULL) {
        flag = (int)atoi(value);
        if (flag <= 1) { 
             coll_param.alltoall_throttle_factor = 1;
        } else { 
             coll_param.alltoall_throttle_factor = flag;
        } 
    }
#endif
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if ((value = getenv("MV2_ALLTOALL_MEDIUM_MSG")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) coll_param.alltoall_medium_msg = flag;
    }
#endif
    if ((value = getenv("MV2_USE_XOR_ALLTOALL")) != NULL) {
        flag = (int)atoi(value);
        if (flag >= 0) use_xor_alltoall = flag;
    }
    if ((value = getenv("MV2_KNOMIAL_INTER_LEADER_THRESHOLD")) != NULL) {
          flag = (int)atoi(value);
         if (flag > 0) knomial_inter_leader_threshold = flag;
     }
    if ((value = getenv("MV2_KNOMIAL_INTRA_NODE_THRESHOLD")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) knomial_intra_node_threshold = flag;
    }
    if ((value = getenv("MV2_USE_SCATTER_RING_INTER_LEADER_BCAST")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) scatter_ring_inter_leader_bcast = flag;
    }
    if ((value = getenv("MV2_USE_SCATTER_RD_INTER_LEADER_BCAST")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) scatter_rd_inter_leader_bcast = flag;
    }
    if ((value = getenv("MV2_USE_KNOMIAL_INTER_LEADER_BCAST")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) knomial_inter_leader_bcast  = flag;
    }
    if ((value = getenv("MV2_BCAST_TWO_LEVEL_SYSTEM_SIZE")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) bcast_two_level_system_size  = flag;
    }
    if ((value = getenv("MV2_USE_BCAST_SHORT_MSG")) != NULL) {
        flag = (int)atoi(value);
        if (flag > 0) bcast_short_msg  = flag;
    }

    if ((value = getenv("MV2_USE_KNOMIAL_2LEVEL_BCAST")) != NULL) { 
       enable_knomial_2level_bcast=!!atoi(value);
       if (enable_knomial_2level_bcast <= 0)  { 
           enable_knomial_2level_bcast = 0;
       } 
    }   

    if ((value = getenv("MV2_KNOMIAL_2LEVEL_BCAST_MESSAGE_SIZE_THRESHOLD"))
           != NULL) {
        knomial_2level_bcast_message_size_threshold=atoi(value);
    }
     
    if ((value = getenv("MV2_KNOMIAL_2LEVEL_BCAST_SYSTEM_SIZE_THRESHOLD"))
             != NULL) {
        knomial_2level_bcast_system_size_threshold=atoi(value);
    }
  
    if ((value = getenv("MV2_KNOMIAL_INTRA_NODE_FACTOR")) != NULL) {
        intra_node_knomial_factor=atoi(value);
        if (intra_node_knomial_factor < INTRA_NODE_KNOMIAL_FACTOR_MIN) { 
            intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if (intra_node_knomial_factor > INTRA_NODE_KNOMIAL_FACTOR_MAX) { 
            intra_node_knomial_factor = INTRA_NODE_KNOMIAL_FACTOR_MAX;
        } 
    }     
  
    if ((value = getenv("MV2_KNOMIAL_INTER_NODE_FACTOR")) != NULL) {
        inter_node_knomial_factor=atoi(value);
        if (inter_node_knomial_factor < INTER_NODE_KNOMIAL_FACTOR_MIN) { 
            inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MIN;
        } 
        if (inter_node_knomial_factor > INTER_NODE_KNOMIAL_FACTOR_MAX) { 
            inter_node_knomial_factor = INTER_NODE_KNOMIAL_FACTOR_MAX;
        } 
    }      

    /* Override MPICH2 default env values */
    MPIR_PARAM_GATHERV_INTER_SSEND_MIN_PROCS = 1024;

    init_thread_reg();
}
#endif
