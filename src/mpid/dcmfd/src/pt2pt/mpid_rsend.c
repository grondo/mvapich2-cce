/*  (C)Copyright IBM Corp.  2007, 2008  */
/**
 * \file src/pt2pt/mpid_rsend.c
 * \brief ADI level implemenation of MPI_Rsend()
 */
#include "mpidimpl.h"

/**
 * \brief ADI level implemenation of MPI_Rsend()
 *
 * \param[in]  buf            The buffer to send
 * \param[in]  count          Number of elements in the buffer
 * \param[in]  datatype       The datatype of each element
 * \param[in]  rank           The destination rank
 * \param[in]  tag            The message tag
 * \param[in]  comm           Pointer to the communicator
 * \param[in]  context_offset Offset from the communicator context ID
 * \param[out] request        Return a pointer to the new request object
 *
 * \returns An MPI Error code
 *
 * Semantics of Rsend are undefined if the receiver has not posted a
 * receive. We define this "undefined" behavior to look like the
 * normal mode send behavior.
 */
int MPID_Rsend(const void    * buf,
               int             count,
               MPI_Datatype    datatype,
               int             rank,
               int             tag,
               MPID_Comm     * comm,
               int             context_offset,
               MPID_Request ** request)
{
  return MPID_Send(buf,
                   count,
                   datatype,
                   rank,
                   tag,
                   comm,
                   context_offset,
                   request);
}
