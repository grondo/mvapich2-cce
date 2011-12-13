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

#include "rdma_impl.h"
#include "mpidi_ch3i_rdma_conf.h"
#include "pmi.h"
#include "vbuf.h"
#include "ibv_param.h"
#include "rdma_cm.h"
#include "mpiutil.h"
#include "cm.h"
#include "dreg.h"
#include "debug_utils.h"

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

int qp_required(MPIDI_VC_t* vc, int my_rank, int dst_rank)
{
	int qp_reqd = 1;

	if ((my_rank == dst_rank) ||
		(rdma_use_smp && (vc->smp.local_rank != -1))) {
		/* Process is local */
		qp_reqd = 0;
	}

	return qp_reqd;
}

int power_two(int x)
{
    int pow = 1;

    while (x) {
        pow = pow * 2;
        x--;
    }

    return pow;
}

struct process_init_info *alloc_process_init_info(int pg_size, int rails)
{
    struct process_init_info *info;
    int i;

    info = MPIU_Malloc(sizeof *info);
    if (!info) {
        return NULL;
    }

    info->lid = (uint16_t **) MPIU_Malloc(pg_size * sizeof(uint16_t *));
    info->gid = (union ibv_gid **)
                    MPIU_Malloc(pg_size * sizeof(union ibv_gid *));
    info->hostid = (int **) MPIU_Malloc(pg_size * sizeof(int *));
    info->qp_num_rdma = (uint32_t **)
                            MPIU_Malloc(pg_size * sizeof(uint32_t *));
    info->hca_type = (uint32_t *) MPIU_Malloc(pg_size * sizeof(uint32_t));
    info->vc_addr  = (uint64_t *) MPIU_Malloc(pg_size * sizeof(uint64_t));
    if (!info->lid
        || !info->gid 
        || !info->hostid 
        || !info->qp_num_rdma
        || !info->hca_type
        || !info->vc_addr) {
        return NULL;
    }

    for (i = 0; i < pg_size; ++i) {
        info->qp_num_rdma[i] = (uint32_t *)
                                    MPIU_Malloc(rails * sizeof(uint32_t));
        info->lid[i] = (uint16_t *) MPIU_Malloc(rails * sizeof(uint16_t));
        info->gid[i] = (union ibv_gid *)
                         MPIU_Malloc(rails * sizeof(union ibv_gid));
        info->hostid[i] = (int *) MPIU_Malloc(rails * sizeof(int));
        if (!info->lid[i]
                || !info->gid[i]
                || !info->hostid[i]
                || !info->qp_num_rdma[i]) {
             return NULL;
        }
    }

    return info;
}

void free_process_init_info(struct process_init_info *info, int pg_size)
{
    int i;

    if (!info)
        return;

    for (i = 0; i < pg_size; ++ i) {
        MPIU_Free(info->qp_num_rdma[i]);
        MPIU_Free(info->lid[i]);
        MPIU_Free(info->gid[i]);
        MPIU_Free(info->hostid[i]);
    }

    MPIU_Free(info->lid);
    MPIU_Free(info->gid);
    MPIU_Free(info->hostid);
    MPIU_Free(info->qp_num_rdma);
    MPIU_Free(info->hca_type);
    MPIU_Free(info->vc_addr);
    MPIU_Free(info); 
}

struct ibv_srq *create_srq(struct MPIDI_CH3I_RDMA_Process_t *proc,
                                int hca_num)
{
    struct ibv_srq_init_attr srq_init_attr;
    struct ibv_srq *srq_ptr = NULL;

    MPIU_Memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context = proc->nic_context[hca_num];
    srq_init_attr.attr.max_wr = viadev_srq_alloc_size;
    srq_init_attr.attr.max_sge = 1;
    /* The limit value should be ignored during SRQ create */
    srq_init_attr.attr.srq_limit = viadev_srq_limit;

#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        srq_ptr = ibv_create_xrc_srq (proc->ptag[hca_num], 
                proc->xrc_domain[hca_num], proc->cq_hndl[hca_num], 
                &srq_init_attr);
        PRINT_DEBUG(DEBUG_XRC_verbose>0, "created xrc srq %d\n", 
                srq_ptr->xrc_srq_num);
    }
    else 
#endif /* _ENABLE_XRC_ */
    {
        srq_ptr = ibv_create_srq(proc->ptag[hca_num], &srq_init_attr);
    }

    if (!srq_ptr) {
        ibv_error_abort(IBV_RETURN_ERR, "Error creating SRQ\n");
    }

    return srq_ptr;
}

static int check_attrs( struct ibv_port_attr *port_attr, struct ibv_device_attr *dev_attr)
{
    int ret = 0;
#ifdef _ENABLE_XRC_
    if (USE_XRC && !(dev_attr->device_cap_flags & IBV_DEVICE_XRC)) {
        fprintf (stderr, "HCA does not support XRC. Disable MV2_USE_XRC.\n");
        ret = 1;
    }
#endif /* _ENABLE_XRC_ */
    if(port_attr->active_mtu < rdma_default_mtu) {
        fprintf(stderr,
                "Active MTU is %d, MV2_DEFAULT_MTU set to %d. See User Guide\n",
                port_attr->active_mtu, rdma_default_mtu);
        ret = 1;
    }

    if(dev_attr->max_qp_rd_atom < rdma_default_qp_ous_rd_atom) {
        fprintf(stderr,
                "Max MV2_DEFAULT_QP_OUS_RD_ATOM is %d, set to %d\n",
                dev_attr->max_qp_rd_atom, rdma_default_qp_ous_rd_atom);
        ret = 1;
    }

    if(MPIDI_CH3I_RDMA_Process.has_srq) {
        if(dev_attr->max_srq_sge < rdma_default_max_sg_list) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_srq_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_srq_wr < viadev_srq_alloc_size) {
            fprintf(stderr,
                    "Max MV2_SRQ_SIZE is %d, set to %d\n",
                    dev_attr->max_srq_wr, (int) viadev_srq_alloc_size);
            ret = 1;
        }
    } else {
        if(dev_attr->max_sge < rdma_default_max_sg_list) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_qp_wr < rdma_default_max_send_wqe) {
            fprintf(stderr,
                    "Max MV2_DEFAULT_MAX_SEND_WQE is %d, set to %d\n",
                    dev_attr->max_qp_wr, (int) rdma_default_max_send_wqe);
            ret = 1;
        }
    }
    if(dev_attr->max_cqe < rdma_default_max_cq_size) {
        fprintf(stderr,
                "Max MV2_DEFAULT_MAX_CQ_SIZE is %d, set to %d\n",
                dev_attr->max_cqe, (int) rdma_default_max_cq_size);
        ret = 1;
    }

    return ret;
}

/*
 * Function: rdma_find_active_port
 *
 * Description:
 *      Finds if the given device has any active ports.
 *
 * Input:
 *      context -   Pointer to the device context obtained by opening device.
 *      ib_dev  -   Pointer to the device from ibv_get_device_list.
 *
 * Return:
 *      Success:    Port number of the active port.
 *      Failure:    ERROR (-1).
 */
int rdma_find_active_port(struct ibv_context *context,struct ibv_device *ib_dev)
{
    int j = 0;
    const char *dev_name = NULL;
    struct ibv_port_attr port_attr;

    if (NULL == ib_dev) {
        return ERROR;
    } else {
        dev_name = ibv_get_device_name(ib_dev);
    }

    for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; ++ j) {
        if ((! ibv_query_port(context, j, &port_attr)) &&
             port_attr.state == IBV_PORT_ACTIVE) {
            if (!strncmp(dev_name, "cxgb3", 5) || !strncmp(dev_name, "cxgb4", 5)
                 || port_attr.lid || (!port_attr.lid && use_iboeth )) {
                /* Chelsio RNIC's don't get LID's as they're not IB devices.
                 * So dont do this check for them. LID on RoCE will be zero.
                 */
                DEBUG_PRINT("Active port number = %d, state = %s, lid = %d\r\n",
                    j, (port_attr.state==IBV_PORT_ACTIVE)?"Active":"Not Active",
                    port_attr.lid);
                return j;
            }
        }
    }

    return ERROR;
}

/*
 * Function: rdma_find_network_type
 *
 * Description:
 *      On a multi-rail network, retrieve the network to use
 *
 * Input:
 *      dev_list        -  List of HCAs on the system.
 *      num_devices     -  Number of HCAs on the system.
 *      num_usable_hcas -  Number of usable HCAs on the system.
 *
 * Return:
 *      Success:    MPI_SUCCESS.
 *      Failure:    ERROR (-1).
 */
#undef FUNCNAME
#define FUNCNAME rdma_find_network_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_find_network_type(struct ibv_device **dev_list, int num_devices,
                           int *num_usable_hcas)
{
    int i = 0;
    int hca_type = 0;
    int network_type = MV2_NETWORK_CLASS_UNKNOWN;
    int num_ib_cards = 0;
    int num_iwarp_cards = 0;
    int num_unknwn_cards = 0;

    dev_list = ibv_get_device_list(&num_devices);

    for (i = 0; i < num_devices; ++i) {
        hca_type = mv2_get_hca_type(dev_list[i]);
        if (MV2_IS_IB_CARD( hca_type )) {
            num_ib_cards++;
        } else if (MV2_IS_IWARP_CARD( hca_type )) {
            num_iwarp_cards++;
        } else {
            num_unknwn_cards++;
        }
        if (MV2_IS_QLE_CARD( hca_type )) {
            PRINT_ERROR("QLogic IB card detected in system\n");
            PRINT_ERROR("Please re-configure the library with the"
                        " '--with-device=ch3:psm' configure option"
                        " for best performance\n");
        }
    }

    if (num_ib_cards && (num_ib_cards >= num_iwarp_cards)) {
        network_type = MV2_NETWORK_CLASS_IB;
        *num_usable_hcas = num_ib_cards;
    } else if (num_iwarp_cards && (num_ib_cards < num_iwarp_cards)) {
        network_type = MV2_NETWORK_CLASS_IWARP;
        *num_usable_hcas = num_iwarp_cards;
    } else {
        network_type = MV2_NETWORK_CLASS_UNKNOWN;
        *num_usable_hcas = num_unknwn_cards;
    }

    return network_type;
}

/*
 * Function: rdma_skip_network_card
 *
 * Description:
 *      On a multi-rail network, skip the HCA that does not match with the
 *      network type
 *
 * Input:
 *      network_type -   Type of underlying network.
 *      ib_dev       -   Pointer to HCA data structure.
 *
 * Return:
 *      Success:    MPI_SUCCESS.
 *      Failure:    ERROR (-1).
 */
#undef FUNCNAME
#define FUNCNAME rdma_skip_network_card
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_skip_network_card(mv2_iba_network_classes network_type,
                           struct ibv_device *ib_dev)
{
    int skip = 0;
    int hca_type = 0;

    hca_type = mv2_get_hca_type(ib_dev);

    if ((network_type == MV2_NETWORK_CLASS_IB) &&
        (MV2_IS_IWARP_CARD( hca_type ))) {
       skip = 1;
    } else if ((network_type == MV2_NETWORK_CLASS_IWARP) &&
        ( MV2_IS_IB_CARD( hca_type ))) {
       skip = 1;
    } else {
       skip = 0;
    }

    return skip;
}

void ring_rdma_close_hca(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int err;
    proc->boot_device = NULL;
    err = ibv_dealloc_pd(proc->boot_ptag);
    if (err)  {
        MPIU_Error_printf("Failed to dealloc pd (%s)\n", strerror(errno));
    }
    err = ibv_close_device(proc->boot_context);
    if (err) {
        MPIU_Error_printf("Failed to close ib device (%s)\n", strerror(errno));
    }
}

#undef FUNCNAME
#define FUNCNAME ring_rdma_open_hca
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int ring_rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i = 0;
    int num_devices = 0;
    int err;
    struct ibv_device *ib_dev = NULL;
    struct ibv_device **dev_list = NULL;
    int is_device_opened = 0;

    dev_list = ibv_get_device_list(&num_devices);
    
    for (i = 0; i < num_devices; i ++) {
        ib_dev = dev_list[i];
        if (!ib_dev) {
            goto fn_exit;
        }

        proc->boot_device = ib_dev;

        proc->boot_context = ibv_open_device(ib_dev);
        if (!proc->boot_context) {
            /* Go to next device */
            continue;
        }
        
        if (ERROR == rdma_find_active_port(proc->boot_context, ib_dev)) {
            /* No active ports. Go to next device */
            err = ibv_close_device(proc->boot_context);
            if (err) {
                MPIU_Error_printf("Failed to close ib device (%s)\n", strerror(errno));
            }
            continue;
        }
            
        proc->boot_ptag = ibv_alloc_pd(proc->boot_context);
        if (!proc->boot_ptag) {
            err = ibv_close_device(proc->boot_context);
            if (err) {
                MPIU_Error_printf("Failed to close ib device (%s)\n", strerror(errno));
            }
            continue;
        }

        is_device_opened = 1;
        break;
    }


fn_exit:
    /* Clean up before exit */
    ibv_free_device_list(dev_list);
    return is_device_opened;
}

/*
 * Function: rdma_open_hca
 *
 * Description:
 *      Opens the HCA and allocates protection domain for it.
 *
 * Input:
 *      proc    -   Pointer to the process data structure.
 *
 * Return:
 *      Success:    MPI_SUCCESS.
 *      Failure:    ERROR (-1).
 */
#undef FUNCNAME
#define FUNCNAME rdma_open_hca
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_open_hca(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i = 0, j = 0;
    int num_devices = 0;
    int network_type = 0;
    int num_usable_hcas = 0;
    int mpi_errno = MPI_SUCCESS;
    struct ibv_device *ib_dev = NULL;
    struct ibv_device **dev_list = NULL;
#ifdef CRC_CHECK
    gen_crc_table();
#endif

    rdma_num_hcas = 0;

    dev_list = ibv_get_device_list(&num_devices);
    
    network_type = rdma_find_network_type(dev_list, num_devices,
                                          &num_usable_hcas);

    if (network_type == MV2_NETWORK_CLASS_UNKNOWN) {
        if (num_usable_hcas) {
            PRINT_ERROR("Unknown HCA type: this build of MVAPICH2 does not"
                         "fully support the HCA found on the system (try with"
                         " other build options)\n");
        } else {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "No IB device found");
        }
    }

    for (i = 0; i < num_devices; i ++) {
        if (rdma_skip_network_card(network_type, dev_list[i])) {
            /* Skip HCA's that don't match with network type */
            continue;
        }

        if (rdma_multirail_usage_policy == MV2_MRAIL_BINDING) {
            /* Bind a process to a HCA */
            if (mrail_use_default_mapping) {
               mrail_user_defined_p2r_mapping = rdma_local_id % num_usable_hcas;
            }
            ib_dev = dev_list[mrail_user_defined_p2r_mapping];
        } else if (!strncmp(rdma_iba_hcas[i], RDMA_IBA_NULL_HCA, 32)) {
            /* User hasn't specified any HCA name
             * We will use the first available HCA(s) */
            ib_dev = dev_list[i];
        } else {
            /* User specified HCA(s), try to look for it */
            j = 0;
            while (dev_list[j]) {
                if (!strncmp(ibv_get_device_name(dev_list[j]),
                            rdma_iba_hcas[rdma_num_hcas], 32)) {
                    ib_dev = dev_list[j];
                    break;
                }
                j++;
            }
        }

        if (!ib_dev) {
            /* Clean up before exit */
            ibv_free_device_list(dev_list);
	        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		            "**fail %s", "No IB device found");
        }

        proc->ib_dev[rdma_num_hcas] = ib_dev;

        proc->nic_context[rdma_num_hcas] = ibv_open_device(ib_dev);
        if (!proc->nic_context[rdma_num_hcas]) {
            /* Clean up before exit */
            ibv_free_device_list(dev_list);
	        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		            "%s %d", "Failed to open HCA number", rdma_num_hcas);
        }

        proc->ptag[rdma_num_hcas] = ibv_alloc_pd(proc->nic_context[rdma_num_hcas]);
        if (!proc->ptag[rdma_num_hcas]) {
            /* Clean up before exit */
            ibv_free_device_list(dev_list);
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, 
                "**fail", "%s%d", "Failed to alloc pd number ", rdma_num_hcas);
        }

        rdma_num_hcas++;
        if ((rdma_multirail_usage_policy == MV2_MRAIL_BINDING) ||
            (rdma_num_req_hcas == rdma_num_hcas)) {
            /* If usage policy is binding, or if we have found enough
             * number of HCAs asked for by the user */
            break;
        }
    }

    if (!strncmp(rdma_iba_hcas[0], RDMA_IBA_NULL_HCA, 32) &&
        (1 == rdma_num_hcas) && (num_devices > 1) &&
        (ERROR == rdma_find_active_port(proc->nic_context[0], proc->ib_dev[0]))) {
        /* Trac #376 - There are multiple rdma capable devices (num_devices) in
         * the system. The user has asked us to use ANY (!strncmp) ONE device
         * (rdma_num_hcas), and the first device does not have an active port. So
         * try to find some other device with an active port.
         */
        for (j = 0; dev_list[j]; j++) {
            ib_dev = dev_list[j];
            if (ib_dev) {
                proc->nic_context[0] = ibv_open_device(ib_dev);
                if (!proc->nic_context[0]) {
                    /* Go to next device */
                    continue;
                }
                if (ERROR != rdma_find_active_port(proc->nic_context[0], ib_dev)) {
                    proc->ib_dev[0] = ib_dev;
                    proc->ptag[0] = ibv_alloc_pd(proc->nic_context[0]);
                    if (!proc->ptag[0]) {
                        /* Clean up before exit */
                        ibv_free_device_list(dev_list);
                        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER,
                             "**fail", "%s%d", "Failed to alloc pd number ", i);
                    }
                }
            }
        }
    }

fn_exit:
    /* Clean up before exit */
    ibv_free_device_list(dev_list);
    return mpi_errno;

fn_fail:
    /* Clean up before exit */
    ibv_free_device_list(dev_list);
    goto fn_exit;
}

#undef FUNCNAME
#define FUNCNAME rdma_iba_hca_init_noqp
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_iba_hca_init_noqp(struct MPIDI_CH3I_RDMA_Process_t *proc,
                  int pg_rank, int pg_size)
{
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;
    union ibv_gid gid;
    int mpi_errno = MPI_SUCCESS;
    int i, j, k;

    /* step 1: open hca, create ptags  and create cqs */
    for (i = 0; i < rdma_num_hcas; i++) {

        MPIU_Memset(&gid, 0, sizeof(gid));
        if(ibv_query_device(proc->nic_context[i], &dev_attr)) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno,
                    MPI_ERR_INTERN,
                    "**fail",
                    "**fail %s",
                    "Error getting HCA attributes\n");
        }

        /* detecting active ports */
        if (rdma_default_port < 0 || rdma_num_ports > 1) {
            k = 0;
            for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; j ++) {
                if ((! ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                        j, &port_attr)) &&
                        (port_attr.state == IBV_PORT_ACTIVE) &&
                        (port_attr.lid || (!port_attr.lid && use_iboeth))) {

                    if (use_iboeth) {
                       if (ibv_query_gid(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                        j, 0, &gid)) {
                            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                              "**fail", "Failed to retrieve gid on rank %d", pg_rank);
                        }
                        MPIDI_CH3I_RDMA_Process.gids[i][k] = gid;
                    } else {
                        MPIDI_CH3I_RDMA_Process.lids[i][k]    = port_attr.lid;
                    }

                    MPIDI_CH3I_RDMA_Process.ports[i][k++] = j;

                    if (check_attrs(&port_attr, &dev_attr)) {
                        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,
                                MPI_ERR_INTERN,
                                "**fail",
                                "**fail %s",
                                "Attributes failed sanity check");
                    }
                }
            }
            if (k < rdma_num_ports) {
                MPIU_ERR_SETFATALANDJUMP2(mpi_errno,
                        MPI_ERR_INTERN,
                        "**fail",
                        "**fail %s %d",
                        "Not enough ports are in active state"
                        "needed active ports %d\n", 
                        rdma_num_ports);
            }
        } else {
            if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, &port_attr)
                || (!port_attr.lid && !use_iboeth)
                || (port_attr.state != IBV_PORT_ACTIVE)) {
                MPIU_ERR_SETFATALANDJUMP2(mpi_errno,
                        MPI_ERR_INTERN,
                        "**fail",
                        "**fail %s %d",
                        "user specified port %d: fail to"
                        "query or not ACTIVE\n",
                        rdma_default_port);
            }
            if (use_iboeth) {
               if (ibv_query_gid(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, 0, &gid)) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "Failed to retrieve gid on rank %d", pg_rank);
                }
                MPIDI_CH3I_RDMA_Process.gids[i][0] = gid;
            } else {
                MPIDI_CH3I_RDMA_Process.lids[i][0]    = port_attr.lid;
            }

            MPIDI_CH3I_RDMA_Process.ports[i][0] = rdma_default_port;

            if (check_attrs(&port_attr, &dev_attr)) {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno,
                        MPI_ERR_INTERN,
                        "**fail",
                        "**fail %s",
                        "Attributes failed sanity check");
            }
        }

        if(rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);

            if(!proc->comp_channel[i]) {
                fprintf(stderr, "cannot create completion channel\n");
                goto err;
            }

            fprintf(stderr,"Created comp channel %p\n", proc->comp_channel[i]);

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);
            proc->send_cq_hndl[i] = NULL;
            proc->recv_cq_hndl[i] = NULL;

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err;
            }
        } else {

            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, NULL, 0);
            proc->send_cq_hndl[i] = NULL;
            proc->recv_cq_hndl[i] = NULL;

            if (!proc->cq_hndl[i]) {
                fprintf(stderr, "cannot create cq\n");
                goto err;
            }
        }

	    if (proc->has_srq) {
	        proc->srq_hndl[i] = create_srq(proc, i);
            if((proc->srq_hndl[i]) == NULL) {
                goto err_cq;
            }
#ifdef _ENABLE_XRC_
            proc->xrc_srqn[i] = proc->srq_hndl[i]->xrc_srq_num;
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "My SRQN=%d rail:%d\n", proc->xrc_srqn[i], i);
#endif /* _ENABLE_XRC_ */
        }
    }

    /*Port for all mgmt*/
    rdma_default_port = MPIDI_CH3I_RDMA_Process.ports[0][0]; 
    return 0;
    err_cq:
        for (i = 0; i < rdma_num_hcas; i ++) {
            if (proc->cq_hndl[i])
                ibv_destroy_cq(proc->cq_hndl[i]);
        }

    err:
        for (i = 0; i < rdma_num_hcas; i ++) {
            if (proc->ptag[i])
                ibv_dealloc_pd(proc->ptag[i]);
        }

        for (i = 0; i < rdma_num_hcas; i ++) {
            if (proc->nic_context[i])
                ibv_close_device(proc->nic_context[i]);
        }
fn_exit:
        return mpi_errno;

fn_fail:
        goto fn_exit;

}

#undef FUNCNAME
#define FUNCNAME rdma_iba_hca_init
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int rdma_iba_hca_init(struct MPIDI_CH3I_RDMA_Process_t *proc, int pg_rank,
                      MPIDI_PG_t *pg, struct process_init_info *info)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_attr      qp_attr;
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;

    MPIDI_VC_t	*vc;

    int i, j, k;
    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;
    int ports[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int lids[MAX_NUM_HCAS][MAX_NUM_PORTS];
    union ibv_gid gids[MAX_NUM_HCAS][MAX_NUM_PORTS];
    int mpi_errno = MPI_SUCCESS;
    int pg_size = MPIDI_PG_Get_size(pg);

    MPIU_Memset(&gids, 0, sizeof(gids));

#ifdef RDMA_CM
  if (!proc->use_rdma_cm)
  {
#endif
    /* step 1: open hca, create ptags  and create cqs */
    for (i = 0; i < rdma_num_hcas; i++) {
        if (ibv_query_device(proc->nic_context[i], &dev_attr)) {
	        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		        "**fail %s", "Error getting HCA attributes");
        }

        /* detecting active ports */
        if (rdma_default_port < 0 || rdma_num_ports > 1) {
            k = 0;
            for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; j ++) {
                if ((! ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                        j, &port_attr)) &&
                        (port_attr.state == IBV_PORT_ACTIVE) &&
                        (port_attr.lid || (!port_attr.lid && use_iboeth))) {

                    if (use_iboeth) {
                       if (ibv_query_gid(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                            j, 0, &gids[i][k])) {
                            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                              "**fail", "Failed to retrieve gid on rank %d", pg_rank);
                        }
                        DEBUG_PRINT("[%d] %s(%d): Getting gid[%d][%d] for"
                                " port %d subnet_prefix = %llx,"
                                " intf_id = %llx\r\n",
                                pg_rank, __FUNCTION__, __LINE__, i, k, k,
                                gids[i][k].global.subnet_prefix,
                                gids[i][k].global.interface_id);
                    } else {
                        lids[i][k]    = port_attr.lid;
                    }
                    ports[i][k++] = j;

                    if (check_attrs(&port_attr, &dev_attr)) {
			            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
				            "**fail", "**fail %s",
				            "Attributes failed sanity check");
                    }
                }
            }
            if (k < rdma_num_ports) {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                     "**activeports", "**activeports %d", rdma_num_ports);
            }
        } else {
            if(ibv_query_port(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                rdma_default_port, &port_attr)
                || (!port_attr.lid && !use_iboeth)
                || (port_attr.state != IBV_PORT_ACTIVE)) {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                     "**portquery", "**portquery %d", rdma_default_port);
            }

            ports[i][0] = rdma_default_port;

            if (use_iboeth) {
                if (ibv_query_gid(MPIDI_CH3I_RDMA_Process.nic_context[i],
                                    rdma_default_port, 0, &gids[i][0])) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**fail", "Failed to retrieve gid on rank %d", pg_rank);
                }

                if (check_attrs(&port_attr, &dev_attr)) {
		            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                       "**fail", "**fail %s", "Attributes failed sanity check");
                }
            } else {
                lids[i][0]  = port_attr.lid;
            }
        }

        if (rdma_use_blocking) {
            proc->comp_channel[i] = 
                ibv_create_comp_channel(proc->nic_context[i]);

            if (!proc->comp_channel[i]) {
		        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			        "**fail", "**fail %s", "cannot create completion channel");
            }

            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                    rdma_default_max_cq_size, NULL, proc->comp_channel[i], 0);
            proc->send_cq_hndl[i] = NULL;
            proc->recv_cq_hndl[i] = NULL;

            if (!proc->cq_hndl[i]) {
		        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			        "**fail", "**fail %s", "cannot create cq");
            }

            if (ibv_req_notify_cq(proc->cq_hndl[i], 0)) {
		        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			        "**fail", "**fail %s", "cannot request cq notification");
            }
        } else {
            /* Allocate the completion queue handle for the HCA */
            proc->cq_hndl[i] = ibv_create_cq(proc->nic_context[i],
                                    rdma_default_max_cq_size, NULL, NULL, 0);
            proc->send_cq_hndl[i] = NULL;
            proc->recv_cq_hndl[i] = NULL;

            if (!proc->cq_hndl[i]) {
		        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err,
			        "**fail", "**fail %s", "cannot create cq");
            }
        }

	    if (proc->has_srq) {
	        proc->srq_hndl[i] = create_srq(proc, i);
        }
    }
#ifdef RDMA_CM
  }
#endif /* RDMA_CM */

    rdma_default_port 	    = ports[0][0];
    /* step 2: create qps for all vc */
    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);

        vc->mrail.num_rails = rdma_num_rails;
        vc->mrail.rails = MPIU_Malloc
            (sizeof *vc->mrail.rails * vc->mrail.num_rails);

        if (!vc->mrail.rails) {
	        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		        "**fail", "**fail %s", "Failed to allocate resources for "
		        "multirails");
        }

        MPIU_Memset (vc->mrail.rails, 0, 
                    (sizeof *vc->mrail.rails * vc->mrail.num_rails));

	    vc->mrail.srp.credits = MPIU_Malloc
                        (sizeof *vc->mrail.srp.credits * vc->mrail.num_rails);
	    if (!vc->mrail.srp.credits) {
	        MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
		        "**fail", "**fail %s", "Failed to allocate resources for "
		        "credits array");
	    }
	    MPIU_Memset(vc->mrail.srp.credits, 0,
	            (sizeof *vc->mrail.srp.credits * vc->mrail.num_rails));

        if (!qp_required(vc, pg_rank, i)) {
            continue;
		}
#ifdef RDMA_CM
	if (proc->use_rdma_cm)
		continue;
#endif
	for (   rail_index = 0; 
        	rail_index < vc->mrail.num_rails;
        	rail_index++) 
	{
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
	    port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
	    MPIU_Memset(&attr, 0, sizeof attr);
	    attr.cap.max_send_wr = rdma_default_max_send_wqe;

        if (proc->has_srq) {
            attr.cap.max_recv_wr = 0;
            attr.srq = proc->srq_hndl[hca_index];
        } else {
            attr.cap.max_recv_wr = rdma_default_max_recv_wqe;
        }

	    attr.cap.max_send_sge = rdma_default_max_sg_list;
	    attr.cap.max_recv_sge = rdma_default_max_sg_list;
	    attr.cap.max_inline_data = rdma_max_inline_size;
	    attr.send_cq = proc->cq_hndl[hca_index];
	    attr.recv_cq = proc->cq_hndl[hca_index];
	    attr.qp_type = IBV_QPT_RC;
	    attr.sq_sig_all = 0;
          
	    vc->mrail.rails[rail_index].qp_hndl = 
	        ibv_create_qp(proc->ptag[hca_index], &attr);

	    if (!vc->mrail.rails[rail_index].qp_hndl) {
		    MPIU_ERR_SETFATALANDSTMT2(mpi_errno, MPI_ERR_OTHER, goto err_cq,
			    "**fail", "%s%d", "Failed to create qp for rank ", i);
	    }

	    vc->mrail.rails[rail_index].nic_context = proc->nic_context[hca_index];
	    vc->mrail.rails[rail_index].hca_index   = hca_index;
	    vc->mrail.rails[rail_index].port	    = ports[hca_index][port_index];
	    vc->mrail.rails[rail_index].lid         = lids[hca_index][port_index];
	    vc->mrail.rails[rail_index].gid         = gids[hca_index][port_index];
	    vc->mrail.rails[rail_index].cq_hndl     = proc->cq_hndl[hca_index];
	    vc->mrail.rails[rail_index].send_cq_hndl	= NULL;
	    vc->mrail.rails[rail_index].recv_cq_hndl	= NULL;

        if (info) {
            info->lid[i][rail_index] = lids[hca_index][port_index];
            info->gid[i][rail_index] = gids[hca_index][port_index];
            info->hca_type[i] = proc->hca_type;
            info->qp_num_rdma[i][rail_index] =
    	        vc->mrail.rails[rail_index].qp_hndl->qp_num;
            info->vc_addr[i] = (uintptr_t)vc;
            DEBUG_PRINT("[%d->%d from %d] vc %p\n", i, pg_rank, pg_rank, vc);
            DEBUG_PRINT("Setting hca type %d\n", info->hca_type[i]);
        }

	    qp_attr.qp_state        = IBV_QPS_INIT;
	    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

	    qp_attr.port_num = ports[hca_index][port_index];
            set_pkey_index(&qp_attr.pkey_index, hca_index,
                           qp_attr.port_num);

	    if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
		    MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto err_cq,
			    "**fail", "**fail %s", "Failed to modify QP to INIT");
        }
	}
    }

#ifdef RDMA_CM
    if (proc->use_rdma_cm)
    {
        if ((mpi_errno = ib_init_rdma_cm(proc, pg_rank, pg_size)) != MPI_SUCCESS)
        {
            MPIU_ERR_POP(mpi_errno);
        }
    }
#endif 

    DEBUG_PRINT("Return from init hca\n");

fn_exit:
    return mpi_errno;

err_cq:
    for (i = 0; i < rdma_num_hcas; ++i) {
        if (proc->cq_hndl[i])
            ibv_destroy_cq(proc->cq_hndl[i]);
    }

err:
    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->ptag[i])
            ibv_dealloc_pd(proc->ptag[i]);
    }

    for (i = 0; i < rdma_num_hcas; i ++) {
        if (proc->nic_context[i])
            ibv_close_device(proc->nic_context[i]);
    }

fn_fail:
    goto fn_exit;
}

/* Allocate memory and handlers */
/*
 * TODO add error handling
 */
int
rdma_iba_allocate_memory(struct MPIDI_CH3I_RDMA_Process_t *proc,
                         int pg_rank, int pg_size)
{
    int ret = 0;

    /* First allocate space for RDMA_FAST_PATH for every connection */
    /*
    for (; i < pg_size; ++i)
    {
        MPIDI_PG_Get_vc(g_cached_pg, i, &vc);

	vc->mrail.rfp.phead_RDMA_send = 0;
	vc->mrail.rfp.ptail_RDMA_send = 0;
	vc->mrail.rfp.p_RDMA_recv = 0;
	vc->mrail.rfp.p_RDMA_recv_tail = 0;
    } */

    MPIDI_CH3I_RDMA_Process.polling_group_size = 0;

    if (rdma_polling_set_limit > 0)
    {
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t**) MPIU_Malloc(rdma_polling_set_limit * sizeof(MPIDI_VC_t*));
    }
    else
    {
        MPIDI_CH3I_RDMA_Process.polling_set = (MPIDI_VC_t**) MPIU_Malloc(pg_size * sizeof(MPIDI_VC_t*));
    }
    
    if (!MPIDI_CH3I_RDMA_Process.polling_set)
    {
	fprintf(
            stderr,
            "[%s:%d]: %s\n",
            __FILE__,
            __LINE__,
            "unable to allocate space for polling set\n");
        return 0;
    }

    /* We need to allocate vbufs for send/recv path */
    if ((ret = allocate_vbufs(MPIDI_CH3I_RDMA_Process.ptag, rdma_vbuf_pool_size)))
    {
        return ret;
    }

#ifdef _ENABLE_UD_
    if (rdma_enable_hybrid) {
        if ((ret = allocate_ud_vbufs(rdma_ud_vbuf_pool_size))) {
            return ret;
        }
    }
#endif /* _ENABLE_UD_ */   
    /* Post the buffers for the SRQ */
    if (MPIDI_CH3I_RDMA_Process.has_srq)
    {
        int hca_num = 0;

        pthread_spin_init(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock, 0);
        pthread_spin_lock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);

        MPIDI_CH3I_RDMA_Process.is_finalizing = 0;

        for (; hca_num < rdma_num_hcas; ++hca_num)
        { 
            pthread_mutex_init(&MPIDI_CH3I_RDMA_Process.srq_post_mutex_lock[hca_num], 0);
            pthread_cond_init(&MPIDI_CH3I_RDMA_Process.srq_post_cond[hca_num], 0);
            MPIDI_CH3I_RDMA_Process.srq_zero_post_counter[hca_num] = 0;
            MPIDI_CH3I_RDMA_Process.posted_bufs[hca_num] = viadev_post_srq_buffers(viadev_srq_fill_size, hca_num);

            {
                struct ibv_srq_attr srq_attr;
                srq_attr.max_wr = viadev_srq_alloc_size;
                srq_attr.max_sge = 1;
                srq_attr.srq_limit = viadev_srq_limit;

                if (ibv_modify_srq(
                    MPIDI_CH3I_RDMA_Process.srq_hndl[hca_num], 
                    &srq_attr,
                    IBV_SRQ_LIMIT))
                {
                    ibv_error_abort(IBV_RETURN_ERR, "Couldn't modify SRQ limit\n");
                }

                /* Start the async thread which watches for SRQ limit events */
                {
                    pthread_attr_t attr;
                    int stacksz_ret;
                    if (pthread_attr_init(&attr))
                    {
                        ibv_error_abort(IBV_RETURN_ERR, "Couldn't init pthread_attr\n");
                    }
                    stacksz_ret = pthread_attr_setstacksize(&attr, 
                            rdma_default_async_thread_stack_size);
                    if (stacksz_ret && stacksz_ret != EINVAL) {
                        ibv_error_abort(IBV_RETURN_ERR, "Couldn't set pthread stack size\n");
                    }
                    pthread_create(
                            &MPIDI_CH3I_RDMA_Process.async_thread[hca_num], 
                            &attr,
                            (void *) async_thread, 
                            (void *) MPIDI_CH3I_RDMA_Process.nic_context[hca_num]);
                }
            }
        }

        pthread_spin_unlock(&MPIDI_CH3I_RDMA_Process.srq_post_spin_lock);
    }

	return ret;
}

/*
 * TODO add error handling 
 */

int
rdma_iba_enable_connections(struct MPIDI_CH3I_RDMA_Process_t *proc,
                            int pg_rank, MPIDI_PG_t *pg, 
                            struct process_init_info *info)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 i, j;
    int                 rail_index, pg_size;
    static int          rdma_qos_sl = 0;
    MPIDI_VC_t * vc;

    /**********************  INIT --> RTR  ************************/
    MPIU_Memset(&qp_attr, 0, sizeof qp_attr);
    qp_attr.qp_state    =   IBV_QPS_RTR;
    qp_attr.rq_psn      =   rdma_default_psn;
    qp_attr.max_dest_rd_atomic  =   rdma_default_max_rdma_dst_ops;
    qp_attr.min_rnr_timer       =   rdma_default_min_rnr_timer;
    if (rdma_use_qos) {
        qp_attr.ah_attr.sl          =   rdma_qos_sl;
        rdma_qos_sl = (rdma_qos_sl + 1) % rdma_qos_num_sls;
    } else {
        qp_attr.ah_attr.sl          =   rdma_default_service_level;
    }
    qp_attr.ah_attr.static_rate =   rdma_default_static_rate;
    qp_attr.ah_attr.src_path_bits   =   rdma_default_src_path_bits;

    if (use_iboeth) {
        qp_attr.ah_attr.grh.dgid.global.subnet_prefix = 0;
        qp_attr.ah_attr.grh.dgid.global.interface_id = 0;
        qp_attr.ah_attr.grh.flow_label = 0;
        qp_attr.ah_attr.grh.sgid_index = 0;
        qp_attr.ah_attr.grh.hop_limit = 1;
        qp_attr.ah_attr.grh.traffic_class = 0;
        qp_attr.ah_attr.is_global      = 1;
        qp_attr.ah_attr.dlid           = 0;
        qp_attr.path_mtu            = IBV_MTU_1024;
    } else {
        qp_attr.ah_attr.is_global   =   0;
        qp_attr.path_mtu    =   rdma_default_mtu;
    }

    qp_attr_mask        |=  IBV_QP_STATE; 
    qp_attr_mask        |=  IBV_QP_PATH_MTU;
    qp_attr_mask        |=  IBV_QP_RQ_PSN;
    qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
    qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
    qp_attr_mask        |=  IBV_QP_AV;

    pg_size = MPIDI_PG_Get_size(pg);
    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);
        if (!qp_required(vc, pg_rank, i)) {
            continue;
		}

        vc->mrail.remote_vc_addr = info->vc_addr[i];
        DEBUG_PRINT("[%d->%d] from %d received vc %08llx\n",
                pg_rank, i, pg_rank, info->vc_addr[i]);

	    for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
            qp_attr.dest_qp_num = info->qp_num_rdma[i][rail_index];
            qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;
            if (use_iboeth) {
                qp_attr.ah_attr.grh.dgid = info->gid[i][rail_index];
            } else {
                qp_attr.ah_attr.dlid = info->lid[i][rail_index];
            }

            /* If HSAM is enabled, include the source path bits and change
            * the destination LID accordingly */

            /* Both source and destination should have the same value of the
            * bits */

            if (MPIDI_CH3I_RDMA_Process.has_hsam) {
                qp_attr.ah_attr.src_path_bits = rail_index %
                                    power_two(MPIDI_CH3I_RDMA_Process.lmc);
                qp_attr.ah_attr.dlid = info->lid[i][rail_index]
                        + rail_index % power_two(MPIDI_CH3I_RDMA_Process.lmc);
            }
            qp_attr_mask    |=  IBV_QP_DEST_QPN;

            if (!use_iboeth && (rdma_3dtorus_support || rdma_path_sl_query)) {
                /* Path SL Lookup */
                int hca_index  = rail_index /
                                    (vc->mrail.num_rails / rdma_num_hcas);
                struct ibv_context *context =
                                     vc->mrail.rails[rail_index].nic_context;
                struct ibv_pd *pd  = proc->ptag[hca_index];
                uint16_t lid       = vc->mrail.rails[rail_index].lid;
                uint16_t rem_lid   = qp_attr.ah_attr.dlid;
                uint32_t port_num  = qp_attr.ah_attr.port_num;
                qp_attr.ah_attr.sl = mv2_get_path_rec_sl(context, pd, port_num,
                                            lid, rem_lid, rdma_3dtorus_support,
                                            rdma_num_sa_query_retries);
            }

            if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl,
                               &qp_attr, qp_attr_mask)) {
                fprintf(stderr, "[%s:%d] Could not modify qp" 
                        "to RTR\n",__FILE__, __LINE__); 
                return 1;
            }
        }
    }

    /************** RTR --> RTS *******************/
    MPIU_Memset(&qp_attr, 0, sizeof qp_attr);
    qp_attr.qp_state        = IBV_QPS_RTS;
    qp_attr.sq_psn          = rdma_default_psn;
    qp_attr.timeout         = rdma_default_time_out;
    qp_attr.retry_cnt       = rdma_default_retry_count;
    qp_attr.rnr_retry       = rdma_default_rnr_retry;
    qp_attr.max_rd_atomic   = rdma_default_qp_ous_rd_atom;

    qp_attr_mask = 0;
    qp_attr_mask =    IBV_QP_STATE              |
                      IBV_QP_TIMEOUT            |
                      IBV_QP_RETRY_CNT          |
                      IBV_QP_RNR_RETRY          |
                      IBV_QP_SQ_PSN             |
                      IBV_QP_MAX_QP_RD_ATOMIC;

    for (i = 0; i < pg_size; i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);

        if (!qp_required(vc, pg_rank, i)) {
            continue;
		}

        for(j = 0; j < rdma_num_rails - 1; j++) {
            vc->mrail.rails[j].s_weight =
                DYNAMIC_TOTAL_WEIGHT / rdma_num_rails;
        }
        
        vc->mrail.rails[rdma_num_rails - 1].s_weight =
            DYNAMIC_TOTAL_WEIGHT -
            (DYNAMIC_TOTAL_WEIGHT / rdma_num_rails) * 
            (rdma_num_rails - 1);

        for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
            if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                                qp_attr_mask)) {
                fprintf(stderr, "[%s:%d] Could not modify rdma qp to RTS\n",
                        __FILE__, __LINE__);
                return 1;
            }
            
            if(MPIDI_CH3I_RDMA_Process.has_apm) {
                reload_alternate_path(vc->mrail.rails[rail_index].qp_hndl);
            }
        }
    }

    DEBUG_PRINT("Done enabling connections\n");
    return 0;
}

void MRAILI_RC_Enable(MPIDI_VC_t *vc)
{
    int i,k;
    PRINT_DEBUG(DEBUG_UD_verbose>0, "Enabled to create RC Channel to rank:%d\n", vc->pg_rank);
    vc->mrail.state |= MRAILI_RC_CONNECTED;
    for (i = 0; i < vc->mrail.num_rails; i++) {
        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            for (k = 0; k < rdma_initial_prepost_depth; k++) {
                PREPOST_VBUF_RECV(vc, i);
            }    
        }
    }
#ifdef _ENABLE_UD_
    if (rdma_enable_hybrid) {
        vc->mrail.rfp.eager_start_cnt = 0;
        MPIDI_CH3I_RDMA_Process.rc_connections++;
        rdma_hybrid_pending_rc_conn--;
        MPIU_Assert(vc->mrail.state & MRAILI_RC_CONNECTING);
    }
#endif
}

void MRAILI_Init_vc(MPIDI_VC_t * vc)
{
    int pg_size;
    int i,k;

#ifdef _ENABLE_XRC_
    if (USE_XRC) {
        if (VC_XST_ISSET (vc, XF_INIT_DONE))
            return;
        else
            VC_XST_SET (vc, XF_INIT_DONE);
    }
    PRINT_DEBUG(DEBUG_XRC_verbose>0, "MRAILI_Init_vc %d\n", vc->pg_rank);
#endif
#ifdef _ENABLE_UD_
    if(vc->mrail.state & MRAILI_UD_CONNECTED) {
        MRAILI_RC_Enable(vc);
        return;
    }
#endif

    PMI_Get_size(&pg_size);

    vc->mrail.rfp.phead_RDMA_send = 0;
    vc->mrail.rfp.ptail_RDMA_send = 0;
    vc->mrail.rfp.p_RDMA_recv = 0;
    vc->mrail.rfp.p_RDMA_recv_tail = 0;
    vc->mrail.rfp.rdma_failed = 0;
    vc->mrail.num_rails = rdma_num_rails;
          
    if (!vc->mrail.rails) {
        vc->mrail.rails = MPIU_Malloc
            (sizeof *vc->mrail.rails * vc->mrail.num_rails);
        if (!vc->mrail.rails) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for multirails\n");
        }
        MPIU_Memset (vc->mrail.rails, 0, 
                (sizeof *vc->mrail.rails * vc->mrail.num_rails));
    }

    if (!vc->mrail.srp.credits) {
        vc->mrail.srp.credits = MPIU_Malloc(sizeof (*vc->mrail.srp.credits) * 
                vc->mrail.num_rails);
        if (!vc->mrail.srp.credits) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for credits array\n");
        }
        MPIU_Memset(vc->mrail.srp.credits, 0 ,
                (sizeof (*vc->mrail.srp.credits) * vc->mrail.num_rails));
    }

    /* Now we will need to */
    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_send_wqe;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
        vc->mrail.rails[i].ext_sendq_size     = 0;
        vc->mrail.rails[i].used_send_cq       = 0;
        vc->mrail.rails[i].used_recv_cq       = 0;
#ifdef _ENABLE_XRC_
        vc->mrail.rails[i].hca_index = i / (rdma_num_rails / rdma_num_hcas);
#endif
    }

    vc->mrail.outstanding_eager_vbufs = 0;
    vc->mrail.coalesce_vbuf = NULL;

    vc->mrail.rfp.rdma_credit = 0;
#ifndef MV2_DISABLE_HEADER_CACHING 
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    MPIU_Memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    MPIU_Memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

    vc->mrail.cmanager.num_channels         = vc->mrail.num_rails;
    vc->mrail.cmanager.num_local_pollings   = 0;

    if (pg_size < rdma_eager_limit && !MPIDI_CH3I_Process.has_dpm) { 
	    vc->mrail.rfp.eager_start_cnt = rdma_polling_set_threshold + 1;
    } else {
	    vc->mrail.rfp.eager_start_cnt = 0;
    }

    vc->mrail.rfp.in_polling_set = 0;

    /* extra one channel for later increase the adaptive rdma */
    vc->mrail.cmanager.msg_channels = MPIU_Malloc
        (sizeof *vc->mrail.cmanager.msg_channels 
         * (vc->mrail.cmanager.num_channels + 1));
    if (!vc->mrail.cmanager.msg_channels) {
        ibv_error_abort(GEN_EXIT_ERR, "No resource for msg channels\n");
    }
    MPIU_Memset(vc->mrail.cmanager.msg_channels, 0,
            sizeof *vc->mrail.cmanager.msg_channels
            * (vc->mrail.cmanager.num_channels + 1));

    vc->mrail.cmanager.next_arriving = NULL;
    vc->mrail.cmanager.inqueue	     = 0;
    vc->mrail.cmanager.vc	     = (void *)vc;

    DEBUG_PRINT("Cmanager total channel %d, local polling %d\n",
            vc->mrail.cmanager.num_channels, vc->mrail.cmanager.num_local_pollings);

    vc->mrail.sreq_head = NULL;
    vc->mrail.sreq_tail = NULL;
    vc->mrail.nextflow  = NULL;
    vc->mrail.inflow    = 0;

    for (i = 0; i < vc->mrail.num_rails; i++) {
        if (!MPIDI_CH3I_RDMA_Process.has_srq 
#ifdef _ENABLE_UD_
        && !rdma_enable_hybrid
#endif
        ) {
            for (k = 0; k < rdma_initial_prepost_depth; k++) {
                 PREPOST_VBUF_RECV(vc, i);
            } 
        }

        vc->mrail.srp.credits[i].remote_credit     = rdma_initial_credits;
        vc->mrail.srp.credits[i].remote_cc         = rdma_initial_credits;
        vc->mrail.srp.credits[i].local_credit      = 0;
        vc->mrail.srp.credits[i].preposts          = rdma_initial_prepost_depth;

        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            vc->mrail.srp.credits[i].initialized   =
                (rdma_prepost_depth == rdma_initial_prepost_depth);
        } else {
            vc->mrail.srp.credits[i].initialized = 1; 
            vc->mrail.srp.credits[i].pending_r3_sends = 0;
        }

        vc->mrail.srp.credits[i].backlog.len       = 0;
        vc->mrail.srp.credits[i].backlog.vbuf_head = NULL;
        vc->mrail.srp.credits[i].backlog.vbuf_tail = NULL;

        vc->mrail.srp.credits[i].rendezvous_packets_expected = 0;
    }

    for(i = 0; i < rdma_num_rails - 1; i++) {
        vc->mrail.rails[i].s_weight =
            DYNAMIC_TOTAL_WEIGHT / rdma_num_rails;
    }
    
    vc->mrail.rails[rdma_num_rails - 1].s_weight =
        DYNAMIC_TOTAL_WEIGHT -
        (DYNAMIC_TOTAL_WEIGHT / rdma_num_rails) * (rdma_num_rails - 1);
    
        vc->mrail.seqnum_next_tosend  = 0;
        vc->mrail.seqnum_next_torecv  = 0;
#ifdef _ENABLE_UD_
    if (rdma_enable_hybrid && ! (vc->mrail.state & MRAILI_UD_CONNECTED)) { 

        vc->mrail.ud.total_messages = 0;
        vc->mrail.ud.ack_pending = 0;

        vc->mrail.state &= ~(MRAILI_RC_CONNECTING | MRAILI_RC_CONNECTING);

        MESSAGE_QUEUE_INIT(&vc->mrail.ud.send_window); 
        MESSAGE_QUEUE_INIT(&vc->mrail.ud.ext_window);
        vc->mrail.ud.cntl_acks = 0;
        vc->mrail.ud.resend_count = 0;
        vc->mrail.ud.ext_win_send_count = 0;
    }
    else
#endif /* _ENABLE_UD_ */
    {
        vc->mrail.state |= MRAILI_RC_CONNECTED;
    }
}

#ifdef _ENABLE_XRC_
int cm_qp_reuse (MPIDI_VC_t *vc, MPIDI_VC_t *orig)
{
    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;
   
    VC_XST_SET (vc, XF_INDIRECT_CONN);
    vc->ch.orig_vc = orig;

    vc->mrail.num_rails = rdma_num_rails;
    if (!vc->mrail.rails) {
        vc->mrail.rails = MPIU_Malloc
            (sizeof *vc->mrail.rails * vc->mrail.num_rails);
        if (!vc->mrail.rails) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for multirails\n");
        }
        MPIU_Memset (vc->mrail.rails, 0, 
                    (sizeof *vc->mrail.rails * vc->mrail.num_rails));
    }

    if (!vc->mrail.srp.credits) {
        vc->mrail.srp.credits = MPIU_Malloc(sizeof *vc->mrail.srp.credits * 
                vc->mrail.num_rails);
        if (!vc->mrail.srp.credits) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for credits array\n");
        }
        MPIU_Memset(vc->mrail.srp.credits, 0 ,
                    (sizeof (*vc->mrail.srp.credits) * vc->mrail.num_rails));
    }

    for (rail_index = 0; rail_index < vc->mrail.num_rails;
    	    rail_index++) {
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
        port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
        
        vc->mrail.rails[rail_index].qp_hndl = 
            orig->mrail.rails[rail_index].qp_hndl;
        vc->mrail.rails[rail_index].nic_context = 
            MPIDI_CH3I_RDMA_Process.nic_context[hca_index];
        vc->mrail.rails[rail_index].hca_index   = hca_index;
        vc->mrail.rails[rail_index].port	= 
            MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        if (use_iboeth) {
            vc->mrail.rails[rail_index].gid     = 
                MPIDI_CH3I_RDMA_Process.gids[hca_index][port_index];
        } else {
            vc->mrail.rails[rail_index].lid     = 
                MPIDI_CH3I_RDMA_Process.lids[hca_index][port_index];
        }
        vc->mrail.rails[rail_index].cq_hndl	= 
            MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
	    vc->mrail.rails[rail_index].send_cq_hndl	= NULL;
	    vc->mrail.rails[rail_index].recv_cq_hndl	= NULL;
    }
    
    cm_send_xrc_cm_msg (vc, orig);
    return 0;
}
#endif /* _ENABLE_XRC_ */

static inline int cm_qp_conn_create(MPIDI_VC_t *vc, int qptype)
{
    struct ibv_qp_init_attr attr;
    struct ibv_qp_attr      qp_attr;

    int hca_index  = 0;
    int port_index = 0;
    int rail_index = 0;

    qp_attr.qp_state  = IBV_QPS_INIT;
    qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;


    vc->mrail.num_rails = rdma_num_rails;
#if defined _ENABLE_XRC_ || _ENABLE_UD_
    if (!vc->mrail.rails) {
#endif
        vc->mrail.rails = MPIU_Malloc
                (sizeof *vc->mrail.rails * vc->mrail.num_rails);

        if (!vc->mrail.rails) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for multirails\n");
        }
        MPIU_Memset (vc->mrail.rails, 0, 
                    (sizeof *vc->mrail.rails * vc->mrail.num_rails));
#if defined _ENABLE_XRC_ || _ENABLE_UD_
    }
#endif

#if defined _ENABLE_XRC_ || _ENABLE_UD_
    if (!vc->mrail.srp.credits) {
#endif
        vc->mrail.srp.credits = MPIU_Malloc(sizeof *vc->mrail.srp.credits * 
                vc->mrail.num_rails);
        if (!vc->mrail.srp.credits) {
            ibv_error_abort(GEN_EXIT_ERR, 
                    "Fail to allocate resources for credits array\n");
        }
        MPIU_Memset(vc->mrail.srp.credits, 0 ,
                    (sizeof (*vc->mrail.srp.credits) * vc->mrail.num_rails));
#if defined _ENABLE_XRC_ || _ENABLE_UD_
    }
#endif

    for (rail_index = 0; 
    	    rail_index < vc->mrail.num_rails;
    	    rail_index++) 
    {
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
        port_index = (rail_index / (vc->mrail.num_rails / (rdma_num_hcas *
                    rdma_num_ports))) % rdma_num_ports;
        MPIU_Memset(&attr, 0, sizeof attr);
        attr.cap.max_send_wr = rdma_default_max_send_wqe;
#ifdef _ENABLE_XRC_
        if (USE_XRC && qptype == MV2_QPT_XRC) 
        {
            attr.xrc_domain = MPIDI_CH3I_RDMA_Process.xrc_domain[hca_index];
            MPIU_Assert (attr.xrc_domain != NULL);
            attr.qp_type = IBV_QPT_XRC;
            attr.srq = NULL;
            attr.cap.max_recv_wr = 0;
        }
        else
#endif 
        {
            if (MPIDI_CH3I_RDMA_Process.has_srq) {
                attr.cap.max_recv_wr = 0;
                attr.srq = MPIDI_CH3I_RDMA_Process.srq_hndl[hca_index];
            } else {
                attr.cap.max_recv_wr = rdma_default_max_recv_wqe;
            }
            attr.qp_type = IBV_QPT_RC;
        }
        attr.cap.max_send_sge = rdma_default_max_sg_list;
        attr.cap.max_recv_sge = rdma_default_max_sg_list;
        attr.cap.max_inline_data = rdma_max_inline_size;
        attr.send_cq = MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
        attr.recv_cq = MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
        attr.sq_sig_all = 0;

        vc->mrail.rails[rail_index].qp_hndl = 
            ibv_create_qp(MPIDI_CH3I_RDMA_Process.ptag[hca_index], &attr);

        if (!vc->mrail.rails[rail_index].qp_hndl) {
            ibv_error_abort(GEN_EXIT_ERR, "Failed to create QP\n");
        }
        vc->mrail.rails[rail_index].nic_context = 
            MPIDI_CH3I_RDMA_Process.nic_context[hca_index];
        vc->mrail.rails[rail_index].hca_index   = hca_index;
        vc->mrail.rails[rail_index].port	= 
            MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        if (use_iboeth) {
            vc->mrail.rails[rail_index].gid     = 
                MPIDI_CH3I_RDMA_Process.gids[hca_index][port_index];
        } else {
            vc->mrail.rails[rail_index].lid     = 
                MPIDI_CH3I_RDMA_Process.lids[hca_index][port_index];
        }
        vc->mrail.rails[rail_index].cq_hndl	= 
            MPIDI_CH3I_RDMA_Process.cq_hndl[hca_index];
	    vc->mrail.rails[rail_index].send_cq_hndl	= NULL;
	    vc->mrail.rails[rail_index].recv_cq_hndl	= NULL;

        qp_attr.qp_state        = IBV_QPS_INIT;
        qp_attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
            IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;

        qp_attr.port_num = MPIDI_CH3I_RDMA_Process.ports[hca_index][port_index];
        set_pkey_index(&qp_attr.pkey_index, hca_index, qp_attr.port_num);

        if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    IBV_QP_STATE              |
                    IBV_QP_PKEY_INDEX         |
                    IBV_QP_PORT               |
                    IBV_QP_ACCESS_FLAGS)) {
                ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to INIT\n");
            }
    }

#ifdef _ENABLE_XRC_ 
    if (USE_XRC && qptype == MV2_QPT_XRC) {
        PRINT_DEBUG(DEBUG_XRC_verbose>0, "Added vc to XRC hash\n");
        add_vc_xrc_hash (vc);
    }
#endif /* _ENABLE_XRC_ */
    return 0;
}

/*function to create qps for the connection and move them to INIT state*/
int cm_qp_create(MPIDI_VC_t *vc, int force, int qptype)
{

#ifdef _ENABLE_XRC_
    int match = 0;
    int rail_index = 0;
    int hca_index = 0;
    int port_index = 0;

    PRINT_DEBUG(DEBUG_XRC_verbose>0, "Talking to %d (force:%d)\n", vc->pg_rank, force);
    if (USE_XRC && !force && qptype == MV2_QPT_XRC) {
        int hash;
        xrc_hash_t *iter;
        
        if (VC_XST_ISSET (vc, XF_REUSE_WAIT)) {
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "Already waiting for REUSE %d\n", vc->pg_rank);
            return MV2_QP_REUSE;
        }

        hash = compute_xrc_hash (vc->smp.hostid);
        iter = xrc_hash[hash];

        while (iter) {
            if (iter->vc->smp.hostid == vc->smp.hostid &&
                    VC_XST_ISUNSET (vc, XF_CONN_CLOSING)) {

                /* Check if processes use same HCA */
                for (rail_index = 0; rail_index < vc->mrail.num_rails;
                      rail_index++) {
                    hca_index  = rail_index /
                                  (vc->mrail.num_rails / rdma_num_hcas);
                    port_index = (rail_index / (vc->mrail.num_rails /
                           (rdma_num_hcas * rdma_num_ports))) % rdma_num_ports;

                    PRINT_DEBUG(DEBUG_XRC_verbose>0, "rail_index = %d, old lid = %d, new lid = %d\n",
                           rail_index,
                           iter->vc->mrail.lid[hca_index][port_index],
                           vc->mrail.lid[hca_index][port_index]);
                    if ((vc->mrail.lid[hca_index][port_index] > 0) && 
                        (vc->mrail.lid[hca_index][port_index] ==
                         iter->vc->mrail.lid[hca_index][port_index])) {
                        /* LID is valid and there is a match
                         * i.e Both VC's use same HCA, so we can re-use QP
                         */
                        match = 1;
                        break;
                    } else if (memcmp(&vc->mrail.gid[hca_index][port_index],
                                      &iter->vc->mrail.gid[hca_index][port_index],
                                      sizeof(union ibv_gid))) {
                        /* We're using RoCE mode. Check for GID's instead of
                         * LID's. As above if GID's match we can re-use QP
                         */
                        match = 1;
                        break;
                    }
                }

                if (!match) {
                    /* Cannot re-use QP. Need to create a new one */
                    PRINT_DEBUG(DEBUG_XRC_verbose>0, "Cannot reuse QP to talk to %d\n", vc->pg_rank);
                    break;
                }

                PRINT_DEBUG(DEBUG_XRC_verbose>0, "Talking to %d Reusing conn to %d XST: 0x%08x\n", 
                      vc->pg_rank, iter->vc->pg_rank, iter->vc->ch.xrc_flags);
                MPIU_Assert (vc->smp.hostid != -1);
                if (VC_XST_ISSET (iter->vc, XF_SEND_CONNECTING)) {
                    xrc_pending_conn_t *n;
                    VC_XST_SET (vc, XF_REUSE_WAIT);

                    n = (xrc_pending_conn_t *) MPIU_Malloc (xrc_pending_conn_s);
                    n->vc = vc;
                    n->next = iter->vc->ch.xrc_conn_queue;
                    iter->vc->ch.xrc_conn_queue = n;
                    PRINT_DEBUG(DEBUG_XRC_verbose>0, "Added %d to pending queue of %d \n", vc->pg_rank,
                            iter->vc->pg_rank);
                }
                else {
                    cm_qp_reuse (vc, iter->vc);
                }
                return MV2_QP_REUSE;
            }
        }
        PRINT_DEBUG(DEBUG_XRC_verbose>0, "Not FOUND!\n");
    }
#endif /* _ENABLE_XRC_ */
    /* XRC not in use or no qps found */
    cm_qp_conn_create (vc, qptype);
    return MV2_QP_NEW;
}


/*function to move qps to rtr and prepost buffers*/
int cm_qp_move_to_rtr(MPIDI_VC_t *vc, uint16_t *lids, union ibv_gid *gids,
                         uint32_t *qpns, 
        int is_rqp, uint32_t *rqpn, int is_dpm)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 rail_index, hca_index;
    static int          rdma_qos_sl = 0;

    for (rail_index = 0; rail_index < rdma_num_rails; rail_index ++) {
        hca_index  = rail_index / (vc->mrail.num_rails / rdma_num_hcas);
        MPIU_Memset(&qp_attr, 0, sizeof qp_attr);
        qp_attr.qp_state    =   IBV_QPS_RTR;
        qp_attr.rq_psn      =   rdma_default_psn;
        qp_attr.max_dest_rd_atomic  =   rdma_default_max_rdma_dst_ops;
        qp_attr.min_rnr_timer       =   rdma_default_min_rnr_timer;
        if (rdma_use_qos) {
            qp_attr.ah_attr.sl          =   rdma_qos_sl;
            rdma_qos_sl = (rdma_qos_sl + 1) % rdma_qos_num_sls;
        } else {
            qp_attr.ah_attr.sl          =   rdma_default_service_level;
        }
        qp_attr.ah_attr.static_rate =   rdma_default_static_rate;
        qp_attr.ah_attr.src_path_bits   =   rdma_default_src_path_bits;
        qp_attr.ah_attr.port_num = vc->mrail.rails[rail_index].port;

        if (use_iboeth) {
            qp_attr.ah_attr.grh.dgid.global.subnet_prefix = 0;
            qp_attr.ah_attr.grh.dgid.global.interface_id = 0;
            qp_attr.ah_attr.grh.flow_label = 0;
            qp_attr.ah_attr.grh.sgid_index = 0;
            qp_attr.ah_attr.grh.hop_limit = 1;
            qp_attr.ah_attr.grh.traffic_class = 0;
            qp_attr.ah_attr.is_global      = 1;
            qp_attr.ah_attr.dlid           = 0;
            qp_attr.path_mtu            = IBV_MTU_1024;
            qp_attr.ah_attr.grh.dgid    = gids[rail_index];
        } else {
            qp_attr.ah_attr.dlid = lids[rail_index];
            qp_attr.ah_attr.is_global   =   0;
            qp_attr.path_mtu    =   rdma_default_mtu;
        }

#ifdef _ENABLE_XRC_
        if (USE_XRC && !is_rqp && !is_dpm) {
            /* Move send qp to RTR */
            qp_attr.dest_qp_num = vc->ch.xrc_rqpn[rail_index];
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "%d dlid: %d\n", vc->ch.xrc_rqpn[rail_index], 
                    lids[rail_index]);
        }
        else /* Move rcv qp to RTR, or no XRC */
#endif
        {
            qp_attr.dest_qp_num = qpns[rail_index];
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "DQPN: %d dlid: %d\n", rail_index[qpns], lids[rail_index]);
        }
    
        if (MPIDI_CH3I_RDMA_Process.has_hsam) {
            qp_attr.ah_attr.src_path_bits = rdma_default_src_path_bits
                + rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
            qp_attr.ah_attr.dlid = lids[rail_index] + rail_index %
                power_two(MPIDI_CH3I_RDMA_Process.lmc);
        } 
    
        qp_attr_mask        |=  IBV_QP_STATE; 
        qp_attr_mask        |=  IBV_QP_PATH_MTU;
        qp_attr_mask        |=  IBV_QP_RQ_PSN;
        qp_attr_mask        |=  IBV_QP_MAX_DEST_RD_ATOMIC;
        qp_attr_mask        |=  IBV_QP_MIN_RNR_TIMER;
        qp_attr_mask        |=  IBV_QP_AV;
        qp_attr_mask        |=  IBV_QP_DEST_QPN;

        if (!use_iboeth && (rdma_3dtorus_support || rdma_path_sl_query)) {
            /* Path SL Lookup */
            struct ibv_context *context =
                                 vc->mrail.rails[rail_index].nic_context;
            struct ibv_pd *pd  = MPIDI_CH3I_RDMA_Process.ptag[hca_index];
            uint16_t lid       = vc->mrail.rails[rail_index].lid;
            uint16_t rem_lid   = qp_attr.ah_attr.dlid;
            uint32_t port_num  = qp_attr.ah_attr.port_num;
            qp_attr.ah_attr.sl = mv2_get_path_rec_sl(context, pd, port_num, lid,
                                                 rem_lid, rdma_3dtorus_support,
                                                 rdma_num_sa_query_retries);
        }

       /* fprintf(stderr, "!!!Modify qp %d with qpnum %08x, dlid %x, port %d\n", 
                rail_index, qp_attr.dest_qp_num, qp_attr.ah_attr.dlid,
                qp_attr.ah_attr.port_num); */
#ifdef _ENABLE_XRC_
        if (USE_XRC && is_rqp) {
            /* Move rcv qp to RTR */
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "%d <-> %d\n", 
                    rqpn[rail_index], qp_attr.dest_qp_num);
            if (ibv_modify_xrc_rcv_qp (
                    MPIDI_CH3I_RDMA_Process.xrc_domain[hca_index],
                    rqpn[rail_index], &qp_attr, qp_attr_mask)) {
                ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to RTR\n");
            }
        }
        else /* Move send qp to RTR */
#endif 
        {
            PRINT_DEBUG(DEBUG_XRC_verbose>0, "dpqn %d\n", 
                    qp_attr.dest_qp_num);
            if (ibv_modify_qp( vc->mrail.rails[rail_index].qp_hndl,
                               &qp_attr, qp_attr_mask)) {
                ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to RTR\n");
            }
        }
    }

    return 0;
}

/*function to move qps to rts and mark the connection available*/
int cm_qp_move_to_rts(MPIDI_VC_t *vc)
{
    struct ibv_qp_attr  qp_attr;
    uint32_t            qp_attr_mask = 0;
    int                 rail_index;

    for (rail_index = 0; rail_index < rdma_num_rails; rail_index++) {
      MPIU_Memset(&qp_attr, 0, sizeof qp_attr);
        qp_attr.qp_state        = IBV_QPS_RTS;
        qp_attr.sq_psn          = rdma_default_psn;
        qp_attr.timeout         = rdma_default_time_out;
        qp_attr.retry_cnt       = rdma_default_retry_count;
        qp_attr.rnr_retry       = rdma_default_rnr_retry;
        qp_attr.max_rd_atomic   = rdma_default_qp_ous_rd_atom;
        
        qp_attr_mask = 0;
        qp_attr_mask =    IBV_QP_STATE              |
                          IBV_QP_TIMEOUT            |
                          IBV_QP_RETRY_CNT          |
                          IBV_QP_RNR_RETRY          |
                          IBV_QP_SQ_PSN             |
                          IBV_QP_MAX_QP_RD_ATOMIC;
        PRINT_DEBUG(DEBUG_XRC_verbose>0, "RTS %d\n", vc->pg_rank);
        if (ibv_modify_qp(vc->mrail.rails[rail_index].qp_hndl, &qp_attr,
                    qp_attr_mask)) {
            ibv_error_abort(GEN_EXIT_ERR, "Failed to modify QP to RTS\n");
        }
        
        if(MPIDI_CH3I_RDMA_Process.has_apm) {
            reload_alternate_path(vc->mrail.rails[rail_index].qp_hndl);
        }

    }
    return 0;
}

int get_pkey_index(uint16_t pkey, int hca_num, int port_num, uint16_t* ix)
{
    uint16_t i = 0;
    struct ibv_device_attr dev_attr;

    if(ibv_query_device(
                MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                &dev_attr)) {

        ibv_error_abort(GEN_EXIT_ERR,
                "Error getting HCA attributes\n");
    }

    for (; i < dev_attr.max_pkeys ; ++i) {
        uint16_t curr_pkey;
        ibv_query_pkey(MPIDI_CH3I_RDMA_Process.nic_context[hca_num], 
                (uint8_t)port_num, (int)i ,&curr_pkey);
        if (pkey == (ntohs(curr_pkey) & PKEY_MASK)) {
            *ix = i;
            return 1;
        }
    }

    return 0;
}

void set_pkey_index(uint16_t * pkey_index, int hca_num, int port_num)
{
    if (rdma_default_pkey == RDMA_DEFAULT_PKEY)
    {
        *pkey_index = rdma_default_pkey_ix;
    }
    else if (!get_pkey_index(
        rdma_default_pkey,
        hca_num,
        port_num,
        pkey_index)
    )
    {
        ibv_error_abort(
            GEN_EXIT_ERR,
            "Can't find PKEY INDEX according to given PKEY\n"
        );
    }
}

#ifdef CKPT
void MRAILI_Init_vc_network(MPIDI_VC_t * vc)
{
    /*Reinitialize VC after channel reactivation
     *      * No change in cmanager, no change in rndv, not support rdma
     *      fast path*/
    int i;

#ifndef MV2_DISABLE_HEADER_CACHING 
    vc->mrail.rfp.cached_miss   = 0;
    vc->mrail.rfp.cached_hit    = 0;
    vc->mrail.rfp.cached_incoming = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    vc->mrail.rfp.cached_outgoing = MPIU_Malloc (sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_outgoing, 0, sizeof(MPIDI_CH3_Pkt_send_t));
    memset(vc->mrail.rfp.cached_incoming, 0, sizeof(MPIDI_CH3_Pkt_send_t));
#endif

    for (i = 0; i < rdma_num_rails; i++) {
        vc->mrail.rails[i].send_wqes_avail    = rdma_default_max_send_wqe;
        vc->mrail.rails[i].ext_sendq_head     = NULL;
        vc->mrail.rails[i].ext_sendq_tail     = NULL;
    }
    for (i = 0; i < vc->mrail.num_rails; i++) {
        int k;
        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            for (k = 0; k < rdma_initial_prepost_depth; k++) {
                PREPOST_VBUF_RECV(vc, i);
            }
        }
        vc->mrail.srp.credits[i].remote_credit     = rdma_initial_credits;
        vc->mrail.srp.credits[i].remote_cc         = rdma_initial_credits;
        vc->mrail.srp.credits[i].local_credit      = 0;
        vc->mrail.srp.credits[i].preposts          = rdma_initial_prepost_depth;

        if (!MPIDI_CH3I_RDMA_Process.has_srq) {
            vc->mrail.srp.credits[i].initialized       =
                (rdma_prepost_depth == rdma_initial_prepost_depth);
        } else {
            vc->mrail.srp.credits[i].initialized = 1;
            vc->mrail.srp.credits[i].pending_r3_sends = 0;
        }
        vc->mrail.srp.credits[i].backlog.len       = 0;
        vc->mrail.srp.credits[i].backlog.vbuf_head = NULL;
        vc->mrail.srp.credits[i].backlog.vbuf_tail = NULL;
        vc->mrail.srp.credits[i].rendezvous_packets_expected = 0;
    }
}

#endif

#ifdef _ENABLE_UD_
int rdma_init_ud(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int mpi_errno = MPI_SUCCESS;
    int hca_index;
    mv2_ud_ctx_t *ud_ctx;
    mv2_ud_qp_info_t qp_info;
    char *val;


    for (hca_index=0; hca_index< rdma_num_hcas; hca_index++)
    {
        qp_info.send_cq = qp_info.recv_cq = proc->cq_hndl[hca_index];
        qp_info.sq_psn = rdma_default_psn;
        qp_info.pd = proc->ptag[hca_index];
        qp_info.cap.max_send_sge = rdma_default_max_sg_list;
        qp_info.cap.max_recv_sge = rdma_default_max_sg_list;
        qp_info.cap.max_send_wr = rdma_default_max_ud_send_wqe;
        qp_info.cap.max_recv_wr = rdma_default_max_ud_recv_wqe;
        qp_info.srq = NULL;
        if((val = getenv("MV2_USE_UD_SRQ"))!=NULL && atoi(val))
        {
            qp_info.srq = create_srq(proc, hca_index);
        }
        qp_info.cap.max_inline_data = rdma_max_inline_size;
        ud_ctx = mv2_ud_create_ctx (&qp_info);
        if (!ud_ctx)
        {
            fprintf(stderr,"Error in create UD qp\n");
            return MPI_ERR_INTERN;
        }

        ud_ctx->send_wqes_avail = rdma_default_max_ud_send_wqe - 50;
        MESSAGE_QUEUE_INIT(&ud_ctx->ext_send_queue);
        ud_ctx->hca_num = hca_index;
        ud_ctx->num_recvs_posted = 0;
        ud_ctx->credit_preserve = (rdma_default_max_ud_recv_wqe / 4 );
        ud_ctx->num_recvs_posted += mv2_post_ud_recv_buffers( 
                (rdma_default_max_ud_recv_wqe - ud_ctx->num_recvs_posted), ud_ctx);

        proc->ud_rails[hca_index] = ud_ctx;
        proc->rc_connections = 0;
        ud_ctx->ext_sendq_count = 0;
    }
    MESSAGE_QUEUE_INIT(&proc->unack_queue);
    PRINT_DEBUG(DEBUG_UD_verbose>0,"Finish setting up UD queue pairs\n");
    return mpi_errno;
}

int mv2_ud_get_remote_info(MPIDI_PG_t *pg, int pg_rank, int pg_size)
{
    int error, mpi_errno = MPI_SUCCESS, i;
    char *key, *val;
    int key_max_sz, val_max_sz;
    mv2_ud_exch_info_t my_info, *all_info;

    all_info = (mv2_ud_exch_info_t *) MPIU_Malloc(pg_size * sizeof(mv2_ud_exch_info_t));

    my_info.lid = MPIDI_CH3I_RDMA_Process.lids[0][0];
    my_info.qpn = MPIDI_CH3I_RDMA_Process.ud_rails[0]->qp->qp_num;

    error = PMI_KVS_Get_key_length_max(&key_max_sz);
    if(error != PMI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "Error getting max key length");
    }

    ++key_max_sz;
    key = MPIU_Malloc(key_max_sz);
    if (key == NULL) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
                "**nomem %s", "PMI key");
    }

    error = PMI_KVS_Get_value_length_max(&val_max_sz);
    if(error != PMI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                "**fail %s", "Error getting max value length");
    }

    ++val_max_sz;
    val = MPIU_Malloc(val_max_sz);
    if (val == NULL) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno,MPI_ERR_OTHER,"**nomem",
                "**nomem %s", "PMI value");
    }

    if (key_max_sz < 20 || val_max_sz < 30) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**fail", "**fail %s", "PMI value too small");
    }

    /*Just put lid for default port and ud_qpn is sufficient*/
    MPIU_Snprintf(key, key_max_sz, "ud_%08d", pg_rank);

    MPIU_Snprintf(val, val_max_sz, "%08hx:%08x",my_info.lid, my_info.qpn);
    PRINT_DEBUG(DEBUG_UD_verbose>0,"rank:%d Put lids: %d ud_qp: %d\n",pg_rank, my_info.lid, my_info.qpn);

    error = PMI_KVS_Put(pg->ch.kvs_name, key, val);
    if (error != PMI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**pmi_kvs_put", "**pmi_kvs_put %d", error);
    }

    error = PMI_KVS_Commit(pg->ch.kvs_name);
    if (error != PMI_SUCCESS) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**pmi_kvs_commit", "**pmi_kvs_commit %d", error);
    }

    error = PMI_Barrier();
    if (error != PMI_SUCCESS)
    {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                "**pmi_barrier", "**pmi_barrier %d", error);
    }

    for (i = 0; i < pg_size; i++) {
        if (pg_rank == i) {
            continue;
        }

        MPIU_Snprintf(key, key_max_sz, "ud_%08d", i);

        error = PMI_KVS_Get(pg->ch.kvs_name, key, val, val_max_sz);
        if (error != PMI_SUCCESS) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                    "**pmi_kvs_get", "**pmi_kvs_get %d", error);
        }

        sscanf(val,"%08hx:%08x",
                    &(all_info[i].lid), &(all_info[i].qpn));
        PRINT_DEBUG(DEBUG_UD_verbose>0,"rank:%d Get lid:%d ud_qpn:%d\n",i,all_info[i].lid,
                all_info[i].qpn);
    }
    MPIDI_CH3I_RDMA_Process.remote_ud_info = all_info;
    MPIU_Free(key); 
    MPIU_Free(val);

fn_exit:
    return mpi_errno;
fn_fail:
    goto fn_exit;

}

int MPIDI_CH3I_UD_Generate_addr_handles(MPIDI_PG_t *pg, int pg_rank, int pg_size)
{
    int i;
    MPIDI_VC_t *vc = NULL;


    for (i=0; i< pg_size; i++)
    {
        MPIDI_PG_Get_vc(pg, i, &vc);
        if (i == pg_rank ) {
            continue;
        }

        mv2_ud_set_vc_info(&vc->mrail.ud, &MPIDI_CH3I_RDMA_Process.remote_ud_info[i],
                MPIDI_CH3I_RDMA_Process.ptag[0], rdma_default_port);

        /* Change vc state to avoid UD CM connection establishment */
        MRAILI_Init_vc(vc);
        vc->ch.state = MPIDI_CH3I_VC_STATE_IDLE;
#ifdef _ENABLE_XRC_
        VC_XST_SET (vc, XF_SEND_IDLE);
#endif
        vc->mrail.state |= MRAILI_UD_CONNECTED;

    }
    PRINT_DEBUG(DEBUG_UD_verbose>0,"Created UD Address handles \n");
    return MPI_SUCCESS;

}

int mv2_ud_setup_zcopy_rndv(struct MPIDI_CH3I_RDMA_Process_t *proc)
{
    int i, hca_index;
    mv2_ud_zcopy_info_t *zcopy_info  = &proc->zcopy_info;
    mv2_ud_qp_info_t qp_info;
    mv2_ud_ctx_t *ud_ctx;

    zcopy_info->rndv_qp_pool = (mv2_rndv_qp_t *)
                MPIU_Malloc(sizeof(mv2_rndv_qp_t) *rdma_ud_num_rndv_qps);
    zcopy_info->rndv_ud_cqs = (struct ibv_cq **) 
                MPIU_Malloc(sizeof(struct ibv_cq *) * rdma_num_hcas);
    zcopy_info->rndv_ud_qps = (mv2_ud_ctx_t **)
                MPIU_Malloc(sizeof(mv2_ud_ctx_t *) *rdma_num_hcas);

    for (i=0; i< rdma_ud_num_rndv_qps; i++) {
        hca_index = i % rdma_num_hcas;

        /* creating 256 extra cq entries than max possible recvs for safer side */
        zcopy_info->rndv_qp_pool[i].ud_cq = 
                ibv_create_cq(proc->nic_context[hca_index], (rdma_ud_zcopy_rq_size + 256), NULL, NULL, 0);
        if (!zcopy_info->rndv_qp_pool[i].ud_cq) {
            fprintf(stderr, "Error in creating ZCOPY Rndv CQ\n");
            return MPI_ERR_INTERN;
        }

        qp_info.send_cq = qp_info.recv_cq = zcopy_info->rndv_qp_pool[i].ud_cq;
        qp_info.sq_psn = rdma_default_psn;
        qp_info.pd = proc->ptag[hca_index];
        qp_info.cap.max_send_sge = 1;
        qp_info.cap.max_recv_sge = 2;
        qp_info.cap.max_send_wr = 1;
        qp_info.cap.max_recv_wr = rdma_ud_zcopy_rq_size;;
        qp_info.srq = NULL;
        qp_info.cap.max_inline_data = 0;
        zcopy_info->rndv_qp_pool[i].ud_qp = mv2_ud_create_qp(&qp_info);
        if(!zcopy_info->rndv_qp_pool[i].ud_qp) {
            fprintf(stderr, "Error in creating ZCOPY Rndv QP\n");
            return MPI_ERR_INTERN;
        }
    
        zcopy_info->rndv_qp_pool[i].seqnum = 0;
        zcopy_info->rndv_qp_pool[i].next = zcopy_info->rndv_qp_pool[i].prev = NULL;
        zcopy_info->rndv_qp_pool[i].hca_num = hca_index;
    }

    for (i=0; i< rdma_ud_num_rndv_qps-1 ; i++) {
        zcopy_info->rndv_qp_pool[i].next = &zcopy_info->rndv_qp_pool[i+1];
    }
    zcopy_info->rndv_qp_pool_free_head = &zcopy_info->rndv_qp_pool[0];

    /* allocate and register GRH buffer */
    zcopy_info->grh_buf = MPIU_Malloc(MV2_UD_GRH_LEN);
    zcopy_info->grh_mr = (void *)dreg_register(zcopy_info->grh_buf, MV2_UD_GRH_LEN);
    zcopy_info->no_free_rndv_qp = 0;

    /* Setup QP for sending zcopy rndv messages */
    for (hca_index=0; hca_index<rdma_num_hcas; hca_index++) {
        zcopy_info->rndv_ud_cqs[hca_index] = 
                ibv_create_cq(proc->nic_context[hca_index], 16384, NULL, NULL, 0);
        if (!zcopy_info->rndv_ud_cqs[hca_index]) {
            fprintf(stderr, "Error in creating ZCOPY Rndv CQ\n");
            return MPI_ERR_INTERN;
        }   
    }

    for (hca_index=0; hca_index<rdma_num_hcas; hca_index++) {
        qp_info.send_cq = qp_info.recv_cq = zcopy_info->rndv_ud_cqs[hca_index];
        qp_info.sq_psn = rdma_default_psn;
        qp_info.pd = proc->ptag[hca_index];
        qp_info.cap.max_send_sge = rdma_default_max_sg_list;
        qp_info.cap.max_recv_sge = rdma_default_max_sg_list;
        qp_info.cap.max_send_wr = rdma_default_max_ud_send_wqe;
        qp_info.cap.max_recv_wr = 1;
        qp_info.srq = NULL;
        qp_info.cap.max_inline_data = 0;
        ud_ctx = mv2_ud_create_ctx (&qp_info);
        if (!ud_ctx)
        {
            fprintf(stderr,"Error in create UD qp\n");
            return MPI_ERR_INTERN;
        }

        ud_ctx->send_wqes_avail = rdma_default_max_ud_send_wqe - 50;
        MESSAGE_QUEUE_INIT(&ud_ctx->ext_send_queue);
        ud_ctx->hca_num = hca_index;
        ud_ctx->num_recvs_posted = 0;
        ud_ctx->credit_preserve = (rdma_default_max_ud_recv_wqe / 4 );
        ud_ctx->num_recvs_posted = 0;
        zcopy_info->rndv_ud_qps[hca_index] = ud_ctx;
    }

    PRINT_DEBUG(DEBUG_ZCY_verbose>2, "ZCOPY Rndv setup done num rndv qps:%d\n", rdma_ud_num_rndv_qps);
    return MPI_SUCCESS;
}
void MPIDI_CH3I_UD_Stats(MPIDI_PG_t *pg)
{

    int i;
    mv2_ud_ctx_t *ud_ctx;
    mv2_ud_vc_info_t *ud_vc;
    MPIDI_VC_t *vc;

    int pg_size = MPIDI_PG_Get_size(pg);
    int pg_rank = MPIDI_Process.my_pg_rank;

    if(pg_rank !=0 && DEBUG_UDSTAT_verbose < 3) {
        return;
    }

    PRINT_INFO(DEBUG_UDSTAT_verbose > 0,"RC conns: %u pending:%d "
        "zcopy_fallback_count:%u\n", MPIDI_CH3I_RDMA_Process.rc_connections, 
        rdma_hybrid_pending_rc_conn, 
        MPIDI_CH3I_RDMA_Process.zcopy_info.no_free_rndv_qp);

    for (i = 0; i < rdma_num_hcas; i++) {
        ud_ctx = MPIDI_CH3I_RDMA_Process.ud_rails[i];
        if (ud_ctx->ext_sendq_count) {
            PRINT_INFO(DEBUG_UDSTAT_verbose > 0,"rail:%d "
            " ext send queue sends: %lu\n", i, ud_ctx->ext_sendq_count);
        }
    }
    for (i = 0; i < pg_size; i++)
    {
        MPIDI_PG_Get_vc(pg, i, &vc);
        if (rdma_use_smp && (vc->smp.local_rank != -1))  continue;
        ud_vc = &vc->mrail.ud;
        if (ud_vc->resend_count) {
            PRINT_INFO(DEBUG_UDSTAT_verbose > 1,"\t[-> %d]: resends:%lu "
                "cntl msg:%lu extwin_msgs:%lu tot_ud_msgs:%llu\n", 
                vc->pg_rank,ud_vc->resend_count, ud_vc->cntl_acks,
                ud_vc->ext_win_send_count, ud_vc->total_messages);
        }
    }
}
#endif /* _ENABLE_UD_ */

/* vi:set sw=4 */
