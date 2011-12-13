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
#include <math.h>

#include "rdma_impl.h"
#include "dreg.h"
#include "ibv_param.h"
#include "infiniband/verbs.h"
#include "mpidrma.h"
#include "pmi.h"
#include "mpiutil.h"

#if defined(_SMP_LIMIC_)
#include <fcntl.h>
#include <sys/mman.h>
#include "mpimem.h"
#endif /*_SMP_LIMIC_*/

#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

//#define DEBUG
#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#define DEBUG_PRINT(args...)
#endif

#ifdef _ENABLE_XRC_
#define IS_XRC_SEND_IDLE_UNSET(vc_ptr) (USE_XRC && VC_XST_ISUNSET (vc_ptr, XF_SEND_IDLE))
#define CHECK_HYBRID_XRC_CONN(vc_ptr) \
do {                                                                    \
    /* This is hack to force XRC connection in hybrid mode*/            \
    if (USE_XRC && VC_XST_ISSET (vc_ptr, XF_SEND_IDLE)                  \
            && !(vc_ptr->mrail.state & MRAILI_RC_CONNECTED)) {          \
        VC_XST_CLR (vc_ptr, XF_SEND_IDLE);                              \
    }                                                                   \
}while(0)                                                                   
#else
#define IS_XRC_SEND_IDLE_UNSET(vc_ptr) (0)
#define CHECK_HYBRID_XRC_CONN(vc_ptr) 
#endif

#define ONESIDED_RDMA_POST(vc_ptr, save_vc, rail)                           \
do {                                                                        \
    if (!(IS_RC_CONN_ESTABLISHED(vc_ptr))                                   \
            || (IS_XRC_SEND_IDLE_UNSET(vc_ptr))                             \
            || !MPIDI_CH3I_CM_One_Sided_SendQ_empty(vc_ptr)) {              \
        /* VC is not ready to be used. Wait till it is ready and send */    \
        MPIDI_CH3I_CM_One_Sided_SendQ_enqueue(vc_ptr, v);                   \
        if (!(vc_ptr->mrail.state & MRAILI_RC_CONNECTED) &&                 \
                  vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_IDLE) {           \
            vc_ptr->ch.state = MPIDI_CH3I_VC_STATE_UNCONNECTED;             \
        }                                                                   \
        CHECK_HYBRID_XRC_CONN(vc_ptr);                                      \
        if (vc_ptr->ch.state == MPIDI_CH3I_VC_STATE_UNCONNECTED) {          \
            /* VC is not connected, initiate connection */                  \
            MPIDI_CH3I_CM_Connect(vc_ptr);                                  \
        }                                                                   \
    } else {                                                                \
        XRC_FILL_SRQN_FIX_CONN (v, vc_ptr, rail);                              \
        if (MRAILI_Flush_wqe(vc_ptr,v,rail) != -1) { /* message not enqueued */\
            -- vc_ptr->mrail.rails[rail].send_wqes_avail;                      \
            IBV_POST_SR(v, vc_ptr, rail, "Failed to post rma put");            \
        }                                                                   \
        if(save_vc) { vc_ptr = save_vc;}                                    \
    }                                                                       \
}while(0);

#define SHM_DIR "/"
#define PID_CHAR_LEN 22
/*20 is to hold rma_shmid, rank, mv2 and other punctuations */
#define SHM_FILENAME_LEN (sizeof(SHM_DIR) + PID_CHAR_LEN + 20)

unsigned short rma_shmid = 100;

MPIDI_Win_pending_lock_t *pending_lock_winlist;

typedef enum {
   SINGLE=0,
   STRIPE,
   REPLICATE
} rail_select_t;

typedef struct {
  uintptr_t win_ptr;
  uint32_t win_rkeys[MAX_NUM_HCAS];
  uint32_t completion_counter_rkeys[MAX_NUM_HCAS];
  uint32_t post_flag_rkeys[MAX_NUM_HCAS];
  uint32_t fall_back;
} win_info;

typedef struct {
     char filename[SHM_FILENAME_LEN];
     size_t size;
     size_t displacement;
     int    shm_fallback;
} file_info;

typedef struct shm_buffer {
  char filename[SHM_FILENAME_LEN];
  void *ptr;
  size_t size;
  int owner;
  int fd;
  int ref_count;
  struct shm_buffer *next;
} shm_buffer;
shm_buffer *shm_buffer_llist = NULL;
shm_buffer *shm_buffer_rlist = NULL;

#if defined(_SMP_LIMIC_)
char* shmem_file;
extern struct smpi_var g_smpi;
extern int g_smp_use_limic2;
extern int limic_fd;
#endif /* _SMP_LIMIC_ */

extern int number_of_op;
static int Decrease_CC(MPID_Win *, int);
static int Post_Get_Put_Get_List(MPID_Win *, 
        MPIDI_msg_sz_t , dreg_entry * ,
        MPIDI_VC_t * , void *local_buf[], 
        void *remote_buf[], void *user_buf[], int length,
        uint32_t lkeys[], uint32_t rkeys[], 
        rail_select_t rail_select);

static int Post_Put_Put_Get_List(MPID_Win *, MPIDI_msg_sz_t,  dreg_entry *, 
        MPIDI_VC_t *, void *local_buf[], void *remote_buf[], int length,
        uint32_t lkeys[], uint32_t rkeys[], rail_select_t rail_select);

static int iba_put(MPIDI_RMA_ops *, MPID_Win *, MPIDI_msg_sz_t);
static int iba_get(MPIDI_RMA_ops *, MPID_Win *, MPIDI_msg_sz_t);
static int Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size);
int     iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
int     iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);
int MRAILI_Handle_one_sided_completions(vbuf * v);                            

static inline int Get_Pinned_Buf(MPID_Win * win_ptr, char **origin, int size)
{
    int mpi_errno = MPI_SUCCESS;
    if (win_ptr->pinnedpool_1sc_index + size >=
        rdma_pin_pool_size) {
        win_ptr->poll_flag = 1;
        while (win_ptr->poll_flag == 1) {
            mpi_errno = MPIDI_CH3I_Progress_test();
            if(mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
        *origin = win_ptr->pinnedpool_1sc_buf;
        win_ptr->pinnedpool_1sc_index = size;
    } else {
        *origin =
            win_ptr->pinnedpool_1sc_buf +
            win_ptr->pinnedpool_1sc_index;
        win_ptr->pinnedpool_1sc_index += size;
    }

fn_fail:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME mv2_allocate_shm_local
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int mv2_allocate_shm_local(int size, void **rnt_buf)
{
    int mpi_errno = MPI_SUCCESS;
    char *shm_file = NULL;
    void *mem_ptr = NULL;
    struct stat file_status;
    int fd;
    shm_buffer *shm_buffer_ptr, *prev_ptr, *curr_ptr;

    shm_file = (char *) MPIU_Malloc(SHM_FILENAME_LEN);
    if(shm_file == NULL) {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","malloc failed");
    }

    sprintf(shm_file, "%smv2-%d-%d-%d.tmp",
                   SHM_DIR, MPIDI_Process.my_pg_rank, getpid(), rma_shmid);
    rma_shmid++;

    fd = shm_open(shm_file, O_CREAT | O_RDWR | O_EXCL, S_IRWXU);
    if(fd == -1) {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit, 
                   "**fail", "**fail %s", strerror(errno));
    }

    if (ftruncate(fd, size) == -1)
    {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto close_file, 
                   "**fail", "**fail %s", "ftruncate failed");
    }

    /*verify file creation*/
    do
    {
        if (fstat(fd, &file_status) != 0)
        {
            MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto close_file,
                   "**fail", "**fail %s", "fstat failed");
        }
    } while (file_status.st_size != size);

    mem_ptr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_LOCKED,
                         fd, 0);
    if (mem_ptr == MAP_FAILED)
    {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto close_file,
                  "**fail", "**fail %s", "mmap failed");
    }

    MPIU_Memset(mem_ptr, 0, size);

    *rnt_buf =  mem_ptr;

    /*adding buffer to the list*/
    shm_buffer_ptr = (shm_buffer *) MPIU_Malloc(sizeof(shm_buffer));
    MPIU_Memcpy(shm_buffer_ptr->filename, shm_file, SHM_FILENAME_LEN);
    shm_buffer_ptr->ptr = mem_ptr;
    shm_buffer_ptr->owner = MPIDI_Process.my_pg_rank;
    shm_buffer_ptr->size = size;
    shm_buffer_ptr->fd = fd;
    shm_buffer_ptr->next = NULL;

    if(NULL == shm_buffer_llist) {
        shm_buffer_llist = shm_buffer_ptr;
        shm_buffer_ptr->next = NULL;
    } else {
        curr_ptr = shm_buffer_llist;
        prev_ptr = shm_buffer_llist;
        while(NULL != curr_ptr) {
          if ((size_t) curr_ptr->ptr > (size_t) shm_buffer_ptr->ptr) {
             break;
          }
          prev_ptr = curr_ptr;
          curr_ptr = curr_ptr->next;
        }
        shm_buffer_ptr->next = prev_ptr->next;
        prev_ptr->next = shm_buffer_ptr;
    }

fn_exit:
    if (shm_file) { 
        MPIU_Free(shm_file);
    }
    return mpi_errno;
close_file: 
    close(fd);
    shm_unlink(shm_file);
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME mv2_deallocate_shm_local
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int mv2_deallocate_shm_local (void *ptr)
{
    int mpi_errno = MPI_SUCCESS;
    shm_buffer *curr_ptr, *prev_ptr;

    curr_ptr = shm_buffer_llist;
    prev_ptr = NULL;
    while (curr_ptr) {
       if (curr_ptr->ptr == ptr) {
           break;
       }
       prev_ptr = curr_ptr;
       curr_ptr = curr_ptr->next;
    }

    /*return if buffer not found in shm buffer list*/
    if (curr_ptr == NULL) {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s", "buffer not found in shm list");
    }

    /*delink the current pointer from the list*/
    if (prev_ptr != NULL) {
       prev_ptr->next = curr_ptr->next;
    } else {
       shm_buffer_llist = curr_ptr->next;
    }

    if (munmap(curr_ptr->ptr, curr_ptr->size)) {
        ibv_error_abort (GEN_EXIT_ERR, 
                 "rdma_iba_1sc: munmap failed in mv2_deallocate_shm_local");
    }
    close(curr_ptr->fd);
    shm_unlink(curr_ptr->filename);
    MPIU_Free(curr_ptr);

fn_exit:
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME mv2_find_and_deallocate_shm_local
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int mv2_find_and_deallocate_shm (shm_buffer **list)
{
    int mpi_errno = MPI_SUCCESS;
    shm_buffer *curr_ptr, *prev_ptr;

    curr_ptr = *list;
    prev_ptr = NULL;
    while (curr_ptr) {
       if (curr_ptr->ref_count == 0) {
          /*delink pointer from the list*/
          if (prev_ptr != NULL) {
             prev_ptr->next = curr_ptr->next;
          } else {
             *list = curr_ptr->next;
          }

          if (munmap(curr_ptr->ptr, curr_ptr->size)) {
                ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc: \
                      mv2_find_and_deallocate_shm_local");
          }
          close(curr_ptr->fd);

          MPIU_Free(curr_ptr);
          if (prev_ptr != NULL) {
             curr_ptr = prev_ptr->next;
          } else {
             curr_ptr = *list;
          }
       } else {
          prev_ptr = curr_ptr;
          curr_ptr = curr_ptr->next;
       }
    }

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME mv2_rma_allocate_shm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int mv2_rma_allocate_shm(int size, int g_rank, int *shmem_fd, 
                   void **rnt_buf, MPID_Comm * comm_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno1 = MPI_SUCCESS;
    int errflag = FALSE;
    void* rma_shared_memory = NULL;
    const char *rma_shmem_dir="/";
    struct stat file_status;
    int length;
    char *rma_shmem_file = NULL;

    length = strlen(rma_shmem_dir)
             + 7 /*this is to hold MV2 and the unsigned short rma_shmid*/
             + PID_CHAR_LEN 
             + 6 /*this is for the hyphens and extension */;

    rma_shmem_file = (char *) MPIU_Malloc(length);
	
	if(g_rank == 0)
    {
       sprintf(rma_shmem_file, "%smv2-%d-%d.tmp",
                       rma_shmem_dir, getpid(), rma_shmid);
       rma_shmid++;
    }

    MPIR_Bcast_impl(rma_shmem_file, length, MPI_CHAR, 0, comm_ptr, &errflag); 

    *shmem_fd = shm_open(rma_shmem_file, O_CREAT | O_RDWR, S_IRWXU);
    if(*shmem_fd == -1){
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 
                                 "**nomem %s", strerror(errno));
		goto fn_exit;
    }

    if (ftruncate(*shmem_fd, size) == -1)
    {
		   mpi_errno =
              MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
		   goto fn_exit;
    } 

    /*verify file creation*/
    do 
    {
        if (fstat(*shmem_fd, &file_status) != 0)
        {
            mpi_errno =
               MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                   __LINE__, MPI_ERR_OTHER, "**nomem", 0);
            goto fn_exit;
        }
    } while (file_status.st_size != size);

    rma_shared_memory = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         *shmem_fd, 0);
    if (rma_shared_memory == MAP_FAILED)
    {
         mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_FATAL, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
         goto fn_exit;
    }

    MPIU_Memset(rma_shared_memory, 0, size);
    *rnt_buf =  rma_shared_memory;

fn_exit:
    mpi_errno1 = MPIR_Barrier_impl(comm_ptr, &errflag);
    if (mpi_errno1 != MPI_SUCCESS) {
        ibv_error_abort (GEN_EXIT_ERR,
                    "rdma_iba_1sc: error calling barrier");
    }
    if (*shmem_fd != -1) { 
        shm_unlink(rma_shmem_file);
    }
    if (rma_shmem_file) {  
        MPIU_Free(rma_shmem_file);
    }
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME mv2_rma_deallocate_shm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void mv2_rma_deallocate_shm(void *addr, int size)
{
    if(munmap(addr, size))
    {
        DEBUG_PRINT("munmap failed in mv2_rma_deallocate_shm with error: %d \n", errno);
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc: mv2_rma_deallocate_shm");
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Alloc_mem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void *MPIDI_CH3I_Alloc_mem (size_t size, MPID_Info *info)
{
   int mpi_errno = MPI_SUCCESS;
   char value[10] = "";
   int flag = 0;
   void *ptr = NULL;

   if (size<=0) {
     goto fn_exit;  
   }

   if (info != NULL) { 
       MPIR_Info_get_impl(info, "alloc_shm", 10, value, &flag);
   }

   if(SMP_INIT && (MPIDI_CH3I_RDMA_Process.has_shm_one_sided || 
      (flag && !strcmp(value, "true")))) {
      mpi_errno = mv2_allocate_shm_local(size, &ptr);
      if(mpi_errno != MPI_SUCCESS) {
          DEBUG_PRINT("shared memory allocation failed in MPIDI_CH3I_Alloc_mem: \
                     %d \n", mpi_errno);
          ptr = MPIU_Malloc(size);
      }
   }
   else {
      ptr = MPIU_Malloc(size);
   }

fn_exit:
   return ptr;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_Free_mem
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_Free_mem (void *ptr)
{
   int mpi_errno = MPI_SUCCESS;

   if(SMP_INIT) {
      mpi_errno = mv2_deallocate_shm_local(ptr);
      if(mpi_errno != MPI_SUCCESS) {
           DEBUG_PRINT("this buffer was not allocated in shared memory, \
                        calling MPIU_Free \n");
           MPIU_Free(ptr);
      }

      mv2_find_and_deallocate_shm(&shm_buffer_rlist);
   } else {
      MPIU_Free(ptr);
   }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_win_lock_enqueue
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_SHM_win_lock_enqueue (MPID_Win *win_ptr) 
{
    MPIDI_Win_pending_lock_t *new;

    if (*((volatile int *) &win_ptr->shm_lock_queued) == 0) {
        new = MPIU_Malloc(sizeof(MPIDI_Win_pending_lock_t));
        new->win_ptr = win_ptr;
        new->next = pending_lock_winlist;
        pending_lock_winlist = new;
        win_ptr->shm_lock_queued = 1;
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_win_lock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int MPIDI_CH3I_SHM_win_lock (int dest_rank, int lock_type_requested, 
                    MPID_Win *win_ptr, int blocking)
{          
    int mpi_errno = MPI_SUCCESS;
    int l_dest_rank, lock_acquired = 0;
    
    l_dest_rank = win_ptr->shm_g2l_rank[dest_rank];
       
    /*Wait and acquire shm lock*/ 
    do {
        if (lock_type_requested == MPI_LOCK_EXCLUSIVE) {
            while(*((volatile int *) &win_ptr->shm_lock[l_dest_rank])
                      != MPID_LOCK_NONE && blocking == 1) {
                mpi_errno = MPIDI_CH3I_Progress_test();
                if (mpi_errno != MPI_SUCCESS) { 
                    ibv_error_abort (GEN_EXIT_ERR, 
                            "rdma_iba_1sc : Progress test returned error \n");
                }
            }
            mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_dest_rank);
            if (mpi_errno != MPI_SUCCESS) { 
                ibv_error_abort (GEN_EXIT_ERR, "mutex lock error \n");
            }
            if(*((volatile int *) &win_ptr->shm_lock[l_dest_rank])
                    == MPID_LOCK_NONE) {
                win_ptr->shm_lock[l_dest_rank] = MPI_LOCK_EXCLUSIVE;
                lock_acquired = MPI_LOCK_EXCLUSIVE;
            }
            mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_dest_rank); 
            if (mpi_errno != MPI_SUCCESS) {
                ibv_error_abort (GEN_EXIT_ERR, "mutex unlock error \n");
            }
        } else if (lock_type_requested == MPI_LOCK_SHARED) {
            while(*((volatile int *) &win_ptr->shm_lock[l_dest_rank])
                    == MPI_LOCK_EXCLUSIVE && blocking == 1) {
                mpi_errno = MPIDI_CH3I_Progress_test();
                if (mpi_errno != MPI_SUCCESS) {
                    ibv_error_abort (GEN_EXIT_ERR,
                            "rdma_iba_1sc : Progress test returned error \n");
                }
            }
            mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_dest_rank);
            if (mpi_errno != MPI_SUCCESS) {
                ibv_error_abort (GEN_EXIT_ERR, "mutex lock error \n");
            }
            if(*((volatile int *) &win_ptr->shm_lock[l_dest_rank])
                    != MPI_LOCK_EXCLUSIVE) {
                win_ptr->shm_lock[l_dest_rank] = MPI_LOCK_SHARED;
                win_ptr->shm_shared_lock_count[l_dest_rank] += 1;
                lock_acquired = MPI_LOCK_SHARED;
            }
            mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_dest_rank); 
            if (mpi_errno != MPI_SUCCESS) {
                ibv_error_abort (GEN_EXIT_ERR, "mutex unlock error \n");
            }
        }
    } while (blocking == 1 && lock_acquired == 0);

    return lock_acquired;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_win_unlock
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_SHM_win_unlock (int dest_rank, MPID_Win *win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int l_dest_rank, lock_type;

    l_dest_rank = win_ptr->shm_g2l_rank[dest_rank];
    lock_type = *((volatile int *) &win_ptr->shm_lock[l_dest_rank]);

    /*Release shmem lock*/
    if (lock_type == MPI_LOCK_EXCLUSIVE) {
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_dest_rank); 
        if (mpi_errno != MPI_SUCCESS) {
            ibv_error_abort (GEN_EXIT_ERR, "mutex lock error \n");
        }
        win_ptr->shm_lock[l_dest_rank] = MPID_LOCK_NONE;
        win_ptr->shm_lock_released[l_dest_rank] = 1;
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_dest_rank);
        if (mpi_errno != MPI_SUCCESS) {
            ibv_error_abort (GEN_EXIT_ERR, "mutex unlock error \n");
        }
    } else if (lock_type == MPI_LOCK_SHARED) {
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, l_dest_rank);
        if (mpi_errno != MPI_SUCCESS) {
            ibv_error_abort (GEN_EXIT_ERR, "mutex lock error \n");
        }
        win_ptr->shm_shared_lock_count[l_dest_rank] -= 1;
        if(*((volatile long *)
              &win_ptr->shm_shared_lock_count[l_dest_rank]) == 0) {
             win_ptr->shm_lock[l_dest_rank] = MPID_LOCK_NONE;
             win_ptr->shm_lock_released[l_dest_rank] = 1;
        }
        mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, l_dest_rank);
        if (mpi_errno != MPI_SUCCESS) {
            ibv_error_abort (GEN_EXIT_ERR, "mutex unlock error \n");
        }
    }
}

/* For active synchronization, it is a blocking call*/
void
MPIDI_CH3I_RDMA_start (MPID_Win* win_ptr, int start_grp_size, int* ranks_in_win_grp) 
{
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    int flag = 0;
    int src;
    int i;
    int counter = 0;

    if (SMP_INIT)
    {
        comm_ptr = win_ptr->comm_ptr;
    }

    while (flag == 0 && start_grp_size != 0)
    {
        /* Need to make sure we make some progress on
         * anything in the extended sendq or coalesced
         * or we can have a deadlock.
         */
        if (counter % 200 == 0)
        {
            MPIDI_CH3I_Progress_test();
        }

        ++counter;

        for (i = 0; i < start_grp_size; ++i)
        {
            flag = 1;
            src = ranks_in_win_grp[i];  /*src is the rank in comm*/

            if (SMP_INIT)
            {
                MPIDI_Comm_get_vc(comm_ptr, src, &vc);

                if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
                {
                    /* Correspoding post has not been issued. */
                    flag = 0;
                    break;
                }
            }
            else if (win_ptr->post_flag[src] == 0)
            {
                /* Correspoding post has not been issued. */
                flag = 0;
                break;
            }
        }
    }
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_INTRANODE_start
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_INTRANODE_start (MPID_Win* win_ptr, int start_grp_size, 
                  int* ranks_in_win_grp) 
{
    int i, rank, local_rank;

    for (i = 0; i < start_grp_size; ++i) {
       rank = ranks_in_win_grp[i];  
       local_rank = win_ptr->shm_g2l_rank[rank];      
 
       if (rank != win_ptr->my_id && local_rank != -1) {
          /* Casting to volatile. This will prevent the compiler 
             from optimizing the loop out or from storing the veriable in 
             registers */
          while (*((volatile int *) &win_ptr->shm_post_flag_me[local_rank]) == 0) {
          }
       }
     }
    
    /*Clearing the post flag counters once we ahve received all the 
      flags for the current epoch*/
    MPIU_Memset(win_ptr->shm_post_flag_me, 0, win_ptr->shm_l_ranks*sizeof(int));
}

/* For active synchronization, if all rma operation has completed, we issue a RDMA
write operation with fence to update the remote flag in target processes*/
int
MPIDI_CH3I_RDMA_complete(MPID_Win * win_ptr,
                         int start_grp_size, int *ranks_in_win_grp)
{
    int i, target, dst;
    int my_rank, comm_size;
    int *nops_to_proc;
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops *curr_ptr;
    MPIDI_VC_t* vc = NULL;

    comm_ptr = win_ptr->comm_ptr;
    my_rank = comm_ptr->rank;
    comm_size = comm_ptr->local_size;

    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; ++i) {
        dst = ranks_in_win_grp[i];
        win_ptr->post_flag[dst] = 0;
    }
    win_ptr->using_start = 0;

    nops_to_proc = (int *) MPIU_Calloc(comm_size, sizeof(int));
    /* --BEGIN ERROR HANDLING-- */
    if (!nops_to_proc) {
        mpi_errno =
            MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                 __LINE__, MPI_ERR_OTHER, "**nomem", 0);
        goto fn_exit;
    }
    /* --END ERROR HANDLING-- */

    /* Check if there are more communication operations to be issued to 
       each traget process */
    for (i = 0; i < comm_size; ++i)
    {
        nops_to_proc[i] = 0;
    }
    curr_ptr = win_ptr->rma_ops_list_head;
    while (curr_ptr != NULL) {
        ++nops_to_proc[curr_ptr->target_rank];
        curr_ptr = curr_ptr->next;
    }

    /* If all the operations had been handled using direct RDMA (nops_to_proc[target] == 0) 
       we can set the complete flag to signal completion. If some operations are pending, the 
       signalling is taken care of in the send/recv path */
    for (i = 0; i < start_grp_size; ++i) {
        target = ranks_in_win_grp[i];

        if (target == my_rank) {
            continue;
        }

        if(SMP_INIT) 
        {
            MPIDI_Comm_get_vc(comm_ptr, target, &vc);
            if (nops_to_proc[target] == 0 && vc->smp.local_nodes == -1)
            {
            /* We ensure local completion of all the pending operations on 
             * the current window and then set the complete flag on the remote
             * side. We do not wait for the completion of this operation as this
             * would unnecessarily wait on completion of all operations on this
             * RC connection.
             */ 
               Decrease_CC(win_ptr, target);
            }
        } 
        else if (nops_to_proc[target] == 0)  
        {
            Decrease_CC(win_ptr, target);
        }
    }

    MPIU_Free(nops_to_proc);

  fn_exit:
    return mpi_errno;
}

/* For active synchronization, if all rma operation has completed, set the 
complete flag on the remote processes. */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_INTRANODE_complete
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_INTRANODE_complete(MPID_Win * win_ptr,
                         int start_grp_size, int *ranks_in_win_grp)
{
    int i, my_rank, my_local_rank, rank, local_rank;
    int *nops_to_proc;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops *curr_ptr;

    comm_ptr = win_ptr->comm_ptr; 
    my_rank = comm_ptr->rank;
    my_local_rank = win_ptr->shm_g2l_rank[my_rank];

    nops_to_proc = (int *) MPIU_Calloc(win_ptr->shm_l_ranks, sizeof(int));
    if (!nops_to_proc) {
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc : memory allocation failed");
    }

    /* Check if there are more communication operations to be issued to 
       each traget process */
    MPIU_Memset((void *) nops_to_proc, 0, win_ptr->shm_l_ranks*sizeof(int));
    curr_ptr = win_ptr->rma_ops_list_head;
    while (curr_ptr != NULL) { 
        local_rank = win_ptr->shm_g2l_rank[curr_ptr->target_rank]; 
        if (local_rank != -1) { 
            ++nops_to_proc[local_rank];
        }
        curr_ptr = curr_ptr->next;
    }

    /* If all the operations had been handled using direct RDMA (nops_to_proc[target] == 0) 
       we can set the complete flag to signal completion. If some operations are pending, the 
       signalling is taken care of in the send/recv path */
    for (i = 0; i < start_grp_size; ++i) {
        rank = ranks_in_win_grp[i];
        local_rank = win_ptr->shm_g2l_rank[rank];

        if (rank == win_ptr->my_id || local_rank == -1) {
            continue;
        }

        if (nops_to_proc[local_rank] == 0)  
        {
            *(win_ptr->shm_cmpl_counter_all[local_rank] + my_local_rank) = 1LL;
        }
    }

    MPIU_Free(nops_to_proc);

    return;
}


/* Waiting for all the completion signals and unregister buffers*/
int MPIDI_CH3I_RDMA_finish_rma(MPID_Win * win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    if (win_ptr->put_get_list_size != 0) {
            win_ptr->poll_flag = 1;
            while(win_ptr->poll_flag == 1){
                mpi_errno = MPIDI_CH3I_Progress_test();
                if(mpi_errno) MPIU_ERR_POP(mpi_errno);
	    }	
    }
    else return 0;

fn_fail:
    return mpi_errno;
}


/* Go through RMA op list once, and start as many RMA ops as possible */
void
MPIDI_CH3I_RDMA_try_rma(MPID_Win * win_ptr, int passive)
{
    MPIDI_RMA_ops *curr_ptr = NULL, *prev_ptr = NULL, *tmp_ptr;
    MPIDI_RMA_ops **list_head = NULL, **list_tail = NULL;
    MPIDI_msg_sz_t size, origin_type_size, target_type_size;
#ifdef _SCHEDULE
    int curr_put = 1;
    int fall_back = 0;
    int force_to_progress = 0, issued = 0;
    MPIDI_RMA_ops * skipped_op = NULL;
#endif
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;

    if (SMP_INIT)
    {
        comm_ptr = win_ptr->comm_ptr;
    }

    list_head = &win_ptr->rma_ops_list_head;
    list_tail = &win_ptr->rma_ops_list_tail;
    prev_ptr = curr_ptr = *list_head;

#ifdef _SCHEDULE
    while (curr_ptr != NULL || skipped_op != NULL)
    {
        if (curr_ptr == NULL)
        {
            curr_ptr = skipped_op;
            skipped_op = NULL;
            ++fall_back;
            if (issued == 0) {
                force_to_progress = 1;
            } else {
                force_to_progress = 0;
                issued = 0;
            }
        }
#else
    while (curr_ptr != NULL) {
#endif

     if (SMP_INIT) {
        MPIDI_Comm_get_vc(comm_ptr, curr_ptr->target_rank, &vc);

        if (vc->smp.local_nodes != -1)  {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }
     }

     if (passive == 0
         && win_ptr->post_flag[curr_ptr->target_rank] == 1
         && win_ptr->using_lock == 0)
     {
         switch (curr_ptr->type)
         {
            case MPIDI_RMA_PUT:
            {
                int origin_dt_derived;
                int target_dt_derived;
                origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                MPID_Datatype_get_size_macro(curr_ptr->origin_datatype, origin_type_size);
                size = curr_ptr->origin_count * origin_type_size;
 
                if (!origin_dt_derived
                    && !target_dt_derived
                    && size > rdma_put_fallback_threshold)
                {
#ifdef _SCHEDULE
                    if (curr_put != 1 || force_to_progress == 1)
                    { 
                      /* Nearest issued rma is not a put. */
#endif
                        ++win_ptr->rma_issued;
                        iba_put(curr_ptr, win_ptr, size);

                        if (*list_head == curr_ptr)
                        {
                            if (*list_head == *list_tail) 
                            {
                               *list_head = *list_tail = NULL;
                            } 
                            else 
                            {
                               *list_head = prev_ptr = curr_ptr->next; 
                            }
                        }
                        else if (*list_tail == curr_ptr)
                        {
                            *list_tail = prev_ptr;
                            (*list_tail)->next = NULL;
                        }
                        else
                        {
                            prev_ptr->next = curr_ptr->next;
                        }

                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                        curr_put = 1;
                        ++issued;
                    }
                    else
                    {
                        /* Nearest issued rma is a put. */
                        if (skipped_op == NULL)
                        {
                            skipped_op = curr_ptr;
                            prev_ptr = curr_ptr;
                            curr_ptr = curr_ptr->next;
                        }
                    }
#endif
                 }                 
                 else
                 {
                     prev_ptr = curr_ptr;
                     curr_ptr = curr_ptr->next;
                 }

                 break;
              }
            case MPIDI_RMA_ACCUMULATE:
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                break;
            case MPIDI_RMA_ACC_CONTIG:
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                break;
            case MPIDI_RMA_GET:
            {
                int origin_dt_derived;
                int target_dt_derived;

                origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) != HANDLE_KIND_BUILTIN ? 1 : 0;
                MPID_Datatype_get_size_macro(curr_ptr->target_datatype, target_type_size);
                size = curr_ptr->target_count * target_type_size;

                if (!origin_dt_derived
                    && !target_dt_derived 
                    && size > rdma_get_fallback_threshold)
                {
#ifdef _SCHEDULE
                    if (curr_put != 0 || force_to_progress == 1)
                    {
#endif
                        ++win_ptr->rma_issued;
                        iba_get(curr_ptr, win_ptr, size);

                        if (*list_head == curr_ptr)
                        {
                            if (*list_head == *list_tail)
                            {
                               *list_head = *list_tail = NULL;
                            }
                            else
                            {
                               *list_head = prev_ptr = curr_ptr->next;
                            }
                        }
                        else if (*list_tail == curr_ptr)
                        {
                            *list_tail = prev_ptr;
                            (*list_tail)->next = NULL;
                        }
                        else
                        {
                            prev_ptr->next = curr_ptr->next;
                        }

                        tmp_ptr = curr_ptr->next;
                        MPIU_Free(curr_ptr);
                        curr_ptr = tmp_ptr;
#ifdef _SCHEDULE
                        curr_put = 0;
                        ++issued;
                    }
                    else
                    {
                        if (skipped_op == NULL)
                        {
                            skipped_op = curr_ptr;
                        }

                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
                    }
#endif
                }
                else
                {
                    prev_ptr = curr_ptr;
                    curr_ptr = curr_ptr->next;
                }

                break;
            }
            default:
                DEBUG_PRINT("Unknown ONE SIDED OP\n");
                ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
                break;
            }
        }
        else
        {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
        }
    }

}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_try_rma
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_LIMIC_try_rma(MPID_Win * win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int complete_size;
    int target_type_size, msg_size;
    int origin_dt_derived, target_dt_derived, target_l_rank;
    int offset, initial_offset, initial_len;
    MPID_Comm *comm_ptr;
    MPIDI_RMA_ops *curr_ptr, *prev_ptr, *tmp_ptr;
    MPIDI_RMA_ops **list_head, **list_tail;

    comm_ptr = win_ptr->comm_ptr;
    list_head = &win_ptr->rma_ops_list_head;
    list_tail = &win_ptr->rma_ops_list_tail;
    prev_ptr = curr_ptr = *list_head; 

    while (curr_ptr != NULL) {
        complete_size = 0;
        target_l_rank = win_ptr->shm_g2l_rank[curr_ptr->target_rank];

        /*if target is on a remote node*/
        if(target_l_rank == -1) {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }

        origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) !=
                  HANDLE_KIND_BUILTIN ? 1 : 0;
        target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                  HANDLE_KIND_BUILTIN ? 1 : 0;

        if(origin_dt_derived || target_dt_derived) {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }

        MPID_Datatype_get_size_macro(curr_ptr->target_datatype,
                  target_type_size);
        msg_size = curr_ptr->target_count * target_type_size;

        initial_offset = win_ptr->peer_lu[curr_ptr->target_rank].offset;
        initial_len = win_ptr->peer_lu[curr_ptr->target_rank].length;
        offset = initial_offset + win_ptr->disp_units[curr_ptr->target_rank]
                        * curr_ptr->target_disp;
        win_ptr->peer_lu[curr_ptr->target_rank].offset = offset;

        switch (curr_ptr->type) {
            case MPIDI_RMA_PUT:
                if (msg_size < limic_put_threshold) {
                    prev_ptr = curr_ptr;
                    curr_ptr = curr_ptr->next;
                    continue;
                }
                complete_size = limic_tx_comp(limic_fd, curr_ptr->origin_addr,
                            msg_size, &(win_ptr->peer_lu[curr_ptr->target_rank]));
                break;
            case MPIDI_RMA_GET:
                if (msg_size < limic_get_threshold) {
                    prev_ptr = curr_ptr;
                    curr_ptr = curr_ptr->next;
                    continue;
                }
                complete_size = limic_rx_comp(limic_fd, curr_ptr->origin_addr,
                            msg_size, &(win_ptr->peer_lu[curr_ptr->target_rank]));
                break;
            default:
                break;
        }

        win_ptr->peer_lu[curr_ptr->target_rank].offset = initial_offset;
        win_ptr->peer_lu[curr_ptr->target_rank].length = initial_len;

        if( complete_size == msg_size ){
            win_ptr->rma_issued++;
            if (*list_head == curr_ptr)
            {
                if (*list_head == *list_tail)
                {
                   *list_head = *list_tail = NULL;
                }
                else
                {
                   *list_head = prev_ptr = curr_ptr->next;
                }
            }
            else if (*list_tail == curr_ptr)
            {
                *list_tail = prev_ptr;
                (*list_tail)->next = NULL;
            }
            else
            {
                prev_ptr->next = curr_ptr->next;
            }
            tmp_ptr = curr_ptr->next;
            MPIU_Free(curr_ptr);
            curr_ptr = tmp_ptr;
        } else {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
        }
    }

    return mpi_errno;
}
#endif /* _SMP_LIMIC_ */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_try_rma
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SHM_try_rma(MPID_Win * win_ptr)
{
    int mpi_errno = MPI_SUCCESS;
    int target_type_size, msg_size;
    int origin_dt_derived, target_dt_derived, target_l_rank;
    void *target_addr;
    MPIDI_RMA_ops *curr_ptr = NULL, *prev_ptr = NULL, *tmp_ptr;
    MPIDI_RMA_ops **list_head = NULL, **list_tail = NULL;
    MPI_User_function *uop;

    list_head = &win_ptr->rma_ops_list_head;
    list_tail = &win_ptr->rma_ops_list_tail;
    prev_ptr = curr_ptr = *list_head; 

    while (curr_ptr != NULL) {
        target_l_rank = win_ptr->shm_g2l_rank[curr_ptr->target_rank]; 

        /*if target is on a remote node*/
        if(target_l_rank == -1) {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }

        origin_dt_derived = HANDLE_GET_KIND(curr_ptr->origin_datatype) !=
                  HANDLE_KIND_BUILTIN ? 1 : 0;
        target_dt_derived = HANDLE_GET_KIND(curr_ptr->target_datatype) !=
                  HANDLE_KIND_BUILTIN ? 1 : 0;

        if(origin_dt_derived || target_dt_derived) {
            prev_ptr = curr_ptr;
            curr_ptr = curr_ptr->next;
            continue;
        }

        MPID_Datatype_get_size_macro(curr_ptr->target_datatype,
                  target_type_size);
        msg_size = curr_ptr->target_count * target_type_size;
        target_addr = (void *) ((size_t) win_ptr->shm_win_buffer_all[target_l_rank] +
                            win_ptr->disp_units[curr_ptr->target_rank] *
                            curr_ptr->target_disp);

        switch (curr_ptr->type)
        {
            case MPIDI_RMA_PUT:
                MPIU_Memcpy(target_addr, curr_ptr->origin_addr, msg_size);
                break;
            case MPIDI_RMA_GET:
                MPIU_Memcpy(curr_ptr->origin_addr, target_addr, msg_size);
                break;
            case MPIDI_RMA_ACCUMULATE:
            case MPIDI_RMA_ACC_CONTIG:
                if (curr_ptr->op == MPI_REPLACE) {
                    /* similar to Put operation */
                    mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, target_l_rank);
                    if (mpi_errno != MPI_SUCCESS) {
                           MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                                "**fail %s", "mutex lock error");
                    }    
                    MPIU_Memcpy(target_addr, curr_ptr->origin_addr, msg_size);
                    mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, target_l_rank);
                    if (mpi_errno != MPI_SUCCESS) {
                           MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                                "**fail %s", "mutex unlock error");
                    } 
                } else if (HANDLE_GET_KIND(curr_ptr->op) 
                                == HANDLE_KIND_BUILTIN) {
                    uop = MPIR_Op_table[(curr_ptr->op)%16 - 1];
                    mpi_errno = MPIDI_CH3I_SHM_win_mutex_lock(win_ptr, target_l_rank);
                    if (mpi_errno != MPI_SUCCESS) {
                           MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                                "**fail %s", "mutex lock error");
                    } 
                    (*uop)(curr_ptr->origin_addr, target_addr,
                        &(curr_ptr->target_count), 
                        &(curr_ptr->target_datatype));
                    mpi_errno = MPIDI_CH3I_SHM_win_mutex_unlock(win_ptr, target_l_rank);
                    if (mpi_errno != MPI_SUCCESS) {
                           MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                                "**fail %s", "mutex unlock error");
                    } 
                } else {
                    /* --BEGIN ERROR HANDLING-- */
                    mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, 
                        MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OP, 
                        "**opnotpredefined", "**opnotpredefined %d", 
                        curr_ptr->op);
                    return mpi_errno;
                }
                break;
            default:
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                continue;
        }

        win_ptr->rma_issued++;
        if (*list_head == curr_ptr)
        {
            if (*list_head == *list_tail)
            {
               *list_head = *list_tail = NULL;
            }
            else
            {
               *list_head = prev_ptr = curr_ptr->next;
            }
        }
        else if (*list_tail == curr_ptr)
        {
            *list_tail = prev_ptr;
            (*list_tail)->next = NULL;
        }
        else
        {
            prev_ptr->next = curr_ptr->next;
        }
        tmp_ptr = curr_ptr->next;
        MPIU_Free(curr_ptr);
        curr_ptr = tmp_ptr;
    }

fn_fail:
    return mpi_errno;
}

/* For active synchronization */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_post
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int MPIDI_CH3I_RDMA_post(MPID_Win * win_ptr, int target_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    int mpi_errno = MPI_SUCCESS;

    char                *origin_addr, *remote_addr;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;
    uint32_t            i, size, hca_index, 
                        r_key[MAX_NUM_SUBRAILS],
                        l_key[MAX_NUM_SUBRAILS];

    /*part 1 prepare origin side buffer */
    remote_addr = (char *) win_ptr->remote_post_flags[target_rank];

    size = sizeof(int);
    Get_Pinned_Buf(win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    for (i=0; i<rdma_num_rails; ++i) {
        hca_index = i/rdma_num_rails_per_hca;
        l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
        r_key[i] = win_ptr->post_flag_rkeys[target_rank * rdma_num_hcas + hca_index];
    }

    Post_Put_Put_Get_List(win_ptr, -1, NULL, 
            tmp_vc, (void *)&origin_addr, 
            (void*)&remote_addr, size, 
            l_key, r_key, SINGLE);

    win_ptr->poll_flag = 1;
    while (win_ptr->poll_flag == 1)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    return mpi_errno;
}

void
MPIDI_CH3I_RDMA_win_create (void *base,
                            MPI_Aint size,
                            int comm_size,
                            int my_rank,
                            MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
 
    int             ret, i,j,arrIndex;
    int             errflag = FALSE;
    win_info        *win_info_exchange;
    uintptr_t       *cc_ptrs_exchange;
    uintptr_t       *post_flag_ptr_send, *post_flag_ptr_recv;
    int             fallback_trigger = 0;

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided)
    {
        (*win_ptr)->fall_back = 1;
        return;
    }

    /*Allocate structure for window information exchange*/
    win_info_exchange = MPIU_Malloc(comm_size * sizeof(win_info));
    if (!win_info_exchange)
    {
        DEBUG_PRINT("Error malloc win_info_exchange when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /*Allocate memory for completion counter pointers exchange*/
    cc_ptrs_exchange =  MPIU_Malloc(comm_size * sizeof(uintptr_t) * rdma_num_rails);
    if (!cc_ptrs_exchange)
    {
        DEBUG_PRINT("Error malloc cc_ptrs_exchangee when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    (*win_ptr)->fall_back = 0;
    /*Register the exposed buffer for this window */
    if (base != NULL && size > 0) {

        (*win_ptr)->win_dreg_entry = dreg_register(base, size);
        if (NULL == (*win_ptr)->win_dreg_entry) {
            (*win_ptr)->fall_back = 1;
            goto err_base_register;
        }
        for (i=0; i < rdma_num_hcas; ++i) {
            win_info_exchange[my_rank].win_rkeys[i] = 
                 (uint32_t) (*win_ptr)->win_dreg_entry->memhandle[i]->rkey;
        }
    } else {
        (*win_ptr)->win_dreg_entry = NULL;
        for (i = 0; i < rdma_num_hcas; ++i) {
             win_info_exchange[my_rank].win_rkeys[i] = 0;
        }
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter = MPIU_Malloc(sizeof(long long) * comm_size 
                * rdma_num_rails);
    if (NULL == (*win_ptr)->completion_counter) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_buf;
    }

    MPIU_Memset((void *) (*win_ptr)->completion_counter, 0, sizeof(long long)   
            * comm_size * rdma_num_rails);

    (*win_ptr)->completion_counter_dreg_entry = dreg_register(
        (void*)(*win_ptr)->completion_counter, sizeof(long long) * comm_size 
               * rdma_num_rails);
    if (NULL == (*win_ptr)->completion_counter_dreg_entry) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_cc_register;
    }

    for (i = 0; i < rdma_num_rails; ++i){
        cc_ptrs_exchange[my_rank * rdma_num_rails + i] =
               (uintptr_t) ((*win_ptr)->completion_counter + i);
    }

    for (i = 0; i < rdma_num_hcas; ++i){
        win_info_exchange[my_rank].completion_counter_rkeys[i] =
               (uint32_t) (*win_ptr)->completion_counter_dreg_entry->
                            memhandle[i]->rkey;
    }

    /*Register buffer for post flags : from target to origin */
    (*win_ptr)->post_flag = (int *) MPIU_Malloc(comm_size * sizeof(int)); 
    if (!(*win_ptr)->post_flag) {
        (*win_ptr)->fall_back = 1;
        goto err_postflag_buf;
    }
    DEBUG_PRINT(
        "rank[%d] : post flag start before exchange is %p\n",
        my_rank,
        (*win_ptr)->post_flag
    ); 
  
    /* Register the post flag */
    (*win_ptr)->post_flag_dreg_entry = 
                dreg_register((void*)(*win_ptr)->post_flag, 
                               sizeof(int)*comm_size);
    if (NULL == (*win_ptr)->post_flag_dreg_entry) {
        /* Fallback case */
        (*win_ptr)->fall_back = 1;
        goto err_postflag_register;
    }

    for (i = 0; i < rdma_num_hcas; ++i)
    {
        win_info_exchange[my_rank].post_flag_rkeys[i] = 
              (uint32_t)((*win_ptr)->post_flag_dreg_entry->memhandle[i]->rkey);
        DEBUG_PRINT(
            "the rank [%d] post_flag rkey before exchange is %x\n", 
            my_rank,
            win_info_exchange[my_rank].post_flag_rkeys[i]
        );
    }

    /* Malloc Pinned buffer for one sided communication */
    (*win_ptr)->pinnedpool_1sc_buf = MPIU_Malloc(rdma_pin_pool_size);
    if (!(*win_ptr)->pinnedpool_1sc_buf) {
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_buf;
    }
    
    (*win_ptr)->pinnedpool_1sc_dentry = dreg_register(
                        (*win_ptr)->pinnedpool_1sc_buf,
                        rdma_pin_pool_size
    );
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry) {
        /* FallBack case */
        (*win_ptr)->fall_back = 1;
        goto err_pinnedpool_register;
    }

    (*win_ptr)->pinnedpool_1sc_index = 0;

    win_info_exchange[my_rank].fall_back = (*win_ptr)->fall_back;    

    /*Exchange the information about rkeys and addresses */
    /* All processes will exchange the setup keys *
     * since each process has the same data for all other
     * processes, use allgather */

    ret = MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, win_info_exchange,
               sizeof(win_info), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather win_info  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* check if any peers fail */
    for (i = 0; i < comm_size; ++i)
    {
            if (win_info_exchange[i].fall_back != 0)
            {
                fallback_trigger = 1;
            }
    } 
    
    if (fallback_trigger) {
        MPIU_Free((void *) win_info_exchange);
        MPIU_Free((void *) cc_ptrs_exchange);
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
        MPIU_Free((void *) (*win_ptr)->pinnedpool_1sc_buf);
        dreg_unregister((*win_ptr)->post_flag_dreg_entry);
        MPIU_Free((void *) (*win_ptr)->post_flag);
        dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
        MPIU_Free((void *) (*win_ptr)->completion_counter);
        dreg_unregister((*win_ptr)->win_dreg_entry);
        (*win_ptr)->fall_back = 1;
        goto fn_exit;
    }

    ret = MPIR_Allgather(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, cc_ptrs_exchange,
             rdma_num_rails*sizeof(uintptr_t), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error cc pointer  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey array for all other processes */
    (*win_ptr)->win_rkeys = (uint32_t *) MPIU_Malloc(comm_size * sizeof(uint32_t) 
                             * rdma_num_hcas);
    if (!(*win_ptr)->win_rkeys) {
        DEBUG_PRINT("Error malloc win->win_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the rkey2 array for all other processes */
    (*win_ptr)->completion_counter_rkeys = (uint32_t *) MPIU_Malloc(comm_size * 
                             sizeof(uint32_t) * rdma_num_hcas); 
    if (!(*win_ptr)->completion_counter_rkeys) {
        DEBUG_PRINT("Error malloc win->completion_counter_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the completion counter array for all other processes */
    (*win_ptr)->all_completion_counter = (long long **) MPIU_Malloc(comm_size * 
                             sizeof(long long *) * rdma_num_rails);
    if (!(*win_ptr)->all_completion_counter) {
        DEBUG_PRINT
            ("error malloc win->all_completion_counter when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the post flag rkey array for all other processes */
    (*win_ptr)->post_flag_rkeys = (uint32_t *) MPIU_Malloc(comm_size *
                                                  sizeof(uint32_t) * rdma_num_hcas);
    if (!(*win_ptr)->post_flag_rkeys) {
        DEBUG_PRINT("error malloc win->post_flag_rkeys when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* Now allocate the post flag ptr array for all other processes */
    (*win_ptr)->remote_post_flags =
        (int **) MPIU_Malloc(comm_size * sizeof(int *));
    if (!(*win_ptr)->remote_post_flags) {
        DEBUG_PRINT
            ("error malloc win->remote_post_flags when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_hcas; ++j) 
        {
            arrIndex = rdma_num_hcas * i + j;
            (*win_ptr)->win_rkeys[arrIndex] = 
                    win_info_exchange[i].win_rkeys[j];
            (*win_ptr)->completion_counter_rkeys[arrIndex] = 
                    win_info_exchange[i].completion_counter_rkeys[j];
            (*win_ptr)->post_flag_rkeys[arrIndex] = 
                    win_info_exchange[i].post_flag_rkeys[j];
        }
    }

    for (i = 0; i < comm_size; ++i)
    {
        for (j = 0; j < rdma_num_rails; ++j)
        {
            arrIndex = rdma_num_rails * i + j;
            (*win_ptr)->all_completion_counter[arrIndex] = (long long *)
                    ((size_t)(cc_ptrs_exchange[arrIndex])
                    + sizeof(long long) * my_rank * rdma_num_rails);
        }
    }

    post_flag_ptr_send = (uintptr_t *) MPIU_Malloc(comm_size * 
            sizeof(uintptr_t));
    if (!post_flag_ptr_send) {
        DEBUG_PRINT("Error malloc post_flag_ptr_send when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    post_flag_ptr_recv = (uintptr_t *) MPIU_Malloc(comm_size * 
            sizeof(uintptr_t));
    if (!post_flag_ptr_recv) {
        DEBUG_PRINT("Error malloc post_flag_ptr_recv when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    /* use all to all to exchange rkey and address for post flag */
    for (i = 0; i < comm_size; ++i)
    {
        (*win_ptr)->post_flag[i] = i != my_rank ? 0 : 1;
        post_flag_ptr_send[i] = (uintptr_t) &((*win_ptr)->post_flag[i]);
    }

    /* use all to all to exchange the address of post flag */
    ret = MPIR_Alltoall_impl(post_flag_ptr_send, sizeof(uintptr_t), MPI_BYTE, 
            post_flag_ptr_recv, sizeof(uintptr_t), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather post flag ptr  when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

    for (i = 0; i < comm_size; ++i) {
        (*win_ptr)->remote_post_flags[i] = (int *) post_flag_ptr_recv[i];
        DEBUG_PRINT(" rank is %d remote rank %d,  post flag addr is %p\n",
                my_rank, i, (*win_ptr)->remote_post_flags[i]);
    }

    MPIU_Free(win_info_exchange);
    MPIU_Free(cc_ptrs_exchange);
    MPIU_Free(post_flag_ptr_send);
    MPIU_Free(post_flag_ptr_recv);
    (*win_ptr)->using_lock = 0;
    (*win_ptr)->using_start = 0;
    /* Initialize put/get queue */
    (*win_ptr)->put_get_list_size = 0;
    (*win_ptr)->put_get_list_tail = 0;
    (*win_ptr)->wait_for_complete = 0;
    (*win_ptr)->rma_issued = 0;

    (*win_ptr)->put_get_list =
        (MPIDI_CH3I_RDMA_put_get_list *) MPIU_Malloc( 
            rdma_default_put_get_list_size *
            sizeof(MPIDI_CH3I_RDMA_put_get_list));
    if (!(*win_ptr)->put_get_list) {
        DEBUG_PRINT("Fail to malloc space for window put get list\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }

fn_exit:
    
    if (1 == (*win_ptr)->fall_back) {
        (*win_ptr)->using_lock = 0;
        (*win_ptr)->using_start = 0;
        /* Initialize put/get queue */
        (*win_ptr)->put_get_list_size = 0;
        (*win_ptr)->put_get_list_tail = 0;
        (*win_ptr)->wait_for_complete = 0;
        (*win_ptr)->rma_issued = 0;
    }

    return;

  err_pinnedpool_register:
    MPIU_Free((void *) (*win_ptr)->pinnedpool_1sc_buf);
  err_pinnedpool_buf:
    dreg_unregister((*win_ptr)->post_flag_dreg_entry);
  err_postflag_register:
    MPIU_Free((void *) (*win_ptr)->post_flag);
  err_postflag_buf:
    dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
  err_cc_register:
    MPIU_Free((void *) (*win_ptr)->completion_counter);
  err_cc_buf:
    dreg_unregister((*win_ptr)->win_dreg_entry);
  err_base_register:
    win_info_exchange[my_rank].fall_back = (*win_ptr)->fall_back;

    ret = MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, win_info_exchange, 
                      sizeof(win_info), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS) {
        DEBUG_PRINT("Error gather window information when creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc");
    }
 
    MPIU_Free((void *) win_info_exchange);
    MPIU_Free((void *) cc_ptrs_exchange);
    goto fn_exit;
     
}

void mv2_rma_init_rank_info (MPID_Win ** win_ptr) 
{
    int             i, j, num_local_procs, comm_size;
    MPIDI_VC_t*     vc;
    MPID_Comm       *comm_ptr;

    comm_ptr = (*win_ptr)->comm_ptr;
    comm_size = comm_ptr->local_size;
    num_local_procs = 0;
    for(i=0; i<comm_size; i++) {
        MPIDI_Comm_get_vc(comm_ptr, i, &vc);
        if((*win_ptr)->my_id == i || vc->smp.local_nodes != -1)
        {
           num_local_procs++;
        }
    } 

    (*win_ptr)->shm_l_ranks = num_local_procs;
    (*win_ptr)->shm_l2g_rank = (int *)
                  MPIU_Malloc(num_local_procs * sizeof(int));
    if((*win_ptr)->shm_l2g_rank == NULL) {
        ibv_error_abort (GEN_EXIT_ERR, 
               "rdma_iba_1sc: error allocating shm_l2g_rank");
    }

    (*win_ptr)->shm_g2l_rank = (int *)
                  MPIU_Malloc(comm_size * sizeof(int));
    if((*win_ptr)->shm_g2l_rank == NULL) {
        ibv_error_abort (GEN_EXIT_ERR, 
               "rdma_iba_1sc: error allocating g2l_rank");
    }

    j=0;
    for(i=0; i<comm_size; i++) {
        MPIDI_Comm_get_vc(comm_ptr, i, &vc);
        if((*win_ptr)->my_id == i || vc->smp.local_nodes != -1) {
            (*win_ptr)->shm_l2g_rank[j] = i;
            (*win_ptr)->shm_g2l_rank[i] = j;
            j++;
        } else {
            (*win_ptr)->shm_g2l_rank[i] = -1;
        }
    }
}

int mv2_rma_allocate_counters(MPID_Win ** win_ptr)
{
    int             i, mpi_errno=MPI_SUCCESS;
    int             num_local_procs, l_rank, g_rank;
    int             cmpl_counter_size, post_flag_size, mutex_size;
    int             lock_size;
    long long       *shm_cmpl_counter_buf;
    int             *shm_post_flag_buf;
    MPID_Comm       *comm_ptr;
    pthread_mutexattr_t attr;

    g_rank = (*win_ptr)->my_id;
    l_rank = (*win_ptr)->shm_g2l_rank[g_rank]; 
    num_local_procs = (*win_ptr)->shm_l_ranks;

    comm_ptr = (*win_ptr)->comm_ptr;

    (*win_ptr)->shm_fd = -1;
    (*win_ptr)->shm_control_buf = NULL;
    (*win_ptr)->shm_post_flag_all = NULL;
    (*win_ptr)->shm_cmpl_counter_all = NULL;
    (*win_ptr)->shm_mutex = NULL; 

    cmpl_counter_size = sizeof(long long) * num_local_procs * num_local_procs;
    post_flag_size = sizeof(int) * num_local_procs * num_local_procs;
    mutex_size = sizeof(pthread_mutex_t) * num_local_procs;
    /*locks requires 3 counters per process: lock, shared lock counter and 
      lock released flag*/
    lock_size = 2 * sizeof(int) * num_local_procs + sizeof(long) 
                    * num_local_procs;
    (*win_ptr)->shm_control_bufsize = cmpl_counter_size + post_flag_size 
                   + mutex_size + lock_size;
    mpi_errno = mv2_rma_allocate_shm(
                              (*win_ptr)->shm_control_bufsize,
                              g_rank,
                              &((*win_ptr)->shm_fd),
                              (void **) &((*win_ptr)->shm_control_buf),
                              comm_ptr);
    if (mpi_errno != MPI_SUCCESS) {
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","shared memory allocation failed");
    } 
 
    shm_cmpl_counter_buf=(long long*)((*win_ptr)->shm_control_buf); 
    (*win_ptr)->shm_cmpl_counter_all = (long long **) MPIU_Malloc(sizeof(long long *) *
                                     num_local_procs);
    if(!(*win_ptr)->shm_cmpl_counter_all) {
       ibv_error_abort (GEN_EXIT_ERR, 
               "rdma_iba_1sc: error allocating shm_cmpl_counter_all");
    }
  
    for(i=0; i<num_local_procs; i++) {
        (*win_ptr)->shm_cmpl_counter_all[i] = 
                       shm_cmpl_counter_buf + num_local_procs * i;
        if(i == l_rank) {
            (*win_ptr)->shm_cmpl_counter_me = 
                       shm_cmpl_counter_buf + num_local_procs * i;
        }
    }
    
    shm_post_flag_buf = (int*)(((uint64_t)(*win_ptr)->shm_control_buf) 
                       + cmpl_counter_size); 
    (*win_ptr)->shm_post_flag_all = (int **) MPIU_Malloc(sizeof(int *) *
                                                  num_local_procs);
    if(!(*win_ptr)->shm_post_flag_all) {
       ibv_error_abort (GEN_EXIT_ERR, 
                      "rdma_iba_1sc: error allocating shm_post_flag_all");
    }
 
    for(i = 0; i<num_local_procs; i++) {
         (*win_ptr)->shm_post_flag_all[i] = 
                         shm_post_flag_buf + num_local_procs * i;

         if(i == l_rank) {
              (*win_ptr)->shm_post_flag_me = 
                         shm_post_flag_buf + num_local_procs * i;
         }
    }

    (*win_ptr)->shm_mutex = (pthread_mutex_t *)((uint64_t)(*win_ptr)->shm_control_buf 
                       + cmpl_counter_size + post_flag_size);

    /*Initialize pthread mutex attribute and initialize pthread mutex*/
    mpi_errno = pthread_mutexattr_init(&attr);
    if (mpi_errno != MPI_SUCCESS) {
        (*win_ptr)->shm_mutex = NULL;
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","mutex attr initialization failed");
    }
    mpi_errno = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (mpi_errno != MPI_SUCCESS) {
        (*win_ptr)->shm_mutex = NULL;
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","mutex pshared initialization failed");
    }
    mpi_errno = pthread_mutex_init(&(*win_ptr)->shm_mutex[l_rank], &attr);
    if (mpi_errno != MPI_SUCCESS) {
        (*win_ptr)->shm_mutex = NULL;
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","mutex initialization failed");
    }
    mpi_errno = pthread_mutexattr_destroy(&attr);
    if (mpi_errno != MPI_SUCCESS) {
        (*win_ptr)->shm_mutex = NULL;
        MPIU_ERR_SETANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_exit,
                   "**fail", "**fail %s","mutex attr destroy failed");
    }

    (*win_ptr)->shm_lock = (int *)((uint64_t)(*win_ptr)->shm_control_buf
                       + cmpl_counter_size + post_flag_size + mutex_size);
    (*win_ptr)->shm_shared_lock_count = (long *)((uint64_t)(*win_ptr)->shm_lock  
                       + sizeof(int) * num_local_procs);
    (*win_ptr)->shm_lock_released = 
                       (int *)((uint64_t)(*win_ptr)->shm_shared_lock_count
                       + sizeof(long) * num_local_procs); 

fn_exit:
    return mpi_errno;
}

void mv2_rma_free_counters(MPID_Win ** win_ptr)
{
    int l_rank;

    l_rank = (*win_ptr)->shm_g2l_rank[(*win_ptr)->my_id];

    if ((*win_ptr)->shm_mutex != NULL) { 
        pthread_mutex_destroy(&(*win_ptr)->shm_mutex[l_rank]);
    }
    if ((*win_ptr)->shm_control_buf != NULL) { 
        mv2_rma_deallocate_shm((*win_ptr)->shm_control_buf, 
                (*win_ptr)->shm_control_bufsize);
    }
    if ((*win_ptr)->shm_fd != -1) {
        close((*win_ptr)->shm_fd);
    }
    if ((*win_ptr)->shm_post_flag_all != NULL) { 
        MPIU_Free((*win_ptr)->shm_post_flag_all);
    }
    if ((*win_ptr)->shm_cmpl_counter_all != NULL) { 
        MPIU_Free((*win_ptr)->shm_cmpl_counter_all);
    }
    MPIU_Free((*win_ptr)->shm_l2g_rank);
    MPIU_Free((*win_ptr)->shm_g2l_rank);
} 

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_win_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_LIMIC_win_create(void *base, MPI_Aint size, MPID_Win ** win_ptr)
{
    int             mpi_errno=MPI_SUCCESS;
    int             errflag = FALSE;
    int             g_rank, comm_size, fallback;
    MPID_Comm       *comm_ptr;
    size_t          tx_init_size;

    if(!SMP_INIT || !g_smp_use_limic2 || 
            !MPIDI_CH3I_RDMA_Process.has_limic_one_sided) {
       (*win_ptr)->limic_fallback = 1;
       return;
    }

    g_rank = (*win_ptr)->my_id;
    comm_ptr = (*win_ptr)->comm_ptr;
    comm_size = comm_ptr->local_size;

    /*initialize helper rank info in the window object*/
    mv2_rma_init_rank_info (win_ptr);

    /*allocate shared memory buffers for post and complete synchronization*/
    fallback = 0;
    mpi_errno = mv2_rma_allocate_counters(win_ptr);
    if (mpi_errno != MPI_SUCCESS) {
        DEBUG_PRINT("rma counters allocation failed \n");
        fallback = 1;
    }

    mpi_errno = MPIR_Allreduce_impl(MPI_IN_PLACE, &fallback, 1, 
                   MPI_INT, MPI_SUM, comm_ptr, &errflag);
    if (mpi_errno != MPI_SUCCESS) {
        ibv_error_abort (GEN_EXIT_ERR,
                    "rdma_iba_1sc: error calling allreduce");
    }

    if (fallback > 0) {
       (*win_ptr)->limic_fallback = 1;
       mv2_rma_free_counters(win_ptr); 
       return;
    }

    (*win_ptr)->peer_lu = (limic_user *) 
                  MPIU_Malloc(comm_size * sizeof(limic_user));
    if((*win_ptr)->peer_lu == NULL) {
        ibv_error_abort (GEN_EXIT_ERR, 
                    "rdma_iba_1sc: error allocating peer_lu");
    }
    MPIU_Memset((*win_ptr)->peer_lu,0,comm_size * sizeof(limic_user));

    /*map the window memory*/
    if (base != NULL && size > 0) {
      tx_init_size = limic_tx_init(limic_fd, base, size,
                   &((*win_ptr)->peer_lu[g_rank]));
      if (tx_init_size != size) {
         ibv_error_abort (GEN_EXIT_ERR, 
                    "rdma_iba_1sc: error calling limic_tx_init");
      }
    }

    mpi_errno = MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
         (*win_ptr)->peer_lu, sizeof(limic_user), MPI_BYTE,
         comm_ptr, &errflag);
    if (mpi_errno != MPI_SUCCESS) {
        ibv_error_abort (GEN_EXIT_ERR, 
                    "rdma_iba_1sc: error calling allgather");
    }

    return;
}  
#endif /* _SMP_LIMIC_ */

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_win_create
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_SHM_win_create(void *base, MPI_Aint size, MPID_Win ** win_ptr)
{
    int                 i, mpi_errno=MPI_SUCCESS;
    int                 errflag = FALSE;
    int                 num_local_procs, remote_rank, g_rank, comm_size;
    int                 found, new_fd;
    void                *new_buffer = NULL;
    shm_buffer          *curr_ptr = NULL, *prev_ptr = NULL, *shm_buffer_ptr = NULL;
    shm_buffer          *remote_buffer = NULL, *new_shm_buffer = NULL;
    file_info           *file_info_exchange = NULL;
    MPID_Comm           *comm_ptr;

    (*win_ptr)->shm_fallback = 0;
    if(!SMP_INIT) {
       (*win_ptr)->shm_fallback = 1;
       goto fn_exit;
    }

    /*initialize helper rank info in the window object*/
    mv2_rma_init_rank_info (win_ptr);

    num_local_procs = (*win_ptr)->shm_l_ranks;
    g_rank = (*win_ptr)->my_id; 
    comm_ptr = (*win_ptr)->comm_ptr;
    comm_size = comm_ptr->local_size; 

    /*allocating structures to exchange information*/
    file_info_exchange = (file_info *)
        MPIU_Malloc(sizeof(file_info) * comm_size);
    if(NULL == file_info_exchange) {
        ibv_error_abort (GEN_EXIT_ERR,
                 "rdma_iba_1sc: error allocating file_info_exchange");
    }
    MPIU_Memset(file_info_exchange, 0, sizeof(file_info)*comm_size);

    if (size > 0) {
       if(NULL != shm_buffer_llist) {
          /*check if the specified buffer was allocated in shared memory*/
          curr_ptr = shm_buffer_llist;
          prev_ptr = shm_buffer_llist;
          while(curr_ptr) {
             if((size_t) curr_ptr->ptr > (size_t) base) {
                   break;
             }
             prev_ptr = curr_ptr;
             curr_ptr = curr_ptr->next;
          }

          if((size_t) prev_ptr->ptr <= (size_t) base &&
             (size_t) prev_ptr->ptr + prev_ptr->size >= (size_t) base + size) {
              shm_buffer_ptr = prev_ptr;
              MPIU_Memcpy(file_info_exchange[g_rank].filename,
                         shm_buffer_ptr->filename,
                         SHM_FILENAME_LEN);
              file_info_exchange[g_rank].size = shm_buffer_ptr->size;
              file_info_exchange[g_rank].displacement =
                   (size_t) base - (size_t) shm_buffer_ptr->ptr;
              file_info_exchange[g_rank].shm_fallback = 0;
          }
          else {
              file_info_exchange[g_rank].shm_fallback = 1;
          }
      }
      else {
          file_info_exchange[g_rank].shm_fallback = 1;
      }
    } else {
       file_info_exchange[g_rank].size = 0;
       file_info_exchange[g_rank].shm_fallback = 1;
    }

    /*allocate shared memory buffers for post and complete synchronization*/
    mpi_errno = mv2_rma_allocate_counters(win_ptr);
    if (mpi_errno != MPI_SUCCESS) {
        file_info_exchange[g_rank].shm_fallback = 1;
    }

    /*check if all local nodes are participating using shared memory*/
    mpi_errno = MPIR_Allgather_impl(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
         file_info_exchange, sizeof(file_info), MPI_BYTE,
         comm_ptr, &errflag);
    if (mpi_errno != MPI_SUCCESS) {
        ibv_error_abort (GEN_EXIT_ERR,
                 "rdma_iba_1sc: allgather failed");
    }

    for(i=0; i<comm_size; i++) {
       if(file_info_exchange[i].shm_fallback == 1) {
           (*win_ptr)->shm_fallback = 1;
           mv2_rma_free_counters(win_ptr);
           goto fn_exit;
       }
    }
    (*win_ptr)->shm_fallback = 0;

    /*allocating space to store remote addresses under window structure*/
    (*win_ptr)->shm_win_buffer_all = MPIU_Malloc(sizeof(void *) *
                 num_local_procs);
    if(NULL == (*win_ptr)->shm_win_buffer_all) {
        ibv_error_abort (GEN_EXIT_ERR, 
                 "rdma_iba_1sc: error allocating shm_win_buffer_all");
    }
    (*win_ptr)->shm_buffer_all = MPIU_Malloc(sizeof(void *) *
                 num_local_procs);
    if(NULL == (*win_ptr)->shm_buffer_all) {
        ibv_error_abort (GEN_EXIT_ERR, 
                 "rdma_iba_1sc: error allocating shm_buffer_all");
    }

    /*map the remote window memory*/
    for (i=0; i<num_local_procs; i++) {
        remote_rank = (*win_ptr)->shm_l2g_rank[i];
        if (g_rank != remote_rank) {
          /*check if this had already been mapped*/
          found=0;
          if (NULL != shm_buffer_rlist) {
             remote_buffer = shm_buffer_rlist;
             while (remote_buffer) {
                if (strcmp(remote_buffer->filename,
                    file_info_exchange[remote_rank].filename) == 0) {
                    found = 1;
                    break;
                }
                remote_buffer = remote_buffer->next;
             }
          }

          if(found == 0) {
              /*open the file*/
              new_fd = shm_open(file_info_exchange[remote_rank].filename,
                     O_RDWR, S_IRWXU);
              if (-1 == new_fd) {
                 ibv_error_abort (GEN_EXIT_ERR, 
                        "rdma_iba_1sc: error in shm_open \n");
              }

              /*mapping memory to the shared file*/
              new_buffer = mmap(0, file_info_exchange[remote_rank].size,
                   PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE | MAP_LOCKED, new_fd, 0);
              if (MAP_FAILED == new_buffer) {
                 ibv_error_abort (GEN_EXIT_ERR, "rdma_iba_1sc: error in mmap");
              }

              /*enqueue the buffer into shm_buffer_rlist*/
              new_shm_buffer = MPIU_Malloc(sizeof(shm_buffer));
              if (NULL == new_shm_buffer) {
                 ibv_error_abort (GEN_EXIT_ERR, 
                        "rdma_iba_1sc: error allocating new_shm_buffer");
              }
              MPIU_Memcpy(new_shm_buffer->filename,
                          file_info_exchange[remote_rank].filename,
                          SHM_FILENAME_LEN);
              new_shm_buffer->ptr = new_buffer;
              new_shm_buffer->size = size;
              new_shm_buffer->fd = new_fd;
              new_shm_buffer->owner = remote_rank;
              new_shm_buffer->ref_count = 1;
              new_shm_buffer->next = shm_buffer_rlist;
              shm_buffer_rlist = new_shm_buffer;
              /*fill shared buffer info into window structure*/
              (*win_ptr)->shm_buffer_all[i] = (void *) new_shm_buffer;
              (*win_ptr)->shm_win_buffer_all[i] = (void *) ((size_t) new_buffer +
                          file_info_exchange[remote_rank].displacement);
           } else { 
                (*win_ptr)->shm_buffer_all[i] = (void *) remote_buffer;
                (*win_ptr)->shm_win_buffer_all[i] = (void *) ((size_t) remote_buffer->ptr +
                          file_info_exchange[remote_rank].displacement);
                remote_buffer->ref_count++;
           }
       } else {
          (*win_ptr)->shm_buffer_all[i] = (void *) shm_buffer_ptr;
          (*win_ptr)->shm_win_buffer_all[i] = (void *) ((size_t) shm_buffer_ptr->ptr +
                          file_info_exchange[remote_rank].displacement);
          shm_buffer_ptr->ref_count++;
       }
    }

fn_exit:
    if (file_info_exchange) {
       MPIU_Free(file_info_exchange);
    }
    return;
}

void MPIDI_CH3I_RDMA_win_free(MPID_Win** win_ptr)
{
    if ((*win_ptr)->win_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->win_dreg_entry);
    }

    MPIU_Free((*win_ptr)->win_rkeys);
    MPIU_Free((*win_ptr)->completion_counter_rkeys);
    MPIU_Free((void *) (*win_ptr)->post_flag);
    MPIU_Free((*win_ptr)->post_flag_rkeys);
    MPIU_Free((*win_ptr)->remote_post_flags);
    MPIU_Free((*win_ptr)->put_get_list);

    if ((*win_ptr)->pinnedpool_1sc_dentry) {
        dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
    }

    MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
    if ((*win_ptr)->completion_counter_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
    }

    if ((*win_ptr)->post_flag_dreg_entry != NULL) {
        dreg_unregister((*win_ptr)->post_flag_dreg_entry);
    }

    MPIU_Free((void *) (*win_ptr)->completion_counter);
    MPIU_Free((void *) (*win_ptr)->all_completion_counter);
}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_LIMIC_win_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_LIMIC_win_free(MPID_Win** win_ptr)
{
    /*deallocated post and complete counters*/
    mv2_rma_free_counters(win_ptr);

    MPIU_Free((*win_ptr)->peer_lu);
}
#endif /*defined(_SMP_LIMIC_)*/

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SHM_win_free
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
void MPIDI_CH3I_SHM_win_free(MPID_Win ** win_ptr)
{
    int                 i;

    /*deallocated post and complete counters*/
    mv2_rma_free_counters(win_ptr);

    for(i=0; i<(*win_ptr)->shm_l_ranks; i++) {
       ((shm_buffer *) (*win_ptr)->shm_buffer_all[i])->ref_count--;
    }

    MPIU_Free((*win_ptr)->shm_win_buffer_all);
    MPIU_Free((*win_ptr)->shm_buffer_all);
}

static int Decrease_CC(MPID_Win * win_ptr, int target_rank)
{
    int                 i;
    int                 mpi_errno = MPI_SUCCESS;
    uint32_t            r_key2[MAX_NUM_SUBRAILS], l_key2[MAX_NUM_SUBRAILS];
    char                *cc = NULL;
    int hca_index;
    void * remote_addr[MAX_NUM_SUBRAILS], *local_addr[MAX_NUM_SUBRAILS];

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, target_rank, &tmp_vc);

    
    Get_Pinned_Buf(win_ptr, &cc, sizeof(long long));

    for (i=0; i<rdma_num_rails; ++i) { 
            hca_index = i/rdma_num_rails_per_hca;
            remote_addr[i]    = (void *)(uintptr_t)
                (win_ptr->all_completion_counter[target_rank*rdma_num_rails+i]);
	    *((long long *) cc) = 1;
            local_addr[i]     = (void *)cc;
            l_key2[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[hca_index]->lkey;
            r_key2[i] = win_ptr->completion_counter_rkeys[target_rank*rdma_num_hcas + hca_index];
    }
 
    Post_Put_Put_Get_List(win_ptr, -1, NULL, tmp_vc, 
                   local_addr, remote_addr, sizeof (long long), l_key2, r_key2, REPLICATE);
    return mpi_errno;
    
}

static int Post_Put_Put_Get_List(  MPID_Win * winptr, 
                            MPIDI_msg_sz_t size, 
                            dreg_entry * dreg_tmp,
                            MPIDI_VC_t * vc_ptr,
                            void *local_buf[], void *remote_buf[],
                            int length,
                            uint32_t lkeys[], uint32_t rkeys[],
                            rail_select_t rail_select)
{
    int mpi_errno = MPI_SUCCESS;
    int rail, i, index, count, bytes_per_rail, posting_length;
    void *local_address, *remote_address;
    vbuf *v;
    MPIDI_VC_t *save_vc = vc_ptr;

    index = winptr->put_get_list_tail;
    winptr->put_get_list[index].op_type     = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry   = dreg_tmp;
    winptr->put_get_list[index].data_size   = size;
    winptr->put_get_list[index].win_ptr     = winptr;
    winptr->put_get_list[index].vc_ptr      = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                    rdma_default_put_get_list_size;
    winptr->put_get_list[index].completion = 0;

    if (rail_select == STRIPE) { /* stripe the message across rails */
        /*post data in chunks of MPIDI_CH3I_RDMA_Process.maxtransfersize as long as the 
          total size per rail is larger than that*/
        count = 0;
        bytes_per_rail = length/rdma_num_rails;
        while (bytes_per_rail > MPIDI_CH3I_RDMA_Process.maxtransfersize) {
           for (i = 0; i < rdma_num_rails; ++i) {
              v = get_vbuf(); 
              if (NULL == v) {
                  MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
              }

              v->list = (void *)(&(winptr->put_get_list[index]));
              v->vc = (void *)vc_ptr;

              ++(winptr->put_get_list[index].completion);
              ++(winptr->put_get_list_size);

              local_address = (void *)((char*)local_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
                         + i*MPIDI_CH3I_RDMA_Process.maxtransfersize);
              remote_address = (void *)((char *)remote_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize
                         + i*MPIDI_CH3I_RDMA_Process.maxtransfersize);

              vbuf_init_rma_put(v, 
                                local_address, 
                                lkeys[i], 
                                remote_address,
                                rkeys[i], 
                                MPIDI_CH3I_RDMA_Process.maxtransfersize, 
                                i);
              ONESIDED_RDMA_POST(vc_ptr, save_vc, i);

           }
          
           ++count;
           length -= rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize;
           bytes_per_rail = length/rdma_num_rails; 
        }

        /* Post remaining data as length < rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
         * Still stripe if length > rdma_large_msg_rail_sharing_threshold*/
        if (length < rdma_large_msg_rail_sharing_threshold) { 
           rail = MRAILI_Send_select_rail(vc_ptr);
           v = get_vbuf();
           if (NULL == v) {
               MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
           }
 
           ++winptr->put_get_list[index].completion;
           ++(winptr->put_get_list_size);

           local_address = (void *)((char*)local_buf[0]
                      + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize);
           remote_address = (void *)((char *)remote_buf[0]
                      + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize);
 
           vbuf_init_rma_put(v, 
                             local_address, 
                             lkeys[rail], 
                             remote_address,
                             rkeys[rail], 
                             length, 
                             rail);
           v->list = (void *)(&(winptr->put_get_list[index]));
           v->vc = (void *)vc_ptr;

            ONESIDED_RDMA_POST(vc_ptr, NULL, rail);
 
        } else {
           for (i = 0; i < rdma_num_rails; ++i) {
              v = get_vbuf(); 
              if (NULL == v) {
                  MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
              }

              v->list = (void *)(&(winptr->put_get_list[index]));
              v->vc = (void *)vc_ptr;

              ++(winptr->put_get_list[index].completion);
              ++(winptr->put_get_list_size);

              local_address = (void *)((char*)local_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
                         + i*bytes_per_rail);
              remote_address = (void *)((char *)remote_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize
                         + i*bytes_per_rail);

              if (i < rdma_num_rails - 1) {
                 posting_length = bytes_per_rail;
              } else {
                 posting_length = length - (rdma_num_rails - 1)*bytes_per_rail;
              }

              vbuf_init_rma_put(v, 
                                local_address, 
                                lkeys[i], 
                                remote_address,
                                rkeys[i], 
                                posting_length, 
                                i);
              ONESIDED_RDMA_POST(vc_ptr, save_vc, i);

           }
        }
    } else if (rail_select == SINGLE) { /* send on a single rail */
        rail = MRAILI_Send_select_rail(vc_ptr);

        v = get_vbuf();
        if (NULL == v) {
            MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
        }

        v->list = (void *)(&(winptr->put_get_list[index]));
        v->vc = (void *)vc_ptr;

        ++winptr->put_get_list[index].completion;
        ++(winptr->put_get_list_size);

        vbuf_init_rma_put(v, 
                          local_buf[0], 
                          lkeys[rail], 
                          remote_buf[0],
                          rkeys[rail], 
                          length, 
                          rail);

        ONESIDED_RDMA_POST(vc_ptr, NULL, rail);

    } else if (rail_select == REPLICATE) { /* send on all rails */
        for (i = 0; i < rdma_num_rails; ++i) {
            v = get_vbuf(); 
            if (NULL == v) {
                MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
            }

            v->list = (void *)(&(winptr->put_get_list[index]));
            v->vc = (void *)vc_ptr;

            ++(winptr->put_get_list[index].completion);
            ++(winptr->put_get_list_size);

            vbuf_init_rma_put(v, 
                              local_buf[i], 
                              lkeys[i], 
                              remote_buf[i],
                              rkeys[i], 
                              length, 
                              i);

            ONESIDED_RDMA_POST(vc_ptr, save_vc, i);
 
        }
    }
 
    while (winptr->put_get_list_size >= rdma_default_put_get_list_size)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }
    
fn_fail:
    return mpi_errno;
}


static int Post_Get_Put_Get_List(  MPID_Win * winptr, 
                            MPIDI_msg_sz_t size, 
                            dreg_entry * dreg_tmp,
                            MPIDI_VC_t * vc_ptr,
                            void *local_buf[], void *remote_buf[],
                            void *user_buf[], int length,
                            uint32_t lkeys[], uint32_t rkeys[],
                            rail_select_t rail_select)
{
     int mpi_errno = MPI_SUCCESS;
     int i, rail, index, count, bytes_per_rail, posting_length;
     void *local_address, *remote_address;
     vbuf *v;
     MPIDI_VC_t *save_vc = vc_ptr;

     index = winptr->put_get_list_tail;
     if(size <= rdma_eagersize_1sc){    
         winptr->put_get_list[index].origin_addr = local_buf[0];
         winptr->put_get_list[index].target_addr = user_buf[0];
     } else {
         winptr->put_get_list[index].origin_addr = NULL;
         winptr->put_get_list[index].target_addr = NULL;
     }
     winptr->put_get_list[index].op_type     = SIGNAL_FOR_GET;
     winptr->put_get_list[index].mem_entry   = dreg_tmp;
     winptr->put_get_list[index].data_size   = size;
     winptr->put_get_list[index].win_ptr     = winptr;
     winptr->put_get_list[index].vc_ptr      = vc_ptr;
     winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
                                   rdma_default_put_get_list_size;
     winptr->put_get_list[index].completion = 0;

     if (rail_select == STRIPE) { /*stripe across the rails*/
        /*post data in chunks of MPIDI_CH3I_RDMA_Process.maxtransfersize as long as the 
          total size per rail is larger than that*/
        count = 0;
        bytes_per_rail = length/rdma_num_rails;
        while (bytes_per_rail > MPIDI_CH3I_RDMA_Process.maxtransfersize) {
           for (i = 0; i < rdma_num_rails; ++i) {
              v = get_vbuf(); 
              if (NULL == v) {
                  MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
              }

              v->list = (void *)(&(winptr->put_get_list[index]));
              v->vc = (void *)vc_ptr;

              ++(winptr->put_get_list[index].completion);
              ++(winptr->put_get_list_size);

              local_address = (void *)((char*)local_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
                         + i*MPIDI_CH3I_RDMA_Process.maxtransfersize);
              remote_address = (void *)((char *)remote_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize
                         + i*MPIDI_CH3I_RDMA_Process.maxtransfersize);

              vbuf_init_rma_get(v, 
                                local_address, 
                                lkeys[i], 
                                remote_address,
                                rkeys[i], 
                                MPIDI_CH3I_RDMA_Process.maxtransfersize, 
                                i);

              ONESIDED_RDMA_POST(vc_ptr, save_vc, i);

           }
          
           ++count;
           length -= rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize;
           bytes_per_rail = length/rdma_num_rails; 
        }

        /* Post remaining data as length < rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
         * Still stripe if length > rdma_large_msg_rail_sharing_threshold*/
        if (length < rdma_large_msg_rail_sharing_threshold) { 
           rail = MRAILI_Send_select_rail(vc_ptr);
           v = get_vbuf();
           if (NULL == v) {
               MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
           }
 
           ++winptr->put_get_list[index].completion;
           ++(winptr->put_get_list_size);

           local_address = (void *)((char*)local_buf[0]
                      + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize);
           remote_address = (void *)((char *)remote_buf[0]
                      + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize);
 
           vbuf_init_rma_get(v, 
                             local_address, 
                             lkeys[rail], 
                             remote_address,
                             rkeys[rail], 
                             length, 
                             rail);
           v->list = (void *)(&(winptr->put_get_list[index]));
           v->vc = (void *)vc_ptr;

           ONESIDED_RDMA_POST(vc_ptr, NULL, rail);
 
        } else {
           for (i = 0; i < rdma_num_rails; ++i) {
              v = get_vbuf(); 
              if (NULL == v) {
                  MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
              }

              v->list = (void *)(&(winptr->put_get_list[index]));
              v->vc = (void *)vc_ptr;

              ++(winptr->put_get_list[index].completion);
              ++(winptr->put_get_list_size);

              local_address = (void *)((char*)local_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize 
                         + i*bytes_per_rail);
              remote_address = (void *)((char *)remote_buf[0] 
                         + count*rdma_num_rails*MPIDI_CH3I_RDMA_Process.maxtransfersize
                         + i*bytes_per_rail);

              if (i < rdma_num_rails - 1) {
                 posting_length = bytes_per_rail;
              } else {
                 posting_length = length - (rdma_num_rails - 1)*bytes_per_rail;
              }

              vbuf_init_rma_get(v, 
                                local_address, 
                                lkeys[i], 
                                remote_address,
                                rkeys[i], 
                                posting_length, 
                                i);

              ONESIDED_RDMA_POST(vc_ptr, save_vc, i);

           }
        }
    } else if (rail_select == SINGLE) { /* send on a single rail */
        rail = MRAILI_Send_select_rail(vc_ptr);
        v = get_vbuf();
        if (NULL == v) {
            MPIU_ERR_SETFATALANDJUMP(mpi_errno, MPI_ERR_OTHER, "**nomem");
        }

        v->list = (void *)(&(winptr->put_get_list[index]));
        v->vc = (void *)vc_ptr;

        winptr->put_get_list[index].completion = 1;
        ++(winptr->put_get_list_size);

        vbuf_init_rma_get(v, local_buf[0], lkeys[rail], remote_buf[0],
                          rkeys[rail], length, rail);

        ONESIDED_RDMA_POST(vc_ptr, NULL, rail);

    }

    while (winptr->put_get_list_size >= rdma_default_put_get_list_size)
    {
        if ((mpi_errno = MPIDI_CH3I_Progress_test()) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }

fn_fail:
    return mpi_errno;
}

int MRAILI_Handle_one_sided_completions(vbuf * v)                            
{
    dreg_entry      	          *dreg_tmp;
    int                           size;
    int                           mpi_errno = MPI_SUCCESS;
    void                          *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list  *list_entry=NULL;
    MPID_Win                      *list_win_ptr;
    list_entry = (MPIDI_CH3I_RDMA_put_get_list *)v->list;
    list_win_ptr = list_entry->win_ptr;

    switch (list_entry->op_type) {
    case (SIGNAL_FOR_PUT):
        {
            dreg_tmp = list_entry->mem_entry;
            size = list_entry->data_size;

            if (size > (int)rdma_eagersize_1sc) {
                --(list_entry->completion);
                if (list_entry->completion == 0) {
                    dreg_unregister(dreg_tmp);
                }
            }
            --(list_win_ptr->put_get_list_size);
            break;
        }
    case (SIGNAL_FOR_GET):
        {
            size = list_entry->data_size;
            target_addr = list_entry->target_addr;
            origin_addr = list_entry->origin_addr;
            dreg_tmp = list_entry->mem_entry;

            if (origin_addr == NULL) {
                MPIU_Assert(size > rdma_eagersize_1sc);
                --(list_entry->completion);
                if (list_entry->completion == 0)
                    dreg_unregister(dreg_tmp);
            } else {
                MPIU_Assert(size <= rdma_eagersize_1sc);
                MPIU_Assert(target_addr != NULL);
                MPIU_Memcpy(target_addr, origin_addr, size);
            }
            --(list_win_ptr->put_get_list_size);
            break;
        }
    default:
            MPIU_ERR_SETSIMPLE(mpi_errno, MPI_ERR_OTHER, "**onesidedcomps");
            break;
    }
    
    if (list_win_ptr->put_get_list_size == 0) 
        list_win_ptr->put_get_list_tail = 0;

    if (list_win_ptr->put_get_list_size == 0){
        list_win_ptr->rma_issued = 0;
        list_win_ptr->pinnedpool_1sc_index = 0;
        list_win_ptr->poll_flag = 0;
     }
#ifndef _OSU_MVAPICH_
fn_fail:
#endif
    return mpi_errno;
}

static int iba_put(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, MPIDI_msg_sz_t size)
{
    char                *remote_address;
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_SUBRAILS],
                        l_key1[MAX_NUM_SUBRAILS], 
                        l_key[MAX_NUM_SUBRAILS];
    int                 i;
    dreg_entry          *tmp_dreg = NULL;
    char                *origin_addr;

    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    /*part 1 prepare origin side buffer target buffer and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank]
        + win_ptr->disp_units[rma_op->target_rank]
        * rma_op->target_disp;

 
    if (size <= rdma_eagersize_1sc) {
        char *tmp = rma_op->origin_addr;

        Get_Pinned_Buf(win_ptr, &origin_addr, size);
        MPIU_Memcpy(origin_addr, tmp, size);

        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);

        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }        

        origin_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }
    
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

    for (i=0; i<rdma_num_rails; ++i) {
        hca_index = i/rdma_num_rails_per_hca;
        r_key[i] = win_ptr->win_rkeys[rma_op->target_rank*rdma_num_hcas + hca_index];
        l_key1[i] = l_key[hca_index];
    }

    if(size < rdma_large_msg_rail_sharing_threshold) { 
        Post_Put_Put_Get_List(win_ptr, size, tmp_dreg, tmp_vc, 
                (void *)&origin_addr, (void *)&remote_address, 
                size, l_key1, r_key, SINGLE);
    } else {
        Post_Put_Put_Get_List(win_ptr, size, tmp_dreg, tmp_vc,
                (void *)&origin_addr, (void *)&remote_address,
                size, l_key1, r_key, STRIPE);
    }

    return mpi_errno;
}


int iba_get(MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, MPIDI_msg_sz_t size)
{
    int                 mpi_errno = MPI_SUCCESS;
    int                 hca_index;
    uint32_t            r_key[MAX_NUM_SUBRAILS], 
                        l_key1[MAX_NUM_SUBRAILS], 
                        l_key[MAX_NUM_SUBRAILS];
    dreg_entry          *tmp_dreg = NULL;
    char                *target_addr, *user_addr;
    char                *remote_addr;
    int                 i;
    MPIDI_VC_t          *tmp_vc;
    MPID_Comm           *comm_ptr;

    MPIU_Assert(rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys  */
    remote_addr = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc) {
        Get_Pinned_Buf(win_ptr, &target_addr, size);
        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = win_ptr->pinnedpool_1sc_dentry->memhandle[i]->lkey;
        }
        user_addr = rma_op->origin_addr;
    } else {
        tmp_dreg = dreg_register(rma_op->origin_addr, size);
        for(i = 0; i < rdma_num_hcas; ++i) {
            l_key[i] = tmp_dreg->memhandle[i]->lkey;
        }
        target_addr = user_addr = rma_op->origin_addr;
        win_ptr->wait_for_complete = 1;
    }

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc(comm_ptr, rma_op->target_rank, &tmp_vc);

   for (i=0; i<rdma_num_rails; ++i)
   {
       hca_index = i/rdma_num_rails_per_hca;
       r_key[i] = win_ptr->win_rkeys[rma_op->target_rank*rdma_num_hcas + hca_index];
       l_key1[i] = l_key[hca_index];
   }

   if(size < rdma_large_msg_rail_sharing_threshold) { 
       Post_Get_Put_Get_List(win_ptr, size, tmp_dreg, 
                             tmp_vc, (void *)&target_addr, 
                             (void *)&remote_addr, (void*)&user_addr, size, 
                             l_key1, r_key, SINGLE );
   } else {
       Post_Get_Put_Get_List(win_ptr, size, tmp_dreg,
                             tmp_vc, (void *)&target_addr,
                             (void *)&remote_addr, (void*)&user_addr, size,
                             l_key1, r_key, STRIPE );
   }

   return mpi_errno;
}
