/**
 * @file matrixconnection.cpp Connection to Matrix home server
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
#include "matrixconnection.h"

#include "json_parse_utils.h"

const std::string MatrixConnection::EVENT_TYPE_CALL_INVITE = "m.call.invite";
const std::string MatrixConnection::EVENT_TYPE_CALL_CANDIDATES = "m.call.candidates";
const std::string MatrixConnection::EVENT_TYPE_CALL_ANSWER = "m.call.answer";
const std::string MatrixConnection::EVENT_TYPE_CALL_HANGUP = "m.call.hangup";

MatrixConnection::MatrixConnection(const std::string& home_server,
                                   const std::string& as_token,
                                   HttpResolver* resolver,
                                   LoadMonitor *load_monitor,
                                   LastValueCache* stats_aggregator) :
  _http(home_server + ":8008",
        false, 
        resolver,
        "connected_matrix_home_servers",
        NULL, // load_monitor,
        stats_aggregator,
        SASEvent::HttpLogLevel::PROTOCOL,
        NULL),
  _as_token(as_token)
{
}

std::string MatrixConnection::build_register_user_req(const std::string &userpart)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("type");
    writer.String("m.login.application_service");

    writer.String("user");
    writer.String(userpart.c_str());

  }
  writer.EndObject();

  return sb.GetString();
}

HTTPCode MatrixConnection::register_user(const std::string& userpart,
                                         const SAS::TrailId trail)
{
  std::string path = "/_matrix/client/api/v1/register?access_token=" + _as_token;
  std::map<std::string,std::string> headers;
  std::string response;
  std::string body = build_register_user_req(userpart);

  HTTPCode rc = _http.send_post(path, headers, response, body, trail);

  // TODO Parse response

  return rc;
}

HTTPCode MatrixConnection::parse_get_room_rsp(const std::string& response,
                                              std::string& id)
{
  HTTPCode rc = HTTP_OK;

  rapidjson::Document doc;
  doc.Parse<0>(response.c_str());

  if (!doc.HasParseError())
  {
    JSON_ASSERT_CONTAINS(doc, "room_id");
    JSON_ASSERT_STRING(doc["room_id"]);
    id = doc["room_id"].GetString();
  }
  else
  {
    LOG_INFO("Failed to parse Matrix-supplied JSON body: %s", response.c_str());
    rc = HTTP_SERVER_ERROR;
  }

  return rc;
}

HTTPCode MatrixConnection::get_room_for_alias(const std::string& alias,
                                              std::string& id,
                                              SAS::TrailId trail)
{
  // TODO URL-encode # properly
  std::string path = "/_matrix/client/api/v1/directory/room/%23" + alias.substr(1) + "?access_token=" + _as_token;
  std::string response;

  HTTPCode rc = _http.send_get(path, response, "", trail);

  if (rc == HTTP_OK)
  {
    rc = parse_get_room_rsp(response, id);
  }
  else
  {
    // TODO Parse error response
  }

  return rc;
}

std::string MatrixConnection::build_create_room_req(const std::string& name,
                                                    const std::string& alias,
                                                    const std::vector<std::string>& invites,
                                                    const bool is_public,
                                                    const std::string& topic)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    if (!name.empty())
    {
      writer.String("name");
      writer.String(name.c_str());
    }

    if (!alias.empty())
    {
      writer.String("room_alias_name");
      writer.String(alias.c_str());
    }

    writer.String("invite");
    writer.StartArray();
    {
      for (auto invite = invites.begin(); invite != invites.end(); ++invite)
      {
        writer.String((*invite).c_str());
      }
    }
    writer.EndArray();

    writer.String("visibility");
    writer.String(is_public ? "public" : "private");

    if (!topic.empty())
    {
      writer.String("topic");
      writer.String(topic.c_str());
    }
  }
  writer.EndObject();

  return sb.GetString();
}

HTTPCode MatrixConnection::parse_create_room_rsp(const std::string& response,
                                                 std::string& id)
{
  HTTPCode rc = HTTP_OK;

  rapidjson::Document doc;
  doc.Parse<0>(response.c_str());

  if (!doc.HasParseError())
  {
    JSON_ASSERT_CONTAINS(doc, "room_id");
    JSON_ASSERT_STRING(doc["room_id"]);
    id = doc["room_id"].GetString();
  }
  else
  {
    LOG_INFO("Failed to parse Matrix-supplied JSON body: %s", response.c_str());
    rc = HTTP_SERVER_ERROR;
  }

  return rc;
}

HTTPCode MatrixConnection::create_room(const std::string& user,
                                       const std::string& name,
                                       const std::string& alias,
                                       const std::vector<std::string>& invites,
                                       std::string& id,
                                       const SAS::TrailId trail,
                                       bool is_public,
                                       const std::string& topic)
{
  std::string path = "/_matrix/client/api/v1/createRoom?access_token=" + _as_token + "&user_id=" + user;
  std::map<std::string,std::string> headers;
  std::string response;
  std::string body = build_create_room_req(name, alias, invites, is_public, topic);

  HTTPCode rc = _http.send_post(path, headers, response, body, trail);

  if (rc == HTTP_OK)
  {
    rc = parse_create_room_rsp(response, id);
  }
  else
  {
    // TODO Parse error response
  }

  return rc;
}

std::string MatrixConnection::build_create_alias_req(const std::string& id)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    if (!id.empty())
    {
      writer.String("room_id");
      writer.String(id.c_str());
    }
  }
  writer.EndObject();

  return sb.GetString();
}

HTTPCode MatrixConnection::create_alias(const std::string& id,
                                        const std::string& alias,
                                        const SAS::TrailId trail)
{
  // TODO URL-encode # properly
  std::string path = "/_matrix/client/api/v1/directory/room/%23" + alias.substr(1) + "?access_token=" + _as_token;
  std::map<std::string,std::string> headers;
  std::string response;
  std::string body = build_create_alias_req(id);

  HTTPCode rc = _http.send_put(path, headers, response, body, trail);

  // TODO Parse error response

  return rc;
}

std::string MatrixConnection::build_call_invite_event(const std::string& call_id,
                                                      const std::string& sdp,
                                                      const int lifetime)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("version");
    writer.Int(0);
    
    writer.String("call_id");
    writer.String(call_id.c_str());

    writer.String("offer");
    writer.StartObject();
    {
      writer.String("type");
      writer.String("offer");

      writer.String("sdp");
      writer.String(sdp.c_str());
    }
    writer.EndObject();

    writer.String("lifetime");
    writer.Int(lifetime);
  }
  writer.EndObject();

  return sb.GetString();
}

std::string MatrixConnection::build_call_candidates_event(const std::string& call_id,
                                                          const std::vector<std::string>& candidates)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("version");
    writer.Int(0);
    
    writer.String("call_id");
    writer.String(call_id.c_str());

    writer.String("rooms");
    writer.StartArray();
    {
      for (auto candidate = candidates.begin(); candidate != candidates.end(); candidate++)
      {
        writer.StartObject();
        {
          writer.String("sdpMid");
          writer.String("video");

          writer.String("sdpMLineIndex");
          writer.Int(1);

          writer.String("candidate");
          writer.String((*candidate).c_str());
        }
        writer.EndObject();
      }
    }
    writer.EndArray();
  }
  writer.EndObject();

  return sb.GetString();
}

std::string MatrixConnection::build_call_answer_event(const std::string& call_id,
                                                      const std::string& sdp)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("version");
    writer.Int(0);
    
    writer.String("call_id");
    writer.String(call_id.c_str());

    writer.String("answer");
    writer.StartObject();
    {
      writer.String("type");
      writer.String("answer");

      writer.String("sdp");
      writer.String(sdp.c_str());
    }
    writer.EndObject();
  }
  writer.EndObject();

  return sb.GetString();
}

std::string MatrixConnection::build_call_hangup_event(const std::string& call_id)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("version");
    writer.Int(0);
    
    writer.String("call_id");
    writer.String(call_id.c_str());
  }
  writer.EndObject();

  return sb.GetString();
}

HTTPCode MatrixConnection::send_event(const std::string& user,
                                      const std::string& room,
                                      const std::string& event_type,
                                      const std::string& event_body,
                                      const SAS::TrailId trail)
{
  std::string path = "/_matrix/client/api/v1/rooms/" + room + "/send/" + event_type + "?access_token=" + _as_token + "&user_id=" + user;
  std::map<std::string,std::string> headers;

  HTTPCode rc = _http.send_post(path, headers, event_body, trail);

  // TODO Parse response

  return rc;
}

std::string MatrixConnection::build_invite_user_req(const std::string& invited_user)
{
  rapidjson::StringBuffer sb;
  rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
  writer.StartObject();
  {
    writer.String("user_id");
    writer.String(invited_user.c_str());
  }
  writer.EndObject();

  return sb.GetString();
}

HTTPCode MatrixConnection::invite_user(const std::string& inviting_user,
                                       const std::string& invited_user,
                                       const std::string& room,
                                       const SAS::TrailId trail)
{
  std::string path = "/_matrix/client/api/v1/rooms/" + room + "/invite?access_token=" + _as_token + "&user_id=" + inviting_user;
  std::map<std::string,std::string> headers;
  std::string body = build_invite_user_req(invited_user);

  HTTPCode rc = _http.send_post(path, headers, body, trail);

  // TODO Parse response

  return rc;
}

HTTPCode MatrixConnection::join_room(const std::string& user,
                                     const std::string& room,
                                     const SAS::TrailId trail)
{
  std::string path = "/_matrix/client/api/v1/rooms/" + room + "/join?access_token=" + _as_token + "&user_id=" + user;
  std::map<std::string,std::string> headers;

  HTTPCode rc = _http.send_post(path, headers, "{}", trail);

  // TODO Parse response

  return rc;
}

