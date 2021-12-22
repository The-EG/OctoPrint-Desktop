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

/* The temperature menu itself */
G_BEGIN_DECLS

#define OPDESK_TYPE_TEMP_MENU (opdesk_temp_menu_get_type())
G_DECLARE_FINAL_TYPE(OPDeskTempMenu, opdesk_temp_menu, OPDESK, TEMP_MENU, GtkMenuItem)

OPDeskTempMenu *opdesk_temp_menu_new(OctoPrintClient *client);

void opdesk_temp_menu_clear_menus(OPDeskTempMenu *temp_menu);
void opdesk_temp_menu_build_menus(OPDeskTempMenu *temp_menu);

G_END_DECLS
