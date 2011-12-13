/*
   (C) 2004 by Argonne National Laboratory.
       See COPYRIGHT in top-level directory.
*/
#include "collchk.h" 

int MPI_File_read_all_end(MPI_File fh, void *buff, MPI_Status *st)
{
    int g2g = 1;
    char call[COLLCHK_SM_STRLEN];
    char err_str[COLLCHK_STD_STRLEN];
    MPI_Comm comm;

    sprintf(call, "FILE_READ_ALL_END");

    /* Check if init has been called */
    g2g = CollChk_is_init();

    if(g2g) {
        /* get the communicator */
        if (!CollChk_get_fh(fh, &comm)) {
            return CollChk_err_han("File has not been opened",
                                   COLLCHK_ERR_FILE_NOT_OPEN,
                                   call, MPI_COMM_WORLD);
        }

        /* check for call consistancy */
        CollChk_same_call(comm, call);

        /* check for previous begin */
        if(COLLCHK_CALLED_BEGIN) {
            if(strcmp(CollChk_begin_str, "READ_ALL") != 0) {
                sprintf(err_str, "Begin does not match end (MPI_File_%s_begin)",
                        CollChk_begin_str);
                return CollChk_err_han(err_str, COLLCHK_ERR_PREVIOUS_BEGIN,
                                       call, comm);
            }
            else {
                CollChk_unset_begin();
            }
        }
        else {
            sprintf(err_str, "MPI_File_%s_begin has not been called",
                             CollChk_begin_str);
            return CollChk_err_han(err_str, COLLCHK_ERR_PREVIOUS_BEGIN,
                                   call, comm);
        }

        /* make the call */
        return PMPI_File_read_all_end(fh, buff, st);
    }
    else {
        /* init not called */
        return CollChk_err_han("MPI_Init() has not been called!",
                               COLLCHK_ERR_NOT_INIT, call, MPI_COMM_WORLD);
    }
}
