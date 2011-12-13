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

#ifndef _VIUTIL_H
#define _VIUTIL_H

#include "mpidi_ch3i_rdma_conf.h"

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include "pmi.h"

#define GEN_EXIT_ERR     -1     /* general error which forces us to abort */
#define GEN_ASSERT_ERR   -2     /* general assert error */
#define UDAPL_RETURN_ERR	  -3    /* udapl funtion return error */
#define UDAPL_STATUS_ERR   -4   /* udapl funtion status error */
#define udapl_error_abort(code, message, args...)  {              \
    int my_rank;                                                    \
    PMI_Get_rank(&my_rank);                                         \
    fprintf(stderr, "[%d] Abort: ", my_rank);    \
    fprintf(stderr, message, ##args);                               \
    fprintf(stderr, " at line %d in file %s\n", __LINE__, __FILE__);\
    exit(code);                                                     \
}

#ifdef DEBUG
#define DEBUG_PRINT(args...)                                  \
do {                                                          \
    int rank;                                                 \
    PMI_Get_rank(&rank);                                      \
    fprintf(stderr, "[%d][%s:%d] ", rank, __FILE__, __LINE__);\
    fprintf(stderr, args);                                    \
} while (0)
#else
#undef DEBUG_PRINT
#define DEBUG_PRINT(args...)
#endif 

#define CHECK_UNEXP(ret, s)                           \
do {                                                  \
    if (ret) {                                        \
        fprintf(stderr, "[%s:%d]: %s\n",              \
                __FILE__,__LINE__, s);                \
    exit(1);                                          \
    }                                                 \
} while (0)

#define CHECK_RETURN(ret, s)                            \
do {                                                    \
    if (ret != DAT_SUCCESS) {                               \
    fprintf(stderr, "[%s:%d] error(%x): %s\n",          \
        __FILE__,__LINE__, ret, s);                     \
    exit(1);                                            \
    }                                                   \
}                                                       \
while (0)

#endif /* _VIUTIL_H */
