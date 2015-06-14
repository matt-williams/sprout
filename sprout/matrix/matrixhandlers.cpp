/**
 * @file matrixhandlers.cpp HttpStack handlers for Matrix
 *
 * Project Clearwater - IMS in the cloud.
 * Copyright (C) 2015  Metaswitch Networks Ltd
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

#include "matrixhandlers.h"
#include "matrix.h"
#include "matrixutils.h"
#include "httpconnection.h"
#include "json_parse_utils.h"
#include <boost/algorithm/string/predicate.hpp>
#include "sproutletproxy.h"

const pjsip_method METHOD_MESSAGE = {(pjsip_method_e)PJSIP_OTHER_METHOD, {"MESSAGE", 7}};
const pj_str_t STR_MIME_TYPE_TEXT = pj_str((char*)"text");
const pj_str_t STR_MIME_SUBTYPE_PLAIN = pj_str((char*)"plain");
const pj_str_t STR_MIME_TYPE_APPLICATION = pj_str((char*)"application");
const pj_str_t STR_MIME_SUBTYPE_SDP = pj_str((char*)"sdp");

pj_status_t create_message(pjsip_tx_data*& req,
                           std::string from_uri,
                           std::string to_uri,
                           std::string message)
{
  LOG_DEBUG("Creating SIP MESSAGE for %s=>%s: %s", from_uri.c_str(), to_uri.c_str(), message.c_str());
  pj_str_t from;
  pj_str_t to;
  pj_cstr(&from, from_uri.c_str());
  pj_cstr(&to, to_uri.c_str());
  pj_status_t status = pjsip_endpt_create_request(stack_data.endpt,
                                                  &METHOD_MESSAGE,
                                                  &to,
                                                  &from,
                                                  &to,
                                                  &from,
                                                  NULL,
                                                  1,
                                                  NULL,
                                                  &req);
  LOG_DEBUG("pjsip_endpt_create_request returns %s", PJUtils::pj_status_to_string(status).c_str());
  if (status == PJ_SUCCESS)
  {
    pj_str_t body;
    pj_cstr(&body, message.c_str());
    req->msg->body = pjsip_msg_body_create(req->pool, &STR_MIME_TYPE_TEXT, &STR_MIME_SUBTYPE_PLAIN, &body);
  }

  // Remove spurious top Via.
  PJUtils::remove_top_via(req);

  return status;
}

pj_status_t create_invite(pjsip_tx_data*& req,
                          std::string from_uri,
                          std::string to_uri,
                          std::string sdp)
{
  LOG_DEBUG("Creating SIP INVITE for %s=>%s: %s", from_uri.c_str(), to_uri.c_str(), sdp.c_str());
  pj_str_t from;
  pj_str_t to;
  pj_cstr(&from, from_uri.c_str());
  pj_cstr(&to, to_uri.c_str());
  pj_status_t status = pjsip_endpt_create_request(stack_data.endpt,
                                                  pjsip_get_invite_method(),
                                                  &to,
                                                  &from,
                                                  &to,
                                                  &from,
                                                  NULL,
                                                  1,
                                                  NULL,
                                                  &req);
  LOG_DEBUG("pjsip_endpt_create_request returns %s", PJUtils::pj_status_to_string(status).c_str());
  if (status == PJ_SUCCESS)
  {
    pj_str_t body;
    pj_cstr(&body, sdp.c_str());
    req->msg->body = pjsip_msg_body_create(req->pool, &STR_MIME_TYPE_APPLICATION, &STR_MIME_SUBTYPE_SDP, &body);
  }

  // Remove spurious top Via.
  PJUtils::remove_top_via(req);

  return status;
}

//
// MatrixTransactionHandler methods.
//
void MatrixTransactionHandler::process_request(HttpStack::Request& req,
                                               SAS::TrailId trail)
{
  HTTPCode rc;

  rapidjson::Document doc;
  doc.Parse<0>(req.get_rx_body().c_str());

  if (!doc.HasParseError())
  {
    JSON_ASSERT_CONTAINS(doc, "events");
    JSON_ASSERT_ARRAY(doc["events"]);
    for (auto events = doc["events"].Begin();
         events != doc["events"].End();
         ++events)
    {
      std::string type;
      JSON_GET_STRING_MEMBER(*events, "type", type);

      std::string room;
      JSON_GET_STRING_MEMBER(*events, "room_id", room);

      if (type == "m.room.member")
      {
        std::string membership;
        JSON_ASSERT_CONTAINS(*events, "content");
        JSON_ASSERT_OBJECT((*events)["content"]);
        JSON_GET_STRING_MEMBER((*events)["content"], "membership", membership);
        if (membership == "invite")
        {
          std::string inviting_user;
          JSON_GET_STRING_MEMBER(*events, "user_id", inviting_user);

          std::string invited_user;
          JSON_GET_STRING_MEMBER(*events, "state_key", invited_user);

          HTTPCode rc = _matrix->connection()->join_room(invited_user, room, trail);
          std::string room_alias = MatrixUtils::get_room_alias(invited_user, inviting_user, _matrix->home_server());

          rc = _matrix->connection()->create_alias(room, room_alias, trail);
        }
      }
      else if (type == "m.room.message")
      {
        std::string from_user;
        JSON_GET_STRING_MEMBER(*events, "user_id", from_user);

        std::string to_user = "@sip_6505550812:matrix.mirw.cw-ngv.com"; // TODO: fix this!
        std::string to_uri = "sip:6505550812@mirw.cw-ngv.com"; // TODO: MatrixUtils::matrix_user_to_ims_uri(to_user)

        std::string message;
        JSON_ASSERT_CONTAINS(*events, "content");
        JSON_ASSERT_OBJECT((*events)["content"]);
        JSON_GET_STRING_MEMBER((*events)["content"], "body", message);

        if (from_user != to_user)
        {
          pjsip_tx_data* req;
          pj_status_t status = create_message(req,
                                              MatrixUtils::matrix_user_to_ims_uri(from_user),
                                              to_uri,
                                              message);
          if (status == PJ_SUCCESS)
          {
            MatrixOutboundTsx* matrix_tsx = (MatrixOutboundTsx*)_matrix->get_proxy()->create_uas_tsx(req, "scscf", trail);
          }
        }
      }
      else
      {
        // TODO Sort out locking - the MatrixTsx could be deleted under our feet
        MatrixTsx* tsx = _matrix->get_tsx(room);
        if (tsx != NULL)
        {
          tsx->add_call_id_to_trail(trail);

          std::string user;
          JSON_GET_STRING_MEMBER(*events, "user_id", user);

          std::string call_id;
          std::string sdp;
          if (boost::starts_with(type, "m.call"))
          {
            JSON_GET_STRING_MEMBER((*events)["content"], "call_id", call_id);

            JSON_ASSERT_CONTAINS(*events, "content");
            JSON_ASSERT_OBJECT((*events)["content"]);

            if (type == "m.call.invite")
            {
              JSON_ASSERT_CONTAINS((*events)["content"], "offer");
              JSON_ASSERT_OBJECT((*events)["content"]["offer"]);
              JSON_GET_STRING_MEMBER((*events)["content"]["offer"], "sdp", sdp);
            }
            else if (type == "m.call.answer")
            {
              JSON_ASSERT_CONTAINS((*events)["content"], "answer");
              JSON_ASSERT_OBJECT((*events)["content"]["answer"]);
              JSON_GET_STRING_MEMBER((*events)["content"]["answer"], "sdp", sdp);
            }
          }
          tsx->rx_matrix_event(type, user, call_id, sdp);
        }
        else if (type == "m.call.invite")
        {
          std::string from_user;
          JSON_GET_STRING_MEMBER(*events, "user_id", from_user);

          std::string to_user = "@sip_6505550812:matrix.mirw.cw-ngv.com"; // TODO: fix this!
          std::string to_uri = "sip:6505550812@mirw.cw-ngv.com"; // TODO: MatrixUtils::matrix_user_to_ims_uri(to_user)

          JSON_ASSERT_CONTAINS(*events, "content");
          JSON_ASSERT_OBJECT((*events)["content"]);

          std::string call_id;
          JSON_GET_STRING_MEMBER((*events)["content"], "call_id", call_id);

          std::string sdp;
          JSON_ASSERT_CONTAINS((*events)["content"], "offer");
          JSON_ASSERT_OBJECT((*events)["content"]["offer"]);
          JSON_GET_STRING_MEMBER((*events)["content"]["offer"], "sdp", sdp);

          if (from_user != to_user)
          {
            pjsip_tx_data* req;
            pj_status_t status = create_invite(req,
                                               MatrixUtils::matrix_user_to_ims_uri(from_user),
                                               to_uri,
                                               sdp);
            if (status == PJ_SUCCESS)
            {
              MatrixOutboundTsx* tsx = (MatrixOutboundTsx*)_matrix->get_proxy()->create_uas_tsx(req, "scscf", trail);
              LOG_DEBUG("tsx = %p", tsx);
              //tsx->set_user(to_user);
              //tsx->set_room_id(room);
              //tsx->set_call_id(call_id);
            }
          }
        }
        else
        {
          LOG_DEBUG("Ignoring event of type %s in empty room %s", type.c_str(), room.c_str());
        }
      }
    }

    // Return 200 OK and an empty JSON object.
    rc = HTTP_OK;
    req.add_content("{}");
  }
  else
  {
    LOG_INFO("Failed to parse Matrix-supplied JSON body: %s", req.get_rx_body().c_str());
    rc = HTTP_SERVER_ERROR;
  }

  req.send_reply(rc, trail);
}

//
// MatrixUserHandler methods.
//
void MatrixUserHandler::process_request(HttpStack::Request& req,
                                        SAS::TrailId trail)
{
  // TODO: run in a separate thread/async

  HTTPCode rc = HTTP_OK;
  const std::string prefix = "/matrix/users/%40";
  std::string path = req.path();
  LOG_DEBUG("Got request for %s - %d > %d?", path.c_str(), path.length(), prefix.length());
  if (path.length() > prefix.length())
  {
    int first_percent = path.find_first_of("%", prefix.length());
    LOG_DEBUG("first percent is %d - > %d?", first_percent, prefix.length());
    if (first_percent > prefix.length())
    {
      std::string user = path.substr(prefix.length(), first_percent - prefix.length());
      LOG_DEBUG("user = %s", user.c_str());
      rc = _matrix->connection()->register_user(user, trail);
    }
  }
  if (rc == HTTP_OK)
  {
    req.add_content("{}");
  }
  req.send_reply(rc, trail);
}
