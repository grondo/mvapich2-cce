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
#include "coll_shmem.h"
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

/* This is the default implementation of scatter. The algorithm is:
   
   Algorithm: MPI_Scatter

   We use a binomial tree algorithm for both short and
   long messages. At nodes other than leaf nodes we need to allocate
   a temporary buffer to store the incoming message. If the root is
   not rank 0, we reorder the sendbuf in order of relative ranks by 
   copying it into a temporary buffer, so that all the sends from the
   root are contiguous and in the right order. In the heterogeneous
   case, we first pack the buffer by using MPI_Pack and then do the
   scatter. 

   Cost = lgp.alpha + n.((p-1)/p).beta
   where n is the total size of the data to be scattered from the root.

   Possible improvements: 

   End Algorithm: MPI_Scatter
*/

/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */

#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_MV2_Binomial
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_MV2_Binomial(void *sendbuf,
                              int sendcnt,
                              MPI_Datatype sendtype,
                              void *recvbuf,
                              int recvcnt,
                              MPI_Datatype recvtype,
                              int root, MPID_Comm * comm_ptr, int *errflag)
{
    MPI_Status status;
    MPI_Aint extent = 0;
    int rank, comm_size, is_homogeneous, sendtype_size;
    int curr_cnt, relative_rank, nbytes, send_subtree_cnt;
    int mask, recvtype_size = 0, src, dst;
#ifdef MPID_HAS_HETERO
    int position;
#endif                          /* MPID_HAS_HETERO */
    int tmp_buf_size = 0;
    void *tmp_buf = NULL;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Comm comm;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (((rank == root) && (sendcnt == 0)) || ((rank != root) && (recvcnt == 0))) {
        return MPI_SUCCESS;
    }

    is_homogeneous = 1;
#ifdef MPID_HAS_HETERO
    if (comm_ptr->is_hetero) {
        is_homogeneous = 0;
    }
#endif                          /* MPID_HAS_HETERO */

/* Use binomial tree algorithm */

    if (rank == root) {
        MPID_Datatype_get_extent_macro(sendtype, extent);
    }

    relative_rank = (rank >= root) ? rank - root : rank - root + comm_size;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

    if (is_homogeneous) {
        /* communicator is homogeneous */
        if (rank == root) {
            /* We separate the two cases (root and non-root) because
               in the event of recvbuf=MPI_IN_PLACE on the root,
               recvcnt and recvtype are not valid */
            MPID_Datatype_get_size_macro(sendtype, sendtype_size);
            nbytes = sendtype_size * sendcnt;
        } else {
            MPID_Datatype_get_size_macro(recvtype, recvtype_size);
            nbytes = recvtype_size * recvcnt;
        }

        curr_cnt = 0;

        /* all even nodes other than root need a temporary buffer to
           receive data of max size (nbytes*comm_size)/2 */
        if (relative_rank && !(relative_rank % 2)) {
            tmp_buf_size = (nbytes * comm_size) / 2;
            tmp_buf = MPIU_Malloc(tmp_buf_size);
            /* --BEGIN ERROR HANDLING-- */
            if (!tmp_buf) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        }

        /* if the root is not rank 0, we reorder the sendbuf in order of
           relative ranks and copy it into a temporary buffer, so that
           all the sends from the root are contiguous and in the right
           order. */
        if (rank == root) {
            if (root != 0) {
                tmp_buf_size = nbytes * comm_size;
                tmp_buf = MPIU_Malloc(tmp_buf_size);
                /* --BEGIN ERROR HANDLING-- */
                if (!tmp_buf) {
                    mpi_errno =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                             __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                    return mpi_errno;
                }
                /* --END ERROR HANDLING-- */

                if (recvbuf != MPI_IN_PLACE) {
                    mpi_errno =
                        MPIR_Localcopy(((char *) sendbuf + extent * sendcnt * rank),
                                       sendcnt * (comm_size - rank), sendtype, tmp_buf,
                                       nbytes * (comm_size - rank), MPI_BYTE);
                } else {
                    mpi_errno =
                        MPIR_Localcopy(((char *) sendbuf + extent * sendcnt * (rank + 1)),
                                       sendcnt * (comm_size - rank - 1), sendtype,
                                       (char *) tmp_buf + nbytes,
                                       nbytes * (comm_size - rank - 1), MPI_BYTE);
                }
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno) {
                    mpi_errno =
                        MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                             __LINE__, MPI_ERR_OTHER, "**fail", 0);
                    return mpi_errno;
                }
                /* --END ERROR HANDLING-- */

                mpi_errno = MPIR_Localcopy(sendbuf, sendcnt * rank, sendtype,
                                           ((char *) tmp_buf +
                                            nbytes * (comm_size - rank)), nbytes * rank,
                                           MPI_BYTE);
                /* --BEGIN ERROR HANDLING-- */
                if (mpi_errno) {
                    mpi_errno =
                        MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                             __LINE__, MPI_ERR_OTHER, "**fail", 0);
                    return mpi_errno;
                }
                /* --END ERROR HANDLING-- */

                curr_cnt = nbytes * comm_size;
            } else {
                curr_cnt = sendcnt * comm_size;
            }
        }

        /* root has all the data; others have zero so far */

        mask = 0x1;
        while (mask < comm_size) {
            if (relative_rank & mask) {
                src = rank - mask;
                if (src < 0)
                    src += comm_size;

                /* The leaf nodes receive directly into recvbuf because
                   they don't have to forward data to anyone. Others
                   receive data into a temporary buffer. */
                if (relative_rank % 2) {
                    mpi_errno = MPIC_Recv_ft(recvbuf, recvcnt, recvtype,
                                             src, MPIR_SCATTER_TAG, comm,
                                             &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }
                } else {
                    mpi_errno = MPIC_Recv_ft(tmp_buf, tmp_buf_size, MPI_BYTE, src,
                                             MPIR_SCATTER_TAG, comm, &status, errflag);
                    if (mpi_errno) {
                        /* for communication errors, just record the error but continue */
                        *errflag = TRUE;
                        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                    }

                    /* the recv size is larger than what may be sent in
                       some cases. query amount of data actually received */
                    MPIR_Get_count_impl(&status, MPI_BYTE, &curr_cnt);
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
        while (mask > 0) {
            if (relative_rank + mask < comm_size) {
                dst = rank + mask;
                if (dst >= comm_size)
                    dst -= comm_size;

                if ((rank == root) && (root == 0)) {
                    send_subtree_cnt = curr_cnt - sendcnt * mask;
                    /* mask is also the size of this process's subtree */
                    mpi_errno = MPIC_Send_ft(((char *) sendbuf +
                                              extent * sendcnt * mask),
                                             send_subtree_cnt,
                                             sendtype, dst,
                                             MPIR_SCATTER_TAG, comm, errflag);
                } else {
                    /* non-zero root and others */
                    send_subtree_cnt = curr_cnt - nbytes * mask;
                    /* mask is also the size of this process's subtree */
                    mpi_errno = MPIC_Send_ft(((char *) tmp_buf + nbytes * mask),
                                             send_subtree_cnt,
                                             MPI_BYTE, dst,
                                             MPIR_SCATTER_TAG, comm, errflag);
                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                curr_cnt -= send_subtree_cnt;
            }
            mask >>= 1;
        }

        if ((rank == root) && (root == 0) && (recvbuf != MPI_IN_PLACE)) {
            /* for root=0, put root's data in recvbuf if not MPI_IN_PLACE */
            mpi_errno = MPIR_Localcopy(sendbuf, sendcnt, sendtype,
                                       recvbuf, recvcnt, recvtype);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**fail", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        } else if (!(relative_rank % 2) && (recvbuf != MPI_IN_PLACE)) {
            /* for non-zero root and non-leaf nodes, copy from tmp_buf
               into recvbuf */
            mpi_errno = MPIR_Localcopy(tmp_buf, nbytes, MPI_BYTE,
                                       recvbuf, recvcnt, recvtype);
            /* --BEGIN ERROR HANDLING-- */
            if (mpi_errno) {
                mpi_errno =
                    MPIR_Err_create_code(mpi_errno, MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**fail", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */
        }

        if (tmp_buf != NULL)
            MPIU_Free(tmp_buf);
    }
#ifdef MPID_HAS_HETERO
    else {                      /* communicator is heterogeneous */
        if (rank == root) {
            MPIR_Pack_size_impl(sendcnt * comm_size, sendtype, &tmp_buf_size);
            tmp_buf = MPIU_Malloc(tmp_buf_size);
            /* --BEGIN ERROR HANDLING-- */
            if (!tmp_buf) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */

            /* calculate the value of nbytes, the number of bytes in packed
               representation that each process receives. We can't
               accurately calculate that from tmp_buf_size because
               MPI_Pack_size returns an upper bound on the amount of memory
               required. (For example, for a single integer, MPICH-1 returns
               pack_size=12.) Therefore, we actually pack some data into
               tmp_buf and see by how much 'position' is incremented. */

            position = 0;
            MPIR_Pack_impl(sendbuf, 1, sendtype, tmp_buf, tmp_buf_size, &position);
            nbytes = position * sendcnt;

            curr_cnt = nbytes * comm_size;

            if (root == 0) {
                if (recvbuf != MPI_IN_PLACE) {
                    position = 0;
                    MPIR_Pack_impl(sendbuf, sendcnt * comm_size, sendtype, tmp_buf,
                                   tmp_buf_size, &position);
                } else {
                    position = nbytes;
                    MPIR_Pack_impl(((char *) sendbuf + extent * sendcnt),
                                   sendcnt * (comm_size - 1), sendtype, tmp_buf,
                                   tmp_buf_size, &position);
                }
            } else {
                if (recvbuf != MPI_IN_PLACE) {
                    position = 0;
                    MPIR_Pack_impl(((char *) sendbuf + extent * sendcnt * rank),
                                   sendcnt * (comm_size - rank), sendtype, tmp_buf,
                                   tmp_buf_size, &position);
                } else {
                    position = nbytes;
                    MPIR_Pack_impl(((char *) sendbuf + extent * sendcnt * (rank + 1)),
                                   sendcnt * (comm_size - rank - 1), sendtype, tmp_buf,
                                   tmp_buf_size, &position);
                }
                MPIR_Pack_impl(sendbuf, sendcnt * rank, sendtype, tmp_buf,
                               tmp_buf_size, &position);
            }
        } else {
            MPIR_Pack_impl_size(recvcnt * (comm_size / 2), recvtype, &tmp_buf_size);
            tmp_buf = MPIU_Malloc(tmp_buf_size);
            /* --BEGIN ERROR HANDLING-- */
            if (!tmp_buf) {
                mpi_errno =
                    MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                         __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                return mpi_errno;
            }
            /* --END ERROR HANDLING-- */

            /* calculate nbytes */
            position = 0;
            MPIR_Pack_impl(recvbuf, 1, recvtype, tmp_buf, tmp_buf_size, &position);
            nbytes = position * recvcnt;

            curr_cnt = 0;
        }

        mask = 0x1;
        while (mask < comm_size) {
            if (relative_rank & mask) {
                src = rank - mask;
                if (src < 0)
                    src += comm_size;

                mpi_errno = MPIC_Recv_ft(tmp_buf, tmp_buf_size, MPI_BYTE, src,
                                         MPIR_SCATTER_TAG, comm, &status, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                /* the recv size is larger than what may be sent in
                   some cases. query amount of data actually received */
                MPIR_Get_count_impl(&status, MPI_BYTE, &curr_cnt);
                break;
            }
            mask <<= 1;
        }

        /* This process is responsible for all processes that have bits
           set from the LSB upto (but not including) mask.  Because of
           the "not including", we start by shifting mask back down
           one. */

        mask >>= 1;
        while (mask > 0) {
            if (relative_rank + mask < comm_size) {
                dst = rank + mask;
                if (dst >= comm_size)
                    dst -= comm_size;

                send_subtree_cnt = curr_cnt - nbytes * mask;
                /* mask is also the size of this process's subtree */
                mpi_errno = MPIC_Send_ft(((char *) tmp_buf + nbytes * mask),
                                         send_subtree_cnt, MPI_BYTE, dst,
                                         MPIR_SCATTER_TAG, comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

                curr_cnt -= send_subtree_cnt;
            }
            mask >>= 1;
        }

        /* copy local data into recvbuf */
        position = 0;
        if (recvbuf != MPI_IN_PLACE)
            MPIR_Unpack_impl(tmp_buf, tmp_buf_size, &position, recvbuf, recvcnt,
                             recvtype);
        MPIU_Free(tmp_buf);
    }
#endif                          /* MPID_HAS_HETERO */

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    return (mpi_errno);
}

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_MV2_Direct
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_MV2_Direct(void *sendbuf,
                            int sendcnt,
                            MPI_Datatype sendtype,
                            void *recvbuf,
                            int recvcnt,
                            MPI_Datatype recvtype,
                            int root, MPID_Comm * comm_ptr, int *errflag)
{

    int rank, comm_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    MPI_Comm comm;
    MPI_Aint sendtype_extent;
    int i, reqs;
    MPI_Request *reqarray;
    MPI_Status *starray;
    MPIU_CHKLMEM_DECL(2);

    comm = comm_ptr->handle;
    rank = comm_ptr->rank;

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);

    /* If I'm the root, then scatter */
    if (((comm_ptr->comm_kind == MPID_INTRACOMM) && (root == rank)) ||
        ((comm_ptr->comm_kind == MPID_INTERCOMM) && (root == MPI_ROOT))) {
        if (comm_ptr->comm_kind == MPID_INTRACOMM)
            comm_size = comm_ptr->local_size;
        else
            comm_size = comm_ptr->remote_size;

        MPID_Datatype_get_extent_macro(sendtype, sendtype_extent);
        /* We need a check to ensure extent will fit in a
         * pointer. That needs extent * (max count) but we can't get
         * that without looping over the input data. This is at least
         * a minimal sanity check. Maybe add a global var since we do
         * loop over sendcount[] in MPI_Scatterv before calling
         * this? */
        MPID_Ensure_Aint_fits_in_pointer(MPI_VOID_PTR_CAST_TO_MPI_AINT sendbuf +
                                         sendtype_extent);

        MPIU_CHKLMEM_MALLOC(reqarray, MPI_Request *, comm_size * sizeof (MPI_Request),
                            mpi_errno, "reqarray");
        MPIU_CHKLMEM_MALLOC(starray, MPI_Status *, comm_size * sizeof (MPI_Status),
                            mpi_errno, "starray");

        reqs = 0;
        for (i = 0; i < comm_size; i++) {
            if (sendcnt) {
                if ((comm_ptr->comm_kind == MPID_INTRACOMM) && (i == rank)) {
                    if (recvbuf != MPI_IN_PLACE) {
                        mpi_errno =
                            MPIR_Localcopy(((char *) sendbuf +
                                            rank * sendcnt * sendtype_extent), sendcnt,
                                           sendtype, recvbuf, recvcnt, recvtype);
                    }
                } else {
                    mpi_errno =
                        MPIC_Isend_ft(((char *) sendbuf + i * sendcnt * sendtype_extent),
                                      sendcnt, sendtype, i, MPIR_SCATTER_TAG, comm,
                                      &reqarray[reqs++], errflag);
                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
        }
        /* ... then wait for *all* of them to finish: */
        mpi_errno = MPIC_Waitall_ft(reqs, reqarray, starray, errflag);
        /* --BEGIN ERROR HANDLING-- */
        if (mpi_errno == MPI_ERR_IN_STATUS) {
            for (i = 0; i < reqs; i++) {
                if (starray[i].MPI_ERROR != MPI_SUCCESS)
                    mpi_errno = starray[i].MPI_ERROR;
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

            }
        }
        /* --END ERROR HANDLING-- */
    }

    else if (root != MPI_PROC_NULL) {   /* non-root nodes, and in the intercomm. case, non-root nodes on remote side */
        if (recvcnt) {
            mpi_errno = MPIC_Recv_ft(recvbuf, recvcnt, recvtype, root,
                                     MPIR_SCATTER_TAG, comm, MPI_STATUS_IGNORE, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
    }

    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

  fn_exit:
    MPIU_CHKLMEM_FREEALL();
    return mpi_errno;
  fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_MV2_two_level_Binomial
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_MV2_two_level_Binomial(void *sendbuf,
                                        int sendcnt,
                                        MPI_Datatype sendtype,
                                        void *recvbuf,
                                        int recvcnt,
                                        MPI_Datatype recvtype,
                                        int root, MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int local_rank, local_size;
    int leader_comm_rank, leader_comm_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int recvtype_size, sendtype_size, nbytes;
    void *tmp_buf = NULL;
    void *leader_scatter_buf = NULL;
    MPI_Status status;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    int leader_root = -1, leader_of_root = -1;
    MPI_Comm shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr, *leader_commptr = NULL;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (((rank == root) && (recvcnt == 0)) || ((rank != root) && (sendcnt == 0))) {
        return MPI_SUCCESS;
    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
    /* extract the rank,size information for the intra-node
     * communicator */
    shmem_comm = comm_ptr->ch.shmem_comm;
    mpi_errno = PMPI_Comm_rank(shmem_comm, &local_rank);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(shmem_comm, &local_size);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);

    if (local_rank == 0) {
        /* Node leader. Extract the rank, size information for the leader
         * communicator */
        leader_comm = comm_ptr->ch.leader_comm;
        mpi_errno = PMPI_Comm_rank(leader_comm, &leader_comm_rank);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        mpi_errno = PMPI_Comm_size(leader_comm, &leader_comm_size);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        MPID_Comm_get_ptr(leader_comm, leader_commptr);
    }

    if (local_size == comm_size) {
        /* purely intra-node scatter. Just use the direct algorithm and we are done */
        mpi_errno = MPIR_Scatter_MV2_Direct(sendbuf, sendcnt, sendtype,
                                            recvbuf, recvcnt, recvtype,
                                            root, comm_ptr, errflag);
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    } else {
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);

        if (rank == root) {
            nbytes = sendcnt * sendtype_size;
        } else {
            nbytes = recvcnt * recvtype_size;
        }

        if (local_rank == 0) {
            /* Node leader, allocate tmp_buffer */
            tmp_buf = MPIU_Malloc(nbytes * local_size);
        }

        leader_of_root = comm_ptr->ch.leader_map[root];
        /* leader_of_root is the global rank of the leader of the root */
        leader_root = comm_ptr->ch.leader_rank[leader_of_root];
        /* leader_root is the rank of the leader of the root in leader_comm.
         * leader_root is to be used as the root of the inter-leader gather ops
         */

        if ((local_rank == 0) && (root != rank)
            && (leader_of_root == rank)) {
            /* The root of the scatter operation is not the node leader. Recv
             * data from the node leader */
            leader_scatter_buf = MPIU_Malloc(nbytes * comm_size);
            mpi_errno = MPIC_Recv_ft(leader_scatter_buf, nbytes * comm_size, MPI_BYTE,
                                     root, MPIR_SCATTER_TAG, comm, &status, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

        if (rank == root && local_rank != 0) {
            /* The root of the scatter operation is not the node leader. Send
             * data to the node leader */
            mpi_errno = MPIC_Send_ft(sendbuf, sendcnt * comm_size, sendtype,
                                     leader_of_root, MPIR_SCATTER_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

        if (leader_comm_size > 1 && local_rank == 0) {
            if (comm_ptr->ch.is_uniform != 1) {
                int *displs = NULL;
                int *sendcnts = NULL;
                int *node_sizes;
                int i = 0;
                node_sizes = comm_ptr->ch.node_sizes;

                if (root != leader_of_root) {
                    if (leader_comm_rank == leader_root) {
                        displs = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts[0] = node_sizes[0] * nbytes;
                        displs[0] = 0;

                        for (i = 1; i < leader_comm_size; i++) {
                            displs[i] = displs[i - 1] + node_sizes[i - 1] * nbytes;
                            sendcnts[i] = node_sizes[i] * nbytes;
                        }
                    }
                    mpi_errno = MPIR_Scatterv(leader_scatter_buf, sendcnts, displs,
                                              MPI_BYTE, tmp_buf, nbytes * local_size,
                                              MPI_BYTE, leader_root, leader_commptr,
                                              errflag);
                } else {
                    if (leader_comm_rank == leader_root) {
                        displs = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts[0] = node_sizes[0] * sendcnt;
                        displs[0] = 0;

                        for (i = 1; i < leader_comm_size; i++) {
                            displs[i] = displs[i - 1] + node_sizes[i - 1] * sendcnt;
                            sendcnts[i] = node_sizes[i] * sendcnt;
                        }
                    }
                    mpi_errno = MPIR_Scatterv(sendbuf, sendcnts, displs,
                                              sendtype, tmp_buf, nbytes * local_size,
                                              MPI_BYTE, leader_root, leader_commptr,
                                              errflag);
                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                if (leader_comm_rank == leader_root) {
                    MPIU_Free(displs);
                    MPIU_Free(sendcnts);
                }
            } else {
                if (leader_of_root != root) {
                    mpi_errno =
                        MPIR_Scatter_MV2_Binomial(leader_scatter_buf, nbytes * local_size,
                                                  MPI_BYTE, tmp_buf, nbytes * local_size,
                                                  MPI_BYTE, leader_root, leader_commptr,
                                                  errflag);
                } else {
                    mpi_errno = MPIR_Scatter_MV2_Binomial(sendbuf, sendcnt * local_size,
                                                          sendtype, tmp_buf,
                                                          nbytes * local_size, MPI_BYTE,
                                                          leader_root, leader_commptr,
                                                          errflag);

                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
        }
        /* The leaders are now done with the inter-leader part. Scatter the data within the nodes */

        if (rank == root && recvbuf == MPI_IN_PLACE) {
            mpi_errno = MPIR_Scatter_MV2_Direct(tmp_buf, nbytes, MPI_BYTE,
                                                sendbuf, sendcnt, sendtype,
                                                0, shmem_commptr, errflag);
        } else {
            mpi_errno = MPIR_Scatter_MV2_Direct(tmp_buf, nbytes, MPI_BYTE,
                                                recvbuf, recvcnt, recvtype,
                                                0, shmem_commptr, errflag);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }

        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    }

  fn_fail:
    /* check if multiple threads are calling this collective function */
    if (comm_size != local_size && local_rank == 0) {
        MPIU_Free(tmp_buf);
        if (leader_of_root == rank && root != rank) {
            MPIU_Free(leader_scatter_buf);
        }
    }
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    return (mpi_errno);
}

#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_MV2_two_level_Direct
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_MV2_two_level_Direct(void *sendbuf,
                                      int sendcnt,
                                      MPI_Datatype sendtype,
                                      void *recvbuf,
                                      int recvcnt,
                                      MPI_Datatype recvtype,
                                      int root, MPID_Comm * comm_ptr, int *errflag)
{
    int comm_size, rank;
    int local_rank, local_size;
    int leader_comm_rank, leader_comm_size;
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int recvtype_size, sendtype_size, nbytes;
    void *tmp_buf = NULL;
    void *leader_scatter_buf = NULL;
    MPI_Status status;
    MPI_Comm comm;
    MPIU_THREADPRIV_DECL;
    MPIU_THREADPRIV_GET;
    int leader_root, leader_of_root = -1;
    MPI_Comm shmem_comm, leader_comm;
    MPID_Comm *shmem_commptr, *leader_commptr = NULL;

    comm = comm_ptr->handle;
    comm_size = comm_ptr->local_size;
    rank = comm_ptr->rank;

    if (((rank == root) && (recvcnt == 0)) || ((rank != root) && (sendcnt == 0))) {
        return MPI_SUCCESS;
    }
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
    /* extract the rank,size information for the intra-node
     * communicator */
    shmem_comm = comm_ptr->ch.shmem_comm;
    mpi_errno = PMPI_Comm_rank(shmem_comm, &local_rank);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_size(shmem_comm, &local_size);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    MPID_Comm_get_ptr(shmem_comm, shmem_commptr);

    if (local_rank == 0) {
        /* Node leader. Extract the rank, size information for the leader
         * communicator */
        leader_comm = comm_ptr->ch.leader_comm;
        mpi_errno = PMPI_Comm_rank(leader_comm, &leader_comm_rank);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        mpi_errno = PMPI_Comm_size(leader_comm, &leader_comm_size);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
        MPID_Comm_get_ptr(leader_comm, leader_commptr);
    }

    if (local_size == comm_size) {
        /* purely intra-node scatter. Just use the direct algorithm and we are done */
        mpi_errno = MPIR_Scatter_MV2_Direct(sendbuf, sendcnt, sendtype,
                                            recvbuf, recvcnt, recvtype,
                                            root, comm_ptr, errflag);
        if (mpi_errno) {
            MPIU_ERR_POP(mpi_errno);
        }
    } else {
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);

        if (rank == root) {
            nbytes = sendcnt * sendtype_size;
        } else {
            nbytes = recvcnt * recvtype_size;
        }

        if (local_rank == 0) {
            /* Node leader, allocate tmp_buffer */
            tmp_buf = MPIU_Malloc(nbytes * local_size);
        }

        leader_of_root = comm_ptr->ch.leader_map[root];
        /* leader_of_root is the global rank of the leader of the root */
        leader_root = comm_ptr->ch.leader_rank[leader_of_root];
        /* leader_root is the rank of the leader of the root in leader_comm.
         * leader_root is to be used as the root of the inter-leader gather ops
         */

        if ((local_rank == 0) && (root != rank)
            && (leader_of_root == rank)) {
            /* The root of the scatter operation is not the node leader. Recv
             * data from the node leader */
            leader_scatter_buf = MPIU_Malloc(nbytes * comm_size);
            mpi_errno = MPIC_Recv_ft(leader_scatter_buf, nbytes * comm_size, MPI_BYTE,
                                     root, MPIR_SCATTER_TAG, comm, &status, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

        if (rank == root && local_rank != 0) {
            /* The root of the scatter operation is not the node leader. Send
             * data to the node leader */
            mpi_errno = MPIC_Send_ft(sendbuf, sendcnt * comm_size, sendtype,
                                     leader_of_root, MPIR_SCATTER_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }

        if (leader_comm_size > 1 && local_rank == 0) {
            if (comm_ptr->ch.is_uniform != 1) {
                int *displs = NULL;
                int *sendcnts = NULL;
                int *node_sizes;
                int i = 0;
                node_sizes = comm_ptr->ch.node_sizes;

                if (root != leader_of_root) {
                    if (leader_comm_rank == leader_root) {
                        displs = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts[0] = node_sizes[0] * nbytes;
                        displs[0] = 0;

                        for (i = 1; i < leader_comm_size; i++) {
                            displs[i] = displs[i - 1] + node_sizes[i - 1] * nbytes;
                            sendcnts[i] = node_sizes[i] * nbytes;
                        }
                    }
                    mpi_errno = MPIR_Scatterv(leader_scatter_buf, sendcnts, displs,
                                              MPI_BYTE, tmp_buf, nbytes * local_size,
                                              MPI_BYTE, leader_root, leader_commptr,
                                              errflag);
                } else {
                    if (leader_comm_rank == leader_root) {
                        displs = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts = MPIU_Malloc(sizeof (int) * leader_comm_size);
                        sendcnts[0] = node_sizes[0] * sendcnt;
                        displs[0] = 0;

                        for (i = 1; i < leader_comm_size; i++) {
                            displs[i] = displs[i - 1] + node_sizes[i - 1] * sendcnt;
                            sendcnts[i] = node_sizes[i] * sendcnt;
                        }
                    }
                    mpi_errno = MPIR_Scatterv(sendbuf, sendcnts, displs,
                                              sendtype, tmp_buf, nbytes * local_size,
                                              MPI_BYTE, leader_root, leader_commptr,
                                              errflag);
                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
                if (leader_comm_rank == leader_root) {
                    MPIU_Free(displs);
                    MPIU_Free(sendcnts);
                }
            } else {
                if (leader_of_root != root) {
                    mpi_errno =
                        MPIR_Scatter_MV2_Direct(leader_scatter_buf, nbytes * local_size,
                                                MPI_BYTE, tmp_buf, nbytes * local_size,
                                                MPI_BYTE, leader_root, leader_commptr,
                                                errflag);
                } else {
                    mpi_errno = MPIR_Scatter_MV2_Direct(sendbuf, sendcnt * local_size,
                                                        sendtype, tmp_buf,
                                                        nbytes * local_size, MPI_BYTE,
                                                        leader_root, leader_commptr,
                                                        errflag);

                }
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
        }
        /* The leaders are now done with the inter-leader part. Scatter the data within the nodes */

        if (rank == root && recvbuf == MPI_IN_PLACE) {
            mpi_errno = MPIR_Scatter_MV2_Direct(tmp_buf, nbytes, MPI_BYTE,
                                                sendbuf, sendcnt, sendtype,
                                                0, shmem_commptr, errflag);
        } else {
            mpi_errno = MPIR_Scatter_MV2_Direct(tmp_buf, nbytes, MPI_BYTE,
                                                recvbuf, recvcnt, recvtype,
                                                0, shmem_commptr, errflag);
        }
        if (mpi_errno) {
            /* for communication errors, just record the error but continue */
            *errflag = TRUE;
            MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
            MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
        }
    }

  fn_fail:
    /* check if multiple threads are calling this collective function */
    if (comm_size != local_size && local_rank == 0) {
        MPIU_Free(tmp_buf);
        if (leader_of_root == rank && root != rank) {
            MPIU_Free(leader_scatter_buf);
        }
    }
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    return (mpi_errno);
}
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_intra_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_intra_MV2(void *sendbuf,
                           int sendcnt,
                           MPI_Datatype sendtype,
                           void *recvbuf,
                           int recvcnt,
                           MPI_Datatype recvtype,
                           int root, MPID_Comm * comm_ptr, int *errflag)
{
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    int range = 0;
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
    int mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int rank, nbytes, comm_size;
    int recvtype_size, sendtype_size;
    MPIU_THREADPRIV_DECL;

    MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
    mpi_errno = PMPI_Comm_size(comm_ptr->handle, &comm_size);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    mpi_errno = PMPI_Comm_rank(comm_ptr->handle, &rank);
    if (mpi_errno) {
        MPIU_ERR_POP(mpi_errno);
    }
    MPIU_THREADPRIV_GET;

    if (rank == root) {
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        nbytes = recvcnt * recvtype_size;
    } else {
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);
        nbytes = sendcnt * sendtype_size;
    }
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    while ((range < size_scatter_tuning_table)
           && (comm_size > scatter_tuning_table[range].numproc)) {
        range++;
    }

    if (use_two_level_scatter == 1 || use_direct_scatter == 1) {
        if (range < size_scatter_tuning_table) {
            if (nbytes < scatter_tuning_table[range].small) {
                mpi_errno = MPIR_Scatter_MV2_Binomial(sendbuf, sendcnt, sendtype,
                                                      recvbuf, recvcnt, recvtype,
                                                      root, comm_ptr, errflag);
            } else if (nbytes > scatter_tuning_table[range].small
                       && nbytes < scatter_tuning_table[range].medium
                       && comm_ptr->ch.shmem_coll_ok == 1 && use_two_level_scatter == 1) {
                mpi_errno = MPIR_Scatter_MV2_two_level_Direct(sendbuf, sendcnt, sendtype,
                                                              recvbuf, recvcnt, recvtype,
                                                              root, comm_ptr, errflag);

            } else {
                mpi_errno = MPIR_Scatter_MV2_Direct(sendbuf, sendcnt, sendtype,
                                                    recvbuf, recvcnt, recvtype,
                                                    root, comm_ptr, errflag);
            }
        } else if (comm_size > scatter_tuning_table[range - 1].numproc
                   && comm_ptr->ch.shmem_coll_ok == 1 && use_two_level_scatter == 1) {
            mpi_errno = MPIR_Scatter_MV2_two_level_Binomial(sendbuf, sendcnt, sendtype,
                                                            recvbuf, recvcnt, recvtype,
                                                            root, comm_ptr, errflag);
        } else {
            mpi_errno = MPIR_Scatter_MV2_Binomial(sendbuf, sendcnt, sendtype,
                                                  recvbuf, recvcnt, recvtype,
                                                  root, comm_ptr, errflag);
        }
    } else {
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
        mpi_errno = MPIR_Scatter_MV2_Binomial(sendbuf, sendcnt, sendtype,
                                              recvbuf, recvcnt, recvtype,
                                              root, comm_ptr, errflag);
#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
    }
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */

    if (mpi_errno) {
        /* for communication errors, just record the error but continue */
        *errflag = TRUE;
        MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
        MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
    }

  fn_fail:
    /* check if multiple threads are calling this collective function */
    MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);

    return (mpi_errno);

}

#if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_)
/* begin:nested */
/* not declared static because a machine-specific function may call this one in some cases */
#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_inter_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_inter_MV2(void *sendbuf,
                           int sendcnt,
                           MPI_Datatype sendtype,
                           void *recvbuf,
                           int recvcnt,
                           MPI_Datatype recvtype,
                           int root, MPID_Comm * comm_ptr, int *errflag)
{
/*  Intercommunicator scatter.
    For short messages, root sends to rank 0 in remote group. rank 0
    does local intracommunicator scatter (binomial tree). 
    Cost: (lgp+1).alpha + n.((p-1)/p).beta + n.beta
   
    For long messages, we use linear scatter to avoid the extra n.beta.
    Cost: p.alpha + n.beta
*/

    int rank, local_size, remote_size, mpi_errno = MPI_SUCCESS;
    int mpi_errno_ret = MPI_SUCCESS;
    int i, nbytes, sendtype_size, recvtype_size;
    MPI_Status status;
    MPI_Aint extent, true_extent, true_lb = 0;
    void *tmp_buf = NULL;
    MPID_Comm *newcomm_ptr = NULL;
    MPI_Comm comm;

    if (root == MPI_PROC_NULL) {
        /* local processes other than root do nothing */
        return MPI_SUCCESS;
    }

    comm = comm_ptr->handle;
    remote_size = comm_ptr->remote_size;
    local_size = comm_ptr->local_size;

    if (root == MPI_ROOT) {
        MPID_Datatype_get_size_macro(sendtype, sendtype_size);
        nbytes = sendtype_size * sendcnt * remote_size;
    } else {
        /* remote side */
        MPID_Datatype_get_size_macro(recvtype, recvtype_size);
        nbytes = recvtype_size * recvcnt * local_size;
    }

    if (nbytes < MPIR_SCATTER_SHORT_MSG) {
        if (root == MPI_ROOT) {
            /* root sends all data to rank 0 on remote group and returns */
            MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
            mpi_errno = MPIC_Send_ft(sendbuf, sendcnt * remote_size,
                                     sendtype, 0, MPIR_SCATTER_TAG, comm, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
            goto fn_exit;
        } else {
            /* remote group. rank 0 receives data from root. need to
               allocate temporary buffer to store this data. */

            rank = comm_ptr->rank;

            if (rank == 0) {
                MPIR_Type_get_true_extent_impl(recvtype, &true_lb, &true_extent);
                MPID_Datatype_get_extent_macro(recvtype, extent);
                tmp_buf =
                    MPIU_Malloc(recvcnt * local_size * (MPIR_MAX(extent, true_extent)));
                /* --BEGIN ERROR HANDLING-- */
                if (!tmp_buf) {
                    mpi_errno =
                        MPIR_Err_create_code(MPI_SUCCESS, MPIR_ERR_RECOVERABLE, FCNAME,
                                             __LINE__, MPI_ERR_OTHER, "**nomem", 0);
                    return mpi_errno;
                }
                /* --END ERROR HANDLING-- */
                /* adjust for potential negative lower bound in datatype */
                tmp_buf = (void *) ((char *) tmp_buf - true_lb);

                MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
                mpi_errno = MPIC_Recv_ft(tmp_buf, recvcnt * local_size,
                                         recvtype, root,
                                         MPIR_SCATTER_TAG, comm, &status, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }

            }

            /* Get the local intracommunicator */
            if (!comm_ptr->local_comm)
                MPIR_Setup_intercomm_localcomm(comm_ptr);

            newcomm_ptr = comm_ptr->local_comm;

            /* now do the usual scatter on this intracommunicator */
            mpi_errno = MPIR_Scatter_MV2(tmp_buf, recvcnt, recvtype,
                                         recvbuf, recvcnt, recvtype, 0,
                                         newcomm_ptr, errflag);
            if (rank == 0)
                MPIU_Free(((char *) tmp_buf + true_lb));
        }
    } else {
        /* long message. use linear algorithm. */
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_ENTER(comm_ptr);
        if (root == MPI_ROOT) {
            MPID_Datatype_get_extent_macro(sendtype, extent);
            for (i = 0; i < remote_size; i++) {
                mpi_errno = MPIC_Send_ft(((char *) sendbuf + sendcnt * i * extent),
                                         sendcnt, sendtype, i,
                                         MPIR_SCATTER_TAG, comm, errflag);
                if (mpi_errno) {
                    /* for communication errors, just record the error but continue */
                    *errflag = TRUE;
                    MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                    MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
                }
            }
        } else {
            mpi_errno = MPIC_Recv_ft(recvbuf, recvcnt, recvtype, root,
                                     MPIR_SCATTER_TAG, comm, &status, errflag);
            if (mpi_errno) {
                /* for communication errors, just record the error but continue */
                *errflag = TRUE;
                MPIU_ERR_SET(mpi_errno, MPI_ERR_OTHER, "**fail");
                MPIU_ERR_ADD(mpi_errno_ret, mpi_errno);
            }
        }
        MPIDU_ERR_CHECK_MULTIPLE_THREADS_EXIT(comm_ptr);
    }

  fn_exit:

    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIR_Scatter_MV2
#undef FCNAME
#define FCNAME MPIU_QUOTE(FUNCNAME)
int MPIR_Scatter_MV2(void *sendbuf, int sendcnt, MPI_Datatype sendtype,
                     void *recvbuf, int recvcnt, MPI_Datatype recvtype,
                     int root, MPID_Comm * comm_ptr, int *errflag)
{
    int mpi_errno = MPI_SUCCESS;

    if (comm_ptr->comm_kind == MPID_INTRACOMM) {
        /* intracommunicator */
        mpi_errno = MPIR_Scatter_intra_MV2(sendbuf, sendcnt, sendtype,
                                           recvbuf, recvcnt, recvtype, root,
                                           comm_ptr, errflag);
        if (mpi_errno)
            MPIU_ERR_POP(mpi_errno);
    } else {
        /* intercommunicator */
        mpi_errno = MPIR_Scatter_inter_MV2(sendbuf, sendcnt, sendtype,
                                           recvbuf, recvcnt, recvtype, root,
                                           comm_ptr, errflag);
        if (mpi_errno)
            MPIU_ERR_POP(mpi_errno);
    }

  fn_exit:
    return mpi_errno;
  fn_fail:

    goto fn_exit;
}

/* end:nested */
#endif                          /* #if defined(_OSU_MVAPICH_) || defined(_OSU_PSM_) */
