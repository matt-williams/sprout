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
#include <boost/regex.hpp>
#include "httpstack.h"

Matrix::Matrix(const std::string& home_server,
               const std::string& as_token,
               HttpResolver* resolver,
               const std::string local_http,
               LoadMonitor *load_monitor,
               LastValueCache* stats_aggregator) :
  Sproutlet("matrix"),
  _home_server(home_server),
  _tsx_map(),
  _transaction_handler(this),
  _user_handler(this),
  _connection(home_server,
              as_token,
              resolver,
              load_monitor,
              stats_aggregator)
{
  HttpStack* http_stack = HttpStack::get_instance();
  try
  {
    http_stack->register_handler("^/matrix/transactions/$",
                                 &_transaction_handler);
    http_stack->register_handler("^/matrix/users/$",
                                 &_user_handler);
  }
  catch (HttpStack::Exception& e)
  {
    LOG_ERROR("Caught HttpStack::Exception - %s - %d\n", e._func, e._rc);
  }
}

/// Creates a MatrixTsx instance.
SproutletTsx* Matrix::get_tsx(SproutletTsxHelper* helper,
                              const std::string& alias,
                              pjsip_msg* req)
{
  MatrixTsx::Config config(this, _home_server, &_connection);

  return new MatrixTsx(helper, config);
}

void Matrix::add_tsx(std::string room_id, MatrixTsx* tsx)
{
  _tsx_map[room_id] = tsx;
}

void Matrix::remove_tsx(std::string room_id)
{
  _tsx_map.erase(room_id);
}

MatrixTsx* Matrix::get_tsx(std::string room_id)
{
  MatrixTsx* tsx = NULL;
  auto tsxs = _tsx_map.find(room_id);
  if (tsxs != _tsx_map.end())
  {
    tsx = (*tsxs).second;
  }
  return tsx;
}

MatrixTsx::MatrixTsx(SproutletTsxHelper* helper, Config& config) :
  SproutletTsx(helper), _config(config)
{
}

std::string MatrixTsx::ims_uri_to_matrix_user(pjsip_uri* uri)
{
  std::string matrix_user = "";
  if (PJSIP_URI_SCHEME_IS_SIP(uri))
  {
    pjsip_sip_uri* sip_uri = (pjsip_sip_uri*)uri;
    matrix_user = "@sip_" + PJUtils::pj_str_to_string(&sip_uri->user) + ":" + _config.home_server;
  }
  else if (PJSIP_URI_SCHEME_IS_TEL(uri))
  {
    pjsip_tel_uri* tel_uri = (pjsip_tel_uri*)uri;
    matrix_user = "@tel_" + PJUtils::pj_str_to_string(&tel_uri->number) + ":" + _config.home_server;
  }
  return matrix_user;
}

std::string MatrixTsx::matrix_uri_to_matrix_user(pjsip_uri* uri)
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
    matrix_user = "@" + PJUtils::pj_str_to_string(&tel_uri->number) + ":" + _config.home_server;
  }
  return matrix_user;
}

std::string MatrixTsx::matrix_user_to_userpart(std::string user)
{
  return user.substr(1, user.find(':', 1) - 1);
}

void MatrixTsx::parse_sdp(const std::string& body,
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

pjsip_uri* MatrixTsx::get_from_uri(pjsip_msg* req)
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

void MatrixTsx::add_record_route(pjsip_msg* msg)
{
  pj_pool_t* pool = get_pool(msg);

  pjsip_param* param = PJ_POOL_ALLOC_T(pool, pjsip_param);
  pj_strdup2(pool, &param->name, "room");
  pj_strdup2(pool, &param->value, _room_id.c_str());

  pjsip_sip_uri* uri = get_reflexive_uri(pool);
  pj_list_insert_before(&uri->other_param, param);

  pjsip_route_hdr* rr = pjsip_rr_hdr_create(pool);
  rr->name_addr.uri = (pjsip_uri*)uri;

  pjsip_msg_insert_first_hdr(msg, (pjsip_hdr*)rr);
}

void MatrixTsx::add_contact(pjsip_msg* msg, pjsip_uri* uri)
{
  pjsip_contact_hdr* contact_hdr = pjsip_contact_hdr_create(get_pool(msg));
  contact_hdr->uri = (pjsip_uri*)uri;
  pjsip_msg_add_hdr(msg, (pjsip_hdr*)contact_hdr);
}

/// Matrix receives an initial request.
void MatrixTsx::on_rx_initial_request(pjsip_msg* req)
{
  // Find the called party in the request URI.
  pjsip_uri* to_uri = req->line.req.uri;
  if (!PJSIP_URI_SCHEME_IS_SIP(to_uri))
  {
    LOG_DEBUG("Request URI isn't a SIP URI");
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string to_matrix_user = matrix_uri_to_matrix_user(to_uri);

  pjsip_uri* from_uri = get_from_uri(req);
  if (from_uri == NULL)
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string from_matrix_user = ims_uri_to_matrix_user(from_uri);
  _from_matrix_user = from_matrix_user;

  // Get the call ID.
  pjsip_cid_hdr* call_id_hdr = (pjsip_cid_hdr*)pjsip_msg_find_hdr_by_name(req,
                                                                          &STR_CALL_ID,
                                                                          NULL);
  std::string call_id = PJUtils::pj_str_to_string(&call_id_hdr->id);
  std::string matrix_call_id = call_id;
  size_t at_pos = matrix_call_id.find('@');
  if (at_pos != matrix_call_id.npos)
  {
    matrix_call_id.replace(at_pos, 1, "-");
  }
  // TODO Should probably do this in the constructor.
  _call_id = call_id;

  // Get the expiry (if present).
  pjsip_expires_hdr* expires_hdr = (pjsip_expires_hdr*)pjsip_msg_find_hdr(req, PJSIP_H_EXPIRES, NULL);
  int expires = (expires_hdr != NULL) ? expires_hdr->ivalue : 180;
  _expires = expires;

  // Get the SDP body.
  if (req->body == NULL ||
      (pj_stricmp2(&req->body->content_type.type, "application") != 0) ||
      (pj_stricmp2(&req->body->content_type.subtype, "sdp") != 0))
  {
    LOG_DEBUG("No SDP body");
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  //std::string body = std::string((const char*)req->body->data, req->body->len);
  //std::string sdp;
  //std::vector<std::string> candidates;
  //parse_sdp(body, sdp, candidates);
  std::string sdp = std::string((const char*)req->body->data, req->body->len);
  _offer_sdp = sdp;

  LOG_DEBUG("Call from %s (%s) to %s (%s)", PJUtils::uri_to_string(PJSIP_URI_IN_ROUTING_HDR, from_uri).c_str(), from_matrix_user.c_str(), PJUtils::uri_to_string(PJSIP_URI_IN_REQ_URI, to_uri).c_str(), to_matrix_user.c_str());
  LOG_DEBUG("Call ID is %s, expiry is %d", call_id.c_str(), expires);
  LOG_DEBUG("SDP is %s", sdp.c_str());

  HTTPCode rc = _config.connection->register_user(matrix_user_to_userpart(from_matrix_user),
                                                  trail());
  // TODO Check and log response

  std::string from_alias = from_matrix_user.substr(1);
  from_alias = from_alias.replace(from_alias.find(":"), 1, "-");
  std::string to_alias = to_matrix_user.substr(1);
  to_alias = to_alias.replace(to_alias.find(":"), 1, "-"); 
  std::string room_alias = "#" + from_alias + "-" + to_alias + ":" + _config.home_server;
  rc = _config.connection->get_room_for_alias(room_alias,
                                              _room_id,
                                              trail());
  // TODO Check and log response

  if (_room_id.empty())
  {
    std::vector<std::string> invites;
    invites.push_back(to_matrix_user);
    rc = _config.connection->create_room(from_matrix_user,
                                         "Call from " + PJUtils::uri_to_string(PJSIP_URI_IN_ROUTING_HDR, from_uri),
                                         "",
                                         invites,
                                         _room_id,
                                         trail());

    // Work around https://matrix.org/jira/browse/SYN-340 by creating alias manually.
    rc = _config.connection->create_alias(_room_id,
                                          room_alias,
                                          trail());
  }
  else
  {
    // TODO Get members and, if not already a member, invite them
    _config.connection->invite_user(from_matrix_user,
                                    to_matrix_user,
                                    _room_id,
                                    trail());

    std::string call_invite_event = _config.connection->build_call_invite_event(_call_id,
                                                                                _offer_sdp,
                                                                                _expires * 1000);
    rc = _config.connection->send_event(_from_matrix_user,
                                        _room_id,
                                        MatrixConnection::EVENT_TYPE_CALL_INVITE,
                                        call_invite_event,
                                        trail());
  }

  _config.matrix->add_tsx(_room_id, this);

  //std::string call_candidates_event = _config.connection->build_call_candidates_event(call_id,
  //                                                                                    candidates);
  //rc = _config.connection->send_event(from_matrix_user,
  //                                    _room_id,
  //                                    MatrixConnection::EVENT_TYPE_CALL_CANDIDATES,
  //                                    call_candidates_event,
  //                                    trail());

  // TODO Only signal ringing when event is published or user enters room?
  pjsip_msg* rsp = create_response(req, PJSIP_SC_RINGING);
  add_record_route(rsp);
  add_contact(rsp, req->line.req.uri);
  send_response(rsp);
  free_msg(req);

  schedule_timer(NULL, _timer_request, expires * 1000);
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
  // Should never happen, but be on the safe side and just forward it.
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
  const pjsip_route_hdr* route = route_hdr();
  if ((route != NULL) &&
      (is_uri_reflexive(route->name_addr.uri)))
  {
    pjsip_sip_uri* uri = (pjsip_sip_uri*)route->name_addr.uri;
    pj_str_t room_str = pj_str("room");
    pjsip_param* param = pjsip_param_find(&uri->other_param, &room_str);
    if (param != NULL)
    {
      _room_id = PJUtils::pj_str_to_string(&param->value);
    }
  }

  pjsip_uri* from_uri = get_from_uri(req);
  if (from_uri == NULL)
  {
    pjsip_msg* rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
    send_response(rsp);
    free_msg(req);
    return;
  }
  std::string from_matrix_user = ims_uri_to_matrix_user(from_uri);

  // Get the call ID.
  pjsip_cid_hdr* call_id_hdr = (pjsip_cid_hdr*)pjsip_msg_find_hdr_by_name(req,
                                                                          &STR_CALL_ID,
                                                                          NULL);
  std::string call_id = PJUtils::pj_str_to_string(&call_id_hdr->id);

  if (req->line.req.method.id == PJSIP_BYE_METHOD)
  {
    std::string call_hangup_event = _config.connection->build_call_hangup_event(call_id);
    HTTPCode rc = _config.connection->send_event(from_matrix_user,
                                                 _room_id,
                                                 MatrixConnection::EVENT_TYPE_CALL_HANGUP,
                                                 call_hangup_event,
                                                 trail());

    pjsip_msg* rsp = create_response(req, PJSIP_SC_OK);
    send_response(rsp);
    free_msg(req);
  }
  else
  {
    // TODO: Support other methods.
    pjsip_msg* rsp = create_response(req, PJSIP_SC_NOT_IMPLEMENTED);
    send_response(rsp);
    free_msg(req);
  }
}

void MatrixTsx::rx_matrix_event(const std::string& type, const std::string& user, const std::string& call_id, const std::string& sdp)
{
  LOG_DEBUG("Received matrix event %s for user %s on call ID %s with SDP %s", type.c_str(), user.c_str(), call_id.c_str(), sdp.c_str());
  if ((type == "m.room.member") &&
      (user != _from_matrix_user))
  {
    // TODO: Adjust expires according to time passed
    std::string call_invite_event = _config.connection->build_call_invite_event(_call_id,
                                                                                _offer_sdp,
                                                                                _expires * 1000);
    HTTPCode rc = _config.connection->send_event(_from_matrix_user,
                                                 _room_id,
                                                 MatrixConnection::EVENT_TYPE_CALL_INVITE,
                                                 call_invite_event,
                                                 trail());
  }
  else if (type == "m.call.answer")
  {
    _answer_sdp = sdp;
    // TODO Not sure I'm supposed to call this method when not in context, but from code-reading it should work.
    // TODO Should maybe turn this into a formal "prod" API rather than explicitly abusing timers.
    cancel_timer(_timer_request);
    schedule_timer(NULL, _timer_now, 0);
  }
  else if (type == "m.call.hangup")
  {
    cancel_timer(_timer_request);
    schedule_timer(NULL, _timer_now, 0);
  }
  else
  {
    LOG_DEBUG("Ignoring event of type %s on call %s", type.c_str(), _call_id.c_str());
  }
}

void MatrixTsx::on_rx_cancel(int status_code, pjsip_msg* cancel_req)
{
  std::string call_hangup_event = _config.connection->build_call_hangup_event(_call_id);
  HTTPCode rc = _config.connection->send_event(_from_matrix_user,
                                               _room_id,
                                               MatrixConnection::EVENT_TYPE_CALL_HANGUP,
                                               call_hangup_event,
                                               trail());

  free_msg(cancel_req);
}

// TODO It would be nice if on_timer_expiry gave you the TimerId that had expired
void MatrixTsx::on_timer_expiry(void* context)
{
  pjsip_msg* req = original_request();
  pjsip_msg* rsp;
  if (!_answer_sdp.empty())
  {
    rsp = create_response(req, PJSIP_SC_OK);

    pj_str_t mime_type = pj_str("application");
    pj_str_t mime_subtype = pj_str("sdp");
    pj_str_t content;
    pj_cstr(&content, _answer_sdp.c_str());
    rsp->body = pjsip_msg_body_create(get_pool(req), &mime_type, &mime_subtype, &content);
  }
  else
  {
    rsp = create_response(req, PJSIP_SC_TEMPORARILY_UNAVAILABLE);
  }
  add_record_route(rsp);
  send_response(rsp);
  free_msg(req);
}

void MatrixTsx::add_call_id_to_trail(SAS::TrailId trail)
{
  SAS::Marker cid_marker(trail, MARKER_ID_SIP_CALL_ID, 1u);
  cid_marker.add_var_param(_call_id);
  SAS::report_marker(cid_marker, SAS::Marker::Scope::Trace);
}
