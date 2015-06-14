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

class Matrix;
class MatrixTsx;

extern "C" {
#include <pjsip.h>
#include <pjlib-util.h>
#include <pjlib.h>
#include <stdint.h>
}

#include "pjutils.h"
#include "stack.h"
#include "sproutlet.h"
#include "matrixhandlers.h"
#include "matrixconnection.h"

/// Definition of MatrixTsx class.
class Matrix : public Sproutlet
{
public:
  /// Constructor.
  Matrix(const std::string& home_server,
         const std::string& as_token,
         HttpResolver* resolver,
         const std::string local_http,
         LoadMonitor *load_monitor,
         LastValueCache* stats_aggregator);

  /// Destructor.
  ~Matrix() {}

  /// Create a MatrixTsx.
  SproutletTsx* get_tsx(SproutletTsxHelper* helper,
                        const std::string& alias,
                        pjsip_msg* req);

  MatrixTsx* get_tsx(std::string room_id);

// TODO: Should really be friend methods
//friend class MatrixTsx:
  void add_tsx(std::string room_id, MatrixTsx* tsx);
  void remove_tsx(std::string room_id);

  MatrixConnection* connection() { return &_connection; }
  const std::string& home_server() { return _home_server; }

private:
  std::string _home_server;
  std::map<std::string,MatrixTsx*> _tsx_map;
  MatrixTransactionHandler _transaction_handler;
  MatrixUserHandler _user_handler;
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
    Config(Matrix* _matrix, std::string _home_server, MatrixConnection* _connection) :
      matrix(_matrix), home_server(_home_server), connection(_connection) {}
    ~Config() {}
    Matrix* matrix;
    const std::string home_server;
    MatrixConnection* connection;
  };

  /// Constructor.
  MatrixTsx(SproutletTsxHelper* helper, Config& config);

  /// Destructor.
  ~MatrixTsx() { if (!_room_id.empty()) { _config.matrix->remove_tsx(_room_id); } }

  /// Implementation of SproutletTsx methods in matrix.
  virtual void on_rx_initial_request(pjsip_msg* req);
  virtual void on_rx_response(pjsip_msg* rsp, int fork_id);
  virtual void on_rx_in_dialog_request(pjsip_msg* req);
  virtual void on_rx_cancel(int status_code, pjsip_msg* cancel_req);
  virtual void on_timer_expiry(void* context);

  void rx_matrix_event(const std::string& type, const std::string& user, const std::string& call_id, const std::string& sdp);
  void add_call_id_to_trail(SAS::TrailId trail);

private:
  void add_record_route(pjsip_msg* msg);
  void add_contact(pjsip_msg* msg, pjsip_uri* uri);

  /// The config object for this transaction.
  Config _config;
  TimerID _timer_request;
  TimerID _timer_now;
  std::string _from_matrix_user;
  std::string _call_id;
  std::string _room_id;
  std::string _event_type;
  std::string _event;
  std::string _answer_sdp;
  int _expires;
};

/// Definition of the MatrixTsx class.
class MatrixOutboundTsx : public SproutletTsx
{
public:
  /// Config object for a MatrixOutboundTsx. Sets sensible defaults for all the
  /// fields.
  class Config
  {
  public:
    Config(Matrix* _matrix, std::string _home_server, MatrixConnection* _connection) :
      matrix(_matrix), home_server(_home_server), connection(_connection) {}
    ~Config() {}
    Matrix* matrix;
    const std::string home_server;
    MatrixConnection* connection;
  };

  /// Constructor.
  MatrixOutboundTsx(SproutletTsxHelper* helper, Config& config);

  /// Destructor.
  ~MatrixOutboundTsx() {}

  /// Implementation of SproutletTsx methods in matrix.
  virtual void on_rx_initial_request(pjsip_msg* req);
  virtual void on_rx_response(pjsip_msg* rsp, int fork_id);
  virtual void on_rx_in_dialog_request(pjsip_msg* req);

  void set_user(const std::string& user) { _user = user; }
  void set_room_id(const std::string& room_id) { _room_id = room_id; }
  void set_call_id(const std::string& call_id) { _call_id = call_id; }

private:
  /// The config object for this transaction.
  Config _config;
  std::string _user;
  std::string _room_id;
  std::string _call_id;

  void add_record_route(pjsip_msg* msg);
};
#endif
