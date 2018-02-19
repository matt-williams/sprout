/* $Id: rina_transport.cpp 4294 2012-11-06 05:02:10Z nanang $ */
/*
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 * Copyright (C) 2013  Metaswitch Networks Ltd
 *  - Support listening on 2 separate TCP ports
 *  - Manage transport references to avoid premature destruction.
 * Copyright (C) 2018  Metaswitch Networks Ltd
 *  - Add RINA support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

extern "C" {
#include <pjsip/sip_endpoint.h>
#include <pjsip/sip_errno.h>
#include <pj/compat/socket.h>
#include <pj/addr_resolv.h>
#include <pj/activesock.h>
#include <pj/assert.h>
#include <pj/lock.h>
#include <pj/log.h>
#include <pj/os.h>
#include <pj/pool.h>
#include <pj/string.h>
}

#include "rina_transport.h"
#include "rina_transport_int.h"
#include <rina/api.h>

#define THIS_FILE	"rina_transport.cpp"

#define MAX_ASYNC_CNT	16
#define POOL_LIS_INIT	512
#define POOL_LIS_INC	512
#define POOL_TP_INIT	512
#define POOL_TP_INC	512

struct rina_listener;
struct rina_transport;


/*
 * This is the RINA listener, which is a "descendant" of pjsip_tpfactory (the
 * SIP transport factory).
 */
struct rina_listener
{
    pjsip_tpfactory	     factory;
    pj_bool_t		     is_registered;
    pjsip_endpoint	    *endpt;
    pjsip_tpmgr		    *tpmgr;
    int			     rina_fd;
    pj_activesock_t	    *asock;
    pj_str_t                 appl;
    pj_str_t                 dif;

    pj_thread_t             *thread;
    bool                     thread_running;
};


/*
 * This structure is used to keep delayed transmit operation in a list.
 * A delayed transmission occurs when application sends tx_data when
 * the RINA connect/establishment is still in progress. These delayed
 * transmission will be "flushed" once the socket is connected (either
 * successfully or with errors).
 */
struct delayed_tdata
{
    PJ_DECL_LIST_MEMBER(struct delayed_tdata);
    pjsip_tx_data_op_key    *tdata_op_key;
    pj_time_val              timeout;
};


/*
 * This structure describes the RINA transport, and it's descendant of
 * pjsip_transport.
 */
struct rina_transport
{
    pjsip_transport	     base;
    pj_bool_t		     is_server;

    /* Do not save listener instance in the transport, because
     * listener might be destroyed during transport's lifetime.
     * See http://trac.pjsip.org/repos/ticket/491
    struct rina_listener	    *listener;
     */

    pj_bool_t		     is_registered;
    pj_bool_t		     is_closing;
    pj_status_t		     close_reason;
    pj_sock_t		     sock;
    pj_activesock_t	    *asock;

    /* Keep-alive timer. */
    pj_timer_entry	     ka_timer;
    pj_time_val		     last_activity;
    pjsip_tx_data_op_key     ka_op_key;
    pj_str_t		     ka_pkt;

    /* RINA transport can only have  one rdata!
     * Otherwise chunks of incoming PDU may be received on different
     * buffer.
     */
    pjsip_rx_data	     rdata;

    /* The buffer passed to the asock for receiving data. */
    char                     rx_buf[PJSIP_NORMAL_PKT_LEN + 1];

    /* Pool used for large message buffers when required. */
    pj_pool_t               *large_msg_pool;

    /* Pending transmission list. */
    struct delayed_tdata     delayed_list;
};


/****************************************************************************
 * PROTOTYPES
 */

/* This callback is called when pending accept() operation completes. */
static pj_bool_t on_accept_complete(struct rina_listener *listener,
				    pj_sock_t newsock,
				    const pj_sockaddr_t *src_addr,
				    int src_addr_len);

/* This callback is called by transport manager to destroy listener */
static pj_status_t lis_destroy(pjsip_tpfactory *factory);

/* This callback is called by transport manager to create transport */
static pj_status_t lis_create_transport(pjsip_tpfactory *factory,
					pjsip_tpmgr *mgr,
					pjsip_endpoint *endpt,
					const pj_sockaddr *rem_addr,
					int addr_len,
					pjsip_transport **transport);

/* Common function to create and initialize transport */
static pj_status_t rina_create(struct rina_listener *listener,
			      pj_pool_t *pool,
			      pj_sock_t sock, pj_bool_t is_server,
			      const pj_sockaddr *local,
			      const pj_sockaddr *remote,
			      const pjsip_host_port *local_name,
			      struct rina_transport **p_rina);


static void rina_perror(const char *sender, const char *title,
		       pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));

    PJ_LOG(1,(sender, "%s: %s [code=%d]", title, errmsg, status));
}

static void rina_pwarning(const char *sender, const char *title,
		         pj_status_t status)
{
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));

    PJ_LOG(3,(sender, "%s: %s [code=%d]", title, errmsg, status));
}


static void sockaddr_to_host_port( pj_pool_t *pool,
				   pjsip_host_port *host_port,
				   const pj_sockaddr *addr )
{
    host_port->host.ptr = (char*) pj_pool_alloc(pool, PJ_INET6_ADDRSTRLEN+4);
    pj_sockaddr_print(addr, host_port->host.ptr, PJ_INET6_ADDRSTRLEN+4, 0);
    host_port->host.slen = pj_ansi_strlen(host_port->host.ptr);
    host_port->port = pj_sockaddr_get_port(addr);
}

static int lis_thread_proc(struct rina_listener *listener);


/*
 * rina_transport_register_type()
 *
 * Register the RINA transport type with PJSIP
 */
int PJSIP_TRANSPORT_RINA = -1;
static int rina_transport_register_type()
{
  int type;
  pjsip_transport_register_type(PJSIP_TRANSPORT_RELIABLE,
                                "RINA",
                                5060,
                                &type);
  return type;
}


static void rina_init_shutdown(struct rina_transport *rina, pj_status_t status)
{
    pjsip_tp_state_callback state_cb;

    if (rina->close_reason == PJ_SUCCESS)
	rina->close_reason = status;

    if (rina->base.is_shutdown || rina->base.is_destroying)
	return;

    /* Prevent immediate transport destroy by application, as transport
     * state notification callback may be stacked and transport instance
     * must remain valid at any point in the callback.
     */
    pjsip_transport_add_ref(&rina->base);

    /* Notify application of transport disconnected state */
    state_cb = pjsip_tpmgr_get_state_cb(rina->base.tpmgr);
    if (state_cb) {
	pjsip_transport_state_info state_info;

	pj_bzero(&state_info, sizeof(state_info));
	state_info.status = rina->close_reason;
	(*state_cb)(&rina->base, PJSIP_TP_STATE_DISCONNECTED, &state_info);
    }

    /* check again */
    if (rina->base.is_shutdown || rina->base.is_destroying) {
        pjsip_transport_dec_ref(&rina->base);
        return;
    }

    /* We can not destroy the transport since high level objects may
     * still keep reference to this transport. So we can only
     * instruct transport manager to gracefully start the shutdown
     * procedure for this transport.
     */
    pjsip_transport_shutdown(&rina->base);

    /* Now, it is ok to destroy the transport. */
    pjsip_transport_dec_ref(&rina->base);
}


/*
 * Initialize pjsip_rina_transport_cfg structure with default values.
 */
PJ_DEF(void) pjsip_rina_transport_cfg_default(pjsip_rina_transport_cfg *cfg)
{
    pj_bzero(cfg, sizeof(*cfg));
}


/****************************************************************************
 * The RINA listener/transport factory.
 */

/*
 * This is the public API to create, initialize, register, and start the
 * RINA listener.
 */
PJ_DEF(pj_status_t) pjsip_rina_transport_start2(
					pjsip_endpoint *endpt,
					const pjsip_rina_transport_cfg *cfg,
					pjsip_tpfactory **p_factory
					)
{
    pj_pool_t *pool;
    pj_sock_t sock = PJ_INVALID_SOCKET;
    struct rina_listener *listener;
    pj_status_t status;
    int rina_fd;
    int rina_status;

    /* Sanity check */
    PJ_ASSERT_RETURN(endpt, PJ_EINVAL);

    pool = pjsip_endpt_create_pool(endpt, "rinalis", POOL_LIS_INIT,
				   POOL_LIS_INC);
    PJ_ASSERT_RETURN(pool, PJ_ENOMEM);


    listener = PJ_POOL_ZALLOC_T(pool, struct rina_listener);
    listener->factory.pool = pool;
    listener->factory.type = (pjsip_transport_type_e)PJSIP_TRANSPORT_RINA;
    listener->factory.type_name = (char*)
		pjsip_transport_get_type_name(listener->factory.type);
    listener->factory.flag =
	pjsip_transport_get_flag_from_type(listener->factory.type);
    listener->rina_fd = -1;
    pj_strdup_with_null(pool, &listener->appl, &cfg->appl);
    pj_strdup_with_null(pool, &listener->dif, &cfg->dif);

    pj_ansi_strcpy(listener->factory.obj_name, "rinalis");

    status = pj_lock_create_recursive_mutex(pool, listener->factory.obj_name,
					    &listener->factory.lock);
    if (status != PJ_SUCCESS)
	goto on_error;

    /* Open a RINA control file descriptor. */
    rina_fd = rina_open();
    if (rina_fd < 0) {
	PJ_LOG(4,(listener->factory.obj_name,
	    "SIP RINA listener failed to open RINA control socket: %s",
	    strerror(errno)));
        status = PJ_STATUS_FROM_OS(errno);
        goto on_error;
    }
    listener->rina_fd = rina_fd;

    /* Register our application with RINA.  Because we pass 0 (rather than
     * RINA_F_NOWAIT) as the third parameter, this will block.
     *
     * Arguably, we should not block here, but this should only happen at
     * start-of-day, and it makes the code much simpler.
     */
    rina_status = rina_register(rina_fd, 
                                (listener->dif.slen > 0) ? listener->dif.ptr : NULL,
                                listener->appl.ptr,
                                0);
    if (rina_status != 0) {
	PJ_LOG(4,(listener->factory.obj_name,
	    "SIP RINA listener failed to register application %.*s: %s",
            listener->appl.slen,
            listener->appl.ptr,
	    strerror(errno)));
        status = PJ_STATUS_FROM_OS(errno);
        goto on_error;
    }

    // TODO: Should we unregister if we subsequently fail?  We're going to
    //       close the control FD, so probably no need.

    /* Fill in local_addr from the local application.  This is a big hack -
     * we should really define a proper type for it.
     */
    pj_sockaddr_init(pj_AF_INET6(), &listener->factory.local_addr, NULL, 0);
    memcpy(&listener->factory.local_addr.ipv6.sin6_addr, listener->appl.ptr, 16);

    /* Fill in the addr_name from the local application too. */
    pj_strdup(pool, &listener->factory.addr_name.host, &listener->appl);
    listener->factory.addr_name.port = 5060;

    pj_ansi_snprintf(listener->factory.obj_name,
		     sizeof(listener->factory.obj_name),
		     "rinalis:%.*s", (int)cfg->appl.slen, cfg->appl.ptr);

    /* Register to transport manager */
    listener->endpt = endpt;
    listener->tpmgr = pjsip_endpt_get_tpmgr(endpt);
    listener->factory.create_transport = lis_create_transport;
    listener->factory.destroy = lis_destroy;
    listener->is_registered = PJ_TRUE;
    status = pjsip_tpmgr_register_tpfactory(listener->tpmgr,
					    &listener->factory);
    if (status == PJSIP_ETYPEEXISTS) {
        /* It's not a problem if there is already a RINA factory defined. */
        status = PJ_SUCCESS;
    }
    if (status != PJ_SUCCESS) {
	listener->is_registered = PJ_FALSE;
	goto on_error;
    }

    /* Start listening for connections.
     *
     * It would be nice to hook into the main PJSIP event loop to do this, but
     * unfortunately this is very focussed on the BSD Sockets API.  Instead,
     * create a thread to do this.
     */
    status = pj_thread_create(pool,
                              listener->factory.obj_name,
                              (pj_thread_proc*)lis_thread_proc,
                              listener,
                              PJ_THREAD_DEFAULT_STACK_SIZE,
                              0,
                              &listener->thread);
    if (status != PJ_SUCCESS)
        goto on_error;
    listener->thread_running = PJ_TRUE;

    PJ_LOG(4,(listener->factory.obj_name,
	     "SIP RINA listener ready for incoming connections at %.*s:%d",
	     (int)listener->factory.addr_name.host.slen,
	     listener->factory.addr_name.host.ptr,
	     listener->factory.addr_name.port));

    /* Return the pointer to user */
    if (p_factory) *p_factory = &listener->factory;

    return PJ_SUCCESS;

on_error:
    if (listener->asock==NULL && sock!=PJ_INVALID_SOCKET)
	pj_sock_close(sock);
    lis_destroy(&listener->factory);
    return status;
}


/*
 * This is the public API to create, initialize, register, and start the
 * RINA listener.
 */
PJ_DEF(pj_status_t) pjsip_rina_transport_start(pjsip_endpoint *endpt,
					        const pj_str_t *appl,
					        const pj_str_t *dif,
					        pjsip_tpfactory **p_factory)
{
    pjsip_rina_transport_cfg cfg;

    pjsip_rina_transport_cfg_default(&cfg);

    if (appl)
	pj_memcpy(&cfg.appl, appl, sizeof(*appl));

    if (dif)
	pj_memcpy(&cfg.dif, dif, sizeof(*dif));

    return pjsip_rina_transport_start2(endpt, &cfg, p_factory);
}


/* This callback is called by transport manager to destroy listener */
static pj_status_t lis_destroy(pjsip_tpfactory *factory)
{
    struct rina_listener *listener = (struct rina_listener *)factory;

    if (listener->is_registered) {
	pjsip_tpmgr_unregister_tpfactory(listener->tpmgr, &listener->factory);
	listener->is_registered = PJ_FALSE;
    }

    if (listener->rina_fd != -1) {
        pj_sock_close(listener->rina_fd); 
        listener->rina_fd = -1;
    }

    if (listener->thread_running) {
//TODO: Kick the thread to stop running.
	pj_thread_join(listener->thread);
        listener->thread_running = PJ_FALSE;
    }

    if (listener->factory.lock) {
	pj_lock_destroy(listener->factory.lock);
	listener->factory.lock = NULL;
    }

    if (listener->factory.pool) {
	pj_pool_t *pool = listener->factory.pool;

	PJ_LOG(4,(listener->factory.obj_name,  "SIP RINA listener destroyed"));

	listener->factory.pool = NULL;
	pj_pool_release(pool);
    }

    return PJ_SUCCESS;
}


/***************************************************************************/
/*
 * RINA Transport
 */

/*
 * Prototypes.
 */
/* Called by transport manager to send message */
static pj_status_t rina_send_msg(pjsip_transport *transport,
				pjsip_tx_data *tdata,
				const pj_sockaddr_t *rem_addr,
				int addr_len,
				void *token,
				pjsip_transport_callback callback);

/* Called by transport manager to shutdown */
static pj_status_t rina_shutdown(pjsip_transport *transport);

/* Called by transport manager to destroy transport */
static pj_status_t rina_destroy_transport(pjsip_transport *transport);

/* Utility to destroy transport */
static pj_status_t rina_destroy(pjsip_transport *transport,
			       pj_status_t reason);

/* Callback on incoming data */
static pj_bool_t on_data_read(pj_activesock_t *asock,
			      void *data,
			      pj_size_t size,
			      pj_status_t status,
			      pj_size_t *remainder);

/* Callback when packet is sent */
static pj_bool_t on_data_sent(pj_activesock_t *asock,
			      pj_ioqueue_op_key_t *send_key,
			      pj_ssize_t sent);

/* Callback when connect completes */
static pj_bool_t on_connect_complete(pj_activesock_t *asock,
				     pj_status_t status);

/* RINA keep-alive timer callback */
static void rina_keep_alive_timer(pj_timer_heap_t *th, pj_timer_entry *e);

/*
 * Common function to create RINA transport, called when pending accept() and
 * pending connect() complete.
 */
static pj_status_t rina_create(struct rina_listener *listener,
			       pj_pool_t *pool,
			       pj_sock_t sock, pj_bool_t is_server,
			       const pj_sockaddr *local,
			       const pj_sockaddr *remote,
			       const pjsip_host_port *local_name,
			       struct rina_transport **p_rina)
{
    struct rina_transport *rina;
    pj_ioqueue_t *ioqueue;
    pj_activesock_cfg asock_cfg;
    pj_activesock_cb rina_callback;
    const pj_str_t ka_pkt = PJSIP_RINA_KEEP_ALIVE_DATA;
    char print_addr[PJ_INET6_ADDRSTRLEN+10];
    pj_status_t status;


    PJ_ASSERT_RETURN(sock != PJ_INVALID_SOCKET, PJ_EINVAL);


    if (pool == NULL) {
	pool = pjsip_endpt_create_pool(listener->endpt, "rina",
				       POOL_TP_INIT, POOL_TP_INC);
	PJ_ASSERT_RETURN(pool != NULL, PJ_ENOMEM);
    }

    /*
     * Create and initialize basic transport structure.
     */
    rina = PJ_POOL_ZALLOC_T(pool, struct rina_transport);
    rina->is_server = is_server;
    rina->sock = sock;
    /*rina->listener = listener;*/
    pj_list_init(&rina->delayed_list);
    rina->base.pool = pool;

    pj_ansi_snprintf(rina->base.obj_name, PJ_MAX_OBJ_NAME,
		     (is_server ? "rinas%p" :"rinac%p"), rina);

    status = pj_atomic_create(pool, 0, &rina->base.ref_cnt);
    if (status != PJ_SUCCESS) {
	goto on_error;
    }

    status = pj_lock_create_recursive_mutex(pool, "rina", &rina->base.lock);
    if (status != PJ_SUCCESS) {
	goto on_error;
    }

    rina->base.key.type = listener->factory.type;
    pj_sockaddr_cp(&rina->base.key.rem_addr, remote);
    rina->base.type_name = (char*)pjsip_transport_get_type_name(
				(pjsip_transport_type_e)rina->base.key.type);
    rina->base.flag = pjsip_transport_get_flag_from_type(
				(pjsip_transport_type_e)rina->base.key.type);

    rina->base.info = (char*) pj_pool_alloc(pool, 64);
    pj_ansi_snprintf(rina->base.info, 64, "%s to %s",
                     rina->base.type_name,
                     pj_sockaddr_print(remote, print_addr,
                                       sizeof(print_addr), 3));

    rina->base.addr_len = pj_sockaddr_get_len(remote);
    pj_sockaddr_cp(&rina->base.local_addr, local);
    if (local_name)
    {
        pj_strdup(pool, &rina->base.local_name.host, &local_name->host);
        rina->base.local_name.port = local_name->port;
    }
    else
    {
        sockaddr_to_host_port(pool, &rina->base.local_name, local);
    }
    sockaddr_to_host_port(pool, &rina->base.remote_name, remote);
    rina->base.dir = is_server? PJSIP_TP_DIR_INCOMING : PJSIP_TP_DIR_OUTGOING;
    PJ_LOG(4, (rina->base.obj_name, "rina->base.local_name: %.*s",
           rina->base.local_name.host.slen, rina->base.local_name.host.ptr));

    rina->base.endpt = listener->endpt;
    rina->base.tpmgr = listener->tpmgr;
    rina->base.send_msg = &rina_send_msg;
    rina->base.do_shutdown = &rina_shutdown;
    rina->base.destroy = &rina_destroy_transport;

    /* Create active socket */
    pj_activesock_cfg_default(&asock_cfg);
    asock_cfg.concurrency = 1;

    pj_bzero(&rina_callback, sizeof(rina_callback));
    rina_callback.on_data_read = &on_data_read;
    rina_callback.on_data_sent = &on_data_sent;
    rina_callback.on_connect_complete = &on_connect_complete;

    ioqueue = pjsip_endpt_get_ioqueue(listener->endpt);
    status = pj_activesock_create(pool, sock, pj_SOCK_STREAM(), &asock_cfg,
				  ioqueue, &rina_callback, rina, &rina->asock);
    if (status != PJ_SUCCESS) {
	goto on_error;
    }

    /* Create a pool for large messages. */
    rina->large_msg_pool =
                     pjsip_endpt_create_pool(listener->endpt, "rina-lm",
                                             POOL_TP_INIT, PJSIP_MAX_PKT_LEN);
    if (rina->large_msg_pool == NULL) {
        goto on_error;
    }

    /* Register transport to transport manager */
    status = pjsip_transport_register(listener->tpmgr, &rina->base);
    if (status != PJ_SUCCESS) {
	goto on_error;
    }

    rina->is_registered = PJ_TRUE;

    /* Initialize keep-alive timer */
    rina->ka_timer.user_data = (void*)rina;
    rina->ka_timer.cb = &rina_keep_alive_timer;
    pj_ioqueue_op_key_init(&rina->ka_op_key.key, sizeof(pj_ioqueue_op_key_t));
    pj_strdup(rina->base.pool, &rina->ka_pkt, &ka_pkt);

    /* Done setting up basic transport. */
    *p_rina = rina;

    PJ_LOG(4,(rina->base.obj_name, "RINA %s transport created",
	      (rina->is_server ? "server" : "client")));

    return PJ_SUCCESS;

on_error:
    rina_destroy(&rina->base, status);
    return status;
}


/* Flush all delayed transmision once the socket is connected. */
static void rina_flush_pending_tx(struct rina_transport *rina)
{
    pj_time_val now;

    pj_gettickcount(&now);
    pj_lock_acquire(rina->base.lock);
    while (!pj_list_empty(&rina->delayed_list)) {
	struct delayed_tdata *pending_tx;
	pjsip_tx_data *tdata;
	pj_ioqueue_op_key_t *op_key;
	pj_ssize_t size;
	pj_status_t status;

	pending_tx = rina->delayed_list.next;
	pj_list_erase(pending_tx);

	tdata = pending_tx->tdata_op_key->tdata;
	op_key = (pj_ioqueue_op_key_t*)pending_tx->tdata_op_key;

        if (pending_tx->timeout.sec > 0 &&
            PJ_TIME_VAL_GT(now, pending_tx->timeout))
        {
            continue;
        }

	/* send! */
	size = tdata->buf.cur - tdata->buf.start;
	status = pj_activesock_send(rina->asock, op_key, tdata->buf.start,
				    &size, 0);
	if (status != PJ_EPENDING) {
            pj_lock_release(rina->base.lock);
	    on_data_sent(rina->asock, op_key, size);
            pj_lock_acquire(rina->base.lock);
	}

    }
    pj_lock_release(rina->base.lock);
}


/* Called by transport manager to destroy transport */
static pj_status_t rina_destroy_transport(pjsip_transport *transport)
{
    struct rina_transport *rina = (struct rina_transport*)transport;

    /* Transport would have been unregistered by now since this callback
     * is called by transport manager.
     */
    rina->is_registered = PJ_FALSE;

    return rina_destroy(transport, rina->close_reason);
}


/* Destroy RINA transport */
static pj_status_t rina_destroy(pjsip_transport *transport,
			       pj_status_t reason)
{
    struct rina_transport *rina = (struct rina_transport*)transport;

    if (rina->close_reason == 0)
	rina->close_reason = reason;

    if (rina->is_registered) {
	rina->is_registered = PJ_FALSE;
	pjsip_transport_destroy(transport);

	/* pjsip_transport_destroy will recursively call this function
	 * again.
	 */
	return PJ_SUCCESS;
    }

    /* Mark transport as closing */
    rina->is_closing = PJ_TRUE;

    /* Stop keep-alive timer. */
    if (rina->ka_timer.id) {
	pjsip_endpt_cancel_timer(rina->base.endpt, &rina->ka_timer);
	rina->ka_timer.id = PJ_FALSE;
    }

    /* Cancel all delayed transmits */
    while (!pj_list_empty(&rina->delayed_list)) {
	struct delayed_tdata *pending_tx;
	pj_ioqueue_op_key_t *op_key;

	pending_tx = rina->delayed_list.next;
	pj_list_erase(pending_tx);

	op_key = (pj_ioqueue_op_key_t*)pending_tx->tdata_op_key;

	on_data_sent(rina->asock, op_key, -reason);
    }

    if (rina->rdata.tp_info.pool) {
	pj_pool_release(rina->rdata.tp_info.pool);
	rina->rdata.tp_info.pool = NULL;
    }

    if (rina->asock) {
	pj_activesock_close(rina->asock);
	rina->asock = NULL;
	rina->sock = PJ_INVALID_SOCKET;
    } else if (rina->sock != PJ_INVALID_SOCKET) {
	pj_sock_close(rina->sock);
	rina->sock = PJ_INVALID_SOCKET;
    }

    if (rina->large_msg_pool) {
        pj_pool_release(rina->large_msg_pool);
        rina->large_msg_pool = NULL;
    }

    if (rina->base.lock) {
	pj_lock_destroy(rina->base.lock);
	rina->base.lock = NULL;
    }

    if (rina->base.ref_cnt) {
	pj_atomic_destroy(rina->base.ref_cnt);
	rina->base.ref_cnt = NULL;
    }

    if (rina->base.pool) {
	pj_pool_t *pool;

	if (reason != PJ_SUCCESS) {
	    char errmsg[PJ_ERR_MSG_SIZE];

	    pj_strerror(reason, errmsg, sizeof(errmsg));
	    PJ_LOG(4,(rina->base.obj_name,
		      "RINA transport destroyed with reason %d: %s",
		      reason, errmsg));

	} else {

	    PJ_LOG(4,(rina->base.obj_name,
		      "RINA transport destroyed normally"));

	}

	pool = rina->base.pool;
	rina->base.pool = NULL;
	pj_pool_release(pool);
    }

    return PJ_SUCCESS;
}


/*
 * This utility function creates receive data buffers and start
 * asynchronous recv() operations from the socket. It is called after
 * accept() or connect() operation complete.
 */
static pj_status_t rina_start_read(struct rina_transport *rina)
{
    pj_pool_t *pool;
    pj_ssize_t size;
    pj_sockaddr *rem_addr;
    void *readbuf[1];
    pj_status_t status;

    /* Init rdata */
    pool = pjsip_endpt_create_pool(rina->base.endpt,
				   "rtd%p",
				   PJSIP_POOL_RDATA_LEN,
				   PJSIP_POOL_RDATA_INC);
    if (!pool) {
	rina_perror(rina->base.obj_name, "Unable to create pool", PJ_ENOMEM);
	return PJ_ENOMEM;
    }

    rina->rdata.tp_info.pool = pool;

    rina->rdata.tp_info.transport = &rina->base;
    rina->rdata.tp_info.tp_data = rina;
    rina->rdata.tp_info.op_key.rdata = &rina->rdata;
    pj_ioqueue_op_key_init(&rina->rdata.tp_info.op_key.op_key,
			   sizeof(pj_ioqueue_op_key_t));

    rina->rdata.pkt_info.packet = NULL;
    rina->rdata.pkt_info.src_addr = rina->base.key.rem_addr;
    rina->rdata.pkt_info.src_addr_len = sizeof(rina->rdata.pkt_info.src_addr);
    rem_addr = &rina->base.key.rem_addr;
    pj_sockaddr_print(rem_addr, rina->rdata.pkt_info.src_name,
                      sizeof(rina->rdata.pkt_info.src_name), 0);
    rina->rdata.pkt_info.src_port = pj_sockaddr_get_port(rem_addr);

    size = PJSIP_NORMAL_PKT_LEN;
    readbuf[0] = rina->rx_buf;
    status = pj_activesock_start_read2(rina->asock, rina->base.pool, size,
				       readbuf, 0);
    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	PJ_LOG(4, (rina->base.obj_name,
		   "pj_activesock_start_read() error, status=%d",
		   status));
	return status;
    }

    return PJ_SUCCESS;
}


static int lis_thread_proc(struct rina_listener *listener)
{
    char *remote_appl;
    int rina_fd;
    pj_sockaddr remote_addr;

//TODO: Locking in case rina_fd is closed?
    while (listener->rina_fd != -1) {
        rina_fd = rina_flow_accept(listener->rina_fd,
                                   &remote_appl,
                                   NULL,
                                   0);
        if (rina_fd != -1) {
            /* Fill in remote_addr from the local application.  This is a big hack -
             * we should really define a proper type for it.
             */
            pj_sockaddr_init(pj_AF_INET6(), &remote_addr, NULL, 0);
            memcpy(&remote_addr.ipv6.sin6_addr, remote_appl, 16);

            on_accept_complete(listener,
                               (pj_sock_t)rina_fd, &remote_addr,
                               sizeof(pj_sockaddr_in6));
        } else {
	    PJ_LOG(4,(listener->factory.obj_name,
	        "SIP RINA listener failed to accept flow: %s",
	        strerror(errno)));
        }
    }

    return 0;
}


/* This callback is called by transport manager for the RINA factory
 * to create outgoing transport to the specified destination.
 */
static pj_status_t lis_create_transport(pjsip_tpfactory *factory,
					pjsip_tpmgr *mgr,
					pjsip_endpoint *endpt,
					const pj_sockaddr *remote_addr,
					int addr_len,
					pjsip_transport **p_transport)
{
    struct rina_listener *listener;
    struct rina_transport *rina;
    pj_status_t status;
    int rina_fd;
    pj_sockaddr local_addr;

    /* Sanity checks */
    PJ_ASSERT_RETURN(factory && mgr && endpt && remote_addr &&
		     addr_len && p_transport, PJ_EINVAL);

    /* Check that address is a sockaddr_in6 - we use the IPv6 address
     * field to store the application name.
     */
    PJ_ASSERT_RETURN((remote_addr->addr.sa_family == pj_AF_INET6() &&
		      addr_len == sizeof(pj_sockaddr_in6)), PJ_EINVAL);


    listener = (struct rina_listener*)factory;

    /* Create a RINA flow.  Because we pass 0 (rather than RINA_F_NOWAIT) as
     * the third parameter, this will block.
     *
     * We really shouldn't block here, but it makes the code much simpler.
     *
     * We use the IPv6 address field to store the remote application name,
     * which is very hacky - we should really plumb it through properly!
     */
    // TODO: Make this non-blocking!
    char *remote_appl = strndup((char *)&remote_addr->ipv6.sin6_addr, 16);
    rina_fd = rina_flow_alloc(listener->dif.ptr,
			      listener->appl.ptr,
			      remote_appl,
			      NULL,
			      0);
    if (rina_fd < 0) {
	PJ_LOG(4,(listener->factory.obj_name,
		  "SIP RINA failed to allocate flow to application %s: %s",
        	  remote_appl,
		  strerror(errno)));
        free(remote_appl);
        return PJ_STATUS_FROM_OS(errno);
    }
    free(remote_appl);

    /* Fill in local_addr from the local application. */
    pj_sockaddr_init(pj_AF_INET6(), &local_addr, NULL, 0);
    memcpy(&local_addr.ipv6.sin6_addr, listener->appl.ptr, 16);

    /* Create the transport descriptor */
    status = rina_create(listener, NULL, rina_fd, PJ_FALSE, &local_addr,
			 remote_addr, &factory->addr_name, &rina);
    if (status != PJ_SUCCESS)
	return status;

    on_connect_complete(rina->asock, PJ_SUCCESS);

    /* Done */
    *p_transport = &rina->base;

    return PJ_SUCCESS;
}


/*
 * This callback is called by when pending rina_flow_accept() operation
 * has completed.
 */
static pj_bool_t on_accept_complete(struct rina_listener *listener,
				    pj_sock_t sock,
				    const pj_sockaddr_t *src_addr,
				    int src_addr_len)
{
    struct rina_transport *rina;
    char addr[PJ_INET6_ADDRSTRLEN+10];
    pjsip_tp_state_callback state_cb;
    pj_sockaddr tmp_src_addr;
    pj_status_t status;

    PJ_UNUSED_ARG(src_addr_len);

    PJ_ASSERT_RETURN(sock != PJ_INVALID_SOCKET, PJ_TRUE);

    PJ_LOG(4,(listener->factory.obj_name,
	      "RINA listener %.*s:%d: got incoming RINA connection "
	      "from %s, sock=%d",
	      (int)listener->factory.addr_name.host.slen,
	      listener->factory.addr_name.host.ptr,
	      listener->factory.addr_name.port,
	      pj_sockaddr_print(src_addr, addr, sizeof(addr), 3),
	      sock));

    /* rina_create() expect pj_sockaddr, so copy src_addr to temporary var,
     * just in case.
     */
    pj_bzero(&tmp_src_addr, sizeof(tmp_src_addr));
    pj_sockaddr_cp(&tmp_src_addr, src_addr);

    /*
     * Incoming connection!
     * Create RINA transport for the new socket.
     */
    status = rina_create( listener, NULL, sock, PJ_TRUE,
			 &listener->factory.local_addr,
			 &tmp_src_addr, &listener->factory.addr_name, &rina);
    if (status == PJ_SUCCESS) {

        /* Add a reference to prevent the transport from being destroyed while
         * we're operating on it.
         */
        pjsip_transport_add_ref(&rina->base);

	status = rina_start_read(rina);
	if (status != PJ_SUCCESS) {
	    PJ_LOG(3,(rina->base.obj_name, "New transport cancelled"));
            pjsip_transport_dec_ref(&rina->base);
	    rina_destroy(&rina->base, status);
	} else {
	    /* Start keep-alive timer */
	    if (PJSIP_RINA_KEEP_ALIVE_INTERVAL) {
		pj_time_val delay = {PJSIP_RINA_KEEP_ALIVE_INTERVAL, 0};
		pjsip_endpt_schedule_timer(listener->endpt,
					   &rina->ka_timer,
					   &delay);
		rina->ka_timer.id = PJ_TRUE;
		pj_gettimeofday(&rina->last_activity);
	    }

	    /* Notify application of transport state accepted */
	    state_cb = pjsip_tpmgr_get_state_cb(rina->base.tpmgr);
	    if (state_cb) {
		pjsip_transport_state_info state_info;

		pj_bzero(&state_info, sizeof(state_info));
		(*state_cb)(&rina->base, PJSIP_TP_STATE_CONNECTED, &state_info);
	    }
            pjsip_transport_dec_ref(&rina->base);
	}
    }

    return PJ_TRUE;
}


/*
 * Callback from ioqueue when packet is sent.
 */
static pj_bool_t on_data_sent(pj_activesock_t *asock,
			      pj_ioqueue_op_key_t *op_key,
			      pj_ssize_t bytes_sent)
{
    struct rina_transport *rina = (struct rina_transport*)
    				pj_activesock_get_user_data(asock);
    pjsip_tx_data_op_key *tdata_op_key = (pjsip_tx_data_op_key*)op_key;

    /* Note that op_key may be the op_key from keep-alive, thus
     * it will not have tdata etc.
     */

    tdata_op_key->tdata = NULL;

    if (tdata_op_key->callback) {
	/*
	 * Notify sip_transport.c that packet has been sent.
	 */
	if (bytes_sent == 0)
	    bytes_sent = -PJ_RETURN_OS_ERROR(OSERR_ENOTCONN);

	tdata_op_key->callback(&rina->base, tdata_op_key->token, bytes_sent);

	/* Mark last activity time */
	pj_gettimeofday(&rina->last_activity);

    }

    /* Check for error/closure */
    if (bytes_sent <= 0) {
	pj_status_t status;

	PJ_LOG(5,(rina->base.obj_name, "RINA send() error, sent=%d",
		  bytes_sent));

	status = (bytes_sent == 0) ? PJ_RETURN_OS_ERROR(OSERR_ENOTCONN) :
				     -bytes_sent;

	rina_init_shutdown(rina, status);

	return PJ_FALSE;
    }

    return PJ_TRUE;
}


/*
 * This callback is called by transport manager to send SIP message
 */
static pj_status_t rina_send_msg(pjsip_transport *transport,
				pjsip_tx_data *tdata,
				const pj_sockaddr_t *rem_addr,
				int addr_len,
				void *token,
				pjsip_transport_callback callback)
{
    struct rina_transport *rina = (struct rina_transport*)transport;
    pj_ssize_t size;
    pj_status_t status = PJ_SUCCESS;

    /* Sanity check */
    PJ_ASSERT_RETURN(transport && tdata, PJ_EINVAL);

    /* Check that there's no pending operation associated with the tdata */
    PJ_ASSERT_RETURN(tdata->op_key.tdata == NULL, PJSIP_EPENDINGTX);

    /* Check the address is supported */
    PJ_ASSERT_RETURN(rem_addr && (addr_len==sizeof(pj_sockaddr_in) ||
	                          addr_len==sizeof(pj_sockaddr_in6)),
	             PJ_EINVAL);

    /* Init op key. */
    tdata->op_key.tdata = tdata;
    tdata->op_key.token = token;
    tdata->op_key.callback = callback;

    /*
     * Send the packet to ioqueue to be sent asynchronously.
     */
    size = tdata->buf.cur - tdata->buf.start;
    status = pj_activesock_send(rina->asock,
				(pj_ioqueue_op_key_t*)&tdata->op_key,
				tdata->buf.start, &size, 0);
 
    if (status != PJ_EPENDING) {
        /* Not pending (could be immediate success or error) */
        tdata->op_key.tdata = NULL;
 
	/* Shutdown transport on closure/errors */
	if (size <= 0) {

	    PJ_LOG(5,(rina->base.obj_name, "RINA send() error, sent=%d",
		      size));

	    if (status == PJ_SUCCESS)
		status = PJ_RETURN_OS_ERROR(OSERR_ENOTCONN);

	    rina_init_shutdown(rina, status);
	}
    }

    return status;
}


/*
 * This callback is called by transport manager to shutdown transport.
 */
static pj_status_t rina_shutdown(pjsip_transport *transport)
{
    struct rina_transport *rina = (struct rina_transport*)transport;

    /* Stop keep-alive timer. */
    if (rina->ka_timer.id) {
	pjsip_endpt_cancel_timer(rina->base.endpt, &rina->ka_timer);
	rina->ka_timer.id = PJ_FALSE;
    }

    return PJ_SUCCESS;
}


/*
 * Callback from ioqueue that an incoming data is received from the socket.
 */
static pj_bool_t on_data_read(pj_activesock_t *asock,
			      void *data,
			      pj_size_t size,
			      pj_status_t status,
			      pj_size_t *remainder)
{
    enum { MAX_IMMEDIATE_PACKET = 10 };
    struct rina_transport *rina;
    pjsip_rx_data *rdata;

    PJ_UNUSED_ARG(data);

    rina = (struct rina_transport*) pj_activesock_get_user_data(asock);
    rdata = &rina->rdata;

    /* Don't do anything if transport is closing. */
    if (rina->is_closing) {
	rina->is_closing++;
	return PJ_FALSE;
    }

    /* Houston, we have packet! Report the packet to transport manager
     * to be parsed.
     */
    if (status == PJ_SUCCESS) {
	pj_ssize_t size_eaten;

	/* Mark this as an activity */
	pj_gettimeofday(&rina->last_activity);

        if (rdata->pkt_info.packet == NULL) {
            /* There is no data already buffered in the rdata, so link the
             * rdata to the receive buffer to see if this is enough for the
             * transport manager to consume.
             */
            rdata->pkt_info.packet = rina->rx_buf;
            rdata->pkt_info.len = size;

        } else {
            /* The rdata already has a linked buffer containing some data, so
             * append the new data and see if the transport manager can consume
             * this.
             */
            pj_memcpy(&rdata->pkt_info.packet[rdata->pkt_info.len], data, size);
            rdata->pkt_info.len += size;

        }

        /* Init pkt_info part. */
        rdata->pkt_info.zero = 0;
        pj_gettimeofday(&rdata->pkt_info.timestamp);

        /* Report to transport manager.
         * The transport manager will tell us how many bytes of the packet
         * have been processed (as valid SIP message).
         */
        size_eaten =
            pjsip_tpmgr_receive_packet(rdata->tp_info.transport->tpmgr,
                                       rdata);

        if (size_eaten < 0) {
          /* There was a problem receiving the message.  This suggests the
           * data on the socket is corrupt.  Shutdown this connection.
           */
          PJ_LOG(2,(THIS_FILE,
                    "Receive failed (%d), closing RINA connection: %s",
                    size_eaten,
                    rina->base.obj_name));

          rina_init_shutdown(rina, PJSIP_EINVALIDMSG);
          return PJ_FALSE;
        }

        pj_assert(size_eaten <= (pj_ssize_t)rdata->pkt_info.len);

        /* Handle unprocessed data. */
        *remainder = rdata->pkt_info.len - size_eaten;
        if (*remainder < PJSIP_NORMAL_PKT_LEN) {

            if (*remainder > 0)
            {
                /* Remainder will fit in receive buffer, so just copy it to
                 * the front of the buffer.
                 */
                if (rdata->pkt_info.packet != rina->rx_buf) {
                    /* Data has been moved to a separate large buffer, so
                     * copy it back to the receive buffer.
                     */
                    pj_memcpy(rina->rx_buf,
                              rdata->pkt_info.packet + size_eaten,
                              *remainder);

                } else if (size_eaten > 0) {
                    /* Data is already in the receive buffer, so just move it
                     * to the front.
                     */
                    pj_memmove(rina->rx_buf,
                               rina->rx_buf + size_eaten,
                               *remainder);

                }
            }

            /* Reset the large message pool. */
            pj_pool_reset(rina->large_msg_pool);

            rdata->pkt_info.packet = NULL;

        } else {

            if (rdata->pkt_info.packet == rina->rx_buf) {
                /* Message is too large for the receive buffer, so allocate a
                 * large message buffer and copy the data across.
                 */
                rdata->pkt_info.packet = (char*)
                        pj_pool_alloc(rina->large_msg_pool, PJSIP_MAX_PKT_LEN);
                pj_memcpy(rdata->pkt_info.packet,
                          rina->rx_buf + size_eaten,
                          *remainder);
                *remainder = 0;

            } else if (size_eaten > 0) {
                /* Move data to the front of the large message buffer. */
                pj_memmove(rdata->pkt_info.packet,
                           rdata->pkt_info.packet + size_eaten,
                           *remainder);

            }

            /* All data has been moved from the receive buffer, so return
             * remainder of zero.
             */
            *remainder = 0;
        }

    } else {

	/* Transport is closed */
	PJ_LOG(4,(rina->base.obj_name, "RINA connection closed"));

	rina_init_shutdown(rina, status);

	return PJ_FALSE;

    }

    /* Reset pool. */
    pj_pool_reset(rdata->tp_info.pool);

    return PJ_TRUE;
}


/*
 * Callback from ioqueue when asynchronous connect() operation completes.
 */
static pj_bool_t on_connect_complete(pj_activesock_t *asock,
				     pj_status_t status)
{
    struct rina_transport *rina;
    pj_sockaddr addr;
    int addrlen;
    pjsip_tp_state_callback state_cb;

    rina = (struct rina_transport*) pj_activesock_get_user_data(asock);

    /* Check connect() status */
    if (status != PJ_SUCCESS) {

	rina_pwarning(rina->base.obj_name, "RINA connect() error", status);
	PJ_LOG(3, (rina->base.obj_name,
	           "Unable to connect to %.*s:%d",
	           (int)rina->base.remote_name.host.slen,
	           rina->base.remote_name.host.ptr,
	           rina->base.remote_name.port));

	/* Mark the transport as failed, ahead of it being shutdown below.  This
	 * avoids it from being selected to send any further messages as a result
	 * of processing in the on_data_sent(), which happens before the
	 * transaction gets marked as shutdown. */
	rina->base.is_failed = PJ_TRUE;

	/* Cancel all delayed transmits */
	while (!pj_list_empty(&rina->delayed_list)) {
	    struct delayed_tdata *pending_tx;
	    pj_ioqueue_op_key_t *op_key;

	    pending_tx = rina->delayed_list.next;
	    pj_list_erase(pending_tx);

	    op_key = (pj_ioqueue_op_key_t*)pending_tx->tdata_op_key;

	    on_data_sent(rina->asock, op_key, -status);
	}

	rina_init_shutdown(rina, status);
	return PJ_FALSE;
    }

    PJ_LOG(4,(rina->base.obj_name,
	      "RINA transport %.*s:%d is connected to %.*s:%d",
	      (int)rina->base.local_name.host.slen,
	      rina->base.local_name.host.ptr,
	      rina->base.local_name.port,
	      (int)rina->base.remote_name.host.slen,
	      rina->base.remote_name.host.ptr,
	      rina->base.remote_name.port));


    /* Update (again) local address, just in case local address currently
     * set is different now that the socket is connected (could happen
     * on some systems, like old Win32 probably?).
     */
    addrlen = sizeof(addr);
    if (pj_sock_getsockname(rina->sock, &addr, &addrlen)==PJ_SUCCESS) {
	pj_sockaddr *tp_addr = &rina->base.local_addr;

	if (pj_sockaddr_has_addr(&addr) &&
	    pj_sockaddr_cmp(&addr, tp_addr) != 0)
	{
	    pj_sockaddr_cp(tp_addr, &addr);
	    sockaddr_to_host_port(rina->base.pool, &rina->base.local_name,
				  tp_addr);
	}
    }

    /* Start pending read */
    status = rina_start_read(rina);
    if (status != PJ_SUCCESS) {
	rina_init_shutdown(rina, status);
	return PJ_FALSE;
    }

    /* Notify application of transport state connected */
    state_cb = pjsip_tpmgr_get_state_cb(rina->base.tpmgr);
    if (state_cb) {
	pjsip_transport_state_info state_info;

	pj_bzero(&state_info, sizeof(state_info));
	(*state_cb)(&rina->base, PJSIP_TP_STATE_CONNECTED, &state_info);
    }

    /* Flush all pending send operations */
    rina_flush_pending_tx(rina);

    /* Start keep-alive timer */
    if (PJSIP_RINA_KEEP_ALIVE_INTERVAL) {
	pj_time_val delay = { PJSIP_RINA_KEEP_ALIVE_INTERVAL, 0 };
	pjsip_endpt_schedule_timer(rina->base.endpt, &rina->ka_timer,
				   &delay);
	rina->ka_timer.id = PJ_TRUE;
	pj_gettimeofday(&rina->last_activity);
    }

    return PJ_TRUE;
}

/* Transport keep-alive timer callback */
static void rina_keep_alive_timer(pj_timer_heap_t *th, pj_timer_entry *e)
{
    struct rina_transport *rina = (struct rina_transport*) e->user_data;
    pj_time_val delay;
    pj_time_val now;
    pj_ssize_t size;
    pj_status_t status;

    PJ_UNUSED_ARG(th);

    rina->ka_timer.id = PJ_TRUE;

    pj_gettimeofday(&now);
    PJ_TIME_VAL_SUB(now, rina->last_activity);

    if (now.sec > 0 && now.sec < PJSIP_RINA_KEEP_ALIVE_INTERVAL) {
	/* There has been activity, so don't send keep-alive */
	delay.sec = PJSIP_RINA_KEEP_ALIVE_INTERVAL - now.sec;
	delay.msec = 0;

	pjsip_endpt_schedule_timer(rina->base.endpt, &rina->ka_timer,
				   &delay);
	rina->ka_timer.id = PJ_TRUE;
	return;
    }

    PJ_LOG(5,(rina->base.obj_name, "Sending %d byte(s) keep-alive to %.*s:%d",
	      (int)rina->ka_pkt.slen, (int)rina->base.remote_name.host.slen,
	      rina->base.remote_name.host.ptr,
	      rina->base.remote_name.port));

    /* Send the data */
    size = rina->ka_pkt.slen;
    status = pj_activesock_send(rina->asock, &rina->ka_op_key.key,
				rina->ka_pkt.ptr, &size, 0);

    if (status != PJ_SUCCESS && status != PJ_EPENDING) {
	rina_perror(rina->base.obj_name,
		   "Error sending keep-alive packet", status);
	rina_init_shutdown(rina, status);
	return;
    }

    /* Register next keep-alive */
    delay.sec = PJSIP_RINA_KEEP_ALIVE_INTERVAL;
    delay.msec = 0;

    pjsip_endpt_schedule_timer(rina->base.endpt, &rina->ka_timer,
			       &delay);
    rina->ka_timer.id = PJ_TRUE;
}



#include "stack.h"

//
// mod_rina_transport is the module implementing RINA
//
static pj_bool_t rina_transport_on_start();

pjsip_module mod_rina_transport =
{
  NULL, NULL,                         // prev, next
  pj_str("mod-rina-transport"),       // Name
  -1,                                 // Id
  PJSIP_MOD_PRIORITY_TRANSPORT_LAYER, // Priority
  NULL,                               // load()
  &rina_transport_on_start,           // start()
  NULL,                               // stop()
  NULL,                               // unload()
  NULL,                               // on_rx_request()
  NULL,                               // on_rx_response()
  NULL,                               // on_tx_request()
  NULL,                               // on_tx_response()
  NULL,                               // on_tsx_state()
};


static pj_bool_t rina_transport_on_start()
{
  PJSIP_TRANSPORT_RINA = rina_transport_register_type();
  return PJ_SUCCESS;
}


pj_status_t init_rina_transport(const std::string& dif, const std::string& local_appl)
{
  pj_status_t status;
  status = pjsip_endpt_register_module(stack_data.endpt, &mod_rina_transport);
  PJ_ASSERT_RETURN(status == PJ_SUCCESS, 1);

  return status;
}


void destroy_rina_transport()
{
  pjsip_endpt_unregister_module(stack_data.endpt, &mod_rina_transport);
}
