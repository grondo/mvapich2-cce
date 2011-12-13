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
 *
 * Thanks to Voltaire for contributing enhancements to
 * the registration cache implementation.
 */

#include "mpidi_ch3i_rdma_conf.h"

#include <mpimem.h>
#include <stdlib.h>

#include "dreg.h"
#include "avl.h"
#include "rdma_impl.h"
#include "udapl_util.h"
#include "mpiutil.h"

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

/*
 * dreg.c: everything having to do with dynamic memory registration. 
 */

#if !defined(DISABLE_PTMALLOC)
static pthread_spinlock_t dreg_lock = 0;
static pthread_spinlock_t dereg_lock = 0;
static pthread_t          th_id_of_lock = -1;
static pthread_t th_id_of_dereg_lock = -1;
#endif

/* statistic */
unsigned long dreg_stat_cache_hit=0;
unsigned long dreg_stat_cache_miss=0;
unsigned long dreg_stat_evicted=0;
static unsigned long pinned_pages_count;

struct dreg_entry *dreg_free_list;
struct dreg_entry *dreg_unused_list;
struct dreg_entry *dreg_unused_tail;

int g_is_dreg_initialized = 0;
#ifdef CKPT
struct dreg_entry *dreg_all_list;
#endif

#define DREG_BEGIN(R) ((R)->pagenum)
#define DREG_END(R) ((R)->pagenum + (R)->npages - 1)

/* list element */
typedef struct _entry
{
    dreg_entry *reg;
    struct _entry *next;
} entry_t;

typedef struct _vma 
{
    unsigned long start;      /* first page number of the area */
    unsigned long end;        /* last page number of the area */
    entry_t *list;	  	  /* all dregs on this virtual memory region */
    unsigned long list_count;  /* number of elements on the list */
    struct _vma *next, *prev; /* double linked list of vmas */
} vma_t;

vma_t vma_list;
AVL_TREE *vma_tree;

#ifndef DISABLE_PTMALLOC

/* Array which stores the memory regions 
 * ptrs which are to be deregistered after 
 * free hook pulls them out of the reg cache
 */
static dreg_entry **deregister_mr_array;

/* Number of pending deregistration
 * operations 
 * Note: This number can never exceed
 * the total number of reg. cache
 * entries
 */
static int n_dereg_mr;

struct iovec *delayed_buf_region;
int buf_reg_count;

/* Keep free list of VMA data structs
 * and entries */
static vma_t vma_free_list;
static entry_t entry_free_list;

#define INIT_FREE_LIST(_list) {                                 \
    (_list)->next = NULL;                                       \
}

#define ADD_FREE_LIST(_list, _v) {                              \
    (_v)->next = (_list)->next;                                 \
    (_list)->next = (_v);                                       \
}

#define GET_FREE_LIST(_list, _v) {                              \
    *(_v) = (_list)->next;                                      \
    if((_list)->next) {                                         \
        (_list)->next = (_list)->next->next;                    \
    }                                                           \
}

#endif

static inline int dreg_remove (dreg_entry *r);

/* Tree functions */
static long vma_compare (void *a, void *b)
{
    const vma_t *vma1 = *(vma_t**)a, *vma2 = *(vma_t**)b;
    return vma1->start - vma2->start;
}

static long vma_compare_search (void *a, void *b)
{
    const vma_t *vma = *(vma_t**)b;
    const unsigned long addr = (unsigned long)a;

    if (vma->end < addr)
        return 1;

    if (vma->start <= addr)
        return 0;

    return -1;
}

static long vma_compare_closest (void *a, void *b)
{
    const vma_t *vma = *(vma_t**)b;
    const unsigned long addr = (unsigned long)a;

    if (vma->end < addr)
        return 1;

    if (vma->start <= addr)
        return 0;

    if (vma->prev->end < addr)
        return 0;

    return -1;
}

static inline vma_t *vma_search (unsigned long addr)
{
    vma_t **vma;
    vma = avlfindex (vma_compare_search, (void*)addr, vma_tree);
    return vma?(*vma):NULL;
}


static unsigned long  avldatasize (void)
{
    return (unsigned long) (sizeof (void *));
}

static inline vma_t *vma_new (unsigned long start, unsigned long end)
{
    vma_t *vma;

    if (rdma_dreg_cache_limit &&
            pinned_pages_count + (end - start + 1) > 
            rdma_dreg_cache_limit)
        return NULL;

#ifndef DISABLE_PTMALLOC
    GET_FREE_LIST(&vma_free_list, &vma);

    if(NULL == vma) {
        vma = MPIU_Malloc(sizeof (vma_t));
    }
#else
    vma = MPIU_Malloc (sizeof (vma_t));
#endif

    if (vma == NULL)
        return NULL;

    vma->start = start;
    vma->end = end;
    vma->next = vma->prev = NULL;
    vma->list = NULL;
    vma->list_count = 0;

    avlins (&vma, vma_tree);

    pinned_pages_count += (vma->end - vma->start + 1);

    return vma;
}

static inline void vma_remove (vma_t *vma)
{
    avldel (&vma, vma_tree);
    pinned_pages_count -= (vma->end - vma->start + 1);
}

static inline void vma_destroy (vma_t *vma)
{
    entry_t *e = vma->list;

    while (e)
    {
        entry_t *t = e;
        e = e->next;
#ifndef DISABLE_PTMALLOC
        ADD_FREE_LIST(&entry_free_list, t);
#else
        MPIU_Free (t);
#endif
    }

#ifndef DISABLE_PTMALLOC
    ADD_FREE_LIST(&vma_free_list, vma);
#else
    MPIU_Free (vma);
#endif
}

static inline long compare_dregs (entry_t *e1, entry_t *e2)
{
    if (DREG_END (e1->reg) != DREG_END (e2->reg))
        return DREG_END (e1->reg) - DREG_END (e2->reg);

    /* tie breaker */
    return (unsigned long)e1->reg - (unsigned long)e2->reg;
}

/* add entry to list of dregs. List sorted by region last page number */
static inline void add_entry (vma_t *vma, dreg_entry *r)
{
    entry_t **i, *e;

#ifndef DISABLE_PTMALLOC
    GET_FREE_LIST(&entry_free_list, &e);

    if(NULL == e) {
        e = MPIU_Malloc(sizeof(entry_t));
    }
#else
    e = MPIU_Malloc (sizeof (entry_t));
#endif
    if (e == NULL)
        return;

    e->reg = r;

    for (i = &vma->list; *i != NULL && compare_dregs (*i, e) > 0; i=&(*i)->next);

    e->next = (*i);
    (*i) = e;
    vma->list_count++;
}

static inline void remove_entry (vma_t *vma, dreg_entry *r)
{
    entry_t **i;

    for (i = &vma->list; *i != NULL && (*i)->reg != r; i=&(*i)->next);

    if (*i)
    {
        entry_t *e = *i;
        *i = (*i)->next;
#ifndef DISABLE_PTMALLOC
        ADD_FREE_LIST(&entry_free_list, e);
#else
        MPIU_Free (e);
#endif
        vma->list_count--;
    }
}

static inline void copy_list (vma_t *to, vma_t *from)
{
    entry_t *f = from->list, **t = &to->list;

    while (f)
    {
        entry_t *e;

#ifndef DISABLE_PTMALLOC
        GET_FREE_LIST(&entry_free_list, &e);

        if(NULL == e) {
            e = MPIU_Malloc(sizeof(entry_t));
        }
#else
        e = MPIU_Malloc (sizeof (entry_t));
#endif

        e->reg = f->reg;
        e->next = NULL;

        *t = e;
        t = &e->next;
        f = f->next;
    }
    to->list_count = from->list_count;
}

/* returns 1 iff two lists contain the same entries */
static inline int compare_lists (vma_t *vma1, vma_t *vma2)
{
    entry_t *e1 = vma1->list, *e2 = vma2->list;

    if (vma1->list_count != vma2->list_count)
        return 0;

    while (1)
    {
        if (e1 == NULL || e2 == NULL)
            return 1;

        if (e1->reg != e2->reg)
            break;

        e1 = e1->next;
        e2 = e2->next;
    }

    return 0;
}

static inline int dreg_remove (dreg_entry *r)
{
    vma_t  *vma;


    vma = vma_search (DREG_BEGIN (r));


    if (vma == NULL)  /* no such region in database */
        return -1;

    while (vma != &vma_list && vma->start <= DREG_END (r))
    {
        remove_entry (vma, r);


        if (vma->list == NULL)
        {
            vma_t *next = vma->next;

            vma_remove (vma);
            vma->prev->next = vma->next;
            vma->next->prev = vma->prev;
            vma_destroy (vma);
            vma = next;
        }
        else
        {
            int merged;

            do {
                merged = 0;
                if (vma->start == vma->prev->end + 1 &&
                        compare_lists (vma, vma->prev))
                {
                    vma_t *t = vma;
                    vma = vma->prev;
                    vma->end = t->end;
                    vma->next = t->next;
                    vma->next->prev = vma;
                    vma_remove (t);
                    vma_destroy (t);
                    merged = 1;
                }
                if (vma->end + 1 == vma->next->start &&
                        compare_lists (vma, vma->next))
                {
                    vma_t *t = vma->next;
                    vma->end = t->end;
                    vma->next = t->next;
                    vma->next->prev = vma;
                    vma_remove (t);
                    vma_destroy (t);
                    merged = 1;
                }
            } while (merged);
            vma = vma->next;
        }
    }

    return 0;
}

static int dreg_insert (dreg_entry *r)
{
    vma_t *i = &vma_list, **v;
    unsigned long begin = DREG_BEGIN (r), end = DREG_END (r);

    v = avlfindex (vma_compare_closest, (void*)begin, vma_tree);

    if (v)
        i = *v;

    while (begin <= end)
    {
        vma_t *vma;

        if (i == &vma_list)
        {
            vma = vma_new (begin, end);

            if (!vma)
                goto remove;

            vma->next = i;
            vma->prev = i->prev;
            i->prev->next = vma;
            i->prev = vma;

            begin = vma->end + 1;

            add_entry (vma, r);
        } 
        else if (i->start > begin)
        {
            vma = vma_new (begin, 
                    (i->start <= end)?(i->start - 1):end);

            if (!vma)
                goto remove;

            /* insert before */
            vma->next = i;
            vma->prev = i->prev;
            i->prev->next = vma;
            i->prev = vma;

            i = vma;

            begin = vma->end + 1;

            add_entry (vma, r);
        }
        else if (i->start == begin)
        {
            if (i->end > end)
            {
                vma = vma_new (end+1, i->end);

                if (!vma)
                    goto remove;

                i->end = end;

                copy_list (vma, i);

                /* add after */
                vma->next = i->next;
                vma->prev = i;
                i->next->prev = vma;
                i->next = vma;

                add_entry (i, r);
                begin = end + 1;
            }
            else
            {
                add_entry (i, r);
                begin = i->end + 1;
            }
        }
        else
        {
            vma = vma_new (begin, i->end);

            if (!vma)
                goto remove;

            i->end = begin - 1;

            copy_list (vma, i);

            /* add after */
            vma->next = i->next;
            vma->prev = i;
            i->next->prev = vma;
            i->next = vma;
        }

        i = i->next;
    }
    return 0;

remove:
    dreg_remove (r);
    return -1;
}


static inline dreg_entry *dreg_lookup (unsigned long begin, unsigned long end )
{
    vma_t *vma;

    vma = vma_search (begin);

    if (!vma)
        return NULL;

#ifdef CKPT
#if 0
    if (vma->list->reg->npages==0)
    {
        return NULL;
    }
#endif    
#endif

    if(!vma->list)
        return NULL;

    if (DREG_END (vma->list->reg) >= end)
        return vma->list->reg;

    return NULL;
}

void vma_db_init (void)
{
    vma_tree = avlinit (vma_compare, avldatasize);
    vma_list.next = &vma_list;
    vma_list.prev = &vma_list;
    vma_list.list = NULL; 
    vma_list.list_count = 0;
}

void dreg_init()
{
    int i;

    pinned_pages_count = 0;
    vma_db_init ();
    dreg_free_list = (dreg_entry *)
        MPIU_Malloc((unsigned)(sizeof(dreg_entry) * rdma_ndreg_entries));

    if (dreg_free_list == NULL) {
        udapl_error_abort(GEN_EXIT_ERR,
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(dreg_entry) * rdma_ndreg_entries);
    }

    MPIU_Memset(dreg_free_list, 0, sizeof(dreg_entry) * rdma_ndreg_entries);

#ifdef CKPT
    dreg_all_list = dreg_free_list;
#endif

    for (i = 0; i < (int) rdma_ndreg_entries - 1; i++) {
        dreg_free_list[i].next = &dreg_free_list[i + 1];
    }
    dreg_free_list[rdma_ndreg_entries - 1].next = NULL;

    dreg_unused_list = NULL;
    dreg_unused_tail = NULL;
    /* cache hit and miss time stat variables initisalization */

    g_is_dreg_initialized = 1;

#ifndef DISABLE_PTMALLOC
    pthread_spin_init(&dreg_lock, 0);
    pthread_spin_init(&dereg_lock, 0);
    
    delayed_buf_region = MPIU_Malloc((unsigned)(sizeof(struct iovec) * rdma_ndreg_entries));
    if (delayed_buf_region == NULL) {
        udapl_error_abort(GEN_EXIT_ERR,
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(sizeof(struct iovec)) * rdma_ndreg_entries);
    }

    MPIU_Memset(delayed_buf_region, 0, sizeof(struct iovec) * rdma_ndreg_entries);
    buf_reg_count = 0;

    deregister_mr_array = (dreg_entry **)
        MPIU_Malloc(sizeof(dreg_entry *) * rdma_ndreg_entries);

    if(NULL == deregister_mr_array) {
        udapl_error_abort(GEN_EXIT_ERR,
                "dreg_init: unable to malloc %d bytes",
                (int) sizeof(dreg_entry *) * rdma_ndreg_entries);
    }

    MPIU_Memset(deregister_mr_array, 0,
            sizeof(dreg_entry *) * rdma_ndreg_entries);

    n_dereg_mr = 0;

    INIT_FREE_LIST(&vma_free_list);

    INIT_FREE_LIST(&entry_free_list);
    
#endif
}

#ifndef DISABLE_PTMALLOC

int have_dereg() 
{
    return pthread_equal(th_id_of_dereg_lock, pthread_self());
}

void lock_dereg()
{
    pthread_spin_lock(&dereg_lock);
    th_id_of_dereg_lock = pthread_self();
}

void unlock_dereg()
{
    th_id_of_dereg_lock = -1;
    pthread_spin_unlock(&dereg_lock);
}

int have_dreg() {
    return pthread_equal(th_id_of_lock, pthread_self());
}


void lock_dreg()
{
    pthread_spin_lock(&dreg_lock);
    th_id_of_lock = pthread_self();
}

void unlock_dreg()
{
   th_id_of_lock = -1;
    pthread_spin_unlock(&dreg_lock);
}

/* 
** try_lock_dreg is non-blocking version if lock_dreg.
*/
int try_lock_dreg()
{
    int ret, count=0, times=0;
    /* Instead of blocking while trying to acquire this lock, we "try" it. If the
     * lock is being held by another thread, we yield the CPU after looping for a
     * while */
    while( (ret = pthread_spin_trylock(&dreg_lock)) != 0 ) { 
        count++;
        if(count > 50)  { 
            count = 0;
            sched_yield();
            /* 
            ** Other thread is already held the lock. try sched_yeld 5 times 
            ** before returns.
            */
            if (++times > 5)
                return 0;
       }
    }
    th_id_of_lock = pthread_self();
    return 1;
}

/* 
 * Check if we have to deregister some memory regions
 * which were previously marked invalid by free hook 
 */

void flush_dereg_mrs_external()
{
    unsigned long j;
    struct dreg_entry *d;


    if(n_dereg_mr == 0 || have_dreg() || have_dereg()) {
        return;
    }

    lock_dreg();
    lock_dereg();

    for(j = 0; j < n_dereg_mr; j++) {

        d = deregister_mr_array[j];
        MPIU_Assert(d->is_valid == 0);
        MPIU_Assert(d->refcount == 0);
        
            if (&(d->memhandle)) {
                if (deregister_memory(&(d->memhandle))) {
                    udapl_error_abort(UDAPL_RETURN_ERR, 
                        "deregistration failed\n");
                    //d->memhandle = NULL;
                }
            }
            if (MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
                DREG_REMOVE_FROM_UNUSED_LIST(d);
            }

            DREG_ADD_TO_FREE_LIST(d);
    }

    n_dereg_mr = 0;
    unlock_dereg();
    unlock_dreg();
}
#endif

/* will return a NULL pointer if registration fails */
dreg_entry *dreg_register(void *buf, int len)
{
    struct dreg_entry *d;
    int rc;

#ifndef DISABLE_PTMALLOC
    lock_dreg();
#endif

    d = dreg_find(buf, len);

    if (d != NULL) {
        dreg_stat_cache_hit++;
        dreg_incr_refcount(d);


    } else {
        dreg_stat_cache_miss++;
        while ((d = dreg_new_entry(buf, len)) == NULL) {
            /* either was not able to obtain a dreg_entry data strucrure
             * or was not able to register memory.  In either case,
             * attempt to evict a currently unused entry and try again.
             */
            rc = dreg_evict();
            if (rc == 0) {
                /* could not evict anything, will not be able to
                 * register this memory.  Return failure.
                 */
#ifndef DISABLE_PTMALLOC
                unlock_dreg();
#endif
                return NULL;
            }
            /* eviction successful, try again */
        }

        dreg_incr_refcount(d);

    }

#ifndef DISABLE_PTMALLOC
    unlock_dreg();
#endif

    return d;
}

void dreg_unregister(dreg_entry * d)
{
#ifndef DISABLE_PTMALLOC
    lock_dreg();
#endif

    dreg_decr_refcount(d);

#ifndef DISABLE_PTMALLOC
    unlock_dreg();
#endif
}


dreg_entry *dreg_find(void *buf, int len)
{
    unsigned long begin = ((unsigned long)buf) >> DREG_PAGEBITS;
    unsigned long end = ((unsigned long)(((char*)buf) + len - 1)) >> DREG_PAGEBITS;

    if(g_is_dreg_initialized) {
        return dreg_lookup (begin, end);
    } else {
        return NULL;
    }
}


/*
 * get a fresh entry off the free list. 
 * Ok to return NULL. Higher levels will deal with it. 
 */

dreg_entry *dreg_get()
{
    dreg_entry *d;
    DREG_GET_FROM_FREE_LIST(d);

    if (d != NULL) {
        d->refcount = 0;
        d->next_unused = NULL;
        d->prev_unused = NULL;
        d->next = NULL;
    } else {
        DEBUG_PRINT("dreg_get: no free dreg entries");
    }
    return (d);
}

void dreg_release(dreg_entry * d)
{
    /* note this correctly handles appending to empty free list */
    d->next = dreg_free_list;
    dreg_free_list = d;
}

/*
 * Decrement reference count on a dreg entry. If ref count goes to 
 * zero, don't free it, but put it on the unused list so we
 * can evict it if necessary. Put on head of unused list. 
 */
void dreg_decr_refcount(dreg_entry * d)
{
    int i;

    MPIU_Assert(d->refcount > 0);
    d->refcount--;

    if (d->refcount == 0) {
        if(MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
            DREG_ADD_TO_UNUSED_LIST(d);
        } else {

            for(i = 0; i < rdma_num_hcas; i++) {

                    d->is_valid = 0;

                    if (deregister_memory(&(d->memhandle))) {

                        udapl_error_abort(UDAPL_RETURN_ERR, 
                                "[%d] deregister fails\n", __LINE__);
                    }
            }

            dreg_remove (d);
            DREG_ADD_TO_FREE_LIST(d);
        }
    }
}

/*
 * Increment reference count on a dreg entry. If reference count
 * was zero and it was on the unused list (meaning it had been
 * previously used, and the refcount had been decremented),
 * we should take it off
 */

void dreg_incr_refcount(dreg_entry * d)
{
    MPIU_Assert(d != NULL);
    if (d->refcount == 0) {
        DREG_REMOVE_FROM_UNUSED_LIST(d);
    }
    d->refcount++;
}

/*
 * Evict a registration. This means delete it from the unused list, 
 * add it to the free list, and deregister the associated memory.
 * Return 1 if success, 0 if nothing to evict.
 *
 * If PTMALLOC is defined, its OK to call flush_dereg_mrs()
 * since dreg_evict() is called from within dreg_register()
 * which has the dreg_lock. Otherwise, it can only be called
 * from finalize, where there's only one thread executing
 * anyways.
 */
int dreg_evict()
{
    void *buf;
    dreg_entry *d;
    unsigned long bufint;
    int hca_index;

    d = dreg_unused_tail;
    if (d == NULL) {
        /* no entries left on unused list, return failure */
        return 0;
    }

    DREG_REMOVE_FROM_UNUSED_LIST(d);

    MPIU_Assert(d->refcount == 0);
    bufint = d->pagenum << DREG_PAGEBITS;
    buf = (void *) bufint;

    for (hca_index = 0; hca_index < rdma_num_hcas; hca_index ++) {          

            d->is_valid = 0;

            if (deregister_memory(&(d->memhandle))) {
                udapl_error_abort(UDAPL_RETURN_ERR,
                        "[%d] Deregister fails\n", __LINE__);
            }
    }

    dreg_remove (d);

    DREG_ADD_TO_FREE_LIST(d);

    dreg_stat_evicted++;
    return 1;
}



/*
 * dreg_new_entry is called only when we have already
 * found that the memory isn't registered. Register it 
 * and put it in the hash table 
 */
dreg_entry *dreg_new_entry(void *buf, int len)
{

    int i;
    dreg_entry *d;
    unsigned long pagenum_low, pagenum_high;
    unsigned long  npages;

    /* user_low_a is the bottom address the user wants to register;
     * user_high_a is one greater than the top address the 
     * user wants to register
     */
    unsigned long user_low_a, user_high_a;

    /* pagebase_low_a and pagebase_high_a are the addresses of 
     * the beginning of the page containing user_low_a and 
     * user_high_a respectively. 
     */
    unsigned long pagebase_low_a, pagebase_high_a;
    void *pagebase_low_p;
    unsigned long register_nbytes;
    
    int ret;

    d = dreg_get();

    if (NULL == d) {
        return d;
    }

    /* calculate base page address for registration */
    user_low_a = (unsigned long) buf;
    user_high_a = user_low_a + (unsigned long) len - 1;

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = 1 + (pagenum_high - pagenum_low);

    if (rdma_dreg_cache_limit != 0 && 
            npages >= (int) rdma_dreg_cache_limit ) {
        return NULL;
    }

    pagebase_low_p = (void *) pagebase_low_a;
    register_nbytes = npages * DREG_PAGESIZE;

    d->pagenum = pagenum_low;
    d->npages = npages;

    if (dreg_insert (d) < 0) {
        dreg_release(d);
        return NULL;
    }

    for(i = 0; i < rdma_num_hcas; i++) {
         ret = register_memory((void *)pagebase_low_p, 
                register_nbytes, i, d);

        /* if not success, return NULL to indicate that we were unable to
         * register this memory.  */
        if (ret) {
            dreg_remove (d);
            dreg_release(d);
            return NULL;
        }
    }

    d->is_valid = 1;
    
    return d;
}

#ifndef DISABLE_PTMALLOC
void find_and_free_dregs_inside(void *buf, size_t len)
{
    unsigned long pagenum_low, pagenum_high;
    unsigned long  npages, begin, end;
    unsigned long user_low_a, user_high_a;
    unsigned long pagebase_low_a, pagebase_high_a;
    struct dreg_entry *d;
    void *addr;
    int i, j=0;
    if(!g_is_dreg_initialized ||
           !MPIDI_CH3I_RDMA_Process.has_lazy_mem_unregister) {
        return;
    }

    if(have_dereg() || have_dreg()) {
        return;
    }
    
    /* 
    We are here with memlock. call non-block version of the lock_dreg 
    to avoid deadlock. Other thread might already held the dreg lock 
    and might trying to get the memlock.
    */
    if (!try_lock_dreg()) {
        
        /* dreg lock request is failed. put (buf,len) in 
        ** in the delayed list and process all entries in
        ** delayed list in next time when dreg lock is granted
        */

        delayed_buf_region[buf_reg_count].iov_base = buf;
        delayed_buf_region[buf_reg_count].iov_len = len;
        buf_reg_count++;
        return;
    }

    lock_dereg();

find_buf:
    /* calculate base page address for registration */
    user_low_a = (unsigned long) buf;
    user_high_a = user_low_a + (unsigned long) len - 1;

    pagebase_low_a = user_low_a & ~DREG_PAGEMASK;
    pagebase_high_a = user_high_a & ~DREG_PAGEMASK;

    /* info to store in hash table */
    pagenum_low = pagebase_low_a >> DREG_PAGEBITS;
    pagenum_high = pagebase_high_a >> DREG_PAGEBITS;
    npages = 1 + (pagenum_high - pagenum_low);

    /* For every page in this buffer find out whether
    * it is registered or not. This is fine, since
    * we register only at a page granularity */

    for(i = 0; i < npages; i++) {
        addr = (void *) ((uintptr_t) pagebase_low_a + i * DREG_PAGESIZE);

        begin = ((unsigned long)addr) >> DREG_PAGEBITS;

        end = ((unsigned long)(((char*)addr) +
                DREG_PAGESIZE - 1)) >> DREG_PAGEBITS;
        
        while ( (d = dreg_lookup (begin, end)) != NULL) {
            if( d->refcount !=0 || d->is_valid == 0 ) {
                /* This memory area is still being referenced
                * by other pending MPI operations, which are
                * expected to call dreg_unregister and thus
                * unpin the buffer. We cannot deregister this
                * page, since other ops are pending from here. */

                /* OR: This memory region is in the process of
                * being deregistered. Leave it alone! */

                break;
            }

            deregister_mr_array[n_dereg_mr] = d;
            d->is_valid = 0;
            /*
            *  dreg_remove can call free() while removing vma_entry
            *  It can lead to resursion here. but, still we have added 
            *  this here on the assumption that dreg_lookup will fail 
            */
            dreg_remove(d);
            n_dereg_mr++;
        }
    }
    
    if (j < buf_reg_count) {
        buf = delayed_buf_region[j].iov_base;
        len = delayed_buf_region[j].iov_len;
        j++;
        goto find_buf;
    }
    buf_reg_count = 0;

    unlock_dereg();
    unlock_dreg();
}
#endif

int register_memory(void * buf, int len, int hca_num, dreg_entry *d)
{
    DAT_RETURN ret;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN reg_size;
    DAT_VADDR reg_addr;

    region.for_va = buf;
    
    ret = dat_lmr_create (MPIDI_CH3I_RDMA_Process.nic[hca_num],
                         DAT_MEM_TYPE_VIRTUAL, region, len,
                         MPIDI_CH3I_RDMA_Process.ptag[hca_num], 
                         DAT_MEM_PRIV_ALL_FLAG,
#if DAT_VERSION_MAJOR > 1 
                         DAT_VA_TYPE_VA,
#endif /* if DAT_VERSION_MAJOR > 1 */
                         &d->memhandle.hndl, &d->memhandle.lkey,
                         &d->memhandle.rkey, &reg_size, &reg_addr);

    DEBUG_PRINT("register return mr %p, buf %p, len %d\n", mr, buf, len);
    return ret;
}

int deregister_memory(VIP_MEM_HANDLE *mr)
{
    int ret;

    ret = dat_lmr_free(mr->hndl);
    CHECK_RETURN(ret, "cannot deregister memory");
    DEBUG_PRINT("deregister mr %p, ret %d\n", mr, ret);
    return ret;
}

