/**
 * @file matrix.h Matrix class definitions.
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

#ifndef MATRIX_H__
#define MATRIX_H__

extern "C" {
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <stdint.h>
}

#include "pjutils.h"
#include "stack.h"
#include "sproutlet.h"
#include "matrixconnection.h"

class MatrixTsx;

/// Definition of MatrixTsx class.
class Matrix : public Sproutlet
{
public:
  /// Constructor.
  Matrix(const std::string& home_server,
         HttpResolver* resolver,
         LoadMonitor *load_monitor,
         LastValueCache* stats_aggregator);

  /// Destructor.
  ~Matrix() {}

  /// Create a MatrixTsx.
  SproutletTsx* get_tsx(SproutletTsxHelper* helper,
                        const std::string& alias,
                        pjsip_msg* req);

private:
  MatrixConnection _connection;
};

/// Definition of the MatrixTsx class.
class MatrixTsx : public SproutletTsx
{
public:
  /// Config object for a MatrixTsx. Sets sensible defaults for all the
  /// fields.
  class Config
  {
  public:
    Config() {}
    ~Config() {}
  };

  /// Constructor.
  MatrixTsx(SproutletTsxHelper* helper, Config& config) :
    SproutletTsx(helper), _config(config) {}

  /// Destructor.
  ~MatrixTsx() {}

  /// Implementation of SproutletTsx methods in matrix.
  virtual void on_rx_initial_request(pjsip_msg* req);
  virtual void on_rx_response(pjsip_msg* rsp, int fork_id);
  virtual void on_rx_in_dialog_request(pjsip_msg* req);

private:
  /// The config object for this transaction.
  Config _config;
};

#endif
