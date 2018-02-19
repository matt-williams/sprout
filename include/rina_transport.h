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

#endif
