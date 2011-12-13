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

#include "mpidi_ch3i_rdma_conf.h"
#include "mpidi_ch3_impl.h"
#include <mpimem.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <assert.h>
#include "pmi.h"
#include "smp_smpi.h"
#include "mpiutil.h"
#include "ch3_hwloc_bind.h"
#include "mv2_arch_hca_detect.h"
#include "coll_shmem.h"

#if defined(MAC_OSX)
#include <netinet/in.h>
#endif /* defined(MAC_OSX) */


#if defined(DEBUG)
#define DEBUG_PRINT(args...) \
    do {                                                          \
	int rank;                                                 \
	PMI_Get_rank(&rank);                                      \
	MPIU_Error_printf("[%d][%s:%d] ", rank, __FILE__, __LINE__);\
	MPIU_Error_printf(args);                                    \
    } while (0)
#else /* defined(DEBUG) */
#define DEBUG_PRINT(args...)
#endif /* defined(DEBUG) */

/* Macros for flow control and rqueues management */
#define SMPI_TOTALIN(sender,receiver)                               \
    g_smpi_shmem->rqueues_flow[sender * g_smpi.num_local_nodes + receiver]->msgs_total_in

#define SMPI_TOTALOUT(sender,receiver)                              \
    g_smpi_shmem->rqueues_flow[receiver * g_smpi.num_local_nodes + sender]->msgs_total_out

#define SMPI_CURRENT(sender,receiver)                               \
    g_smpi_shmem->rqueues_params_c[sender].current

#define SMPI_NEXT(sender,receiver)                                  \
    g_smpi_shmem->rqueues_params_n[receiver].next

#define SMPI_FIRST_S(sender,receiver)                                 \
    g_smpi_shmem->rqueues_limits_s[receiver].first

#define SMPI_LAST_S(sender,receiver)                                  \
    g_smpi_shmem->rqueues_limits_s[receiver].last

#define SMPI_FIRST_R(sender,receiver)                                 \
    g_smpi_shmem->rqueues_limits_r[sender].first

#define SMPI_LAST_R(sender,receiver)                                  \
    g_smpi_shmem->rqueues_limits_r[sender].last

struct smpi_var g_smpi;
struct shared_mem *g_smpi_shmem;
static struct shared_buffer_pool s_sh_buf_pool;
static SEND_BUF_T** s_buffer_head = NULL;
static SEND_BUF_T* s_my_buffer_head = NULL;
static char s_hostname[HOSTNAME_LEN];
int SMP_INIT = 0;
int SMP_ONLY = 0;
static void** s_current_ptr = NULL;
static int* s_current_bytes = NULL;
static int* s_total_bytes = NULL;
static char *shmem_file = NULL;
static char *pool_file = NULL;

int g_size_shmem = 0;
int g_size_pool = 0; 

/* SMP user parameters */
 
int g_smp_eagersize;
int s_smpi_length_queue;
int s_smp_num_send_buffer;
int s_smp_batch_size;

#if defined(_SMP_LIMIC_)
int limic_fd;
int g_smp_use_limic2 = 1;
static inline void adjust_lu_info(struct limic_user *lu, int old_len);
void MPIDI_CH3I_SMP_send_limic_comp(struct limic_header *l_header,
                                    MPIDI_VC_t* vc, int nb);
extern MPID_Request * create_request(void * hdr, MPIDI_msg_sz_t hdr_sz,
                                            MPIU_Size_t nb);
#endif

extern int enable_shmem_collectives;
extern struct MPIDI_CH3I_RDMA_Process_t MPIDI_CH3I_RDMA_Process;

extern int MPIDI_Get_num_nodes();
extern int rdma_set_smp_parameters(struct MPIDI_CH3I_RDMA_Process_t *proc);
extern void MPIDI_CH3I_SHMEM_COLL_Cleanup();

#if defined(MAC_OSX) || defined(_PPC64_) 
#if defined(__GNUC__)
/* can't use -ansi for vxworks ccppc or this will fail with a syntax error
 * */
#define STBAR()  asm volatile ("sync": : :"memory")     /* ": : :" for C++ */
#define READBAR() asm volatile ("sync": : :"memory")
#define WRITEBAR() asm volatile ("sync": : :"memory")
#else /* defined(__GNUC__) */
#if  defined(__IBMC__) || defined(__IBMCPP__)
extern void __iospace_eieio(void);
extern void __iospace_sync(void);
#define STBAR()   __iospace_sync ()
#define READBAR() __iospace_sync ()
#define WRITEBAR() __iospace_eieio ()
#else /* defined(__IBMC__) || defined(__IBMCPP__) */
#error Do not know how to make a store barrier for this system
#endif /* defined(__IBMC__) || defined(__IBMCPP__) */
#endif /* defined(__GNUC__) */

#if !defined(WRITEBAR)
#define WRITEBAR() STBAR()
#endif /* !defined(WRITEBAR) */
#if !defined(READBAR)
#define READBAR() STBAR()
#endif /* !defined(READBAR) */

#else /* defined(MAC_OSX) || defined(_PPC64_) */
#define WRITEBAR()
#define READBAR()
#endif /* defined(MAC_OSX) || defined(_PPC64_) */

static int smpi_exchange_info(MPIDI_PG_t *pg);
static inline SEND_BUF_T *get_buf_from_pool (void);
static inline void send_buf_reclaim (void);
static inline void put_buf_to_pool (int, int);
static inline void link_buf_to_send_queue (int, int);


/*
 * This function currently exits(-1) on error.  The semantics need to be
 * changed to gracefully set mpi_errno and exit the same as other errors.
 *
 * TODO trigger an mpi error in the calling function
 */
/*********** Declaration for locally used buffer ***********/
static inline void smpi_malloc_assert(void *ptr, char *fct, char *msg)
{
    int rank;

    if (NULL == ptr) {
        PMI_Get_rank(&rank);
        MPIU_Error_printf("Cannot Allocate Memory: [%d] in function %s, context: %s\n",
                     rank, fct, msg);
        exit(-1);
    }
}

/*
 * TODO add error handling
 */
static inline int get_host_id(char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname(myhostname);
    host_id = (unsigned long)(((struct in_addr *)
            hostent->h_addr_list[0])->s_addr);

    return host_id;
}

static inline int smpi_get_avail_length(int dest)
{
    int avail;
    WRITEBAR();
    if (SMPI_TOTALIN(g_smpi.my_local_id, dest) >=
        SMPI_TOTALOUT(g_smpi.my_local_id, dest)) {
    WRITEBAR();
    avail = (g_smpi.available_queue_length -
        (SMPI_TOTALIN(g_smpi.my_local_id, dest) -
         SMPI_TOTALOUT(g_smpi.my_local_id, dest)));
    } else {
    WRITEBAR();
    avail = g_smpi.available_queue_length -
        (SMPI_MAX_INT - SMPI_TOTALOUT(g_smpi.my_local_id, dest) +
         SMPI_TOTALIN(g_smpi.my_local_id, dest));
    }

    avail = ((avail / (int) SMPI_CACHE_LINE_SIZE) * SMPI_CACHE_LINE_SIZE) -
    SMPI_CACHE_LINE_SIZE;

    if (avail < 0) avail = 0;
    return avail;
}

static inline void smpi_complete_send(unsigned int my_id,
    unsigned int destination,
    unsigned int length)
{
    SMPI_NEXT(my_id, destination) += SMPI_ALIGN(length);

    if (SMPI_NEXT(my_id, destination) > SMPI_LAST_S(my_id, destination)) {
    SMPI_NEXT(my_id, destination) = SMPI_FIRST_S(my_id, destination);
    }
    WRITEBAR();
    SMPI_TOTALIN(my_id, destination) += SMPI_ALIGN(length);

}

static inline void smpi_complete_recv(unsigned int from_grank,
    unsigned int my_id,
    unsigned int length)
{
    SMPI_CURRENT(from_grank, my_id) += SMPI_ALIGN(length);

    if (SMPI_CURRENT(from_grank, my_id) > SMPI_LAST_R(from_grank, my_id)) {
    SMPI_CURRENT(from_grank, my_id) = SMPI_FIRST_R(from_grank, my_id);
    }
    WRITEBAR();
    SMPI_TOTALOUT(from_grank, my_id) += SMPI_ALIGN(length);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_process_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#if defined(_SMP_LIMIC_)
static inline int MPIDI_CH3I_SMP_Process_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t* pkt, int* index, 
                  struct limic_header *l_header, int *use_limic)
#else
static inline int MPIDI_CH3I_SMP_Process_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t* pkt, int* index)
#endif
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
    int mpi_errno = MPI_SUCCESS;

#if defined(_SMP_LIMIC_)
    if (MPIDI_CH3_PKT_LIMIC_COMP == pkt->type)
    {
        /* convert to MPIDI_CH3_Pkt_limic_comp_t */
        MPIDI_CH3_Pkt_limic_comp_t *lc_pkt = (MPIDI_CH3_Pkt_limic_comp_t *)pkt;
        MPID_Request *sreq = (MPID_Request *)(lc_pkt->send_req_id);
        
        int nb = lc_pkt->nb;
        int complete = 0;

        if (MPIDI_CH3I_Request_adjust_iov(sreq, nb)) {
            MPIDI_CH3U_Handle_send_req(vc, sreq, &complete);
        }
        MPIU_Assert(complete);
   
        return mpi_errno;
    }
#endif

    if (MPIDI_CH3_PKT_RNDV_R3_DATA == pkt->type)
    {
        MPIDI_CH3_Pkt_rndv_r3_data_t * pkt_header = (MPIDI_CH3_Pkt_rndv_r3_data_t*) pkt;

#if defined(_SMP_LIMIC_)
        /* This is transferred through limic2, retrieve related info */
        if (pkt_header->send_req_id) {
            *use_limic = 1;
            MPIU_Memcpy(&(l_header->lu), s_current_ptr[vc->smp.local_nodes], sizeof(limic_user));
         
            s_current_ptr[vc->smp.local_nodes] = (void*)(
                (unsigned long) s_current_ptr[vc->smp.local_nodes]
                + sizeof(limic_user));

            l_header->total_bytes = *((int *)s_current_ptr[vc->smp.local_nodes]);
            l_header->send_req_id = (MPID_Request *)(pkt_header->send_req_id);

            s_current_ptr[vc->smp.local_nodes] = (void*)(
                (unsigned long) s_current_ptr[vc->smp.local_nodes]
                + sizeof(int));

            s_current_bytes[vc->smp.local_nodes] = s_current_bytes[vc->smp.local_nodes] -
                sizeof(struct limic_user) - sizeof(int);
        } else {
#endif
            if ((*index = pkt_header->src.smp_index) == -1)
            {
                MPIU_ERR_SETFATALANDJUMP1(
                    mpi_errno,
                    MPI_ERR_OTHER,
                    "**fail",
                    "**fail %s",
                    "*index == -1"
                );
            }
#if defined(_SMP_LIMIC_)
        }
#endif
    vc->smp.recv_current_pkt_type = SMP_RNDV_MSG;

    MPID_Request* rreq = NULL;
        MPID_Request_get_ptr(((MPIDI_CH3_Pkt_rndv_r3_data_t*) pkt)->receiver_req_id, rreq);
    DEBUG_PRINT("R3 data received, don't need to proceed\n");
    vc->smp.recv_active = rreq;
    goto fn_exit;
    }

#if defined(CKPT)
    /*
     * Handle the MPIDI_CH3_PKT_CM_SUSPEND packet
     * for the shared memory channel
     */
    else if (pkt->type == MPIDI_CH3_PKT_CM_SUSPEND) {
		vc->mrail.suspended_rails_recv++;
		DEBUG_PRINT("%s (pid %p):[%d <= %d]: get CM_SUSPEND vcstate=%d, send=%d,recv=%d\n", __func__, 
			pthread_self(), MPIDI_Process.my_pg_rank, vc->pg_rank, vc->ch.state,
			vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv );

		if( vc->mrail.suspended_rails_send > 0 && 
			vc->mrail.suspended_rails_recv > 0 )
		{
			vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
			vc->mrail.suspended_rails_send = 0;
			vc->mrail.suspended_rails_recv = 0;
			DEBUG_PRINT("%s [%d vc_%d]: turn to SUSPENDED\n", __func__, 
				MPIDI_Process.my_pg_rank, vc->pg_rank );
		}
		else{
		}
	    /*** if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDING) {
    	    cm_send_suspend_msg(vc);
	        vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
    	} ***/
	    goto fn_exit;
    }

    /*
     * Handle the MPIDI_CH3_PKT_CM_REACTIVATION_DONE packet
     * for the shared memory channel
     */
    else if (pkt->type == MPIDI_CH3_PKT_CM_REACTIVATION_DONE) {
		DEBUG_PRINT("%s (pid %p):[%d <= %d]: get CM_REACT, vcstate=%d\n", __func__, 
			pthread_self(), MPIDI_Process.my_pg_rank, vc->pg_rank, vc->ch.state);
    if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED) {
        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    }
    vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
    goto fn_exit;
    }
#endif /* defined(CKPT) */

    if (pkt->type == MPIDI_CH3_PKT_EAGER_SEND_CONTIG) {
        MPIDI_msg_sz_t buflen = s_total_bytes[vc->smp.local_nodes] -
            sizeof(MPIDI_CH3_Pkt_eager_send_t);
        if ((mpi_errno = MPIDI_CH3_PktHandler_EagerSend_Contig(
                        vc,
                        pkt,
                        &buflen,
                        &vc->smp.recv_active)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        if (!vc->smp.recv_active) {
            s_current_ptr[vc->smp.local_nodes] = NULL;
            s_current_bytes[vc->smp.local_nodes] = 0;        
            smpi_complete_recv(vc->smp.local_nodes,
                    g_smpi.my_local_id,
                    s_total_bytes[vc->smp.local_nodes] +
                    sizeof(int));
            s_total_bytes[vc->smp.local_nodes] = 0;
            goto fn_exit;
        }
    } else {
        MPIDI_msg_sz_t buflen = sizeof(MPIDI_CH3_Pkt_t);

        if ((mpi_errno = MPIDI_CH3U_Handle_recv_pkt(
                        vc,
                        pkt,
                        &buflen,
                        &vc->smp.recv_active)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }

        vc->smp.recv_current_pkt_type = SMP_EAGER_MSG;
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_PROGRESS_HEADER);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_write_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_write_progress(MPIDI_PG_t *pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    int mpi_errno = MPI_SUCCESS;
    int nb;
    int i = 0;
    MPIDI_VC_t *vc;
    int complete;

    for (i=0; i < g_smpi.num_local_nodes; ++i)
    {
        MPIDI_PG_Get_vc(pg, g_smpi.l2g_rank[i], &vc);

#if defined(CKPT)
        /* Don't touch a suspended channel */
        if (vc->ch.state == MPIDI_CH3I_VC_STATE_SUSPENDED)
            continue;
#endif /* defined(CKPT) */

        vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
/*
        if (vc->smp.send_active)
*/
        while (vc->smp.send_active != NULL) {
                MPID_Request *req = vc->smp.send_active;

                if(req->dev.iov_offset >= req->dev.iov_count) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s",
                    "req->dev.iov_offset >= req->dev.iov_count");
                }

                switch (req->ch.reqtype)
                {
                case REQUEST_RNDV_R3_HEADER:
                    vc->smp.send_current_pkt_type = SMP_RNDV_MSG;
                            MPIDI_CH3I_SMP_writev_rndv_header(
                                vc,
                                req->dev.iov + req->dev.iov_offset,
                                req->dev.iov_count - req->dev.iov_offset,
                                &nb
                            );
                        break;
                    case REQUEST_RNDV_R3_DATA:
                            if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
                            {
                                mpi_errno = MPIDI_CH3I_SMP_writev_rndv_data(
                                    vc,
                                    req->dev.iov + req->dev.iov_offset,
                                    req->dev.iov_count - req->dev.iov_offset,
                                    &nb
                               );
                            }
                            else
                            {
                                MPIDI_CH3I_SMP_writev_rndv_data_cont(
                                    vc,
                                    req->dev.iov + req->dev.iov_offset,
                                    req->dev.iov_count - req->dev.iov_offset,
                                    &nb
                                );
                            }
                        break;
                    default:
                            MPIDI_CH3I_SMP_writev(
                                vc,
                                req->dev.iov + req->dev.iov_offset,
                                req->dev.iov_count - req->dev.iov_offset,
                                &nb
                            );
                        break;
            }

            if (mpi_errno != MPI_SUCCESS)
            {
                MPIU_ERR_POP(mpi_errno);
            }

            DEBUG_PRINT("shm_writev returned %d", nb);

            if (nb > 0)
            {
                    if (MPIDI_CH3I_Request_adjust_iov(req, nb))
                    {
                        /* Write operation complete */
                                if ((mpi_errno = MPIDI_CH3U_Handle_send_req(vc, req, &complete)) != MPI_SUCCESS)
                                {
                                    MPIU_ERR_POP(mpi_errno);
                                }

                        if (complete) {
                            req->ch.reqtype = REQUEST_NORMAL;
							if( !MPIDI_CH3I_SMP_SendQ_empty(vc) ){
                            	MPIDI_CH3I_SMP_SendQ_dequeue(vc);
							}
                            DEBUG_PRINT("Dequeue request from sendq %p, now head %p\n",
                                req, vc->smp.sendq_head);
					#ifdef CKPT
						MPIDI_CH3I_MRAILI_Pkt_comm_header* p = 
								(MPIDI_CH3I_MRAILI_Pkt_comm_header*)(&(req->dev.pending_pkt));
						if( p->type >= MPIDI_CH3_PKT_CM_SUSPEND && 
							p->type <= MPIDI_CH3_PKT_CR_REMOTE_UPDATE ){
							DEBUG_PRINT("%s [%d vc_%d]: imm-write msg %s(%d)\n", __func__,
							MPIDI_Process.my_pg_rank, vc->pg_rank, 
							MPIDI_CH3_Pkt_type_to_string[p->type],p->type );
						}
						if( p->type == MPIDI_CH3_PKT_CM_SUSPEND ){
							vc->mrail.suspended_rails_send++;
						//	printf("%s: [%d vc_%d]: imm-write SUSP_MSG, send=%d, recv=%d\n", 
						//		__func__, MPIDI_Process.my_pg_rank, vc->pg_rank, 
						//	vc->mrail.suspended_rails_send, vc->mrail.suspended_rails_recv);
							if( vc->mrail.suspended_rails_send > 0 &&
								vc->mrail.suspended_rails_recv > 0 )
							{
								vc->ch.state = MPIDI_CH3I_VC_STATE_SUSPENDED;
								vc->mrail.suspended_rails_send = 0;
								vc->mrail.suspended_rails_recv =0;
				DEBUG_PRINT("%s[%d <= %d]:turn to SUSPENDED, send-act=%p, sendq-head=%p\n", __func__, 
									MPIDI_Process.my_pg_rank, vc->pg_rank, 
									vc->smp.send_active, vc->smp.sendq_head );
							}
						}
					#endif
                        } else {
                            if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
                                vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;
                        }
                        vc->smp.send_active = MPIDI_CH3I_SMP_SendQ_head(vc);
                    } else {
                        if (vc->smp.send_current_pkt_type == SMP_RNDV_MSG)
                            vc->smp.send_current_pkt_type = SMP_RNDV_MSG_CONT;

                        MPIDI_DBG_PRINTF((65, FCNAME,
                            "iovec updated by %d bytes but not complete",
                            nb));

                        if(req->dev.iov_offset >= req->dev.iov_count) {
                            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                                    "**fail", "**fail %s",
                                    "req->dev.iov_offset >= req->dev.iov_count");
                        }

                        break;
                    }
            } else {
                MPIDI_DBG_PRINTF((65, FCNAME,
                        "shm_post_writev returned %d bytes",
                        nb));
                break;
            }
        } /* while (vc->smp.send_active != NULL) */
    } /* for (i=0; i < g_smpi.num_local_nodes; ++i) */

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_WRITE_PROGRESS);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}


#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_read_progress
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_read_progress (MPIDI_PG_t* pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
    int mpi_errno = MPI_SUCCESS;
    MPIDI_VC_t* vc = NULL;
    MPIDI_CH3_Pkt_t* pkt_head = NULL;
    size_t nb = 0;
    int complete = 0;
    int i = 0;
    int index = -1;
#if defined(_SMP_LIMIC_)
    struct limic_header l_header;
    int use_limic = 0;
#endif
    int from;

    for (i=1; i < g_smpi.num_local_nodes; ++i)
    {
        from = (g_smpi.my_local_id + i) % g_smpi.num_local_nodes;

        MPIDI_PG_Get_vc(pg, g_smpi.l2g_rank[from], &vc);

        if (!vc->smp.recv_active)
        {
            MPIDI_CH3I_SMP_pull_header(vc, &pkt_head);

            if (pkt_head)
            {
#if defined(_SMP_LIMIC_)
                mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head, &index, &l_header, &use_limic);
#else
                mpi_errno = MPIDI_CH3I_SMP_Process_header(vc, pkt_head, &index);
#endif
                if (mpi_errno!=MPI_SUCCESS)
                {
                    MPIU_ERR_POP(mpi_errno);
                }
            }
        }

        if (vc->smp.recv_active)
        {
            switch(vc->smp.recv_current_pkt_type)
            {
            case SMP_RNDV_MSG:
#if defined(_SMP_LIMIC_)
                    mpi_errno = MPIDI_CH3I_SMP_readv_rndv(
                        vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count - vc->smp.recv_active->dev.iov_offset,
                        index,
                        &l_header,
                        &nb, use_limic
                    );
#else
                    mpi_errno = MPIDI_CH3I_SMP_readv_rndv(
                        vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count - vc->smp.recv_active->dev.iov_offset,
                        index,
                        &nb
                    );
#endif
                break;
            case SMP_RNDV_MSG_CONT:
#if defined(_SMP_LIMIC_)
                mpi_errno = MPIDI_CH3I_SMP_readv_rndv_cont(vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->dev.iov_offset, index, &vc->smp.current_l_header,
                        &nb, vc->smp.use_limic);
#else
                mpi_errno = MPIDI_CH3I_SMP_readv_rndv_cont(vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->dev.iov_offset, index, &nb);
#endif
                        break;
            default:
                        mpi_errno = MPIDI_CH3I_SMP_readv(vc,
                        &vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset],
                        vc->smp.recv_active->dev.iov_count -
                        vc->smp.recv_active->dev.iov_offset, &nb);
                        break;
            }

            if (mpi_errno)
            {
                MPIU_ERR_POP(mpi_errno);
            }

        DEBUG_PRINT("request to fill: iovlen %d, iov[0].len %d, [1] %d, nb %d\n",
            vc->smp.recv_active->dev.iov_count -
            vc->smp.recv_active->dev.iov_offset,
            vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset].MPID_IOV_LEN,
            vc->smp.recv_active->dev.iov[vc->smp.recv_active->dev.iov_offset + 1].MPID_IOV_LEN, nb);
        if (nb > 0) {
        if (MPIDI_CH3I_Request_adjust_iov(vc->smp.recv_active, nb)) {
            DEBUG_PRINT("adjust iov finished, handle req\n");
            mpi_errno = MPIDI_CH3U_Handle_recv_req(vc, vc->smp.recv_active, &complete);
            DEBUG_PRINT("finished handle req, complete %d\n", complete);

            if(mpi_errno) MPIU_ERR_POP(mpi_errno);

            if (complete) {
#if defined(_SMP_LIMIC_)
                        /* send completion message with sender's send request
                         * and number of bytes received.
                         * header type is MPIDI_CH3_PKT_LIMIC_COMP
                         */
                        if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG && use_limic) {
                            MPIDI_CH3I_SMP_send_limic_comp(&l_header, vc, nb);
                        } else if (vc->smp.recv_current_pkt_type == SMP_RNDV_MSG_CONT &&
                                   vc->smp.use_limic) {
                            vc->smp.current_nb += nb;
                            MPIDI_CH3I_SMP_send_limic_comp(&vc->smp.current_l_header, vc, vc->smp.current_nb);
                        }
#endif
                        vc->smp.recv_active = NULL;
            } else {
                        if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG) {
                            vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;
#if defined(_SMP_LIMIC_)
                            if (use_limic) {
                                vc->smp.current_l_header = l_header;
                                vc->smp.current_nb = nb;
                                vc->smp.use_limic = 1;
                            } else {
                                vc->smp.use_limic = 0;
                            }
#endif
                         }
#if defined(_SMP_LIMIC_)
                        else {
                            if (vc->smp.use_limic) {
                                vc->smp.current_nb += nb;
                            }
                        }
#endif
                    }
        } else {
#if defined(_SMP_LIMIC_)
                    MPIU_Assert(vc->smp.recv_current_pkt_type != SMP_RNDV_MSG ||
                        !use_limic);
#endif
            if(vc->smp.recv_current_pkt_type == SMP_RNDV_MSG) {
                vc->smp.recv_current_pkt_type = SMP_RNDV_MSG_CONT;

#if defined(_SMP_LIMIC_)
                if (use_limic) {
                    vc->smp.use_limic = 1;
                    vc->smp.current_l_header = l_header;
                    vc->smp.current_nb = nb;
                } else {
                    vc->smp.use_limic = 0;
                }
#endif
            }
        }
        }
    }
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_READ_PROGRESS);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

void MPIDI_CH3I_SMP_unlink() 
{ 
    /*clean up pool file*/
    if (g_smpi.fd_pool != -1) { 
        unlink(pool_file);
    } 
    if (pool_file != NULL) {
        MPIU_Free(pool_file);
    }
    pool_file = NULL;

    /*clean up shmem file*/
    if (g_smpi.fd != -1) { 
        unlink(shmem_file);
    }
    if (shmem_file != NULL) { 
        MPIU_Free(shmem_file);
    }
    shmem_file = NULL;
}

void MPIDI_CH3I_set_smp_only()
{
    char *value;

    g_smpi.only_one_device = 0;
    SMP_ONLY = 0;
    if (MPIDI_CH3I_Process.has_dpm) {
        return;
    }
    
    if ((value = getenv("MV2_USE_SHARED_MEM")) != NULL) {
        rdma_use_smp = !!atoi(value);
    }

    if ((value = getenv("MV2_USE_BLOCKING")) != NULL) {
        rdma_use_blocking = !!atoi(value);
    }

    if (MPIDI_Get_num_nodes() == 1) {
        if(!rdma_use_smp || rdma_use_blocking) {
            return;
        }
        g_smpi.only_one_device = 1;
        SMP_ONLY = 1;
    }
}

void MPIDI_CH3I_SMP_Init_VC(MPIDI_VC_t *vc)
{
    /*initialize RNDV parameter*/
    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow  = NULL;
    vc->mrail.inflow    = 0;
}

void MPIDI_CH3I_SMP_cleanup() 
{ 
    /*clean up pool file*/
    if (g_smpi.send_buf_pool_ptr != NULL) {
        munmap(g_smpi.send_buf_pool_ptr, g_size_pool); 
    }
    if (g_smpi.fd_pool != -1) { 
        close(g_smpi.fd_pool);
        unlink(pool_file);
    } 
    if (pool_file != NULL) {
        MPIU_Free(pool_file);
    }
    g_smpi.send_buf_pool_ptr = NULL;
    g_smpi.fd_pool = -1;
    pool_file = NULL;

    /*clean up shmem file*/
    if (g_smpi.mmap_ptr != NULL) { 
        munmap(g_smpi.mmap_ptr, g_size_shmem);        
    }
    if (g_smpi.fd != -1) { 
        close(g_smpi.fd);
        unlink(shmem_file);
    }
    if (shmem_file != NULL) { 
        MPIU_Free(shmem_file);
    }
    g_smpi.mmap_ptr = NULL;
    g_smpi.fd = -1;
    shmem_file = NULL;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_Init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_init(MPIDI_PG_t *pg)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_INIT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_INIT);
    int mpi_errno = MPI_SUCCESS;
    unsigned int i, j, pid, wait;
    int sh_size, pid_len, rq_len;
    struct stat file_status;
    struct stat file_status_pool;
    int pagesize = getpagesize();
    char *value;
    struct shared_mem *shmem;
    SEND_BUF_T *send_buf = NULL;
#if defined(SOLARIS)
    char *setdir="/tmp";
#else
    char *setdir="/dev/shm";
#endif
    char *shmem_dir, *shmdir;
    size_t pathlen;
#if defined(_X86_64_)
    volatile char tmpchar;
#endif /* defined(_X86_64_) */
    
    /* Set SMP params based on architecture */
    rdma_set_smp_parameters(&MPIDI_CH3I_RDMA_Process);

    if(rdma_use_blocking) {
        /* blocking is enabled, so
         * automatically disable
         * shared memory */
        return MPI_SUCCESS;
    }

    if (!rdma_use_smp) {
#if defined(HAVE_LIBHWLOC)
        /* Shared Memory is turned off. We cannot do 
         * any CPU Affinity in this case */ 
        mv2_enable_affinity = 0;
#endif
        return MPI_SUCCESS;
    }

    if(MPIDI_CH3I_Process.has_dpm) {
        return MPI_SUCCESS;
    }

    /*
     * Do the initializations here. These will be needed on restart
     * after a checkpoint has been taken.
     */

    if ((shmdir = getenv("MV2_SHMEM_DIR")) != NULL) {
        shmem_dir = shmdir;
    } else {
        shmem_dir = setdir;
    }
    pathlen = strlen(shmem_dir);
#if defined(_SMP_LIMIC_)
    if ((value = getenv("MV2_SMP_USE_LIMIC2")) != NULL) {
        g_smp_use_limic2 = atoi(value);
    }
#endif

#if defined(HAVE_LIBHWLOC)
    if ((value = getenv("MV2_ENABLE_AFFINITY")) != NULL) {
        mv2_enable_affinity = atoi(value);
    }

    if (mv2_enable_affinity && (value = getenv("MV2_CPU_MAPPING")) != NULL) {
        /* Affinity is on and the user has supplied a cpu mapping string */
        int linelen = strlen(value);
        if (linelen < s_cpu_mapping_line_max)
        {
            s_cpu_mapping_line_max = linelen;
        }
        s_cpu_mapping = (char*) MPIU_Malloc(sizeof(char) * (s_cpu_mapping_line_max + 1));
        strncpy(s_cpu_mapping, value, s_cpu_mapping_line_max);
        s_cpu_mapping[s_cpu_mapping_line_max] = '\0';
    } 

    if(mv2_enable_affinity && (value = getenv("MV2_CPU_MAPPING")) == NULL ) {
        /* Affinity is on and the user has not specified a mapping string */
        if ((value = getenv("MV2_CPU_BINDING_POLICY")) != NULL) {
            /* User has specified a binding policy */
            if (!strcmp(value, "bunch")  ||  !strcmp(value, "BUNCH")) {
                    policy = POLICY_BUNCH;
            } else if (!strcmp(value, "scatter") || !strcmp(value, "SCATTER")) {
                    policy = POLICY_SCATTER;
            } else {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                                                    "**fail", "**fail %s",
                                                    "CPU_BINDING_PRIMITIVE: Policy should be bunch or scatter.");
            } 
         } else {
            /* User has not specified a binding policy.
             * We are going to do "bunch" binding, by default  */
            policy = POLICY_BUNCH;
         }
    } 
#endif  /* defined(HAVE_LIBHWLOC) */

    if (gethostname(s_hostname, sizeof(char) * HOSTNAME_LEN) < 0) {
       MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
           "%s: %s", "gethostname", strerror(errno));
    }

    DEBUG_PRINT("gethostname: %s\n", s_hostname);

    if ((mpi_errno = smpi_exchange_info(pg)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

    DEBUG_PRINT("finished exchange info\n");

    /* Convert to bytes */
    g_smp_eagersize = g_smp_eagersize + 1;

#if defined(DEBUG)
    int my_rank;
    PMI_Get_rank(&my_rank);

    if (my_rank == 0)
    {
        DEBUG_PRINT("smp eager size %d\n, smp queue length %d\n",
            g_smp_eagersize, s_smpi_length_queue);
    }
#endif /* defined(DEBUG) */

    if (g_smp_eagersize > s_smpi_length_queue / 2) {
       MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
           "**fail %s", "SMP_EAGERSIZE should not exceed half of "
           "SMPI_LENGTH_QUEUE. Note that SMP_EAGERSIZE "
           "and SMPI_LENGTH_QUEUE are set in KBytes.");
    }

    g_smpi.fd = -1;
    g_smpi.fd_pool = -1; 
    g_smpi.mmap_ptr = NULL; 
    g_smpi.send_buf_pool_ptr = NULL;
    g_smpi.available_queue_length =
          (s_smpi_length_queue - g_smp_eagersize - sizeof(int));

    /* add pid for unique file name */
    shmem_file =
        (char *) MPIU_Malloc(sizeof(char) * (pathlen + HOSTNAME_LEN + 26 + PID_CHAR_LEN));
    if(!shmem_file) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
            "**nomem %s", "shmem_file");
    }

    pool_file =
        (char *) MPIU_Malloc (sizeof (char) * (pathlen + HOSTNAME_LEN + 26 + PID_CHAR_LEN));
    if(!pool_file) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
            "**nomem %s", "pool_file");
    }

    /* unique shared file name */
    sprintf(shmem_file, "%s/ib_shmem-%s-%s-%d.tmp",
        shmem_dir, pg->ch.kvs_name, s_hostname, getuid());
    DEBUG_PRINT("shemfile %s\n", shmem_file);

    sprintf (pool_file, "%s/ib_pool-%s-%s-%d.tmp", shmem_dir, pg->ch.kvs_name,
        s_hostname, getuid ());
    DEBUG_PRINT("shemfile %s\n", pool_file);

    /* open the shared memory file */
    g_smpi.fd = open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (g_smpi.fd < 0) {
        /* fallback */
        sprintf(shmem_file, "/tmp/ib_shmem-%s-%s-%d.tmp",
                pg->ch.kvs_name, s_hostname, getuid());

        DEBUG_PRINT("shemfile %s\n", shmem_file);

        sprintf (pool_file, "/tmp/ib_pool-%s-%s-%d.tmp", pg->ch.kvs_name,
                s_hostname, getuid ());
        DEBUG_PRINT("shemfile %s\n", pool_file);

        g_smpi.fd =
            open(shmem_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
        if (g_smpi.fd < 0) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "%s: %s", "open", strerror(errno));
        }
    }

    g_smpi.fd_pool =
        open (pool_file, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
    if (g_smpi.fd_pool < 0) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                    FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                    "%s: %s", "open",
                    strerror(errno)); 
        goto cleanup_files;
    }

    /* compute the size of this file */
    pid_len = g_smpi.num_local_nodes * sizeof(int);
    /* pid_len need to be padded to cache aligned, in order to make sure the
     * following flow control structures cache aligned. */
    pid_len = pid_len + SMPI_CACHE_LINE_SIZE - (pid_len % SMPI_CACHE_LINE_SIZE);
    rq_len = sizeof(smpi_rqueues) * g_smpi.num_local_nodes *
        (g_smpi.num_local_nodes - 1);
    sh_size = sizeof(struct shared_mem) + pid_len 
          + SMPI_ALIGN(rq_len) + SMPI_CACHE_LINE_SIZE * 2;

    g_size_shmem = (SMPI_CACHE_LINE_SIZE + sh_size + pagesize 
          + (g_smpi.num_local_nodes * (g_smpi.num_local_nodes - 1) 
          * (SMPI_ALIGN(s_smpi_length_queue + pagesize))));

    DEBUG_PRINT("sizeof shm file %d\n", g_size_shmem);

    g_size_pool =
    SMPI_ALIGN (sizeof (SEND_BUF_T) * s_smp_num_send_buffer +
        pagesize) * g_smpi.num_local_nodes + SMPI_CACHE_LINE_SIZE;
	
    DEBUG_PRINT("sizeof pool file %d\n", g_size_pool);
    if (g_smpi.my_local_id == 0) 
    {
       DEBUG_PRINT("%s[%d]: size_shmem=%d, size_pool = %d\n", 
          __func__, MPIDI_Process.my_pg_rank, g_size_shmem, g_size_pool);
    }
    /* initialization of the shared memory file */
    /* just set size, don't really allocate memory, to allow intelligent memory
     * allocation on NUMA arch */
    if (g_smpi.my_local_id == 0) {
       if (ftruncate(g_smpi.fd, 0)) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER, 
                       FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                       "%s: %s", "ftruncate",
                       strerror(errno));
           goto cleanup_files;
       }
 
       /* set file size, without touching pages */
       if (ftruncate(g_smpi.fd, g_size_shmem)) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                       FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                       "%s: %s", "ftruncate",
                       strerror(errno));
           goto cleanup_files;
       }
 
       if (ftruncate (g_smpi.fd_pool, 0)) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                       FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                       "%s: %s", "ftruncate",
                       strerror(errno)); 
           goto cleanup_files;
       }
 
       if (ftruncate (g_smpi.fd_pool, g_size_pool)) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                       FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                       "%s: %s", "ftruncate",
                       strerror(errno)); 
           goto cleanup_files;
       }

#if !defined(_X86_64_)
       {
           char *buf;
           buf = (char *) MPIU_Calloc(g_size_shmem + 1, sizeof(char));
           if (write(g_smpi.fd, buf, g_size_shmem) != g_size_shmem) {
              mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                        FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 
                       "%s: %s", "write",
                       strerror(errno));
              MPIU_Free(buf);
              goto cleanup_files;
           }
           MPIU_Free(buf);
       }
 
       {
           char *buf;
           buf = (char *) MPIU_Calloc (g_size_pool + 1, sizeof (char));
           if (write (g_smpi.fd_pool, buf, g_size_pool) != g_size_pool) {
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

       if (lseek(g_smpi.fd, 0, SEEK_SET) != 0) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                      "%s: %s", "lseek",
                      strerror(errno)); 
           goto cleanup_files;
       }
 
       if (lseek (g_smpi.fd_pool, 0, SEEK_SET) != 0) {
           /* to clean up tmp shared file */
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                      FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 
                       "%s: %s", "lseek",
                       strerror(errno)); 
           goto cleanup_files;
       }
    }

    if (enable_shmem_collectives) {
        /* Shared memory for collectives */
        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_init(pg, g_smpi.my_local_id)) != MPI_SUCCESS)
        {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                   FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", 
                   "%s", "SHMEM_COLL_init failed");
            goto cleanup_files;
        }
    }

    DEBUG_PRINT("process arrives before sync stage\n");
    /* synchronization between local processes */
    do {
       if (fstat(g_smpi.fd, &file_status) != 0 ||
           fstat (g_smpi.fd_pool, &file_status_pool) != 0) {
             /* to clean up tmp shared file */
             mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                   FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                   "%s: %s", "fstat",
                   strerror(errno));
             goto cleanup_files;
       }
       usleep(1);
    } while (file_status.st_size != g_size_shmem ||
         file_status_pool.st_size != g_size_pool);

    g_smpi_shmem = (struct shared_mem *) MPIU_Malloc(sizeof(struct shared_mem));
    if(!g_smpi_shmem) {
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
       goto cleanup_files;
    }

    DEBUG_PRINT("before mmap\n");

    /* mmap of the shared memory file */
    g_smpi.mmap_ptr = mmap(0, g_size_shmem,
        (PROT_READ | PROT_WRITE), (MAP_SHARED), g_smpi.fd, 0);
    if (g_smpi.mmap_ptr == (void *) -1) {
       /* to clean up tmp shared file */
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", "%s: %s", 
                "mmap", strerror(errno));
       goto cleanup_files;
    }

    g_smpi.send_buf_pool_ptr = mmap (0, g_size_pool, 
        (PROT_READ | PROT_WRITE), (MAP_SHARED), g_smpi.fd_pool, 0);
    if (g_smpi.send_buf_pool_ptr == (void *) -1) {
       /* to clean up tmp shared file */
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", "%s: %s", 
                "mmap", strerror(errno));
       goto cleanup_files;
    }

    shmem = (struct shared_mem *) g_smpi.mmap_ptr;
    if (((long) shmem & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
       /* to clean up tmp shared file */
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", "%s", 
                "error in shifting mmaped shmem");
       goto cleanup_files;
    }

    s_buffer_head = (SEND_BUF_T **) MPIU_Malloc(sizeof(SEND_BUF_T *) * g_smpi.num_local_nodes);
    if(!s_buffer_head) {
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
       goto cleanup_files;
    }

    for(i=0; i < g_smpi.num_local_nodes; ++i) {
       s_buffer_head[i] = (SEND_BUF_T *)((unsigned long)g_smpi.send_buf_pool_ptr +
           SMPI_ALIGN(sizeof(SEND_BUF_T) * s_smp_num_send_buffer +
               pagesize) * i);

       if (((long) s_buffer_head[i] & (SMPI_CACHE_LINE_SIZE - 1)) != 0) {
          /* to clean up tmp shared file */
          mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                   FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s", 
                   "error in shifting mmaped pool");
          goto cleanup_files;
       }
    }
    s_my_buffer_head = s_buffer_head[g_smpi.my_local_id];

    s_sh_buf_pool.free_head = 0;

    s_sh_buf_pool.send_queue = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);
    if(!s_sh_buf_pool.send_queue) {
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
       goto cleanup_files;
    }

    s_sh_buf_pool.tail = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);
    if(!s_sh_buf_pool.tail) {
       mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
       goto cleanup_files;
    }

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
       s_sh_buf_pool.send_queue[i] = s_sh_buf_pool.tail[i] = -1;
    }

#if defined(_X86_64_)
    for (i = 0; i < s_smp_num_send_buffer; ++i) {
       send_buf =&(s_my_buffer_head[i]);
       send_buf->myindex = i;
       send_buf->next = i+1;
       send_buf->busy = 0;
       send_buf->len = 0;
       send_buf->has_next = 0;
       send_buf->msg_complete = 0;

       for (j = 0; j < SMP_SEND_BUF_SIZE; j += pagesize) {
           tmpchar = (send_buf->buf)[j];
       }
    }
    send_buf->next = -1;
#else /* defined(_X86_64_) */
    if (0 == g_smpi.my_local_id) {
       for(j = 0; j < g_smpi.num_local_nodes; ++j){
           for (i = 0; i < s_smp_num_send_buffer; ++i) {
              send_buf = &s_buffer_head[j][i];
              send_buf->myindex = i;
              send_buf->next = i+1;
              send_buf->busy = 0;
              send_buf->len = 0;
              send_buf->has_next = 0;
              send_buf->msg_complete = 0;
           }
           send_buf->next = -1;
       }
    }
#endif /* defined(_X86_64_) */

    /* Initialize shared_mem pointers */
    g_smpi_shmem->pid = (int *) shmem;

    g_smpi_shmem->rqueues_params_c =
        (smpi_params_c *) MPIU_Malloc(sizeof(smpi_params_c)*g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_params_n =
        (smpi_params_n *) MPIU_Malloc(sizeof(smpi_params_n)*g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_flow =
        (smpi_rqueues **) MPIU_Malloc(sizeof(smpi_rqueues *)*g_smpi.num_local_nodes * g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_limits_s =
        (smpi_rq_limit *) MPIU_Malloc(sizeof(smpi_rq_limit)*g_smpi.num_local_nodes);
    g_smpi_shmem->rqueues_limits_r =
        (smpi_rq_limit *) MPIU_Malloc(sizeof(smpi_rq_limit)*g_smpi.num_local_nodes);

    if (g_smpi_shmem->rqueues_params_c == NULL ||
        g_smpi_shmem->rqueues_params_n == NULL ||
        g_smpi_shmem->rqueues_flow == NULL ||
        g_smpi_shmem->rqueues_limits_s == NULL ||
        g_smpi_shmem->rqueues_limits_r == NULL ) {
         mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                  FCNAME, __LINE__, MPI_ERR_OTHER, "**nomem", 0);
         goto cleanup_files;
    }

    if (g_smpi.num_local_nodes > 1) {
        g_smpi_shmem->rqueues_flow[0*g_smpi.num_local_nodes + 1] = 
            (smpi_rqueues *)((char *)shmem + 
                    pid_len + SMPI_CACHE_LINE_SIZE);

        for (i = 2; i < g_smpi.num_local_nodes; ++i) {
            g_smpi_shmem->rqueues_flow[0*g_smpi.num_local_nodes + i] = 
                (smpi_rqueues *)((char*)g_smpi_shmem->rqueues_flow[0*g_smpi.num_local_nodes + i - 1] + 
                        SMPI_CACHE_LINE_SIZE);
        }

        smpi_rqueues * tmp_rqueues =
            g_smpi_shmem->rqueues_flow[0*g_smpi.num_local_nodes + g_smpi.num_local_nodes - 1];

        for (i = 1; i < g_smpi.num_local_nodes; ++i) {
            for (j = 0; j < g_smpi.num_local_nodes; ++j) {
                if(j == i) continue;
                if(i < j) {
                    g_smpi_shmem->rqueues_flow[i*g_smpi.num_local_nodes + j] = 
                        tmp_rqueues = (smpi_rqueues *)((char*)tmp_rqueues + SMPI_CACHE_LINE_SIZE);
                } else {
                    g_smpi_shmem->rqueues_flow[i*g_smpi.num_local_nodes + j] =
                        (smpi_rqueues*)((char*)g_smpi_shmem->rqueues_flow[j*g_smpi.num_local_nodes
                                + i] + (SMPI_CACHE_LINE_SIZE/2));
                }
            }
        }

        g_smpi_shmem->pool =
            (char *)((char *)g_smpi_shmem->rqueues_flow[1] + SMPI_ALIGN(rq_len) +
                    SMPI_CACHE_LINE_SIZE);
    } else {
        g_smpi_shmem->rqueues_flow[0] = NULL;
        g_smpi_shmem->pool =
            (char *)((char *)shmem + pid_len + SMPI_CACHE_LINE_SIZE);
    }
 
    for (i=0; i < g_smpi.num_local_nodes; ++i) {
        if ( i == g_smpi.my_local_id)
            continue;
        g_smpi_shmem->rqueues_limits_s[i].first = 
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) *
                (i * (g_smpi.num_local_nodes - 1) + 
                (g_smpi.my_local_id > i ? (g_smpi.my_local_id - 1) : g_smpi.my_local_id)));
        g_smpi_shmem->rqueues_limits_r[i].first = 
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) * 
                (g_smpi.my_local_id * (g_smpi.num_local_nodes - 1) + 
                (i > g_smpi.my_local_id ? (i - 1): i)));
        g_smpi_shmem->rqueues_limits_s[i].last =
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) * 
                (i * (g_smpi.num_local_nodes - 1) + 
                (g_smpi.my_local_id > i ? (g_smpi.my_local_id - 1) : g_smpi.my_local_id)) +
                g_smpi.available_queue_length);
        g_smpi_shmem->rqueues_limits_r[i].last =
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) * 
                (g_smpi.my_local_id * (g_smpi.num_local_nodes - 1) + 
                (i > g_smpi.my_local_id ? (i - 1): i)) + 
                g_smpi.available_queue_length);
        g_smpi_shmem->rqueues_params_c[i].current = 
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) *
                (g_smpi.my_local_id * (g_smpi.num_local_nodes - 1) + 
                (i > g_smpi.my_local_id ? (i - 1): i)));
        g_smpi_shmem->rqueues_params_n[i].next = 
            SMPI_ALIGN(pagesize + (pagesize + s_smpi_length_queue) *
                (i * (g_smpi.num_local_nodes - 1) + 
                (g_smpi.my_local_id > i ? (g_smpi.my_local_id - 1) : g_smpi.my_local_id)));
    }

    /* init rqueues in shared memory */
    if (0 == g_smpi.my_local_id) {
       for (i = 0; i < g_smpi.num_local_nodes; ++i) {
           for (j = 0; j < g_smpi.num_local_nodes; ++j) {
           if (i != j) {
               READBAR();
               g_smpi_shmem->rqueues_flow[j*g_smpi.num_local_nodes + i]->msgs_total_in = 0;
               g_smpi_shmem->rqueues_flow[i*g_smpi.num_local_nodes + j]->msgs_total_out = 0;
               READBAR();
           }
           }
       }
    }

    if (enable_shmem_collectives) {
        /* Memory Mapping shared files for collectives*/
        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_Mmap(pg, g_smpi.my_local_id)) != MPI_SUCCESS)
        {
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                 FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s", 
                 "SHMEM_COLL_Mmap failed");
           goto cleanup_files;
        }
    }
    /* another synchronization barrier */
    if (0 == g_smpi.my_local_id) {
    wait = 1;
    while (wait) {
        wait = 0;
        for (i = 1; i < g_smpi.num_local_nodes; ++i) {
        if (g_smpi_shmem->pid[i] == 0) {
            wait = 1;
        }
        }
    }

    pid = getpid();
    if (0 == pid) {
        mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
              FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s: %s",
              "getpid", strerror(errno));
        goto cleanup_files;
    }

    g_smpi_shmem->pid[g_smpi.my_local_id] = pid;
    WRITEBAR();
    } else {
    while (g_smpi_shmem->pid[0] != 0);
    while (g_smpi_shmem->pid[0] == 0) {
        g_smpi_shmem->pid[g_smpi.my_local_id] = getpid();
        WRITEBAR();
    }
    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
        if (g_smpi_shmem->pid[i] <= 0) {
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
               FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s: %s",
               "getpid", strerror(errno));
           goto cleanup_files;
        }
    }
    }

    /* Unlinking shared memory files*/
    MPIDI_CH3I_SMP_unlink();
    if (enable_shmem_collectives){
        MPIDI_CH3I_SHMEM_COLL_Unlink();
    }

#if defined(_X86_64_)
    /*
     * Okay, here we touch every page in the shared memory region.
     * We do this to get the pages allocated so that they are local
     * to the receiver on a numa machine (instead of all being located
     * near the first process).
     */
    {
       int receiver, sender;
 
       for (receiver = 0; receiver < g_smpi.num_local_nodes; ++receiver) {
           volatile char *ptr = g_smpi_shmem->pool;
           volatile char tmp;
 
           sender = g_smpi.my_local_id;
           if (sender != receiver) {
              int k;
           
              for (k = SMPI_FIRST_S(sender, receiver);
                  k < SMPI_LAST_S(sender, receiver); k += pagesize) {
                  tmp = ptr[k];
              }
           }
       }
    }
#endif /* defined(_X86_64_) */

    s_current_ptr = (void **) MPIU_Malloc(sizeof(void *) * g_smpi.num_local_nodes);
    if (!s_current_ptr) {
      MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
          "**nomem %s", "s_current_ptr");
    }

    s_current_bytes = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);
    if (!s_current_bytes) {
      MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
          "**nomem %s", "s_current_bytes");
    }

    s_total_bytes = (int *) MPIU_Malloc(sizeof(int) * g_smpi.num_local_nodes);
    if (!s_total_bytes) {
       MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
           "**nomem %s", "s_total_bytes");
    }

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
       s_current_ptr[i] = NULL;
       s_current_bytes[i] = 0;
       s_total_bytes[i] = 0;
    }

#ifdef _SMP_LIMIC_
    if (g_smp_use_limic2) {
        limic_fd = limic_open();
    
        if (limic_fd == -1) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
                        "%s: %s", "LiMIC2 device does not exist",
                        strerror(errno));
        }
    }
#endif

    SMP_INIT = 1;

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_INIT);
    return mpi_errno;

cleanup_files:
    MPIDI_CH3I_SMP_cleanup();
    if (enable_shmem_collectives){
        MPIDI_CH3I_SHMEM_COLL_Cleanup();
    }
fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_finalize
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_finalize()
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);

    /* unmap the shared memory file */
    munmap(g_smpi.mmap_ptr, g_size_shmem);
    close(g_smpi.fd);

    munmap(g_smpi.send_buf_pool_ptr, g_size_pool);
    close(g_smpi.fd_pool);

    if(s_buffer_head) {
        MPIU_Free(s_buffer_head);
    }

    if (g_smpi.l2g_rank) {
    MPIU_Free(g_smpi.l2g_rank);
    } 

    if(g_smpi_shmem) {
       if(g_smpi_shmem->rqueues_params_c != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_params_c);
       } 
       if(g_smpi_shmem->rqueues_params_n != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_params_n);
       } 
       if(g_smpi_shmem->rqueues_flow != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_flow);
       } 
       if(g_smpi_shmem->rqueues_limits_s != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_limits_s);
       }
       if(g_smpi_shmem->rqueues_limits_r != NULL) { 
          MPIU_Free(g_smpi_shmem->rqueues_limits_r);
       }
       if(g_smpi_shmem != NULL) { 
          MPIU_Free(g_smpi_shmem);
       }
    }

    if(s_current_ptr) {
    MPIU_Free(s_current_ptr);
    }

    if(s_current_bytes) {
    MPIU_Free(s_current_bytes);
    }

    if(s_total_bytes) {
    MPIU_Free(s_total_bytes);
    }

    if(s_sh_buf_pool.send_queue) {
    MPIU_Free(s_sh_buf_pool.send_queue);
    }

    if(s_sh_buf_pool.tail) {
    MPIU_Free(s_sh_buf_pool.tail);
    }

	if (enable_shmem_collectives){
    /* Freeing up shared memory collective resources*/
    MPIDI_CH3I_SHMEM_COLL_finalize( g_smpi.my_local_id, g_smpi.num_local_nodes);
    }

#if defined(HAVE_LIBHWLOC)
    if(mv2_enable_affinity == 1) { 
       hwloc_topology_destroy(topology);
    } 
#endif

#ifdef _SMP_LIMIC_
    limic_close(limic_fd);
#endif

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_FINALIZE);
    return MPI_SUCCESS;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_writev_rndv_header(MPIDI_VC_t * vc, const MPID_IOV * iov,
    const int n, int *num_bytes_ptr)
{
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    int i, offset = 0;
    MPIDI_CH3_Pkt_rndv_r3_data_t *pkt_header;
#if defined(_SMP_LIMIC_)
    size_t err;
    size_t  total_bytes = 0;
    MPID_Request *sreq = NULL;
    limic_user lu;
#endif
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_HEADER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_HEADER);

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (!pkt_avail) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
        + SMPI_NEXT(g_smpi.my_local_id,
        vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

    /* iov[0] is the header pkt */
    pkt_header = (MPIDI_CH3_Pkt_rndv_r3_data_t *)(iov[0].MPID_IOV_BUF);

#if defined(_SMP_LIMIC_)
    /* sreq is the send request handle for the data */
    sreq = pkt_header->send_req_id;

    /* sreq_req_id is set to NULL for non-contig data, then fall back to shared memory
     * instead of using limic; or else, continue data transfer by limic */
    if (g_smp_use_limic2 && sreq) {

        assert(sreq->dev.iov_count == 1);
        /* The last sizeof(int) is the total num of data bytes */
        pkt_len = iov[0].MPID_IOV_LEN + sizeof(limic_user) * sreq->dev.iov_count + sizeof(int);

        if (pkt_avail < pkt_len) {
            *num_bytes_ptr = 0;
            goto fn_exit;
        }

        /* number of bytes */
        ptr = (void *) ((unsigned long) ptr + sizeof(int));

        /* header format:
         * normal header | lu | total_num_bytes
         */
        MPIU_Memcpy(ptr, iov[0].MPID_IOV_BUF, iov[0].MPID_IOV_LEN);
        ptr = (void *) ((unsigned long) ptr +
                  iov[0].MPID_IOV_LEN);

        for(i = 0; i < sreq->dev.iov_count; ++i) {
            err = limic_tx_init( limic_fd, sreq->dev.iov[i].MPID_IOV_BUF,
                      sreq->dev.iov[i].MPID_IOV_LEN, &lu);
            if (!err) {
                MPIU_ERR_SETFATALANDJUMP1(err, MPI_ERR_OTHER,
                    "**fail", "**fail %s",
                    "LiMIC: (MPIDI_CH3I_SMP_writev_rndv_header) limic_tx_init fail");
            }
            total_bytes += sreq->dev.iov[i].MPID_IOV_LEN;

            /* copy the limic_user information to the shared memory
               and move the shared memory pointer properly
             */
            MPIU_Memcpy(ptr, &lu, sizeof(limic_user));
            ptr = (void *) ((unsigned long) ptr + sizeof(limic_user));
        }

        *((int *) ptr) = total_bytes;
        ptr = (void *) ((unsigned long) ptr + sizeof(int));

        /* the first element will be pkt_len (header len) */
        *((int *) ptr_head) = pkt_len;
        *num_bytes_ptr = iov[0].MPID_IOV_LEN;

        smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
            pkt_len + sizeof(int));
    } else {
#endif /* _SMP_LIMIC */

    if (pkt_avail < SMPI_SMALLEST_SIZE) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    send_buf_reclaim();

    if (s_sh_buf_pool.free_head == -1) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    pkt_header->src.smp_index = s_sh_buf_pool.free_head;

    ptr = (void *) ((unsigned long) ptr + sizeof(int));

    i = 0;
    *num_bytes_ptr = 0;
    do {
    pkt_len = 0;
    for (; i < n;) {
        DEBUG_PRINT
        ("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
         iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
         pkt_avail);
        if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
        if (offset != 0) {
          MPIU_Memcpy(ptr,
                (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
                iov[i].MPID_IOV_LEN - offset);
            pkt_avail -= (iov[i].MPID_IOV_LEN - offset);
            ptr =
            (void *) ((unsigned long) ptr +
                iov[i].MPID_IOV_LEN - offset);
            pkt_len += (iov[i].MPID_IOV_LEN - offset);
            offset = 0;
        } else {
          MPIU_Memcpy(ptr, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
            pkt_avail -= iov[i].MPID_IOV_LEN;
            ptr =
            (void *) ((unsigned long) ptr +
                iov[i].MPID_IOV_LEN);
            pkt_len += iov[i].MPID_IOV_LEN;
        }
        ++i;
        } else if (pkt_avail > 0) {
          MPIU_Memcpy(ptr,
            (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset), pkt_avail);
        ptr = (void *) ((unsigned long) ptr + pkt_avail);
        pkt_len += pkt_avail;
        offset += pkt_avail;
        pkt_avail = 0;
        break;
        }
    }

    DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
    *num_bytes_ptr += pkt_len;
    *((int *) ptr_head) = pkt_len;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        pkt_len + sizeof(int));
    if (i == n) {
        DEBUG_PRINT("counter value, in %d, out %d\n",
            SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
        break;
    }

    DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
#if defined(_SMP_LIMIC_)
    }
#endif /* _SMP_LIMIC_ */

fn_exit:
    DEBUG_PRINT("writev_rndv_header returns bytes %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_HEADER);
    return;

#if defined(_SMP_LIMIC_)
fn_fail:
    goto fn_exit;
#endif /* _SMP_LIMIC_ */
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_data_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_writev_rndv_data_cont(MPIDI_VC_t * vc, const MPID_IOV * iov,
    const int n, int *num_bytes_ptr)
{
    volatile void *ptr_volatile;
    void *ptr_head;
    int pkt_avail;
    int pkt_len = 0;
    int i, offset = 0;
    int destination = vc->smp.local_nodes;
    int first_index;
    SEND_BUF_T *send_buf = NULL;
    SEND_BUF_T *tmp_buf = NULL;
    int has_sent = 0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_DATA_CONT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_DATA_CONT);

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (pkt_avail < 2*sizeof(int)) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    pkt_avail = SMP_SEND_BUF_SIZE;

    send_buf_reclaim();

    if (s_sh_buf_pool.free_head == -1) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    first_index = s_sh_buf_pool.free_head;

    i = 0;
    *num_bytes_ptr = 0;
    do {
    pkt_len = 0;
    for (; i < n;) {
        DEBUG_PRINT
        ("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
         iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
         pkt_avail);

        if(has_sent >= s_smp_batch_size)
        break;
        ++has_sent;

        send_buf = get_buf_from_pool();
        if(send_buf == NULL)
        break;

        if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
        if (offset != 0) {
          MPIU_Memcpy(send_buf->buf,
                (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
                iov[i].MPID_IOV_LEN - offset);
            send_buf->busy = 1;
            send_buf->len = iov[i].MPID_IOV_LEN - offset;
            send_buf->has_next = 1;

            link_buf_to_send_queue (destination, send_buf->myindex);
            tmp_buf = send_buf;

            pkt_len += (iov[i].MPID_IOV_LEN - offset);
            offset = 0;
        } else {
          MPIU_Memcpy(send_buf->buf, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
            send_buf->busy = 1;
            send_buf->len = iov[i].MPID_IOV_LEN;
            send_buf->has_next = 1;

            link_buf_to_send_queue (destination, send_buf->myindex);
            tmp_buf = send_buf;

            pkt_len += iov[i].MPID_IOV_LEN;
        }
        ++i;
        } else if (pkt_avail > 0) {
          MPIU_Memcpy(send_buf->buf,
            (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
            pkt_avail);
        send_buf->busy = 1;
        send_buf->len = pkt_avail;
        send_buf->has_next = 1;

        link_buf_to_send_queue (destination, send_buf->myindex);
        tmp_buf = send_buf;

        pkt_len += pkt_avail;
        offset += pkt_avail;
        }
    }

    DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
    *num_bytes_ptr += pkt_len;

    if (i == n || has_sent >= s_smp_batch_size) {
        DEBUG_PRINT("counter value, in %d, out %d\n",
            SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
        break;
    }

    send_buf_reclaim();

    } while (s_sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
    tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
        + SMPI_NEXT(g_smpi.my_local_id,
        vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    ptr_head =(void *) ((unsigned long)ptr_head + sizeof(int));
    (*((int *) ptr_head)) = first_index;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        2*sizeof(int));

fn_exit:
    DEBUG_PRINT("writev_rndv_data_cont returns bytes %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_WRITEV_RNDV_DATA_CONT);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev_rndv_data
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_writev_rndv_data(MPIDI_VC_t * vc, const MPID_IOV * iov,
    const int n, int *num_bytes_ptr)
{
    volatile void *ptr_volatile;
    void *ptr_head;
    int pkt_avail;
    int pkt_len = 0;
    int i, offset = 0;
    int destination = vc->smp.local_nodes;
    SEND_BUF_T *send_buf = NULL;
    SEND_BUF_T *tmp_buf = NULL;
    int has_sent=0;
    int mpi_errno = MPI_SUCCESS;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_WRITE_RNDV_DATA);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_WRITE_RNDV_DATA);

    pkt_avail = SMP_SEND_BUF_SIZE;

    if(s_sh_buf_pool.free_head == -1) {
    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
        "**fail %s", "s_sh_buf_pool.free_head == -1");
    }

    i = 0;
    *num_bytes_ptr = 0;
    do {
    pkt_len = 0;
    for (; i < n;) {
        DEBUG_PRINT
        ("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
         iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
         pkt_avail);
        if(has_sent >= s_smp_batch_size)
        break;
        ++has_sent;

        send_buf = get_buf_from_pool();
        if(send_buf == NULL)
        break;

        if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
        if (offset != 0) {
          MPIU_Memcpy(send_buf->buf,
                (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
                iov[i].MPID_IOV_LEN - offset);
            send_buf->busy = 1;
            send_buf->len = iov[i].MPID_IOV_LEN - offset;
            send_buf->has_next = 1;

            link_buf_to_send_queue (destination, send_buf->myindex);
            tmp_buf = send_buf;

            pkt_len += (iov[i].MPID_IOV_LEN - offset);
            offset = 0;
        } else {
          MPIU_Memcpy(send_buf->buf, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
            send_buf->busy = 1;
            send_buf->len = iov[i].MPID_IOV_LEN;
            send_buf->has_next = 1;

            link_buf_to_send_queue (destination, send_buf->myindex);
            tmp_buf = send_buf;

            pkt_len += iov[i].MPID_IOV_LEN;
        }
        ++i;
        } else if (pkt_avail > 0) {
          MPIU_Memcpy(send_buf->buf,
            (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
            pkt_avail);
        send_buf->busy = 1;
        send_buf->len = pkt_avail;
        send_buf->has_next = 1;

        link_buf_to_send_queue (destination, send_buf->myindex);
        tmp_buf = send_buf;

        pkt_len += pkt_avail;
        offset += pkt_avail;
        }
    }

    DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
    *num_bytes_ptr += pkt_len;

    if (i == n || has_sent >= s_smp_batch_size) {
        DEBUG_PRINT("counter value, in %d, out %d\n",
            SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
        break;
    }

    send_buf_reclaim();

    } while (s_sh_buf_pool.free_head != -1);

    if(tmp_buf != NULL){
    tmp_buf->has_next = 0;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
        + SMPI_NEXT(g_smpi.my_local_id,
        vc->smp.local_nodes));
    ptr_head = (void *) ptr_volatile;
    (*((int *) ptr_head)) = (*num_bytes_ptr);

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        sizeof(int));

fn_exit:
    DEBUG_PRINT("writev_rndv_data returns bytes %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_WRITE_RNDV_DATA);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_writev
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_writev(MPIDI_VC_t * vc, const MPID_IOV * iov,
    const int n, int *num_bytes_ptr)
{
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    int i, offset = 0;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_WRITEV);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_WRITEV);

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    if (!pkt_avail) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
        + SMPI_NEXT(g_smpi.my_local_id,
        vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    pkt_avail = (pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
    pkt_avail -= sizeof(int);

    if (pkt_avail < iov[0].MPID_IOV_LEN) {
    *num_bytes_ptr = 0;
    goto fn_exit;
    }

    ptr = (void *) ((unsigned long) ptr + sizeof(int));

    i = 0;
    *num_bytes_ptr = 0;
    do {
    pkt_len = 0;
    for (; i < n;) {
        DEBUG_PRINT
        ("i %d, iov[i].len %d, (len-offset) %d, pkt_avail %d\n", i,
         iov[i].MPID_IOV_LEN, (iov[i].MPID_IOV_LEN - offset),
         pkt_avail);
        if (pkt_avail >= (iov[i].MPID_IOV_LEN - offset)) {
        if (offset != 0) {
          MPIU_Memcpy(ptr,
                (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset),
                iov[i].MPID_IOV_LEN - offset);
            pkt_avail -= (iov[i].MPID_IOV_LEN - offset);
            ptr =
            (void *) ((unsigned long) ptr +
                iov[i].MPID_IOV_LEN - offset);
            pkt_len += (iov[i].MPID_IOV_LEN - offset);
            offset = 0;
        } else {
          MPIU_Memcpy(ptr, iov[i].MPID_IOV_BUF, iov[i].MPID_IOV_LEN);
            pkt_avail -= iov[i].MPID_IOV_LEN;
            ptr =
            (void *) ((unsigned long) ptr +
                iov[i].MPID_IOV_LEN);
            pkt_len += iov[i].MPID_IOV_LEN;
        }
        ++i;
        } else if (pkt_avail > 0) {
          MPIU_Memcpy(ptr,
            (void *) ((unsigned long) iov[i].MPID_IOV_BUF +
                offset), pkt_avail);
        ptr = (void *) ((unsigned long) ptr + pkt_avail);
        pkt_len += pkt_avail;
        offset += pkt_avail;
        pkt_avail = 0;
        break;
        }
    }

    DEBUG_PRINT("current pkt consumed, pkt_len %d\n", pkt_len);
    *num_bytes_ptr += pkt_len;
    *((int *) ptr_head) = pkt_len;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        (pkt_len + sizeof(int)));

    if (i == n) {
        DEBUG_PRINT("counter value, in %d, out %d\n",
            SMPI_TOTALIN(0, 1), SMPI_TOTALOUT(0, 1));
        break;
    }

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);
    if (pkt_avail != 0) {
        pkt_avail =
        (pkt_avail > g_smp_eagersize) ? g_smp_eagersize : pkt_avail;
        pkt_avail -= sizeof(int);
        ptr_head = (void *) ((g_smpi_shmem->pool)
            + SMPI_NEXT(g_smpi.my_local_id,
            vc->smp.local_nodes));
        ptr = (void *) ((unsigned long) ptr_head + sizeof(int));
    }
    DEBUG_PRINT("new pkt avail %d\n", pkt_avail);
    } while (pkt_avail > 0);
fn_exit:
    DEBUG_PRINT("writev returns bytes %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_WRITEV);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_write_contig
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void MPIDI_CH3I_SMP_write_contig(MPIDI_VC_t * vc, MPIDI_CH3_Pkt_type_t reqtype,
                          const void * buf, MPIDI_msg_sz_t data_sz, int rank,
                          int tag, MPID_Comm * comm, int context_offset,
                          int *num_bytes_ptr)
{
#if defined(MPID_USE_SEQUENCE_NUMBERS)
    MPID_Seqnum_t seqnum;
#endif
    int pkt_avail;
    int pkt_len = 0;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_WRITE_CONTIG);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_WRITE_CONTIG);

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);

    pkt_len = sizeof(MPIDI_CH3_Pkt_eager_send_t) + data_sz;
    if (pkt_avail < pkt_len + sizeof(int) ) {
        *num_bytes_ptr = 0;
        return;
    }

    MPIDI_CH3_Pkt_t *upkt;
    MPIDI_CH3_Pkt_eager_send_t * eager_pkt;
    ptr_volatile = (void *) ((g_smpi_shmem->pool)
        + SMPI_NEXT(g_smpi.my_local_id,
        vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;
    
    ptr = (void *) ((unsigned long) ptr + sizeof(int));
    *num_bytes_ptr = 0;

    upkt = (MPIDI_CH3_Pkt_t *) ptr;
    eager_pkt = &((*upkt).eager_send);
    MPIDI_Pkt_init(eager_pkt, reqtype);
    eager_pkt->match.parts.rank = comm->rank;
    eager_pkt->match.parts.tag  = tag;
    eager_pkt->match.parts.context_id   = comm->context_id + context_offset;
    eager_pkt->sender_req_id    = MPI_REQUEST_NULL;
    eager_pkt->data_sz      = data_sz;

    MPIDI_VC_FAI_send_seqnum(vc, seqnum);
    MPIDI_Pkt_set_seqnum(eager_pkt, seqnum);

    memcpy((void *) ((unsigned long) ptr +
                     sizeof(MPIDI_CH3_Pkt_eager_send_t)), buf, data_sz);

    *num_bytes_ptr += pkt_len;
    *((int *) ptr_head) = pkt_len;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        (pkt_len + sizeof(int)));
    
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_WRITE_CONTIG);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv_rndv_cont
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#if defined(_SMP_LIMIC_)
int MPIDI_CH3I_SMP_readv_rndv_cont(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, struct limic_header *l_header,
        size_t *num_bytes_ptr, int use_limic)
#else
int MPIDI_CH3I_SMP_readv_rndv_cont(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, size_t *num_bytes_ptr)
#endif
{
    int mpi_errno = MPI_SUCCESS;
    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    int destination = recv_vc_ptr->smp.local_nodes;
    int current_index = index;
    int recv_offset = 0;
    size_t msglen, iov_len;
    void *current_buf;
    SEND_BUF_T *recv_buf;
#if defined(_SMP_LIMIC_)
    int i;
    size_t err, old_len;
    int total_bytes = l_header->total_bytes;
#endif
    /* all variable must be declared before the state declarations */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_READV_RNDV_CONT);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_READV_RNDV_CONT);

    *num_bytes_ptr = 0;

#if defined(_SMP_LIMIC_)
    if (use_limic) {
        /* copy the message from the send buffer to the receive buffer */
        msglen = total_bytes;
        iov_len = iov[0].MPID_IOV_LEN;

        for (i = 0; total_bytes > 0 && iov_off < iovlen; ) {
            if (msglen == iov_len) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
        }

                received_bytes += msglen;
                total_bytes -= msglen;

                assert(total_bytes == 0 && ++iov_off >= iovlen);

            } else if (msglen > iov_len) {
                old_len = l_header->lu.length;
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      iov_len, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
        }

                received_bytes += iov_len;
                total_bytes -= iov_len;
                msglen -= iov_len;

                adjust_lu_info(&(l_header->lu), old_len);

                if (++iov_off >= iovlen)
                    break;
                buf_off = 0;
                iov_len = iov[iov_off].MPID_IOV_LEN;

            }  else if (msglen > 0) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err ) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");
        }

                received_bytes += msglen;
                total_bytes -= msglen;
            }
        }

        *num_bytes_ptr = received_bytes;
        l_header->total_bytes -= received_bytes;
    } else {
#endif /* _SMP_LIMIC_ */
    if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
    if(s_total_bytes[recv_vc_ptr->smp.local_nodes] != 0) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "s_total_bytes[recv_vc_ptr->smp.local_nodes] "
            "!= 0");
    }

    if (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) ==
        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id)) {
        goto fn_exit;
    }

    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
        (void *) ((g_smpi_shmem->pool) +
            SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));
    WRITEBAR();
    s_total_bytes[recv_vc_ptr->smp.local_nodes] =
        *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
    s_current_bytes[recv_vc_ptr->smp.local_nodes] =
        s_total_bytes[recv_vc_ptr->smp.local_nodes];

    DEBUG_PRINT
        ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
         s_current_bytes[recv_vc_ptr->smp.local_nodes],
         s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
         iov[0].MPID_IOV_LEN);
    WRITEBAR();

    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
        (void *)((unsigned long) s_current_ptr[recv_vc_ptr->smp.local_nodes] +
            sizeof(int));
    current_index = *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
        g_smpi.my_local_id, 2*sizeof(int));
    } else {
    s_total_bytes[recv_vc_ptr->smp.local_nodes] =
        s_current_bytes[recv_vc_ptr->smp.local_nodes];
    current_index = recv_vc_ptr->smp.read_index;
    recv_offset = recv_vc_ptr->smp.read_off;
    }

    if (current_index != -1) {
    /** last smp packet has not been drained up yet **/
    DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
        iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
        iov[iov_off].MPID_IOV_LEN);

    recv_buf = &s_buffer_head[destination][current_index];

    if(recv_buf->busy != 1) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "recv_buf->busy != 1");
    }

    msglen = recv_buf->len - recv_offset;
    current_buf = (void *)((unsigned long) recv_buf->buf + recv_offset);
    iov_len = iov[0].MPID_IOV_LEN;

    for (;
        iov_off < iovlen
        && s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0
        && current_index != -1;) {

        if (msglen > iov_len) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, iov_len);
        READBAR();
        current_buf = (void *) ((unsigned long) current_buf +
            iov_len);
        msglen -= iov_len;
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            iov_len;
        received_bytes += iov_len;
        buf_off = 0;
        ++iov_off;

        if (iov_off >= iovlen) {
            recv_vc_ptr->smp.read_index = current_index;
            recv_vc_ptr->smp.read_off = (unsigned long) current_buf -
            (unsigned long) recv_buf->buf;
            break;
        }

        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
            recv_buf->busy = 0;
            break;
        }

        else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s",
                "s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
        }

        iov_len = iov[iov_off].MPID_IOV_LEN;
        } else if (msglen == iov_len) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, iov_len);
        READBAR();
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            iov_len;
        received_bytes += iov_len;
        buf_off = 0;
        ++iov_off;

        if (iov_off >= iovlen) {
            recv_vc_ptr->smp.read_index = recv_buf->next;
            recv_vc_ptr->smp.read_off = 0;
            recv_buf->busy = 0;
            break;
        }
        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
            MPIU_Assert(s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0);
            recv_buf->busy = 0;
            break;
        }

        iov_len = iov[iov_off].MPID_IOV_LEN;

        if(recv_buf->has_next == 0){
            recv_buf->busy = 0;
            break;
        }

        current_index = recv_buf->next;
        recv_buf->busy = 0;
        recv_buf = &s_buffer_head[destination][current_index];
        MPIU_Assert(recv_buf->busy == 1);
        msglen = recv_buf->len;
        current_buf = (void *)recv_buf->buf;

        } else if (msglen > 0) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, msglen);
        READBAR();
        iov_len -= msglen;
        received_bytes += msglen;
        buf_off += msglen;
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
            recv_buf->busy = 0;
            break;
        }
        if(recv_buf->has_next == 0){
            recv_buf->busy = 0;
            break;
        }

        current_index = recv_buf->next;
        recv_buf->busy = 0;
        recv_buf = &s_buffer_head[destination][current_index];
        MPIU_Assert(recv_buf->busy == 1);
        msglen = recv_buf->len;
        current_buf = (void *)recv_buf->buf;
        }
    }
    *num_bytes_ptr += received_bytes;
    DEBUG_PRINT
        ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
         s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
         iov_off, iovlen);

    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }
    received_bytes = 0;
    if (iov_off == iovlen) {
        /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
        goto fn_exit;
    }
    }
#if defined(_SMP_LIMIC_)
    }
#endif

fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_READV_RNDV_CONT);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv_rndv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
#if defined(_SMP_LIMIC_)
int MPIDI_CH3I_SMP_readv_rndv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
        const int iovlen, int index, struct limic_header *l_header, size_t *num_bytes_ptr, int use_limic)
#else
int MPIDI_CH3I_SMP_readv_rndv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
    const int iovlen, int index, size_t *num_bytes_ptr)
#endif
{
    int mpi_errno = MPI_SUCCESS;
    size_t iov_off = 0, buf_off = 0;
    size_t received_bytes = 0;
    size_t msglen, iov_len;
    /* all variable must be declared before the state declarations */
#if defined(_SMP_LIMIC_)
    int i;
    size_t  err, old_len;
    size_t total_bytes = l_header->total_bytes;
#endif
    int destination = recv_vc_ptr->smp.local_nodes;
    int current_index = index;
    void *current_buf;
    SEND_BUF_T *recv_buf;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_READ_RNDV);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_READ_RNDV);

    *num_bytes_ptr = 0;

    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
    DEBUG_PRINT("total in %d, total out %d\n",
        SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
                    g_smpi.my_local_id));

    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
        g_smpi.my_local_id,
        s_total_bytes[recv_vc_ptr->smp.local_nodes] +
        sizeof(int));

    DEBUG_PRINT("total in %d, total out %d\n",
        SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));
    s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }

#if defined(_SMP_LIMIC_)
    if (use_limic) {
        /* copy the message from the send buffer to the receive buffer */
        msglen = total_bytes;
        iov_len = iov[0].MPID_IOV_LEN;

        for (i = 0; total_bytes > 0 && iov_off < iovlen; ) {
            if (msglen == iov_len) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err )
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += msglen;
                total_bytes -= msglen;

                assert(total_bytes == 0 && ++iov_off >= iovlen);

            } else if (msglen > iov_len) {
                old_len = l_header->lu.length;
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      iov_len, &(l_header->lu));
                if( !err )
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += iov_len;
                total_bytes -= iov_len;
                msglen -= iov_len;

                adjust_lu_info(&(l_header->lu), old_len);

                if (++iov_off >= iovlen)
                    break;
                buf_off = 0;
                iov_len = iov[iov_off].MPID_IOV_LEN;

            } else if (msglen > 0) {
                err = limic_rx_comp(limic_fd,
                      (void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
                      msglen, &(l_header->lu));
                if( !err )
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "**fail %s",
                        "LiMIC: (MPIDI_CH3I_SMP_readv_rndv) limic_rx_comp fail");

                received_bytes += msglen;
                total_bytes -= msglen;
            }
        }

        *num_bytes_ptr = received_bytes;
        l_header->total_bytes -= received_bytes;
    } else {
#endif /* _SMP_LIMIC_ */
    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) ==
        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id));

    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
    (void *) ((g_smpi_shmem->pool) +
        SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));
    WRITEBAR();
    s_total_bytes[recv_vc_ptr->smp.local_nodes] =
    *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
    s_current_bytes[recv_vc_ptr->smp.local_nodes] =
    s_total_bytes[recv_vc_ptr->smp.local_nodes];
    smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
        g_smpi.my_local_id,sizeof(int));
    DEBUG_PRINT
    ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
     s_current_bytes[recv_vc_ptr->smp.local_nodes],
     s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
     iov[0].MPID_IOV_LEN);

    WRITEBAR();

    if (current_index != -1) {
    /** last smp packet has not been drained up yet **/
    DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
        iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
        iov[iov_off].MPID_IOV_LEN);

    recv_buf = &s_buffer_head[destination][current_index];

    if(recv_buf->busy != 1) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
            "**fail %s", "recv_buf->busy == 1");
    }

    msglen = recv_buf->len;
    current_buf = (void *)recv_buf->buf;
    iov_len = iov[0].MPID_IOV_LEN;

    for (;
        iov_off < iovlen
        && s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0
        && current_index != -1;) {

        if (msglen > iov_len) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, iov_len);
        READBAR();
        current_buf = (void *) ((unsigned long) current_buf +
            iov_len);
        msglen -= iov_len;
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            iov_len;
        received_bytes += iov_len;
        buf_off = 0;
        ++iov_off;

        if (iov_off >= iovlen) {
            recv_vc_ptr->smp.read_index = current_index;
            recv_vc_ptr->smp.read_off = (unsigned long) current_buf -
            (unsigned long) recv_buf->buf;
            break;
        }
        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] <= 0) {
            MPIU_Assert(s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0);
            recv_buf->busy = 0;
            break;
        }

        iov_len = iov[iov_off].MPID_IOV_LEN;
        } else if (msglen == iov_len) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, iov_len);
        READBAR();
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            iov_len;
        received_bytes += iov_len;
        buf_off = 0;
        ++iov_off;

        if (iov_off >= iovlen) {
            recv_vc_ptr->smp.read_index = recv_buf->next;
            recv_vc_ptr->smp.read_off = 0;
            recv_buf->busy = 0;
            break;
        }

        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
            recv_buf->busy = 0;
            break;
        }

        else if(s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s",
                "s_current_bytes[recv_vc_ptr->smp.local_nodes] < 0");
        }

        iov_len = iov[iov_off].MPID_IOV_LEN;

        if(recv_buf->has_next == 0){
            recv_buf->busy = 0;
            break;
        }

        current_index = recv_buf->next;
        recv_buf->busy = 0;
        recv_buf = &s_buffer_head[destination][current_index];

        if(recv_buf->busy != 1) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s", "recv_buf->busy != 1");
        }

        msglen = recv_buf->len;
        current_buf = (void *)recv_buf->buf;

        } else if (msglen > 0) {
        READBAR();
        MPIU_Memcpy((void *) ((unsigned long)iov[iov_off].MPID_IOV_BUF + buf_off),
            (void *) current_buf, msglen);
        READBAR();
        iov_len -= msglen;
        received_bytes += msglen;
        buf_off += msglen;
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -= msglen;

        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] == 0) {
            recv_buf->busy = 0;
            break;
        }
        if(recv_buf->has_next == 0){
            recv_buf->busy = 0;
            break;
        }

        current_index = recv_buf->next;
        recv_buf->busy = 0;
        recv_buf = &s_buffer_head[destination][current_index];

        if(recv_buf->busy != 1) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s", "recv_buf->busy != 1");
        }

        msglen = recv_buf->len;
        current_buf = (void *)recv_buf->buf;
        }
    }
    *num_bytes_ptr += received_bytes;
    DEBUG_PRINT
        ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
         s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
         iov_off, iovlen);

    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }
    received_bytes = 0;
    if (iov_off == iovlen) {
        /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
        goto fn_exit;
    }
    }
#if defined(_SMP_LIMIC_)
    }
#endif

fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_READ_RNDV);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_readv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_readv(MPIDI_VC_t * recv_vc_ptr, const MPID_IOV * iov,
    const int iovlen, size_t  *num_bytes_ptr)
{
    int mpi_errno = MPI_SUCCESS;

    int iov_off = 0, buf_off = 0;
    int received_bytes = 0;
    /* all variable must be declared before the state declarations */
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_READV);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_READV);

    *num_bytes_ptr = 0;

    DEBUG_PRINT
    ("current byte %d, total bytes %d, iovlen %d, iov[0].len %d\n",
     s_current_bytes[recv_vc_ptr->smp.local_nodes],
     s_total_bytes[recv_vc_ptr->smp.local_nodes], iovlen,
     iov[0].MPID_IOV_LEN);
    WRITEBAR();
    if (s_current_ptr[recv_vc_ptr->smp.local_nodes] != NULL) {
    /** last smp packet has not been drained up yet **/
    DEBUG_PRINT("iov_off %d, current bytes %d, iov len %d\n",
        iov_off, s_current_bytes[recv_vc_ptr->smp.local_nodes],
        iov[iov_off].MPID_IOV_LEN);
    for (;
        iov_off < iovlen
        && s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] >=
            iov[iov_off].MPID_IOV_LEN) {
        READBAR();
        MPIU_Memcpy((void *) iov[iov_off].MPID_IOV_BUF,
            (void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
            iov[iov_off].MPID_IOV_LEN);
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((unsigned long)
                s_current_ptr[recv_vc_ptr->smp.local_nodes] +
                iov[iov_off].MPID_IOV_LEN);
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            iov[iov_off].MPID_IOV_LEN;
        received_bytes += iov[iov_off].MPID_IOV_LEN;
        ++iov_off;
        } else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
        READBAR();
        MPIU_Memcpy((void *) iov[iov_off].MPID_IOV_BUF,
            (void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
            s_current_bytes[recv_vc_ptr->smp.local_nodes]);
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((unsigned long)
                s_current_ptr[recv_vc_ptr->smp.local_nodes] +
                s_current_bytes[recv_vc_ptr->smp.local_nodes]);
        received_bytes +=
            s_current_bytes[recv_vc_ptr->smp.local_nodes];
        buf_off = s_current_bytes[recv_vc_ptr->smp.local_nodes];
        s_current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
        }
    }
    *num_bytes_ptr += received_bytes;
    DEBUG_PRINT
        ("current bytes %d, num_bytes %d, iov_off %d, iovlen %d\n",
         s_current_bytes[recv_vc_ptr->smp.local_nodes], *num_bytes_ptr,
         iov_off, iovlen);

    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id,
            s_total_bytes[recv_vc_ptr->smp.local_nodes] +
            sizeof(int));

        DEBUG_PRINT("total in %d, total out %d\n",
            SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id),
            SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));

        s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }
    received_bytes = 0;
    if (iov_off == iovlen) {
        /* assert: s_current_ptr[recv_vc_ptr->smp.local_nodes] == 0 */
        goto fn_exit;
    }
    }

    WRITEBAR();
    while (SMPI_TOTALIN(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id) !=
        SMPI_TOTALOUT(recv_vc_ptr->smp.local_nodes, g_smpi.my_local_id)) {
    /****** received the next smp packet **************/
    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
        (void *) ((g_smpi_shmem->pool) +
            SMPI_CURRENT(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id));
    WRITEBAR();
    s_total_bytes[recv_vc_ptr->smp.local_nodes] =
        *((int *) s_current_ptr[recv_vc_ptr->smp.local_nodes]);
    s_current_bytes[recv_vc_ptr->smp.local_nodes] =
        s_total_bytes[recv_vc_ptr->smp.local_nodes];
    READBAR();
    s_current_ptr[recv_vc_ptr->smp.local_nodes] =
        (void *) ((unsigned long)
            s_current_ptr[recv_vc_ptr->smp.local_nodes] +
            sizeof(int));

    /****** starting to fill the iov buffers *********/
    for (;
        iov_off < iovlen
        && s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0;) {
        if (s_current_bytes[recv_vc_ptr->smp.local_nodes] >=
            iov[iov_off].MPID_IOV_LEN - buf_off) {
        WRITEBAR();
        MPIU_Memcpy((void *) ((unsigned long) iov[iov_off].
                MPID_IOV_BUF + buf_off),
            (void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
            iov[iov_off].MPID_IOV_LEN - buf_off);
        s_current_bytes[recv_vc_ptr->smp.local_nodes] -=
            (iov[iov_off].MPID_IOV_LEN - buf_off);
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((unsigned long)
                s_current_ptr[recv_vc_ptr->smp.local_nodes] +
                (iov[iov_off].MPID_IOV_LEN - buf_off));
        received_bytes += (iov[iov_off].MPID_IOV_LEN - buf_off);
        ++iov_off;
        buf_off = 0;
        } else if (s_current_bytes[recv_vc_ptr->smp.local_nodes] > 0) {
        WRITEBAR();
        MPIU_Memcpy((void *) ((unsigned long) iov[iov_off].
                MPID_IOV_BUF + buf_off),
            (void *) s_current_ptr[recv_vc_ptr->smp.local_nodes],
            s_current_bytes[recv_vc_ptr->smp.local_nodes]);
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] =
            (void *) ((unsigned long)
                s_current_ptr[recv_vc_ptr->smp.local_nodes] +
                s_current_bytes[recv_vc_ptr->smp.local_nodes]);
        received_bytes +=
            s_current_bytes[recv_vc_ptr->smp.local_nodes];
        buf_off += s_current_bytes[recv_vc_ptr->smp.local_nodes];
        s_current_bytes[recv_vc_ptr->smp.local_nodes] = 0;
        }
    }
    *num_bytes_ptr += received_bytes;
    if (0 == s_current_bytes[recv_vc_ptr->smp.local_nodes]) {
        READBAR();
        s_current_ptr[recv_vc_ptr->smp.local_nodes] = NULL;
        smpi_complete_recv(recv_vc_ptr->smp.local_nodes,
            g_smpi.my_local_id,
            s_total_bytes[recv_vc_ptr->smp.local_nodes] +
            sizeof(int));
        s_total_bytes[recv_vc_ptr->smp.local_nodes] = 0;
    }
    received_bytes = 0;
    if (iov_off == iovlen) {
        goto fn_exit;
    }
    WRITEBAR();
    }
fn_exit:
    DEBUG_PRINT("return with nb %d\n", *num_bytes_ptr);
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_READV);
    return mpi_errno;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_pull_header
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_SMP_pull_header(MPIDI_VC_t* vc, MPIDI_CH3_Pkt_t** pkt_head)
{
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_PULL_HEADER);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_PULL_HEADER);
    if (s_current_bytes[vc->smp.local_nodes] != 0)
    {
        MPIU_Error_printf(
            "current bytes %d, total bytes %d, remote id %d\n",
            s_current_bytes[vc->smp.local_nodes],
            s_total_bytes[vc->smp.local_nodes],
            vc->smp.local_nodes
        );
        MPIU_Assert(s_current_bytes[vc->smp.local_nodes] == 0);
    }

    if (s_total_bytes[vc->smp.local_nodes] != 0)
    {
        READBAR();
        s_current_ptr[vc->smp.local_nodes] = NULL;
        smpi_complete_recv(
            vc->smp.local_nodes,
            g_smpi.my_local_id,
            s_total_bytes[vc->smp.local_nodes] + sizeof(int)
        );
        s_total_bytes[vc->smp.local_nodes] = 0;
        s_current_bytes[vc->smp.local_nodes] = 0;
    }

    WRITEBAR();

    if (SMPI_TOTALIN(vc->smp.local_nodes, g_smpi.my_local_id) !=
        SMPI_TOTALOUT(vc->smp.local_nodes, g_smpi.my_local_id))
    {
        DEBUG_PRINT(
            "remote %d, local %d, total in %d, total out %d\n",
            vc->smp.local_nodes,
            g_smpi.my_local_id,
            SMPI_TOTALIN(vc->smp.local_nodes, g_smpi.my_local_id),
            SMPI_TOTALOUT(vc->smp.local_nodes, g_smpi.my_local_id)
        );

        READBAR();
        s_current_ptr[vc->smp.local_nodes] = (void*)(g_smpi_shmem->pool +
            SMPI_CURRENT(vc->smp.local_nodes, g_smpi.my_local_id));
        WRITEBAR();
        s_total_bytes[vc->smp.local_nodes] = *((int*) s_current_ptr[vc->smp.local_nodes]);
        WRITEBAR();
        *pkt_head = (void*) ((unsigned long) s_current_ptr[vc->smp.local_nodes] + sizeof(int));
        DEBUG_PRINT(
            "bytes arrived %d, head type %d, headersize %d\n",
            s_total_bytes[vc->smp.local_nodes],
            ((MPIDI_CH3_Pkt_t*) *pkt_head)->type,
            MPIDI_CH3U_PKT_SIZE(*pkt_head)
        );
        s_current_bytes[vc->smp.local_nodes] = s_total_bytes[vc->smp.local_nodes] - MPIDI_CH3U_PKT_SIZE(*pkt_head);
        READBAR();
        s_current_ptr[vc->smp.local_nodes] = (void*)(
            (unsigned long) s_current_ptr[vc->smp.local_nodes]
            + sizeof(int)
            + MPIDI_CH3U_PKT_SIZE(*pkt_head)
        );
        DEBUG_PRINT("current bytes %d\n", s_current_bytes[vc->smp.local_nodes]);
    }
    else
    {
        *pkt_head = NULL;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_PULL_HEADER);
    return MPI_SUCCESS;
}


static int smpi_exchange_info(MPIDI_PG_t *pg)
{
    int mpi_errno = MPI_SUCCESS;
    int pg_rank, pg_size;

    int i = 0;
    int j;

    MPIDI_VC_t* vc = NULL;
    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMPI_EXCHANGE_INFO);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMPI_EXCHANGE_INFO);

    PMI_Get_rank(&pg_rank);
    PMI_Get_size(&pg_size);

    g_smpi.num_local_nodes = MPIDI_Num_local_processes(pg);

    for (i = 0; i < pg->size; ++i) {
        MPIDI_PG_Get_vc(pg, i, &vc);
        vc->smp.local_nodes = vc->smp.local_rank;
    }

    /* Get my VC */
    MPIDI_PG_Get_vc(pg, pg_rank, &vc);
    g_smpi.my_local_id = vc->smp.local_nodes;

#if defined(HAVE_LIBHWLOC)
    if (mv2_enable_affinity) {
        mpi_errno = smpi_setaffinity();
        if (mpi_errno != MPI_SUCCESS) {
            MPIU_ERR_POP(mpi_errno);
        }
    }
#endif /* defined(HAVE_LIBHWLOC) */

    DEBUG_PRINT("num local nodes %d, my local id %d\n",
        g_smpi.num_local_nodes, g_smpi.my_local_id);

    g_smpi.l2g_rank = (unsigned int *) MPIU_Malloc(g_smpi.num_local_nodes * sizeof(int));
    if(g_smpi.l2g_rank == NULL) {
    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**nomem",
        "**nomem %s", "g_smpi.12g_rank");
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(off, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    MPIU_Error_printf(
        "malloc: in ib_rank_lid_table for SMP");
#if defined(__SUNPRO_C) || defined(__SUNPRO_CC)
#pragma error_messages(default, E_STATEMENT_NOT_REACHED)
#endif /* defined(__SUNPRO_C) || defined(__SUNPRO_CC) */
    }

    for (i = 0, j = 0; j < pg_size; ++j) {
        MPIDI_PG_Get_vc(pg, j, &vc);

        if (vc->smp.local_nodes != -1) {
            g_smpi.l2g_rank[i] = j;
            i++;
    	}
    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMPI_EXCHANGE_INFO);
    return mpi_errno;

fn_fail:
    goto fn_exit;
}

/*----------------------------------------------------------*/
    static inline SEND_BUF_T *
get_buf_from_pool ()
{
    SEND_BUF_T *ptr;

    if (s_sh_buf_pool.free_head == -1) 
    return NULL;

    ptr = &s_my_buffer_head[s_sh_buf_pool.free_head];
    s_sh_buf_pool.free_head = ptr->next;
    ptr->next = -1;

    MPIU_Assert (ptr->busy == 0);

    return ptr;
}

static inline void send_buf_reclaim ()
{
    int i, index, last_index;
    SEND_BUF_T *ptr;

    for (i = 0; i < g_smpi.num_local_nodes; ++i) {
    if (i != g_smpi.my_local_id) {
        index = s_sh_buf_pool.send_queue[i];
        last_index = -1;
        ptr = NULL;
        while (index != -1) {
        ptr = &s_my_buffer_head[index];
        if(ptr->busy == 1)
            break;
        last_index = index;
        index = ptr->next;
        }
        if (last_index != -1)
        put_buf_to_pool (s_sh_buf_pool.send_queue[i], last_index);
        s_sh_buf_pool.send_queue[i] = index;
        if (s_sh_buf_pool.send_queue[i] == -1)
        s_sh_buf_pool.tail[i] = -1;
    }
    }
}

    static inline void
put_buf_to_pool (int head, int tail)
{
    SEND_BUF_T *ptr;

    MPIU_Assert (head != -1);
    MPIU_Assert (tail != -1);

    ptr = &s_my_buffer_head[tail];

    ptr->next = s_sh_buf_pool.free_head;
    s_sh_buf_pool.free_head = head;
}

static inline void link_buf_to_send_queue (int dest, int index)
{
    if (s_sh_buf_pool.send_queue[dest] == -1) {
        s_sh_buf_pool.send_queue[dest] = index;
    } else {
        s_my_buffer_head[(s_sh_buf_pool.tail[dest])].next = index;
    }
    s_sh_buf_pool.tail[dest] = index;
}

#if defined(_SMP_LIMIC_)
#undef FUNCNAME
#define FUNCNAME adjust_lu_info
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static inline void adjust_lu_info(struct limic_user *lu, int old_len)
{
    unsigned long va = lu->va + lu->length;
    int pgcount, len = old_len - lu->length;
    int pagesize = getpagesize();

    MPIDI_STATE_DECL(MPID_STATE_ADJUST_LU_INFO);
    MPIDI_FUNC_ENTER(MPID_STATE_ADJUST_LU_INFO);

    pgcount = (va + len + pagesize - 1)/pagesize - va/pagesize;
    MPIU_Assert(pgcount);

    lu->va = va;
    lu->nr_pages = pgcount;
    lu->offset = va & (pagesize-1);
    lu->length = len;

    MPIDI_FUNC_EXIT(MPID_STATE_ADJUST_LU_INFO);
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_SMP_send_limic_comp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/* l_header contains send_req_id from the sender,
 * nb is the number bytes received
 */
void MPIDI_CH3I_SMP_send_limic_comp(struct limic_header *l_header,
                                    MPIDI_VC_t* vc, int nb)
{
    MPIDI_CH3_Pkt_limic_comp_t pkt;
    int pkt_sz = sizeof(MPIDI_CH3_Pkt_limic_comp_t);
    int pkt_avail;
    volatile void *ptr_volatile;
    void *ptr_head, *ptr;
    MPID_Request *sreq = NULL;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);

    pkt.type = MPIDI_CH3_PKT_LIMIC_COMP;
    pkt.send_req_id = (MPI_Request *)l_header->send_req_id;
    pkt.nb = nb;
    
    /*make sure the complete message not sent between other unfinished message */
    if (MPIDI_CH3I_SMP_SendQ_head(vc)) {
        sreq = create_request(&pkt, pkt_sz, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        return;
    }

    pkt_avail = smpi_get_avail_length(vc->smp.local_nodes);
    if (pkt_avail < pkt_sz+sizeof(int)) {
        /* queue the message */
        sreq = create_request(&pkt, pkt_sz, 0);
        MPIDI_CH3I_SMP_SendQ_enqueue(vc, sreq);
        return;
    }

    ptr_volatile = (void *) ((g_smpi_shmem->pool)
            + SMPI_NEXT(g_smpi.my_local_id,
                vc->smp.local_nodes));
    ptr_head = ptr = (void *) ptr_volatile;

    ptr = (void *) ((unsigned long) ptr + sizeof(int));
    MPIU_Memcpy(ptr, &pkt, pkt_sz);

    ptr = (void *) ((unsigned long) ptr + pkt_sz);
    *((int *) ptr_head) = pkt_sz;

    smpi_complete_send(g_smpi.my_local_id, vc->smp.local_nodes,
        pkt_sz + sizeof(int));

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_CH3I_SMP_SEND_LIMIC_COMP);
    return;
}

#endif /* _SMP_LIMIC_ */

/* vi:set sw=4 */
