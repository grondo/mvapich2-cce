#ifndef _MPIRUN_RSH_H
#define _MPIRUN_RSH_H 1
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

/* if this is defined, we will use the VI network for
 * the termination barrier, and not the socket based
 * barrier.
 */
#if 0
#define USE_VIADEV_BARRIER  1
#endif

#define BASE_ENV_LEN        17
#define COMMAND_LEN        20000

#define ENV_CMD            "/usr/bin/env"
#define RSH_CMD            "/usr/bin/rsh"
#define SSH_CMD            "/usr/bin/ssh"
#define XTERM            "/usr/bin/xterm"
#define SSH_ARG            "-q"
#define RSH_ARG            "NOPARAM"
#define BASH_CMD        "/bin/bash"
#define BASH_ARG        "-c"

#ifdef USE_DDD
#define DEBUGGER        "/usr/bin/ddd"
#else
#define DEBUGGER        "/usr/bin/gdb"
#endif

#define _GNU_SOURCE
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#ifdef MAC_OSX
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <assert.h>
#include <libgen.h>
#include "mpirun_util.h"
#include <mpirunconf.h>

#define PRINT_MVAPICH2_VERSION() printf("Version: mvapich2-" MVAPICH2_VERSION "\n")

#define PMGR_VERSION PMGR_COLLECTIVE

#define MAXLINE 1024
typedef struct {
    int totspawns;
    int spawnsdone;
    int dpmtot;
    int dpmindex;
    int launch_num;
    char buf[MAXLINE];
    char linebuf[MAXLINE];
    char runbuf[MAXLINE];
    char argbuf[MAXLINE];
    char *spawnfile;
} spawn_info_t;

/*This list is used for dpm to take care of the mpirun_rsh started.*/
typedef struct list_pid_mpirun {
    pid_t pid;
    struct list_pid_mpirun *next;
} list_pid_mpirun_t;

extern int NSPAWNS;

#define RUNNING(i) ((plist[i].state == P_STARTED ||                 \
            plist[i].state == P_CONNECTED ||                        \
            plist[i].state == P_RUNNING) ? 1 : 0)

/* other information: a.out and rank are implicit. */

#define SEPARATOR ':'

#ifndef PARAM_GLOBAL
#define PARAM_GLOBAL "/etc/mvapich.conf"
#endif

#define TOTALVIEW_CMD "/usr/totalview/bin/totalview"
#endif

int handle_spawn_req(int readsock);
void mpispawn_checkin(int);

/* vi:set sw=4 sts=4 tw=80: */
