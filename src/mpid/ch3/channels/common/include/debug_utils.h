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

#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <stdio.h>
#include <errno.h>
#include <string.h>


// Define a prefix to distinguish output from different processes
// - prefix: string
extern void set_output_prefix( char* prefix );

// Get the output prefix
extern const char *get_output_prefix();


// Common print function
#define _COMMON_PRINT_( FMT, args... ) \
do { fprintf( stderr, "[%s][%s] "FMT, get_output_prefix(), __func__, ##args ); } while(0)


// Print (always) an error message 
#define PRINT_ERROR( FMT, args... ) \
do { _COMMON_PRINT_( FMT, ##args ); } while(0)

// Print (always) an error message with the errno error message
#define MAX_ERR_MSG 200
#define PRINT_ERROR_ERRNO( FMT, ERRCODE, args... )                \
do {                                                              \
  char err_msg[MAX_ERR_MSG];                                      \
  strerror_r( ERRCODE, err_msg, MAX_ERR_MSG );                    \
  _COMMON_PRINT_( FMT": %s (%d)\n", ##args, err_msg, ERRCODE );   \
} while(0)

// Check condition and if true, print the message
#define PRINT_INFO( COND, FMT, args... )  \
do {                                      \
    if ( COND ) {                         \
        _COMMON_PRINT_( FMT, ##args );    \
    }                                     \
} while(0)

// Check condition and if failed:
// - print an error message with the errno error message
// - abort
#define CHECK_ERROR_ERRNO( COND, FMT, errno, args... )   \
do {                                       \
    if( !(COND) ) {   \
        PRINT_ERROR_ERRNO( "At %s:%d: "FMT, errno, __FILE__, __LINE__, ##args );     \
        abort();                           \
}} while(0)


#if !defined(NDEBUG)

// (Only when debug enabled) 
// Check assertion and if failed:
// - print an error message 
// - abort
#define ASSERT_MSG( COND, FMT, args... )   \
do {                                       \
    if( !(COND) ) {   \
        _COMMON_PRINT_( "At %s:%d: "FMT, __FILE__, __LINE__, ##args );     \
        abort();                           \
}} while(0)


// (Only when debug enabled) 
// Check condition and if true, print the debug message 
#define PRINT_DEBUG( COND, FMT, args... ) \
do {                                      \
    if ( COND ) {   \
        _COMMON_PRINT_( FMT, ##args );    \
    }                                     \
} while(0)

#else

#define ASSERT_MSG( COND, FMT, args... )
#define PRINT_DEBUG( COND, FMT, args... )

#endif




// Verbosity level for fork/kill/waitpid operations in mpirun_rsh and mpispawn
extern int DEBUG_Fork_verbose;

// Verbosity level for Fault Tolerance operations
extern int DEBUG_FT_verbose;

// Verbosity level for Migration operations
extern int DEBUG_MIG_verbose;

// Verbosity level for UD flow control
extern int DEBUG_UD_verbose;

// Verbosity level for UD ZCOPY Rndv
extern int DEBUG_ZCY_verbose;

// Verbosity level for On-Demand Connection Management
extern int DEBUG_CM_verbose;

// Verbosity level for XRC
extern int DEBUG_XRC_verbose;

// Verbosity level for UD stats
extern int DEBUG_UDSTAT_verbose;

// Verbosity level for memory stats
extern int DEBUG_MEM_verbose;

// Initialize the verbosity level of the above variables
extern int initialize_debug_variables();

extern void print_mem_usage();

#endif
