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

#ifdef HAVE_LIBHWLOC
#include <hwloc.h>
#include <dirent.h>
#endif /* #ifdef HAVE_LIBHWLOC */

#include "mv2_arch_hca_detect.h"

static mv2_arch_type g_mv2_arch_type = MV2_ARCH_UNKWN;
static int g_mv2_num_cpus = -1;


#define CONFIG_FILE         "/proc/cpuinfo"
#define MAX_LINE_LENGTH     512
#define MAX_NAME_LENGTH     64

#define CLOVERTOWN_MODEL    15
#define HARPERTOWN_MODEL    23
#define NEHALEM_MODEL       26
#define INTEL_E5630_MODEL   44

#define MV2_STR_VENDOR_ID    "vendor_id"
#define MV2_STR_AUTH_AMD     "AuthenticAMD"
#define MV2_STR_MODEL        "model"
#define MV2_STR_WS            " "
#define MV2_STR_PHYSICAL     "physical"

#ifndef HAVE_LIBHWLOC

#define MAX_NUM_CPUS 32
int INTEL_XEON_DUAL_MAPPING[]      = {0,1,0,1};
int INTEL_CLOVERTOWN_MAPPING[]     = {0,0,1,1,0,0,1,1};                  /*        ((0,1),(4,5))((2,3),(6,7))             */
int INTEL_HARPERTOWN_LEG_MAPPING[] = {0,1,0,1,0,1,0,1};                  /* legacy ((0,2),(4,6))((1,3),(5,7))             */
int INTEL_HARPERTOWN_COM_MAPPING[] = {0,0,0,0,1,1,1,1};                  /* common ((0,1),(2,3))((4,5),(6,7))             */
int INTEL_NEHALEM_LEG_MAPPING[]    = {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1};  /* legacy (0,2,4,6)(1,3,5,7) with hyperthreading */
int INTEL_NEHALEM_COM_MAPPING[]    = {0,0,0,0,1,1,1,1,0,0,0,0,1,1,1,1};  /* common (0,1,2,3)(4,5,6,7) with hyperthreading */
int AMD_OPTERON_DUAL_MAPPING[]     = {0,0,1,1};
int AMD_BARCELONA_MAPPING[]        = {0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3};
int AMD_MAGNY_CRS_MAPPING[]        = {1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,2,2,2,2};

#endif /* #ifndef HAVE_LIBHWLOC */

/* CPU Family */
typedef enum{
    CPU_FAMILY_NONE=0,
    CPU_FAMILY_INTEL,
    CPU_FAMILY_AMD,
} mv2_cpu_type;


/* Identify architecture type */
#ifdef HAVE_LIBHWLOC
mv2_arch_type mv2_get_arch_type()
{
    if ( MV2_ARCH_UNKWN == g_mv2_arch_type ) {
        FILE *fp;
        int num_sockets = 0, cpu_model = 0, num_cpus = 0, ret;
        unsigned topodepth = -1, depth = -1;
        char line[MAX_LINE_LENGTH], *tmp, *key;

        mv2_arch_type arch_type = MV2_ARCH_UNKWN;
        mv2_cpu_type cpu_type = CPU_FAMILY_NONE;
        hwloc_topology_t topology;

        /* Initialize hw_loc */
        ret = hwloc_topology_init(&topology);
        if ( 0 != ret ){
            fprintf( stderr, "Warning: %s: Failed to initialize hwloc\n",
                    __func__ );
            return arch_type;
        }
        hwloc_topology_load(topology);

        /* Determine topology depth */
        topodepth = hwloc_topology_get_depth(topology);
        if( HWLOC_TYPE_DEPTH_UNKNOWN == topodepth ) {
            fprintf(stderr, "Warning: %s: Failed to determine topology depth.\n", __func__ );
            return arch_type;
        }

        /* Count number of (logical) processors */
        depth = hwloc_get_type_depth(topology, HWLOC_OBJ_PU);

        if( HWLOC_TYPE_DEPTH_UNKNOWN == depth ) {
            fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__ );
            return arch_type;
        }
        if(! (num_cpus = hwloc_get_nbobjs_by_depth(topology, depth))) {
            fprintf(stderr, "Warning: %s: Failed to determine number of processors.\n", __func__);
            return arch_type;
        }
        g_mv2_num_cpus = num_cpus;

        /* Count number of sockets */
        depth = hwloc_get_type_depth(topology, HWLOC_OBJ_SOCKET);
        if( HWLOC_TYPE_DEPTH_UNKNOWN == depth ) {
            fprintf(stderr, "Warning: %s: Failed to determine number of sockets.\n", __func__);
            return arch_type;
        } else {
            num_sockets = hwloc_get_nbobjs_by_depth(topology, depth);
        }

        /* free topology info */
        hwloc_topology_destroy( topology );

        /* Parse /proc/cpuinfo for additional useful things */
        if(fp = fopen(CONFIG_FILE, "r")) { 

            while(! feof(fp)) {
                memset(line, 0, MAX_LINE_LENGTH);
                fgets(line, MAX_LINE_LENGTH - 1, fp); 

                if(! (key = strtok(line, "\t:"))) {
                    continue;
                }

                /* Identify the CPU Family */
                if(! strcmp(key, MV2_STR_VENDOR_ID)) {
                    strtok(NULL, MV2_STR_WS);
                    tmp = strtok(NULL, MV2_STR_WS);

                    if (! strncmp(tmp, MV2_STR_AUTH_AMD, strlen( MV2_STR_AUTH_AMD
                                    ))) {
                        cpu_type = CPU_FAMILY_AMD;

                    } else {
                        cpu_type = CPU_FAMILY_INTEL;
                    }
                    continue;
                }

                if( !cpu_model ) {

                    if(! strcmp(key, MV2_STR_MODEL)) {
                        strtok(NULL, MV2_STR_WS);
                        tmp = strtok(NULL, MV2_STR_WS);
                        sscanf(tmp, "%d", &cpu_model);
                        continue;
                    }
                }
            }
            fclose(fp);

            if( CPU_FAMILY_INTEL == cpu_type ) {
                arch_type = MV2_ARCH_INTEL;

                if(2 == num_sockets ) {

                    if(4 == num_cpus) {
                        arch_type = MV2_ARCH_INTEL_XEON_DUAL_4;

                    } else if(8 == num_cpus) {

                        if(CLOVERTOWN_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_CLOVERTOWN_8;

                        } else if(HARPERTOWN_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_HARPERTOWN_8;

                        } else if(NEHALEM_MODEL == cpu_model) {
                            arch_type = MV2_ARCH_INTEL_NEHALEM_8;
                        
                        } else if(INTEL_E5630_MODEL == cpu_model){
                            arch_type = MV2_ARCH_INTEL_XEON_E5630_8;
                        } 

                    } else if(16 == num_cpus) {

                        if(NEHALEM_MODEL == cpu_model) {  /* nehalem with smt on */
                            arch_type = MV2_ARCH_INTEL_NEHALEM_16;
                        }
                    }
                }

            } else if(CPU_FAMILY_AMD == cpu_type) {
                arch_type = MV2_ARCH_AMD;

                if(2 == num_sockets) {

                    if(4 == num_cpus) {
                        arch_type = MV2_ARCH_AMD_OPTERON_DUAL_4;

                    } else if(24 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_MAGNY_COURS_24;
                    }
                } else if(4 == num_sockets) {

                    if(16 == num_cpus) {
                        arch_type =  MV2_ARCH_AMD_BARCELONA_16;
                    }
                }
            }
        } else {
            fprintf(stderr, "Warning: %s: Failed to open \"%s\".\n", __func__,
                    CONFIG_FILE);
        }
        g_mv2_arch_type = arch_type;
        return arch_type;
    } else {
        return g_mv2_arch_type;
    }

}

#else

mv2_arch_type mv2_get_arch_type()
{
    if ( MV2_ARCH_UNKWN == g_mv2_arch_type ) {
        char line[MAX_LINE_LENGTH];
        char input[MAX_NAME_LENGTH];
        char bogus1[MAX_NAME_LENGTH];
        char bogus2[MAX_NAME_LENGTH];
        char bogus3[MAX_NAME_LENGTH];

        int physical_id;
        int core_mapping[MAX_NUM_CPUS];
        int core_index = 0, num_cpus;
        int model;
        int vendor_set=0, model_set=0;

        mv2_cpu_type cpu_type = CPU_FAMILY_NONE;
        mv2_arch_type arch_type = MV2_ARCH_UNKWN;

        FILE* fp=fopen(CONFIG_FILE,"r");
        if (fp == NULL){
            fprintf( stderr, "Cannot open cpuinfo file\n");
            return arch_type;
        }

        memset(core_mapping, 0, sizeof(core_mapping));

        while(!feof(fp)){
            memset(line,0,MAX_LINE_LENGTH);
            fgets(line, MAX_LINE_LENGTH, fp);

            memset(input, 0, MAX_NAME_LENGTH);
            sscanf(line, "%s", input);

            if (!vendor_set) {
                if (strcmp(input, MV2_STR_VENDOR_ID) == 0) {
                    memset(input, 0, MAX_NAME_LENGTH);
                    sscanf(line,"%s%s%s",bogus1, bogus2, input);
                    if (strcmp(input, MV2_STR_AUTH_AMD ) == 0) {
                        cpu_type = CPU_FAMILY_AMD;
                        arch_type = MV2_ARCH_AMD;
                    } else {
                        cpu_type = CPU_FAMILY_INTEL;
                        arch_type = MV2_ARCH_INTEL;
                    }
                    vendor_set = 1;
                }
            }

            if (!model_set){
                if (strcmp(input, MV2_STR_MODEL) == 0) {
                    sscanf(line, "%s%s%d", bogus1, bogus2, &model);
                    model_set = 1;
                }
            }

            if (strcmp(input, MV2_STR_PHYSICAL ) == 0) {
                sscanf(line, "%s%s%s%d", bogus1, bogus2, bogus3, &physical_id);
                core_mapping[core_index++] = physical_id;
            }
        }

        /* Set the number of cores per node */
        g_mv2_num_cpus = num_cpus = core_index;
        if ( 4 == num_cpus ) {
            if((memcmp(INTEL_XEON_DUAL_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) == 0)
                    && ( CPU_FAMILY_INTEL == cpu_type )){
                arch_type = MV2_ARCH_INTEL_XEON_DUAL_4;
            } else
                if((memcmp(AMD_OPTERON_DUAL_MAPPING,core_mapping,sizeof(int)*num_cpus)
                            == 0)
                        && ( CPU_FAMILY_AMD == cpu_type )){
                    arch_type = MV2_ARCH_AMD_OPTERON_DUAL_4;
                }
        } else if ( 8 == num_cpus ) {
            if( CPU_FAMILY_INTEL == cpu_type ) {
                if( CLOVERTOWN_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_CLOVERTOWN_8;
                }
                else if( HARPERTOWN_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_HARPERTOWN_8;
                }
                else if( NEHALEM_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_NEHALEM_8;
                }
                else if( INTEL_E5630_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_XEON_E5630_8;
                }
            }
        } else if ( 16 == num_cpus ) {
            if( CPU_FAMILY_INTEL == cpu_type ) {
                if( NEHALEM_MODEL == model ) {
                    arch_type = MV2_ARCH_INTEL_NEHALEM_16;
                }
            }
            else if( CPU_FAMILY_AMD == cpu_type ) {
                if(0 == memcmp(AMD_BARCELONA_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_BARCELONA_16;
                }
            }
        } else if  ( 24 == num_cpus ){
            if ( CPU_FAMILY_AMD == cpu_type ){
                if (0 == memcmp(AMD_MAGNY_CRS_MAPPING,core_mapping,
                            sizeof(int)*num_cpus) ) {
                    arch_type = MV2_ARCH_AMD_MAGNY_COURS_24;
                }
            }
        }

        fclose(fp);
        g_mv2_arch_type = arch_type;
        return arch_type;

    }else {
        return g_mv2_arch_type;
    }
}


#endif

/* API for getting the number of cpus */
int mv2_get_num_cpus()
{
    /* Check if num_cores is already identified */
    if ( -1 == g_mv2_num_cpus ){
        g_mv2_arch_type = mv2_get_arch_type();
    }
    return g_mv2_num_cpus;
}

