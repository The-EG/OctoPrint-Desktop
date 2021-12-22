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
#define G_LOG_USE_STRUCTURED
#define G_LOG_DOMAIN "octoclient"
#include <glib.h>

#include <libsoup/soup.h>
#include "client.h"

struct _OctoPrintClient {
    GObject parent_instance;

    SoupSession *session;
    char *url;
    char *api_key;
};

G_DEFINE_TYPE (OctoPrintClient, octoprint_client, G_TYPE_OBJECT)

typedef enum {
    PROP_URL = 1,
    PROP_API_KEY,
    N_PROPERTIES
} OctoPrintClientProperty;

static GParamSpec *octoprint_client_properties[N_PROPERTIES] = { NULL, };

static void octoprint_client_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OctoPrintClient *self = OCTOPRINT_CLIENT(object);

    switch ((OctoPrintClientProperty)property_id) {
    case PROP_URL:
        g_free(self->url);
        self->url = g_value_dup_string(value);
        break;
    case PROP_API_KEY:
        g_free(self->api_key);
        self->api_key = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void octoprint_client_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OctoPrintClient *self = OCTOPRINT_CLIENT(object);
    switch ((OctoPrintClientProperty)property_id) {
    case PROP_URL:
        g_value_set_string(value, self->url);
        break;
    case PROP_API_KEY:
        g_value_set_string(value, self->api_key);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}


static void octoprint_client_dispose(GObject *object) {

    G_OBJECT_CLASS(octoprint_client_parent_class)->dispose(object);
}

static void octoprint_client_finalize(GObject *object) {
    OctoPrintClient *self = OCTOPRINT_CLIENT(object);

    g_free(self->url);
    g_free(self->api_key);
    g_object_unref(self->session);

    G_OBJECT_CLASS(octoprint_client_parent_class)->finalize(object);
}

static void octoprint_client_init(OctoPrintClient *client) {
    client->session = soup_session_new();
}

OctoPrintClient *octoprint_client_new(const char *const url, const char *const api_key) {
    return g_object_new(OCTOPRINT_TYPE_CLIENT,
        "url", url,
        "api-key", api_key,
        NULL);
}

static JsonObject *octoprint_client_perform(OctoPrintClient *client, const char *const method, const char *const path, JsonNode *data) {
    gchar *full_url = g_strdup_printf("%s%s", client->url, path);
    SoupMessage *msg = soup_message_new(method, full_url);

    soup_message_headers_append(msg->request_headers, "X-Api-Key", client->api_key);
    if(data) {
        JsonGenerator *gen = json_generator_new();
        json_generator_set_root(gen, data);
        gsize blen;
        gchar *body = json_generator_to_data(gen, &blen);
        g_object_unref(gen);

        soup_message_set_request(msg, "application/json", SOUP_MEMORY_TAKE, body, blen);
    }

    guint ret_code = soup_session_send_message(client->session, msg);
    g_free(full_url);

    if (ret_code >= 200 && ret_code < 300) {
        JsonObject *obj = NULL;
        if(msg->response_body->length) {
            JsonParser *parser = json_parser_new();
            json_parser_load_from_data(parser, msg->response_body->data, -1, NULL);

            JsonNode *root = json_parser_get_root(parser);
            obj = json_node_dup_object(root);
            g_object_unref(parser);
        }
        g_object_unref(msg);

        g_message("%s %s -> %d", method, path, ret_code);
        return obj;
    } else {
        g_object_unref(msg);
        g_warning("%s %s -> %d", method, path, ret_code);
        return NULL;
    }
}

JsonObject *octoprint_client_login(OctoPrintClient *client) {

    // build the body : {passive: true}
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "passive");
    json_builder_add_boolean_value(builder, TRUE);
    json_builder_end_object(builder);

    JsonNode *data = json_builder_get_root(builder);    

    JsonObject *ret = octoprint_client_perform(client, "POST", "/api/login", data);
    g_object_unref(builder);
    json_node_unref(data);

    return ret;
}

JsonObject *octoprint_client_pluginmanager_plugins(OctoPrintClient *client) {
    return octoprint_client_perform(client, "GET", "/plugin/pluginmanager/plugins", NULL);
}

gboolean octoprint_client_plugin_enabled(OctoPrintClient *client, const char *const plugin_id) {
    JsonObject *resp = octoprint_client_pluginmanager_plugins(client);

    if (resp==NULL) {
        g_warning("Couldn't determine if %s is enabled", plugin_id);
        return FALSE;
    }

    JsonArray *plugins = json_object_get_array_member(resp, "plugins");
    GList *plugin_member_first = json_array_get_elements(plugins);
    GList *plugin_member = plugin_member_first;
    while(plugin_member) {
        JsonObject *plugin = json_node_get_object(plugin_member->data);
        const gchar *pkey = json_object_get_string_member(plugin, "key");
        gboolean penabled = json_object_get_boolean_member(plugin, "enabled");

        if(g_strcmp0(pkey, plugin_id)==0) {
            g_list_free(plugin_member_first);
            json_object_unref(resp);
            g_debug("Checking if plugin is enabled: %s = %s", plugin_id, penabled ? "Yes" : "No");
            return penabled;
        }

        plugin_member = plugin_member->next;
    }
    g_list_free(plugin_member_first);

    json_object_unref(resp);

    g_debug("Checking if plugin is enabled: %s not installed (No)", plugin_id);
    return FALSE;
}

JsonObject *octoprint_client_get_settings(OctoPrintClient *client) {
    return octoprint_client_perform(client, "GET", "/api/settings", NULL);
}

gchar *octoprint_client_get_setting_string(OctoPrintClient *client, const char *const setting_path) {
    JsonObject *settings = octoprint_client_get_settings(client);

    if(!settings) return NULL;

    JsonNode *root = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(root, settings);

    GError *err;
    JsonNode *setting_value = json_path_query(setting_path, root, &err);
    
    if (!setting_value) {
        g_warning("Couldn't lookup octoprint setting %s: %s", setting_path, err->message);
    }

    JsonArray *matches = json_node_get_array(setting_value);
    const char *val = json_array_get_string_element(matches, 0);

    char *ret = NULL;

    if (val) ret = g_strdup(val);
    
    json_node_unref(root);
    json_node_unref(setting_value);
    json_object_unref(settings);

    g_debug("OctoPrint setting (string) %s => %s", setting_path, ret);

    return ret;
}

void octoprint_client_plugin_simple_api_command(OctoPrintClient *client, const char *const plugin_id, JsonNode *payload) {
    char *path = g_strdup_printf("/api/plugin/%s", plugin_id);

    octoprint_client_perform(client, "POST", path, payload);
    g_free(path);
}

void octoprint_client_psucontrol_turn_on(OctoPrintClient *client) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "command");
    json_builder_add_string_value(builder, "turnPSUOn");
    json_builder_end_object(builder);

    JsonNode *data = json_builder_get_root(builder);  

    octoprint_client_plugin_simple_api_command(client, "psucontrol", data);
    g_object_unref(builder);
}

void octoprint_client_psucontrol_turn_off(OctoPrintClient *client) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "command");
    json_builder_add_string_value(builder, "turnPSUOff");
    json_builder_end_object(builder);

    JsonNode *data = json_builder_get_root(builder);  

    octoprint_client_plugin_simple_api_command(client, "psucontrol", data);
    g_object_unref(builder);
}

static void octoprint_client_class_init(OctoPrintClientClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = octoprint_client_get_property;
    object_class->set_property = octoprint_client_set_property;
    object_class->dispose = octoprint_client_dispose;
    object_class->finalize = octoprint_client_finalize;

    octoprint_client_properties[PROP_URL] = g_param_spec_string("url", "URL", "The OctoPrint base URL.", NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);
    octoprint_client_properties[PROP_API_KEY] = g_param_spec_string("api-key", "API Key", "The OctoPrint API Key that can be used to access the REST API.", NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_properties(object_class, N_PROPERTIES, octoprint_client_properties);
}

JsonObject *octoprint_client_get_connection(OctoPrintClient *client) {
    return octoprint_client_perform(client, "GET", "/api/connection", NULL);
}

gchar *octoprint_client_get_current_profile(OctoPrintClient *client) {
    JsonObject *connection = octoprint_client_get_connection(client);

    if(!connection) return NULL;
    JsonObject *current = json_object_get_object_member(connection, "current");
    const gchar *profile = json_object_get_string_member(current, "printerProfile");

    gchar *ret = g_strdup(profile);

    json_object_unref(connection);
    return ret;
}

JsonObject *octoprint_client_get_printer_profile(OctoPrintClient *client, const gchar *profile_id) {
    char *path = g_strdup_printf("/api/printerprofiles/%s", profile_id);
    JsonObject *resp = octoprint_client_perform(client, "GET", path, NULL);

    g_free(path);

    return resp;
}

void octoprint_client_set_bed_target(OctoPrintClient *client, gint target) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "command");
    json_builder_add_string_value(builder, "target");
    json_builder_set_member_name(builder, "target");
    json_builder_add_int_value(builder, target);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    octoprint_client_perform(client, "POST", "/api/printer/bed", root);
    g_object_unref(builder);
}
void octoprint_client_set_chamber_target(OctoPrintClient *client, gint target) {
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "command");
    json_builder_add_string_value(builder, "target");
    json_builder_set_member_name(builder, "target");
    json_builder_add_int_value(builder, target);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    octoprint_client_perform(client, "POST", "/api/printer/chamber", root);
    g_object_unref(builder);
}

void octoprint_client_set_tool_target(OctoPrintClient *client, gint tool, gint target) {
    char *tooln = g_strdup_printf("tool%d", tool);

    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "command");
    json_builder_add_string_value(builder, "target");
    json_builder_set_member_name(builder, "targets");
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, tooln);
    json_builder_add_int_value(builder, target);
    json_builder_end_object(builder);
    json_builder_end_object(builder);

    JsonNode *root = json_builder_get_root(builder);

    octoprint_client_perform(client, "POST", "/api/printer/tool", root);
    g_free(tooln);
    g_object_unref(builder);
}
 