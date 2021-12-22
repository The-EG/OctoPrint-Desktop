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

#include <glib.h>
#include <json-glib/json-glib.h>

G_BEGIN_DECLS

#define OCTOPRINT_TYPE_CLIENT octoprint_client_get_type()
G_DECLARE_FINAL_TYPE(OctoPrintClient, octoprint_client, OCTOPRINT, CLIENT, GObject)

OctoPrintClient *octoprint_client_new(const char *const url, const char *const api_key);

JsonObject *octoprint_client_login(OctoPrintClient *client);

JsonObject *octoprint_client_pluginmanager_plugins(OctoPrintClient *client);
gboolean octoprint_client_plugin_enabled(OctoPrintClient *client, const char *const plugin_id);

JsonObject *octoprint_client_get_settings(OctoPrintClient *client);

gchar *octoprint_client_get_setting_string(OctoPrintClient *client, const char *const setting_path);

void octoprint_client_plugin_simple_api_command(OctoPrintClient *client, const char *const plugin_id, JsonNode *payload);

JsonObject *octoprint_client_get_connection(OctoPrintClient *client);
gchar *octoprint_client_get_current_profile(OctoPrintClient *client);
JsonObject *octoprint_client_get_printer_profile(OctoPrintClient *client, const gchar *profile_id);

void octoprint_client_set_bed_target(OctoPrintClient *client, gint target);
void octoprint_client_set_chamber_target(OctoPrintClient *client, gint target);
void octoprint_client_set_tool_target(OctoPrintClient *client, gint tool, gint target);

/* Plugin specific.
   NOTE: these assume the plugin is present and do nothing to verify that is true */
void octoprint_client_psucontrol_turn_on(OctoPrintClient *client);
void octoprint_client_psucontrol_turn_off(OctoPrintClient *client);

G_END_DECLS