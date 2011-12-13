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
#include <unistd.h>
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#include "coll_shmem.h"
#endif /* defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
#include <unistd.h>

int MPIR_Bcast_intra_MV2(void *, int, MPI_Datatype, int, MPID_Comm *, int *); 

/* Not PMPI_LOCAL because it is called in intercomm allgather */
#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_inter_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Bcast_inter_MV2 (
    void *buffer,
    int count,
    MPI_Datatype datatype,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag)
{
/*  Intercommunicator broadcast.
    Root sends to rank 0 in remote group. Remote group does local
    intracommunicator broadcast.
*/
    int rank, mpi_errno;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Status status;
    MPID_Comm *newcomm_ptr = NULL;
    MPI_Comm comm;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIR_BCAST_INTER);

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIR_BCAST_INTER);

    comm = comm_ptr->handle;

    if (root == MPI_PROC_NULL)
    {
        /* local processes other than root do nothing */
        mpi_errno = MPI_SUCCESS;
    }
    else if (root == MPI_ROOT)
    {
        /* root sends to rank 0 on remote group and returns */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
        mpi_errno =  MPIC_Send_ft(buffer, count, datatype, 0,
                                  MPIR_BCAST_TAG, comm, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT( comm_ptr );
    }
    else
    {
        /* remote group. rank 0 on remote group receives from root */

       rank = comm_ptr->rank;

        if (rank == 0)
        {
            mpi_errno = MPIC_Recv_ft(buffer, count, datatype, root,
                                     MPIR_BCAST_TAG, comm, &status, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

        /* Get the local intracommunicator */
        if (!comm_ptr->local_comm) { 
            MPIR_Setup_intercomm_localcomm( comm_ptr );
        } 

        newcomm_ptr = comm_ptr->local_comm;

        /* now do the usual broadcast on this intracommunicator
           with rank 0 as root. */
        mpi_errno = MPIR_Bcast_intra_MV2(buffer, count, datatype, 0, newcomm_ptr, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

    MPID_MPI_FUNC_EXIT(MPID_STATE_MPIR_BCAST_INTER);
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
}

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)

/* A binomial tree broadcast algorithm.  Good for short messages, 
   Cost = lgp.alpha + n.lgp.beta */
#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_binomial_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Bcast_binomial_MV2(
    void *buffer,
    int count,
    MPI_Datatype datatype,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag)
{
    int        rank, comm_size, src, dst;
    int        relative_rank, mask;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int nbytes=0;
    int type_size, is_contig, is_homogeneous;
    int position;
    void *tmp_buf=NULL;
    MPI_Comm comm;
    MPID_Datatype *dtp;
    MPIU_CHKLMEM_DECL(1);

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* If there is only one process, return */
    if (comm_size == 1) goto fn_exit;

    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN)
        is_contig = 1;
    else {
        MPID_Datatype_get_ptr(datatype, dtp);
        is_contig = dtp->is_contig;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    /* MPI_Type_size() might not give the accurate size of the packed
     * datatype for heterogeneous systems (because of padding, encoding,
     * etc). On the other hand, MPI_Pack_size() can become very
     * expensive, depending on the implementation, especially for
     * heterogeneous systems. We want to use MPI_Type_size() wherever
     * possible, and MPI_Pack_size() in other places.
     */
    if (is_homogeneous)
        MPID_Datatype_get_size_macro(datatype, type_size);
    else
        MPIR_Pack_size_impl(1, datatype, &type_size);

    nbytes = type_size * count;

    if (!is_contig || !is_homogeneous)
    {
        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, nbytes, mpi_errno, "tmp_buf");

        /* TODO: Pipeline the packing and communication */
        position = 0;
        if (rank == root) {
            mpi_errno = MPIR_Pack_impl(buffer, count, datatype, tmp_buf, nbytes,
                                       &position);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }

    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    /* Use short message algorithm, namely, binomial tree */

    /* Algorithm:
       This uses a fairly basic recursive subdivision algorithm.
       The root sends to the process comm_size/2 away; the receiver becomes
       a root for a subtree and applies the same process. 

       So that the new root can easily identify the size of its
       subtree, the (subtree) roots are all powers of two (relative
       to the root) If m = the first power of 2 such that 2^m >= the
       size of the communicator, then the subtree at root at 2^(m-k)
       has size 2^k (with special handling for subtrees that aren't
       a power of two in size).

       Do subdivision.  There are two phases:
       1. Wait for arrival of data.  Because of the power of two nature
       of the subtree roots, the source of this message is alwyas the
       process whose relative rank has the least significant 1 bit CLEARED.
       That is, process 4 (100) receives from process 0, process 7 (111) 
       from process 6 (110), etc.   
       2. Forward to my subtree

       Note that the process that is the tree root is handled automatically
       by this code, since it has no bits set.  */

    mask = 0x1;
    while (mask < comm_size)
    {
        if (relative_rank & mask)
        {
            src = rank - mask;
            if (src < 0) src += comm_size;
            if (!is_contig || !is_homogeneous)
                mpi_errno = MPIC_Recv_ft(tmp_buf,nbytes,MPI_BYTE,src,
                                         MPIR_BCAST_TAG,comm,MPI_STATUS_IGNORE, 
                                         errflag);
            else
                mpi_errno = MPIC_Recv_ft(buffer,count,datatype,src,
                                         MPIR_BCAST_TAG,comm,MPI_STATUS_IGNORE,
                                         errflag);
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

    /* This process is responsible for all processes that have bits
       set from the LSB upto (but not including) mask.  Because of
       the "not including", we start by shifting mask back down one.

       We can easily change to a different algorithm at any power of two
       by changing the test (mask > 1) to (mask > block_size) 

       One such version would use non-blocking operations for the last 2-4
       steps (this also bounds the number of MPI_Requests that would
       be needed).  */

    mask >>= 1;
    while (mask > 0)
    {
        if (relative_rank + mask < comm_size)
        {
            dst = rank + mask;
            if (dst >= comm_size) dst -= comm_size;
            if (!is_contig || !is_homogeneous)
                mpi_errno = MPIC_Send_ft(tmp_buf,nbytes,MPI_BYTE,dst,
                                         MPIR_BCAST_TAG,comm, errflag);
            else
                mpi_errno = MPIC_Send_ft(buffer,count,datatype,dst,
                                         MPIR_BCAST_TAG,comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
        mask >>= 1;
    }

    if (!is_contig || !is_homogeneous)
    {
        if (rank != root)
        {
            position = 0;
            mpi_errno = MPIR_Unpack_impl(tmp_buf, nbytes, &position, buffer,
                                         count, datatype);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);

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

/* FIXME it would be nice if we could refactor things to minimize
   duplication between this and MPIR_Scatter_intra and friends.  We can't use
   MPIR_Scatter_intra as is without inducing an extra copy in the noncontig case. */
/* There are additional arguments included here that are unused because we
   always assume that the noncontig case has been packed into a contig case by
   the caller for now.  Once we start handling noncontig data at the upper level
   we can start handling it here.
   
   At the moment this function always scatters a buffer of nbytes starting at
   tmp_buf address. */
#undef FUNCNAME
#define FUNCNAME scatter_for_bcast_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int scatter_for_bcast_MV2(
    void *buffer ATTRIBUTE((unused)),
    int count ATTRIBUTE((unused)),
    MPI_Datatype datatype ATTRIBUTE((unused)),
    int root,
    MPID_Comm *comm_ptr,
    int nbytes,
    void *tmp_buf,
    int is_contig,
    int is_homogeneous,
    int *errflag)
{
    MPI_Status status;
    int        rank, comm_size, src, dst;
    int        relative_rank, mask;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int scatter_size, curr_size, recv_size = 0, send_size;
    MPI_Comm comm;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    /* use long message algorithm: binomial tree scatter followed by an
 * allgather */

    /* The scatter algorithm divides the buffer into nprocs pieces and
       scatters them among the processes. Root gets the first piece,
       root+1 gets the second piece, and so forth. Uses the same binomial
       tree algorithm as above. Ceiling division
       is used to compute the size of each piece. This means some
       processes may not get any data. For example if bufsize = 97 and
       nprocs = 16, ranks 15 and 16 will get 0 data. On each process, the
       scattered data is stored at the same offset in the buffer as it is
       on the root process. */

    scatter_size = (nbytes + comm_size - 1)/comm_size; /* ceiling division */
    curr_size = (rank == root) ? nbytes : 0; /* root starts with all the
                                                data */

    mask = 0x1;
    while (mask < comm_size)
    {
        if (relative_rank & mask)
        {
            src = rank - mask;
            if (src < 0) src += comm_size;
            recv_size = nbytes - relative_rank*scatter_size;
            /* recv_size is larger than what might actually be sent by the
               sender. We don't need compute the exact value because MPI
               allows you to post a larger recv.*/
            if (recv_size <= 0)
            {
                curr_size = 0; /* this process doesn't receive any data
                                  because of uneven division */
            }
            else
            {
                mpi_errno = MPIC_Recv_ft(((char *)tmp_buf +
                                          relative_rank*scatter_size),
                                         recv_size, MPI_BYTE, src,
                                         MPIR_BCAST_TAG, comm, &status,
                                         errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    curr_size = 0;
                } else
                    /* query actual size of data received */
                    MPIR_Get_count_impl(&status, MPI_BYTE, &curr_size);
            }
            break;
        }
        mask <<= 1;
    }

    /* This process is responsible for all processes that have bits
       set from the LSB upto (but not including) mask.  Because of
       the "not including", we start by shifting mask back down
       one. */

    mask >>= 1;
    while (mask > 0)
    {
        if (relative_rank + mask < comm_size)
        {
            send_size = curr_size - scatter_size * mask;
            /* mask is also the size of this process's subtree */

            if (send_size > 0)
            {
                dst = rank + mask;
                if (dst >= comm_size) dst -= comm_size;
                mpi_errno = MPIC_Send_ft(((char *)tmp_buf +
                                          scatter_size*(relative_rank+mask)),
                                         send_size, MPI_BYTE, dst,
                                         MPIR_BCAST_TAG, comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but
 * continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                curr_size -= send_size;
            }
        }
        mask >>= 1;
    }

    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
}

/*
   Broadcast based on a scatter followed by an allgather.

   We first scatter the buffer using a binomial tree algorithm. This costs
   lgp.alpha + n.((p-1)/p).beta
   If the datatype is contiguous and the communicator is homogeneous,
   we treat the data as bytes and divide (scatter) it among processes
   by using ceiling division. For the noncontiguous or heterogeneous
   cases, we first pack the data into a temporary buffer by using
   MPI_Pack, scatter it as bytes, and unpack it after the allgather.

   For the allgather, we use a recursive doubling algorithm for 
   medium-size messages and power-of-two number of processes. This
   takes lgp steps. In each step pairs of processes exchange all the
   data they have (we take care of non-power-of-two situations). This
   costs approximately lgp.alpha + n.((p-1)/p).beta. (Approximately
   because it may be slightly more in the non-power-of-two case, but
   it's still a logarithmic algorithm.) Therefore, for long messages
   Total Cost = 2.lgp.alpha + 2.n.((p-1)/p).beta
*/

#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_scatter_doubling_allgather_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Bcast_scatter_doubling_allgather_MV2(
    void *buffer,
    int count,
    MPI_Datatype datatype,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag)
{
    MPI_Status status;
    int rank, comm_size, dst;
    int relative_rank, mask;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int scatter_size, nbytes=0, curr_size, recv_size = 0;
    int type_size, j, k, i, tmp_mask, is_contig, is_homogeneous;
    int relative_dst, dst_tree_root, my_tree_root, send_offset;
    int recv_offset, tree_root, nprocs_completed, offset, position;
    MPIU_CHKLMEM_DECL(1);
    MPI_Comm comm;
    MPID_Datatype *dtp;
    MPI_Aint true_extent, true_lb;
    void *tmp_buf;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;
    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    /* If there is only one process, return */
    if (comm_size == 1) goto fn_exit;

    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN)
        is_contig = 1;
    else {
        MPID_Datatype_get_ptr(datatype, dtp);
        is_contig = dtp->is_contig;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    /* MPI_Type_size() might not give the accurate size of the packed
     * datatype for heterogeneous systems (because of padding, encoding,
     * etc). On the other hand, MPI_Pack_size() can become very
     * expensive, depending on the implementation, especially for
     * heterogeneous systems. We want to use MPI_Type_size() wherever
     * possible, and MPI_Pack_size() in other places.
     */
    if (is_homogeneous)
        MPID_Datatype_get_size_macro(datatype, type_size);
    else
        MPIR_Pack_size_impl(1, datatype, &type_size);

    nbytes = type_size * count;

    if (is_contig && is_homogeneous)
    {
        /* contiguous and homogeneous. no need to pack. */
        MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

        tmp_buf = (char *) buffer + true_lb;
    }
    else
    {
        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, nbytes, mpi_errno, "tmp_buf");

        /* TODO: Pipeline the packing and communication */
        position = 0;
        if (rank == root) {
            mpi_errno = MPIR_Pack_impl(buffer, count, datatype, tmp_buf, nbytes,
                                       &position);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }


    scatter_size = (nbytes + comm_size - 1)/comm_size; /* ceiling division */
    curr_size = (rank == root) ? nbytes : 0; /* root starts with all the
                                                data */


    mpi_errno = scatter_for_bcast_MV2(buffer, count, datatype, root, comm_ptr,
                                  nbytes, tmp_buf, is_contig, is_homogeneous,
                                  errflag);
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    /* medium size allgather and pof2 comm_size. use recurive doubling. */

    mask = 0x1;
    i = 0;
    while (mask < comm_size)
    {
        relative_dst = relative_rank ^ mask;

        dst = (relative_dst + root) % comm_size;

        /* find offset into send and recv buffers.
           zero out the least significant "i" bits of relative_rank and
           relative_dst to find root of src and dst
           subtrees. Use ranks of roots as index to send from
           and recv into  buffer */

        dst_tree_root = relative_dst >> i;
        dst_tree_root <<= i;

        my_tree_root = relative_rank >> i;
        my_tree_root <<= i;

        send_offset = my_tree_root * scatter_size;
        recv_offset = dst_tree_root * scatter_size;

        if (relative_dst < comm_size)
        {
            mpi_errno = MPIC_Sendrecv_ft(((char *)tmp_buf + send_offset),
                                         curr_size, MPI_BYTE, dst,
                                          MPIR_BCAST_TAG,
                                         ((char *)tmp_buf + recv_offset),
                                         (nbytes-recv_offset < 0 ? 0 : nbytes-recv_offset),
                                         MPI_BYTE, dst, MPIR_BCAST_TAG, comm,
                                         &status, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                recv_size = 0;
            } else
                MPIR_Get_count_impl(&status, MPI_BYTE, &recv_size);
            curr_size += recv_size;
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
           so that it doesn't show up as red in the coverage tests. */

        /* --BEGIN EXPERIMENTAL-- */
        if (dst_tree_root + mask > comm_size)
        {
            nprocs_completed = comm_size - my_tree_root - mask;
            /* nprocs_completed is the number of processes in this
               subtree that have all the data. Send data to others
               in a tree fashion. First find root of current tree
               that is being divided into two. k is the number of
               least-significant bits in this process's rank that
               must be zeroed out to find the rank of the root */
            j = mask;
            k = 0;
            while (j)
            {
                j >>= 1;
                k++;
            }
            k--;

            offset = scatter_size * (my_tree_root + mask);
            tmp_mask = mask >> 1;

            while (tmp_mask)
            {
                relative_dst = relative_rank ^ tmp_mask;
                dst = (relative_dst + root) % comm_size;

                tree_root = relative_rank >> k;
                tree_root <<= k;
                /* send only if this proc has data and destination
                   doesn't have data. */

                if ((relative_dst > relative_rank) &&
                    (relative_rank < tree_root + nprocs_completed)
                    && (relative_dst >= tree_root + nprocs_completed))
                {

                    mpi_errno = MPIC_Send_ft(((char *)tmp_buf + offset),
                                             recv_size, MPI_BYTE, dst,
                                             MPIR_BCAST_TAG, comm, errflag);
                    /* recv_size was set in the previous
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
                else if ((relative_dst < relative_rank) &&
                         (relative_dst < tree_root + nprocs_completed) &&
                         (relative_rank >= tree_root + nprocs_completed))
                {
                    mpi_errno = MPIC_Recv_ft(((char *)tmp_buf + offset),
                                             nbytes - offset,
                                             MPI_BYTE, dst, MPIR_BCAST_TAG,
                                             comm, &status, errflag);
                    /* nprocs_completed is also equal to the no. of processes
                       whose data we don't have */
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        recv_size = 0;
                    } else
                        MPIR_Get_count_impl(&status, MPI_BYTE, &recv_size);
                    curr_size += recv_size;
                }
                tmp_mask >>= 1;
                k--;
            }
        }
        /* --END EXPERIMENTAL-- */

        mask <<= 1;
        i++;
    }

    if (!is_contig || !is_homogeneous)
    {
        if (rank != root)
        {
            position = 0;
            mpi_errno = MPIR_Unpack_impl(tmp_buf, nbytes, &position, buffer,
                                         count, datatype);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
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

/*
   Broadcast based on a scatter followed by an allgather.

   We first scatter the buffer using a binomial tree algorithm. This costs
   lgp.alpha + n.((p-1)/p).beta
   If the datatype is contiguous and the communicator is homogeneous,
   we treat the data as bytes and divide (scatter) it among processes
   by using ceiling division. For the noncontiguous or heterogeneous
   cases, we first pack the data into a temporary buffer by using
   MPI_Pack, scatter it as bytes, and unpack it after the allgather.

   We use a ring algorithm for the allgather, which takes p-1 steps.
   This may perform better than recursive doubling for long messages and
   medium-sized non-power-of-two messages.
   Total Cost = (lgp+p-1).alpha + 2.n.((p-1)/p).beta
*/
#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_scatter_ring_allgather_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Bcast_scatter_ring_allgather_MV2(
    void *buffer,
    int count,
    MPI_Datatype datatype,
    int root,
    MPID_Comm *comm_ptr,
    int *errflag)
{
    int rank, comm_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int scatter_size, nbytes;
    int type_size, j, i, is_contig, is_homogeneous;
    int position;
    int *recvcnts, *displs, left, right, jnext;
    void *tmp_buf;
    MPI_Comm comm;
    MPID_Datatype *dtp;
    MPI_Aint true_extent, true_lb;
    MPIU_CHKLMEM_DECL(3);

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    /* If there is only one process, return */
    if (comm_size == 1) goto fn_exit;

    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN)
        is_contig = 1;
    else {
        MPID_Datatype_get_ptr(datatype, dtp);
        is_contig = dtp->is_contig;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    /* MPI_Type_size() might not give the accurate size of the packed
     * datatype for heterogeneous systems (because of padding, encoding,
     * etc). On the other hand, MPI_Pack_size() can become very
     * expensive, depending on the implementation, especially for
     * heterogeneous systems. We want to use MPI_Type_size() wherever
     * possible, and MPI_Pack_size() in other places.
     */
    if (is_homogeneous)
        MPID_Datatype_get_size_macro(datatype, type_size);
    else
        MPIR_Pack_size_impl(1, datatype, &type_size);

    nbytes = type_size * count;

    if (is_contig && is_homogeneous)
    {
        /* contiguous and homogeneous. no need to pack. */
        MPIR_Type_get_true_extent_impl(datatype, &true_lb, &true_extent);

        tmp_buf = (char *) buffer + true_lb;
    }
    else
    {
        MPIU_CHKLMEM_MALLOC(tmp_buf, void *, nbytes, mpi_errno, "tmp_buf");

        /* TODO: Pipeline the packing and communication */
        position = 0;
        if (rank == root) {
            mpi_errno = MPIR_Pack_impl(buffer, count, datatype, tmp_buf, nbytes,
                                       &position);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
        }
    }

    scatter_size = (nbytes + comm_size - 1)/comm_size; /* ceiling division */

    mpi_errno = scatter_for_bcast_MV2(buffer, count, datatype, root, comm_ptr,
                                  nbytes, tmp_buf, is_contig, is_homogeneous,
                                  errflag);
    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

    /* long-message allgather or medium-size but non-power-of-two. use ring
 * algorithm. */

    MPIU_CHKLMEM_MALLOC(recvcnts, int *, comm_size*sizeof(int), 
                        mpi_errno, "recvcnts");
    MPIU_CHKLMEM_MALLOC(displs,   int *, comm_size*sizeof(int), 
                        mpi_errno, "displs");

    for (i=0; i<comm_size; i++)
    {
        recvcnts[i] = nbytes - i*scatter_size;
        if (recvcnts[i] > scatter_size)
            recvcnts[i] = scatter_size;
        if (recvcnts[i] < 0)
            recvcnts[i] = 0;
    }

    displs[0] = 0;
    for (i=1; i<comm_size; i++)
        displs[i] = displs[i-1] + recvcnts[i-1];

    left  = (comm_size + rank - 1) % comm_size;
    right = (rank + 1) % comm_size;

    j     = rank;
    jnext = left;
    for (i=1; i<comm_size; i++)
    {
        mpi_errno =
            MPIC_Sendrecv_ft((char *)tmp_buf +
                             displs[(j-root+comm_size)%comm_size],
                             recvcnts[(j-root+comm_size)%comm_size],
                             MPI_BYTE, right, MPIR_BCAST_TAG,
                             (char *)tmp_buf +
                             displs[(jnext-root+comm_size)%comm_size],
                             recvcnts[(jnext-root+comm_size)%comm_size],
                             MPI_BYTE, left,
                             MPIR_BCAST_TAG, comm, MPI_STATUS_IGNORE, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }

        j     = jnext;
        jnext = (comm_size + jnext - 1) % comm_size;
    }

    if (!is_contig || !is_homogeneous)
    {
        if (rank != root)
        {
            position = 0;
            mpi_errno = MPIR_Unpack_impl(tmp_buf, nbytes, &position, buffer,
                                         count, datatype);
            if (mpi_errno) MPIU_ERR_POP(mpi_errno);
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

#undef FUNCNAME
#define FUNCNAME MPIR_Shmem_Bcast_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)

int MPIR_Shmem_Bcast_MV2(
    void *buffer, 
    int count, 
    MPI_Datatype datatype, 
    int root, 
    MPID_Comm *shmem_comm_ptr)
{ 
    int mpi_errno = MPI_SUCCESS; 
    int shmem_comm_rank, nbytes, type_size; 
    int local_rank, local_size; 

    MPID_Datatype_get_size_macro(datatype, type_size);
    nbytes = count*type_size; 
    shmem_comm_rank = shmem_comm_ptr->ch.shmem_comm_rank; 
    void *shmem_buf = NULL; 
   
    local_rank = shmem_comm_ptr->rank; 
    local_size = shmem_comm_ptr->local_size; 

    if(local_size == 1 || count == 0) { 
          return MPI_SUCCESS; 
    } 

    if(local_rank == 0) { 
            MPIDI_CH3I_SHMEM_Bcast_GetBuf(local_size, local_rank,
                                      shmem_comm_rank, (void *)&shmem_buf);
            mpi_errno = MPIR_Localcopy(buffer, count, datatype,
                                        shmem_buf, nbytes, MPI_BYTE);
            MPIDI_CH3I_SHMEM_Bcast_Complete(local_size, local_rank, 
                                             shmem_comm_rank); 
    } else { 
            MPIDI_CH3I_SHMEM_Bcast_GetBuf(local_size, local_rank,
                                      shmem_comm_rank, (void *)&shmem_buf);
            mpi_errno = MPIR_Localcopy(shmem_buf, nbytes, MPI_BYTE,
                                        buffer, count, datatype);
            MPIDI_CH3I_SHMEM_Bcast_Complete(local_size, local_rank, 
                                             shmem_comm_rank); 
    }
    if (mpi_errno) { 
        MPIU_ERR_POP(mpi_errno); 
    }  
    fn_fail :
       return mpi_errno;

} 
    

#undef FUNCNAME
#define FUNCNAME MPIR_Knomial_Bcast_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Knomial_Bcast_MV2(
        void *buffer,
        int count,
        MPI_Datatype datatype,
        int root,
        MPID_Comm *comm_ptr, 
        int *errflag)
{
    MPI_Comm comm;
    int local_size=0,rank;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Request *reqarray=NULL; 
    MPI_Status  *starray=NULL;
    int src,dst,mask,relative_rank;
    int k;
    comm  = comm_ptr->handle;
    PMPI_Comm_size ( comm, &local_size );
    rank = comm_ptr->rank;
    MPIU_CHKLMEM_DECL(2);

    MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, 2*intra_node_knomial_factor*sizeof(MPI_Request), 
                        mpi_errno, "reqarray");

    MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, 2*intra_node_knomial_factor*sizeof(MPI_Status), 
                        mpi_errno, "starray");



   /* intra-node k-nomial bcast  */ 
    if(local_size > 1) { 
      relative_rank = (rank >= root) ? rank - root : rank - root + local_size;
      mask = 0x1;

      while (mask < local_size) {
         if (relative_rank % (intra_node_knomial_factor*mask)) {
            src = relative_rank/(intra_node_knomial_factor*mask)*
                             (intra_node_knomial_factor*mask)+root;
            if (src >= local_size) {
               src -= local_size;
            }

            mpi_errno = MPIC_Recv_ft(buffer,count, datatype, src,
                                 MPIR_BCAST_TAG,comm,MPI_STATUS_IGNORE, 
                                errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            break;
         }
         mask *= intra_node_knomial_factor;
      }
      mask /= intra_node_knomial_factor;
   
      while (mask > 0) {
          int reqs=0; 
          for(k=1;k<intra_node_knomial_factor;k++) {
              if (relative_rank + mask*k < local_size) {
                  dst = rank + mask*k;
                  if (dst >= local_size) {
                    dst -= local_size;
                  }
                  mpi_errno = MPIC_Isend_ft (buffer, count, datatype ,dst,
                                    MPIR_BCAST_TAG, comm, &reqarray[reqs++], 
                                    errflag);
                  if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                   }
              }
         }
         mpi_errno = MPIC_Waitall_ft(reqs, reqarray, starray, errflag);
         if (mpi_errno && mpi_errno != MPI_ERR_IN_STATUS) MPIU_ERR_POP(mpi_errno);

         /* --BEGIN ERROR HANDLING-- */
         if (mpi_errno == MPI_ERR_IN_STATUS) {
              int j; 
              for (j=0; j<reqs; j++) {
                    if (starray[j].MPI_ERROR != MPI_SUCCESS) {
                        mpi_errno = starray[j].MPI_ERROR;
                        if (mpi_errno) {
                            /* for communication errors, just record the error but continue */
                            *errflag = TRUE;
                            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                        }
                    }
               }
          }
          mask /= intra_node_knomial_factor;
      }
   }  

  fn_fail :
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
} 


#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_inter_node_helper_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
static int MPIR_Bcast_inter_node_helper_MV2(
    void *buffer, 
    int count, 
    MPI_Datatype datatype, 
    int root, 
    MPID_Comm *comm_ptr, 
    int *errflag)
{
    int rank, comm_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int nbytes, type_size; 
    MPI_Comm comm, shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr = 0, *leader_commptr = 0;
    int local_rank, local_size, global_rank=-1;
    int leader_root, leader_of_root;

    comm = comm_ptr->handle; 

    mpi_errno = PMPI_Comm_rank(comm, &rank);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(comm, &comm_size);
    if(mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
    }

    shmem_comm = comm_ptr->ch.shmem_comm;
    MPID_Comm_get_ptr(shmem_comm,shmem_commptr);
    local_rank = shmem_commptr->rank; 
    local_size = shmem_commptr->local_size; 

    leader_comm = comm_ptr->ch.leader_comm;
    MPID_Comm_get_ptr(leader_comm,leader_commptr);

    if ((local_rank == 0)&&(local_size > 1)) {
        global_rank = leader_commptr->rank;
    }

    leader_of_root = comm_ptr->ch.leader_map[root];
    leader_root = comm_ptr->ch.leader_rank[leader_of_root];
    MPID_Datatype_get_size_macro(datatype, type_size);
    nbytes = count*type_size; 
 
    if (local_size > 1) {
        if ((local_rank == 0) &&
            (root != rank) &&
            (leader_root == global_rank)) {
             mpi_errno = MPIC_Recv_ft (buffer, count, datatype, root,
                        MPIR_BCAST_TAG, comm, MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
        if ((local_rank != 0) && (root == rank)) {
            mpi_errno  = MPIC_Send_ft(buffer,count,datatype,
                                   leader_of_root, MPIR_BCAST_TAG,
                                   comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    }


    if(local_rank == 0) { 
        leader_comm = comm_ptr->ch.leader_comm;
        root = leader_root; 
        MPID_Comm_get_ptr(leader_comm,leader_commptr);
        comm_size = leader_commptr->local_size; 
        rank      = leader_commptr->rank; 

        if (knomial_inter_leader_bcast == 1 
                  && nbytes <= knomial_inter_leader_threshold ) {
                 mpi_errno =  MPIR_Knomial_Bcast_MV2(buffer, count, 
                                     datatype, root, leader_commptr, errflag);  
        } else { 
            if(scatter_ring_inter_leader_bcast) { 
                 mpi_errno = MPIR_Bcast_scatter_ring_allgather_MV2(buffer, count, 
                                     datatype, root, leader_commptr, errflag);  
            } 
            else if(scatter_rd_inter_leader_bcast) {                                        
                 mpi_errno = MPIR_Bcast_scatter_doubling_allgather_MV2(buffer, count, 
                                     datatype, root, leader_commptr, errflag);   
            } 
            else if(knomial_inter_leader_bcast) { 
                      mpi_errno =  MPIR_Knomial_Bcast_MV2(buffer, count, 
                                     datatype, root, leader_commptr, errflag);  
            } else {    
                 mpi_errno = MPIR_Bcast_binomial_MV2(buffer, count, 
                                     datatype, root, leader_commptr, errflag);  
            } 
            if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        }    
    } 

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;
}
#endif /*#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */


#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)

int MPIR_Bcast_intra_MV2 ( 
        void *buffer, 
        int count, 
        MPI_Datatype datatype, 
        int root, 
        MPID_Comm *comm_ptr, 
       int *errflag)
{
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int comm_size, rank;
    int nbytes=0, intra_node_root=0;
    int type_size, is_homogeneous, is_contig, position;
    void *tmp_buf = NULL; 
    MPID_Comm *shmem_commptr;
    MPI_Comm shmem_comm;
    MPID_Datatype *dtp;
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)  */
    
    MPIU_THREADPRIV_DECL;
    MPID_MPI_STATE_DECL(MPID_STATE_MPIR_BCAST);

    MPID_MPI_FUNC_ENTER(MPID_STATE_MPIR_BCAST);
    MPIU_CHKLMEM_DECL(1);
    
    /* The various MPIR_Bcast_* impls use NMPI functions, so we bump the nest
       count here to avoid repeatedly calling incr/decr. */
    MPIU_THREADPRIV_GET;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER( comm_ptr );
    if (count == 0) goto fn_exit;


#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank; 
    
    if (HANDLE_GET_KIND(datatype) == HANDLE_KIND_BUILTIN)
        is_contig = 1;
    else {
        MPID_Datatype_get_ptr(datatype, dtp);
        is_contig = dtp->is_contig;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero)
        is_homogeneous = 0;
#endif

    /* MPI_Type_size() might not give the accurate size of the packed
     * datatype for heterogeneous systems (because of padding, encoding,
     * etc). On the other hand, MPI_Pack_size() can become very
     * expensive, depending on the implementation, especially for
     * heterogeneous systems. We want to use MPI_Type_size() wherever
     * possible, and MPI_Pack_size() in other places.
     */
    if (is_homogeneous) {
        MPID_Datatype_get_size_macro(datatype, type_size);
    }
    else {
        MPIR_Pack_size_impl(1, datatype, &type_size);
    }
    nbytes = type_size * count;

    if(comm_ptr->ch.shmem_coll_ok == 1 && 
       enable_shmem_bcast == 1         && 
       comm_size > bcast_two_level_system_size) { 
         if (!is_contig || !is_homogeneous) {
                MPIU_CHKLMEM_MALLOC(tmp_buf, void *, nbytes, mpi_errno, "tmp_buf");

                /* TODO: Pipeline the packing and communication */
                position = 0;
                if (rank == root) { 
                   mpi_errno = MPIR_Pack_impl(buffer, count, datatype, tmp_buf, nbytes,
                                       &position);
                  if (mpi_errno) MPIU_ERR_POP(mpi_errno);
                } 
         }

         shmem_comm = comm_ptr->ch.shmem_comm; 
         MPID_Comm_get_ptr(shmem_comm,shmem_commptr);
         if (!is_contig || !is_homogeneous) {
                mpi_errno = MPIR_Bcast_inter_node_helper_MV2( tmp_buf, nbytes, MPI_BYTE,
                                root, comm_ptr, errflag);  
         } else { 
                mpi_errno = MPIR_Bcast_inter_node_helper_MV2( buffer, count, datatype,
                                root, comm_ptr, errflag);  
         } 
         if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
         }

         /* We are now done with the inter-node phase */ 
         if(nbytes <= knomial_intra_node_threshold) { 
              if (!is_contig || !is_homogeneous) {
                     mpi_errno = MPIR_Shmem_Bcast_MV2( tmp_buf, nbytes, MPI_BYTE,
                         root, shmem_commptr);   
              } else { 
                     mpi_errno = MPIR_Shmem_Bcast_MV2( buffer, count, datatype,
                         root, shmem_commptr);   
              } 
         } else { 
              if (!is_contig || !is_homogeneous) {
                     mpi_errno = MPIR_Knomial_Bcast_MV2( tmp_buf, nbytes, MPI_BYTE,
                         intra_node_root, shmem_commptr, errflag);  
              } else { 
                     mpi_errno = MPIR_Knomial_Bcast_MV2( buffer, count, datatype,
                         intra_node_root, shmem_commptr, errflag);  
              } 
         }   
         if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
         }
         if (!is_contig || !is_homogeneous) {
            /* Finishing up...*/
            if (rank != root) {
                position = 0;
                mpi_errno = MPIR_Unpack_impl(tmp_buf, nbytes, &position, buffer,
                                         count, datatype);
            }
         }
    } else { 
            if(nbytes <= bcast_short_msg) { 
                    mpi_errno = MPIR_Bcast_binomial_MV2(buffer, count, datatype, root,
                                    comm_ptr, errflag);
            } else {
                    if(scatter_rd_inter_leader_bcast) {
                         mpi_errno = MPIR_Bcast_scatter_ring_allgather_MV2(buffer, count,
                                             datatype, root, comm_ptr, errflag);
                    }
                    else {
                         mpi_errno = MPIR_Bcast_scatter_doubling_allgather_MV2(buffer, count,
                                             datatype, root, comm_ptr, errflag);
                    }
            }
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
    } 
#else /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */ 
    mpi_errno = MPIR_Bcast( buffer, count, datatype,
             root, comm_ptr, errflag);  
    if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }
#endif /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
   

fn_exit:
    MPIU_CHKLMEM_FREEALL();
    if (mpi_errno_ret)
        mpi_errno = mpi_errno_ret;
    else if (*errflag)
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**coll_fail");
    return mpi_errno;
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
 fn_fail:
    goto fn_exit;
#endif /* #ifdef _OSU_MVAPICH_ */
}

#undef FUNCNAME
#define FUNCNAME MPIR_Bcast_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Bcast_MV2(void *buf, int count, MPI_Datatype datatype,
                  int root, MPID_Comm *comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
        /* intracommunicator */
        mpi_errno = MPIR_Bcast_intra_MV2(buf, count, datatype,
                                        root, comm_ptr, errflag);
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    } else {
        mpi_errno = MPIR_Bcast_inter_MV2(buf, count, datatype,
                                        root, comm_ptr, errflag);
        /* intercommunicator */
        if (mpi_errno) MPIU_ERR_POP(mpi_errno);
    }

 fn_exit:
    return mpi_errno;
 fn_fail:
    goto fn_exit;
}

