/*
 * Copyright (C) 1999-2001 The Regents of the University of California
 * (through E.O. Lawrence Berkeley National Laboratory), subject to
 * approval by the U.S. Department of Energy.
 *
 * Use of this software is under license. The license agreement is included
 * in the file MVICH_LICENSE.TXT.
 *
 * Developed at Berkeley Lab as part of MVICH.
 *
 * Authors: Bill Saphir      <wcsaphir@lbl.gov>
 *          Michael Welcome  <mlwelcome@lbl.gov>
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
#include <mpimem.h>

#include "mem_hooks.h"
#include <infiniband/verbs.h>
#include "pmi.h"
#include "rdma_impl.h"
#include "vbuf.h"
#include "dreg.h"
#include "mpiutil.h"
#include <errno.h>
#include <string.h>
#include <debug_utils.h>

/* head of list of allocated vbuf regions */
static vbuf_region *vbuf_region_head = NULL;
/*
 * free_vbuf_head is the head of the free list
 */
static vbuf *free_vbuf_head = NULL;

/*
 * cache the nic handle, and ptag the first time a region is
 * allocated (at init time) for later additional vbur allocations
 */
static struct ibv_pd *ptag_save[MAX_NUM_HCAS];

static int vbuf_n_allocated = 0;
static long num_free_vbuf = 0;
static long num_vbuf_get = 0;
static long num_vbuf_freed = 0;

#ifdef _ENABLE_UD_
static vbuf *ud_free_vbuf_head = NULL;
static int ud_vbuf_n_allocated = 0;
static long ud_num_free_vbuf = 0;
static long ud_num_vbuf_get = 0;
static long ud_num_vbuf_freed = 0;
#endif

static pthread_spinlock_t vbuf_lock;

#if defined(DEBUG)
void dump_vbuf(char* msg, vbuf* v)
{
    int i = 0;
    int len = 100;
    MPIDI_CH3I_MRAILI_Pkt_comm_header* header = v->pheader;
    DEBUG_PRINT("%s: dump of vbuf %p, type = %d\n", msg, v, header->type);
    len = 100;

    for (; i < len; ++i)
    {
        if (0 == i % 16)
        {
            DEBUG_PRINT("\n  ");
        }

        DEBUG_PRINT("%2x  ", (unsigned int) v->buffer[i]);
    }

    DEBUG_PRINT("\n");
    DEBUG_PRINT("  END OF VBUF DUMP\n");
}
#endif /* defined(DEBUG) */

void print_vbuf_usage()
{
    int tot_mem = 0;
    
    tot_mem = (vbuf_n_allocated * (rdma_vbuf_total_size + sizeof(struct vbuf)));

#ifdef _ENABLE_UD_
    tot_mem += (ud_vbuf_n_allocated * (rdma_default_ud_mtu + sizeof(struct vbuf))); 
    PRINT_INFO(DEBUG_MEM_verbose, "RC VBUFs:%d  UD VBUFs:%d TOT MEM:%d kB\n",
                    vbuf_n_allocated, ud_vbuf_n_allocated, (tot_mem / 1024));
#else
    PRINT_INFO(DEBUG_MEM_verbose, "RC VBUF: %d  TOT MEM: %d kB\n", 
                        vbuf_n_allocated, (tot_mem / 1024));
#endif
}

int init_vbuf_lock(void)
{
    int mpi_errno = MPI_SUCCESS;

    if (pthread_spin_init(&vbuf_lock, 0))
    {
        mpi_errno = MPIR_Err_create_code(
            mpi_errno,
            MPIR_ERR_FATAL,
            "init_vbuf_lock",
            __LINE__,
            MPI_ERR_OTHER,
            "**fail",
            "%s: %s",
            "pthread_spin_init",
            strerror(errno));
    }

    return mpi_errno;
}

void deallocate_vbufs(int hca_num)
{
    vbuf_region *r = vbuf_region_head;

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_lock(&vbuf_lock);
    }

    while (r)
    {
        if (r->mem_handle[hca_num] != NULL
            && ibv_dereg_mr(r->mem_handle[hca_num]))
        {
            ibv_error_abort(IBV_RETURN_ERR, "could not deregister MR");
        }

        DEBUG_PRINT("deregister vbufs\n");
        r = r->next;
    }

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
         pthread_spin_unlock(&vbuf_lock);
    }
}

void deallocate_vbuf_region(void)
{
    vbuf_region *curr = vbuf_region_head;
    vbuf_region *next = NULL;

    while (curr) {
        next = curr->next;
        free(curr->malloc_start);

        if (rdma_enable_hugepage && curr->shmid >= 0) {
            shmdt(curr->malloc_buf_start);
        } else 
        {
            free(curr->malloc_buf_start);
        }

        MPIU_Free(curr);
        curr = next;
    }
}

int alloc_hugepage_region (int *shmid, void **buffer, int *nvbufs, int buf_size)
{
    int ret = 0;
    size_t size = *nvbufs * buf_size;
    MRAILI_ALIGN_LEN(size, HUGEPAGE_ALIGN);

    /* create hugepage shared region */
    *shmid = shmget(IPC_PRIVATE, size, 
                        SHM_HUGETLB | IPC_CREAT | SHM_R | SHM_W);
    if (*shmid < 0) {
        goto fn_fail;
    }

    /* attach shared memory */
    *buffer = (void *) shmat(*shmid, SHMAT_ADDR, SHMAT_FLAGS);
    if (*buffer == (void *) -1) {
        goto fn_fail;
    }
    
    /* Mark shmem for removal */
    if (shmctl(*shmid, IPC_RMID, 0) != 0) {
        fprintf(stderr, "Failed to mark shm for removal\n");
    }
    
    /* Find max no.of vbufs can fit in allocated buffer */
    *nvbufs = size / buf_size;
     
fn_exit:
    return ret;
fn_fail:
    ret = -1;
    if (rdma_enable_hugepage >= 2) {
        fprintf(stderr,"[%d] Failed to allocate buffer from huge pages. "
                       "fallback to regular pages. requested buf size:%lu\n",
                        MPIDI_Process.my_pg_rank, size);
    }
    goto fn_exit;
}    

static int allocate_vbuf_region(int nvbufs)
{

    struct vbuf_region *reg = NULL;
    void *mem = NULL;
    int i = 0;
    vbuf *cur = NULL;
    void *vbuf_dma_buffer = NULL;
    int alignment_vbuf = 64;
    int alignment_dma = getpagesize();
    int result = 0;

    DEBUG_PRINT("Allocating a new vbuf region.\n");

    if (free_vbuf_head != NULL)
    {
        ibv_error_abort(GEN_ASSERT_ERR, "free_vbuf_head = NULL");
    }

    /* are we limiting vbuf allocation?  If so, make sure
     * we dont alloc more than allowed
     */
    if (rdma_vbuf_max > 0)
    {
        nvbufs = MIN(nvbufs, rdma_vbuf_max - vbuf_n_allocated);

        if (nvbufs <= 0)
        {
            ibv_error_abort(GEN_EXIT_ERR, "VBUF alloc failure, limit exceeded");
        }
    }

    reg = (struct vbuf_region *) MPIU_Malloc (sizeof(struct vbuf_region));

    if (NULL == reg)
    {
        ibv_error_abort(GEN_EXIT_ERR, "Unable to malloc a new struct vbuf_region");
    }

    if (rdma_enable_hugepage) {
        result = alloc_hugepage_region (&reg->shmid, &vbuf_dma_buffer, &nvbufs, rdma_vbuf_total_size);
    }

    /* do posix_memalign if enable hugepage disabled or failed */
    if (rdma_enable_hugepage == 0 || result != 0 )  
    {
        reg->shmid = -1;
        result = posix_memalign(&vbuf_dma_buffer, alignment_dma, nvbufs * rdma_vbuf_total_size);
    }

    if ((result!=0) || (NULL == vbuf_dma_buffer))
    {
       ibv_error_abort(GEN_EXIT_ERR, "unable to malloc vbufs DMA buffer");
    }
    
    if (posix_memalign(
        (void**) &mem,
        alignment_vbuf,
        nvbufs * sizeof(vbuf)))
    {
        fprintf(stderr, "[%s %d] Cannot allocate vbuf region\n", __FILE__, __LINE__);
        return -1;
    }
  
    /* region should be registered for all of the hca */
    for (i=0 ; i < rdma_num_hcas; ++i)
    {
        reg->mem_handle[i] = ibv_reg_mr(
            ptag_save[i],
            vbuf_dma_buffer,
            nvbufs * rdma_vbuf_total_size,
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

        if (!reg->mem_handle[i])
        {
            /* de-register already registered with other hcas*/
            for (i = i-1; i >=0 ; --i)
            {
                if (reg->mem_handle[i] != NULL
                        && ibv_dereg_mr(reg->mem_handle[i]))
                {
                    fprintf(stderr, "[%s %d] Cannot de-register vbuf region\n", __FILE__, __LINE__);
                }
            }
            /* free allocated buffers */
            free(vbuf_dma_buffer);
            free(mem);
            MPIU_Free(reg);
            fprintf(stderr, "[%s %d] Cannot register vbuf region\n", __FILE__, __LINE__);
            return -1;
        }
    }

    MPIU_Memset(mem, 0, nvbufs * sizeof(vbuf));
    MPIU_Memset(vbuf_dma_buffer, 0, nvbufs * rdma_vbuf_total_size);

    vbuf_n_allocated += nvbufs;
    num_free_vbuf += nvbufs;
    reg->malloc_start = mem;
    reg->malloc_buf_start = vbuf_dma_buffer;
    reg->malloc_end = (void *) ((char *) mem + nvbufs * sizeof(vbuf));
    reg->malloc_buf_end = (void *) ((char *) vbuf_dma_buffer + nvbufs * rdma_vbuf_total_size);

    reg->count = nvbufs;
    free_vbuf_head = mem;
    reg->vbuf_head = free_vbuf_head;

    DEBUG_PRINT(
        "VBUF REGION ALLOCATION SZ %d TOT %d FREE %ld NF %ld NG %ld\n",
        nvbufs,
        vbuf_n_allocated,
        num_free_vbuf,
        num_vbuf_freed,
        num_vbuf_get);

    /* init the free list */
    for (i = 0; i < nvbufs - 1; ++i)
    {
        cur = free_vbuf_head + i;
        cur->desc.next = free_vbuf_head + i + 1;
        cur->region = reg;
	cur->head_flag = (VBUF_FLAG_TYPE *) ((char *)vbuf_dma_buffer
            + (i + 1) * rdma_vbuf_total_size
            - sizeof * cur->head_flag);
        cur->buffer = (unsigned char *) ((char *)vbuf_dma_buffer
            + i * rdma_vbuf_total_size);

        cur->eager = 0;
        cur->content_size = 0;
        cur->coalesce = 0;
    }

    /* last one needs to be set to NULL */
    cur = free_vbuf_head + nvbufs - 1;
    cur->desc.next = NULL;
    cur->region = reg;
    cur->head_flag = (VBUF_FLAG_TYPE *) ((char *)vbuf_dma_buffer
        + nvbufs * rdma_vbuf_total_size
        - sizeof * cur->head_flag);
    cur->buffer = (unsigned char *) ((char *)vbuf_dma_buffer
        + (nvbufs - 1) * rdma_vbuf_total_size);
    cur->eager = 0;
    cur->content_size = 0;
    cur->coalesce = 0;

    /* thread region list */
    reg->next = vbuf_region_head;
    vbuf_region_head = reg;

    return 0;
}

/* this function is only called by the init routines.
 * Cache the nic handle and ptag for later vbuf_region allocations.
 */
int allocate_vbufs(struct ibv_pd* ptag[], int nvbufs)
{
    int i = 0;

    for (; i < rdma_num_hcas; ++i)
    {
        ptag_save[i] = ptag[i];
    }

    if(allocate_vbuf_region(nvbufs) != 0) {
        ibv_va_error_abort(GEN_EXIT_ERR,
            "VBUF reagion allocation failed. Pool size %d\n", vbuf_n_allocated);
    }
    
    return 0;
}

vbuf* get_vbuf(void)
{
    vbuf* v = NULL;

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
    	pthread_spin_lock(&vbuf_lock);
    }

    /*
     * It will often be possible for higher layers to recover
     * when no vbuf is available, but waiting for more descriptors
     * to complete. For now, just abort.
     */
    if (NULL == free_vbuf_head)
    {
        if(allocate_vbuf_region(rdma_vbuf_secondary_pool_size) != 0) {
            ibv_va_error_abort(GEN_EXIT_ERR,
                "VBUF reagion allocation failed. Pool size %d\n", vbuf_n_allocated);
        }
    }

    v = free_vbuf_head;
    --num_free_vbuf;
    ++num_vbuf_get;

    /* this correctly handles removing from single entry free list */
    free_vbuf_head = free_vbuf_head->desc.next;

    /* need to change this to RPUT_VBUF_FLAG later
     * if we are doing rput */
    v->padding = NORMAL_VBUF_FLAG;
    v->pheader = (void *)v->buffer;

    /* this is probably not the right place to initialize shandle to NULL.
     * Do it here for now because it will make sure it is always initialized.
     * Otherwise we would need to very carefully add the initialization in
     * a dozen other places, and probably miss one.
     */
    v->sreq = NULL;
    v->coalesce = 0;
    v->content_size = 0;
    v->eager = 0;
    /* Decide which transport need to assign here */
    v->transport = IB_TRANSPORT_RC;

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_unlock(&vbuf_lock);
    }

    return(v);
}

void MRAILI_Release_vbuf(vbuf* v)
{
#ifdef _ENABLE_UD_
    /* This message might be in progress. Wait for ib send completion 
     * to release this buffer to avoid to reusing buffer
     */
    if(v->transport== IB_TRANSPORT_UD 
        && v->flags & UD_VBUF_SEND_INPROGRESS) {
        v->flags |= UD_VBUF_FREE_PENIDING;
        return;
    }
#endif

    /* note this correctly handles appending to empty free list */
#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_lock(&vbuf_lock);
    }

    DEBUG_PRINT("release_vbuf: releasing %p previous head = %p, padding %d\n", v, free_vbuf_head, v->padding);

#ifdef _ENABLE_UD_
    if(v->transport == IB_TRANSPORT_UD) {
        MPIU_Assert(v != ud_free_vbuf_head);
        v->desc.next = ud_free_vbuf_head;
        ud_free_vbuf_head = v;
        ++ud_num_free_vbuf;
        ++ud_num_vbuf_freed;
    } 
    else 
#endif /* _ENABLE_UD_ */
    {
        MPIU_Assert(v != free_vbuf_head);
        v->desc.next = free_vbuf_head;
        free_vbuf_head = v;
        ++num_free_vbuf;
        ++num_vbuf_freed;
    }

    if (v->padding != NORMAL_VBUF_FLAG
        && v->padding != RPUT_VBUF_FLAG
        && v->padding != RGET_VBUF_FLAG
        && v->padding != RDMA_ONE_SIDED)
    {
        ibv_error_abort(GEN_EXIT_ERR, "vbuf not correct.\n");
    }

    *v->head_flag = 0;
    v->pheader = NULL;
    v->content_size = 0;
    v->sreq = NULL;
    v->vc = NULL;

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
        || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
        || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_unlock(&vbuf_lock);
    }
}

#ifdef _ENABLE_UD_
static int allocate_ud_vbuf_region(int nvbufs)
{

    struct vbuf_region *reg = NULL;
    void *mem = NULL;
    int i = 0;
    vbuf *cur = NULL;
    void *vbuf_dma_buffer = NULL;
    int alignment_vbuf = 64;
    int alignment_dma = getpagesize();
    int result = 0;

    PRINT_DEBUG(DEBUG_UD_verbose>0,"Allocating a UD buf region.\n");

    if (ud_free_vbuf_head != NULL)
    {
        ibv_error_abort(GEN_ASSERT_ERR, "free_vbuf_head = NULL");
    }

    reg = (struct vbuf_region *) MPIU_Malloc (sizeof(struct vbuf_region));

    if (NULL == reg)
    {
        ibv_error_abort(GEN_EXIT_ERR,
                "Unable to malloc a new struct vbuf_region");
    }
    
    if (rdma_enable_hugepage) {
        result = alloc_hugepage_region (&reg->shmid, &vbuf_dma_buffer, &nvbufs,
rdma_default_ud_mtu);
    }

    /* do posix_memalign if enable hugepage disabled or failed */
    if (rdma_enable_hugepage == 0 || result != 0 )  
    {
        reg->shmid = -1;
        result = posix_memalign(&vbuf_dma_buffer, 
            alignment_dma, nvbufs * rdma_default_ud_mtu);
    }

    if ((result!=0) || (NULL == vbuf_dma_buffer))
    {
        ibv_error_abort(GEN_EXIT_ERR, "unable to malloc vbufs DMA buffer");
    }
    
    if (posix_memalign(
                (void**) &mem,
                alignment_vbuf,
                nvbufs * sizeof(vbuf)))
    {
        fprintf(stderr, "[%s %d] Cannot allocate vbuf region\n", 
                __FILE__, __LINE__);
        return -1;
    }

    MPIU_Memset(mem, 0, nvbufs * sizeof(vbuf));
    MPIU_Memset(vbuf_dma_buffer, 0, nvbufs * rdma_default_ud_mtu);

    ud_vbuf_n_allocated += nvbufs;
    ud_num_free_vbuf += nvbufs;
    reg->malloc_start = mem;
    reg->malloc_buf_start = vbuf_dma_buffer;
    reg->malloc_end = (void *) ((char *) mem + nvbufs * sizeof(vbuf));
    reg->malloc_buf_end = (void *) ((char *) vbuf_dma_buffer + 
            nvbufs * rdma_default_ud_mtu);

    reg->count = nvbufs;
    ud_free_vbuf_head = mem;
    reg->vbuf_head = ud_free_vbuf_head;

    PRINT_DEBUG(DEBUG_UD_verbose>0,
            "VBUF REGION ALLOCATION SZ %d TOT %d FREE %ld NF %ld NG %ld\n",
            rdma_default_ud_mtu,
            ud_vbuf_n_allocated,
            ud_num_free_vbuf,
            ud_num_vbuf_freed,
            ud_num_vbuf_get);

    /* region should be registered for both of the hca */
    for (; i < rdma_num_hcas; ++i)
    {
        reg->mem_handle[i] = ibv_reg_mr(
                ptag_save[i],
                vbuf_dma_buffer,
                nvbufs * rdma_default_ud_mtu,
                IBV_ACCESS_LOCAL_WRITE );

        if (!reg->mem_handle[i])
        {
            fprintf(stderr, "[%s %d] Cannot register vbuf region\n", 
                    __FILE__, __LINE__);
            return -1;
        }
    }

    /* init the free list */
    for (i = 0; i < nvbufs; ++i)
    {
        cur = ud_free_vbuf_head + i;
        cur->desc.next = ud_free_vbuf_head + i + 1;
        if (i == (nvbufs -1)) cur->desc.next = NULL;
        cur->region = reg;
        cur->head_flag = (VBUF_FLAG_TYPE *) ((char *)vbuf_dma_buffer
                + (i + 1) * rdma_default_ud_mtu - sizeof * cur->head_flag);
        cur->buffer = (unsigned char *) ((char *)vbuf_dma_buffer
                + i * rdma_default_ud_mtu);

        cur->eager = 0;
        cur->content_size = 0;
        cur->coalesce = 0;
    }

    /* thread region list */
    reg->next = vbuf_region_head;
    vbuf_region_head = reg;

    return 0;
}

int allocate_ud_vbufs(int nvbufs)
{
    return allocate_ud_vbuf_region(nvbufs);
}

vbuf* get_ud_vbuf(void)
{
    vbuf* v = NULL;

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
            || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
            || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_lock(&vbuf_lock);
    }

    if (NULL == ud_free_vbuf_head)
    {
        if(allocate_ud_vbuf_region(rdma_vbuf_secondary_pool_size) != 0) {
            ibv_va_error_abort(GEN_EXIT_ERR,
                    "UD VBUF reagion allocation failed. Pool size %d\n", vbuf_n_allocated);
        }
    }


    v = ud_free_vbuf_head;
    --ud_num_free_vbuf;
    ++ud_num_vbuf_get;

    /* this correctly handles removing from single entry free list */
    ud_free_vbuf_head = ud_free_vbuf_head->desc.next;

    /* need to change this to RPUT_VBUF_FLAG later
     * if we are doing rput */
    v->padding = NORMAL_VBUF_FLAG;
    v->pheader = (void *)v->buffer;
    v->transport = IB_TRANSPORT_UD;
    v->retry_count = 0;
    v->flags = 0;

    /* this is probably not the right place to initialize shandle to NULL.
     * Do it here for now because it will make sure it is always initialized.
     * Otherwise we would need to very carefully add the initialization in
     * a dozen other places, and probably miss one.
     */
    v->sreq = NULL;
    v->coalesce = 0;
    v->content_size = 0;
    v->eager = 0;
    /* Decide which transport need to assign here */

#if !defined(CKPT)
    if (MPIDI_CH3I_RDMA_Process.has_srq
#if defined(RDMA_CM)
            || MPIDI_CH3I_RDMA_Process.use_rdma_cm_on_demand
#endif /* defined(RDMA_CM) */
            || MPIDI_CH3I_Process.cm_type == MPIDI_CH3I_CM_ON_DEMAND)
#endif /* !defined(CKPT) */
    {
        pthread_spin_unlock(&vbuf_lock);
    }

    return(v);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_ud_recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_ud_recv(vbuf* v, unsigned long len, int hca_num)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_UD_RECV);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_UD_RECV);

    MPIU_Assert(v != NULL);

    v->desc.u.rr.next = NULL;
    v->desc.u.rr.wr_id = (uintptr_t) v;
    v->desc.u.rr.num_sge = 1;
    v->desc.u.rr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle[hca_num]->lkey;
    v->desc.sg_entry.addr = (uintptr_t)(v->buffer);
    v->padding = NORMAL_VBUF_FLAG;
    v->rail = hca_num;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RECV);
}

#endif /* _ENABLE_UD_ */

void MRAILI_Release_recv_rdma(vbuf* v)
{
    vbuf *next_free = NULL;
    MPIDI_VC_t * c = (MPIDI_VC_t *)v->vc;
    int i;

    int next = c->mrail.rfp.p_RDMA_recv_tail + 1;

    if (next >= num_rdma_buffer)
    {
        next = 0;
    }

    next_free = &(c->mrail.rfp.RDMA_recv_buf[next]);
    v->padding = FREE_FLAG;
    *v->head_flag = 0;
    v->sreq = NULL;
    v->content_size = 0;

    if (v != next_free)
    {
        return;
    }

    /* search all free buffers */
    for (i = next; i != c->mrail.rfp.p_RDMA_recv;)
    {
        if (c->mrail.rfp.RDMA_recv_buf[i].padding == FREE_FLAG)
        {
            ++c->mrail.rfp.rdma_credit;

            if (++c->mrail.rfp.p_RDMA_recv_tail >= num_rdma_buffer)
            {
                c->mrail.rfp.p_RDMA_recv_tail = 0;
            }

            c->mrail.rfp.RDMA_recv_buf[i].padding = BUSY_FLAG;
            *c->mrail.rfp.RDMA_recv_buf[i].head_flag = 0;
        }
        else
        {
            break;
        }

        if (++i >= num_rdma_buffer)
        {
            i = 0;
        }
    }
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_rdma_write
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_rdma_write(vbuf* v)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RDMA_WRITE);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RDMA_WRITE);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.opcode = IBV_WR_RDMA_WRITE;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.wr_id = (uintptr_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->padding = FREE_FLAG;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RDMA_WRITE);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_send
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_send(vbuf* v, unsigned long len, int rail)
{
    int hca_num = rail / (rdma_num_rails / rdma_num_hcas);

    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_SEND);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_SEND);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_SEND;
    v->desc.u.sr.wr_id = (uintptr_t) v;
    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle[hca_num]->lkey;
    v->desc.sg_entry.addr = (uintptr_t)(v->buffer);
    v->padding = NORMAL_VBUF_FLAG;
    v->rail = rail;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_SEND);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_recv
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_recv(vbuf* v, unsigned long len, int rail)
{
    int hca_num = rail / (rdma_num_rails / rdma_num_hcas);

    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RECV);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RECV);

    MPIU_Assert(v != NULL);

    v->desc.u.rr.next = NULL;
    v->desc.u.rr.wr_id = (uintptr_t) v;
    v->desc.u.rr.num_sge = 1;
    v->desc.u.rr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = v->region->mem_handle[hca_num]->lkey;
    v->desc.sg_entry.addr = (uintptr_t)(v->buffer);
    v->padding = NORMAL_VBUF_FLAG;
    v->rail = rail;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RECV);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_rget
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_rget(
    vbuf* v,
    void* local_address,
    uint32_t lkey, 
    void* remote_address,
    uint32_t rkey,
    int len,
    int rail)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RGET);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RGET);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_READ;
    v->desc.u.sr.wr_id = (uintptr_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t)(remote_address);
    v->desc.u.sr.wr.rdma.rkey = rkey;

    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t)(local_address);
    v->padding = RGET_VBUF_FLAG;
    v->rail = rail;	
    DEBUG_PRINT("RDMA Read\n");

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RGET);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_rput
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_rput(
    vbuf* v,
    void* local_address,
    uint32_t lkey,
    void* remote_address,
    uint32_t rkey,
    int len,
    int rail)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RPUT);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RPUT);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_WRITE;
    v->desc.u.sr.wr_id = (uintptr_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t)(remote_address);
    v->desc.u.sr.wr.rdma.rkey = rkey;

    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t)(local_address);
    v->padding = RPUT_VBUF_FLAG;
    v->rail = rail;	
    DEBUG_PRINT("RDMA write\n");

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RPUT);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_rma_get
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_rma_get(vbuf *v, void *l_addr, uint32_t lkey,
                       void *r_addr, uint32_t rkey, int len, int rail)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RMA_GET);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RMA_GET);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_READ;
    v->desc.u.sr.wr_id = (uintptr_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t)(r_addr);
    v->desc.u.sr.wr.rdma.rkey = rkey;

    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t)(l_addr);
    v->padding = RDMA_ONE_SIDED;
    v->rail = rail;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RMA_GET);
}

#undef FUNCNAME
#define FUNCNAME vbuf_init_rma_put
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_init_rma_put(vbuf *v, void *l_addr, uint32_t lkey,
                       void *r_addr, uint32_t rkey, int len, int rail)
{
    MPIDI_STATE_DECL(MPID_STATE_VBUF_INIT_RMA_PUT);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_INIT_RMA_PUT);

    v->desc.u.sr.next = NULL;
    v->desc.u.sr.send_flags = IBV_SEND_SIGNALED;
    v->desc.u.sr.opcode = IBV_WR_RDMA_WRITE;
    v->desc.u.sr.wr_id = (uintptr_t) v;

    v->desc.u.sr.num_sge = 1;
    v->desc.u.sr.wr.rdma.remote_addr = (uintptr_t)(r_addr);
    v->desc.u.sr.wr.rdma.rkey = rkey;

    v->desc.u.sr.sg_list = &(v->desc.sg_entry);
    v->desc.sg_entry.length = len;
    v->desc.sg_entry.lkey = lkey;
    v->desc.sg_entry.addr = (uintptr_t)(l_addr);
    v->padding = RDMA_ONE_SIDED;
    v->rail = rail;

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_INIT_RMA_PUT);
}

#if defined(CKPT)

#undef FUNCNAME
#define FUNCNAME vbuf_reregister_all
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
void vbuf_reregister_all()
{
    int i = 0;
    vbuf_region *vr = vbuf_region_head;

    MPIDI_STATE_DECL(MPID_STATE_VBUF_REREGISTER_ALL);
    MPIDI_FUNC_ENTER(MPID_STATE_VBUF_REREGISTER_ALL);

    for (; i < rdma_num_hcas; ++i)
    {
        ptag_save[i] = MPIDI_CH3I_RDMA_Process.ptag[i];
    }

    while (vr)
    {
        for (i = 0; i < rdma_num_hcas; ++i)
        {
            vr->mem_handle[i] = ibv_reg_mr(
                ptag_save[i],
                vr->malloc_buf_start,
                vr->count * rdma_vbuf_total_size,
                IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

            if (!vr->mem_handle[i])
            {
                ibv_error_abort(IBV_RETURN_ERR,"Cannot reregister vbuf region\n");
            }
        }

        vr = vr->next;
    }

    MPIDI_FUNC_EXIT(MPID_STATE_VBUF_REREGISTER_ALL);
}
#endif /* defined(CKPT) */

/* vi:set sw=4 tw=80: */
