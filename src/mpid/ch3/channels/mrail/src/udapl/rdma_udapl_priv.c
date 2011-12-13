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
#include "pmi.h"
#include "udapl_util.h"
#include "udapl_param.h"
#include "mpiutil.h"

#include DAT_HEADER

#ifdef MAC_OSX
#include <netinet/in.h>
#endif

#include <netdb.h>
#include <string.h>

#include "vbuf.h"

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

#define QPLEN_XDR       (2*8+2*8+2*16+6)        /* 70-bytes */

#define UNIT_QPLEN      (8)
#define IBA_PMI_ATTRLEN (16)
#define IBA_PMI_VALLEN  (4096)

extern DAT_VLEN rdma_default_mtu_size;
extern int cached_pg_size;
extern int cached_pg_rank;

DAT_EP_HANDLE temp_ep_handle;
void
MPIDI_CH3I_RDMA_util_atos (char *str, DAT_SOCK_ADDR * addr);
void
MPIDI_CH3I_RDMA_util_stoa (DAT_SOCK_ADDR * addr, char *str); 


/* gethostname does return a NULL-terminated string if
 * the hostname length is less than "HOSTNAME_LEN". Otherwise,
 * it is unspecified.
 */
void MPIDI_CH3I_RDMA_util_get_ia_addr (DAT_SOCK_ADDR * ia_addr,
                                       DAT_SOCK_ADDR * ret_addr);
static void conn_server (int n, int pg_rank, int pg_size);

static void conn_server_1sc (int n, int pg_rank, int pg_size);

static inline int
get_host_id (char *myhostname, int hostname_len)
{
    int host_id = 0;
    struct hostent *hostent;

    hostent = gethostbyname (myhostname);
    host_id = (int) ((struct in_addr *) hostent->h_addr_list[0])->s_addr;
    return host_id;
}

#ifdef USE_MPD_RING
int
rdma_iba_bootstrap_cleanup (struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int ret;
    /* Free all the temorary memory used for MPD_RING */
    ret = VAPI_deregister_mr (proc->nic, proc->boot_mem_hndl.hndl);
    MPIU_Free (proc->boot_mem);

    ret = VAPI_destroy_qp (proc->nic, proc->boot_qp_hndl[0]);
    CHECK_RETURN (ret, "could not destroy lhs QP");

    ret = VAPI_destroy_qp (proc->nic, proc->boot_qp_hndl[1]);
    CHECK_RETURN (ret, "could not destroy rhs QP");

    ret = VAPI_destroy_cq (proc->nic, proc->boot_cq_hndl);
    CHECK_RETURN (ret, "could not destroy CQ");
    return ret;
}


/* A ring-based barrier: process 0 initiates two tokens 
 * going clockwise and counter-clockwise, respectively.
 * The return of these two tokens completes the barrier.
 */
int
bootstrap_barrier (struct MPIDI_CH3I_RDMA_Process_t *proc,
                   int pg_rank, int pg_size)
{
    /* work entries related variables */
    VAPI_sr_desc_t sr;
    VAPI_sg_lst_entry_t sg_entry_s;

    /* completion related variables */
    VAPI_cq_hndl_t cq_hndl;
    VAPI_wc_desc_t rc;

    /* Memory related variables */
    VIP_MEM_HANDLE *mem_handle;
    int unit_length;
    char *barrier_buff;
    char *send_addr;
    char *temp_buff;
    int barrier_recvd = 0;
    int ret;

    /* Find out the barrier address */
    barrier_buff = proc->boot_mem;
    unit_length = UNIT_QPLEN * (pg_size);
    temp_buff = (char *) barrier_buff + pg_rank * unit_length;
    mem_handle = &proc->boot_mem_hndl;

    sr.opcode = VAPI_SEND;
    sr.comp_type = VAPI_SIGNALED;
    sr.sg_lst_len = 1;
    sr.sg_lst_p = &sg_entry_s;
    sr.remote_qkey = 0;
    sg_entry_s.lkey = mem_handle->lkey;
    cq_hndl = proc->boot_cq_hndl;

    if (pg_rank == 0)
      {
          /* send_lhs(); send_rhs(); recv_lhs(); recv_rhs(); */
          sr.id = 0;
          sg_entry_s.len = UNIT_QPLEN;
          send_addr = temp_buff + unit_length;
          snprintf (send_addr, UNIT_QPLEN, "000");
          sg_entry_s.addr = (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
          ret = VAPI_post_sr (proc->nic, proc->boot_qp_hndl[1], &sr);
          CHECK_RETURN (ret, "Error posting send!\n");

          sr.id = 1;
          sg_entry_s.len = 2 * UNIT_QPLEN;
          send_addr = temp_buff + unit_length + UNIT_QPLEN;
          snprintf (send_addr, 2 * UNIT_QPLEN, "111");
          sg_entry_s.addr = (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
          ret = VAPI_post_sr (proc->nic, proc->boot_qp_hndl[0], &sr);
          CHECK_RETURN (ret, "Error posting second send!\n");

          while (barrier_recvd < 2)
            {
                ret = VAPI_CQ_EMPTY;
                rc.opcode = VAPI_CQE_INVAL_OPCODE;

                /* Polling CQ */
                ret = VAPI_poll_cq (proc->nic, cq_hndl, &rc);
                if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA)
                  {
                      DEBUG_PRINT ("Receive forward %s reverse %s id %d \n",
                                   (char *) temp_buff,
                                   (char *) (temp_buff + UNIT_QPLEN),
                                   (int) rc.id);

                      barrier_recvd++;
                  }
                else if (ret == VAPI_EINVAL_HCA_HNDL
                         || ret == VAPI_EINVAL_CQ_HNDL)
                  {
                      fprintf (stderr,
                               "[%d](%s:%d)invalid send CQ or DEV hndl\n",
                               pg_rank, __FILE__, __LINE__);
                      fflush (stderr);
                  }
            }                   /* end of while loop */
      }
    else
      {
          /* recv_lhs(); send_rhs(); recv_rhs(); send_lhs(); */
          while (barrier_recvd < 2)
            {
                ret = VAPI_CQ_EMPTY;
                rc.opcode = VAPI_CQE_INVAL_OPCODE;

                /* Polling CQ */
                ret = VAPI_poll_cq (proc->nic, cq_hndl, &rc);
                if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA)
                  {
                      DEBUG_PRINT ("Received forward %s reverse %s id %d\n",
                                   (char *) temp_buff,
                                   (char *) (temp_buff + UNIT_QPLEN),
                                   (int) rc.id);

                      barrier_recvd++;

                      /* find out which one is received, and forward accordingly */
                      sr.id = rc.id + 1 - pg_size;
                      sg_entry_s.len = (sr.id + 1) * UNIT_QPLEN;
                      send_addr = temp_buff + sr.id * UNIT_QPLEN;
                      sg_entry_s.addr =
                          (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
                      ret = VAPI_post_sr (proc->nic,
                                          proc->boot_qp_hndl[1 - sr.id], &sr);
                      CHECK_RETURN (ret, "Error posting second send!\n");
                  }
                else if (ret == VAPI_EINVAL_HCA_HNDL
                         || ret == VAPI_EINVAL_CQ_HNDL)
                  {
                      fprintf (stderr, "[%d](%s:%d)invalid CQ or DEV hndl\n",
                               pg_rank, __FILE__, __LINE__);
                      fflush (stderr);
                  }
            }
      }
    return 0;
}


/* Set up a ring of qp's, here a separate bootstrap channel is used.*/
static void
ib_vapi_enable_ring (int lhs, int rhs, char *ring_addr)
{

    VAPI_qp_attr_t qp_attr;
    VAPI_qp_cap_t qp_cap;
    VAPI_qp_attr_mask_t qp_attr_mask;

    int i;
    VAPI_ret_t ret;

    /* Get lhs and rhs hca_lid and qp # */
    char temp_str[UNIT_QPLEN + 1];
    char *temp_ptr;

    MPIDI_CH3I_RDMA_Process_t *proc;

    proc = &MPIDI_CH3I_RDMA_Process;
    temp_str[UNIT_QPLEN] = '\0';

    for (i = 0; i < 2; i++)
      {

          /* hca_lid + lhs_qp + rhs_qp */
          temp_ptr = ring_addr + i * (UNIT_QPLEN * 3);
          strncpy (temp_str, temp_ptr, UNIT_QPLEN);
          proc->boot_tb[i][0] = strtol (temp_str, NULL, 16);
          DEBUG_PRINT ("Got hca_lid %s num %08X\n",
                       temp_str, proc->boot_tb[i][0]);

          /* qp # to me usually on rhs unless I am at the beginning */
          temp_ptr += UNIT_QPLEN * (2 - i);
          strncpy (temp_str, temp_ptr, UNIT_QPLEN);
          proc->boot_tb[i][1] = strtol (temp_str, NULL, 16);
          DEBUG_PRINT ("Got queue_pair %s num %08X\n",
                       temp_str, proc->boot_tb[i][1]);
      }

    /* Modifying  QP to INIT */
    QP_ATTR_MASK_CLR_ALL (qp_attr_mask);
    qp_attr.qp_state = VAPI_INIT;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.port = rdma_default_port;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_PORT);
    qp_attr.remote_atomic_flags = VAPI_EN_REM_WRITE | VAPI_EN_REM_READ;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_REMOTE_ATOMIC_FLAGS);

    {
        ret = VAPI_modify_qp (proc->nic, proc->boot_qp_hndl[0], &qp_attr,
                              &qp_attr_mask, &qp_cap);
        CHECK_RETURN (ret, "Could not modify lhs qp to INIT!\n");
        ret = VAPI_modify_qp (proc->nic, proc->boot_qp_hndl[1], &qp_attr,
                              &qp_attr_mask, &qp_cap);
        CHECK_RETURN (ret, "Could not modify rhs qp to INIT!\n");
    }

    /**********************  INIT --> RTR  ************************/
    QP_ATTR_MASK_CLR_ALL (qp_attr_mask);
    qp_attr.qp_state = VAPI_RTR;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.qp_ous_rd_atom = rdma_default_qp_ous_rd_atom;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_QP_OUS_RD_ATOM);
    qp_attr.path_mtu = rdma_default_mtu;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_PATH_MTU);
    qp_attr.rq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_RQ_PSN);
    qp_attr.pkey_ix = rdma_default_pkey_ix;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_PKEY_IX);
    qp_attr.min_rnr_timer = rdma_default_min_rnr_timer;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_MIN_RNR_TIMER);

    qp_attr.av.sl = rdma_default_service_level;
    qp_attr.av.grh_flag = FALSE;
    qp_attr.av.static_rate = rdma_default_static_rate;
    qp_attr.av.src_path_bits = rdma_default_src_path_bits;

    /* lhs */
    for (i = 0; i < 2; i++)
      {
          qp_attr.dest_qp_num = proc->boot_tb[i][1];
          QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_DEST_QP_NUM);
          qp_attr.av.dlid = proc->boot_tb[i][0];
          QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_AV);

          DEBUG_PRINT ("Remote LID[%d] QP=%d\n",
                       qp_attr.av.dlid, qp_attr.dest_qp_num);
          ret =
              VAPI_modify_qp (proc->nic, proc->boot_qp_hndl[i], &qp_attr,
                              &qp_attr_mask, &qp_cap);
          CHECK_RETURN (ret, "Could not modify qp to RTR!\n");
          DEBUG_PRINT ("Modified to RTR..Qp[%d]\n", i);
      }

    /************** RTS *******************/
    QP_ATTR_MASK_CLR_ALL (qp_attr_mask);
    qp_attr.qp_state = VAPI_RTS;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_QP_STATE);
    qp_attr.sq_psn = rdma_default_psn;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_SQ_PSN);
    qp_attr.timeout = rdma_default_time_out;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_TIMEOUT);
    qp_attr.retry_count = rdma_default_retry_count;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_RETRY_COUNT);
    qp_attr.rnr_retry = rdma_default_rnr_retry;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_RNR_RETRY);
    qp_attr.ous_dst_rd_atom = rdma_default_max_rdma_dst_ops;
    QP_ATTR_MASK_SET (qp_attr_mask, QP_ATTR_OUS_DST_RD_ATOM);

    {
        ret = VAPI_modify_qp (proc->nic, proc->boot_qp_hndl[0],
                              &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN (ret, "Could not modify lhs qp to RTS!\n");
        ret = VAPI_modify_qp (proc->nic, proc->boot_qp_hndl[1],
                              &qp_attr, &qp_attr_mask, &qp_cap);
        CHECK_RETURN (ret, "Could not modify rhs qp to RTS!\n");
    }
}

/* Using a ring of queue pairs to exchange all the queue_pairs,
 * If mpd is used,  only info about lhs and rsh are provided. */
static void
ib_vapi_bootstrap_ring (int lhs, int rhs,
                        char *ring_addr,
                        int pg_rank, int pg_size,
                        char *local_addr,
                        char *alladdrs, VIP_MEM_HANDLE * mem_handle)
{
    int i, j, ret;
    int recv_index;

    /* Now enable the queue pairs and post descriptors */
    ib_vapi_enable_ring (lhs, rhs, ring_addr);

    /* Register alladdrs and post receive descriptors */
    {
        /* work entries related variables */
        VAPI_rr_desc_t rr;
        VAPI_sg_lst_entry_t sg_entry_r;
        VAPI_sr_desc_t sr;
        VAPI_sg_lst_entry_t sg_entry_s;

        /* completion related variables */
        VAPI_cq_hndl_t cq_hndl;
        VAPI_wc_desc_t rc, sc;

        /* Memory related variables */
        VAPI_mrw_t mr_in, mr_out;

        int unit_length;
        char *dest_loc;
        char *recv_addr;
        char *send_addr;

        struct MPIDI_CH3I_RDMA_Process_t *proc;
        proc = &MPIDI_CH3I_RDMA_Process;

#if 1
        /* Same as local_addr_len; memory is already regitered */
        unit_length = pg_size * QPLEN_XDR;
#else
        /* Same as local_addr_len */
        unit_length = (pg_size + 1) * QPLEN_XDR;
        mr_in.acl = VAPI_EN_LOCAL_WRITE | VAPI_EN_REMOTE_WRITE;
        mr_in.l_key = 0;
        mr_in.r_key = 0;
        mr_in.pd_hndl = proc->ptag;
        mr_in.start = (VAPI_virt_addr_t) (virt_addr_t) alladdrs;
        mr_in.size = pg_size * unit_length;
        mr_in.type = VAPI_MR;

        ret = VAPI_register_mr (proc->nic, &mr_in,
                                &mem_handle->hndl, &mr_out);
        mem_handle->lkey = mr_out.l_key;
        mem_handle->rkey = mr_out.r_key;
#endif
        /* Copy local_addr to the correct slot in alladdrs
         * and  post receive descriptors for all_addr */
        dest_loc = alladdrs + pg_rank * unit_length;
        strncpy (dest_loc, local_addr, unit_length);

        recv_index = 0;

        /* Post receive for all_addr */
        for (j = 0; j < pg_size + 1; j++)
          {
              /* The last two entries are used for a barrier,
               * they overlap the local address */
              if ((j + 1) < pg_size)
                {
                    recv_addr = alladdrs + unit_length *
                        ((pg_rank + pg_size - j - 1) % pg_size);
                }
              else if (j < pg_size)
                {
                    recv_addr = alladdrs + unit_length * pg_rank;
                }
              else
                {
                    recv_addr = alladdrs + unit_length * pg_rank + QPLEN_XDR;
                }

              /* Fillup a recv descriptor */
              rr.comp_type = VAPI_SIGNALED;
              rr.opcode = VAPI_RECEIVE;
              rr.id = j;
              rr.sg_lst_len = 1;
              rr.sg_lst_p = &(sg_entry_r);
              sg_entry_r.lkey = mem_handle->lkey;
              sg_entry_r.addr = (VAPI_virt_addr_t) (virt_addr_t) recv_addr;

              if ((j + 1) >= pg_size)
                  sg_entry_r.len = (j + 2 - pg_size) * QPLEN_XDR;
              else
                  sg_entry_r.len = unit_length;

              /* Post the recv descriptor */
              if (j < pg_size)
                {
                    ret =
                        VAPI_post_rr (proc->nic, proc->boot_qp_hndl[0], &rr);
                }
              else
                {
                    ret =
                        VAPI_post_rr (proc->nic, proc->boot_qp_hndl[1], &rr);
                }
              CHECK_RETURN (ret, "Error posting recv!");
          }

        /* synchronize all the processes */
        ret = PMI_Barrier ();
        CHECK_UNEXP ((ret != 0), "PMI_KVS_Barrier error \n");

        recv_index = 0;

        /* transfer all the addresses */
        for (j = 0; j < pg_size - 1; j++)
          {

              send_addr = alladdrs +
                  unit_length * ((pg_rank + pg_size - j) % pg_size);

              /* send to rhs */
              sr.opcode = VAPI_SEND;
              sr.comp_type = VAPI_SIGNALED;
              sr.id = j;
              sr.sg_lst_len = 1;
              sr.sg_lst_p = &sg_entry_s;

              sr.remote_qkey = 0;
              sg_entry_s.addr = (VAPI_virt_addr_t) (MT_virt_addr_t) send_addr;
              sg_entry_s.len = unit_length;
              sg_entry_s.lkey = mem_handle->lkey;

              ret = VAPI_post_sr (proc->nic, proc->boot_qp_hndl[1], &sr);
              CHECK_RETURN (ret, "Error posting send!");

              /* recv from lhs */
              cq_hndl = proc->boot_cq_hndl;

              ret = VAPI_CQ_EMPTY;
              rc.opcode = VAPI_CQE_INVAL_OPCODE;
              rc.id = -1;

              do
                {
                    ret = VAPI_poll_cq (proc->nic, cq_hndl, &rc);
                    if (ret == VAPI_OK && rc.opcode == VAPI_CQE_RQ_SEND_DATA)
                      {
                          /* Detect any earlier message */
                          if (rc.id != recv_index)
                            {
                                fprintf (stderr,
                                         "[%d](%s:%d)unexpected message %p vs %d\n",
                                         pg_rank, __FILE__, __LINE__,
                                         rc.id, recv_index);
                                exit (1);
                            }
                          else
                            {
                                DEBUG_PRINT ("expected message %d\n", rc.id);
                                recv_index = rc.id + 1;
                            }
                      }
                    else if (ret == VAPI_EINVAL_HCA_HNDL
                             || ret == VAPI_EINVAL_CQ_HNDL)
                      {
                          fprintf (stderr,
                                   "[%d](%s:%d)invalid CQ or DEV hndl\n",
                                   pg_rank, __FILE__, __LINE__);
                          fflush (stderr);
                      }
                }
              while (rc.opcode != VAPI_CQE_RQ_SEND_DATA);       /* do-while loop */
          }                     /* end of for loop */
    }                           /* end of alltoall exchange */
}
#endif

/* Exchange address info with other processes in the job.
 * MPD provides the ability for processes within the job to
 * publish information which can then be querried by other
 * processes.  It also provides a simple barrier sync.
 */
static int
rdma_pmi_exchange_addresses (int pg_rank, int pg_size,
                             void *localaddr, int addrlen, void *alladdrs)
{
    int ret, i, j, lhs, rhs, len_local, len_remote, key_max_sz, val_max_sz;
    char attr_buff[IBA_PMI_ATTRLEN];
    char val_buff[IBA_PMI_VALLEN];
    char *temp_localaddr = (char *) localaddr;
    char *temp_alladdrs = (char *) alladdrs;
    char *key, *val;
    char    *kvsname = NULL;

    /* Allocate space for pmi keys and values */
    ret = PMI_KVS_Get_key_length_max (&key_max_sz);
    MPIU_Assert (ret == PMI_SUCCESS);
    key_max_sz++;
    key = MPIU_Malloc (key_max_sz);
    CHECK_UNEXP ((key == NULL), "Could not get key \n");

    ret = PMI_KVS_Get_value_length_max (&val_max_sz);
    MPIU_Assert (ret == PMI_SUCCESS);
    val_max_sz++;
    val = MPIU_Malloc (val_max_sz);
    CHECK_UNEXP ((val == NULL), "Could not get val \n");
    len_local = strlen (temp_localaddr);

    /* TODO: Double check the value of value */
    MPIU_Assert (len_local <= val_max_sz);

    /* Be sure to use different keys for different processes */
    MPIU_Memset (attr_buff, 0, IBA_PMI_ATTRLEN * sizeof (char));
    snprintf (attr_buff, IBA_PMI_ATTRLEN, "MVAPICH2_%04d", pg_rank);

    /* put the kvs into PMI */
    MPIU_Strncpy (key, attr_buff, key_max_sz);
    MPIU_Strncpy (val, temp_localaddr, val_max_sz);
    MPIDI_PG_GetConnKVSname( &kvsname );
    ret = PMI_KVS_Put(kvsname, key, val);

    CHECK_UNEXP ((ret != 0), "PMI_KVS_Put error \n");

    ret = PMI_KVS_Commit(kvsname);
    CHECK_UNEXP ((ret != 0), "PMI_KVS_Commit error \n");

    /* Wait until all processes done the same */
    ret = PMI_Barrier ();
    CHECK_UNEXP ((ret != 0), "PMI_Barrier error \n");

#ifdef USE_MPD_RING
    lhs = (pg_rank + pg_size - 1) % pg_size;
    rhs = (pg_rank + 1) % pg_size;

    for (i = 0; i < 2; i++)
      {
          /* get lhs and rhs processes' data */
          j = (i == 0) ? lhs : rhs;
#else
    for (j = 0; j < pg_size; j++)
      {
          /* get lhs and rhs processes' data */
#endif

          /* Use the key to extract the value */
          MPIU_Memset (attr_buff, 0, IBA_PMI_ATTRLEN * sizeof (char));
          MPIU_Memset (val_buff, 0, IBA_PMI_VALLEN * sizeof (char));
          snprintf (attr_buff, IBA_PMI_ATTRLEN, "MVAPICH2_%04d", j);
          MPIU_Strncpy (key, attr_buff, key_max_sz);

	  ret = PMI_KVS_Get(kvsname, key, val, val_max_sz);
          CHECK_UNEXP ((ret != 0), "PMI_KVS_Get error \n");
          MPIU_Strncpy (val_buff, val, val_max_sz);

          /* Simple sanity check before stashing it to the alladdrs */
          len_remote = strlen (val_buff);

          MPIU_Assert (len_remote >= len_local);
          strncpy (temp_alladdrs, val_buff, len_local);
          temp_alladdrs += len_local;
      }

    /* Free the key-val pair */
    MPIU_Free (key);
    MPIU_Free (val);

    /* this barrier is to prevent some process from overwriting values that
       has not been get yet */
    ret = PMI_Barrier ();
    CHECK_UNEXP ((ret != 0), "PMI_Barrier error \n");
    return (1);
}

int
rdma_iba_hca_init (struct MPIDI_CH3I_RDMA_Process_t *proc,
                   MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    DAT_EP_ATTR ep_attr;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;
    DAT_EVENT event;
    DAT_IA_ATTR ia_attr;
    DAT_EP_PARAM param;

    unsigned int act_num_cqe;
    int i;
    DAT_RETURN ret = DAT_SUCCESS;

    /* XXX: The device name now defaults to be "InfiniHost0" */
    /* XXX: Now the default number of hca is 1 */

    for (i = 0; i < proc->num_hcas; i++)
      {
          ret =
              dat_ia_open (dapl_provider, DAPL_DEFAULT_ASYNC_EVD_SIZE,
                           &async_evd_handle, &proc->nic[i]);
          CHECK_RETURN (ret, "Cannot open IA");

          ret = dat_ia_query (proc->nic[i], &async_evd_handle, DAT_IA_ALL, &ia_attr, 0, /* provider attr mask */
                              NULL);    /* provider attr */
          CHECK_RETURN (ret, "Cannot query IA");

          MPIDI_CH3I_RDMA_util_get_ia_addr (ia_attr.ia_address_ptr,
                                            &(rdma_iba_addr_table.
                                              ia_addr[pg_rank][i]));

          /* allocate completion queues, protection handle before qp */

          ret = dat_pz_create (proc->nic[i], &(proc->ptag[i]));
          CHECK_RETURN (ret, "Cannot create PZ");

          ret = dat_evd_create (proc->nic[i],
                                MIN (RDMA_DEFAULT_MAX_CQ_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_DTO_FLAG, &(proc->cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create EVD");

          ret = dat_evd_create (proc->nic[i],
                                MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_CR_FLAG, &(proc->creq_cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create creq EVD");

          ret = dat_evd_create (proc->nic[i],
                                MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_CONNECTION_FLAG,
                                &(proc->conn_cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create conn EVD");

          rdma_iba_addr_table.service_id[pg_rank][i] = 1024 + getpid () + i;
          ret = dat_psp_create (proc->nic[i],
                                rdma_iba_addr_table.service_id[pg_rank][i],
                                proc->creq_cq_hndl[i],
                                DAT_PSP_CONSUMER_FLAG, &(proc->psp_hndl[i]));
          while(ret != DAT_SUCCESS){
              rdma_iba_addr_table.service_id[pg_rank][i] += 1024;
              ret = dat_psp_create (proc->nic[i],
                                    rdma_iba_addr_table.service_id[pg_rank][i],
                                    proc->creq_cq_hndl[i],
                                    DAT_PSP_CONSUMER_FLAG, &(proc->psp_hndl[i]));
          }
      }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    /* This part is for built up QPs and CQ for one-sided communication */
    ret = dat_evd_create (proc->nic[0],
                          MIN (RDMA_DEFAULT_MAX_CQ_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_DTO_FLAG, &proc->cq_hndl_1sc);
    CHECK_RETURN (ret, "cannot create cq evd 1sc");

    ret = dat_evd_create (proc->nic[0],
                          MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_CR_FLAG, &(proc->creq_cq_hndl_1sc));
    CHECK_RETURN (ret, "cannot create creq EVD 1sc");

    ret = dat_evd_create (proc->nic[0],
                          MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_CONNECTION_FLAG, &(proc->conn_cq_hndl_1sc));
    CHECK_RETURN (ret, "cannot create conn EVD 1sc");

    rdma_iba_addr_table.service_id_1sc[pg_rank][0] =
        getpid () + pg_size + 1124;
    ret =
        dat_psp_create (proc->nic[0],
                        rdma_iba_addr_table.service_id_1sc[pg_rank][0],
                        proc->creq_cq_hndl_1sc, DAT_PSP_CONSUMER_FLAG,
                        &(proc->psp_hndl_1sc));
    while(ret != DAT_SUCCESS){
        rdma_iba_addr_table.service_id_1sc[pg_rank][0] += 1124;
        ret = dat_psp_create (proc->nic[0],
                              rdma_iba_addr_table.service_id_1sc[pg_rank][0],
                              proc->creq_cq_hndl_1sc, DAT_PSP_CONSUMER_FLAG,
                              &(proc->psp_hndl_1sc));
    }
  }

    /* Queue Pair creation, Basic */
    if (strcmp (dapl_provider, "ccil") == 0)
      {
          ret = dat_ep_create (proc->nic[0],
                               proc->ptag[0],
                               proc->cq_hndl[0],
                               proc->cq_hndl[0],
                               proc->conn_cq_hndl[0], NULL, &temp_ep_handle);
          ret =
              dat_ep_query (temp_ep_handle, DAT_EP_FIELD_EP_ATTR_ALL, &param);

          ep_attr = param.ep_attr;
          ep_attr.max_rdma_read_iov = 1;
          ep_attr.max_rdma_write_iov = 4;
          ep_attr.max_recv_dtos = rdma_default_max_recv_wqe;
          ep_attr.max_request_dtos = rdma_default_max_send_wqe;
      }
    else if (strcmp (dapl_provider, "ib0") == 0)
      {
          ep_attr.service_type = DAT_SERVICE_TYPE_RC;
          ep_attr.max_mtu_size = DAPL_GEN2_MAX_MSG_SIZE;
          ep_attr.max_message_size = DAPL_GEN2_MAX_MSG_SIZE;
          ep_attr.max_rdma_size = 0;
          ep_attr.qos = DAT_QOS_BEST_EFFORT;
          ep_attr.recv_completion_flags = DAT_COMPLETION_DEFAULT_FLAG;
          ep_attr.request_completion_flags = DAT_COMPLETION_UNSIGNALLED_FLAG;
          ep_attr.max_recv_dtos = rdma_default_max_recv_wqe;
          ep_attr.max_request_dtos = rdma_default_max_send_wqe;
          ep_attr.max_recv_iov = 4;
          ep_attr.max_request_iov = 4;
          ep_attr.max_rdma_read_in = DAPL_DEFAULT_MAX_RDMA_IN;
          ep_attr.max_rdma_read_out = DAPL_DEFAULT_MAX_RDMA_OUT;

          ep_attr.ep_transport_specific_count = 0;
          ep_attr.ep_transport_specific = NULL;
          ep_attr.ep_provider_specific_count = 0;
          ep_attr.ep_provider_specific = NULL;

          ep_attr.max_rdma_read_iov = 0;
          ep_attr.max_rdma_write_iov = 0;
          ep_attr.srq_soft_hw = 0;
      }
    else
      {
          ep_attr.service_type = DAT_SERVICE_TYPE_RC;
          ep_attr.max_mtu_size = rdma_default_mtu_size;
          ep_attr.max_message_size = ia_attr.max_message_size;
          ep_attr.max_rdma_size = ia_attr.max_rdma_size;
          ep_attr.qos = DAT_QOS_BEST_EFFORT;
          ep_attr.recv_completion_flags = DAT_COMPLETION_DEFAULT_FLAG;
          ep_attr.request_completion_flags = DAT_COMPLETION_UNSIGNALLED_FLAG;
          ep_attr.max_recv_dtos =
              MIN (rdma_default_max_recv_wqe, ia_attr.max_dto_per_ep);
          ep_attr.max_request_dtos =
              MIN (rdma_default_max_send_wqe, ia_attr.max_dto_per_ep);
          ep_attr.max_recv_iov =
              MIN (rdma_default_max_sg_list,
                   ia_attr.max_iov_segments_per_dto);
          ep_attr.max_request_iov =
              MIN (rdma_default_max_sg_list,
                   ia_attr.max_iov_segments_per_dto);
          ep_attr.max_rdma_read_in = DAPL_DEFAULT_MAX_RDMA_IN;
          ep_attr.max_rdma_read_out = DAPL_DEFAULT_MAX_RDMA_OUT;

          ep_attr.ep_transport_specific_count = 0;
          ep_attr.ep_transport_specific = NULL;
          ep_attr.ep_provider_specific_count = 0;
          ep_attr.ep_provider_specific = NULL;

          ep_attr.max_rdma_read_iov = 0;
          ep_attr.max_rdma_write_iov = 0;
          ep_attr.srq_soft_hw = 0;
      }

    for (i = 0; i < pg_size; i++)
      {
          int hca_index = 0;
          int j = 0, qp_index = 0;
          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (cached_pg, i, &vc);
          /* Currently we only support one rail */
          MPIU_Assert (vc->mrail.subrail_per_hca * proc->num_hcas ==
                  vc->mrail.num_total_subrails);
          vc->mrail.qp_hndl =
              (DAT_EP_HANDLE *) MPIU_Malloc (sizeof (DAT_EP_HANDLE) *
                                        vc->mrail.num_total_subrails);
          /* per connection per qp information is saved to 
             rdma_iba_addr_table.qp_num_rdma[][] */
          for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
               qp_index++)
            {

                ret = dat_ep_create (proc->nic[hca_index],
                                     proc->ptag[hca_index],
                                     proc->cq_hndl[hca_index],
                                     proc->cq_hndl[hca_index],
                                     proc->conn_cq_hndl[hca_index],
                                     &ep_attr,
                                     &(vc->mrail.qp_hndl[qp_index]));
                CHECK_RETURN (ret, "Could not create EP");

                j++;
                if (j == vc->mrail.subrail_per_hca)
                  {
                      j = 0;
                      hca_index++;
                  }
            }
      }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    for (i = 0; i < pg_size; i++)
      {

          if (i == pg_rank)
              continue;

          MPIDI_PG_Get_vc (cached_pg, i, &vc);
          ret = dat_ep_create (proc->nic[0],
                               proc->ptag[0],
                               proc->cq_hndl_1sc,
                               proc->cq_hndl_1sc,
                               proc->conn_cq_hndl_1sc,
                               &ep_attr, &(vc->mrail.qp_hndl_1sc));
          CHECK_RETURN (ret, "Could not create EP for _1SC_");

      }
  }

    return DAT_SUCCESS;
}

/* For on demand connection mode */
int
rdma_iba_hca_init_noep (struct MPIDI_CH3I_RDMA_Process_t *proc,
                   MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    DAT_EP_ATTR ep_attr;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;
    DAT_EVENT event;
    DAT_IA_ATTR ia_attr;
    DAT_EP_PARAM param;

    unsigned int act_num_cqe;
    int i;
    DAT_RETURN ret = DAT_SUCCESS;

    /* XXX: The device name now defaults to be "InfiniHost0" */
    /* XXX: Now the default number of hca is 1 */

    for (i = 0; i < proc->num_hcas; i++)
      {
          ret =
              dat_ia_open (dapl_provider, DAPL_DEFAULT_ASYNC_EVD_SIZE,
                           &async_evd_handle, &proc->nic[i]);
          CHECK_RETURN (ret, "Cannot open IA");

          ret = dat_ia_query (proc->nic[i], &async_evd_handle, DAT_IA_ALL, &ia_attr, 0, /* provider attr mask */
                              NULL);    /* provider attr */
          CHECK_RETURN (ret, "Cannot query IA");

          MPIDI_CH3I_RDMA_util_get_ia_addr (ia_attr.ia_address_ptr,
                                            &(rdma_iba_addr_table.
                                              ia_addr[pg_rank][i]));

          /* allocate completion queues, protection handle before qp */
          ret = dat_pz_create (proc->nic[i], &(proc->ptag[i]));
          CHECK_RETURN (ret, "Cannot create PZ");

          ret = dat_evd_create (proc->nic[i],
                                MIN (RDMA_DEFAULT_MAX_CQ_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_DTO_FLAG, &(proc->cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create EVD");

          ret = dat_evd_create (proc->nic[i],
                                MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_CR_FLAG, &(proc->creq_cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create creq EVD");

          ret = dat_evd_create (proc->nic[i],
                                MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                                     ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                                DAT_EVD_CONNECTION_FLAG,
                                &(proc->conn_cq_hndl[i]));
          CHECK_RETURN (ret, "cannot create conn EVD");

          rdma_iba_addr_table.service_id[pg_rank][i] = 1024 + getpid () + i;
          MPIDI_CH3I_RDMA_Process.service_id[i] =
              rdma_iba_addr_table.service_id[pg_rank][i];

          ret = dat_psp_create (proc->nic[i],
                                rdma_iba_addr_table.service_id[pg_rank][i],
                                proc->creq_cq_hndl[i],
                                DAT_PSP_CONSUMER_FLAG, &(proc->psp_hndl[i]));
          while(ret != DAT_SUCCESS){
              rdma_iba_addr_table.service_id[pg_rank][i] += 1024;
              ret = dat_psp_create (proc->nic[i],
                                    rdma_iba_addr_table.service_id[pg_rank][i],
                                    proc->creq_cq_hndl[i],
                                    DAT_PSP_CONSUMER_FLAG, &(proc->psp_hndl[i]));
          }
      }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    /* This part is for built up QPs and CQ for one-sided communication */
    ret = dat_evd_create (proc->nic[0],
                          MIN (RDMA_DEFAULT_MAX_CQ_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_DTO_FLAG, &proc->cq_hndl_1sc);
    CHECK_RETURN (ret, "cannot create cq evd 1sc");

    ret = dat_evd_create (proc->nic[0],
                          MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_CR_FLAG, &(proc->creq_cq_hndl_1sc));
    CHECK_RETURN (ret, "cannot create creq EVD 1sc");

    ret = dat_evd_create (proc->nic[0],
                          MIN (DAPL_DEFAULT_MIN_EVD_SIZE,
                               ia_attr.max_evd_qlen), DAT_HANDLE_NULL,
                          DAT_EVD_CONNECTION_FLAG, &(proc->conn_cq_hndl_1sc));
    CHECK_RETURN (ret, "cannot create conn EVD 1sc");

    rdma_iba_addr_table.service_id_1sc[pg_rank][0] =
        getpid () + pg_size + 1124;
    ret =
        dat_psp_create (proc->nic[0],
                        rdma_iba_addr_table.service_id_1sc[pg_rank][0],
                        proc->creq_cq_hndl_1sc, DAT_PSP_CONSUMER_FLAG,
                        &(proc->psp_hndl_1sc));
    while(ret != DAT_SUCCESS){
        rdma_iba_addr_table.service_id_1sc[pg_rank][0] += 1124;
        ret = dat_psp_create (proc->nic[0],
                              rdma_iba_addr_table.service_id_1sc[pg_rank][0],
                              proc->creq_cq_hndl_1sc, DAT_PSP_CONSUMER_FLAG,
                              &(proc->psp_hndl_1sc));
    }
  }

    for (i = 0; i < pg_size; i++) {
        if (i == pg_rank)
            continue;

        MPIDI_PG_Get_vc (cached_pg, i, &vc);
        vc->mrail.qp_hndl =
            (DAT_EP_HANDLE *) MPIU_Malloc (sizeof (DAT_EP_HANDLE) *
                                      vc->mrail.num_total_subrails);
    }

    return DAT_SUCCESS;
}

void cm_ep_create(MPIDI_VC_t *vc)
{
    DAT_EP_ATTR ep_attr;
    DAT_IA_ATTR ia_attr;
    DAT_EP_PARAM param;
    DAT_EVD_HANDLE async_evd_handle = DAT_HANDLE_NULL;

    unsigned int act_num_cqe;
    int i;
    DAT_RETURN ret = DAT_SUCCESS;
    MPIDI_CH3I_RDMA_Process_t *proc = &MPIDI_CH3I_RDMA_Process;

    ret = dat_ia_query (proc->nic[0], &async_evd_handle, DAT_IA_ALL, 
                        &ia_attr, 
                        0, /* provider attr mask */
                        NULL);    /* provider attr */
    CHECK_RETURN (ret, "Cannot query IA");

    /* Queue Pair creation, Basic */
    if (strcmp (dapl_provider, "ccil") == 0)
      {
          ret = dat_ep_create (proc->nic[0],
                               proc->ptag[0],
                               proc->cq_hndl[0],
                               proc->cq_hndl[0],
                               proc->conn_cq_hndl[0], 
                               NULL, &temp_ep_handle);
          ret =
              dat_ep_query (temp_ep_handle, DAT_EP_FIELD_EP_ATTR_ALL, &param);

          ep_attr = param.ep_attr;
          ep_attr.max_rdma_read_iov = 1;
          ep_attr.max_rdma_write_iov = 4;
          ep_attr.max_recv_dtos = rdma_default_max_recv_wqe;
          ep_attr.max_request_dtos = rdma_default_max_send_wqe;
      }
    else if (strcmp (dapl_provider, "ib0") == 0)
      {
          ep_attr.service_type = DAT_SERVICE_TYPE_RC;
          ep_attr.max_mtu_size = DAPL_GEN2_MAX_MSG_SIZE;
          ep_attr.max_message_size = DAPL_GEN2_MAX_MSG_SIZE;
          ep_attr.max_rdma_size = 0;
          ep_attr.qos = DAT_QOS_BEST_EFFORT;
          ep_attr.recv_completion_flags = DAT_COMPLETION_DEFAULT_FLAG;
          ep_attr.request_completion_flags = DAT_COMPLETION_UNSIGNALLED_FLAG;
          ep_attr.max_recv_dtos = rdma_default_max_recv_wqe;
          ep_attr.max_request_dtos = rdma_default_max_send_wqe;
          ep_attr.max_recv_iov = 4;
          ep_attr.max_request_iov = 4;
          ep_attr.max_rdma_read_in = DAPL_DEFAULT_MAX_RDMA_IN;
          ep_attr.max_rdma_read_out = DAPL_DEFAULT_MAX_RDMA_OUT;

          ep_attr.ep_transport_specific_count = 0;
          ep_attr.ep_transport_specific = NULL;
          ep_attr.ep_provider_specific_count = 0;
          ep_attr.ep_provider_specific = NULL;
          ep_attr.max_rdma_read_iov = 0;
          ep_attr.max_rdma_write_iov = 0;
          ep_attr.srq_soft_hw = 0;
      }
    else
      {
          ep_attr.service_type = DAT_SERVICE_TYPE_RC;
          ep_attr.max_mtu_size = rdma_default_mtu_size;
          ep_attr.max_message_size = ia_attr.max_message_size;
          ep_attr.max_rdma_size = ia_attr.max_rdma_size;
          ep_attr.qos = DAT_QOS_BEST_EFFORT;
          ep_attr.recv_completion_flags = DAT_COMPLETION_DEFAULT_FLAG;
          ep_attr.request_completion_flags = DAT_COMPLETION_UNSIGNALLED_FLAG;
          ep_attr.max_recv_dtos =
              MIN (rdma_default_max_recv_wqe, ia_attr.max_dto_per_ep);
          ep_attr.max_request_dtos =
              MIN (rdma_default_max_send_wqe, ia_attr.max_dto_per_ep);
          ep_attr.max_recv_iov =
              MIN (rdma_default_max_sg_list,
                   ia_attr.max_iov_segments_per_dto);
          ep_attr.max_request_iov =
              MIN (rdma_default_max_sg_list,
                   ia_attr.max_iov_segments_per_dto);
          ep_attr.max_rdma_write_iov = 0;
          ep_attr.max_rdma_read_iov = 0;
          ep_attr.max_rdma_read_in = DAPL_DEFAULT_MAX_RDMA_IN;
          ep_attr.max_rdma_read_out = DAPL_DEFAULT_MAX_RDMA_OUT;

          ep_attr.ep_transport_specific_count = 0;
          ep_attr.ep_transport_specific = NULL;
          ep_attr.ep_provider_specific_count = 0;
          ep_attr.ep_provider_specific = NULL;
      }

      ret = dat_ep_create (proc->nic[0],
                           proc->ptag[0],
                           proc->cq_hndl[0],
                           proc->cq_hndl[0],
                           proc->conn_cq_hndl[0],
                           &ep_attr,
                           &(vc->mrail.qp_hndl[0]));
      CHECK_RETURN (ret, "Could not create EP");
}

/* Allocate memory and handlers */
int
rdma_iba_allocate_memory (struct MPIDI_CH3I_RDMA_Process_t *proc,
                          MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    int ret, i = 0;
    int iter_hca;

  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    /*The memory for sending the long int variable */

    VIP_MEM_HANDLE mem_handle;
    DAT_REGION_DESCRIPTION region;
    DAT_VLEN reg_size;
    DAT_VADDR reg_addr;
    DAT_RETURN result;

    /* First allocate space for RDMA fast path for every connection */
    for (i = 0; i < pg_size; i++)
      {
          int tmp_index, j;
          if (i == pg_rank)
              continue;

          /* Allocate RDMA buffers, have an extra one for some ACK message */
          MPIDI_PG_Get_vc (cached_pg, i, &vc);
#define RDMA_ALIGNMENT 4096
#define RDMA_ALIGNMENT_OFFSET (4096)
          /* allocate RDMA buffers */
          vc->mrail.rfp.RDMA_send_buf_orig =
              MPIU_Malloc (sizeof (struct vbuf) * (num_rdma_buffer) +
                      2 * RDMA_ALIGNMENT);
          vc->mrail.rfp.RDMA_recv_buf_orig =
              MPIU_Malloc (sizeof (struct vbuf) * (num_rdma_buffer) +
                      2 * RDMA_ALIGNMENT);
          /* align vbuf->buffer to 64 byte boundary */
          vc->mrail.rfp.RDMA_send_buf =
              (struct vbuf
               *) ((unsigned long) (vc->mrail.rfp.RDMA_send_buf_orig)
                   / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                   RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);
          vc->mrail.rfp.RDMA_recv_buf =
              (struct vbuf
               *) ((unsigned long) (vc->mrail.rfp.RDMA_recv_buf_orig)
                   / RDMA_ALIGNMENT * RDMA_ALIGNMENT +
                   RDMA_ALIGNMENT + RDMA_ALIGNMENT_OFFSET);

          if (!vc->mrail.rfp.RDMA_send_buf || !vc->mrail.rfp.RDMA_recv_buf)
            {
                fprintf (stderr, "[%s:%d]: %s\n", __FILE__, __LINE__,
                         "Fail to register required buffers");
                exit (1);
            }

          /* zero buffers */
          MPIU_Memset (vc->mrail.rfp.RDMA_send_buf, 0,
                 sizeof (struct vbuf) * (num_rdma_buffer));
          MPIU_Memset (vc->mrail.rfp.RDMA_recv_buf, 0,
                 sizeof (struct vbuf) * (num_rdma_buffer));

          DEBUG_PRINT ("sizeof vbuf %d, numrdma %d\n", sizeof (struct vbuf),
                       num_rdma_buffer);
          /* set pointers */
          vc->mrail.rfp.phead_RDMA_send = 0;
          vc->mrail.rfp.ptail_RDMA_send = num_rdma_buffer - 1;
          vc->mrail.rfp.p_RDMA_recv = 0;
          vc->mrail.rfp.p_RDMA_recv_tail = num_rdma_buffer - 1;

          vc->mrail.rfp.RDMA_send_buf_hndl = MPIU_Malloc (sizeof (VIP_MEM_HANDLE) *
                                                     MPIDI_CH3I_RDMA_Process.
                                                     num_hcas);
          vc->mrail.rfp.RDMA_recv_buf_hndl =
              MPIU_Malloc (sizeof (VIP_MEM_HANDLE) *
                      MPIDI_CH3I_RDMA_Process.num_hcas);

          for (iter_hca = 0; iter_hca < MPIDI_CH3I_RDMA_Process.num_hcas;
               iter_hca++)
            {
                /* initialize unsignal record */

                region.for_va = (DAT_PVOID) vc->mrail.rfp.RDMA_send_buf;
                result = dat_lmr_create (proc->nic[iter_hca],
                                         DAT_MEM_TYPE_VIRTUAL, region,
                                         sizeof (struct vbuf) *
                                         (num_rdma_buffer),
                                         proc->ptag[iter_hca],
                                         DAT_MEM_PRIV_ALL_FLAG,
#if DAT_VERSION_MAJOR > 1 
                                         DAT_VA_TYPE_VA,
#endif /* if DAT_VERSION_MAJOR > 1 */
                                         &(vc->mrail.rfp.
                                           RDMA_send_buf_hndl[iter_hca].hndl),
                                         &(vc->mrail.rfp.
                                           RDMA_send_buf_hndl[iter_hca].lkey),
                                         &(vc->mrail.rfp.
                                           RDMA_send_buf_hndl[iter_hca].rkey),
                                         &reg_size, &reg_addr);

                if (result != DAT_SUCCESS)
                    udapl_error_abort (GEN_ASSERT_ERR, "cannot create lmr");

                region.for_va = (DAT_PVOID) vc->mrail.rfp.RDMA_recv_buf;
                result = dat_lmr_create (proc->nic[iter_hca],
                                         DAT_MEM_TYPE_VIRTUAL, region,
                                         sizeof (struct vbuf) *
                                         (num_rdma_buffer),
                                         proc->ptag[iter_hca],
                                         DAT_MEM_PRIV_ALL_FLAG,
#if DAT_VERSION_MAJOR > 1
                                         DAT_VA_TYPE_VA,
#endif /* if DAT_VERSION_MAJOR > 1 */
                                         &(vc->mrail.rfp.
                                           RDMA_recv_buf_hndl[iter_hca].hndl),
                                         &(vc->mrail.rfp.
                                           RDMA_recv_buf_hndl[iter_hca].lkey),
                                         &(vc->mrail.rfp.
                                           RDMA_recv_buf_hndl[iter_hca].rkey),
                                         &reg_size, &reg_addr);

                if (result != DAT_SUCCESS)
                    udapl_error_abort (GEN_ASSERT_ERR, "cannot create lmr");

            }
      }
  }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    vc->mrail.postsend_times_1sc = 0;
  }

    /* We need now allocate vbufs for send/recv path */
    allocate_vbufs (MPIDI_CH3I_RDMA_Process.nic, MPIDI_CH3I_RDMA_Process.ptag,
                    udapl_vbuf_pool_size);
    return MPI_SUCCESS;
}

int
rdma_iba_enable_connections (struct MPIDI_CH3I_RDMA_Process_t *proc,
                             MPIDI_VC_t * vc, int pg_rank, int pg_size)
{
    int i, step;
    DAT_RETURN ret = DAT_SUCCESS;
    int num_connected;
    DAT_EVENT event;
    int count;
    int tmp1;

    num_connected = 0;
    for (step = 1; step < pg_size; step++)
      {
          int hca_index = 0, qp_index;
          int j = 0;

          for (qp_index = 0; qp_index < vc->mrail.num_total_subrails;
               qp_index++)
            {
                tmp1 = 0;

                if (pg_rank - step >= 0)
                  {
                      /* I am the client, make the connection */
                      tmp1++;
                      i = pg_rank - step;
                      MPIDI_PG_Get_vc (cached_pg, i, &vc);
                      ret = dat_ep_connect (vc->mrail.qp_hndl[qp_index], &rdma_iba_addr_table.ia_addr[i][hca_index], rdma_iba_addr_table.service_id[i][hca_index], DAT_TIMEOUT_INFINITE, sizeof (int),  /* private_data_size */
                                            (DAT_PVOID) & pg_rank,      /* private_data */
                                            DAT_QOS_BEST_EFFORT,        /* QoS */
                                            DAT_CONNECT_DEFAULT_FLAG);

                      CHECK_RETURN (ret, "Could not connect to remote EP");
                  }
                if (pg_rank + step < pg_size)
                  {
                      /* I am the server, the conn_server thread will wait for the connection request from the client */
                      conn_server (1, pg_rank, pg_size);
                      tmp1++;
                  }

                PMI_Barrier ();

                while (num_connected < tmp1)
                  {
                      ret =
                          dat_evd_wait (proc->conn_cq_hndl[hca_index],
                                        DAT_TIMEOUT_INFINITE, 1, &event,
                                        &count);

                      if (ret != DAT_SUCCESS)
                          continue;

                      switch (event.event_number)
                        {
                        case DAT_CONNECTION_EVENT_TIMED_OUT:
                            printf ("Error: dat_evd_wait() time out. \n");
                            break;

                        case DAT_CONNECTION_EVENT_ESTABLISHED:
                            num_connected++;
                            break;

                        default:
                            break;
                        }
                  }
                num_connected = 0;
                PMI_Barrier ();

                j++;
                if (j == vc->mrail.subrail_per_hca)
                  {
                      j = 0;
                      hca_index++;
                  }
            }
      }

  if (MPIDI_CH3I_RDMA_Process.has_one_sided) {
    int num_connected_1sc = 0;
    for (step = 1; step < pg_size; step++)
      {
          tmp1 = 0;

          if (pg_rank - step >= 0)
            {
                /* I am the client, make the connection */
                tmp1++;
                i = pg_rank - step;
                MPIDI_PG_Get_vc (cached_pg, i, &vc);

                ret = dat_ep_connect (vc->mrail.qp_hndl_1sc, &rdma_iba_addr_table.ia_addr[i][0], rdma_iba_addr_table.service_id_1sc[i][0], DAT_TIMEOUT_INFINITE, sizeof (int),  /* private_data_size */
                                      (DAT_PVOID) & pg_rank,    /* private_data */
                                      DAT_QOS_BEST_EFFORT,      /* QoS */
                                      DAT_CONNECT_DEFAULT_FLAG);

                CHECK_RETURN (ret, "Could not connect to remote EP");
            }
          if (pg_rank + step < pg_size)
            {
                tmp1++;
                /* I am the server, the conn_server thread will wait for the connection request from the client */
                conn_server_1sc (1, pg_rank, pg_size);
            }

          PMI_Barrier ();
          /* Be the server or the client, there will be an event indicating that the connection is established */
          while (num_connected_1sc < tmp1)
            {
                ret =
                    dat_evd_wait (proc->conn_cq_hndl_1sc,
                                  DAT_TIMEOUT_INFINITE, 1, &event, &count);

                if (ret != DAT_SUCCESS)
                    continue;

                switch (event.event_number)
                  {
                  case DAT_CONNECTION_EVENT_TIMED_OUT:
                      break;

                  case DAT_CONNECTION_EVENT_ESTABLISHED:
                      num_connected_1sc++;
                      break;

                  default:
                      break;
                  }
            }
          num_connected_1sc = 0;
          PMI_Barrier ();
      }
  }

    return DAT_SUCCESS;
}

void
MRAILI_Init_vc (MPIDI_VC_t * vc, int pg_rank)
{
    int channels = vc->mrail.num_total_subrails;
    int i;
    MRAILI_Channel_info subchannel;

    vc->mrail.send_wqes_avail = MPIU_Malloc (sizeof (int) * channels);
    vc->mrail.ext_sendq_head = MPIU_Malloc (sizeof (aint_t) * channels);
    vc->mrail.ext_sendq_tail = MPIU_Malloc (sizeof (aint_t) * channels);

    /* Now we will need to */
    for (i = 0; i < channels; i++)
      {
          vc->mrail.send_wqes_avail[i] = rdma_default_max_send_wqe;
          DEBUG_PRINT ("set send_wqe_avail as %d\n",
                       rdma_default_max_send_wqe);
          vc->mrail.ext_sendq_head[i] = NULL;
          vc->mrail.ext_sendq_tail[i] = NULL;
      }
    vc->mrail.next_packet_expected = 0;
    vc->mrail.next_packet_tosend = 0;

  if (MPIDI_CH3I_RDMA_Process.has_rdma_fast_path) {
    /* prefill desc of credit_vbuf */
    for (i = 0; i < num_rdma_buffer; i++)
      {
          /* prefill vbuf desc */
          vbuf_init_rdma_write (&vc->mrail.rfp.RDMA_send_buf[i]);
          /* associate vc to vbuf */
          vc->mrail.rfp.RDMA_send_buf[i].vc = (void *) vc;
          vc->mrail.rfp.RDMA_send_buf[i].padding = FREE_FLAG;
          vc->mrail.rfp.RDMA_recv_buf[i].vc = (void *) vc;
          vc->mrail.rfp.RDMA_recv_buf[i].padding = BUSY_FLAG;
      }

    vc->mrail.rfp.rdma_credit = 0;
#ifndef MV2_DISABLE_HEADER_CACHING 
    vc->mrail.rfp.cached_miss = 0;
    vc->mrail.rfp.cached_hit = 0;
    vc->mrail.rfp.cached_incoming = MPIU_Malloc (sizeof (MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = MPIU_Malloc (sizeof (MPIDI_CH3_Pkt_send_t));
    MPIU_Memset (vc->mrail.rfp.cached_outgoing, 0, sizeof (MPIDI_CH3_Pkt_send_t));
    MPIU_Memset (vc->mrail.rfp.cached_incoming, 0, sizeof (MPIDI_CH3_Pkt_send_t));
#endif
    vc->mrail.cmanager.total_subrails = 2;
    vc->mrail.cmanager.num_local_pollings = 1;
    vc->mrail.cmanager.poll_channel = MPIU_Malloc (sizeof (vbuf * (**)(void *)));
  } else {
      vc->mrail.cmanager.total_subrails = 1;
      vc->mrail.cmanager.num_local_pollings = 0;
  }

    vc->mrail.cmanager.v_queue_head =
        MPIU_Malloc (sizeof (vbuf *) * vc->mrail.cmanager.total_subrails);
    vc->mrail.cmanager.v_queue_tail =
        MPIU_Malloc (sizeof (vbuf *) * vc->mrail.cmanager.total_subrails);
    vc->mrail.cmanager.len =
        MPIU_Malloc (sizeof (int) * vc->mrail.cmanager.total_subrails);

    for (i = 0; i < vc->mrail.cmanager.total_subrails; i++)
      {
          vc->mrail.cmanager.v_queue_head[i] =
              vc->mrail.cmanager.v_queue_tail[i] = NULL;
          vc->mrail.cmanager.len[i] = 0;
      }

    DEBUG_PRINT ("Cmanager total rail %d\n",
                 vc->mrail.cmanager.total_subrails);

    /* Set the initial value for credits */
    /* And for each queue pair, we need to prepost a certain number of recv descriptors */
    vc->mrail.srp.backlog.len = 0;
    vc->mrail.srp.backlog.vbuf_head = NULL;
    vc->mrail.srp.backlog.vbuf_tail = NULL;

    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow = NULL;
    vc->mrail.inflow = 0;

    for (i = 0; i < vc->mrail.num_total_subrails; i++)
      {
          int k;

          MRAILI_CHANNEL_INFO_INIT (subchannel, i, vc);

          for (k = 0; k < udapl_initial_prepost_depth; k++)
            {
                PREPOST_VBUF_RECV (vc, subchannel);
            }

          vc->mrail.srp.remote_credit[i] = udapl_initial_credits;
          vc->mrail.srp.remote_cc[i] = udapl_initial_credits;
          vc->mrail.srp.local_credit[i] = 0;
          vc->mrail.srp.preposts[i] = udapl_initial_prepost_depth;
          vc->mrail.srp.initialized[i] =
              (udapl_prepost_depth == udapl_initial_prepost_depth);

          vc->mrail.srp.rendezvous_packets_expected[i] = 0;
      }
    DEBUG_PRINT
        ("[Init:priv] remote_credit %d, remote_cc %d, local_credit %d, prepost%d\n ",
         vc->mrail.srp.remote_credit[0], vc->mrail.srp.remote_cc[0],
         vc->mrail.srp.local_credit[0], vc->mrail.srp.preposts[0]);
}

void
MPIDI_CH3I_RDMA_util_get_ia_addr (DAT_SOCK_ADDR * ia_addr,
                                  DAT_SOCK_ADDR * ret_addr)
{
    int i;
    char *p;
    static char hostname[255];

    MPIU_Memcpy (ret_addr, ia_addr, sizeof (DAT_SOCK_ADDR));

}

/*  convert DAT_SOCK_ADDR to string  */
void
MPIDI_CH3I_RDMA_util_atos (char *str, DAT_SOCK_ADDR * addr)
{
    sprintf (str, "%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u", addr->sa_family,      /* short */
             (unsigned char) addr->sa_data[0],  /* char */
             (unsigned char) addr->sa_data[1],
             (unsigned char) addr->sa_data[2],
             (unsigned char) addr->sa_data[3],
             (unsigned char) addr->sa_data[4],
             (unsigned char) addr->sa_data[5],
             (unsigned char) addr->sa_data[6],
             (unsigned char) addr->sa_data[7],
             (unsigned char) addr->sa_data[8],
             (unsigned char) addr->sa_data[9],
             (unsigned char) addr->sa_data[10],
             (unsigned char) addr->sa_data[11],
             (unsigned char) addr->sa_data[12],
             (unsigned char) addr->sa_data[13]);
}

/*  convert string to DAT_SOCK_ADDR  */
void
MPIDI_CH3I_RDMA_util_stoa (DAT_SOCK_ADDR * addr, char *str)
{
    int xx, x[14], j;

    sscanf (str, "%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u",
            &xx, &x[0], &x[1], &x[2], &x[3], &x[4], &x[5], &x[6],
            &x[7], &x[8], &x[9], &x[10], &x[11], &x[12], &x[13]);
    addr->sa_family = xx;
    for (j = 0; j < 14; j++)
      {
          addr->sa_data[j] = x[j];
      }
}

static void
conn_server (int n, int pg_rank, int pg_size)
{
    DAT_RETURN status;
    DAT_EVENT event;
    DAT_COUNT count;
    DAT_CR_ARRIVAL_EVENT_DATA cr_stat;
    DAT_CR_PARAM cr_param;
    MPIDI_VC_t *vc;
    int i;
    int num = 0;

    while (num < n)
      {
          status =
              dat_evd_wait (MPIDI_CH3I_RDMA_Process.creq_cq_hndl[0],
                            DAT_TIMEOUT_INFINITE, 1, &event, &count);
          if (status != DAT_SUCCESS)
            {
                continue;
            }
          if (event.event_number == DAT_CONNECTION_REQUEST_EVENT)
            {
                cr_stat = event.event_data.cr_arrival_event_data;
                if ((cr_stat.conn_qual !=
                     rdma_iba_addr_table.service_id[pg_rank][0])
                    || (cr_stat.sp_handle.psp_handle !=
                        MPIDI_CH3I_RDMA_Process.psp_hndl[0]))
                  {
                      MPIU_Internal_error_printf
                          ("Error: connection request mismatch.\n");
                      if (cr_stat.cr_handle)
                        {
                            dat_cr_reject(
                                cr_stat.cr_handle
#if DAT_VERSION_MAJOR > 1 
                                , 0,
                                NULL
#endif /* if DAT_VERSION_MAJOR > 1 */
                            );
                        }
                      continue;
                  }

                status =
                    dat_cr_query (cr_stat.cr_handle,
                                  DAT_CR_FIELD_PRIVATE_DATA, &cr_param);
                if (status != DAT_SUCCESS)
                  {
                      MPIU_Internal_error_printf
                          ("Error: fail to get private data from the connection request.\n");
                      continue;
                  }

                i = *(int *) cr_param.private_data;

                MPIDI_PG_Get_vc (cached_pg, i, &vc);
                status =
                    dat_cr_accept (cr_stat.cr_handle, vc->mrail.qp_hndl[0], 0,
                                   (DAT_PVOID) NULL);
                if (status != DAT_SUCCESS)
                  {
                      MPIU_Internal_error_printf
                          ("Error: fail to send REP.\n");
                  }
                num++;
            }                   /* if */
      }                         /* while */
}

static void
conn_server_1sc (int n, int pg_rank, int pg_size)
{
    DAT_RETURN status;
    DAT_EVENT event;
    DAT_COUNT count;
    DAT_CR_ARRIVAL_EVENT_DATA cr_stat;
    DAT_CR_PARAM cr_param;
    MPIDI_VC_t *vc;
    int i;
    int num = 0;

    while (num < n)
      {
          status =
              dat_evd_wait (MPIDI_CH3I_RDMA_Process.creq_cq_hndl_1sc,
                            DAT_TIMEOUT_INFINITE, 1, &event, &count);
          if (status != DAT_SUCCESS)
            {
                continue;
            }
          if (event.event_number == DAT_CONNECTION_REQUEST_EVENT)
            {
                cr_stat = event.event_data.cr_arrival_event_data;
                if ((cr_stat.conn_qual !=
                     rdma_iba_addr_table.service_id_1sc[pg_rank][0])
                    || (cr_stat.sp_handle.psp_handle !=
                        MPIDI_CH3I_RDMA_Process.psp_hndl_1sc))
                  {
                      MPIU_Internal_error_printf
                          ("Error: connection request mismatch.\n");
                      if (cr_stat.cr_handle)
                        {
                            dat_cr_reject(
                                cr_stat.cr_handle
#if DAT_VERSION_MAJOR > 1
                                , 0,
                                NULL
#endif /* if DAT_VERSION_MAJOR > 1 */
                            );
                        }
                      continue;
                  }

                status =
                    dat_cr_query (cr_stat.cr_handle,
                                  DAT_CR_FIELD_PRIVATE_DATA, &cr_param);
                if (status != DAT_SUCCESS)
                  {
                      MPIU_Internal_error_printf
                          ("Error: fail to get private data from the connection request.\n");
                      continue;
                  }

                i = *(int *) cr_param.private_data;

                MPIDI_PG_Get_vc (cached_pg, i, &vc);
                status =
                    dat_cr_accept (cr_stat.cr_handle, vc->mrail.qp_hndl_1sc,
                                   0, (DAT_PVOID) NULL);
                if (status != DAT_SUCCESS)
                  {
                      MPIU_Internal_error_printf
                          ("Error: fail to send REP.\n");
                  }
                num++;
            }                   /* if */
      }                         /* while */
}

