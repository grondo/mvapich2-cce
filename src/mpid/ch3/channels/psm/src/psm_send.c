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

#define _GNU_SOURCE

#include "psmpriv.h"
#include "mpidpre.h"

/* send packet: if Ssend call, add MQ flag.
                if MT issue isend and return with flag 
                if ST do blocking send, update cc_ptr
*/

#undef FUNCNAME
#define FUNCNAME psm_send_pkt
#undef FCNAME
#define FCNAME MPIDI_QUOTE(FUNCNAME)
psm_error_t psm_send_pkt(MPID_Request **rptr, MPIDI_Message_match m, 
                 int dest, void *buf, int buflen)
{
    psm_error_t psmerr;
    uint64_t stag = 0;
    uint32_t flags = MQ_FLAGS_NONE;
    MPID_Request *req = *rptr;
    uint8_t blocking = 1;

    MAKE_PSM_SELECTOR(stag, m.parts.context_id, m.parts.tag, m.parts.rank);
    if(req && req->psm_flags & PSM_SYNC_SEND) {
        DBG("sync send psm\n");
        flags = PSM_MQ_FLAG_SENDSYNC;
        blocking = 0;
    }

    DBG("psm_mq_send: ctx = %d tag = %d\n", m.parts.context_id, m.parts.tag);
    DBG("psm_mq_send: dst = %d src = %d\n", dest, m.partsrank);

    if(blocking && !CAN_BLK_PSM(buflen))
        blocking = 0;

    if(blocking) {
        DBG("blocking send\n");
        _psm_enter_;
        psmerr = psm_mq_send(psmdev_cw.mq, psmdev_cw.epaddrs[dest],
                flags, stag, buf, buflen);
        _psm_exit_;
        if(req) {
            *(req->cc_ptr) = 0;
        }
    } else {
        if(!req) {
            DBG("psm_send_pkt created new req\n");
            req = psm_create_req();
            req->kind = MPID_REQUEST_SEND;
            *rptr = req;
        }

        req->psm_flags |= PSM_NON_BLOCKING_SEND;
        DBG("nb send posted for blocking mpi_send\n");
        _psm_enter_;
        psmerr = psm_mq_isend(psmdev_cw.mq, psmdev_cw.epaddrs[dest],
                flags, stag, buf, buflen, req, &(req->mqreq));
        _psm_exit_;
        ++psm_tot_sends;
    }

    return psmerr;
}

/* isend:
        if issend, append MQ flag
        issue isend 
*/

psm_error_t psm_isend_pkt(MPID_Request *req, MPIDI_Message_match m, 
                  int dest, void *buf, int buflen)
{
    uint64_t stag = 0;
    uint32_t flags = MQ_FLAGS_NONE;
    psm_error_t psmerr;
    
    MAKE_PSM_SELECTOR(stag, m.parts.context_id, m.parts.tag, m.parts.rank);
    assert(req);
    if(req->psm_flags & PSM_SYNC_SEND) {
        DBG("sync Isend psm\n");
        flags = PSM_MQ_FLAG_SENDSYNC;
    }

    assert(dest < psmdev_cw.pg_size);
    DBG("psm_mq_isend: ctx = %d tag = %d\n", m.context_id, m.tag);
    DBG("psm_mq_isend: dst = %d src = %d\n", dest, m.rank);

    _psm_enter_;
    psmerr = psm_mq_isend(psmdev_cw.mq, psmdev_cw.epaddrs[dest], 
            flags, stag, buf, buflen, req, &(req->mqreq));
    _psm_exit_;
    ++psm_tot_sends;
    return psmerr;
}

/* create a new MPID_Request */

MPID_Request * psm_create_req()
{
    MPID_Request *req = MPID_Request_create();
    MPIU_Object_set_ref(req, 2);
    return req;
}
