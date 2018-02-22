/**
 * @file hssconnection.h Definitions for HSSConnection class.
 *
 * Copyright (C) Metaswitch Networks 2017
 * If license terms are provided to you in a COPYING file in the root directory
 * of the source code repository by which you are accessing this code, then
 * the license outlined in that COPYING file applies to your use.
 * Otherwise no rights are granted except for those provided to you by
 * Metaswitch Networks in a separate written agreement.
 */

#ifndef RINA_TRANSPORT_H__
#define RINA_TRANSPORT_H__

#include <string>

extern int PJSIP_TRANSPORT_RINA;
pj_status_t init_rina_transport(const std::string& dif, const std::string& local_appl);
void destroy_rina_transport();


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

#endif
