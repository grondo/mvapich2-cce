/* -*- Mode: C; c-basic-offset:4 ; -*- */
/* Copyright (c) 2003-2011, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 */
/*
 *
 *  (C) 2001 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "mpiimpl.h"

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "datatype.h"
#include "coll_shmem.h"
extern struct coll_runtime coll_param;
#endif

int allgather_tuning(int comm_size, int pof2)
{
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) 
   char *value;
   if (pof2 == 1 && (value = getenv("MV2_ALLGATHER_RD_THRESHOLD")) != NULL) { 
       /* pof2 case. User has set the run-time parameter "MV2_ALLGATHER_RD_THRESHOLD".
        * Just use that value */
        return coll_param.allgather_rd_threshold; 
   } if(pof2 == 0 && (value = getenv("MV2_ALLGATHER_BRUCK_THRESHOLD")) != NULL) {
       /* Non-pof2 case. User has set the run-time parameter "MV2_ALLGATHER_BRUCK_THRESHOLD".
        * Just use that value */
        return coll_param.allgather_bruck_threshold;
   } else {
       /* User has not used any run-time parameters. 
        */ 
       if(comm_size <= ALLGATHER_SMALL_SYSTEM_SIZE) {
 	   return tuning_table[ALLGATHER_ID][SMALL]; 
       } else if(comm_size > ALLGATHER_SMALL_SYSTEM_SIZE && comm_size <= ALLGATHER_MEDIUM_SYSTEM_SIZE) { 
	   return tuning_table[ALLGATHER_ID][MEDIUM];
       } else { 
           return tuning_table[ALLGATHER_ID][LARGE];
       } 
   }
#else
   if(pof2 == 1) { 
       return MV2_ALLGATHER_SHORT_MSG; 
   } else { 
       return MV2_ALLGATHER_LONG_MSG; 
   } 
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */ 

}

/* This is the default implementation of allgather. The algorithm is:
   
   Algorithm: MPI_Allgather

   For short messages and non-power-of-two no. of processes, we use
   the algorithm from the Jehoshua Bruck et al IEEE TPDS Nov 97
   paper. It is a variant of the disemmination algorithm for
   barrier. It takes ceiling(lg p) steps.

   Cost = lgp.alpha + n.((p-1)/p).beta
   where n is total size of data gathered on each process.

   For short or medium-size messages and power-of-two no. of
   processes, we use the recursive doubling algorithm.

   Cost = lgp.alpha + n.((p-1)/p).beta

   TODO: On TCP, we may want to use recursive doubling instead of the Bruck
   algorithm in all cases because of the pairwise-exchange property of
   recursive doubling (see Benson et al paper in Euro PVM/MPI
   2003).

   It is interesting to note that either of the above algorithms for
   MPI_Allgather has the same cost as the tree algorithm for MPI_Gather!

   For long messages or medium-size messages and non-power-of-two
   no. of processes, we use a ring algorithm. In the first step, each
   process i sends its contribution to process i+1 and receives
   the contribution from process i-1 (with wrap-around). From the
   second step onwards, each process i forwards to process i+1 the
   data it received from process i-1 in the previous step. This takes
   a total of p-1 steps.

   Cost = (p-1).alpha + n.((p-1)/p).beta

   We use this algorithm instead of recursive doubling for long
   messages because we find that this communication pattern (nearest
   neighbor) performs twice as fast as recursive doubling for long
   messages (on Myrinet and IBM SP).

   Possible improvements: 

   End Algorithm: MPI_Allgather
*/
/* begin:nested */
/* not declared static because a machine-specific function may call this 
   one in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Allgather_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allgather_intra_MV2 ( 
    void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype,
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr, 
    int *errflag )
{
    int comm_size, rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Aint   recvtype_extent;
    MPI_Aint recvtype_true_extent, recvbuf_extent, recvtype_true_lb;
    int j, i, src, rem;
    void *tmp_buf;
    int curr_cnt, dst, type_size, left, right, jnext;
    MPI_Comm comm;
    int pof2=0, comm_size_is_pof2=0;
    MPI_Status status;
    int mask, dst_tree_root, my_tree_root, is_homogeneous,  
        send_offset, recv_offset, last_recv_cnt = 0, nprocs_completed, k,
        offset, tmp_mask, tree_root;
#ifdef MPID_HAS_HETERO
    int position, tmp_buf_size, nbytes;
#endif

    if (((sendcount == 0) && (sendbuf != MPI_IN_PLACE)) || (recvcount == 0)) { 
        return MPI_SUCCESS;
    } 
    
    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    MPID_Datatype_get_extent_macro( recvtype, recvtype_extent );
    MPID_Datatype_get_size_macro( recvtype, type_size );

    /* check if comm_size is a power of two */
    pof2 = 1;
    while (pof2 < comm_size)
        pof2 *= 2;
    if (pof2 == comm_size) { 
        comm_size_is_pof2 = 1;
    } else { 
        comm_size_is_pof2 = 0;
   }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    if ((recvcount*type_size <=  allgather_tuning(comm_size, comm_size_is_pof2)) &&
        (comm_size_is_pof2 == 1)) {
        /* Short or medium size message and power-of-two no. of processes. Use
         * recursive doubling algorithm */   

        is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
        if (comm_ptr->is_hetero) { 
           is_homogeneous = 0;
        } 
#endif
    
        if (is_homogeneous) {
            /* homogeneous. no need to pack into tmp_buf on each node. copy
               local data into recvbuf */ 
            if (sendbuf != MPI_IN_PLACE) {
                mpi_errno = MPIR_Localcopy (sendbuf, sendcount, sendtype,
                                            ((char *)recvbuf +
                                             rank*recvcount*recvtype_extent), 
                                            recvcount, recvtype);
		if (mpi_errno) {
		    MPIU_ERR_POP(mpi_errno);
		}
            }
            
            curr_cnt = recvcount;
            
            mask = 0x1;
            i = 0;
            while (mask < comm_size) {
                dst = rank ^ mask;
                
                /* find offset into send and recv buffers. zero out 
                   the least significant "i" bits of rank and dst to 
                   find root of src and dst subtrees. Use ranks of 
                   roots as index to send from and recv into buffer */ 
                
                dst_tree_root = dst >> i;
                dst_tree_root <<= i;
                
                my_tree_root = rank >> i;
                my_tree_root <<= i;

		/* FIXME: saving an MPI_Aint into an int */
                send_offset = my_tree_root * recvcount * recvtype_extent;
                recv_offset = dst_tree_root * recvcount * recvtype_extent;
                
                if (dst < comm_size) {
                    mpi_errno = MPIC_Sendrecv_ft(((char *)recvbuf + send_offset),
                                              curr_cnt, recvtype, dst,
                                              MPIR_ALLGATHER_TAG,  
                                              ((char *)recvbuf + recv_offset),
					      (comm_size-dst_tree_root)*recvcount,
                                              recvtype, dst,
                                              MPIR_ALLGATHER_TAG, comm, &status, errflag);
		    if (mpi_errno) {
                       /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        last_recv_cnt = 0;
		    }
                    
                    MPIR_Get_count_impl(&status, recvtype, &last_recv_cnt);
                    curr_cnt += last_recv_cnt;
                }
                
                /* if some processes in this process's subtree in this step
                   did not have any destination process to communicate with
                   because of non-power-of-two, we need to send them the
                   data that they would normally have received from those
                   processes. That is, the haves in this subtree must send to
                   the havenots. We use a logarithmic recursive-halfing algorithm
                   for this. */
                
                /* This part of the code will not currently be
                 executed because we are not using recursive
                 doubling for non power of two. Mark it as experimental
                 so that it doesn't show up as red in the coverage
                 tests. */  

		/* --BEGIN EXPERIMENTAL-- */
                if (dst_tree_root + mask > comm_size) {
                    nprocs_completed = comm_size - my_tree_root - mask;
                    /* nprocs_completed is the number of processes in this
                       subtree that have all the data. Send data to others
                       in a tree fashion. First find root of current tree
                       that is being divided into two. k is the number of
                       least-significant bits in this process's rank that
                       must be zeroed out to find the rank of the root */ 
                    j = mask;
                    k = 0;
                    while (j) {
                        j >>= 1;
                        k++;
                    }
                    k--;

		    /* FIXME: saving an MPI_Aint into an int */
                    offset = recvcount * (my_tree_root + mask) * recvtype_extent;
                    tmp_mask = mask >> 1;
                    
                    while (tmp_mask) {
                        dst = rank ^ tmp_mask;
                        
                        tree_root = rank >> k;
                        tree_root <<= k;
                        
                        /* send only if this proc has data and destination
                           doesn't have data. at any step, multiple processes
                           can send if they have the data */
                        if ((dst > rank) && 
                            (rank < tree_root + nprocs_completed)
                            && (dst >= tree_root + nprocs_completed)) {
                            mpi_errno = MPIC_Send_ft(((char *)recvbuf + offset),
                                                  last_recv_cnt,
                                                  recvtype, dst,
                                                  MPIR_ALLGATHER_TAG, comm, errflag); 
                            /* last_recv_cnt was set in the previous
                               receive. that's the amount of data to be
                               sent now. */
			    if (mpi_errno) {
                               /* for communication errors, just record the error but continue */
                                *errflag = TRUE;
                                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
			    }
                        }
                        /* recv only if this proc. doesn't have data and sender
                           has data */
                        else if ((dst < rank) && 
                                 (dst < tree_root + nprocs_completed) &&
                                 (rank >= tree_root + nprocs_completed)) {
                            mpi_errno = MPIC_Recv_ft(((char *)recvbuf + offset),  
						  (comm_size - (my_tree_root + mask))*recvcount,
                                                  recvtype, dst,
                                                  MPIR_ALLGATHER_TAG,
                                                  comm, &status, errflag); 
                            /* nprocs_completed is also equal to the
                               no. of processes whose data we don't have */
			    if (mpi_errno) {
                                /* for communication errors, just record the error but continue */
                                *errflag = TRUE;
                                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                                last_recv_cnt = 0;
			    }
                            MPIR_Get_count_impl(&status, recvtype, &last_recv_cnt);
                            curr_cnt += last_recv_cnt;
                        }
                        tmp_mask >>= 1;
                        k--;
                    }
                }
                /* --END EXPERIMENTAL-- */
                
                mask <<= 1;
                i++;
            }
        }
        
#ifdef MPID_HAS_HETERO
        else { 
            /* heterogeneous. need to use temp. buffer. */
            
            MPIR_Pack_size_impl(recvcount*comm_size, recvtype, &tmp_buf_size);
            
            tmp_buf = MPIU_Malloc(tmp_buf_size);
	    /* --BEGIN ERROR HANDLING-- */
            if (!tmp_buf) { 
                mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
                return mpi_errno;
            }
	    /* --END ERROR HANDLING-- */
            
            /* calculate the value of nbytes, the number of bytes in packed
               representation that each process contributes. We can't simply divide
               tmp_buf_size by comm_size because tmp_buf_size is an upper
               bound on the amount of memory required. (For example, for
               a single integer, MPICH-1 returns pack_size=12.) Therefore, we
               actually pack some data into tmp_buf and see by how much
               'position' is incremented. */
            
            position = 0;
            MPIR_Pack_impl(recvbuf, 1, recvtype, tmp_buf, tmp_buf_size, &position);
            nbytes = position*recvcount;
            
            /* pack local data into right location in tmp_buf */
            position = rank * nbytes;
            if (sendbuf != MPI_IN_PLACE) {
                MPIR_Pack_impl(sendbuf, sendcount, sendtype, tmp_buf, tmp_buf_size, &position);
            } else {
                /* if in_place specified, local data is found in recvbuf */
                MPIR_Pack_impl(((char *)recvbuf + recvtype_extent*rank), recvcount,
                          recvtype, tmp_buf, tmp_buf_size, &position);
            }
            
            curr_cnt = nbytes;
            
            mask = 0x1;
            i = 0;
            while (mask < comm_size) {
                dst = rank ^ mask;
                
                /* find offset into send and recv buffers. zero out 
                   the least significant "i" bits of rank and dst to 
                   find root of src and dst subtrees. Use ranks of 
                   roots as index to send from and recv into buffer. */ 
                
                dst_tree_root = dst >> i;
                dst_tree_root <<= i;
                
                my_tree_root = rank >> i;
                my_tree_root <<= i;
                
                send_offset = my_tree_root * nbytes;
                recv_offset = dst_tree_root * nbytes;
                
                if (dst < comm_size) {
                    mpi_errno = MPIC_Sendrecv_ft(((char *)tmp_buf + send_offset),
                                              curr_cnt, MPI_BYTE, dst,
                                              MPIR_ALLGATHER_TAG,  
                                              ((char *)tmp_buf + recv_offset),
					      tmp_buf_size - recv_offset,
                                              MPI_BYTE, dst,
                                              MPIR_ALLGATHER_TAG, comm, &status, errflag);
		    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        last_recv_cnt = 0;
		    }
                    
                    MPIR_Get_count_impl(&status, MPI_BYTE, &last_recv_cnt);
                    curr_cnt += last_recv_cnt;
                }
                
                /* if some processes in this process's subtree in this step
                   did not have any destination process to communicate with
                   because of non-power-of-two, we need to send them the
                   data that they would normally have received from those
                   processes. That is, the haves in this subtree must send to
                   the havenots. We use a logarithmic recursive-halfing 
		   algorithm for this. */
                
                if (dst_tree_root + mask > comm_size) {
                    nprocs_completed = comm_size - my_tree_root - mask;
                    /* nprocs_completed is the number of processes in this
                       subtree that have all the data. Send data to others
                       in a tree fashion. First find root of current tree
                       that is being divided into two. k is the number of
                       least-significant bits in this process's rank that
                       must be zeroed out to find the rank of the root */ 
                    j = mask;
                    k = 0;
                    while (j) {
                        j >>= 1;
                        k++;
                    }
                    k--;
                    
                    offset = nbytes * (my_tree_root + mask);
                    tmp_mask = mask >> 1;
                    
                    while (tmp_mask) {
                        dst = rank ^ tmp_mask;
                        tree_root = rank >> k;
                        tree_root <<= k;
                        
                        /* send only if this proc has data and destination
                           doesn't have data. at any step, multiple processes
                           can send if they have the data */
                        if ((dst > rank) && 
                            (rank < tree_root + nprocs_completed)
                            && (dst >= tree_root + nprocs_completed)) {
                            
                            mpi_errno = MPIC_Send_ft(((char *)tmp_buf + offset),
                                                  last_recv_cnt, MPI_BYTE,
                                                  dst, MPIR_ALLGATHER_TAG,
                                                  comm, errflag);  
                            /* last_recv_cnt was set in the previous
                               receive. that's the amount of data to be
                               sent now. */
			    if (mpi_errno) {
                                /* for communication errors, just record the error but continue */
                                *errflag = TRUE;
                                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
			    }
                        }
                        /* recv only if this proc. doesn't have data and sender
                           has data */
                        else if ((dst < rank) && 
                                 (dst < tree_root + nprocs_completed) &&
                                 (rank >= tree_root + nprocs_completed)) {
                            mpi_errno = MPIC_Recv(((char *)tmp_buf + offset),
                                                  tmp_buf_size - offset,
                                                  MPI_BYTE, dst,
                                                  MPIR_ALLGATHER_TAG,
                                                  comm, &status, errflag); 
                            /* nprocs_completed is also equal to the
                               no. of processes whose data we don't have */
			    if (mpi_errno) {
                                /* for communication errors, just record the error but continue */
                                *errflag = TRUE;
                                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                                last_recv_cnt = 0;
			    }
                            MPIR_Get_count_impl(&status, MPI_BYTE, &last_recv_cnt);
                            curr_cnt += last_recv_cnt;
                        }
                        tmp_mask >>= 1;
                        k--;
                    }
                }
                mask <<= 1;
                i++;
            }
            
            position = 0;
            MPIR_Unpack_impl(tmp_buf, tmp_buf_size, &position, recvbuf,
                        recvcount*comm_size, recvtype);

            MPIU_Free(tmp_buf);
        }
#endif /* MPID_HAS_HETERO */
    } else if (recvcount*type_size <=  allgather_tuning(comm_size, comm_size_is_pof2)) {
        /* Short message and non-power-of-two no. of processes. Use
         * Bruck algorithm (see description above). */

        /* allocate a temporary buffer of the same size as recvbuf. */

        /* get true extent of recvtype */
        MPIR_Type_get_true_extent_impl(recvtype, &recvtype_true_lb, &recvtype_true_extent);  
        recvbuf_extent = recvcount * comm_size *
            (MPIR_MAX(recvtype_true_extent, recvtype_extent));

        tmp_buf = MPIU_Malloc(recvbuf_extent);
        /* --BEGIN ERROR HANDLING-- */
        if (!tmp_buf) {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0 );
            return mpi_errno;
        }
	/* --END ERROR HANDLING-- */
            
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - recvtype_true_lb);

        /* copy local data to the top of tmp_buf */ 
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno = MPIR_Localcopy (sendbuf, sendcount, sendtype,
                                        tmp_buf, recvcount, recvtype);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        } else {
            mpi_errno = MPIR_Localcopy (((char *)recvbuf +
                                         rank * recvcount * recvtype_extent), 
                                        recvcount, recvtype, tmp_buf, 
                                        recvcount, recvtype);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }
        
        /* do the first \floor(\lg p) steps */

        curr_cnt = recvcount;
        pof2 = 1;
        while (pof2 <= comm_size/2) {
            src = (rank + pof2) % comm_size;
            dst = (rank - pof2 + comm_size) % comm_size;
            
            mpi_errno = MPIC_Sendrecv_ft(tmp_buf, curr_cnt, recvtype, dst,
                                      MPIR_ALLGATHER_TAG,
                                      ((char *)tmp_buf + curr_cnt*recvtype_extent),
                                      curr_cnt, recvtype,
                                      src, MPIR_ALLGATHER_TAG, comm,
                                      MPI_STATUS_IGNORE, errflag);
	    if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            curr_cnt *= 2;
            pof2 *= 2;
        }

        /* if comm_size is not a power of two, one more step is needed */

        rem = comm_size - pof2;
        if (rem) {
            src = (rank + pof2) % comm_size;
            dst = (rank - pof2 + comm_size) % comm_size;
            
            mpi_errno = MPIC_Sendrecv_ft(tmp_buf, rem * recvcount, recvtype,
                                      dst, MPIR_ALLGATHER_TAG,
                                      ((char *)tmp_buf + curr_cnt*recvtype_extent),
                                      rem * recvcount, recvtype,
                                      src, MPIR_ALLGATHER_TAG, comm,
                                      MPI_STATUS_IGNORE, errflag);
	    if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
	    }
        }

        /* Rotate blocks in tmp_buf down by (rank) blocks and store
         * result in recvbuf. */
        
        mpi_errno = MPIR_Localcopy(tmp_buf, (comm_size-rank)*recvcount,
                  recvtype, (char *) recvbuf + rank*recvcount*recvtype_extent, 
                                       (comm_size-rank)*recvcount, recvtype);
	if (mpi_errno) {
	    MPIU_ERR_POP(mpi_errno);
	}

        if (rank) {
            mpi_errno = MPIR_Localcopy((char *) tmp_buf + 
                                   (comm_size-rank)*recvcount*recvtype_extent, 
                                       rank*recvcount, recvtype, recvbuf,
                                       rank*recvcount, recvtype);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }

        MPIU_Free((char*)tmp_buf + recvtype_true_lb);
    } else {  /* long message or medium-size message and non-power-of-two
             * no. of processes. use ring algorithm. */
      
        /* First, load the "local" version in the recvbuf. */
        if (sendbuf != MPI_IN_PLACE) {
            mpi_errno = MPIR_Localcopy(sendbuf, sendcount, sendtype, 
                                       ((char *)recvbuf +
                                        rank*recvcount*recvtype_extent),  
                                       recvcount, recvtype);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }
        
        /* 
           Now, send left to right.  This fills in the receive area in 
           reverse order.
        */
        left  = (comm_size + rank - 1) % comm_size;
        right = (rank + 1) % comm_size;
        
        j     = rank;
        jnext = left;
        for (i=1; i<comm_size; i++) {
            mpi_errno = MPIC_Sendrecv_ft(((char *)recvbuf +
                                       j*recvcount*recvtype_extent), 
                                      recvcount, recvtype, right,
                                      MPIR_ALLGATHER_TAG, 
                                      ((char *)recvbuf +
                                       jnext*recvcount*recvtype_extent), 
                                      recvcount, recvtype, left, 
                                      MPIR_ALLGATHER_TAG, comm,
                                      MPI_STATUS_IGNORE, errflag);
	    if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
	    }
            j	    = jnext;
            jnext = (comm_size + jnext - 1) % comm_size;
        }
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );

 fn_fail:
    return (mpi_errno);
}
/* end:nested */

/* begin:nested */
/* not declared static because a machine-specific function may call this one 
   in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Allgatherv_inter_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allgather_inter_MV2 ( 
    void *sendbuf, 
    int sendcount, 
    MPI_Datatype sendtype,
    void *recvbuf, 
    int recvcount, 
    MPI_Datatype recvtype, 
    MPID_Comm *comm_ptr, 
    int *errflag )
{
    /* Intercommunicator Allgather.
       Each group does a gather to local root with the local
       intracommunicator, and then does an intercommunicator broadcast.
    */

    int rank, local_size, remote_size, mpi_errno = MPI_SUCCESS, root;
    MPI_Aint true_extent, true_lb = 0, extent, send_extent;
    void *tmp_buf=NULL;
    MPID_Comm *newcomm_ptr = NULL;

    local_size = comm_ptr->local_size; 
    remote_size = comm_ptr->remote_size;
    rank = comm_ptr->rank;

    if ((rank == 0) && (sendcount != 0)) {
        /* In each group, rank 0 allocates temp. buffer for local
           gather */
        MPIR_Type_get_true_extent_impl(sendtype, &true_lb, &true_extent);
        MPID_Datatype_get_extent_macro( sendtype, send_extent );
        extent = MPIR_MAX(send_extent, true_extent);

        tmp_buf = MPIU_Malloc(extent*sendcount*local_size);
        if (!tmp_buf) {
	    MPIU_ERR_SETANDJUMP(mpi_errno,MPI_ERR_OTHER,"**nomem");
        }
        /* adjust for potential negative lower bound in datatype */
        tmp_buf = (void *)((char*)tmp_buf - true_lb);
    }

    /* Get the local intracommunicator */
    if (!comm_ptr->local_comm)
	MPIR_Setup_intercomm_localcomm( comm_ptr );

    newcomm_ptr = comm_ptr->local_comm;

    if (sendcount != 0) {
        mpi_errno = MPIR_Gather_MV2(sendbuf, sendcount, sendtype, tmp_buf, sendcount,
                                sendtype, 0, newcomm_ptr, errflag);
	if (mpi_errno) {
	    MPIU_ERR_POP(mpi_errno);
	}
    }

    /* first broadcast from left to right group, then from right to
       left group */
    if (comm_ptr->is_low_group) {
        /* bcast to right*/
        if (sendcount != 0) {
            root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
            mpi_errno = MPIR_Bcast_inter_MV2(tmp_buf, sendcount*local_size,
                                         sendtype, root, comm_ptr, errflag);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }

        /* receive bcast from right */
        if (recvcount != 0) {
            root = 0;
            mpi_errno = MPIR_Bcast_inter_MV2(recvbuf, recvcount*remote_size,
                                         recvtype, root, comm_ptr, errflag);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }
    } else {
        /* receive bcast from left */
        if (recvcount != 0) {
            root = 0;
            mpi_errno = MPIR_Bcast_inter_MV2(recvbuf, recvcount*remote_size,
                                         recvtype, root, comm_ptr, errflag);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
	    }
        }

        /* bcast to left */
        if (sendcount != 0) {
            root = (rank == 0) ? MPI_ROOT : MPI_PROC_NULL;
            mpi_errno = MPIR_Bcast_inter_MV2(tmp_buf, sendcount*local_size,
                                         sendtype, root, comm_ptr, errflag);
	    if (mpi_errno) {
		MPIU_ERR_POP(mpi_errno);
           }
        }
    }
 fn_fail:
    if ((rank == 0) && (sendcount != 0) && tmp_buf)
        MPIU_Free((char*)tmp_buf+true_lb);

    return mpi_errno;
}
/* end:nested */

#undef FUNCNAME
#define FUNCNAME MPIR_Allgather_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Allgather_MV2(void *sendbuf, int sendcount, MPI_Datatype sendtype,
                       void *recvbuf, int recvcount, MPI_Datatype recvtype,
                       MPID_Comm * comm_ptr, int *errflag)
{
    
    int mpi_errno = MPI_SUCCESS;
    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
        int comm_size_is_pof2=0;
        int nbytes=0, comm_size, recvtype_size;
        
        /* Get the size of the communicator */
        comm_size = comm_ptr->local_size;
        
        /*Check if it's a pof2 or not*/    
        if(!(comm_size & (comm_size-1))){
            comm_size_is_pof2=1;
        } 
        
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        nbytes = recvtype_size * recvcount;
        
        /* intracommunicator */
        if (allgather_ranking == 1 && comm_ptr->ch.allgather_comm_ok == 1 && 
           (nbytes <=  allgather_tuning(comm_size, comm_size_is_pof2))) {
                    int i, rank;
                    MPI_Aint   recvtype_extent;
                    int sendtype_iscontig=0, recvtype_iscontig=0;
                    void *tmp_recv_buf=NULL;
                    
                    if(sendtype != MPI_DATATYPE_NULL && recvtype != MPI_DATATYPE_NULL){
                        MPIR_Datatype_iscontig(sendtype, &sendtype_iscontig);
                        MPIR_Datatype_iscontig(recvtype, &recvtype_iscontig);
                    }

                    MPID_Datatype_get_extent_macro( recvtype, recvtype_extent );
                    mpi_errno = PMPI_Comm_rank(comm_ptr->handle, &rank);
                    if(mpi_errno) {
                        MPIU_ERR_POP(mpi_errno);
                    }

                    MPID_Comm *allgather_comm_ptr;
                    MPID_Comm_get_ptr(comm_ptr->ch.allgather_comm, allgather_comm_ptr);
                    
                    /*creation of a temporary recvbuf*/
                    tmp_recv_buf = MPIU_Malloc( recvcount * comm_size * recvtype_extent ); 
                    if (!tmp_recv_buf) { 
                        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPIR_ERR_RECOVERABLE, 
                                                          FCNAME, __LINE__, MPI_ERR_OTHER, 
                                                          "**nomem", 0 );
                        return mpi_errno;
                    }
                    /* Calling Allgather with temporary buffer and allgather communicator */
                    if(sendbuf != MPI_IN_PLACE) {
                        mpi_errno = MPIR_Allgather_intra_MV2(sendbuf, sendcount, sendtype,
                                                         tmp_recv_buf, recvcount, recvtype,
                                                         allgather_comm_ptr, errflag);
                    } else { 
                        mpi_errno = MPIR_Allgather_intra_MV2(recvbuf + rank * recvcount * 
                                                         recvtype_extent, recvcount, recvtype,
                                                         tmp_recv_buf, recvcount, recvtype,
                                                         allgather_comm_ptr, errflag);
                    } 
                    if (mpi_errno){
                        MPIU_ERR_POP(mpi_errno);
                    }
                    /* Reordering data into recvbuf */ 
                    if (sendtype_iscontig == 1 && recvtype_iscontig ==1 ) { 
                            for (i=0; i<comm_size; i++){
                                 MPIUI_Memcpy((void*)((char*)recvbuf +
                                               (comm_ptr->ch.allgather_new_ranks[i])*nbytes),
                                               (char*)tmp_recv_buf+ i*nbytes, nbytes);
                            }
                    } else { 
                            for (i=0; i<comm_size; i++){
                                 mpi_errno = MPIR_Localcopy( (void *)((char*)tmp_recv_buf + 
                                                 i*recvcount*recvtype_extent),
                                                 recvcount, recvtype,
                                                 (void *)((char*)recvbuf + 
                                                 (comm_ptr->ch.allgather_new_ranks[i])*
                                                 recvcount*recvtype_extent),
                                                 recvcount, recvtype);
                                 if (mpi_errno){
                                      MPIU_ERR_POP(mpi_errno);
                                 }
                            }
                    } 
                    MPIU_Free(tmp_recv_buf); 
        } else {
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
            mpi_errno = MPIR_Allgather_intra_MV2(sendbuf, sendcount, sendtype,
                                                 recvbuf, recvcount, recvtype,
                                                 comm_ptr, errflag);
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)       
        }
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */    
        if (mpi_errno){
            MPIU_ERR_POP(mpi_errno);
        }
    } else {
        /* intercommunicator */
        mpi_errno = MPIR_Allgather_inter_MV2(sendbuf, sendcount, sendtype,
                                             recvbuf, recvcount, recvtype,
                                             comm_ptr, errflag);
        if (mpi_errno){
            MPIU_ERR_POP(mpi_errno);
        }
    }
  
  fn_exit:
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}                   
