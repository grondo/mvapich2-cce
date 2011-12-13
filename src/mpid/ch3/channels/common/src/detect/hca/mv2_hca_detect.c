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

#include <stdio.h>
#include <string.h>
#include <infiniband/verbs.h>
#include <infiniband/umad.h>

#include "mv2_arch_hca_detect.h"

static mv2_multirail_info_type g_mv2_multirail_info = mv2_num_rail_unknown;

#define MV2_STR_MLX4         "mlx4"
#define MV2_STR_MTHCA        "mthca"
#define MV2_STR_IPATH        "ipath"
#define MV2_STR_QIB          "qib"
#define MV2_STR_EHCA         "ehca"
#define MV2_STR_CXGB3        "cxgb3"
#define MV2_STR_CXGB4        "cxgb4"
#define MV2_STR_NES0         "nes0"

/* Possible Architecture- Card Combinations */
const mv2_arch_hca_type
mv2_arch_hca_types[MV2_NUM_ARCH_TYPES][MV2_NUM_HCA_TYPES] = { 

    /* Arch Type = MV2_ARCH_UNKWN */
    {   
        MV2_ARCH_UNKWN_HCA_UNKWN,  
        MV2_ARCH_UNKWN_HCA_MLX_PCI_EX_SDR, 
        MV2_ARCH_UNKWN_HCA_MLX_PCI_EX_DDR, 
        MV2_ARCH_UNKWN_HCA_MLX_CX_SDR,
        MV2_ARCH_UNKWN_HCA_MLX_CX_DDR,
        MV2_ARCH_UNKWN_HCA_MLX_CX_QDR,
        MV2_ARCH_UNKWN_HCA_PATH_HT, 
        MV2_ARCH_UNKWN_HCA_MLX_PCI_X, 
        MV2_ARCH_UNKWN_HCA_IBM_EHCA,
        MV2_ARCH_UNKWN_HCA_CHELSIO_T3,
        MV2_ARCH_UNKWN_HCA_CHELSIO_T4,
        MV2_ARCH_UNKWN_HCA_INTEL_NE020
    },  

    /* Arch Type = MV2_ARCH_AMD_BARCELONA */
    {   
        MV2_ARCH_AMD_BRCLNA_16_HCA_UNKWN,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_SDR,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_DDR,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_CX_QDR,
        MV2_ARCH_AMD_BRCLNA_16_HCA_PATH_HT,
        MV2_ARCH_AMD_BRCLNA_16_HCA_MLX_PCI_X,
        MV2_ARCH_AMD_BRCLNA_16_HCA_IBM_EHCA,
        MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T3,
        MV2_ARCH_AMD_BRCLNA_16_HCA_CHELSIO_T4,
        MV2_ARCH_AMD_BRCLNA_16_HCA_INTEL_NE020
    },
    /* Arch Type = MV2_ARCH_AMD_MAGNY_COURS */
    {
        MV2_ARCH_AMD_MGNYCRS_24_HCA_UNKWN,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_SDR,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_DDR,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_CX_QDR,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_PATH_HT,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_MLX_PCI_X,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_IBM_EHCA,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T3,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_CHELSIO_T4,
        MV2_ARCH_AMD_MGNYCRS_24_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_INTEL_CLOVERTOWN */
    {
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_UNKWN,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_PATH_HT,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_CLVRTWN_8_HCA_INTEL_NE020
    },
    /* Arch Type = MV2_ARCH_INTEL_NEHALEM (8)*/
    {
        MV2_ARCH_INTEL_NEHLM_8_HCA_UNKWN,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_NEHLM_8_HCA_PATH_HT,
        MV2_ARCH_INTEL_NEHLM_8_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_NEHLM_8_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_NEHLM_8_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_NEHLM_8_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_INTEL_NEHALEM (16)*/
    {
        MV2_ARCH_INTEL_NEHLM_16_HCA_UNKWN,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_NEHLM_16_HCA_PATH_HT,
        MV2_ARCH_INTEL_NEHLM_16_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_NEHLM_16_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_NEHLM_16_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_NEHLM_16_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_INTEL_HARPERTOWN */
    {
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_UNKWN,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_PATH_HT,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_HRPRTWN_8_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_INTEL_XEON_DUAL_4 */
    {
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_UNKWN,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_PATH_HT,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_XEON_DUAL_4_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_AMD_OPTERON_DUAL */
    {
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_UNKWN,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_SDR,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_DDR,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_CX_QDR,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_PATH_HT,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_MLX_PCI_X,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_IBM_EHCA,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T3,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_CHELSIO_T4,
        MV2_ARCH_AMD_OPTRN_DUAL_4_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_INTEL */
    {
        MV2_ARCH_INTEL_HCA_UNKWN,
        MV2_ARCH_INTEL_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_HCA_PATH_HT,
        MV2_ARCH_INTEL_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_AMD */
    {
        MV2_ARCH_AMD_HCA_UNKWN,
        MV2_ARCH_AMD_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_AMD_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_AMD_HCA_MLX_CX_SDR,
        MV2_ARCH_AMD_HCA_MLX_CX_DDR,
        MV2_ARCH_AMD_HCA_MLX_CX_QDR,
        MV2_ARCH_AMD_HCA_PATH_HT,
        MV2_ARCH_AMD_HCA_MLX_PCI_X,
        MV2_ARCH_AMD_HCA_IBM_EHCA,
        MV2_ARCH_AMD_HCA_CHELSIO_T3,
        MV2_ARCH_AMD_HCA_CHELSIO_T4,
        MV2_ARCH_AMD_HCA_INTEL_NE020
    },

    /* Arch Type = MV2_ARCH_IBM_PPC */
    {
        MV2_ARCH_IBM_PPC_HCA_UNKWN,
        MV2_ARCH_IBM_PPC_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_IBM_PPC_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_IBM_PPC_HCA_MLX_CX_SDR,
        MV2_ARCH_IBM_PPC_HCA_MLX_CX_DDR,
        MV2_ARCH_IBM_PPC_HCA_MLX_CX_QDR,
        MV2_ARCH_IBM_PPC_HCA_PATH_HT,
        MV2_ARCH_IBM_PPC_HCA_MLX_PCI_X,
        MV2_ARCH_IBM_PPC_HCA_IBM_EHCA,
        MV2_ARCH_IBM_PPC_HCA_CHELSIO_T3,
        MV2_ARCH_IBM_PPC_HCA_CHELSIO_T4,
        MV2_ARCH_IBM_PPC_HCA_INTEL_NE020
    },

        /* Arch Type : MV2_ARCH_INTEL_XEON_E5630_8 */
    {   
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_UNKWN,    
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_EX_SDR,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_EX_DDR,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_SDR,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_DDR,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_CX_QDR,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_PATH_HT,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_MLX_PCI_X,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_IBM_EHCA,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_CHELSIO_T3,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_CHELSIO_T4,
        MV2_ARCH_INTEL_XEON_E5630_8_HCA_INTEL_NE020
    }, 
};


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

static mv2_hca_type mv2_hca_name_to_type ( char *dev_name )
{
    int rate=0;
    mv2_hca_type hca_type = MV2_HCA_UNKWN;

    if (!strncmp(dev_name, MV2_STR_MLX4, 4) || !strncmp(dev_name,
                MV2_STR_MTHCA, 5)) {
        umad_ca_t umad_ca;

        hca_type = MV2_HCA_MLX_PCI_X;

        if (umad_init() < 0) {
            return hca_type;
        }

        memset(&umad_ca, 0, sizeof(umad_ca_t));

        if (umad_get_ca(dev_name, &umad_ca) < 0) {
            return hca_type;
        }

        if (!getenv("MV2_USE_RDMAOE")) {
            rate = get_rate(&umad_ca);
            if (!rate) {
                umad_release_ca(&umad_ca);
                umad_done();
                return hca_type;
            }
        }

        if (!strncmp(dev_name, MV2_STR_MTHCA, 5)) {
            hca_type = MV2_HCA_MLX_PCI_X;

            if (!strncmp(umad_ca.ca_type, "MT25", 4)) {

                switch (rate) {
                    case 20:
                        hca_type = MV2_HCA_MLX_PCI_EX_DDR;
                        break;

                    case 10:
                        hca_type = MV2_HCA_MLX_PCI_EX_SDR;
                        break;

                    default:
                        hca_type = MV2_HCA_MLX_PCI_EX_SDR;
                        break;
                }

            } else if (!strncmp(umad_ca.ca_type, "MT23", 4)) {
                hca_type = MV2_HCA_MLX_PCI_X;

            } else {
                hca_type = MV2_HCA_MLX_PCI_EX_SDR; 
            }
        } else { /* mlx4 */ 

            switch(rate) {
                case 40:
                    hca_type = MV2_HCA_MLX_CX_QDR;
                    break;

                case 20:
                    hca_type = MV2_HCA_MLX_CX_DDR;
                    break;

                case 10:
                    hca_type = MV2_HCA_MLX_CX_SDR;
                    break;

                default:
                    hca_type = MV2_HCA_MLX_CX_SDR;
                    break;
            }
        }

        umad_release_ca(&umad_ca);
        umad_done();

    } else if(!strncmp(dev_name, MV2_STR_IPATH, 5)) {
        hca_type = MV2_HCA_PATH_HT;

    } else if(!strncmp(dev_name, MV2_STR_QIB, 3)) {
        hca_type = MV2_HCA_QIB;

    } else if(!strncmp(dev_name, MV2_STR_EHCA, 4)) {
        hca_type = MV2_HCA_IBM_EHCA;

    } else if (!strncmp(dev_name, MV2_STR_CXGB3, 5)) {
        hca_type = MV2_HCA_CHELSIO_T3;

    } else if (!strncmp(dev_name, MV2_STR_CXGB4, 5)) {
        hca_type = MV2_HCA_CHELSIO_T4;

    } else if (!strncmp(dev_name, MV2_STR_NES0, 4)) {
        hca_type = MV2_HCA_INTEL_NE020;

    } else {
        hca_type = MV2_HCA_UNKWN;
    }    
    return hca_type;
}

/* Identify hca type */
mv2_hca_type mv2_get_hca_type( struct ibv_device *dev )
{
    char* dev_name = NULL;

    dev_name = (char*) ibv_get_device_name( dev );

    if (!dev_name) {
        return MV2_HCA_UNKWN;
    } else {
        return mv2_hca_name_to_type( dev_name );
    }
}

mv2_arch_hca_type mv2_get_arch_hca_type ( struct ibv_device *dev )
{
    return mv2_arch_hca_types[mv2_get_arch_type()][mv2_get_hca_type(dev)];
}

mv2_multirail_info_type mv2_get_multirail_info()
{
    if ( mv2_num_rail_unknown == g_mv2_multirail_info ) {
        int num_devices;
        struct ibv_device **dev_list = NULL;

        /* Get the number of rails */
        dev_list = ibv_get_device_list(&num_devices);

        switch (num_devices){
            case 1:
                g_mv2_multirail_info = mv2_num_rail_1;
                break;
            case 2:
                g_mv2_multirail_info = mv2_num_rail_2;
                break;
            case 3:
                g_mv2_multirail_info = mv2_num_rail_3;
                break;
            case 4:
                g_mv2_multirail_info = mv2_num_rail_4;
                break;
            default:
                g_mv2_multirail_info = mv2_num_rail_unknown;
                break;
        }
        ibv_free_device_list(dev_list);
    }
    return g_mv2_multirail_info;
}

