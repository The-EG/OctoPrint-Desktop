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
#include <gtk/gtk.h>
#include "octoprint/client.h"
#include "octoprint/socket.h"

G_BEGIN_DECLS

#define OPDESK_TYPE_PSU_MENU (opdesk_psu_menu_get_type())
G_DECLARE_FINAL_TYPE(OPDeskPSUMenu, opdesk_psu_menu, OPDESK, PSU_MENU, GtkMenuItem)

OPDeskPSUMenu *opdesk_psu_menu_new(OctoPrintClient *client, OctoPrintSocket *socket);

G_END_DECLS
