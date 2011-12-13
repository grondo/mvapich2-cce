#define BENCHMARK "OSU One Sided MPI_Get Passive Bandwidth Test"
/*
 * Copyright (C) 2003-2011 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University.
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 */

/*
This program is available under BSD licensing.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

(1) Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

(2) Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

(3) Neither the name of The Ohio State University nor the names of
their contributors may be used to endorse or promote products derived
from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#include <mpi.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <getopt.h>

#define MAX_ALIGNMENT 65536
#define MAX_MSG_SIZE (1<<22)

/* Note we have a upper limit for buffer size, so be extremely careful
 * if you want to change the loop size or warm up size */
int loop = 100;
int window_size = 32;
int skip = 20;

int loop_large = 30;
int window_size_large = 32;
int skip_large = 10;

int large_message_size = 8192;

#ifdef PACKAGE_VERSION
#   define HEADER "# " BENCHMARK " v" PACKAGE_VERSION "\n"
#else
#   define HEADER "# " BENCHMARK "\n"
#endif

#ifndef FIELD_WIDTH
#   define FIELD_WIDTH 20
#endif

#ifndef FLOAT_PRECISION
#   define FLOAT_PRECISION 2
#endif

int main (int argc, char *argv[])
{
    char        *s_buf=NULL, *r_buf=NULL;
    char        *s_buf1=NULL, *r_buf1=NULL;
    int         myid, numprocs, i, j;
    int         size, page_size, no_hints = 0;
    double      t_start = 0.0, t_end = 0.0, t = 0.0;
    MPI_Win     win;
    MPI_Info    win_info;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &myid);

    if (numprocs != 2) {
        if (myid == 0) {
            fprintf(stderr, "This test requires exactly two processes\n");
        }

        MPI_Finalize();

        return EXIT_FAILURE;
    }

    while (1) {
        static struct option long_options[] =
            {{"no-hints", no_argument, NULL, 'n'},
             {0, 0, 0, 0}};
        int option, index;

        option = getopt_long (argc, argv, "n::",
                            long_options, &index);

        if (option == -1) {
            break;
        }

        switch (option) {
            case 'n':
                no_hints = 1;
                break;
            default:
                if (myid == 0) {
                    fprintf(stderr, "Invalid Option \n");
                }
                MPI_Finalize();
                return EXIT_FAILURE;
        }
    }

    page_size = getpagesize();
    assert(page_size <= MAX_ALIGNMENT);

    if (no_hints == 0) {
        /* Providing MVAPICH2 specific hint to allocate memory 
         * in shared space. MVAPICH2 optimizes communication          
         * on windows created in this memory */
        MPI_Info_create(&win_info);
        MPI_Info_set(win_info, "alloc_shm", "true");

        MPI_Alloc_mem((MAX_MSG_SIZE*window_size + MAX_ALIGNMENT), 
                        win_info, &s_buf1);
    } else {
        MPI_Alloc_mem((MAX_MSG_SIZE*window_size + MAX_ALIGNMENT), 
                        MPI_INFO_NULL, &s_buf1);
    }
    if (NULL == s_buf1) {
		fprintf(stderr, "Allocation failed \n");
		MPI_Finalize();
		return EXIT_FAILURE;
    } 
    MPI_Alloc_mem((MAX_MSG_SIZE*window_size + MAX_ALIGNMENT), MPI_INFO_NULL, &r_buf1);
    if (NULL == r_buf1) {
		fprintf(stderr, "Allocation failed \n");
		MPI_Finalize();
		return EXIT_FAILURE;
    }  
    

    s_buf =
        (char *) (((unsigned long) s_buf1 + (page_size - 1)) / page_size *
                  page_size);
    r_buf =
        (char *) (((unsigned long) r_buf1 + (page_size - 1)) / page_size *
                  page_size);

    assert((s_buf != NULL) && (r_buf != NULL));

    if (myid == 0) {
        fprintf(stdout, HEADER);
        fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH,
                "Bandwidth (MB/s)");
        fflush(stdout);
    }

    /* Bandwidth test */
    for (size = 1; size <= MAX_MSG_SIZE; size *= 2) {
        if (size > large_message_size) {
            loop = loop_large;
            skip = skip_large;
            window_size = window_size_large;
        }

        /* Window creation and warming-up */
        MPI_Win_create(s_buf, size * window_size, 1, MPI_INFO_NULL,
                MPI_COMM_WORLD, &win);

        if (myid == 0) {
            for (i = 0; i < skip + loop; i++) {
                if (i == skip) {
                    t_start = MPI_Wtime ();
                }
                MPI_Win_lock(MPI_LOCK_EXCLUSIVE, 1, 0, win); 

                for(j = 0; j < window_size; j++) {
                    MPI_Get(r_buf+(j * size), size, MPI_CHAR, 1, j * size, size, MPI_CHAR,
                            win);
                }

                MPI_Win_unlock(1, win); 
            }

            t_end = MPI_Wtime();
            t = t_end - t_start;
        } 

        MPI_Barrier(MPI_COMM_WORLD);

        if (myid == 0) {
            double tmp = size / 1e6 * loop * window_size;
            fprintf(stdout, "%-*d%*.*f\n", 10, size, FIELD_WIDTH,
                    FLOAT_PRECISION, tmp / t);
            fflush(stdout);
        }

        MPI_Win_free(&win);
    }     

    if (no_hints == 0) {
        MPI_Info_free(&win_info);
    }
    
    MPI_Free_mem(s_buf1);
	MPI_Free_mem(r_buf1);
    MPI_Finalize();

    return EXIT_SUCCESS;
}

/* vi: set sw=4 sts=4 tw=80: */
