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

#include "mpiimpl.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include <mpimem.h>
#include "mpidimpl.h"
#include "mpicomm.h"
#include "coll_shmem.h"
#include <pthread.h>
#ifndef GEN_EXIT_ERR
#define GEN_EXIT_ERR    -1
#endif
#ifndef ibv_error_abort
#define ibv_error_abort(code, message) do {                     \
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

#define MAX_SHMEM_COMM  4
#define MAX_NUM_COMM    10000
#define MAX_ALLOWED_COMM   250
unsigned int comm_registry [MAX_NUM_COMM];
unsigned int comm_registered = 0;
unsigned int comm_count = 0;

int shmem_comm_count = 0;
static pthread_mutex_t comm_lock  = PTHREAD_MUTEX_INITIALIZER;
extern int g_shmem_coll_blocks;

#define MAX_NUM_THREADS 1024
pthread_t thread_reg[MAX_NUM_THREADS];

void clear_2level_comm (MPID_Comm* comm_ptr)
{
    comm_ptr->ch.allgather_comm_ok = 0;
    comm_ptr->ch.shmem_coll_ok = 0;
    comm_ptr->ch.leader_map  = NULL;
    comm_ptr->ch.leader_rank = NULL;
}

int free_2level_comm (MPID_Comm* comm_ptr)
{
    MPID_Comm *shmem_comm_ptr=NULL; 
    MPID_Comm *leader_comm_ptr=NULL;
    MPID_Comm *allgather_comm_ptr=NULL;
    int local_rank=0;
    int mpi_errno=MPI_SUCCESS;

    if (comm_ptr->ch.leader_map != NULL)  { 
        MPIU_Free(comm_ptr->ch.leader_map);  
    }
    if (comm_ptr->ch.leader_rank != NULL) { 
        MPIU_Free(comm_ptr->ch.leader_rank); 
    }

    MPID_Comm_get_ptr((comm_ptr->ch.shmem_comm), shmem_comm_ptr );
    MPID_Comm_get_ptr((comm_ptr->ch.leader_comm), leader_comm_ptr );
    if(comm_ptr->ch.allgather_comm_ok == 1)  { 
       MPID_Comm_get_ptr((comm_ptr->ch.allgather_comm), allgather_comm_ptr );
       MPIU_Free(comm_ptr->ch.allgather_new_ranks); 
    } 

    local_rank = shmem_comm_ptr->rank; 

    if(local_rank == 0) { 
        if(comm_ptr->ch.node_sizes != NULL) { 
            MPIU_Free(comm_ptr->ch.node_sizes); 
        } 
    } 
    if (local_rank == 0 && leader_comm_ptr != NULL) { 
        mpi_errno = MPIR_Comm_release(leader_comm_ptr, 0);
        if (mpi_errno != MPI_SUCCESS) { 
            goto fn_fail;
        } 
    }
    if (shmem_comm_ptr != NULL)  { 
        mpi_errno = MPIR_Comm_release(shmem_comm_ptr, 0);
        if (mpi_errno != MPI_SUCCESS) { 
            goto fn_fail;
        } 
     }
    if (allgather_comm_ptr != NULL)  { 
        mpi_errno = MPIR_Comm_release(allgather_comm_ptr, 0);
        if (mpi_errno != MPI_SUCCESS) { 
            goto fn_fail;
        } 
     }

    clear_2level_comm(comm_ptr);
    fn_exit:
       return mpi_errno;
    fn_fail:
       goto fn_exit;
}

int create_2level_comm (MPI_Comm comm, int size, int my_rank)
{
    static const char FCNAME[] = "create_2level_comm";
    int mpi_errno = MPI_SUCCESS;
    MPID_Comm* comm_ptr;
    MPID_Comm* comm_world_ptr;
    MPI_Group subgroup1, comm_group;
    MPID_Group *group_ptr=NULL;
    int leader_comm_size, my_local_size, my_local_id, input_flag =0, output_flag=0;
    int errflag = FALSE;
    int leader_group_size=0;
  
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    MPID_Comm_get_ptr( comm, comm_ptr );
    MPID_Comm_get_ptr( MPI_COMM_WORLD, comm_world_ptr );

    int* shmem_group = MPIU_Malloc(sizeof(int) * size);
    if (NULL == shmem_group){
        printf("Couldn't malloc shmem_group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    /* Creating local shmem group */
    int i = 0;
    int local_rank = 0;
    int grp_index = 0;
    comm_ptr->ch.leader_comm=MPI_COMM_NULL;
    comm_ptr->ch.shmem_comm=MPI_COMM_NULL;

    MPIDI_VC_t* vc = NULL;
    for (; i < size ; ++i){
       MPIDI_Comm_get_vc(comm_ptr, i, &vc);
       if (my_rank == i || vc->smp.local_rank >= 0){
           shmem_group[grp_index] = i;
           if (my_rank == i){
               local_rank = grp_index;
           }
           ++grp_index;
       }  
    } 

    /* Creating leader group */
    int leader = 0;
    leader = shmem_group[0];


    /* Gives the mapping to any process's leader in comm */
    comm_ptr->ch.leader_map = MPIU_Malloc(sizeof(int) * size);
    if (NULL == comm_ptr->ch.leader_map){
        printf("Couldn't malloc group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }
    
    mpi_errno = MPIR_Allgather_impl (&leader, 1, MPI_INT , comm_ptr->ch.leader_map, 1, MPI_INT, comm_ptr, &errflag);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    int* leader_group = MPIU_Malloc(sizeof(int) * size);
    if (NULL == leader_group){
        printf("Couldn't malloc leader_group\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    /* Gives the mapping from leader's rank in comm to 
     * leader's rank in leader_comm */
    comm_ptr->ch.leader_rank = MPIU_Malloc(sizeof(int) * size);
    if (NULL == comm_ptr->ch.leader_rank){
        printf("Couldn't malloc marker\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    for (i=0; i < size ; ++i){
         comm_ptr->ch.leader_rank[i] = -1;
    }
    int* group = comm_ptr->ch.leader_map;
    grp_index = 0;
    for (i=0; i < size ; ++i){
        if (comm_ptr->ch.leader_rank[(group[i])] == -1){
            comm_ptr->ch.leader_rank[(group[i])] = grp_index;
            leader_group[grp_index++] = group[i];
           
        }
    }
    leader_group_size = grp_index;
    comm_ptr->ch.leader_group_size = leader_group_size;

    mpi_errno = PMPI_Comm_group(comm, &comm_group);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }
    
    mpi_errno = PMPI_Group_incl(comm_group, leader_group_size, leader_group, &subgroup1);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    mpi_errno = PMPI_Comm_create(comm, subgroup1, &(comm_ptr->ch.leader_comm));
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    MPID_Comm *leader_ptr;
    MPID_Comm_get_ptr( comm_ptr->ch.leader_comm, leader_ptr );
       
    MPIU_Free(leader_group);
    MPID_Group_get_ptr( subgroup1, group_ptr );
    if(group_ptr != NULL) { 
       mpi_errno = PMPI_Group_free(&subgroup1);
       if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
       }
    } 

    mpi_errno = PMPI_Comm_split(comm, leader, local_rank, &(comm_ptr->ch.shmem_comm));
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    MPID_Comm *shmem_ptr;
    MPID_Comm_get_ptr(comm_ptr->ch.shmem_comm, shmem_ptr);


    mpi_errno = PMPI_Comm_rank(comm_ptr->ch.shmem_comm, &my_local_id);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(comm_ptr->ch.shmem_comm, &my_local_size);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }

    if(my_local_id == 0) { 
           int array_index=0;
           mpi_errno = PMPI_Comm_size(comm_ptr->ch.leader_comm, &leader_comm_size);
           if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
           }

           comm_ptr->ch.node_sizes = MPIU_Malloc(sizeof(int)*leader_comm_size);
           mpi_errno = PMPI_Allgather(&my_local_size, 1, MPI_INT,
				 comm_ptr->ch.node_sizes, 1, MPI_INT, comm_ptr->ch.leader_comm);
           if(mpi_errno) {
              MPIU_ERR_POP(mpi_errno);
           }
           comm_ptr->ch.is_uniform = 1; 
           for(array_index=0; array_index < leader_comm_size; array_index++) { 
                if(comm_ptr->ch.node_sizes[0] != comm_ptr->ch.node_sizes[array_index]) {
                     comm_ptr->ch.is_uniform = 0; 
                     break;
                }
           }
     } 
    
    comm_ptr->ch.is_global_block = 0; 
    /* We need to check to see if the ranks are block or not. Each node leader
     * gets the global ranks of all of its children processes. It scans through
     * this array to see if the ranks are in block order. The node-leaders then
     * do an allreduce to see if all the other nodes are also in block order.
     * This is followed by an intra-node bcast to let the children processes
     * know of the result of this step */ 
    if(my_local_id == 0) {
            int is_local_block = 1; 
            int index = 1; 
            
            while( index < my_local_size) { 
                    if( (shmem_group[index] - 1) != 
                         shmem_group[index - 1]) { 
                            is_local_block = 0; 
                            break; 
                    }
            index++;  
            }  

            comm_ptr->ch.shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
            mpi_errno = MPIR_Allreduce_impl(&(is_local_block), 
                                      &(comm_ptr->ch.is_global_block), 1, 
                                      MPI_INT, MPI_LAND, leader_ptr, &errflag);
            if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
            } 
            mpi_errno = MPIR_Bcast_impl(&(comm_ptr->ch.is_global_block),1, MPI_INT, 0,
                                   shmem_ptr, &errflag); 
            if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
            } 
    } else { 
            mpi_errno = MPIR_Bcast_impl(&(comm_ptr->ch.is_global_block),1, MPI_INT, 0,
                                   shmem_ptr, &errflag); 
            if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
            } 
    }      
                              

    if (my_local_id == 0){
        lock_shmem_region();
        increment_shmem_comm_count();
        shmem_comm_count = get_shmem_comm_count();
        unlock_shmem_region();
    }
    
    shmem_ptr->ch.shmem_coll_ok = 0; 
    /* To prevent Bcast taking the knomial_2level_bcast route */
    mpi_errno = MPIR_Bcast_impl (&shmem_comm_count, 1, MPI_INT, 0, shmem_ptr, &errflag);
     if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }


    if (shmem_comm_count <= g_shmem_coll_blocks){
        shmem_ptr->ch.shmem_comm_rank = shmem_comm_count-1;
        input_flag = 1;
    } else{
        input_flag = 0;
    }
    comm_ptr->ch.shmem_coll_ok = 0;/* To prevent Allreduce taking shmem route*/
    mpi_errno = MPIR_Allreduce_impl(&input_flag, &output_flag, 1, MPI_INT, MPI_LAND, comm_ptr, &errflag);
    if(mpi_errno) {
       MPIU_ERR_POP(mpi_errno);
    }
    comm_ptr->ch.allgather_comm_ok = 0;
    if (allgather_ranking){
        int is_contig =1, check_leader =1, check_size=1, is_local_ok=0,is_block=0;
        int PPN;
        int shmem_grp_size = my_local_size;
        int leader_rank; 
        MPI_Group allgather_group; 
        comm_ptr->ch.allgather_comm=MPI_COMM_NULL; 
        comm_ptr->ch.allgather_new_ranks=NULL;

        if(comm_ptr->ch.leader_comm != MPI_COMM_NULL) { 
            PMPI_Comm_rank(comm_ptr->ch.leader_comm, &leader_rank); 
        } 

        mpi_errno=MPIR_Bcast_impl(&leader_rank, 1, MPI_INT, 0, shmem_ptr, &errflag);
        if(mpi_errno) {
        	MPIU_ERR_POP(mpi_errno);
        } 

        for (i=1; i < shmem_grp_size; i++ ){
            if (shmem_group[i] != shmem_group[i-1]+1){
                is_contig =0; 
                break;
            }
        }

        if (leader != (shmem_grp_size*leader_rank)){
            check_leader=0;
        }

        if (shmem_grp_size != (size/leader_group_size)){
            check_size=0;
        }

        is_local_ok = is_contig && check_leader && check_size;

        mpi_errno = MPIR_Allreduce_impl(&is_local_ok, &is_block, 1, MPI_INT, MPI_LAND, comm_ptr, &errflag);
        if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }

        if (is_block){
            int counter=0,j;
            comm_ptr->ch.allgather_new_ranks = MPIU_Malloc(sizeof(int)*size);
            if (NULL == comm_ptr->ch.allgather_new_ranks){
                    mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                  FCNAME, __LINE__, MPI_ERR_OTHER, 
                                                  "**nomem", 0 );
                    return mpi_errno;
            }
   
            PPN = shmem_grp_size;
            
            for (j=0; j < PPN; j++){
                for (i=0; i < leader_group_size; i++){
                    comm_ptr->ch.allgather_new_ranks[counter] = j + i*PPN;
                    counter++;
                }
            }

            mpi_errno = PMPI_Group_incl(comm_group, size, comm_ptr->ch.allgather_new_ranks, &allgather_group);
            if(mpi_errno) {
                 MPIU_ERR_POP(mpi_errno);
            }  
            mpi_errno = PMPI_Comm_create(comm_ptr->handle, allgather_group, &(comm_ptr->ch.allgather_comm));
            if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
            }
            comm_ptr->ch.allgather_comm_ok = 1;
            
            mpi_errno=PMPI_Group_free(&allgather_group);
            if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
            }
        }
    }
    mpi_errno=PMPI_Group_free(&comm_group);
    if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
    }

    if (output_flag == 1){
        comm_ptr->ch.shmem_coll_ok = 1;
        comm_registry[comm_registered++] = comm_ptr->context_id;
    } else{
        comm_ptr->ch.shmem_coll_ok = 0;
        MPID_Group_get_ptr( subgroup1, group_ptr );
        if(group_ptr != NULL) { 
             mpi_errno = PMPI_Group_free(&subgroup1);
             if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
             }
        }
        MPID_Group_get_ptr( comm_group, group_ptr );
        if(group_ptr != NULL) { 
             mpi_errno = PMPI_Group_free(&comm_group);
             if(mpi_errno) {
               MPIU_ERR_POP(mpi_errno);
             }
        }
        free_2level_comm(comm_ptr);
        comm_ptr->ch.shmem_comm = MPI_COMM_NULL; 
        comm_ptr->ch.leader_comm = MPI_COMM_NULL; 
    }
    ++comm_count;
    MPIU_Free(shmem_group);

    fn_fail: 
       MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    
    return (mpi_errno);


}

int init_thread_reg(void)
{
    int j = 0;

    for (; j < MAX_NUM_THREADS; ++j)
    {
        thread_reg[j] = -1;
    }

    return 1;
}

int check_split_comm(pthread_t my_id)
{
    int j = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (pthread_equal(thread_reg[j], my_id))
        {
            pthread_mutex_unlock(&comm_lock);
            return 0;
        }
    }

    pthread_mutex_unlock(&comm_lock);
    return 1;
}

int disable_split_comm(pthread_t my_id)
{
    int j = 0;
    int found = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (thread_reg[j] == -1)
        {
            thread_reg[j] = my_id;
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&comm_lock);

    if (found == 0)
    {
        printf("Error:max number of threads reached\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    return 1;
}


int enable_split_comm(pthread_t my_id)
{
    int j = 0;
    int found = 0;
    pthread_mutex_lock(&comm_lock);

    for (; j < MAX_NUM_THREADS; ++j)
    {
        if (pthread_equal(thread_reg[j], my_id))
        {
            thread_reg[j] = -1;
            found = 1;
            break;
        }
    }

    pthread_mutex_unlock(&comm_lock);

    if (found == 0)
    {
        printf("Error: Could not locate thread id\n");
        ibv_error_abort (GEN_EXIT_ERR, "create_2level_com");
    }

    return 1;
}

int check_comm_registry(MPI_Comm comm)
{
    MPID_Comm* comm_ptr;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    MPID_Comm_get_ptr( comm, comm_ptr );
    int context_id = 0, i =0, my_rank, size;
    context_id = comm_ptr->context_id;

    PMPI_Comm_rank(comm, &my_rank);
    PMPI_Comm_size(comm, &size);

    for (i = 0; i < comm_registered; i++){
        if (comm_registry[i] == context_id){
            return 1;
        }
    }

    return 0;
}

#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
