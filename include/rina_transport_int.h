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


/**
 * @}
 */

#endif	/* __RINA_TRANSPORT_INT_H__ */
