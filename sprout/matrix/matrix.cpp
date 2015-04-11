/**
 * @file matrix.cpp Implementation of matrix-gateway
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
#include "matrix.h"

Matrix::Matrix(const std::string& home_server,
               const std::string& as_token,
               HttpResolver* resolver,
               LoadMonitor *load_monitor,
               LastValueCache* stats_aggregator) :
  Sproutlet("matrix"),
  _connection(home_server,
              as_token,
              resolver,
              load_monitor,
              stats_aggregator)
{
  std::vector<std::string> user_regexs;
  user_regexs.push_back("@tel_\\+?[0-9][0-9]*:.*");
  user_regexs.push_back("@sip_.*:.*");
  std::vector<std::string> alias_regexs;
  alias_regexs.push_back("#call-.*:.*");
  std::vector<std::string> room_regexs;
  std::string hs_token;
  HTTPCode rc = _connection.register_as("http://127.0.0.1:11888", user_regexs, alias_regexs, room_regexs, hs_token);
  if (rc == HTTP_OK)
  {
    LOG_INFO("Successfully registered AS - token: %s", hs_token.c_str());
  }
  else
  {
    LOG_INFO("Failed to register AS - rc = %lu", rc);
  }
}

/// Creates a MatrixTsx instance.
SproutletTsx* Matrix::get_tsx(SproutletTsxHelper* helper,
                                    const std::string& alias,
                                    pjsip_msg* req)
{
  MatrixTsx::Config config;

  return new MatrixTsx(helper, config);
}

/// Matrix receives an initial request.
void MatrixTsx::on_rx_initial_request(pjsip_msg* req)
{
  send_request(req);
}

/// Matrix receives a response. It will add all the Via headers from the
/// original request back on and send the response on. It can also change
/// the response in various ways depending on the configuration that was
/// specified in the Route header of the original request.
/// - It can mangle the dialog identifiers using its mangalgorithm.
/// - It can mangle the Contact URI using its mangalgorithm.
/// - It can mangle the Record-Route and Route headers URIs.
void MatrixTsx::on_rx_response(pjsip_msg* rsp, int fork_id)
{
  send_response(rsp);
}

/// Matrix receives an in dialog request. It will strip off all the Via
/// headers and send the request on. It can also change the request in various
/// ways depending on the configuration in its Route header.
/// - It can mangle the dialog identifiers using its mangalgorithm.
/// - It can mangle the Request URI and Contact URI using its mangalgorithm.
/// - It can mangle the To URI using its mangalgorithm.
/// - It can edit the S-CSCF Route header to turn the request into either an
///   originating or terminating request.
/// - It can edit the S-CSCF Route header to turn the request into an out of
///   the blue request.
/// - It can mangle the Record-Route headers URIs.
void MatrixTsx::on_rx_in_dialog_request(pjsip_msg* req)
{
  send_request(req);
}
