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

#include "pmi.h"
#include "rdma_impl.h"
#include "mpiutil.h"
#include <debug_utils.h>

#undef DEBUG_PRINT
#ifdef DEBUG
#define DEBUG_PRINT(args...)                                          \
    do {                                                              \
        int rank;                                                     \
        PMI_Get_rank(&rank);                                          \
        fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);    \
        fprintf(stderr, args);                                        \
        fflush(stderr);                                               \
    } while (0)
#else
#define DEBUG_PRINT(args...)
#endif

static pthread_spinlock_t g_apm_lock;

/*
 * TODO add error handling
 */
void init_apm_lock()
{
    pthread_spin_init(&g_apm_lock, 0);
}

#undef FUNCNAME
#define FUNCNAME lock_apm
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static void lock_apm()
{
    pthread_spin_lock(&g_apm_lock);
    return;
}

#undef FUNCNAME
#define FUNCNAME unlock_apm 
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
static void unlock_apm()
{
    pthread_spin_unlock(&g_apm_lock);
    return;
}

const char *MPIDI_CH3_VC_GetStateString(MPIDI_VC_t *vc)
{
    return NULL;
}

MRAILI_Channel_manager *arriving_head = NULL;
MRAILI_Channel_manager *arriving_tail = NULL;

#define INDEX_GLOBAL(_cmanager,_global_index) (_global_index) 

#define INDEX_LOCAL(_cmanager,_local_index) \
    (((_cmanager)->num_channels - (_cmanager)->num_local_pollings) + (_local_index))

static inline void CHANNEL_ENQUEUE(MRAILI_Channel_manager* cmanager)
{
    if (arriving_tail == NULL)
    {
        arriving_head = arriving_tail = cmanager;
        cmanager->next_arriving = NULL;
    }
    else
    {
        arriving_tail->next_arriving = cmanager;
        cmanager->next_arriving = NULL;
        arriving_tail = cmanager;
    }

    cmanager->inqueue = 1;
}

static inline void VQUEUE_ENQUEUE(MRAILI_Channel_manager* cmanager, int index, vbuf* v)
{                      
    v->desc.next = NULL;

    if (cmanager->msg_channels[index].v_queue_tail == NULL)
    { 
        cmanager->msg_channels[index].v_queue_head = v;      
    }
    else
    {
        cmanager->msg_channels[index].v_queue_tail->desc.next = v;
    }

    cmanager->msg_channels[index].v_queue_tail = v;
    ++cmanager->msg_channels[index].len;

    if (!cmanager->inqueue)
    {
        CHANNEL_ENQUEUE(cmanager);
    }
}

static inline vbuf* VQUEUE_DEQUEUE(MRAILI_Channel_manager* cmanager, int index)
{
    vbuf* v = cmanager->msg_channels[index].v_queue_head;
    cmanager->msg_channels[index].v_queue_head = v->desc.next;

    if (v == cmanager->msg_channels[index].v_queue_tail)
    {
        cmanager->msg_channels[index].v_queue_tail = NULL;
    }

    --cmanager->msg_channels[index].len;
    v->desc.next = NULL;
    return v;
}

static inline int PKT_IS_NOOP(void* v)
{        
    return ((MPIDI_CH3I_MRAILI_Pkt_comm_header*) ((vbuf*) v)->pheader)->type == MPIDI_CH3_PKT_NOOP;
}

#undef FUNCNAME
#define FUNCNAME GetSeqNumVbuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/*FIXME: Ideally this functionality should be provided by higher levels*/
static inline int GetSeqNumVbuf(vbuf * buf)
{
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;
    if (NULL == buf) {
        return PKT_IS_NULL;
    }

    p = buf->pheader;

    if (p->type == MPIDI_CH3_PKT_NOOP) {
        return PKT_NO_SEQ_NUM;
    }
    
    return (p->seqnum);
}

static inline vbuf * MPIDI_CH3I_RDMA_poll(MPIDI_VC_t * vc)
{
    vbuf *v = NULL;
    volatile VBUF_FLAG_TYPE *head;
    volatile VBUF_FLAG_TYPE *tail;
    int size;

    if (num_rdma_buffer == 0)
        return NULL;

    v = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
    head = v->head_flag;

    if (*head && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail) {
        size = (*head & FAST_RDMA_SIZE_MASK);
        tail = (VBUF_FLAG_TYPE *) (v->buffer + size);
        /* rdma write is in progress, the tail has not received yet.*/
        if(*head != *tail) {
            return NULL;
        }
        /* advance receive pointer */
        if (++(vc->mrail.rfp.p_RDMA_recv) >= num_rdma_buffer) {
            vc->mrail.rfp.p_RDMA_recv = 0;
        }
        v->pheader = v->buffer;
            DEBUG_PRINT("[recv: poll rdma] recv %d, tail %d, size %d\n",
                    vc->pg_rank,
                    vc->mrail.rfp.p_RDMA_recv, vc->mrail.rfp.p_RDMA_recv_tail, *head);
        v->content_size = (*head & FAST_RDMA_SIZE_MASK);
    } else {
        v = NULL;
    }
    return v;
}

static int MPIDI_CH3I_MRAILI_Test_pkt(vbuf **vbuf_handle)
{
    int type = T_CHANNEL_NO_ARRIVE;
    while (arriving_head) {
        type = MPIDI_CH3I_MRAILI_Waiting_msg(arriving_head->vc, vbuf_handle, 0);
        if (type == T_CHANNEL_NO_ARRIVE) {
            arriving_head->inqueue = 0;
            arriving_head = arriving_head->next_arriving;
            if (!arriving_head)
                arriving_tail = NULL;
        } else {
            break;
        }
    }

    return type;
}

/* Yet this functionality has not been implemented */
int MPIDI_CH3I_MRAILI_Register_channels(MPIDI_VC_t *vc, int num, vbuf *(*func[])(void *))
{
    return MPI_SUCCESS;    
}

int MPIDI_CH3I_MRAILI_Get_next_vbuf_local(MPIDI_VC_t* vc, vbuf** vbuf_handle, int is_blocking)
{
    int type = T_CHANNEL_NO_ARRIVE;
    int i = 0;
    *vbuf_handle = NULL;

    for (; i < vc->mrail.num_rails; ++i)
    {
        if (vc->mrail.rails[i].send_wqes_avail < RDMA_LOW_WQE_THRESHOLD)
        {
            break;
        }
    }

    if (i != vc->mrail.num_rails)
    {
        vbuf* unused;
        MPIDI_CH3I_MRAILI_Cq_poll(&unused, vc, 1, is_blocking);
    }

    return type;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Get_next_vbuf
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Get_next_vbuf(MPIDI_VC_t** vc_ptr, vbuf** vbuf_ptr) 
{
    *vc_ptr = NULL;
    *vbuf_ptr = NULL;
    VBUF_FLAG_TYPE size;
    int type;
    MPIDI_STATE_DECL(MPID_GEN2_MPIDI_CH3I_MRAILI_GET_NEXT_VBUF);
    MPIDI_FUNC_ENTER(MPID_GEN2_MPIDI_CH3I_MRAILI_GET_NEXT_VBUF);

    type = MPIDI_CH3I_MRAILI_Test_pkt(vbuf_ptr);

    switch(type)
    { 
    case T_CHANNEL_CONTROL_MSG_ARRIVE:
    case T_CHANNEL_EXACT_ARRIVE:
            *vc_ptr = (*vbuf_ptr)->vc;
        goto fn_exit;
    case T_CHANNEL_OUT_OF_ORDER_ARRIVE:
            type = T_CHANNEL_NO_ARRIVE;
            *vbuf_ptr = NULL;
        break;
    }

    if (num_rdma_buffer == 0)
    {
        goto fn_exit;
    }

    int i = 0;
    MPIDI_VC_t* vc = NULL;
    int seq;
    vbuf* v = NULL;
    volatile VBUF_FLAG_TYPE* tail = NULL;
    volatile VBUF_FLAG_TYPE* head = NULL;

    /* no msg is queued, poll rdma polling set */
    for (; i < MPIDI_CH3I_RDMA_Process.polling_group_size; ++i)
    {
        vc = MPIDI_CH3I_RDMA_Process.polling_set[i];
        seq  = GetSeqNumVbuf(vc->mrail.cmanager.msg_channels[INDEX_LOCAL(&vc->mrail.cmanager,0)].v_queue_head);

        if (seq == PKT_IS_NULL)
        {
            v = &(vc->mrail.rfp.RDMA_recv_buf[vc->mrail.rfp.p_RDMA_recv]);
            head = v->head_flag;

            if (*head && vc->mrail.rfp.p_RDMA_recv != vc->mrail.rfp.p_RDMA_recv_tail)
            {
                size = (*head & FAST_RDMA_SIZE_MASK);
                tail = (VBUF_FLAG_TYPE *) (v->buffer + size);
                /* If the tail has not received yet, than go ahead and
                ** poll next connection */
                if (*head != *tail) {
                    continue;
                }
                
                DEBUG_PRINT("Get one!\n");

                if (++vc->mrail.rfp.p_RDMA_recv >= num_rdma_buffer)
                {
                    vc->mrail.rfp.p_RDMA_recv = 0;
                }

                v->pheader = v->buffer;
                v->content_size = size;
                *head = 0;

                seq = GetSeqNumVbuf(v);

#ifdef _ENABLE_UD_
                if (rdma_enable_hybrid){
                    v->seqnum = seq;
                    type = T_CHANNEL_HYBRID_MSG_ARRIVE;
                    *vbuf_ptr = v;
                    *vc_ptr = v->vc;
                    PRINT_DEBUG(DEBUG_UD_verbose>1,"received seqnum:%d expected:%d vc_ptr:%p\n",seq, vc->mrail.seqnum_next_torecv, v->vc);
                    goto fn_exit;
                } 
                else
#endif
                {
                    if (seq == vc->mrail.seqnum_next_torecv)
                    {
                        DEBUG_PRINT("Get one exact seq: %d\n", seq);
                        type = T_CHANNEL_EXACT_ARRIVE;
                        ++vc->mrail.seqnum_next_torecv;
                        *vbuf_ptr = v;
                        *vc_ptr = v->vc;
                        goto fn_exit;
                    }
                    else if (seq == PKT_NO_SEQ_NUM)
                    {
                        type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                        DEBUG_PRINT("[vbuf_local]: get control msg\n");
                        *vbuf_ptr = v;
                        *vc_ptr = v->vc;
                        goto fn_exit;
                    }
                    else
                    {
                        DEBUG_PRINT("Get one out of order seq: %d, expecting %d\n", seq, vc->mrail.seqnum_next_torecv);
                        VQUEUE_ENQUEUE(&vc->mrail.cmanager, INDEX_LOCAL(&vc->mrail.cmanager, 0), v);
                        continue;
                    }
                }
            }
            else
            {
                continue;
            }
        } 
        if (seq == vc->mrail.seqnum_next_torecv)
        {
            *vbuf_ptr = VQUEUE_DEQUEUE(&vc->mrail.cmanager, INDEX_LOCAL(&vc->mrail.cmanager, 0));
            *vc_ptr = (*vbuf_ptr)->vc;
            ++vc->mrail.seqnum_next_torecv;
            type = T_CHANNEL_EXACT_ARRIVE;
            goto fn_exit;
        }
        else if (seq == PKT_NO_SEQ_NUM)
        {
            *vbuf_ptr = VQUEUE_DEQUEUE(&vc->mrail.cmanager, INDEX_LOCAL(&vc->mrail.cmanager, 0));
            *vc_ptr = (*vbuf_ptr)->vc;
            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
            goto fn_exit;
        }

    }

fn_exit:
    MPIDI_FUNC_EXIT(MPID_GEN2_MPIDI_CH3I_MRAILI_GET_NEXT_VBUF);
    return type;
}

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_MRAILI_Waiting_msg
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Waiting_msg(MPIDI_VC_t * vc, vbuf ** vbuf_handle, int blocking) 
{
    MRAILI_Channel_manager * cmanager = &vc->mrail.cmanager;
    int i = 0;
    int seq;
    int seq_expected = vc->mrail.seqnum_next_torecv;
    int type = T_CHANNEL_NO_ARRIVE;
    MPIDI_STATE_DECL(MPID_GEN2_MPIDI_CH3I_MRAILIWAITING_MSG);
    MPIDI_FUNC_ENTER(MPID_GEN2_MPIDI_CH3I_MRAILIWAITING_MSG);

    *vbuf_handle = NULL;

    if (blocking) {
        DEBUG_PRINT("{entering} solve_out_of_order next expected %d, channel %d, head %p (%d)\n", 
                vc->mrail.seqnum_next_torecv, cmanager->num_channels, 
                cmanager->msg_channels[0].v_queue_head,
                GetSeqNumVbuf(cmanager->msg_channels[0].v_queue_head));
    }

    for (; i < cmanager->num_channels; ++i)
    {
        seq = GetSeqNumVbuf(cmanager->msg_channels[i].v_queue_head);
        if (seq == seq_expected) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
            type = T_CHANNEL_EXACT_ARRIVE;
            ++vc->mrail.seqnum_next_torecv;
            goto fn_exit;
        } else if (PKT_NO_SEQ_NUM == seq) {
            *vbuf_handle = VQUEUE_DEQUEUE(cmanager, i);
            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
            goto fn_exit;
        } else if (PKT_IS_NULL == seq) {
            /* Do nothing */
        } else {
            *vbuf_handle = cmanager->msg_channels[i].v_queue_head;
            type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
        }
    }

    /* Obviously the packet with correct sequence hasn't arrived */
    while (blocking) {
        /* poll local subrails*/
        for (i = 0; i < cmanager->num_local_pollings; ++i) {
            seq = GetSeqNumVbuf(cmanager->msg_channels[INDEX_LOCAL(cmanager,i)].v_queue_head);
            if (seq == seq_expected) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
                ++vc->mrail.seqnum_next_torecv;
                type = T_CHANNEL_EXACT_ARRIVE;
                goto fn_exit;
            } else if (seq == PKT_NO_SEQ_NUM) {
                *vbuf_handle = VQUEUE_DEQUEUE(cmanager, INDEX_LOCAL(cmanager,i));
                type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                goto fn_exit;
            }
            else if (vc->mrail.rfp.in_polling_set) {
                *vbuf_handle = MPIDI_CH3I_RDMA_poll(vc);
                seq = GetSeqNumVbuf(*vbuf_handle);
                if (seq == seq_expected) {
                    type = T_CHANNEL_EXACT_ARRIVE;
                    ++vc->mrail.seqnum_next_torecv;
                    goto fn_exit;
                }
                else if( seq == PKT_NO_SEQ_NUM) {
                    type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                    goto fn_exit;
                } else if (*vbuf_handle != NULL){
                    VQUEUE_ENQUEUE(cmanager, INDEX_LOCAL(cmanager,i), *vbuf_handle);
                    *vbuf_handle = NULL;
                }
            }
        }

        type = MPIDI_CH3I_MRAILI_Cq_poll(vbuf_handle, vc, 0, blocking);
        if (type != T_CHANNEL_NO_ARRIVE) {
            switch(type) {
                case (T_CHANNEL_EXACT_ARRIVE):
                    goto fn_exit;
                case (T_CHANNEL_OUT_OF_ORDER_ARRIVE):
                    continue;
                case (T_CHANNEL_CONTROL_MSG_ARRIVE):
                    goto fn_exit;
                default:
                    ibv_error_abort(GEN_ASSERT_ERR, "Unexpected return type\n");
                    break;
            }
        } else {

        }
    } 
fn_exit:
    if (blocking) {
        DEBUG_PRINT("{return} solve_out_of_order, type %d, next expected %d\n", 
                type, vc->mrail.seqnum_next_torecv);
    }
    MPIDI_FUNC_EXIT(MPID_GEN2_MPIDI_CH3I_MRAILIWAITING_MSG);
    return type;
}

#if 1
static unsigned long debug = 0;
MPIDI_VC_t *debug_vc;
#define LONG_WAIT (40000000)
#endif

#undef FUNCNAME
#define FUNCNAME MPIDI_CH3I_RDMA_cq_poll
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int MPIDI_CH3I_MRAILI_Cq_poll(vbuf **vbuf_handle, 
        MPIDI_VC_t * vc_req, int receiving, int is_blocking)
{
    int ne, ret;
    MPIDI_VC_t *vc = NULL;
    struct ibv_wc wc;
    vbuf *v;
    int i = 0;
    int cq_choice = 0;
    int num_cqs = 0;
    int needed;
    int is_send_completion;
    int type = T_CHANNEL_NO_ARRIVE;
    static unsigned long nspin = 0;
    struct ibv_cq *ev_cq; 
    struct ibv_cq *chosen_cq; 
    void *ev_ctx;
    MPIDI_CH3I_MRAILI_Pkt_comm_header *p;

    int myrank;
    MPIDI_STATE_DECL(MPID_GEN2_MRAILI_CQ_POLL);
    MPIDI_FUNC_ENTER(MPID_GEN2_MRAILI_CQ_POLL);
    myrank = PMI_Get_rank(&myrank);

    *vbuf_handle = NULL;
    needed = 0;

    if (!receiving && !vc_req) {
        type = MPIDI_CH3I_MRAILI_Test_pkt(vbuf_handle);
        if (type == T_CHANNEL_EXACT_ARRIVE 
                || type == T_CHANNEL_CONTROL_MSG_ARRIVE)
            goto fn_exit;
    }

    if (rdma_iwarp_use_multiple_cq &&
        MV2_IS_CHELSIO_IWARP_CARD(MPIDI_CH3I_RDMA_Process.hca_type) &&
        (MPIDI_CH3I_RDMA_Process.cluster_size != VERY_SMALL_CLUSTER)) {
        num_cqs = 2;
    } else {
        num_cqs = 1;
    }

    for (; i < rdma_num_hcas; ++i) {
        for (cq_choice = 0; cq_choice < num_cqs; ++cq_choice) {
            if (1 == num_cqs) {
	            chosen_cq = MPIDI_CH3I_RDMA_Process.cq_hndl[i];
	        } else {
	            if (0 == cq_choice) {
	                chosen_cq = MPIDI_CH3I_RDMA_Process.send_cq_hndl[i];
                } else {
	                chosen_cq = MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i];
                }
	        }
	        ne = ibv_poll_cq(chosen_cq, 1, &wc);
	        if (ne < 0 ) {
	            ibv_error_abort(IBV_RETURN_ERR, "Fail to poll cq\n");
	        } else if (ne) {         
	            v = (vbuf *) ((uintptr_t) wc.wr_id);
	
	            vc = (MPIDI_VC_t *) (v->vc);
                cq_poll_completion = 1;
	
	            if (wc.status != IBV_WC_SUCCESS) {
	                if (wc.opcode == IBV_WC_SEND ||
	                    wc.opcode == IBV_WC_RDMA_WRITE ) {
			    		fprintf(stderr, "[%d->%d] send desc error, wc_opcode=%d\n",myrank, vc->pg_rank, wc.opcode );
	                } else {
			    		fprintf(stderr, "[%d<-%d] recv desc error, wc_opcode=%d\n",myrank, vc->pg_rank, wc.opcode);
					}
                    fprintf(stderr, "[%d->%d] wc.status=%d, wc.wr_id=%p, wc.opcode=%d, vbuf->phead->type=%d = %s\n", 
                           myrank, vc->pg_rank, wc.status, v, 
			               wc.opcode,((MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader)->type, 
           			MPIDI_CH3_Pkt_type_to_string[((MPIDI_CH3I_MRAILI_Pkt_comm_header*)v->pheader)->type] );
	
	                ibv_va_error_abort(IBV_STATUS_ERR,
	                        "[] Got completion with error %d, "
	                        "vendor code=0x%x, dest rank=%d\n",
	                        wc.status,    
	                        wc.vendor_err, 
	                        ((MPIDI_VC_t *)v->vc)->pg_rank
	                        );
	            }

                is_send_completion = (wc.opcode == IBV_WC_SEND
                    || wc.opcode == IBV_WC_RDMA_WRITE
                    || wc.opcode == IBV_WC_RDMA_READ);
	
                if (2 == num_cqs) {
    	            if (0 == cq_choice) {
    	                if (MPIDI_CH3I_RDMA_Process.global_used_send_cq) {
                             MPIDI_CH3I_RDMA_Process.global_used_send_cq--;
    	                } else {
                            DEBUG_PRINT("[%d] Possibly received a duplicate \
                                       send completion event \n", 
                                       MPIDI_Process.my_pg_rank);
    	                }
    	            } 
                } else {
                       if(is_send_completion && 
                              (MPIDI_CH3I_RDMA_Process.global_used_send_cq > 0)) {
                             MPIDI_CH3I_RDMA_Process.global_used_send_cq--;
                       } else {
                            DEBUG_PRINT("[%d] Possibly received a duplicate \
                                       send completion event \n",
                                       MPIDI_Process.my_pg_rank);
                       }     
                }
 
	            if(!is_send_completion && (MPIDI_CH3I_RDMA_Process.has_srq
                                    || v->transport == IB_TRANSPORT_UD)) {
                    SET_PKT_LEN_HEADER(v, wc);
                    SET_PKT_HEADER_OFFSET(v);
                    p = v->pheader;
#ifdef _ENABLE_UD_
                    MPIDI_PG_Get_vc(MPIDI_Process.my_pg, p->src.rank, &vc);
#else
                    vc = (MPIDI_VC_t *)p->src.vc_addr;
#endif
                    v->vc = vc;
                    v->rail = p->rail;
	            } 
	            
	            /* get the VC and increase its wqe */
	            if (is_send_completion) {
#ifdef _ENABLE_UD_
                if (rdma_enable_hybrid) {
                    if(v->transport == IB_TRANSPORT_RC  || 
                        (v->pheader && IS_CNTL_MSG(v->pheader))) {
                        MRAILI_Process_send(v);
                    }
                    if (v->transport == IB_TRANSPORT_UD) {
                        mv2_ud_update_send_credits(v);
                    }
                    if(v->transport == IB_TRANSPORT_UD &&
                            v->flags & UD_VBUF_SEND_INPROGRESS) {
                        v->flags &= ~(UD_VBUF_SEND_INPROGRESS);
                        if (v->flags & UD_VBUF_FREE_PENIDING) {
                            v->flags &= ~(UD_VBUF_FREE_PENIDING);
                            MRAILI_Release_vbuf(v);
                        }
                    }
                }
                else
#endif
                {
	                MRAILI_Process_send(v);
                }
                    type = T_CHANNEL_NO_ARRIVE;
                    *vbuf_handle = NULL;
	            } else if ((NULL == vc_req || vc_req == vc) && 0 == receiving ){
	                /* In this case, we should return the vbuf 
	                 * any way if it is next expected*/
	                int seqnum = GetSeqNumVbuf(v);
	                *vbuf_handle = v; 
                    SET_PKT_LEN_HEADER(v, wc);
                    SET_PKT_HEADER_OFFSET(v);
                    v->seqnum =  seqnum;
                    p = v->pheader;
                    PRINT_DEBUG(DEBUG_UD_verbose>1,"Received from rank:%d seqnum :%d ack:%d size:%d type:%d trasport :%d \n",vc->pg_rank, v->seqnum, p->acknum, v->content_size, p->type, v->transport);
#ifdef _ENABLE_UD_
                    if (v->transport == IB_TRANSPORT_UD)
                    {
                        mv2_ud_ctx_t *ud_ctx = 
                            MPIDI_CH3I_RDMA_Process.ud_rails[i];
                        --ud_ctx->num_recvs_posted;
                        if(ud_ctx->num_recvs_posted < ud_ctx->credit_preserve) {
                            ud_ctx->num_recvs_posted += mv2_post_ud_recv_buffers(
                                    (rdma_default_max_ud_recv_wqe - ud_ctx->num_recvs_posted), ud_ctx);
                        }
                    }
                    else
#endif 
                    if (MPIDI_CH3I_RDMA_Process.has_srq) {
	                    pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.
	                            srq_post_spin_lock);
	
	                    if(v->padding == NORMAL_VBUF_FLAG) {
	                        /* Can only be from SRQ path */
	                        --MPIDI_CH3I_RDMA_Process.posted_bufs[i];
	                    }
	
	                    if(MPIDI_CH3I_RDMA_Process.posted_bufs[i] <= 
	                            rdma_credit_preserve) {
	                        /* Need to post more to the SRQ */
	                        MPIDI_CH3I_RDMA_Process.posted_bufs[i] +=
	                            viadev_post_srq_buffers(viadev_srq_fill_size - 
	                                MPIDI_CH3I_RDMA_Process.posted_bufs[i], i);
	
	                    }
	
	                    pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.
	                            srq_post_spin_lock);
	
	                    /* Check if we need to release the SRQ limit thread */
	                    if (MPIDI_CH3I_RDMA_Process.
	                            srq_zero_post_counter[i] >= 1) {
	                        pthread_mutex_lock(
	                                &MPIDI_CH3I_RDMA_Process.
	                                srq_post_mutex_lock[i]);
	                        MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[i] = 0;
	                        pthread_cond_signal(&MPIDI_CH3I_RDMA_Process.
	                                srq_post_cond[i]);
	                        pthread_mutex_unlock(
	                                &MPIDI_CH3I_RDMA_Process.
	                                srq_post_mutex_lock[i]);
	                    }
	
	                }
	                else
	                {
	                    --vc->mrail.srp.credits[v->rail].preposts;
	
	                    needed = rdma_prepost_depth + rdma_prepost_noop_extra
	                             + MIN(rdma_prepost_rendezvous_extra,
	                                   vc->mrail.srp.credits[v->rail].
	                                   rendezvous_packets_expected);
	                }
#ifdef _ENABLE_UD_
                    if (rdma_enable_hybrid){
                        if (IS_CNTL_MSG(p)){
                            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                        } else {
                            type = T_CHANNEL_HYBRID_MSG_ARRIVE;
                        }
                    }
                    else
#endif
                    {
                        if (seqnum == PKT_NO_SEQ_NUM){
                            type = T_CHANNEL_CONTROL_MSG_ARRIVE;
                        } else if (seqnum == vc->mrail.seqnum_next_torecv) {
                            vc->mrail.seqnum_next_toack = vc->mrail.seqnum_next_torecv;
                            ++vc->mrail.seqnum_next_torecv;
                            type = T_CHANNEL_EXACT_ARRIVE;
                            DEBUG_PRINT("[channel manager] get one with exact seqnum\n");
                        } else {
                            type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
                            VQUEUE_ENQUEUE(&vc->mrail.cmanager, 
                                    INDEX_GLOBAL(&vc->mrail.cmanager, v->rail),
                                    v);
                            DEBUG_PRINT("get recv %d (%d)\n", seqnum, vc->mrail.seqnum_next_torecv);
                        }
                    }
	                if (!MPIDI_CH3I_RDMA_Process.has_srq && v->transport != IB_TRANSPORT_UD) {
                          
	                    if (PKT_IS_NOOP(v)) {
	                        PREPOST_VBUF_RECV(vc, v->rail);
	                        /* noops don't count for credits */
	                        --vc->mrail.srp.credits[v->rail].local_credit;
	                    } 
	                    else if ((vc->mrail.srp.credits[v->rail].preposts 
                                 < rdma_rq_size) &&
	                             (vc->mrail.srp.credits[v->rail].preposts + 
	                             rdma_prepost_threshold < needed))
	                    {
	                        do {
	                            PREPOST_VBUF_RECV(vc, v->rail);
	                        } while (vc->mrail.srp.credits[v->rail].preposts 
                                     < rdma_rq_size &&
	                                 vc->mrail.srp.credits[v->rail].preposts 
                                     < needed);
	                    }
	
	                    MRAILI_Send_noop_if_needed(vc, v->rail);
	                }
	
	                if (type == T_CHANNEL_CONTROL_MSG_ARRIVE || 
	                        type == T_CHANNEL_EXACT_ARRIVE ||
                            type == T_CHANNEL_HYBRID_MSG_ARRIVE || 
	                        type == T_CHANNEL_OUT_OF_ORDER_ARRIVE) {
	                    goto fn_exit;
	                }
	            } else {
	                /* Commenting out the assert - possible coding error
	                 * MPIU_Assert(0);
	                 */
	                /* Now since this is not the packet we want, we have to 
                     * enqueue it */
	                type = T_CHANNEL_OUT_OF_ORDER_ARRIVE;
	                *vbuf_handle = NULL;
	                v->content_size = wc.byte_len;
	                VQUEUE_ENQUEUE(&vc->mrail.cmanager,
	                        INDEX_GLOBAL(&vc->mrail.cmanager, v->rail),
	                        v);
                    if (v->transport != IB_TRANSPORT_UD) {
                        if (MPIDI_CH3I_RDMA_Process.has_srq) {
                            pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

                            if(v->padding == NORMAL_VBUF_FLAG ) {
                                /* Can only be from SRQ path */
                                --MPIDI_CH3I_RDMA_Process.posted_bufs[i];
                            }

                            if(MPIDI_CH3I_RDMA_Process.posted_bufs[i] <= rdma_credit_preserve) {
                                /* Need to post more to the SRQ */
                                MPIDI_CH3I_RDMA_Process.posted_bufs[i] +=
                                    viadev_post_srq_buffers(viadev_srq_fill_size - 
                                            MPIDI_CH3I_RDMA_Process.posted_bufs[i], i);

                            }

                            pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.
                                    srq_post_spin_lock);
                        } else {
                            --vc->mrail.srp.credits[v->rail].preposts;

                            needed = rdma_prepost_depth + rdma_prepost_noop_extra
                                + MIN(rdma_prepost_rendezvous_extra,
                                        vc->mrail.srp.credits[v->rail].
                                        rendezvous_packets_expected);

                            if (PKT_IS_NOOP(v)) {
                                PREPOST_VBUF_RECV(vc, v->rail);
                                --vc->mrail.srp.credits[v->rail].local_credit;
                            }
                            else if ((vc->mrail.srp.credits[v->rail].preposts 
                                        < rdma_rq_size) &&
                                    (vc->mrail.srp.credits[v->rail].preposts + 
                                     rdma_prepost_threshold < needed)) {
                                do {
                                    PREPOST_VBUF_RECV(vc, v->rail);
                                } while (vc->mrail.srp.credits[v->rail].preposts 
                                        < rdma_rq_size && 
                                        vc->mrail.srp.credits[v->rail].preposts 
                                        < needed);
                            }
                            MRAILI_Send_noop_if_needed(vc, v->rail);
                        }
                    }
	            }
	        } else {
	            *vbuf_handle = NULL;
	            type = T_CHANNEL_NO_ARRIVE;
	            ++nspin;
	
	            /* Blocking mode progress */
	            if(rdma_use_blocking && is_blocking && nspin >= rdma_blocking_spin_count_threshold){
	                /* Okay ... spun long enough, now time to go to sleep! */
	
	#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
	                MPIU_THREAD_CHECK_BEGIN
	                MPID_Thread_mutex_unlock(&MPIR_ThreadInfo.global_mutex);
	                MPIU_THREAD_CHECK_END
	#endif
	                do {    
	                    ret = ibv_get_cq_event(
	                            MPIDI_CH3I_RDMA_Process.comp_channel[i], 
	                            &ev_cq, &ev_ctx);
	                    if (ret && errno != EINTR) {
	                        ibv_va_error_abort(IBV_RETURN_ERR,
	                                "Failed to get cq event: %d\n", ret);
	                    }       
	                } while (ret && errno == EINTR); 
	#if (MPICH_THREAD_LEVEL == MPI_THREAD_MULTIPLE)
	                MPIU_THREAD_CHECK_BEGIN
	                MPID_Thread_mutex_lock(&MPIR_ThreadInfo.global_mutex);
	                MPIU_THREAD_CHECK_END
	#endif
	
                    if (num_cqs == 1) {
		                if (ev_cq != MPIDI_CH3I_RDMA_Process.cq_hndl[i]) {
		                    ibv_error_abort(IBV_STATUS_ERR,
                                             "Event in unknown CQ\n");
		                }
		
	                   ibv_ack_cq_events(MPIDI_CH3I_RDMA_Process.cq_hndl[i], 1);
		
		                if (ibv_req_notify_cq(
                                    MPIDI_CH3I_RDMA_Process.cq_hndl[i], 0)) {
		                    ibv_error_abort(IBV_RETURN_ERR,
		                            "Couldn't request for CQ notification\n");
		                }
                    } else {
		                if (ev_cq == MPIDI_CH3I_RDMA_Process.send_cq_hndl[i]) {
	                        ibv_ack_cq_events(
                                    MPIDI_CH3I_RDMA_Process.send_cq_hndl[i], 1);
		
		                    if (ibv_req_notify_cq(
                                  MPIDI_CH3I_RDMA_Process.send_cq_hndl[i], 0)) {
		                        ibv_error_abort(IBV_RETURN_ERR,
		                           "Couldn't request for CQ notification\n");
		                    }
                        } else if (ev_cq == 
                                    MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i]) {
	                        ibv_ack_cq_events(
                                    MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i], 1);
		
		                    if (ibv_req_notify_cq(
                                  MPIDI_CH3I_RDMA_Process.recv_cq_hndl[i], 0)) {
		                        ibv_error_abort(IBV_RETURN_ERR,
		                           "Couldn't request for CQ notification\n");
		                    }
		                } else {
		                   ibv_error_abort(IBV_STATUS_ERR,
                                             "Event in unknown CQ\n");
                        }
                    }
	                nspin = 0;
	            }
	        }
        }
    }
fn_exit:
#if 1
    if (type == T_CHANNEL_NO_ARRIVE) {
        int rank;
        ++debug;
        PMI_Get_rank(&rank);
        if (debug % LONG_WAIT == 0) {
        MPIU_dbg_printf("polling\n");
/*	    if (vc_req)
		debug_vc = vc_req;
            fprintf(stderr, "[%d] waiting %lu rounds but get nothing, arriving "
		    "head %p, vc req %p, receiving %d\n", rank, debug,
		    arriving_head, vc_req, receiving);
            fprintf(stderr, "   vc is %p (%d), expecting %d, "
                    "channels %d, queue heads <%p:%d:%d><%p:%d:%d>\n",
                    debug_vc, debug_vc->pg_rank, debug_vc->seqnum_recv, 
                    debug_vc->mrail.cmanager.num_channels,
                    debug_vc->mrail.cmanager.msg_channels[0].v_queue_head,
		    GetSeqNumVbuf(debug_vc->mrail.cmanager.msg_channels[0].v_queue_head),
		    debug_vc->mrail.cmanager.msg_channels[0].v_queue_head ? 
		    ((MPIDI_CH3I_MRAILI_Pkt_comm_header*)
             debug_vc->mrail.cmanager.msg_channels[0].
             v_queue_head->pheader)->type : -222,
                    debug_vc->mrail.cmanager.msg_channels[1].v_queue_head,
		    GetSeqNumVbuf(debug_vc->mrail.cmanager.msg_channels[1].v_queue_head),
		    debug_vc->mrail.cmanager.msg_channels[1].v_queue_head ?
		    ((MPIDI_CH3I_MRAILI_Pkt_comm_header*)debug_vc->mrail.
             cmanager.msg_channels[1].v_queue_head->pheader)->type : -222);
            fprintf(stderr, "   backlog length %d, wqe <%d><%d>, "
                    "ext queue heads <%p><%p>\n",
                    debug_vc->mrail.srp.credits[0].backlog.len,
                    debug_vc->mrail.rails[0].send_wqes_avail,
                    debug_vc->mrail.rails[1].send_wqes_avail,
                    debug_vc->mrail.rails[0].ext_sendq_head,
                    debug_vc->mrail.rails[1].ext_sendq_head
                   );
*/
        }
    } else {
        debug = 0;
        debug_vc = vc;
    }
#endif
    MPIDI_FUNC_EXIT(MPID_GEN2_MRAILI_CQ_POLL);
    return type;
}

void async_thread(void *context)
{
    struct ibv_async_event event;
    struct ibv_srq_attr srq_attr;
    int post_new, i, hca_num = -1;
#ifdef _ENABLE_XRC_
    int xrc_event = 0; 
#endif

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    while (1) {
        if (ibv_get_async_event((struct ibv_context *) context, &event)) {
            fprintf(stderr, "Error getting event!\n"); 
        }

        for(i = 0; i < rdma_num_hcas; i++) {
            if(MPIDI_CH3I_RDMA_Process.nic_context[i] == context) {
                hca_num = i;
            }
        }

        pthread_mutex_lock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[hca_num]);
#ifdef _ENABLE_XRC_        
        if (event.event_type & IBV_XRC_QP_EVENT_FLAG) {
            event.event_type ^= IBV_XRC_QP_EVENT_FLAG;
            xrc_event = 1;
        }
#endif

        switch (event.event_type) {
            /* Fatal */
            case IBV_EVENT_CQ_ERR:
            case IBV_EVENT_QP_FATAL:
            case IBV_EVENT_QP_REQ_ERR:
            case IBV_EVENT_QP_ACCESS_ERR:
                ibv_va_error_abort(GEN_EXIT_ERR, "Got FATAL event %d\n",
                        event.event_type);
                break;
            case IBV_EVENT_PATH_MIG_ERR:
#ifdef DEBUG
                if(MPIDI_CH3I_RDMA_Process.has_apm) {
                    DEBUG_PRINT("Path Migration Failed\n");
                }
#endif /* ifdef DEBUG */
                ibv_va_error_abort(GEN_EXIT_ERR, "Got FATAL event %d\n",
                        event.event_type);
                break;
            case IBV_EVENT_PATH_MIG:
                if(MPIDI_CH3I_RDMA_Process.has_apm && !apm_tester){
                    DEBUG_PRINT("Path Migration Successful\n");
                    reload_alternate_path((&event)->element.qp);
                }

                if(!MPIDI_CH3I_RDMA_Process.has_apm) {
                    ibv_va_error_abort(GEN_EXIT_ERR, "Got FATAL event %d\n",
                            event.event_type);
                }
                
                break;

            case IBV_EVENT_DEVICE_FATAL:
            case IBV_EVENT_SRQ_ERR:
                ibv_va_error_abort(GEN_EXIT_ERR, "Got FATAL event %d\n",
                        event.event_type);
                break;

            case IBV_EVENT_COMM_EST:
            case IBV_EVENT_PORT_ACTIVE:
            case IBV_EVENT_SQ_DRAINED:
            case IBV_EVENT_PORT_ERR:
            case IBV_EVENT_LID_CHANGE:
            case IBV_EVENT_PKEY_CHANGE:
            case IBV_EVENT_SM_CHANGE:
            case IBV_EVENT_QP_LAST_WQE_REACHED:
                break;

            case IBV_EVENT_SRQ_LIMIT_REACHED:

                pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

                if(-1 == hca_num) {
                    /* Was not able to find the context,
                     * error condition */
                    ibv_error_abort(GEN_EXIT_ERR,
                            "Couldn't find out SRQ context\n");
                }

                /* dynamically re-size the srq to be larger */
                viadev_srq_fill_size *= 2;
                if (viadev_srq_fill_size > viadev_srq_alloc_size) {
                    viadev_srq_fill_size = viadev_srq_alloc_size;
                }

                rdma_credit_preserve = (viadev_srq_fill_size > 200) ?
                     (viadev_srq_fill_size - 100) : (viadev_srq_fill_size / 2);
                
                /* Need to post more to the SRQ */
                post_new = MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num];

                MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] +=
                    viadev_post_srq_buffers(viadev_srq_fill_size -
                            viadev_srq_limit, hca_num);

                post_new = MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] - 
                    post_new;

                pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.
                        srq_post_spin_lock);

                if(!post_new) {
                    pthread_mutex_lock(
                            &MPIDI_CH3I_RDMA_Process.
                            srq_post_mutex_lock[hca_num]);

                    ++MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[hca_num];

                    while(MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[hca_num] >= 1 
                            && !(*(volatile int*)&MPIDI_CH3I_RDMA_Process.is_finalizing)) {
                        /* Cannot post to SRQ, since all WQEs
                         * might be waiting in CQ to be pulled out */
                        pthread_cond_wait(
                                &MPIDI_CH3I_RDMA_Process.
                                srq_post_cond[hca_num],
                                &MPIDI_CH3I_RDMA_Process.
                                srq_post_mutex_lock[hca_num]);
                    }
                    pthread_mutex_unlock(&MPIDI_CH3I_RDMA_Process.
                            srq_post_mutex_lock[hca_num]);
                } else {
                    /* Was able to post some, so erase old counter */
                    if(MPIDI_CH3I_RDMA_Process.
                            srq_zero_post_counter[hca_num]) {
                        MPIDI_CH3I_RDMA_Process.
                            srq_zero_post_counter[hca_num] = 0;
                    }
                }

                pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

                srq_attr.max_wr = viadev_srq_fill_size;
                srq_attr.max_sge = 1;
                srq_attr.srq_limit = viadev_srq_limit;

                if (ibv_modify_srq(MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                            &srq_attr, IBV_SRQ_LIMIT)) {
                    ibv_va_error_abort(GEN_EXIT_ERR,
                            "Couldn't modify SRQ limit (%u) after posting %d\n",
                            viadev_srq_limit, post_new);
                }

                pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

                break;
            default:
                fprintf(stderr,
                        "Got unknown event %d ... continuing ...\n",
                        event.event_type);
        }
#ifdef _ENABLE_XRC_
        if (xrc_event) {
            event.event_type |= IBV_XRC_QP_EVENT_FLAG;
            xrc_event = 0;
        }
#endif

        ibv_ack_async_event(&event);
        pthread_mutex_unlock(&MPIDI_CH3I_RDMA_Process.async_mutex_lock[hca_num]);
    }
}

/* This function is used for implmeneting  "Alternate Path Specification"
 * and "Path Loading Request Module", (SMTPS 2007 Paper) */

/* Description:
 * sl: service level, which can be changed once the QoS is enabled 
 * 
 * alt_timeout: alternate timeout, which can be increased to maximum, which
 * may make the failover little slow
 *
 * path_mig_state: Path migration state, since we are combining the modules
 * this should be the path migration state of the QP
 *
 * pkey_index: Index of the partition
 *
 * static_rate: typically "0" is the safest, which corresponds to the
 * maximum value of the static rate
 *
 * Alternate Path can be specified in multiple ways:
 * Using Same Port, but different LMC (common case)
 * rdma_num_qp_per_port refers to the number of QPs per port. The QPs
 * will use the first "rdma_num_qp_per_port" set of paths, and their
 * alternate paths are specified by a displacement of rdma_num_qp_per_port.
 * As an example, with rdma_num_qp_per_port = 4, first QP will use path0 and
 * path4, second QP will use path1 and path5 and so on, as the primary and
 * alternate path respectively.
 *
 * Finally, Since this function also implements Path Loading Rquest Module,
 * it should modify the QP with the specification of the alternate path */

int reload_alternate_path(struct ibv_qp *qp)
{

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    enum ibv_qp_attr_mask attr_mask;

    lock_apm();

    /* For Sanity */
    MPIU_Memset(&attr, 0, sizeof attr);
    MPIU_Memset(&init_attr, 0, sizeof init_attr);

    attr_mask = 0;

    if (ibv_query_qp(qp, &attr,
                attr_mask, &init_attr)) {
        ibv_error_abort(GEN_EXIT_ERR, "Failed to query QP\n");
    }

    /* This value should change with enabling of QoS */
    attr.alt_ah_attr.sl =  attr.ah_attr.sl;
    attr.alt_ah_attr.static_rate = attr.ah_attr.static_rate;
    attr.alt_ah_attr.port_num =  attr.ah_attr.port_num;
    attr.alt_ah_attr.is_global =  attr.ah_attr.is_global;
    attr.alt_timeout = attr.timeout;
    attr.alt_port_num = attr.port_num;
    attr.alt_ah_attr.src_path_bits =
        (attr.ah_attr.src_path_bits + rdma_num_qp_per_port) %
        power_two(MPIDI_CH3I_RDMA_Process.lmc);
    attr.alt_ah_attr.dlid =
        attr.ah_attr.dlid - attr.ah_attr.src_path_bits
        + attr.alt_ah_attr.src_path_bits;
    attr.path_mig_state = IBV_MIG_REARM;
    attr_mask = 0;
    attr_mask |= IBV_QP_ALT_PATH;
    attr_mask |= IBV_QP_PATH_MIG_STATE;

    if (ibv_modify_qp(qp, &attr, attr_mask))
    {
        ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP\n");
    }

    unlock_apm();
    
    return 0;
}

/* This function is used for implementing "Path Migration Module"
 * (SMTPS 2007) */

/* Description:
 * A lock is used to make sure that either the main thread or the
 * asynchronous thread is able to query the QP and set the parameters
 *
 * APM is not performed on every iteration, it is extremely slow, APM_COUNT
 * is used to make sure that it occurs only once during APM_COUNT
 */

int perform_manual_apm(struct ibv_qp* qp)
{
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    enum ibv_qp_attr_mask attr_mask;
    static int count_to_apm = -1;

    ++count_to_apm;

    if (count_to_apm)
    {
        return 0;
    }
    
    /* For Sanity */    
    MPIU_Memset(&attr, 0, sizeof attr);
    MPIU_Memset(&init_attr, 0, sizeof init_attr);
    attr_mask = 0;
    lock_apm();
    ibv_query_qp(qp, &attr,
                attr_mask, &init_attr);

    if (IBV_MIG_ARMED == attr.path_mig_state)
    {
        attr.path_mig_state = IBV_MIG_MIGRATED;
        MPIU_Assert(attr.qp_state == IBV_QPS_RTS);
        ibv_modify_qp(qp, &attr,
                    IBV_QP_PATH_MIG_STATE);
    }
    
    unlock_apm(); 
    return 0;
}

void
MPIDI_CH3I_Cleanup_after_connection(MPIDI_VC_t *vc)
{
    int i;
#define pset        MPIDI_CH3I_RDMA_Process.polling_set
#define pgrsz       MPIDI_CH3I_RDMA_Process.polling_group_size

    if(0 == pgrsz)
        return;

    for(i = 0; i < pgrsz; i++) {
        if(vc == pset[i]) {
            pset[i] = pset[pgrsz - 1];
            pgrsz -= 1;
            DEBUG_PRINT("VC removed from polling set\n");
            return;
        }
    }
#undef pgrsz
#undef pset
}

int
MPIDI_CH3I_Check_pending_send(MPIDI_VC_t *vc)
{
    int i;
    for(i = 0; i < rdma_num_rails; i++) {
        if(vc->mrail.rails[i].send_wqes_avail != rdma_default_max_send_wqe) {
            vc->free_vc = 1;
            return 1;
        }
    }

    return 0;
}
