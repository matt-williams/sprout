/* $Id: rina_transport_int.h 3553 2011-05-05 06:14:19Z nanang $ */
/* 
 * Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 * Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
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
#ifndef __RINA_TRANSPORT_INT_H__
#define __RINA_TRANSPORT_INT_H__

/**
 * @file rina_transport_int.h
 * @brief SIP RINA Transport.
 */

#include <pjsip/sip_transport.h>
#include <pj/sock_qos.h>


/**
 * Set the interval to send keep-alive packet for RINA transports.
 * If the value is zero, keep-alive will be disabled for RINA.
 *
 * Default: 90 (seconds)
 *
 * @see PJSIP_RINA_KEEP_ALIVE_DATA
 */
#ifndef PJSIP_RINA_KEEP_ALIVE_INTERVAL
#   define PJSIP_RINA_KEEP_ALIVE_INTERVAL    90
#endif


/**
 * Set the payload of the RINA keep-alive packet.
 *
 * Default: CRLF
 */
#ifndef PJSIP_RINA_KEEP_ALIVE_DATA
#   define PJSIP_RINA_KEEP_ALIVE_DATA        { "\r\n\r\n", 4 }
#endif


PJ_BEGIN_DECL

/**
 * @brief API to create and register RINA transport.
 * @{
 * The functions below are used to create RINA transport and register 
 * the transport to the framework.
 */

/**
 * Settings to be specified when creating the RINA transport. Application 
 * should initialize this structure with its default values by calling 
 * pjsip_rina_transport_cfg_default().
 */
typedef struct pjsip_rina_transport_cfg
{
    /**
     * Local application name to be advertised as the address of this SIP
     * transport.
     */
    pj_str_t		appl;

    /**
     * Optional DIF name to use.
     */
    pj_str_t		dif;

} pjsip_rina_transport_cfg;


/**
 * Initialize pjsip_rina_transport_cfg structure with default values for
 * the specifed address family.
 *
 * @param cfg		The structure to initialize.
 * @param af		Address family to be used.
 */
PJ_DECL(void) pjsip_rina_transport_cfg_default(pjsip_rina_transport_cfg *cfg,
					      int af);


/**
 * Register support for SIP RINA transport by creating RINA listener on
 * the specified address and port. This function will create an
 * instance of SIP RINA transport factory and register it to the
 * transport manager.
 *
 * @param endpt		The SIP endpoint.
 * @param appl          Application to listen for.
 * @param dif           Optional DIF to listen on.
 * @param p_factory	Optional pointer to receive the instance of the
 *			SIP RINA transport factory just created.
 *
 * @return		PJ_SUCCESS when the transport has been successfully
 *			started and registered to transport manager, or
 *			the appropriate error code.
 */
PJ_DECL(pj_status_t) pjsip_rina_transport_start(pjsip_endpoint *endpt,
					        const pj_str_t *appl,
					        const pj_str_t *dif,
					        pjsip_tpfactory **p_factory);


/**
 * Another variant of #pjsip_rina_transport_start().
 *
 * @param endpt		The SIP endpoint.
 * @param cfg		RINA transport settings. Application should initialize
 *			this setting with #pjsip_rina_transport_cfg_default().
 * @param p_factory	Optional pointer to receive the instance of the
 *			SIP RINA transport factory just created.
 *
 * @return		PJ_SUCCESS when the transport has been successfully
 *			started and registered to transport manager, or
 *			the appropriate error code.
 */
PJ_DECL(pj_status_t) pjsip_rina_transport_start2(
					pjsip_endpoint *endpt,
					const pjsip_rina_transport_cfg *cfg,
					pjsip_tpfactory **p_factory
					);


PJ_END_DECL

/**
 * @}
 */

#endif	/* __RINA_TRANSPORT_INT_H__ */
