/*! \file */
/*
 *  (C) 2006 by Argonne National Laboratory.
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

#include <infiniband/verbs.h>
#include <infiniband/umad.h>

#include "mpidimpl.h"
#include "mpidbg.h"
#include "pmi.h"

#include "ib_errors.h"
#include "ib_process.h"

#include "ib_hca.h"


/**
 * Define ENABLE_HCA_REPORT in order to print a report on the local HCAs status.
 * The function <code>MPID_nem_ib_hca_check</code> prints the report.
 *
 * #define ENABLE_HCA_REPORT
*/
#undef ENABLE_HCA_REPORT

/**
 *  Number of HCSs.
 *  (Was rdma_num_hcas).
 */
int ib_hca_num_hcas = 1;

/**
 *  Number of ports.
 *  (Was rdma_num_ports).
 */
int ib_hca_num_ports = 1;

/**
 * The list of the HCAs found in the system.
 */
MPID_nem_ib_nem_hca hca_list[MAX_NUM_HCAS];


/**
 * Check the ibv_port_attr and ibv_device_attr.
 */
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
    	MPIU_Error_printf( "Active MTU is %d, MV2_DEFAULT_MTU set to %d. See User Guide\n",
                port_attr->active_mtu, rdma_default_mtu);
        ret = 1;
    }

    if(dev_attr->max_qp_rd_atom < rdma_default_qp_ous_rd_atom) {
    	MPIU_Error_printf( "Max MV2_DEFAULT_QP_OUS_RD_ATOM is %d, set to %d\n",
                dev_attr->max_qp_rd_atom, rdma_default_qp_ous_rd_atom);
        ret = 1;
    }

    if(process_info.has_srq) {
        if(dev_attr->max_srq_sge < rdma_default_max_sg_list) {
        	MPIU_Error_printf( "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_srq_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_srq_wr < viadev_srq_alloc_size) {
        	MPIU_Error_printf( "Max MV2_SRQ_SIZE is %d, set to %d\n",
                    dev_attr->max_srq_wr, (int) viadev_srq_alloc_size);
            ret = 1;
        }
    } else {
        if(dev_attr->max_sge < rdma_default_max_sg_list) {
        	MPIU_Error_printf( "Max MV2_DEFAULT_MAX_SG_LIST is %d, set to %d\n",
                    dev_attr->max_sge, rdma_default_max_sg_list);
            ret = 1;
        }

        if(dev_attr->max_qp_wr < rdma_default_max_send_wqe) {
        	MPIU_Error_printf( "Max MV2_DEFAULT_MAX_SEND_WQE is %d, set to %d\n",
                    dev_attr->max_qp_wr, (int) rdma_default_max_send_wqe);
            ret = 1;
        }
    }
    if(dev_attr->max_cqe < rdma_default_max_cq_size) {
    	MPIU_Error_printf( "Max MV2_DEFAULT_MAX_CQ_SIZE is %d, set to %d\n",
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
static int rdma_find_active_port(struct ibv_context *context,struct ibv_device *ib_dev)
{
    int j = 0;
    const char *dev_name = NULL;
    struct ibv_port_attr port_attr;

    if (NULL == ib_dev) {
        return -1;
    } else {
        dev_name = ibv_get_device_name(ib_dev);
    }

    for (j = 1; j <= RDMA_DEFAULT_MAX_PORTS; ++ j) {
        if ((! ibv_query_port(context, j, &port_attr)) &&
             port_attr.state == IBV_PORT_ACTIVE) {
            if (!strncmp(dev_name, "cxgb3", 5) || !strncmp(dev_name, "cxgb4", 5)
                || port_attr.lid) {
                /* Chelsio RNIC's don't get LID's as they're not IB devices.
                 * So dont do this check for them.
                 */
                DEBUG_PRINT("Active port number = %d, state = %s, lid = %d\r\n",
                    j, (port_attr.state==IBV_PORT_ACTIVE)?"Active":"Not Active",
                    port_attr.lid);
                return j;
            }
        }
    }

    return -1;
}

static int get_rate(umad_ca_t *umad_ca)
{
    int i;

    for (i = 1; i <= umad_ca->numports; i++) {
        if (IBV_PORT_ACTIVE == umad_ca->ports[i]->state) {
            return umad_ca->ports[i]->rate;
        }
    }
    return 0;
}


#undef FUNCNAME
#define FUNCNAME hcaNameToType
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * Get the type of a device, from name.
 *
 * Output in hca_type
 *
 */
int hcaNameToType(char *dev_name, HCA_Type* hca_type)
{
    MPIDI_STATE_DECL(MPID_STATE_HCANAMETOTYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_HCANAMETOTYPE);
    int mpi_errno = MPI_SUCCESS;
    int rate;

    *hca_type = UNKNOWN_HCA;

    if (!strncmp(dev_name, "mlx4", 4) || !strncmp(dev_name, "mthca", 5)) {
        umad_ca_t umad_ca;

        *hca_type = MLX_PCI_X;

        if (umad_init() < 0) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadinit");
        }

        memset(&umad_ca, 0, sizeof(umad_ca_t));
        if (umad_get_ca(dev_name, &umad_ca) < 0) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadgetca");
        }

        rate = get_rate(&umad_ca);
        if (!rate) {
            umad_release_ca(&umad_ca);
            umad_done();
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**umadgetrate");
        }

        if (!strncmp(dev_name, "mthca", 5)) {
            *hca_type = MLX_PCI_X;

            if (!strncmp(umad_ca.ca_type, "MT25", 4)) {
                switch (rate) {
                case 20:
                    *hca_type = MLX_PCI_EX_DDR;
                    break;
                case 10:
                    *hca_type = MLX_PCI_EX_SDR;
                    break;
                default:
                    *hca_type = MLX_PCI_EX_SDR;
                    break;
                }
            } else if (!strncmp(umad_ca.ca_type, "MT23", 4)) {
                *hca_type = MLX_PCI_X;
            } else {
                *hca_type = MLX_PCI_EX_SDR;
            }
        } else { /* mlx4 */
            switch(rate) {
            case 40:
                *hca_type = MLX_CX_QDR;
                break;
            case 20:
                *hca_type = MLX_CX_DDR;
                break;
            case 10:
                *hca_type = MLX_CX_SDR;
                break;
            default:
                *hca_type = MLX_CX_SDR;
                break;
            }
        }

        umad_release_ca(&umad_ca);
        umad_done();
    } else if(!strncmp(dev_name, "ipath", 5)) {
        *hca_type = PATH_HT;
    } else if(!strncmp(dev_name, "ehca", 4)) {
        *hca_type = IBM_EHCA;
    } else if (!strncmp(dev_name, "cxgb3", 5)) {
        *hca_type = CHELSIO_T3;
    } else if (!strncmp(dev_name, "cxgb4", 5)) {
        *hca_type = CHELSIO_T4;
    } else {
        *hca_type = UNKNOWN_HCA;
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_HCANAMETOTYPE);
    return mpi_errno;
}



#undef FUNCNAME
#define FUNCNAME get_hca_type
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * Get the type of a device.
 *
 * @param dev the device.
 * @param ctx the device context.
 * @param hca_type the type (output).
 *
 * @return MPI_SUCCESS if succeded, MPI_ERR_OTHER if failed
 *
 * \see HCA_Type
 */
static inline int get_hca_type (struct ibv_device* dev, struct ibv_context* ctx, HCA_Type* hca_type)
{
    MPIDI_STATE_DECL(MPID_STATE_GET_HCA_TYPE);
    MPIDI_FUNC_ENTER(MPID_STATE_GET_HCA_TYPE);
    int ret;
    int mpi_errno = MPI_SUCCESS;
    struct ibv_device_attr dev_attr;

    memset(&dev_attr, 0, sizeof(struct ibv_device_attr));

    char* dev_name = (char*) ibv_get_device_name(dev);
    if (!dev_name)
    {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**ibv_get_device_name");
    }

    ret = ibv_query_device(ctx, &dev_attr);
    if (ret)
    {
        MPIU_ERR_SETANDJUMP1(
            mpi_errno,
            MPI_ERR_OTHER,
            "**ibv_query_device",
            "**ibv_query_device %s",
            dev_name
        );
    }

    if ((mpi_errno = hcaNameToType(dev_name, hca_type)) != MPI_SUCCESS)
    {
        MPIU_ERR_POP(mpi_errno);
    }

fn_fail:
    MPIDI_FUNC_EXIT(MPID_STATE_GET_HCA_TYPE);
    return mpi_errno;
}


#ifdef ENABLE_HCA_REPORT
static char *port_state_str[] = {
        "???",
        "Down",
        "Initializing",
        "Armed",
        "Active"
};

static char *port_phy_state_str[] = {
        "No state change",
        "Sleep",
        "Polling",
        "Disabled",
        "PortConfigurationTraining",
        "LinkUp",
        "LinkErrorRecovery",
        "PhyTest"
};
#endif



#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_init_hca
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * Initialize the HCAs
 * Look at rdma_open_hca() & rdma_iba_hca_init_noqp() in
 * mvapich2/trunk/src/mpid/ch3/channels/mrail/src/gen2/rdma_iba_priv.c
 *
 * Store all the HCA info in mv2_nem_dev_info_t->hca[hca_num]
 *
 * Output:
 *         hca_list: fill it with the HCAs information
 *
 * \see hca_list
 */
int MPID_nem_ib_init_hca()
{
    int mpi_errno = MPI_SUCCESS;

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_INIT_HCA);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_INIT_HCA);


    struct ibv_device *ib_dev    = NULL;
    struct ibv_device **dev_list = NULL;
    int nHca;
    int num_devices = 0;

#ifdef CRC_CHECK
    gen_crc_table();
#endif
    memset( hca_list, 0, sizeof(hca_list) );

    /* Get the list of devices */
    dev_list = ibv_get_device_list(&num_devices);
    if (dev_list==NULL) {
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
	            "**fail %s", "No IB device found");
    }

    if (umad_init() < 0)
        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
	            "**fail %s", "Can't init UMAD library");

    /* Runtime checks */
    MPIU_Assert( num_devices<=MAX_NUM_HCAS );
    if ( num_devices> MAX_NUM_HCAS) {
        MPIU_Error_printf( "WARNING: found %d IB devices, the maximum is %d (MAX_NUM_HCAS). ",
        		num_devices, MAX_NUM_HCAS);
        num_devices = MAX_NUM_HCAS;
    }

    if ( ib_hca_num_hcas > num_devices) {
    	MPIU_Error_printf( "WARNING: user requested %d IB devices, the available number is %d. ",
        		ib_hca_num_hcas, num_devices);
        ib_hca_num_hcas = num_devices;
    }

    MPIU_DBG_MSG_P( CH3_CHANNEL, VERBOSE, "[HCA] Found %d HCAs\n", num_devices);
    MPIU_DBG_MSG_P( CH3_CHANNEL, VERBOSE, "[HCA] User requested %d\n", ib_hca_num_hcas);


    /* Retrieve information for each found device */
    for (nHca = 0; nHca < ib_hca_num_hcas; nHca++) {

    	/* Check for user choice */
        if( (rdma_iba_hca[0]==0) || (strncmp(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32)!=0) || (ib_hca_num_hcas > 1)) {
            /* User hasn't specified any HCA name, or the number of HCAs is greater then 1 */
            ib_dev = dev_list[nHca];

        } else {
            /* User specified a HCA, try to look for it */
            int dev_count;

            dev_count = 0;
            while(dev_list[dev_count]) {
                if(!strncmp(ibv_get_device_name(dev_list[dev_count]), rdma_iba_hca, 32)) {
                    ib_dev = dev_list[dev_count];
                    break;
                }
                dev_count++;
            }
        }

        /* Check if device has been identified */
        hca_list[nHca].ib_dev = ib_dev;
        if (!ib_dev) {
	        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
		            "**fail %s", "No IB device found");
        }

        MPIU_DBG_MSG_P( CH3_CHANNEL, VERBOSE, "[HCA] HCA device %d found\n", nHca);



        hca_list[nHca].nic_context = ibv_open_device(ib_dev);
        if (hca_list[nHca].nic_context==NULL) {
	        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER, "**fail",
		            "%s %d", "Failed to open HCA number", nHca);
        }

        hca_list[nHca].ptag = ibv_alloc_pd(hca_list[nHca].nic_context);
        if (!hca_list[nHca].ptag) {
            MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER,
                    "**fail", "%s%d", "Failed to alloc pd number ", nHca);
        }


        /* Set the hca type */
    #if defined(RDMA_CM)
        if (process_info.use_iwarp_mode) {
    	    if ((mpi_errno = rdma_cm_get_hca_type(process_info.use_iwarp_mode, &process_info.hca_type)) != MPI_SUCCESS)
    	    {
    		    MPIU_ERR_POP(mpi_errno);
    	    }

    	    if (process_info.hca_type == CHELSIO_T3)
    	    {
    		    process_info.use_iwarp_mode = 1;
    	    }
        }
        else
    #endif /* defined(RDMA_CM) */

		mpi_errno = get_hca_type(hca_list[nHca].ib_dev, hca_list[nHca].nic_context, &hca_list[nHca].hca_type);
        if (mpi_errno != MPI_SUCCESS)
        {
        	fprintf(stderr, "[%s, %d] Error in get_hca_type", __FILE__, __LINE__ );
            MPIU_ERR_POP(mpi_errno);
        }

    }



    if (!strncmp(rdma_iba_hca, RDMA_IBA_NULL_HCA, 32) &&
        (ib_hca_num_hcas==1) && (num_devices > nHca) &&
        (rdma_find_active_port(hca_list[0].nic_context, hca_list[nHca].ib_dev)==-1)) {
        /* Trac #376 - There are multiple rdma capable devices (num_devices) in
         * the system. The user has asked us to use ANY (!strncmp) ONE device
         * (rdma_num_hcas), and the first device does not have an active port. So
         * try to find some other device with an active port.
         */
    	int j;
        for (j = 0; dev_list[j]; j++) {
            ib_dev = dev_list[j];
            if (ib_dev) {
            	hca_list[0].nic_context = ibv_open_device(ib_dev);
                if (!hca_list[0].nic_context) {
                    /* Go to next device */
                    continue;
                }
                if (rdma_find_active_port(hca_list[0].nic_context, ib_dev)!=-1) {
                	hca_list[0].ib_dev = ib_dev;
                	hca_list[0].ptag = ibv_alloc_pd(hca_list[0].nic_context);
                    if (!hca_list[0].ptag) {
                        MPIU_ERR_SETFATALANDJUMP2(mpi_errno, MPI_ERR_OTHER,
                             "**fail", "%s%d", "Failed to alloc pd number ", nHca);
                    }
                }
            }
        }
    }

fn_exit:
    /* Clean up before exit */
	if (dev_list!=NULL)
	  ibv_free_device_list(dev_list);

    MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_INIT_HCA);
    return mpi_errno;
fn_fail:
    goto fn_exit;
}


/**
 * Create a srq using process info data.
 */
struct ibv_srq *create_srq(int hca_num)
{
    struct ibv_srq_init_attr srq_init_attr;
    struct ibv_srq *srq_ptr = NULL;

    memset(&srq_init_attr, 0, sizeof(srq_init_attr));

    srq_init_attr.srq_context    = hca_list[hca_num].nic_context;
    srq_init_attr.attr.max_wr    = viadev_srq_alloc_size;
    srq_init_attr.attr.max_sge   = 1;
    /* The limit value should be ignored during SRQ create */
    srq_init_attr.attr.srq_limit = viadev_srq_limit;

    srq_ptr = ibv_create_srq(hca_list[hca_num].ptag, &srq_init_attr);

    if (!srq_ptr) {
        ibv_error_abort(-1, "Error creating SRQ\n");
    }

    return srq_ptr;
}


#undef FUNCNAME
#define FUNCNAME MPID_nem_ib_open_ports
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
/**
 * the first step in original MPID_nem_ib_setup_conn() function
 * open hca, create ptags  and create cqs
 */
int MPID_nem_ib_open_ports()
{
    int mpi_errno = MPI_SUCCESS;

    /* Infiniband Verb Structures */
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;

    int nHca; /* , curRank, rail_index ; */

    MPIDI_STATE_DECL(MPID_STATE_MPIDI_OPEN_HCA);
    MPIDI_FUNC_ENTER(MPID_STATE_MPIDI_OPEN_HCA);

    for (nHca = 0; nHca < ib_hca_num_hcas; nHca++) {
        if (ibv_query_device(hca_list[nHca].nic_context, &dev_attr)) {
            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER, "**fail",
                    "**fail %s", "Error getting HCA attributes");
        }

        /* detecting active ports */
        if (rdma_default_port < 0 || ib_hca_num_ports > 1) {
            int nPort;
            int k = 0;
            for (nPort = 1; nPort <= RDMA_DEFAULT_MAX_PORTS; nPort ++) {
                if ((! ibv_query_port(hca_list[nHca].nic_context, nPort, &port_attr)) &&
                            port_attr.state == IBV_PORT_ACTIVE &&
                            (port_attr.lid || (!port_attr.lid && use_iboeth))) {
                    if (use_iboeth) {
                        if (ibv_query_gid(hca_list[nHca].nic_context,
                                        nPort, 0, &hca_list[nHca].gids[k])) {
                            /* new error information function needed */
                            MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                                    "**fail", "Failed to retrieve gid on rank %d", process_info.rank);
                        }
                        DEBUG_PRINT("[%d] %s(%d): Getting gid[%d][%d] for"
                                " port %d subnet_prefix = %llx,"
                                " intf_id = %llx\r\n",
                                process_info.rank, __FUNCTION__, __LINE__, nHca, k, k,
                                hca_list[nHca].gids[k].global.subnet_prefix,
                                hca_list[nHca].gids[k].global.interface_id);
                    } else {
                        hca_list[nHca].lids[k]    = port_attr.lid;
                    }
                    hca_list[nHca].ports[k++] = nPort;

                    if (check_attrs(&port_attr, &dev_attr)) {
                        MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                                "**fail", "**fail %s",
                                "Attributes failed sanity check");
                    }
                }
            }
            if (k < ib_hca_num_ports) {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**activeports", "**activeports %d", ib_hca_num_ports);
            }
        } else {
            if(ibv_query_port(hca_list[nHca].nic_context,
                        rdma_default_port, &port_attr)
                || (!port_attr.lid && !use_iboeth)
                || (port_attr.state != IBV_PORT_ACTIVE)) {
                MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                        "**portquery", "**portquery %d", rdma_default_port);
            }

            hca_list[nHca].ports[0] = rdma_default_port;

            if (use_iboeth) {
                if (ibv_query_gid(hca_list[nHca].nic_context, 0, 0, &hca_list[nHca].gids[0])) {
                    /* new error function needed */
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                            "**fail", "Failed to retrieve gid on rank %d", process_info.rank);
                }

                if (check_attrs(&port_attr, &dev_attr)) {
                    MPIU_ERR_SETFATALANDJUMP1(mpi_errno, MPI_ERR_OTHER,
                            "**fail", "**fail %s", "Attributes failed sanity check");
                }
            } else {
                hca_list[nHca].lids[0]  = port_attr.lid;
            }
        }

        if (rdma_use_blocking) {
            hca_list[nHca].comp_channel = ibv_create_comp_channel(hca_list[nHca].nic_context);

            if (!hca_list[nHca].comp_channel) {
                MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_fail,
                        "**fail", "**fail %s", "cannot create completion channel");
            }

            hca_list[nHca].send_cq_hndl = NULL;
            hca_list[nHca].recv_cq_hndl = NULL;
            hca_list[nHca].cq_hndl = ibv_create_cq(hca_list[nHca].nic_context,
                    rdma_default_max_cq_size, NULL, hca_list[nHca].comp_channel, 0);
            if (!hca_list[nHca].cq_hndl) {
                MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_fail,
                        "**fail", "**fail %s", "cannot create cq");
            }

            if (ibv_req_notify_cq(hca_list[nHca].cq_hndl, 0)) {
                MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_fail,
                        "**fail", "**fail %s", "cannot request cq notification");
            }
        } else {
            /* Allocate the completion queue handle for the HCA */
            hca_list[nHca].send_cq_hndl = NULL;
            hca_list[nHca].recv_cq_hndl = NULL;

            hca_list[nHca].cq_hndl = ibv_create_cq(hca_list[nHca].nic_context,
                    rdma_default_max_cq_size, NULL, NULL, 0);
            if (!hca_list[nHca].cq_hndl) {
                MPIU_ERR_SETFATALANDSTMT1(mpi_errno, MPI_ERR_OTHER, goto fn_fail,
                        "**fail", "**fail %s", "cannot create cq");
            }
        }

        /* to decouple process_info, may need to store has_srq to hca structure??? */
        if (process_info.has_srq) {
            hca_list[nHca].srq_hndl = create_srq(nHca);
        }
    }

    rdma_default_port       = hca_list[0].ports[0];

    fn_exit:

        MPIDI_FUNC_EXIT(MPID_STATE_MPIDI_OPEN_HCA);
        return mpi_errno;

    fn_fail:
        goto fn_exit;
}

