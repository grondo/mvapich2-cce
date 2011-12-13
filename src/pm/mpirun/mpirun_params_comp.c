
/*RAM
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

#include <mpirunconf.h>
#include "mpirun_rsh.h"
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <math.h>
#include "mpispawn_tree.h"
#include "mpirun_util.h"
#include "mpmd.h"
#include "mpirun_dbg.h"
#include "mpirun_params.h"
#include "mpirun_ckpt.h"

#if defined(_NSIG)
#define NSIG _NSIG
#endif                          /* defined(_NSIG) */

extern int read_hostfile(char *hostfile_name);

process *plist = NULL;
int nprocs = 0;
int aout_index = 0;

/* xxx need to add checking for string overflow, do this more carefully ... */
char *mpispawn_param_env = NULL;
char *spawnfile;
char *binary_dirname;

#if defined(USE_RSH)
int use_rsh = 1;
#else                           /* defined(USE_RSH) */
int use_rsh = 0;
#endif                          /* defined(USE_RSH) */

int xterm_on = 0;
int show_on = 0;
int use_dirname = 1;
int hostfile_on = 0;
int param_count = 0;
int legacy_startup = 0;
int dpm = 0;
extern spawn_info_t spinf;
int USE_LINEAR_SSH = 1;         /* By default, use linear ssh. Enable
                                   -fastssh for tree based ssh */

char hostfile[HOSTFILE_LEN + 1];

/*
  The group active for mpispawn. NULL if no group change is required.
 */
char *change_group = NULL;

#define PARAM_NP    0
#define PARAM_SG    17
#define PARAM_N     18
#define PARAM_ENV   19

static struct option option_table[] = {
    {"np", required_argument, 0, 0},
    {"gdb", no_argument, 0, 0},
    {"xterm", no_argument, 0, 0},
    {"machinefile", required_argument, 0, 0},
    {"show", no_argument, 0, 0},
    {"rsh", no_argument, 0, 0},
    {"ssh", no_argument, 0, 0},
    {"help", no_argument, 0, 0},
    {"v", no_argument, 0, 0},
    {"tv", no_argument, 0, 0},
    {"legacy", no_argument, 0, 0},
    {"startedByTv", no_argument, 0, 0},
    {"spawnfile", required_argument, 0, 0},
    {"dpm", no_argument, 0, 0},
    {"fastssh", no_argument, 0, 0},
    //This option is to activate the mpmd, it requires the configuration file as argument
    {"config", required_argument, 0, 0},
    {"dpmspawn", required_argument, 0, 0},
    // This option enables the group selection for mpispawns
    {"sg", required_argument, 0, 0},
    {"n", required_argument, 0, 0},
    {"env", required_argument, 0, 0},
    {0, 0, 0, 0}
};

#if !defined(HAVE_GET_CURRENT_DIR_NAME)
char *get_current_dir_name()
{
    struct stat64 dotstat;
    struct stat64 pwdstat;
    char *pwd = getenv("PWD");

    if (pwd != NULL && stat64(".", &dotstat) == 0 && stat64(pwd, &pwdstat) == 0 && pwdstat.st_dev == dotstat.st_dev && pwdstat.st_ino == dotstat.st_ino) {
        /* The PWD value is correct. */
        return strdup(pwd);
    }

    size_t size = 1;
    char *buffer;

    for (;; ++size) {
        buffer = malloc(size);

        if (!buffer) {
            return NULL;
        }

        if (getcwd(buffer, size) == buffer) {
            break;
        }

        free(buffer);

        if (errno != ERANGE) {
            return NULL;
        }
    }

    return buffer;
}
#endif                          /* !defined(HAVE_GET_CURRENT_DIR_NAME) */

#if !defined(HAVE_STRNDUP)
char *strndup(const char *s, size_t n)
{
    size_t len = strlen(s);

    if (n < len) {
        len = n;
    }

    char *result = malloc(len + 1);

    if (!result) {
        return NULL;
    }

    result[len] = '\0';
    return memcpy(result, s, len);
}
#endif                          /* !defined(HAVE_STRNDUP) */

#define PARAMFILE_LEN 256

static void check_option(int argc, char *argv[], int option_index, char *totalview_cmd)
{

    char *tmp;

    switch (option_index) {
    case PARAM_NP:             /* -np */
    case PARAM_N:              /* -n */
        nprocs = atoi(optarg);
        if (nprocs < 1) {
            usage();
            exit(EXIT_FAILURE);
        }
        break;
    case 1:                    /* -debug */
        debug_on = 1;
        xterm_on = 1;
        break;
    case 2:                    /* -xterm */
        xterm_on = 1;
        break;
    case 3:                    /* -hostfile */
        hostfile_on = 1;
        strncpy(hostfile, optarg, HOSTFILE_LEN);
        if (strlen(optarg) >= HOSTFILE_LEN - 1)
            hostfile[HOSTFILE_LEN] = '\0';
        break;
    case 4:
        show_on = 1;
        break;
    case 5:
        use_rsh = 1;
        break;
    case 6:
        use_rsh = 0;
        break;
    case 7:
        usage();
        exit(EXIT_SUCCESS);
        break;
    case 8:
        PRINT_MVAPICH2_VERSION();
        exit(EXIT_SUCCESS);
        break;
    case 9:
        {
            /* -tv */
            char *tv_env;
            int count, idx;
            char **new_argv;
            tv_env = getenv("TOTALVIEW");
            if (tv_env != NULL) {
                strncpy(totalview_cmd, tv_env, TOTALVIEW_CMD_LEN);
            } else {
                fprintf(stderr, "TOTALVIEW env is NULL, use default: %s\n", TOTALVIEW_CMD);
                sprintf(totalview_cmd, "%s", TOTALVIEW_CMD);
            }
            new_argv = (char **) malloc(sizeof(char **) * argc + 3);
            new_argv[0] = totalview_cmd;
            new_argv[1] = argv[0];
            new_argv[2] = "-a";
            new_argv[3] = "-startedByTv";
            idx = 4;
            for (count = 1; count < argc; count++) {
                if (strcmp(argv[count], "-tv"))
                    new_argv[idx++] = argv[count];
            }
            new_argv[idx] = NULL;
            if (execv(new_argv[0], new_argv)) {
                perror("execv");
                exit(EXIT_FAILURE);
            }

        }
        break;
    case 10:
        legacy_startup = 1;
        break;
    case 11:
        /* -startedByTv */
        use_totalview = 1;
        debug_on = 1;
        break;
    case 12:                   /* spawnspec given */
        spawnfile = strdup(optarg);
        DBG(fprintf(stderr, "spawn spec file = %s\n", spawnfile));
        break;
    case 13:
        dpm = 1;
        break;
    case 14:                   /* -fastssh */
#ifndef CKPT
        USE_LINEAR_SSH = 0;
#endif                          /* CKPT */
        break;
        //With this option the user want to activate the mpmd
    case 15:
        configfile_on = 1;
        strncpy(configfile, optarg, CONFILE_LEN);
        if (strlen(optarg) >= CONFILE_LEN - 1)
            configfile[CONFILE_LEN] = '\0';
        break;
    case 16:
        spinf.totspawns = atoi(optarg);
        break;
    case PARAM_SG:
        /* sg: change the active group */
        change_group = optarg;
        DBG(printf("Group change requested: '%s'\n", change_group));
        break;
    case PARAM_ENV:
        /* -env */
        if (mpispawn_param_env) {
            tmp = mkstr("%s MPISPAWN_GENERIC_NAME_%d=%s" " MPISPAWN_GENERIC_VALUE_%d=%s", mpispawn_param_env, param_count, argv[optind - 1], param_count, argv[optind]);

            free(mpispawn_param_env);
        } else {
            tmp = mkstr("MPISPAWN_GENERIC_NAME_%d=%s" " MPISPAWN_GENERIC_VALUE_%d=%s", param_count, argv[optind - 1], param_count, argv[optind]);
        }

        if (tmp) {
            mpispawn_param_env = tmp;
            param_count++;
        } else {
            fprintf(stderr, "malloc failed in read_param_file\n");
            exit(EXIT_FAILURE);
        }
        optind++;
        break;
    case 20:
        usage();
        exit(EXIT_SUCCESS);
        break;
    default:
        fprintf(stderr, "Unknown option\n");
        usage();
        exit(EXIT_FAILURE);
        break;
    }
}

/**
 * Command line analysis function.
 *
 * mpirun [-debug] [-xterm] -np N [-hostfile hfile] a.out [args]
 */
void commandLine(int argc, char *argv[], char *totalview_cmd, char **env)
{
    int i;
    int c, option_index;

    size_t hostname_len = 0;

    do {
        c = getopt_long_only(argc, argv, "+", option_table, &option_index);
        switch (c) {
        case '?':
        case ':':
            usage();
            exit(EXIT_FAILURE);
            break;
        case EOF:
            break;
        case 0:
            check_option(argc, argv, option_index, totalview_cmd);
            break;
        default:
            fprintf(stderr, "Unreachable statement!\n");
            usage();
            exit(EXIT_FAILURE);
            break;
        }
    }
    while (c != EOF);

    if (!nprocs && !configfile_on) {
        usage();
        exit(EXIT_FAILURE);
    }

    binary_dirname = dirname(strdup(argv[0]));
    if (strlen(binary_dirname) == 1 && argv[0][0] != '.') {
        use_dirname = 0;
    }
    //If the mpmd is active we need to parse the configuration file
    if (configfile_on) {
        /*TODO In the future the user can add the nprocs on the command line. Now the
         * number of processes is defined in the configfile */
        nprocs = 0;
        plist = parse_config(configfile, &nprocs);
        DBG(fprintf(stderr, "PARSED CONFIG FILE\n"));

    }

    /*
     * There is no hostfile given
     */
    if (!hostfile_on) {
        aout_index = optind;

        /*
         * Check for default hostfile
         */
        sprintf(hostfile, "%s/.mpirun_hosts", env2str("HOME"));
        if (file_exists(hostfile)) {
            hostfile_on = 1;
        }
    }

    if (!configfile_on) {
        plist = malloc(nprocs * sizeof(process));
        if (plist == NULL) {
            perror("malloc");
            exit(EXIT_FAILURE);
        }

        for (i = 0; i < nprocs; i++) {
            plist[i].state = P_NOTSTARTED;
            plist[i].device = NULL;
            plist[i].port = -1;
            plist[i].remote_pid = 0;
            //TODO ADD EXECNAME AND ARGS

        }
    }

    /* grab hosts from command line or file */
    /* TODO: remove  hostname_len variable */
    hostname_len = 0;
    if (hostfile_on) {
        hostname_len = read_hostfile(hostfile);
    }

    /*
     * Use localhost since default hostfile does not exist
     */
    else {
        for (i = 0; i < nprocs; i++) {
            plist[i].hostname = (char *) strdup("localhost");
            if (hostname_len < strlen(plist[i].hostname)) {
                hostname_len = strlen(plist[i].hostname);
            }
        }
    }
}

void usage(void)
{
    fprintf(stderr, "usage: mpirun_rsh [-v] [-sg group] [-rsh|-ssh] " "[-gdb] -[tv] [-xterm] [-show] [-legacy] -n N" "[-machinefile hfile] a.out args | -config configfile [-hostfile hfile]\n");
    fprintf(stderr, "Where:\n");
    fprintf(stderr, "\tsg         =>  execute the processes as different group ID\n");
    fprintf(stderr, "\trsh        =>  to use rsh for connecting\n");
    fprintf(stderr, "\tssh        =>  to use ssh for connecting\n");
    fprintf(stderr, "\tgdb        =>  run each process under the control of gdb\n");
    fprintf(stderr, "\ttv         =>  run each process under the control of TotalView\n");
    fprintf(stderr, "\txterm      =>  run remote processes under xterm\n");
    fprintf(stderr, "\tshow       =>  show command for remote execution but don't run it\n");
    fprintf(stderr, "\tlegacy     =>  use old startup method (1 ssh/process)\n");
    fprintf(stderr, "\tnp         =>  specify the number of processes\n");
    fprintf(stderr, "\thfile      =>  name of file containing hosts, one per line\n");
    fprintf(stderr, "\ta.out      =>  name of MPI binary\n");
    fprintf(stderr, "\targs       =>  arguments for MPI binary\n");
    fprintf(stderr, "\tconfig     =>  name of file containing the exe information: each line has the form -n numProc : exe args\n");
    fprintf(stderr, "\n");
}

int file_exists(char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (fp) {
        fclose(fp);
        return 1;
    }
    return 0;
}

/* vi:set sw=4 sts=4 tw=76 expandtab: */
