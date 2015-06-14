/**
 * @file matrixutils.cpp Utilities for Matrix gateway
 *
 * Project Clearwater - IMS in the Cloud
 * Copyright (C) 2014  Metaswitch Networks Ltd
 *
 * Parts of this module were derived from GPL licensed PJSIP sample code
 * with the following copyrights.
 *   Copyright (C) 2008-2011 Teluu Inc. (http://www.teluu.com)
 *   Copyright (C) 2003-2008 Benny Prijono <benny@prijono.org>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version, along with the "Special Exception" for use of
 * the program along with SSL, set forth below. This program is distributed
 * in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details. You should have received a copy of the GNU General Public
 * License along with this program.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * The author can be reached by email at clearwater@metaswitch.com or by
 * post at Metaswitch Networks Ltd, 100 Church St, Enfield EN2 6BQ, UK
 *
 * Special Exception
 * Metaswitch Networks Ltd  grants you permission to copy, modify,
 * propagate, and distribute a work formed by combining OpenSSL with The
 * Software, or a work derivative of such a combination, even if such
 * copying, modification, propagation, or distribution would otherwise
 * violate the terms of the GPL. You must comply with the GPL in all
 * respects for all of the code used other than OpenSSL.
 * "OpenSSL" means OpenSSL toolkit software distributed by the OpenSSL
 * Project and licensed under the OpenSSL Licenses, or a work based on such
 * software and licensed under the OpenSSL Licenses.
 * "OpenSSL Licenses" means the OpenSSL License and Original SSLeay License
 * under which the OpenSSL Project distributes the OpenSSL toolkit software,
 * as those licenses appear in the file LICENSE-OPENSSL.
 */

#include "log.h"
#include "constants.h"
#include "matrixutils.h"
#include <boost/regex.hpp>
#include <boost/algorithm/string/predicate.hpp>

std::string MatrixUtils::ims_uri_to_matrix_user(pjsip_uri* uri, const std::string& home_server)
{
  std::string matrix_user = "";
  if (PJSIP_URI_SCHEME_IS_SIP(uri))
  {
    pjsip_sip_uri* sip_uri = (pjsip_sip_uri*)uri;
    matrix_user = "@sip_" + PJUtils::pj_str_to_string(&sip_uri->user) + ":" + home_server;
  }
  else if (PJSIP_URI_SCHEME_IS_TEL(uri))
  {
    pjsip_tel_uri* tel_uri = (pjsip_tel_uri*)uri;
    matrix_user = "@tel_" + PJUtils::pj_str_to_string(&tel_uri->number) + ":" + home_server;
  }
  return matrix_user;
}

std::string MatrixUtils::matrix_uri_to_matrix_user(pjsip_uri* uri, const std::string& home_server)
{
  std::string matrix_user = "";
  if (PJSIP_URI_SCHEME_IS_SIP(uri))
  {
    pjsip_sip_uri* sip_uri = (pjsip_sip_uri*)uri;
    matrix_user = "@" + PJUtils::pj_str_to_string(&sip_uri->user) + ":" + PJUtils::pj_str_to_string(&sip_uri->host);
  }
  else if (PJSIP_URI_SCHEME_IS_TEL(uri))
  {
    pjsip_tel_uri* tel_uri = (pjsip_tel_uri*)uri;
    matrix_user = "@" + PJUtils::pj_str_to_string(&tel_uri->number) + ":" + home_server;
  }
  return matrix_user;
}

std::string MatrixUtils::matrix_user_to_userpart(const std::string& user)
{
  return user.substr(1, user.find(':', 1) - 1);
}

std::string MatrixUtils::matrix_user_to_home_server(const std::string& user)
{
  return user.substr(user.find(':', 1) + 1);
}

std::string MatrixUtils::matrix_user_to_ims_uri(const std::string& user)
{
  std::string userpart = MatrixUtils::matrix_user_to_userpart(user);
  std::string home_server = MatrixUtils::matrix_user_to_home_server(user);
  std::string ims_uri;
  if (boost::starts_with(userpart, "sip_"))
  {
    ims_uri = "sip:" + userpart.substr(4) + "@" + home_server;
  }
  else if (boost::starts_with(userpart, "tel_"))
  {
    ims_uri = "tel:" + userpart.substr(4);
  }
  else
  {
    ims_uri = "sip:" + userpart + "@" + home_server;
  }
  LOG_DEBUG("Matrix user %s => IMS URI %s", user.c_str(), ims_uri.c_str());
  return ims_uri;
}

void MatrixUtils::parse_sdp(const std::string& body,
                            std::string& sdp,
                            std::vector<std::string>& candidates)
{
  boost::regex candidate_regex = boost::regex("a=candidate:[0-9]+ [0-9]+ tcp [0-9]+ ([0-9.]+) .*", boost::regex_constants::no_except);

  // Split the message body into each SDP line.
  std::stringstream stream(body);
  std::string line;

  while(std::getline(stream, line))
  {
    boost::smatch results;
    if (boost::regex_match(line, results, candidate_regex))
    {
      candidates.push_back(results[1]);
    }
    else
    {
      sdp.append(line);
      sdp.append("\n");
    }
    line = "";
  }
}

pjsip_uri* MatrixUtils::get_from_uri(pjsip_msg* req)
{
  // Find the calling party in the P-Asserted-Identity header.
  pjsip_routing_hdr* asserted_id =
    (pjsip_routing_hdr*)pjsip_msg_find_hdr_by_name(req,
                                                   &STR_P_ASSERTED_IDENTITY,
                                                   NULL);
  if (asserted_id == NULL)
  {
    LOG_DEBUG("No P-Asserted-Identity");
    return NULL;
  }

  return (pjsip_uri*)pjsip_uri_get_uri(&asserted_id->name_addr);
}

std::string MatrixUtils::get_room_alias(const std::string& ims_matrix_user, const std::string& other_matrix_user, const std::string& home_server)
{
  std::string ims_alias = ims_matrix_user.substr(1);
  ims_alias = ims_alias.replace(ims_alias.find(":"), 1, "-");
  std::string other_alias = other_matrix_user.substr(1);
  other_alias = other_alias.replace(other_alias.find(":"), 1, "-"); 
  return "#call-" + ims_alias + "-" + other_alias + ":" + home_server;
}
