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
#include <unistd.h>
#include "psmpriv.h"
#include "psm_vbuf.h"
#include <dirent.h>
#include "coll_shmem.h"

volatile unsigned int MPIDI_CH3I_progress_completion_count = 0; //ODOT: what is this ?
volatile int MPIDI_CH3I_progress_blocked = FALSE;
volatile int MPIDI_CH3I_progress_wakeup_signalled = FALSE;

/* Globals */
/* psm device instance */
struct psmdev_info_t    psmdev_cw;
uint32_t                ipath_rndv_thresh;
uint8_t                 ipath_debug_enable;
uint32_t                ipath_dump_frequency;

static char    scratch[WRBUFSZ];
static char             *kvsid;
static psm_uuid_t       psm_uuid;

static int  psm_bcast_uuid(int pg_size, int pg_rank);
static int  psm_allgather_epid(psm_epid_t *list, int pg_size, int pg_rank);
static void psm_other_init(MPIDI_PG_t *pg);
static void psm_preinit(int pg_size);
static int  decode(unsigned s_len, char *src, unsigned d_len, char *dst);
static int  encode(unsigned s_len, char *src, unsigned d_len, char *dst);

extern void MPIDI_CH3I_SHMEM_COLL_Cleanup();

#undef FUNCNAME
#define FUNCNAME psm_doinit
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
int psm_doinit(int has_parent, MPIDI_PG_t *pg, int pg_rank)
{
    int verno_major, verno_minor;
    int pg_size, mpi_errno;
    psm_epid_t myid, *epidlist = NULL;
    psm_error_t *errs = NULL, err;

    pg_size = MPIDI_PG_Get_size(pg);
    MPIDI_PG_GetConnKVSname(&kvsid);
    psmdev_cw.pg_size = pg_size;
    verno_major = PSM_VERNO_MAJOR;
    verno_minor = PSM_VERNO_MINOR;

    /* initialize shared memory for collectives */
    if (enable_shmem_collectives) {
        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_init(pg, pg->ch.local_process_id)) != MPI_SUCCESS)
        {
            mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                   FCNAME, __LINE__, MPI_ERR_OTHER, "**fail",
                   "%s", "SHMEM_COLL_init failed");
            goto cleanup_files;
        }

        PMI_Barrier();

        /* Memory Mapping shared files for collectives*/
        if ((mpi_errno = MPIDI_CH3I_SHMEM_COLL_Mmap(pg, pg->ch.local_process_id)) != MPI_SUCCESS)
        {
           mpi_errno = MPIR_Err_create_code( MPI_SUCCESS, MPI_ERR_OTHER,
                 FCNAME, __LINE__, MPI_ERR_OTHER, "**fail", "%s",
                 "SHMEM_COLL_Mmap failed");
           goto cleanup_files;
        }

        MPIDI_CH3I_SHMEM_COLL_Unlink();
    }  

    assert(pg_rank < pg_size);
    mpi_errno = psm_bcast_uuid(pg_size, pg_rank);
    if(mpi_errno != MPI_SUCCESS) {
        goto fn_fail;
    }

    psm_preinit(pg_size);
    psm_error_register_handler(NULL, PSM_ERRHANDLER_NO_HANDLER);

    err = psm_init(&verno_major, &verno_minor);
    if(err != PSM_OK) {
        fprintf(stderr, "psm_init failed with error: %s\n", psm_error_get_string(err));
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**psminit");
    }

    if((err = psm_ep_open(psm_uuid, NULL, &psmdev_cw.ep, &myid)) != PSM_OK) {
        fprintf(stderr, "psm_ep_open failed with error %s\n", psm_error_get_string(err));
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**psmepopen");
    }
    epidlist = (psm_epid_t *)MPIU_Malloc(pg_size * sizeof(psm_epid_t));
    if(epidlist == NULL) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_NO_MEM, "**psmnomem");
    }
    epidlist[pg_rank] = myid;

    mpi_errno = psm_allgather_epid(epidlist, pg_size, pg_rank);
    if(mpi_errno != MPI_SUCCESS) {
        goto fn_fail;
    }

    psmdev_cw.epaddrs = (psm_epaddr_t *) MPIU_Malloc(pg_size * sizeof(psm_epaddr_t));
    errs = (psm_error_t *) MPIU_Malloc(pg_size * sizeof(psm_error_t));
    if(psmdev_cw.epaddrs == NULL || errs == NULL) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_NO_MEM, "**psmnomem");
    }

    if((err = psm_ep_connect(psmdev_cw.ep, pg_size, epidlist, NULL, errs, 
                psmdev_cw.epaddrs, TIMEOUT * SEC_IN_NS)) != PSM_OK) {
        fprintf(stderr, "psm_ep_connect failed with error %s\n", psm_error_get_string(err));
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_INTERN, "**psmconnectfailed");
    }
    DBG("psm_ep_connect done\n");

    if((err = psm_mq_init(psmdev_cw.ep, PSM_MQ_ORDERMASK_ALL, NULL, 0, 
                &psmdev_cw.mq)) != PSM_OK) {
        DBG("psm_mq_init failed\n");
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_INTERN, "**psm_mqinitfailed");
    }
    /* initialize VC state, eager size value, queues etc */
    psm_other_init(pg);

    MPIU_Free(errs);
    MPIU_Free(epidlist);
    return MPI_SUCCESS;

cleanup_files:
    MPIDI_CH3I_SHMEM_COLL_Cleanup();
fn_fail:
    if(errs)
        MPIU_Free(errs);
    if(epidlist)
        MPIU_Free(epidlist);
    return MPI_ERR_INTERN;
}

static int filter(const struct dirent *ent)
{
    int res;
    sprintf(scratch, "mpi_%s_", kvsid);
    res = strncmp(ent->d_name, scratch, strlen(scratch));
    if(res)     return 0;
    else        return 1;
}
    
/*  handle special psm init. PSM_DEVICES init, version test for setting
 *  MPI_LOCALRANKS, MPI_LOCALRANKID 
 *  Updated on Fed 2 2010 based on patch provided by Ben Truscott. Refer to 
 *  TRAC Ticket #457 */
static void psm_preinit(int pg_size)
{
    FILE *fp;
    struct dirent **fls;
    int n, id = 0, i, universesize;

    if(pg_size > 0)
        universesize = pg_size;
    else
        universesize = 1; /*May be started without mpiexec.*/

    /* We should not override user settings for these parameters. 
     * This might cause problems with the new greedy context acquisition 
     * when multiple jobs share the same node. Refer to TRAC Ticket #457
     * putenv("PSM_SHAREDCONTEXTS=1");
     * putenv("PSM_SHAREDCONTEXTS_MAX=16");*/

    /* for psm versions 2.0 or later, hints are needed for context sharing */
    if(PSM_VERNO >= 0x0105) {
        sprintf(scratch, "/dev/shm/mpi_%s_%d", kvsid, getpid());
        fp = fopen(scratch, "w");
        if(fp == NULL) {
            goto skip;
        }
        PMI_Barrier();
        n = scandir("/dev/shm", &fls, filter, NULL);  
        sprintf(scratch, "mpi_%s_%d", kvsid, getpid());
        for(i = 0; i < n; i++) {
            if(0 == strcmp(scratch, fls[i]->d_name))
                id = i;
            free(fls[i]);
        }   
        free(fls);

        PMI_Barrier();
        DBG("localid %d localranks %d\n", id, n);
        snprintf(scratch, sizeof(scratch), "%d", n);
	setenv("MPI_LOCALNRANKS", scratch, 0);
        snprintf(scratch, sizeof(scratch), "%d", id);
	setenv("MPI_LOCALRANKID", scratch, 0);

        /* Should not override user settings. Updating to handle all 
         * possible scenarios. Refer to TRAC Ticket #457 */
        if ( getenv("PSM_DEVICES") == NULL ) {
            if (universesize > n && n > 1) {
                /* There are both local and remote ranks present;
                 * we require both the shm and ipath devices in
                 * this case. */
                putenv("PSM_DEVICES=self,shm,ipath");
            }
            else if (universesize > n && n == 1) {
                /* There are only remote ranks; we do not require
                 * the shm device. */
                putenv("PSM_DEVICES=self,ipath");
            }
            else if (universesize == n && n > 1) {
                /* There are only local ranks; we do not require the
                 * ipath device. */
                putenv("PSM_DEVICES=self,shm");
            }
            else if (universesize == 1 && n == 1) {
                /* This is the only rank; we do not need either the
                   shm or the ipath device. */
                putenv("PSM_DEVICES=self");
            }
            else {
                /* Impossible situation? Leave PSM_DEVICES as it
                 * previously was. */
            }
        }

        sprintf(scratch, "/dev/shm/mpi_%s_%d", kvsid, getpid());
        unlink(scratch);
        fclose(fp);
    } else {
skip:
        /* If we cannot not open the memory-mapped file for writing (shm) 
         * or if we are unsure of the version of PSM, let PSM_DEVICES
         * to be the default (usually "self,ipath") or what the user 
         * has set. Refer to TRAC Ticket #457
         * putenv("PSM_DEVICES=self,shm,ipath"); */
         DBG("Memory-mapped file creation failed or unknown PSM version. \
              Leaving PSM_DEVICES to default or user's settings. \n");
    }
}

/* all ranks provide their epid via PMI put/get */
static int psm_allgather_epid(psm_epid_t *list, int pg_size, int pg_rank)
{
    char *kvs_name;
    int kvslen;
    char *kvskey;
    int i, mpi_errno = MPI_SUCCESS;

    if(pg_size == 1)
        return MPI_SUCCESS;

    PMI_KVS_Get_key_length_max(&kvslen);
    kvskey = (char *) MPIU_Malloc (kvslen);

    DBG("[%d] my epid = %d\n", pg_rank, list[pg_rank]);
    MPIDI_PG_GetConnKVSname(&kvs_name);
    MPIU_Snprintf(kvskey, kvslen, "pmi_epidkey_%d", pg_rank);
    MPIU_Snprintf(scratch, WRBUFSZ, "%lu", list[pg_rank]);
    if(PMI_KVS_Put(kvs_name, kvskey, scratch) != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**epid_putfailed");
    }
    if(PMI_KVS_Commit(kvs_name) != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**epid_putcommit");
    }
    PMI_Barrier();

    for(i = 0; i < pg_size; i++) {
        if(i == pg_rank)
            continue;

        MPIU_Snprintf(kvskey, kvslen, "pmi_epidkey_%d", i);
        if(PMI_KVS_Get(kvs_name, kvskey, scratch, WRBUFSZ) != PMI_SUCCESS) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**epid_getfailed");
        }
        sscanf(scratch, "%lu", &(list[i]));
        DBG("[%d] got epid %llu\n", pg_rank, list[i]);
    }
    PMI_Barrier();
    MPIU_Free(kvskey);
    DBG("epid collected from all\n");
    return MPI_SUCCESS;

fn_fail:
    DBG("epid put/commit/get failed\n");
    return MPI_ERR_INTERN;
}

/* broadcast the uuid to all ranks via PMI put/get */
static int psm_bcast_uuid(int pg_size, int pg_rank)
{
    char *kvs_name;
    int mpi_errno = MPI_SUCCESS, valen;
    int kvslen, srclen = sizeof(psm_uuid_t), dst = WRBUFSZ;
    char *kvskey;

    if(pg_rank == ROOT)
        psm_uuid_generate(psm_uuid);

    if(pg_size == 1)
        return MPI_SUCCESS;

    PMI_KVS_Get_key_length_max(&kvslen);
    PMI_KVS_Get_value_length_max(&valen);
    kvskey = (char *) MPIU_Malloc (kvslen);
    MPIDI_PG_GetConnKVSname(&kvs_name);
    snprintf(kvskey, kvslen, MPID_PSM_UUID"_%d_%s", pg_rank, kvs_name);

    DBG("key name = %s\n", kvskey);
    if(pg_rank == ROOT) {
        encode(srclen, (char *)&psm_uuid, dst, scratch);
    } else {
        strcpy(scratch, "dummy-entry");
    }
    
    if(PMI_KVS_Put(kvs_name, kvskey, scratch) != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**pmiputuuid");
    }
    if(PMI_KVS_Commit(kvs_name) != PMI_SUCCESS) {
        MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**pmicommituuid");
    }

    PMI_Barrier();
    if(pg_rank != ROOT) {
        snprintf(kvskey, kvslen, MPID_PSM_UUID"_0_%s", kvs_name);
        if(PMI_KVS_Get(kvs_name, kvskey, scratch, WRBUFSZ) != PMI_SUCCESS) {
            MPIU_ERR_SETANDJUMP(mpi_errno, MPI_ERR_OTHER, "**pmigetuuid");
        }
        strcat(scratch, "==");
        srclen = strlen(scratch);
        if(decode(srclen, scratch, sizeof(psm_uuid_t), (char *)&psm_uuid)) {
            fprintf(stderr, "base-64 decode failed of UUID\n");
            goto fn_fail;
        }
    }
    PMI_Barrier();
    MPIU_Free(kvskey);
    return MPI_SUCCESS;

fn_fail:
    DBG("uuid bcast failed\n");
    return MPI_ERR_INTERN;
}

/* Ch3 expects channel to initialize VC fields.
   force_eager is used because psm internally manages eager/rndv so
   we can just force one code-path for all message sizes */

static void psm_other_init(MPIDI_PG_t *pg)
{
    MPIDI_VC_t *vc;
    int i;
    char *flag;

    for(i = 0; i < MPIDI_PG_Get_size(pg); i++) {
        MPIDI_PG_Get_vc(pg, i, &vc);

        vc->state = MPIDI_VC_STATE_ACTIVE;
        vc->force_eager = 1;
        vc->eager_max_msg_sz = PSM_VBUFSZ;
        vc->rndvSend_fn = NULL;
        vc->rndvRecv_fn = NULL;
    }

    psm_mq_getopt(psmdev_cw.mq, PSM_MQ_RNDV_IPATH_SZ,
                &ipath_rndv_thresh);
    psm_mq_getopt(psmdev_cw.mq, PSM_MQ_RNDV_SHM_SZ,
                &i);
    if(i < ipath_rndv_thresh)
        ipath_rndv_thresh = i;
    DBG("blocking threshold %d\n", ipath_rndv_thresh);
    ipath_debug_enable = 0;
    if((flag = getenv("MV2_PSM_DEBUG")) != NULL) {
        ipath_debug_enable = !!atoi(flag);
    }
    ipath_dump_frequency = 10;
    if((flag = getenv("MV2_PSM_DUMP_FREQUENCY")) != NULL) {
        ipath_dump_frequency = atoi(flag);
    }

    psm_queue_init();
    psm_init_vbuf_lock();
    psm_allocate_vbufs(PSM_INITIAL_POOL_SZ);
    psm_init_1sided();
}

static char base64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                       "abcdefghijklmnopqrstuvwxyz"
                       "0123456789"
                       "+/";
/*
** ENCODE RAW into BASE64
*/

/* Encode source from raw data into Base64 encoded string */
static int encode(unsigned s_len, char *src, unsigned d_len, char *dst)
{
    unsigned triad;

    for (triad = 0; triad < s_len; triad += 3) {
        unsigned long int sr;
        unsigned byte;

        for (byte = 0; (byte<3)&&(triad+byte<s_len); ++byte) {
            sr <<= 8;
            sr |= (*(src+triad+byte) & 0xff);
        }

        sr <<= (6-((8*byte)%6))%6; /*shift left to next 6bit alignment*/

        if (d_len < 4) 
            return 1; /* error - dest too short */

        *(dst+0) = *(dst+1) = *(dst+2) = *(dst+3) = '=';
        switch(byte) {
        case 3:
            *(dst+3) = base64[sr&0x3f];
            sr >>= 6;
        case 2:
            *(dst+2) = base64[sr&0x3f];
            sr >>= 6;
        case 1:
            *(dst+1) = base64[sr&0x3f];
            sr >>= 6;
            *(dst+0) = base64[sr&0x3f];
        }
        dst += 4; d_len -= 4;
    }
    return 0;
}

/*
** DECODE BASE64 into RAW
*/

/* determine which sextet value a Base64 character represents */
static int tlu(int byte)
{
    int index;

    for (index = 0; index < 64; ++index)
        if (base64[index] == byte)
            break;
        if (index > 63) index = -1;
            return index;
}

/*
** Decode source from Base64 encoded string into raw data
**
** Returns: 0 - Success
** 1 - Error - Source underflow - need more base64 data
** 2 - Error - Chunk contains half a byte of data
** 3 - Error - Decoded results will overflow output buffer
*/
static int decode(unsigned s_len, char *src, unsigned d_len, char *dst)
{
    unsigned six, dix;

    dix = 0;

    for (six = 0; six < s_len; six += 4) {
        unsigned long sr;
        unsigned ix;

        sr = 0;
        for (ix = 0; ix < 4; ++ix) {
            int sextet;

            if (six+ix >= s_len)
                return 1;
            if ((sextet = tlu(*(src+six+ix))) < 0)
                break;
            sr <<= 6;
            sr |= (sextet & 0x3f);
        }

        switch (ix) {
        case 0: /* end of data, no padding */
            return 0;

        case 1: /* can't happen */
            return 2;

        case 2: /* 1 result byte */
            sr >>= 4;
            if (dix > d_len) 
                return 3;
            *(dst+dix) = (sr & 0xff);
            ++dix;
            break;

        case 3: /* 2 result bytes */
            sr >>= 2;
            if (dix+1 > d_len) 
                return 3;
            *(dst+dix+1) = (sr & 0xff);
            sr >>= 8;
            *(dst+dix) = (sr & 0xff);
            dix += 2;
            break;

        case 4: /* 3 result bytes */
            if (dix+2 > d_len) 
                return 3;
            *(dst+dix+2) = (sr & 0xff);
            sr >>= 8;
            *(dst+dix+1) = (sr & 0xff);
            sr >>= 8;
            *(dst+dix) = (sr & 0xff);
            dix += 3;
            break;
        }
    }
    return 0;
}
