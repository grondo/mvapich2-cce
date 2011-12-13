/* -*- Mode: C; c-basic-offset:4 ; -*- */
/*
 *
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


#include "mpiimpl.h"
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "coll_shmem.h"
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */


/* This function implements a binomial tree reduce.

   Cost = lgp.alpha + n.lgp.beta + n.lgp.gamma
 */
#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_binomial_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Reduce_binomial_MV2 ( 
    void *sendbuf, 
    void *recvbuf, 
    int count, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    int root, 
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Status status;
    int comm_size, rank, is_commutative;
    int mask, relrank, source, lroot;
    MPI_User_function *uop;
    MPI_Aint true_lb, true_extent, extent; 
    void *tmp_buf;
    MPID_Op *op_ptr;
    MPI_Comm comm;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(2);
    MPIU_THREADPRIV_DECL;

    if (count == 0) return MPI_SUCCESS;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_GET;
    MPIU_THREADPRIV_FIELD(op_errno) = 0;

    /* Create a temporary buffer */

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op%16 - 1];
    }
    else {
        MPID_Op_get_ptr(op, op_ptr);
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE)
            is_commutative = 0;
        else
            is_commutative = 1;
        
#ifdef HAVE_CXX_BINDING            
            if (op_ptr->language == MPID_LANG_CXX) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
                is_cxx_uop = 1;
            }
            else
#endif
        if ((op_ptr->language == MPID_LANG_C))
            uop = (MPI_User_function *) op_ptr->function.c_function;
        else
            uop = (MPI_User_function *) op_ptr->function.f77_function;
    }

    /* I think this is the worse case, so we can avoid an assert() 
     * inside the for loop */
    /* should be buf+{this}? */
    MPID_Ensure_Aint_fits_in_pointer(count * MPIR_MAX(extent, true_extent));

    MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*(MPIR_MAX(extent,true_extent)),
                        mpi_errno, "temporary buffer");
    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *)((char*)tmp_buf - true_lb);
    
    /* If I'm not the root, then my recvbuf may not be valid, therefore
       I have to allocate a temporary one */
    if (rank != root) {
        MPIU_CHKLMEM_MALLOC(recvbuf, void *, 
                            count*(MPIR_MAX(extent,true_extent)), 
                            mpi_errno, "receive buffer");
        recvbuf = (void *)((char*)recvbuf - true_lb);
    }

    if ((rank != root) || (sendbuf != MPI_IN_PLACE)) {
        mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf,
                                   count, datatype);
        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    }

    /* This code is from MPICH-1. */

    /* Here's the algorithm.  Relative to the root, look at the bit pattern in 
       my rank.  Starting from the right (lsb), if the bit is 1, send to 
       the node with that bit zero and exit; if the bit is 0, receive from the
       node with that bit set and combine (as long as that node is within the
       group)
       
       Note that by receiving with source selection, we guarentee that we get
       the same bits with the same input.  If we allowed the parent to receive 
       the children in any order, then timing differences could cause different
       results (roundoff error, over/underflows in some cases, etc).
       
       Because of the way these are ordered, if root is 0, then this is correct
       for both commutative and non-commutitive operations.  If root is not
       0, then for non-commutitive, we use a root of zero and then send
       the result to the root.  To see this, note that the ordering is
       mask = 1: (ab)(cd)(ef)(gh)            (odds send to evens)
       mask = 2: ((ab)(cd))((ef)(gh))        (3,6 send to 0,4)
       mask = 4: (((ab)(cd))((ef)(gh)))      (4 sends to 0)
       
       Comments on buffering.  
       If the datatype is not contiguous, we still need to pass contiguous 
       data to the user routine.  
       In this case, we should make a copy of the data in some format, 
       and send/operate on that.
       
       In general, we can't use MPI_PACK, because the alignment of that
       is rather vague, and the data may not be re-usable.  What we actually
       need is a "squeeze" operation that removes the skips.
    */
    mask    = 0x1;
    if (is_commutative) 
        lroot   = root;
    else
        lroot   = 0;
    relrank = (rank - lroot + comm_size) % comm_size;
    
    while (/*(mask & relrank) == 0 && */mask < comm_size) {
        /* Receive */
        if ((mask & relrank) == 0) {
            source = (relrank | mask);
            if (source < comm_size) {
                source = (source + lroot) % comm_size;
                mpi_errno = MPIC_Recv_ft(tmp_buf, count, datatype, source, 
                                         MPIR_REDUCE_TAG, comm, &status, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                /* The sender is above us, so the received buffer must be
                   the second argument (in the noncommutative case). */
                if (is_commutative) {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)( tmp_buf, recvbuf, 
                                                        count, datatype, uop );
                    }
                    else 
#endif
                        (*uop)(tmp_buf, recvbuf, &count, &datatype);
                }
                else {
#ifdef HAVE_CXX_BINDING
                    if (is_cxx_uop) {
                        (*MPIR_Process.cxx_call_op_fn)( recvbuf, tmp_buf,
                                                        count, datatype, uop );
                    }
                    else 
#endif
                        (*uop)(recvbuf, tmp_buf, &count, &datatype);
                    mpi_errno = MPIR_Localcopy(tmp_buf, count, datatype,
                                               recvbuf, count, datatype);
                    if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
                }
            }
        }
        else {
            /* I've received all that I'm going to.  Send my result to 
               my parent */
            source = ((relrank & (~ mask)) + lroot) % comm_size;
            mpi_errno  = MPIC_Send_ft(recvbuf, count, datatype,
                                      source, MPIR_REDUCE_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            break;
        }
        mask <<= 1;
    }

    if (!is_commutative && (root != 0))
    {
        if (rank == 0)
        {
            mpi_errno  = MPIC_Send_ft(recvbuf, count, datatype, root,
                                      MPIR_REDUCE_TAG, comm, errflag);
        }
        else if (rank == root)
        {
            mpi_errno = MPIC_Recv_ft(recvbuf, count, datatype, 0,
                                    MPIR_REDUCE_TAG, comm, &status, errflag);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

    /* FIXME does this need to be checked after each uop invocation for
       predefined operators? */
    /* --BEGIN ERROR HANDLING-- */
    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
        goto fn_fail;
    }
    /* --END ERROR HANDLING-- */

fn_exit:
    MPIU_CHKLMEM_FREEALL();
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

/* An implementation of Rabenseifner's reduce algorithm (see
   http://www.hlrs.de/organization/par/services/models/mpi/myreduce.html).

   This algorithm implements the reduce in two steps: first a
   reduce-scatter, followed by a gather to the root. A
   recursive-halving algorithm (beginning with processes that are
   distance 1 apart) is used for the reduce-scatter, and a binomial tree
   algorithm is used for the gather. The non-power-of-two case is
   handled by dropping to the nearest lower power-of-two: the first
   few odd-numbered processes send their data to their left neighbors
   (rank-1), and the reduce-scatter happens among the remaining
   power-of-two processes. If the root is one of the excluded
   processes, then after the reduce-scatter, rank 0 sends its result to
   the root and exits; the root now acts as rank 0 in the binomial tree
   algorithm for gather.

   For the power-of-two case, the cost for the reduce-scatter is 
   lgp.alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma. The cost for the
   gather to root is lgp.alpha + n.((p-1)/p).beta. Therefore, the
   total cost is:
   Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta + n.((p-1)/p).gamma

   For the non-power-of-two case, assuming the root is not one of the
   odd-numbered processes that get excluded in the reduce-scatter,
   Cost = (2.floor(lgp)+1).alpha + (2.((p-1)/p) + 1).n.beta + 
           n.(1+(p-1)/p).gamma
*/
#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_redscat_gather_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Reduce_redscat_gather_MV2 ( 
    void *sendbuf, 
    void *recvbuf, 
    int count, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    int root, 
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int comm_size, rank, pof2, rem, newrank;
    int mask, *cnts, *disps, i, j, send_idx=0;
    int recv_idx, last_idx=0, newdst;
    int dst, send_cnt, recv_cnt, newroot, newdst_tree_root, newroot_tree_root; 
    MPI_User_function *uop;
    MPI_Aint true_lb, true_extent, extent; 
    void *tmp_buf;
    MPID_Op *op_ptr;
    MPI_Comm comm;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif
    MPIU_CHKLMEM_DECL(4);
    MPIU_THREADPRIV_DECL;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* set op_errno to 0. stored in perthread structure */
    MPIU_THREADPRIV_GET;
    MPIU_THREADPRIV_FIELD(op_errno) = 0;

    /* Create a temporary buffer */

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op%16 - 1];
    }
    else {
        MPID_Op_get_ptr(op, op_ptr);
        
#ifdef HAVE_CXX_BINDING            
            if (op_ptr->language == MPID_LANG_CXX) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
                is_cxx_uop = 1;
            }
            else
#endif
        if ((op_ptr->language == MPID_LANG_C))
            uop = (MPI_User_function *) op_ptr->function.c_function;
        else
            uop = (MPI_User_function *) op_ptr->function.f77_function;
    }

    /* I think this is the worse case, so we can avoid an assert() 
     * inside the for loop */
    /* should be buf+{this}? */
    MPID_Ensure_Aint_fits_in_pointer(count * MPIR_MAX(extent, true_extent));

    MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*(MPIR_MAX(extent,true_extent)),
                        mpi_errno, "temporary buffer");
    /* adjust for potential negative lower bound in datatype */
    tmp_buf = (void *)((char*)tmp_buf - true_lb);
    
    /* If I'm not the root, then my recvbuf may not be valid, therefore
       I have to allocate a temporary one */
    if (rank != root) {
        MPIU_CHKLMEM_MALLOC(recvbuf, void *, 
                            count*(MPIR_MAX(extent,true_extent)), 
                            mpi_errno, "receive buffer");
        recvbuf = (void *)((char*)recvbuf - true_lb);
    }

    if ((rank != root) || (sendbuf != MPI_IN_PLACE)) {
        mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf,
                                   count, datatype);
        if (mpi_errno) { MPIU_ERR_POP(mpi_errno); }
    }

    /* find nearest power-of-two less than or equal to comm_size */
    pof2 = 1;
    while (pof2 <= comm_size) pof2 <<= 1;
    pof2 >>=1;

    rem = comm_size - pof2;

    /* In the non-power-of-two case, all odd-numbered
       processes of rank < 2*rem send their data to
       (rank-1). These odd-numbered processes no longer
       participate in the algorithm until the very end. The
       remaining processes form a nice power-of-two. 

       Note that in MPI_Allreduce we have the even-numbered processes
       send data to odd-numbered processes. That is better for
       non-commutative operations because it doesn't require a
       buffer copy. However, for MPI_Reduce, the most common case
       is commutative operations with root=0. Therefore we want
       even-numbered processes to participate the computation for
       the root=0 case, in order to avoid an extra send-to-root
       communication after the reduce-scatter. In MPI_Allreduce it
       doesn't matter because all processes must get the result. */
    
    if (rank < 2*rem) {
        if (rank % 2 != 0) { /* odd */
            mpi_errno = MPIC_Send_ft(recvbuf, count,
                                     datatype, rank-1,
                                     MPIR_REDUCE_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            
            /* temporarily set the rank to -1 so that this
               process does not pariticipate in recursive
               doubling */
            newrank = -1; 
        }
        else { /* even */
            mpi_errno = MPIC_Recv_ft(tmp_buf, count,
                                     datatype, rank+1,
                                     MPIR_REDUCE_TAG, comm,
                                     MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            
            /* do the reduction on received data. */
            /* This algorithm is used only for predefined ops
               and predefined ops are always commutative. */
#ifdef HAVE_CXX_BINDING
            if (is_cxx_uop) {
                (*MPIR_Process.cxx_call_op_fn)( tmp_buf, recvbuf, 
                                                count,
                                                datatype,
                                                uop ); 
            }
            else 
#endif
                (*uop)(tmp_buf, recvbuf, &count, &datatype);

            /* change the rank */
            newrank = rank / 2;
        }
    }
    else  /* rank >= 2*rem */
        newrank = rank - rem;
    
    /* for the reduce-scatter, calculate the count that
       each process receives and the displacement within
       the buffer */

    /* We allocate these arrays on all processes, even if newrank=-1,
       because if root is one of the excluded processes, we will
       need them on the root later on below. */
    MPIU_CHKLMEM_MALLOC(cnts, int *, pof2*sizeof(int), mpi_errno, "counts");
    MPIU_CHKLMEM_MALLOC(disps, int *, pof2*sizeof(int), mpi_errno, "displacements");
    
    if (newrank != -1) {
        for (i=0; i<(pof2-1); i++) 
            cnts[i] = count/pof2;
        cnts[pof2-1] = count - (count/pof2)*(pof2-1);
        
        disps[0] = 0;
        for (i=1; i<pof2; i++)
            disps[i] = disps[i-1] + cnts[i-1];
        
        mask = 0x1;
        send_idx = recv_idx = 0;
        last_idx = pof2;
        while (mask < pof2) {
            newdst = newrank ^ mask;
            /* find real rank of dest */
            dst = (newdst < rem) ? newdst*2 : newdst + rem;
            
            send_cnt = recv_cnt = 0;
            if (newrank < newdst) {
                send_idx = recv_idx + pof2/(mask*2);
                for (i=send_idx; i<last_idx; i++)
                    send_cnt += cnts[i];
                for (i=recv_idx; i<send_idx; i++)
                    recv_cnt += cnts[i];
            }
            else {
                recv_idx = send_idx + pof2/(mask*2);
                for (i=send_idx; i<recv_idx; i++)
                    send_cnt += cnts[i];
                for (i=recv_idx; i<last_idx; i++)
                    recv_cnt += cnts[i];
            }
            
/*                    printf("Rank %d, send_idx %d, recv_idx %d, send_cnt %d, recv_cnt %d, last_idx %d\n", newrank, send_idx, recv_idx,
                  send_cnt, recv_cnt, last_idx);
*/
            /* Send data from recvbuf. Recv into tmp_buf */ 
            mpi_errno = MPIC_Sendrecv_ft((char *) recvbuf +
                                         disps[send_idx]*extent,
                                         send_cnt, datatype,
                                         dst, MPIR_REDUCE_TAG,
                                         (char *) tmp_buf +
                                         disps[recv_idx]*extent,
                                         recv_cnt, datatype, dst,
                                         MPIR_REDUCE_TAG, comm,
                                         MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            
            /* tmp_buf contains data received in this step.
               recvbuf contains data accumulated so far */
            
            /* This algorithm is used only for predefined ops
               and predefined ops are always commutative. */
#ifdef HAVE_CXX_BINDING
            if (is_cxx_uop) {
                (*MPIR_Process.cxx_call_op_fn)((char *) tmp_buf +
                                               disps[recv_idx]*extent,
                                               (char *) recvbuf + 
                                               disps[recv_idx]*extent, 
                                               recv_cnt, datatype, uop);
            }
            else 
#endif
                (*uop)((char *) tmp_buf + disps[recv_idx]*extent,
                       (char *) recvbuf + disps[recv_idx]*extent, 
                       &recv_cnt, &datatype);
            
            /* update send_idx for next iteration */
            send_idx = recv_idx;
            mask <<= 1;

            /* update last_idx, but not in last iteration
               because the value is needed in the gather
               step below. */
            if (mask < pof2)
                last_idx = recv_idx + pof2/mask;
        }
    }

    /* now do the gather to root */
    
    /* Is root one of the processes that was excluded from the
       computation above? If so, send data from newrank=0 to
       the root and have root take on the role of newrank = 0 */ 

    if (root < 2*rem) {
        if (root % 2 != 0) {
            if (rank == root) {    /* recv */
                /* initialize the arrays that weren't initialized */
                for (i=0; i<(pof2-1); i++) 
                    cnts[i] = count/pof2;
                cnts[pof2-1] = count - (count/pof2)*(pof2-1);
                
                disps[0] = 0;
                for (i=1; i<pof2; i++)
                    disps[i] = disps[i-1] + cnts[i-1];
                
                mpi_errno = MPIC_Recv_ft(recvbuf, cnts[0], datatype,
                                         0, MPIR_REDUCE_TAG, comm,
                                         MPI_STATUS_IGNORE, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                newrank = 0;
                send_idx = 0;
                last_idx = 2;
            }
            else if (newrank == 0) {  /* send */
                mpi_errno = MPIC_Send_ft(recvbuf, cnts[0], datatype,
                                         root, MPIR_REDUCE_TAG, comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                newrank = -1;
            }
            newroot = 0;
        }
        else newroot = root / 2;
    }
    else
        newroot = root - rem;

    if (newrank != -1) {
        j = 0;
        mask = 0x1;
        while (mask < pof2) {
            mask <<= 1;
            j++;
        }
        mask >>= 1;
        j--;
        while (mask > 0) {
            newdst = newrank ^ mask;

            /* find real rank of dest */
            dst = (newdst < rem) ? newdst*2 : newdst + rem;
            /* if root is playing the role of newdst=0, adjust for
               it */
            if ((newdst == 0) && (root < 2*rem) && (root % 2 != 0))
                dst = root;
            
            /* if the root of newdst's half of the tree is the
               same as the root of newroot's half of the tree, send to
               newdst and exit, else receive from newdst. */

            newdst_tree_root = newdst >> j;
            newdst_tree_root <<= j;
            
            newroot_tree_root = newroot >> j;
            newroot_tree_root <<= j;

            send_cnt = recv_cnt = 0;
            if (newrank < newdst) {
                /* update last_idx except on first iteration */
                if (mask != pof2/2)
                    last_idx = last_idx + pof2/(mask*2);
                
                recv_idx = send_idx + pof2/(mask*2);
                for (i=send_idx; i<recv_idx; i++)
                    send_cnt += cnts[i];
                for (i=recv_idx; i<last_idx; i++)
                    recv_cnt += cnts[i];
            }
            else {
                recv_idx = send_idx - pof2/(mask*2);
                for (i=send_idx; i<last_idx; i++)
                    send_cnt += cnts[i];
                for (i=recv_idx; i<send_idx; i++)
                    recv_cnt += cnts[i];
            }
            
            if (newdst_tree_root == newroot_tree_root) {
                /* send and exit */
                /* printf("Rank %d, send_idx %d, send_cnt %d, last_idx %d\n", newrank, send_idx, send_cnt, last_idx);
                   fflush(stdout); */
                /* Send data from recvbuf. Recv into tmp_buf */ 
                mpi_errno = MPIC_Send_ft((char *) recvbuf +
                                         disps[send_idx]*extent,
                                         send_cnt, datatype,
                                         dst, MPIR_REDUCE_TAG,
                                         comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                break;
            }
            else {
                /* recv and continue */
                /* printf("Rank %d, recv_idx %d, recv_cnt %d, last_idx %d\n", newrank, recv_idx, recv_cnt, last_idx);
                   fflush(stdout); */
                mpi_errno = MPIC_Recv_ft((char *) recvbuf +
                                         disps[recv_idx]*extent,
                                         recv_cnt, datatype, dst,
                                         MPIR_REDUCE_TAG, comm,
                                         MPI_STATUS_IGNORE, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
            
            if (newrank > newdst) send_idx = recv_idx;
            
            mask >>= 1;
            j--;
        }
    }

    /* FIXME does this need to be checked after each uop invocation for
       predefined operators? */
    /* --BEGIN ERROR HANDLING-- */
    if (MPIU_THREADPRIV_FIELD(op_errno)) {
        mpi_errno = MPIU_THREADPRIV_FIELD(op_errno);
        goto fn_fail;
    }
    /* --END ERROR HANDLING-- */

fn_exit:
    MPIU_CHKLMEM_FREEALL();
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
fn_fail:
    goto fn_exit;
}

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#undef FUNCNAME
#define FUNCNAME MPIR_Shmem_Reduce_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Reduce_shmem_MV2 (
    void *sendbuf,
    void *recvbuf,
    int count,
    MPI_Datatype datatype,
    MPI_Op op,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int i,stride, local_rank, local_size, shmem_comm_rank; 
    MPI_User_function *uop; 
    MPID_Op *op_ptr;
    char* shmem_buf = NULL;
    void* local_buf = NULL;
    MPI_Aint   true_lb, true_extent, extent;
#ifdef HAVE_CXX_BINDING
    int is_cxx_uop = 0;
#endif

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
    stride = count*MPIR_MAX(extent,true_extent);
    
    local_rank       = comm_ptr->rank;
    local_size       = comm_ptr->local_size; 
    shmem_comm_rank  = comm_ptr->ch.shmem_comm_rank;

    if(sendbuf != MPI_IN_PLACE && local_rank == 0) { 
        mpi_errno = MPIR_Localcopy(sendbuf, count, datatype, recvbuf, count,
                                               datatype);
        if (mpi_errno) {
             MPIU_ERR_POP(mpi_errno); 
        }
    } 

    if(local_size == 0) { 
         /* Only one process. So, return */
         goto fn_exit; 
    } 
 
    /* Get the operator and check whether it is commutative or not */
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        /* get the function by indexing into the op table */
        uop = MPIR_Op_table[op%16 - 1];
    } else {
        MPID_Op_get_ptr(op, op_ptr);
#if defined(HAVE_CXX_BINDING)
        if (op_ptr->language == MPID_LANG_CXX) {
               uop = (MPI_User_function *) op_ptr->function.c_function;
               is_cxx_uop = 1;
        } else {
#endif /* defined(HAVE_CXX_BINDING) */
            if ((op_ptr->language == MPID_LANG_C)) {
                uop = (MPI_User_function *) op_ptr->function.c_function;
            } else  {
                uop = (MPI_User_function *) op_ptr->function.f77_function;
            }
#if defined(HAVE_CXX_BINDING)
        }
#endif /* defined(HAVE_CXX_BINDING) */
    }
 
    if(local_rank == 0) { 
           MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                     shmem_comm_rank, (void *)&shmem_buf);
           for (i = 1; i < local_size; i++) {
                local_buf = (char*)shmem_buf + stride*i;
#if defined(HAVE_CXX_BINDING)
                if (is_cxx_uop)  { 
                     (*MPIR_Process.cxx_call_op_fn)( local_buf, recvbuf,
                               count, datatype, uop );
                } else { 
#endif /* defined(HAVE_CXX_BINDING) */
                     (*uop)(local_buf, recvbuf, &count, &datatype);
#if defined(HAVE_CXX_BINDING)
                }
#endif 
            } 
            MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size,
                               local_rank, shmem_comm_rank);
    } else {
            MPIDI_CH3I_SHMEM_COLL_GetShmemBuf(local_size, local_rank,
                                      shmem_comm_rank, (void *)&shmem_buf);
            local_buf = (char*)shmem_buf + stride*local_rank;
            mpi_errno = MPIR_Localcopy(sendbuf, count, datatype,
                                       local_buf, count, datatype);
            if (mpi_errno) { 
                   MPIU_ERR_POP(mpi_errno); 
            }
            MPIDI_CH3I_SHMEM_COLL_SetGatherComplete(local_size, local_rank,
                                shmem_comm_rank);
    }


fn_exit:
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
fn_fail:
    goto fn_exit;


}


#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_two_level_helper_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Reduce_two_level_helper_MV2 (
    void *sendbuf,
    void *recvbuf,
    int count,
    MPI_Datatype datatype,
    MPI_Op op,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int mpi_errno = MPI_SUCCESS; 
    int mpi_errno_ret = MPI_SUCCESS;
    int my_rank, total_size, local_rank, local_size; 
    int leader_comm_rank=-1, leader_comm_size=0; 
    MPI_Comm comm, shmem_comm, leader_comm; 
    int leader_root, leader_of_root; 
    MPID_Comm *shmem_commptr=NULL, *leader_commptr=NULL; 
    void *in_buf=NULL, *out_buf=NULL, *tmp_buf = NULL; 
    MPI_Aint true_lb, true_extent, extent; 
    int type_size;
    MPID_Op *op_ptr;
    int is_commutative=0, stride=0; 
    MPIU_CHKLMEM_DECL(1);
    MPID_Datatype_get_size_macro(datatype, type_size);

    my_rank    = comm_ptr->rank;
    total_size = comm_ptr->local_size; 
    comm       = comm_ptr->handle; 
    shmem_comm = comm_ptr->ch.shmem_comm;

    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);
    local_rank      = shmem_commptr->rank; 
    local_size      = shmem_commptr->local_size; 
    
    leader_of_root = comm_ptr->ch.leader_map[root];
    leader_root = comm_ptr->ch.leader_rank[leader_of_root];

    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
    } else {
        MPID_Op_get_ptr(op, op_ptr)
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else  {
            is_commutative = 1;
        }
    }

    MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);
    MPID_Datatype_get_extent_macro(datatype, extent);
    stride = count*MPIR_MAX(extent,true_extent);

    if(local_size == total_size) {
            /* First handle the case where there is only one node */
            if(comm_ptr->ch.shmem_coll_ok == 1 &&
               stride  <= coll_param.shmem_intra_reduce_msg &&
               disable_shmem_reduce == 0 &&
               is_commutative == 1) { 
                   out_buf = recvbuf; 
                   if(sendbuf != MPI_IN_PLACE) { 
                        in_buf = sendbuf; 
                   } else { 
                        in_buf = recvbuf;
                   } 
                   if(local_rank == 0 && my_rank != root) { 
                        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*
                                                  (MPIR_MAX(extent,true_extent)),
                                                  mpi_errno, "receive buffer");
                        tmp_buf = (void *)((char*)tmp_buf - true_lb);
                        out_buf = tmp_buf; 
                   }
                   mpi_errno = MPIR_Reduce_shmem_MV2(in_buf, out_buf, count, 
                                                  datatype, op,
                                                  0, shmem_commptr, errflag);
                   if (local_rank == 0 && root != my_rank) {
                         mpi_errno  = MPIC_Send_ft( out_buf, count, datatype, root,
                                                  MPIR_REDUCE_TAG, comm,
                                                  errflag );
                   }
                   if ((local_rank != 0) && (root == my_rank)) {
                         mpi_errno = MPIC_Recv_ft (recvbuf, count, datatype,
                                                  leader_of_root, MPIR_REDUCE_TAG, comm,
                                                  MPI_STATUS_IGNORE, errflag);
                   }
            } else {  
                  mpi_errno = MPIR_Reduce_binomial_MV2(sendbuf, recvbuf, count,
                                 datatype, op,
                                 root, comm_ptr, errflag);
            } 
            if (mpi_errno) {
                  /* for communication errors, just record the error but
                  * continue */
                  *errflag = TRUE;
                  MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                  MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            /* We are done */
            goto fn_exit;
    }
    
    if(local_rank == 0) {
        leader_comm       =  comm_ptr->ch.leader_comm;
        MPID_Comm_get_ptr(leader_comm, leader_commptr);
        leader_comm_rank  =  leader_commptr->rank; 
        leader_comm_size  =  leader_commptr->local_size; 
        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, count*
                                  (MPIR_MAX(extent,true_extent)),
                                  mpi_errno, "receive buffer");
        tmp_buf = (void *)((char*)tmp_buf - true_lb);
    }


    /*Fix the input and outbuf buffers for the intra-node reduce.
     *Node leaders will have the reduced data in tmp_buf after 
     *this step*/
    if(sendbuf != MPI_IN_PLACE) {
         in_buf = sendbuf;
    } else {
         in_buf = recvbuf;
    } 
    if(local_rank == 0) {
         out_buf  = tmp_buf; 
    } else {
         out_buf = NULL; 
    }


    /* Lets first do the intra-node reduce operations */
    if(comm_ptr->ch.shmem_coll_ok == 1 && 
      stride  <= coll_param.shmem_reduce_msg && 
      disable_shmem_reduce == 0 &&
      is_commutative == 1 ) {
          mpi_errno = MPIR_Reduce_shmem_MV2(in_buf, out_buf, count, 
                         datatype, op,
                         0, shmem_commptr, errflag);
    } else {  
          mpi_errno = MPIR_Reduce_binomial_MV2(in_buf, out_buf, count, 
                         datatype, op,
                         0, shmem_commptr, errflag);
    }
    if (mpi_errno) {
         /* for communication errors, just record the error but
          * continue */
           *errflag = TRUE;
           MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
           MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    /* Now work on the inter-leader phase. Data is in tmp_buf */
    if(local_rank == 0 && leader_comm_size > 1) {
            /*The leader of root will have the global reduced data in tmp_buf 
              or recv_buf
              at the end of the reduce */
             if(leader_comm_rank == leader_root) {
                   if(my_rank == root) { 
                       /* I am the root of the leader-comm, and the 
                        * root of the reduce op. So, I will write the 
                        * final result directly into my recvbuf */
                       in_buf  = tmp_buf; 
                       out_buf = recvbuf; 
                   } else { 
                       in_buf   = MPI_IN_PLACE; 
                       out_buf  = tmp_buf;
                   } 
             } else  {
                   in_buf  = tmp_buf; 
                   out_buf = NULL;
             }

            
            if(count*type_size  <= coll_param.reduce_short_msg) {
                 mpi_errno = MPIR_Reduce_binomial_MV2(in_buf, out_buf, count,
                                          datatype, op,
                                          leader_root, leader_commptr,errflag);
            } else { 
                 mpi_errno = MPIR_Reduce_redscat_gather_MV2(in_buf, out_buf, count,
                                          datatype, op,
                                          leader_root, leader_commptr,errflag);
            } 
            if (mpi_errno) {
                   /* for communication errors, just record the error
                    * but continue */
                    *errflag = TRUE;
                     MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                     MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
    } 

    if (local_size > 1) {
    /* Send the message to the root if the leader is not the
     * root of the reduce operation. The reduced data is in tmp_buf */
        if ((local_rank == 0) && (root != my_rank)
             && (leader_root == leader_comm_rank)) {
               mpi_errno  = MPIC_Send_ft( tmp_buf, count, datatype, root,
                                          MPIR_REDUCE_TAG, comm,
                                          errflag );
               if (mpi_errno) {
                   /* for communication errors, just record the error 
                    * but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
               }
        }

        if ((local_rank != 0) && (root == my_rank)) {
             mpi_errno = MPIC_Recv_ft (recvbuf, count, datatype,
                                   leader_of_root,
                                   MPIR_REDUCE_TAG, comm, 
                                   MPI_STATUS_IGNORE, errflag);
             if (mpi_errno) {
                  /* for communication errors, just record the error but
                  * continue */
                  *errflag = TRUE;
                  MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                  MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
             }
         }
    } 

fn_exit:
    MPIU_CHKLMEM_FREEALL();
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
fn_fail:
    goto fn_exit;
}
#endif /*  #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */





/* This is the default implementation of reduce. The algorithm is:
   
   Algorithm: MPI_Reduce

   For long messages and for builtin ops and if count >= pof2 (where
   pof2 is the nearest power-of-two less than or equal to the number
   of processes), we use Rabenseifner's algorithm (see 
   http://www.hlrs.de/organization/par/services/models/mpi/myreduce.html ).
   This algorithm implements the reduce in two steps: first a
   reduce-scatter, followed by a gather to the root. A
   recursive-halving algorithm (beginning with processes that are
   distance 1 apart) is used for the reduce-scatter, and a binomial tree
   algorithm is used for the gather. The non-power-of-two case is
   handled by dropping to the nearest lower power-of-two: the first
   few odd-numbered processes send their data to their left neighbors
   (rank-1), and the reduce-scatter happens among the remaining
   power-of-two processes. If the root is one of the excluded
   processes, then after the reduce-scatter, rank 0 sends its result to
   the root and exits; the root now acts as rank 0 in the binomial tree
   algorithm for gather.

   For the power-of-two case, the cost for the reduce-scatter is 
   lgp.alpha + n.((p-1)/p).beta + n.((p-1)/p).gamma. The cost for the
   gather to root is lgp.alpha + n.((p-1)/p).beta. Therefore, the
   total cost is:
   Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta + n.((p-1)/p).gamma

   For the non-power-of-two case, assuming the root is not one of the
   odd-numbered processes that get excluded in the reduce-scatter,
   Cost = (2.floor(lgp)+1).alpha + (2.((p-1)/p) + 1).n.beta + 
           n.(1+(p-1)/p).gamma


   For short messages, user-defined ops, and count < pof2, we use a
   binomial tree algorithm for both short and long messages. 

   Cost = lgp.alpha + n.lgp.beta + n.lgp.gamma


   We use the binomial tree algorithm in the case of user-defined ops
   because in this case derived datatypes are allowed, and the user
   could pass basic datatypes on one process and derived on another as
   long as the type maps are the same. Breaking up derived datatypes
   to do the reduce-scatter is tricky.

   FIXME: Per the MPI-2.1 standard this case is not possible.  We
   should be able to use the reduce-scatter/gather approach as long as
   count >= pof2.  [goodell@ 2009-01-21]

   Possible improvements: 

   End Algorithm: MPI_Reduce
*/


/* not declared static because a machine-specific function may call this one 
   in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Reduce_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Reduce_MV2 ( 
    void *sendbuf, 
    void *recvbuf, 
    int count, 
    MPI_Datatype datatype, 
    MPI_Op op, 
    int root, 
    MPID_Comm *comm_ptr,
    int *errflag )
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int is_commutative;
    MPID_Op *op_ptr;
#endif /*  #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    int comm_size, type_size, pof2;
    if (count == 0) return MPI_SUCCESS;
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );

    comm_size = comm_ptr->local_size;
    
    MPID_Datatype_get_size_macro(datatype, type_size);
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) {
        is_commutative = 1;
        /* get the function by indexing into the op table */
    } else {
        MPID_Op_get_ptr(op, op_ptr)
        if (op_ptr->kind == MPID_OP_USER_NONCOMMUTE) {
            is_commutative = 0;
        } else  {
            is_commutative = 1;
        }
    }
#endif /*  #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */


    /* find nearest power-of-two less than or equal to comm_size */
    pof2 = 1;
    while (pof2 <= comm_size) pof2 <<= 1;
    pof2 >>=1;

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    if(comm_ptr->ch.shmem_coll_ok == 1  
       && is_commutative == 1
       && count*type_size < coll_param.reduce_2level_threshold) {
             mpi_errno = MPIR_Reduce_two_level_helper_MV2(sendbuf, recvbuf, count, 
                                                       datatype, op, 
                                                       root, comm_ptr, errflag);
             if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
             }
    } else
#endif /*  #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    { 
            if ((count*type_size > MPIR_PARAM_REDUCE_SHORT_MSG_SIZE) &&
                (HANDLE_GET_KIND(op) == HANDLE_KIND_BUILTIN) && (count >= pof2)) {
                /* do a reduce-scatter followed by gather to root. */
                mpi_errno = MPIR_Reduce_redscat_gather_MV2(sendbuf, recvbuf, count, 
                                                       datatype, op, 
                                                       root, comm_ptr, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
            else {
                /* use a binomial tree algorithm */ 
                mpi_errno = MPIR_Reduce_binomial_MV2(sendbuf, recvbuf, count, 
                                                 datatype, op, 
                                                 root, comm_ptr, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
    } 
        

	/* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
}


