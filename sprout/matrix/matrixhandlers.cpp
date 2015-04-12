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
#include "httpconnection.h"
#include "json_parse_utils.h"
#include <boost/algorithm/string/predicate.hpp>

//
// MatrixTransactionHandler methods.
//
void MatrixTransactionHandler::process_request(HttpStack::Request& req,
                                               SAS::TrailId trail)
{
  HTTPCode rc = HTTP_OK;

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
      else
      {
        LOG_DEBUG("Ignoring event of type %s in empty room %s", type.c_str(), room.c_str());
      }
    }
  }
  else
  {
    LOG_INFO("Failed to parse Matrix-supplied JSON body: %s", req.get_rx_body().c_str());
    rc = HTTP_SERVER_ERROR;
  }

  req.send_reply(rc, trail);
}
