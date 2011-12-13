/* Copyright (c) 200333-2010, The Ohio State University. All rights
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
#ifndef _MPIRUN_CKPT_H
#define _MPIRUN_CKPT_H

#include "mpirunconf.h"

#ifdef CKPT

#include "mpirun_rsh.h"
#include "mpirun_dbg.h"
#include "common_ckpt.h"

// Initialize CR
// - it should be called only once at mpirun_rsh startup
// - it should *not* be called after restart
int CR_initialize();

// Finalize CR
// - it should be called only once at mpirun_rsh ending
// - it should *not* be called before restart
int CR_finalize();

// Start CR thread
// - it can be called again after CR_stop_thread() has been called
// - it should be called after restart
int CR_thread_start( unsigned int nspawns );

// Stop CR thread
// - it can be called again after CR_start_thread() has been called
// - it should be called when restarting
// If blocking is set, it will wait for the CR thread to terminate
int CR_thread_stop( int blocking );


char *create_mpispawn_vars(char *mpispawn_env);
void save_ckpt_vars_env(void);
void save_ckpt_vars(char *, char *);
void set_ckpt_nprocs(int nprocs);

#define CR_SESSION_MAX  16
extern char sessionid[CR_SESSION_MAX];


// =====================================================
// For Migration

#define HOSTFILE_LEN 256
extern int nsparehosts;
extern int sparehosts_on;
extern char sparehostfile[HOSTFILE_LEN + 1];
extern char **sparehosts;
extern struct spawn_info_s *spawninfo;

int read_sparehosts(char *hostfile, char ***hostarr, int *nhosts);

// =====================================================

#endif


#endif
