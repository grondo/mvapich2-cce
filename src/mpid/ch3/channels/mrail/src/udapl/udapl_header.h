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

#ifndef _UDAPL_HEADER_H
#define _UDAPL_HEADER_H

#include "mpidi_ch3i_rdma_conf.h"

#include "udapl_param.h"
#include "udapl_arch.h"
#include <inttypes.h>
#include <stdlib.h>

#include DAT_HEADER

#define INVAL_HNDL (0xffffffff)

#define MAX_WIN_NUM           (16)
#define SIGNAL_FOR_PUT        (1)
#define SIGNAL_FOR_GET        (2)
#define SIGNAL_FOR_LOCK_ACT   (3)
#define SIGNAL_FOR_DECR_CC    (4)

/* memory handle */
typedef struct
{
    DAT_LMR_HANDLE hndl;
    DAT_LMR_CONTEXT lkey;
    DAT_RMR_CONTEXT rkey;
} VIP_MEM_HANDLE;

#if 0
#define D_PRINT(fmt, args...)	{fprintf(stderr, "[%d][%s:%d]", viadev.me, __FILE__, __LINE__);\
				 fprintf(stderr, fmt, ## args); fflush(stderr);}
#else
#define D_PRINT(fmt, args...)
#endif

#endif
