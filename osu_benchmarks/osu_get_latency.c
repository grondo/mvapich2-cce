#define BENCHMARK "OSU One Sided MPI_Get latency Test"
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
#include <string.h>
#include <assert.h>
#include <getopt.h>

#define MAX_ALIGNMENT 65536
#define MAX_SIZE (1<<22)
#define MYBUFSIZE (MAX_SIZE + MAX_ALIGNMENT)

int skip = 1000;
int loop = 10000;
int skip_large = 10;
int loop_large = 100;
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
    int         rank, destrank, nprocs, i;
    int         size, page_size, no_hints = 0;
    char       *A, *B;
    char       *s_buf, *r_buf;
    MPI_Group   comm_group, group;
    MPI_Win     win;
    MPI_Info    win_info;    
    double      t_start=0.0, t_end=0.0;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (nprocs != 2) {
        if (rank == 0) {
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
                if (rank == 0) {
                    fprintf(stderr, "Invalid Option \n");
                }
                MPI_Finalize();
                return EXIT_FAILURE;
        }
    }

    if (no_hints == 0) {
        /* Providing MVAPICH2 specific hint to allocate memory 
         * in shared space. MVAPICH2 optimizes communication          
         * on windows created in this memory */
        MPI_Info_create(&win_info);
        MPI_Info_set(win_info, "alloc_shm", "true");

        MPI_Alloc_mem (MYBUFSIZE, win_info, &A);
    } else {
        MPI_Alloc_mem (MYBUFSIZE, MPI_INFO_NULL, &A);
    }
    if (NULL == A) {
         fprintf(stderr, "[%d] Buffer Allocation Failed \n", rank);
         exit(-1);
    }

    MPI_Alloc_mem (MYBUFSIZE, MPI_INFO_NULL, &B);
    if (NULL == B) {
         fprintf(stderr, "[%d] Buffer Allocation Failed \n", rank);
         exit(-1);
    }

    page_size = getpagesize();
    assert(page_size <= MAX_ALIGNMENT);

    s_buf =
        (char *) (((unsigned long) A + (page_size - 1)) /
                  page_size * page_size);
    r_buf =
        (char *) (((unsigned long) B + (page_size - 1)) /
                  page_size * page_size);

    memset(r_buf, 0, MAX_SIZE);
    memset(s_buf, 1, MAX_SIZE);

    if (rank == 0) {
        fprintf(stdout, HEADER);
        fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "Latency (us)"); 
        fflush(stdout);
    }

    MPI_Comm_group(MPI_COMM_WORLD, &comm_group);

    for (size = 0; size <= MAX_SIZE; size = (size ? size * 2 : 1)) {
        if (size > large_message_size) {
            loop = loop_large;
            skip = skip_large;
        }

        MPI_Win_create(s_buf, size, 1, MPI_INFO_NULL, MPI_COMM_WORLD, &win);

        if (rank == 0) {
            destrank = 1;

            MPI_Group_incl(comm_group, 1, &destrank, &group);
            MPI_Barrier(MPI_COMM_WORLD);

            for (i = 0; i < skip + loop; i++) {
                MPI_Win_start(group, 0, win);

                if (i == skip) {
                    t_start = MPI_Wtime ();
                }

                MPI_Get(r_buf, size, MPI_CHAR, 1, 0, size, MPI_CHAR, win);
                MPI_Win_complete(win);
                MPI_Win_post(group, 0, win);
                MPI_Win_wait(win);
            }

            t_end = MPI_Wtime ();
        } else {
            /* rank=1 */
            destrank = 0;

            MPI_Group_incl(comm_group, 1, &destrank, &group);
            MPI_Barrier(MPI_COMM_WORLD);

            for (i = 0; i < skip + loop; i++) {
                MPI_Win_post(group, 0, win);
                MPI_Win_wait(win);
                MPI_Win_start(group, 0, win);
                MPI_Get(r_buf, size, MPI_CHAR, 0, 0, size, MPI_CHAR, win);
                MPI_Win_complete(win);
            }
        }

        MPI_Barrier(MPI_COMM_WORLD);

        if (rank == 0) {
            fprintf(stdout, "%-*d%*.*f\n", 10, size, FIELD_WIDTH,
                    FLOAT_PRECISION, (t_end - t_start) * 1.0e6 / loop / 2);
            fflush(stdout);
        }

        MPI_Group_free(&group);
        MPI_Win_free(&win);
    }

    if (no_hints == 0) {
        MPI_Info_free(&win_info);
    }

    MPI_Free_mem(A);
    MPI_Free_mem(B);
 
    MPI_Group_free(&comm_group);
    MPI_Finalize();

    return EXIT_SUCCESS;
}

/* vi: set sw=4 sts=4 tw=80: */
