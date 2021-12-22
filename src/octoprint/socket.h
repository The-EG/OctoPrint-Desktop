// Copyright 2021 Taylor Talkington
// 
// This file is part of OctoPrint-Desktop.
//
// OctoPrint-Desktop is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// OctoPrint-Desktop is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with OctoPrint-Desktop.  If not, see <https://www.gnu.org/licenses/>.
#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define OCTOPRINT_TYPE_SOCKET octoprint_socket_get_type()
G_DECLARE_FINAL_TYPE (OctoPrintSocket, octoprint_socket, OCTOPRINT, SOCKET, GObject)

OctoPrintSocket *octoprint_socket_new(const char *const url);

void octoprint_socket_connect(OctoPrintSocket *socket);
void octoprint_socket_disconnect(OctoPrintSocket *socket);

gboolean octoprint_socket_is_connected(OctoPrintSocket *socket);

void octoprint_socket_auth(OctoPrintSocket *socket, const char *const user, const char *const session);

G_END_DECLS