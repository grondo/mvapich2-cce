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
#include <mpimem.h>
#include "rdma_impl.h"
#include "udapl_header.h"
#include "dreg.h"
#include <math.h>
#include "udapl_param.h"
#include "mpidrma.h"
#include "pmi.h"
#include "udapl_util.h"
#include "mpiutil.h"

#undef FUNCNAME
#define FUNCNAME 1SC_PUT_datav
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)

#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */ 

#ifndef GEN_EXIT_ERR
#define GEN_EXIT_ERR    -1
#endif
#ifndef ibv_error_abort
#define ibv_error_abort(code, message) do                       \
{                                                               \
	int my_rank;                                                \
	PMI_Get_rank(&my_rank);                                     \
	fprintf(stderr, "[%d] Abort: ", my_rank);                   \
	fprintf(stderr, message);                                   \
	fprintf(stderr, " at line %d in file %s\n", __LINE__,       \
	    __FILE__);                                              \
    fflush (stderr);                                            \
	exit(code);                                                 \
} while (0)
#endif

extern int number_of_op;

int Decrease_CC (MPID_Win *, int);
int POST_PUT_PUT_GET_LIST (MPID_Win *, int, dreg_entry *,
                           MPIDI_VC_t *, UDAPL_DESCRIPTOR *);
int POST_GET_PUT_GET_LIST (MPID_Win *, int, void *, void *,
                           dreg_entry *, MPIDI_VC_t *, UDAPL_DESCRIPTOR *);
int Consume_signals (MPID_Win *, aint_t);
int IBA_PUT (MPIDI_RMA_ops *, MPID_Win *, int);
int IBA_GET (MPIDI_RMA_ops *, MPID_Win *, int);
int IBA_ACCUMULATE (MPIDI_RMA_ops *, MPID_Win *, int);
int iba_lock (MPID_Win *, MPIDI_RMA_ops *, int);
int iba_unlock (MPID_Win *, MPIDI_RMA_ops *, int);

void MPIDI_CH3I_RDMA_lock (MPID_Win * win_ptr,
                           int target_rank, int lock_type, int blocking);
void Get_Pinned_Buf (MPID_Win * win_ptr, char **origin, int size);
int     iba_lock(MPID_Win *, MPIDI_RMA_ops *, int);
int     iba_unlock(MPID_Win *, MPIDI_RMA_ops *, int);

typedef struct {
  uintptr_t win_ptr;
  uint32_t win_rkey;
  uintptr_t completion_counter_ptr;
  uint32_t completion_counter_rkey;
  uint32_t post_flag_rkey;
  uint32_t fall_back;
} win_info;

#undef DEBUG_PRINT
#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */



/* For active synchronization, it is a blocking call*/





void
MPIDI_CH3I_RDMA_start (MPID_Win * win_ptr,
                       int start_grp_size, int *ranks_in_win_grp)
{
    MPIDI_VC_t* vc = NULL;
    MPID_Comm* comm_ptr = NULL;
    int flag = 0, src, i;

    if (SMP_INIT)
    {
        comm_ptr = win_ptr->comm_ptr;
    }

    DEBUG_PRINT ("before while loop %d\n", win_ptr->post_flag[1]);
    while (flag == 0 && start_grp_size != 0)
      {
          for (i = 0; i < start_grp_size; i++)
            {
                flag = 1;
                src = ranks_in_win_grp[i];      /*src is the rank in comm */

                if (SMP_INIT) {
                    MPIDI_Comm_get_vc (comm_ptr, src, &vc);
                    if (win_ptr->post_flag[src] == 0 && vc->smp.local_nodes == -1)
                      {
                        /*correspoding post has not been issued */
                        flag = 0;
                        break;
                      }
                } else if (win_ptr->post_flag[src] == 0)
                  {
                      /*correspoding post has not been issued */
                      flag = 0;
                      break;
                  }
            }                   /* end of for loop  */
      }                         /* end of while loop */
    DEBUG_PRINT ("finish while loop\n");
}

/* For active synchronization, if all rma operation has completed, we issue a RDMA
write operation with fence to update the remote flag in target processes*/
int
MPIDI_CH3I_RDMA_complete (MPID_Win * win_ptr,
                              int start_grp_size, int *ranks_in_win_grp)
{
    int i, target, dst;
    int my_rank, comm_size;
    int* nops_to_proc = NULL;
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm* comm_ptr = NULL;
    MPIDI_RMA_ops* curr_ptr = NULL;
    MPIDI_VC_t* vc = NULL;

    comm_ptr = win_ptr->comm_ptr;
    my_rank = comm_ptr->rank;
    comm_size = comm_ptr->local_size;

    /* clean up all the post flag */
    for (i = 0; i < start_grp_size; i++)
      {
          dst = ranks_in_win_grp[i];
          win_ptr->post_flag[dst] = 0;
      }
    win_ptr->using_start = 0;

    nops_to_proc = (int *) MPIU_Calloc (comm_size, sizeof (int));
    /* --BEGIN ERROR HANDLING-- */
    if (!nops_to_proc)
      {
          mpi_errno =
              MPIR_Err_create_code (MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                    __LINE__, MPI_ERR_OTHER, "**nomem", 0);
          goto fn_exit;
      }
    /* --END ERROR HANDLING-- */

    /* set rma_target_proc[i] to 1 if rank i is a target of RMA
       ops from this process */
    for (i = 0; i < comm_size; i++)
        nops_to_proc[i] = 0;
    curr_ptr = win_ptr->rma_ops_list_head;
    while (curr_ptr != NULL)
      {
          nops_to_proc[curr_ptr->target_rank]++;
          curr_ptr = curr_ptr->next;
      }

    for (i = 0; i < start_grp_size; i++)
      {
          target = ranks_in_win_grp[i]; /* target is the rank is comm */

          if (target == my_rank) {
             continue;
          }

          if (SMP_INIT) {
              MPIDI_Comm_get_vc (comm_ptr, target, &vc);
              if (nops_to_proc[target] == 0 && vc->smp.local_nodes == -1) {
                  Decrease_CC (win_ptr, target);
              }
          } else if (nops_to_proc[target] == 0)
            {
                Decrease_CC (win_ptr, target);
            }
      }
  fn_exit:
    return mpi_errno;
}

/* Waiting for all the completion signals and unregister buffers*/
int
MPIDI_CH3I_RDMA_finish_rma (MPID_Win * win_ptr)
{
    if (win_ptr->put_get_list_size != 0)
      {
          return Consume_signals (win_ptr, 0);
      }
    else
        return 0;
}


/* Go through RMA op list once, and start as many RMA ops as possible */
void
MPIDI_CH3I_RDMA_try_rma (MPID_Win * win_ptr, int passive)
{
    MPIDI_RMA_ops *curr_ptr = NULL, *prev_ptr = NULL, *tmp_ptr;
    MPIDI_RMA_ops **list_head = NULL, **list_tail = NULL;
    int size, origin_type_size, target_type_size;
#ifdef _SCHEDULE
    int curr_put = 1;
    int fall_back = 0;
    int force_to_progress = 0, issued = 0;
    MPIDI_RMA_ops *skipped_op = NULL;
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
          if (curr_ptr == NULL && skipped_op != NULL)
            {
                curr_ptr = skipped_op;
                skipped_op = NULL;
                fall_back++;
                if (issued == 0)
                  {
                      force_to_progress = 1;
                  }
                else
                  {
                      force_to_progress = 0;
                      issued = 0;
                  }
            }
#else
    while (curr_ptr != NULL)
      {
#endif

      if (SMP_INIT) {
          MPIDI_Comm_get_vc (comm_ptr, curr_ptr->target_rank, &vc);

          if (vc->smp.local_nodes != -1)
            {
                prev_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                continue;
            }
      }

          if (passive == 0 && win_ptr->post_flag[curr_ptr->target_rank] == 1
              && win_ptr->using_lock == 0)
            {
                switch (curr_ptr->type)
                  {
                  case (MPIDI_RMA_PUT):
                      {
                          int origin_dt_derived, target_dt_derived;

                          if (HANDLE_GET_KIND (curr_ptr->origin_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              origin_dt_derived = 1;
                          else
                              origin_dt_derived = 0;
                          if (HANDLE_GET_KIND (curr_ptr->target_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              target_dt_derived = 1;
                          else
                              target_dt_derived = 0;

                          MPID_Datatype_get_size_macro (curr_ptr->
                                                        origin_datatype,
                                                        origin_type_size);
                          size = curr_ptr->origin_count * origin_type_size;

                          if (!origin_dt_derived && !target_dt_derived
                              && size > rdma_put_fallback_threshold)
                            {
                                if (IBA_PUT (curr_ptr, win_ptr, size)
                                    != MPI_SUCCESS)
                                  {
                                      prev_ptr = curr_ptr;
                                      curr_ptr = curr_ptr->next;
                                  }
                                else
                                  {
                                      win_ptr->rma_issued++;
                                      if (*list_head == curr_ptr)
                                        {
                                            if (*list_head == *list_tail)
                                              {
                                                  *list_head = *list_tail 
                                                        = NULL;
                                              }
                                            else
                                              {
                                                  *list_head = prev_ptr 
                                                        = curr_ptr->next;
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
                                      MPIU_Free (curr_ptr);
                                      curr_ptr = tmp_ptr;
                                  }
                            }
                          else
                            {
                                prev_ptr = curr_ptr;
                                curr_ptr = curr_ptr->next;
                            }
                          break;
                      }
                  case (MPIDI_RMA_ACCUMULATE):
                      {
                          prev_ptr = curr_ptr;
                          curr_ptr = curr_ptr->next;
                          break;
                      }
                  case MPIDI_RMA_ACC_CONTIG:
                    {
                        prev_ptr = curr_ptr;
                        curr_ptr = curr_ptr->next;
                        break;
                    }
                  case (MPIDI_RMA_GET):
                      {
                          int origin_dt_derived, target_dt_derived;
                          if (HANDLE_GET_KIND (curr_ptr->origin_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              origin_dt_derived = 1;
                          else
                              origin_dt_derived = 0;
                          if (HANDLE_GET_KIND (curr_ptr->target_datatype) !=
                              HANDLE_KIND_BUILTIN)
                              target_dt_derived = 1;
                          else
                              target_dt_derived = 0;
                          MPID_Datatype_get_size_macro (curr_ptr->
                                                        target_datatype,
                                                        target_type_size);
                          size = curr_ptr->target_count * target_type_size;

                          if (!origin_dt_derived && !target_dt_derived
                              && size > rdma_get_fallback_threshold)
                            {
                                if (IBA_GET (curr_ptr, win_ptr, size)
                                    != MPI_SUCCESS)
                                  {
                                      prev_ptr = curr_ptr;
                                      curr_ptr = curr_ptr->next;
                                  }
                                else
                                  {
                                      win_ptr->rma_issued++;
                                      if (*list_head == curr_ptr)
                                        {
                                            if (*list_head == *list_tail)
                                              {
                                                  *list_head = *list_tail 
                                                        = NULL;
                                              }
                                            else
                                              {
                                                  *list_head = prev_ptr 
                                                        = curr_ptr->next;
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
                                      MPIU_Free (curr_ptr);
                                      curr_ptr = tmp_ptr;
                                  }
                            }
                          else
                            {
                                prev_ptr = curr_ptr;
                                curr_ptr = curr_ptr->next;
                            }
                          break;
                      }
                  default:
                      printf ("Unknown ONE SIDED OP\n");
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
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

/* For active synchronization */
#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_post
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FCNAME)
int MPIDI_CH3I_RDMA_post (MPID_Win * win_ptr, int target_rank)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_RDMA_POST);

    char *origin_addr;
    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    /*part 1 prepare origin side buffer */
    char* remote_address = (char *) win_ptr->remote_post_flags[target_rank];
    uint32_t l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
    uint32_t r_key = win_ptr->post_flag_rkeys[target_rank];
    int size = sizeof (int);
    Get_Pinned_Buf (win_ptr, &origin_addr, size);
    *((int *) origin_addr) = 1;

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc (comm_ptr, target_rank, &tmp_vc);

    /*part 2 Do RDMA WRITE */
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) remote_address;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    POST_PUT_PUT_GET_LIST (win_ptr, -1, NULL, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);
    Consume_signals (win_ptr, 0);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_RDMA_POST);
    return MPI_SUCCESS;
}

void
MPIDI_CH3I_RDMA_win_create (void *base,
                            MPI_Aint size,
                            int comm_size,
                            int my_rank,
                            MPID_Win ** win_ptr, MPID_Comm * comm_ptr)
{
    int ret, i, fall_back;
    int errflag = FALSE;
    win_info *win_info_exchange;
    uintptr_t *post_flag_ptr_send, *post_flag_ptr_recv;

    if (strcmp(dapl_provider, "nes0") == 0) {
          (*win_ptr)->fall_back = 1;
          return;
    }

    if (!MPIDI_CH3I_RDMA_Process.has_one_sided) {
        (*win_ptr)->fall_back = 1;
        return;
    }

    /*Exchagne the information about rkeys and addresses */
    win_info_exchange = MPIU_Malloc (comm_size * sizeof (win_info));
    if (!win_info_exchange)
    {
          printf ("Error malloc when creating win_info_exchange structure\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    (*win_ptr)->fall_back = 0;
    /*Register the exposed buffer in this window */
    if (base != NULL && size > 0)
    {
          (*win_ptr)->win_dreg_entry = dreg_register (base, size);
          if (NULL == (*win_ptr)->win_dreg_entry) {
              (*win_ptr)->fall_back = 1;
          } else {
              win_info_exchange[my_rank].win_rkey =
                 (uint32_t) ((*win_ptr)->win_dreg_entry->memhandle.rkey);
          }

    } else {

          (*win_ptr)->win_dreg_entry = NULL;
          win_info_exchange[my_rank].win_rkey = -1;

    }

    ret =
       MPIR_Allreduce_impl (&((*win_ptr)->fall_back), &fall_back, 1, MPI_INT,
                            MPI_SUM, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS)
    {
        printf ("Error allreduce while creating windows\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    if (fall_back != 0)
    {
        (*win_ptr)->fall_back = 1;
        goto win_unregister;
    }

    /*Register buffer for completion counter */
    (*win_ptr)->completion_counter =
        MPIU_Malloc (sizeof (long long) * comm_size);
    MPIU_Memset ((void *)((*win_ptr)->completion_counter), 0,
            sizeof (long long) * comm_size);
    (*win_ptr)->completion_counter_dreg_entry =
        dreg_register ((void *) (*win_ptr)->completion_counter,
                       sizeof (long long) * comm_size);
    if (NULL == (*win_ptr)->completion_counter_dreg_entry)
    {
          (*win_ptr)->fall_back = 1;
          goto win_unregister;
    }

    win_info_exchange[my_rank].completion_counter_ptr = 
        (uintptr_t) ((*win_ptr)->completion_counter); 
    win_info_exchange[my_rank].completion_counter_rkey =
        (uint32_t) ((*win_ptr)->completion_counter_dreg_entry->
        memhandle.rkey);

    /*Register buffer for post flags : from target to origin */
    (*win_ptr)->post_flag = (int *) MPIU_Malloc (comm_size * sizeof (int));
    MPIU_Memset ((void *)((*win_ptr)->post_flag), 0,
            sizeof (int) * comm_size);
    (*win_ptr)->post_flag_dreg_entry =
        dreg_register ((void *) (*win_ptr)->post_flag,
                       sizeof (int) * comm_size);
    if (NULL == (*win_ptr)->post_flag_dreg_entry)
    {
        (*win_ptr)->fall_back = 1;
        goto cc_unregister;
    }

    win_info_exchange[my_rank].post_flag_rkey  =
        (uint32_t) (*win_ptr)->post_flag_dreg_entry->memhandle.rkey;

    /* Preregister buffer*/
    (*win_ptr)->pinnedpool_1sc_buf = MPIU_Malloc (rdma_pin_pool_size);
    (*win_ptr)->pinnedpool_1sc_dentry =
        dreg_register ((*win_ptr)->pinnedpool_1sc_buf, rdma_pin_pool_size);
    if (NULL == (*win_ptr)->pinnedpool_1sc_dentry)
    {
          (*win_ptr)->fall_back = 1;
          goto post_unregister;
    }
    (*win_ptr)->pinnedpool_1sc_index = 0;

    win_info_exchange[my_rank].fall_back = (*win_ptr)->fall_back;

    ret =
        MPIR_Allgather_impl (MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, (void *) win_info_exchange, 
                 sizeof(win_info), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS)
      {
          printf ("Error while gathering window information in win_create\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
      }

    /* check if any peers fail */
    for (i = 0; i < comm_size; ++i)
    {
            if (win_info_exchange[i].fall_back != 0)
            {
               (*win_ptr)->fall_back = 1;
               dreg_unregister((*win_ptr)->pinnedpool_1sc_dentry);
               MPIU_Free((*win_ptr)->pinnedpool_1sc_buf);
               dreg_unregister((*win_ptr)->post_flag_dreg_entry);
               MPIU_Free((*win_ptr)->post_flag);
               dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
               MPIU_Free((*win_ptr)->completion_counter);
               dreg_unregister((*win_ptr)->win_dreg_entry); 
               MPIU_Free(win_info_exchange);
               goto fn_exit;
            }
    }    

    (*win_ptr)->win_rkeys =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->win_rkeys)
    {
          printf ("Error malloc win->win_rkeys when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    (*win_ptr)->completion_counter_rkeys =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->completion_counter_rkeys)
    {
          printf ("Error malloc win->completion_counter_rkeys when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    (*win_ptr)->all_completion_counter =
        (long long **) MPIU_Malloc (comm_size * sizeof (long long *));
    if (!(*win_ptr)->all_completion_counter)
    {
          printf
              ("error malloc win->all_completion_counter when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    (*win_ptr)->post_flag_rkeys =
        (uint32_t *) MPIU_Malloc (comm_size * sizeof (uint32_t));
    if (!(*win_ptr)->post_flag_rkeys)
    {
          printf ("error malloc win->post_flag_rkeys when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    (*win_ptr)->remote_post_flags =
        (int **) MPIU_Malloc (comm_size * sizeof (int *));
    if (!(*win_ptr)->remote_post_flags)
    {
          printf
              ("error malloc win->remote_post_flags when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    for (i = 0; i < comm_size; i++)
    {
          (*win_ptr)->win_rkeys[i] = win_info_exchange[i].win_rkey;
          (*win_ptr)->completion_counter_rkeys[i] =
              win_info_exchange[i].completion_counter_rkey;
          (*win_ptr)->all_completion_counter[i] =
              (long long *) ((size_t)(win_info_exchange[i].completion_counter_ptr) 
                   + sizeof(long long) * my_rank);
          (*win_ptr)->post_flag_rkeys[i] = win_info_exchange[i].post_flag_rkey;
    }

    MPIU_Free (win_info_exchange);

    post_flag_ptr_send = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!post_flag_ptr_send)
    {
          printf ("Error malloc post_flag_ptr_send when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    post_flag_ptr_recv = (uintptr_t *) MPIU_Malloc (comm_size * sizeof (uintptr_t));
    if (!post_flag_ptr_recv)
    {
          printf ("Error malloc post_flag_ptr_recv when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    /* use all to all to exchange address for post flag */
    for (i = 0; i < comm_size; i++)
    {
          if (i != my_rank)
              (*win_ptr)->post_flag[i] = 0;
          else
              (*win_ptr)->post_flag[i] = 1;
          post_flag_ptr_send[i] = (uintptr_t) & ((*win_ptr)->post_flag[i]);
    }

    ret =
        MPIR_Alltoall_impl (post_flag_ptr_send, sizeof(uintptr_t), MPI_BYTE, post_flag_ptr_recv, 
                            sizeof(uintptr_t), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS)
    {
          printf ("Error alltoall exchange post flag rkey when creating windows\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

    for (i = 0; i < comm_size; i++)
    {
          (*win_ptr)->remote_post_flags[i] = (int *) post_flag_ptr_recv[i];
    }

    MPIU_Free(post_flag_ptr_send);
    MPIU_Free(post_flag_ptr_recv);

    /* Initialize put/get queue and other counters*/
    (*win_ptr)->using_lock = 0;
    (*win_ptr)->using_start = 0;
    (*win_ptr)->my_id = my_rank;
    (*win_ptr)->comm_size = comm_size;

    (*win_ptr)->put_get_list_size = 0;
    (*win_ptr)->put_get_list_tail = 0;
    (*win_ptr)->put_get_list =
        MPIU_Malloc (rdma_default_put_get_list_size *
                     sizeof (MPIDI_CH3I_RDMA_put_get_list));
    (*win_ptr)->wait_for_complete = 0;
    (*win_ptr)->rma_issued = 0;

    if (!(*win_ptr)->put_get_list)
    {
          printf ("Fail to malloc space for window put get list\n");
          ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }

  fn_exit:
    if (1 == (*win_ptr)->fall_back)
    {
          (*win_ptr)->using_lock = 0;
          (*win_ptr)->using_start = 0;
          (*win_ptr)->my_id = my_rank;

          (*win_ptr)->comm_size = comm_size;
          /* Initialize put/get queue */
          (*win_ptr)->put_get_list_size = 0;
          (*win_ptr)->put_get_list_tail = 0;
          (*win_ptr)->wait_for_complete = 0;
          (*win_ptr)->rma_issued = 0;
    }
    return;
  post_unregister:
    if((*win_ptr)->post_flag_dreg_entry)
    {
          dreg_unregister((*win_ptr)->post_flag_dreg_entry);
          MPIU_Free((*win_ptr)->post_flag);
    }
  cc_unregister:
    if((*win_ptr)->completion_counter_dreg_entry)
    {
          dreg_unregister((*win_ptr)->completion_counter_dreg_entry);
          MPIU_Free((*win_ptr)->completion_counter);
    }
  win_unregister:
    if((*win_ptr)->win_dreg_entry) 
    {
          dreg_unregister((*win_ptr)->win_dreg_entry);
    }

    win_info_exchange[my_rank].fall_back = (uint32_t) (*win_ptr)->fall_back;

    ret =
        MPIR_Allgather_impl (MPI_IN_PLACE, 0, MPI_DATATYPE_NULL, win_info_exchange, 
                        sizeof(win_info), MPI_BYTE, comm_ptr, &errflag);
    if (ret != MPI_SUCCESS)
    {
        printf ("Error when gathering win_info in win_create\n");
        ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
    }
 
    MPIU_Free(win_info_exchange);
    goto fn_exit;
}

void
MPIDI_CH3I_RDMA_win_free (MPID_Win ** win_ptr)
{
    if ((*win_ptr)->win_dreg_entry != NULL)
      {
          dreg_unregister ((*win_ptr)->win_dreg_entry);
      }
    MPIU_Free ((*win_ptr)->win_rkeys);
    MPIU_Free ((*win_ptr)->completion_counter_rkeys);
    MPIU_Free ((*win_ptr)->post_flag_rkeys);
    MPIU_Free ((*win_ptr)->remote_post_flags);
    MPIU_Free ((*win_ptr)->put_get_list);
    dreg_unregister ((*win_ptr)->pinnedpool_1sc_dentry);
    MPIU_Free ((*win_ptr)->pinnedpool_1sc_buf);
    if ((*win_ptr)->completion_counter_dreg_entry != NULL)
      {
          dreg_unregister ((*win_ptr)->completion_counter_dreg_entry );
      }
    if ((*win_ptr)->post_flag_dreg_entry  != NULL)
      {
          dreg_unregister ((*win_ptr)->post_flag_dreg_entry);
      }
    MPIU_Free ((*win_ptr)->completion_counter);
    MPIU_Free ((*win_ptr)->all_completion_counter);
}

int
Decrease_CC (MPID_Win * win_ptr, int target_rank)
{
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key2, l_key2;
    int ret;
    long long *cc;
    DAT_EP_HANDLE qp_hndl;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc (comm_ptr, target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;
    Get_Pinned_Buf (win_ptr, (char **) &cc, sizeof (long long));
    *((long long *) cc) = 1;
    l_key2 = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
    r_key2 = win_ptr->completion_counter_rkeys[target_rank];

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 1;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key2;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) cc;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = sizeof (long long);
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key2;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) (win_ptr->
                                     all_completion_counter[target_rank]);
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = sizeof (long long);
    POST_PUT_PUT_GET_LIST (win_ptr, -1, NULL, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);

    return mpi_errno;
}

int
POST_PUT_PUT_GET_LIST (MPID_Win * winptr,
                       int size,
                       dreg_entry * dreg_tmp,
                       MPIDI_VC_t * vc_ptr, UDAPL_DESCRIPTOR * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;
    winptr->put_get_list[index].op_type = SIGNAL_FOR_PUT;
    winptr->put_get_list[index].mem_entry = dreg_tmp;
    winptr->put_get_list[index].data_size = size;
    winptr->put_get_list[index].win_ptr = winptr;
    winptr->put_get_list[index].vc_ptr = vc_ptr;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
        rdma_default_put_get_list_size;
    winptr->put_get_list_size++;
    vc_ptr->mrail.postsend_times_1sc++;

    desc_p->cookie.as_ptr = (&(winptr->put_get_list[index]));

    {
        unsigned int completionflags = 0;

        if (desc_p->fence)
          {
              completionflags = DAT_COMPLETION_BARRIER_FENCE_FLAG;
          }

        ret = dat_ep_post_rdma_write (vc_ptr->mrail.qp_hndl_1sc, 1,
                                      &(desc_p->local_iov),
                                      desc_p->cookie,
                                      &(desc_p->remote_iov), completionflags);
    }

    CHECK_RETURN (ret, "Fail in posting RDMA_Write");

    if (winptr->put_get_list_size == rdma_default_put_get_list_size
        || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_send_wqe - 1)
      {
          Consume_signals (winptr, 0);
      }
    return index;
}

/*functions storing the get operations */
int
POST_GET_PUT_GET_LIST (MPID_Win * winptr,
                       int size,
                       void *target_addr,
                       void *origin_addr,
                       dreg_entry * dreg_tmp,
                       MPIDI_VC_t * vc_ptr, UDAPL_DESCRIPTOR * desc_p)
{
    int index = winptr->put_get_list_tail;
    int ret;

    winptr->put_get_list[index].op_type = SIGNAL_FOR_GET;
    winptr->put_get_list[index].origin_addr = origin_addr;
    winptr->put_get_list[index].target_addr = target_addr;
    winptr->put_get_list[index].data_size = size;
    winptr->put_get_list[index].mem_entry = dreg_tmp;
    winptr->put_get_list[index].win_ptr = winptr;
    winptr->put_get_list[index].vc_ptr = vc_ptr;
    winptr->put_get_list_size++;
    vc_ptr->mrail.postsend_times_1sc++;
    winptr->put_get_list_tail = (winptr->put_get_list_tail + 1) %
        rdma_default_put_get_list_size;

    desc_p->cookie.as_ptr = &winptr->put_get_list[index];
    winptr->wait_for_complete = 1;

    {
        unsigned int completionflags = 0;

        if (desc_p->fence)
            completionflags = DAT_COMPLETION_BARRIER_FENCE_FLAG;

        ret = dat_ep_post_rdma_read (vc_ptr->mrail.qp_hndl_1sc, 1,
                                     &(desc_p->local_iov),
                                     desc_p->cookie,
                                     &(desc_p->remote_iov), completionflags);
    }

    CHECK_RETURN (ret, "Fail in posting RDMA_READ");

    if (strcmp (dapl_provider, "ib0") == 0)
      {
          if (winptr->put_get_list_size == rdma_default_put_get_list_size
              || vc_ptr->mrail.postsend_times_1sc ==
              DAPL_DEFAULT_MAX_RDMA_IN - 1)
              /* the get queue is full */
              Consume_signals (winptr, 0);
      }
    else
      {
          if (winptr->put_get_list_size == rdma_default_put_get_list_size
              || vc_ptr->mrail.postsend_times_1sc == rdma_default_max_send_wqe - 1)
              /* the get queue is full */
              Consume_signals (winptr, 0);
      }
    return index;
}

/*if signal == -1, cousume all the signals, 
  otherwise return when signal is found*/
int
Consume_signals (MPID_Win * winptr, aint_t expected)
{
    DAT_EVENT event;
    DAT_RETURN ret;
    dreg_entry *dreg_tmp;
    int i = 0, size;
    void *target_addr, *origin_addr;
    MPIDI_CH3I_RDMA_put_get_list *list_entry = NULL;
    MPID_Win *list_win_ptr;
    MPIDI_VC_t *list_vc_ptr;

    while (1)
      {
          ret = dat_evd_dequeue (MPIDI_CH3I_RDMA_Process.cq_hndl_1sc, &event);
          if (ret == DAT_SUCCESS)
            {
                i++;
                if (event.event_number != DAT_DTO_COMPLETION_EVENT
                    || event.event_data.dto_completion_event_data.status !=
                    DAT_DTO_SUCCESS)
                  {
                      printf ("in Consume_signals %d  \n",
                              winptr->pinnedpool_1sc_index);
                      printf ("\n DAT_EVD_ERROR inside Consume_signals\n");
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");

                  }
                MPIU_Assert (event.event_data.dto_completion_event_data.status ==
                        DAT_DTO_SUCCESS);
                MPIU_Assert (event.event_data.dto_completion_event_data.
                        user_cookie.as_64 != -1);

                if ((aint_t) event.event_data.dto_completion_event_data.
                    user_cookie.as_ptr == expected)
                  {
                      goto fn_exit;
                  }

                /* if id is not equal to expected id, the only possible cq
                 * is for the posted signaled PUT/GET.
                 * Otherwise we have error.
                 */
                list_entry =
                    (MPIDI_CH3I_RDMA_put_get_list *) (aint_t) event.
                    event_data.dto_completion_event_data.user_cookie.as_ptr;

                list_win_ptr = list_entry->win_ptr;
                list_vc_ptr = list_entry->vc_ptr;
                if (list_entry->op_type == SIGNAL_FOR_PUT)
                  {
                      dreg_tmp = list_entry->mem_entry;
                      size = list_entry->data_size;
                      if (size > (int) rdma_eagersize_1sc)
                        {
                            dreg_unregister (dreg_tmp);
                        }
                      list_win_ptr->put_get_list_size--;
                      list_vc_ptr->mrail.postsend_times_1sc--;
                  }
                else if (list_entry->op_type == SIGNAL_FOR_GET)
                  {
                      size = list_entry->data_size;
                      target_addr = list_entry->target_addr;
                      origin_addr = list_entry->origin_addr;
                      dreg_tmp = list_entry->mem_entry;
                      if (origin_addr == NULL)
                        {
                            MPIU_Assert(size > rdma_eagersize_1sc);
                            dreg_unregister (dreg_tmp);
                        }
                      else
                        {
                            MPIU_Assert(size <= rdma_eagersize_1sc);
                            MPIU_Assert(target_addr != NULL);
                            MPIU_Memcpy (target_addr, origin_addr, size);
                        }
                      list_win_ptr->put_get_list_size--;
                      list_vc_ptr->mrail.postsend_times_1sc--;
                  }
                else
                  {
                      fprintf (stderr, "Error! rank %d, Undefined op_type, op type %d, \
                list id %u, expecting id %u\n",
                               winptr->my_id, list_entry->op_type, list_entry, expected);
                      ibv_error_abort (GEN_EXIT_ERR, "rdma_udapl_1sc");
                  }
            }


          if (winptr->put_get_list_size == 0)
              winptr->put_get_list_tail = 0;
          if (winptr->put_get_list_size == 0 && (expected == 0))
            {
                winptr->rma_issued = 0;
                winptr->pinnedpool_1sc_index = 0;
                goto fn_exit;
            }
      }                         /* end of while */
  fn_exit:
    MPIU_Assert(event.event_data.dto_completion_event_data.user_cookie.as_64 ==
            expected || expected == 0);

    return 0;
}

int
IBA_PUT (MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char *remote_address;
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key, l_key;
    int ret;
    DAT_EP_HANDLE qp_hndl;
    int origin_type_size;
    dreg_entry *tmp_dreg = NULL;
    char *origin_addr;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;

    int index;

    /*part 1 prepare origin side buffer target buffer and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank]
        + win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;
    if (size <= rdma_eagersize_1sc)
      {
          char *tmp = rma_op->origin_addr;

          Get_Pinned_Buf (win_ptr, &origin_addr, size);
          MPIU_Memcpy (origin_addr, tmp, size);
          l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
      }
    else
      {
          tmp_dreg = dreg_register (rma_op->origin_addr, size);
          if (!tmp_dreg)
            {
                return -1;
            }
          l_key = tmp_dreg->memhandle.lkey;
          origin_addr = rma_op->origin_addr;
          win_ptr->wait_for_complete = 1;
      }
    r_key = win_ptr->win_rkeys[rma_op->target_rank];
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc (comm_ptr, rma_op->target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;

    /*part 2 Do RDMA WRITE */
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address
#endif /* if DAT_VERSION_MAJOR < 2 */
            = (DAT_VADDR) (unsigned long) remote_address;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    POST_PUT_PUT_GET_LIST (win_ptr, size, tmp_dreg, tmp_vc,
                           &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                   put_get_list_tail]);

    return mpi_errno;
}

int
IBA_GET (MPIDI_RMA_ops * rma_op, MPID_Win * win_ptr, int size)
{
    char *remote_address;
    int mpi_errno = MPI_SUCCESS;
    uint32_t r_key, l_key;
    /* int ret = 0; */
    DAT_EP_HANDLE qp_hndl;
    int index;
    dreg_entry *tmp_dreg;
    char *origin_addr;

    MPIDI_VC_t *tmp_vc;
    MPID_Comm *comm_ptr;


    MPIU_Assert (rma_op->type == MPIDI_RMA_GET);
    /*part 1 prepare origin side buffer target address and keys */
    remote_address = (char *) win_ptr->base_addrs[rma_op->target_rank] +
        win_ptr->disp_units[rma_op->target_rank] * rma_op->target_disp;

    if (size <= rdma_eagersize_1sc)
      {
          Get_Pinned_Buf (win_ptr, &origin_addr, size);
          l_key = win_ptr->pinnedpool_1sc_dentry->memhandle.lkey;
      }
    else
      {
          tmp_dreg = dreg_register (rma_op->origin_addr, size);
          if (tmp_dreg == NULL)
            {
                return -1;
            }
          l_key = tmp_dreg->memhandle.lkey;
          origin_addr = rma_op->origin_addr;
      }

    r_key = win_ptr->win_rkeys[rma_op->target_rank];
    comm_ptr = win_ptr->comm_ptr;
    MPIDI_Comm_get_vc (comm_ptr, rma_op->target_rank, &tmp_vc);
    qp_hndl = tmp_vc->mrail.qp_hndl_1sc;

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].completion_flag =
        DAT_COMPLETION_DEFAULT_FLAG;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].fence = 0;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.lmr_context =
        l_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        virtual_address = (DAT_VADDR) (unsigned long) origin_addr;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].local_iov.
        segment_length = size;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        rmr_context = r_key;
    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
#if DAT_VERSION_MAJOR < 2
        target_address = (DAT_VADDR) (unsigned long) remote_address;
#else /* if DAT_VERSION_MAJOR < 2 */
        virtual_address = (DAT_VADDR) (unsigned long) remote_address;
#endif /* DAT_VERSION_MAJOR < 2 */
    /*        = (DAT_VADDR) (unsigned long) remote_address; */

    tmp_vc->mrail.ddesc1sc[win_ptr->put_get_list_tail].remote_iov.
        segment_length = size;
    if (size <= rdma_eagersize_1sc)
        index = POST_GET_PUT_GET_LIST (win_ptr, size, rma_op->origin_addr,
                                       origin_addr, NULL, tmp_vc,
                                       &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                               put_get_list_tail]);
    else
        index = POST_GET_PUT_GET_LIST (win_ptr, size, NULL, NULL,
                                       tmp_dreg, tmp_vc,
                                       &tmp_vc->mrail.ddesc1sc[win_ptr->
                                                               put_get_list_tail]);

    return mpi_errno;
}

void
Get_Pinned_Buf (MPID_Win * win_ptr, char **origin, int size)
{
    if (win_ptr->pinnedpool_1sc_index + size >= rdma_pin_pool_size)
      {
          Consume_signals (win_ptr, 0);
          *origin = win_ptr->pinnedpool_1sc_buf;
          win_ptr->pinnedpool_1sc_index = size;
      }
    else
      {
          *origin =
              win_ptr->pinnedpool_1sc_buf + win_ptr->pinnedpool_1sc_index;
          win_ptr->pinnedpool_1sc_index += size;
      }
}

