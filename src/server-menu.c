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
#define G_LOG_DOMAIN "opdesk-server-menu"
#include <glib.h>

#include "server-menu.h"
#include "psu-menu.h"
#include "temp-menu.h"
#include "octoprint/client.h"
#include "octoprint/socket.h"

struct _OPDeskServerMenu {
    GtkMenuItem parent_inst;

    OPDeskConfig *config;

    OctoPrintClient *client;
    OctoPrintSocket *socket;

    gboolean connected_to_op;
    gboolean no_retry;

    GIcon *notification_icon;

    struct {
        gboolean operational;
        gboolean paused;
        gboolean printing;
        gboolean pausing;
        gboolean cancelling;
        gboolean sd_ready;
        gboolean error;
        gboolean ready;
    } state;

    GtkWidget *submenu;
    GtkWidget *open_menu;
    GtkWidget *reconnect_menu;
    OPDeskPSUMenu *psu_menu;
    OPDeskTempMenu *temp_menu;


    char *print_filename;
    float print_progress;
    float time_left;
    char *status_text;

    GHashTable *current_temps;

    gboolean have_display_layer_progress;
    gint64 current_layer;
    gint64 total_layers;

    JsonObject *event_payload;

    GRegex *message_pat;
    GRegex *template_var_pat;
};

G_DEFINE_TYPE(OPDeskServerMenu, opdesk_server_menu, GTK_TYPE_MENU_ITEM);

typedef enum {
    MENU_PROP_CONFIG = 1,
    N_PROPERTIES
} OPDeskServerMenuProperties;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void opdesk_server_menu_dispose_config(OPDeskServerMenu *menu);
static void opdesk_server_menu_setup_config(OPDeskServerMenu *menu);
static char *opdesk_server_menu_format_message(OPDeskServerMenu *menu, const char *message);

struct OPDeskServerMenuTempData {
    char *name;
    float target;
    float actual;
    float offset;
};
typedef struct OPDeskServerMenuTempData OPDeskServerMenuTempData;

static void opdesk_server_menu_temp_data_free(OPDeskServerMenuTempData *data) {
    g_free(data->name);
    g_free(data);
}

static gchar *format_time_left(guint64 time_left) {
    guint days = 0;
    guint hours = 0;
    guint minutes = 0;

    days = time_left / 60 / 60 / 24;
    hours = (time_left - (days * 60 * 60 * 24)) / 60 / 60;
    minutes = (time_left - (days * 60 * 60 * 24) - (hours * 60 * 60)) / 60;

    gchar *days_str;
    gchar *hours_str;
    gchar *minutes_str;

    if (days) days_str = g_strdup_printf("%d days", days);
    else days_str = g_strdup("");
    if (hours) hours_str = g_strdup_printf("%d hours", hours);
    else hours_str = g_strdup("");
    minutes_str = g_strdup_printf("%d minutes", minutes);

    gchar *str = g_strdup_printf("%s %s %s", days_str, hours_str, minutes_str);
    g_strstrip(str);

    g_free(days_str);
    g_free(hours_str);
    g_free(minutes_str);

    return str;
}

static void opdesk_server_menu_update_status(OPDeskServerMenu *menu) {
    gchar *new_status;
    OPDeskConfigStatusTemplateType template_type;
    
    if (!menu->connected_to_op) {
        template_type = STATUS_TEMPLATE_NOT_CONNECTED;
    } else if (!menu->state.operational) {
        if (menu->state.error) {
            template_type = STATUS_TEMPLATE_OFFLINE_ERROR;
        } else {
            template_type = STATUS_TEMPLATE_OFFLINE;
        }
        gtk_widget_set_sensitive(GTK_WIDGET(menu->temp_menu), FALSE);
    } else {
        if (menu->state.cancelling) {
            template_type = STATUS_TEMPLATE_CANCELLING;
        } else if (menu->state.pausing) {
            template_type = STATUS_TEMPLATE_PAUSING;
        } else if (menu->state.paused) {
            template_type = STATUS_TEMPLATE_PAUSED;
        } else if (menu->state.printing) {
            template_type = STATUS_TEMPLATE_PRINTING;
        } else {
            template_type = STATUS_TEMPLATE_READY;
        }
        gtk_widget_set_sensitive(GTK_WIDGET(menu->temp_menu), TRUE);
    }
    const gchar *template = opdesk_config_get_status_template(menu->config, template_type);
    new_status = opdesk_server_menu_format_message(menu, template);

    if (g_strcmp0(menu->status_text, new_status)==0) {
        // no change in status
        g_free(new_status);
        return;
    }

    g_free(menu->status_text);
    menu->status_text = new_status;

    //gtk_menu_item_set_label(GTK_MENU_ITEM(menu), menu->status_text);
    GtkWidget *lbl = gtk_bin_get_child(GTK_BIN(menu));
    gtk_label_set_markup(GTK_LABEL(lbl), menu->status_text);

    /*
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_status_icon_set_tooltip_text(GTK_STATUS_ICON(app->tray_icon), app->status_text);
    #pragma GCC diagnostic pop
    gtk_menu_item_set_label(GTK_MENU_ITEM(app->tray_menu.label_item), app->status_text);
    */
}

static void opdesk_server_menu_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OPDeskServerMenu *self = OPDESK_SERVER_MENU(object);

    switch ((OPDeskServerMenuProperties)property_id) {
    case MENU_PROP_CONFIG:
        opdesk_server_menu_dispose_config(self);
        self->config = g_value_get_object(value);
        if(self->config) g_object_ref(self->config);
        opdesk_server_menu_setup_config(self);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_server_menu_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OPDeskServerMenu *self = OPDESK_SERVER_MENU(object);
    switch ((OPDeskServerMenuProperties)property_id) {
    case MENU_PROP_CONFIG:
        g_value_set_object(value, self->config);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_server_menu_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_server_menu_parent_class)->dispose(object);
}

static void opdesk_server_menu_finalize(GObject *object) {
    OPDeskServerMenu *self = OPDESK_SERVER_MENU(object);
    opdesk_server_menu_dispose_config(self);
    g_object_unref(self->notification_icon);
    g_hash_table_destroy(self->current_temps);
    g_free(self->print_filename);
    g_free(self->status_text);
    g_regex_unref(self->message_pat);
    g_regex_unref(self->template_var_pat);
    G_OBJECT_CLASS(opdesk_server_menu_parent_class)->finalize(object);
}

static void opdesk_server_menu_class_init(OPDeskServerMenuClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = opdesk_server_menu_get_property;
    object_class->set_property = opdesk_server_menu_set_property;
    object_class->dispose = opdesk_server_menu_dispose;
    object_class->finalize = opdesk_server_menu_finalize;

    obj_properties[MENU_PROP_CONFIG] = g_param_spec_object("config", "config", "OctoPrint Server Config", OPDESK_TYPE_CONFIG, G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

static void on_reconnect_activate(GtkWidget *widget, OPDeskServerMenu *menu) {
    if(menu->connected_to_op) {
        menu->no_retry = TRUE;
        octoprint_socket_disconnect(menu->socket);
    }

    octoprint_socket_connect(menu->socket);
}

static void on_open_activate(GtkWidget *widget, OPDeskServerMenu *menu) {
    const char *url = opdesk_config_get_octoprint_url(menu->config);
    g_message("Opening %s in a browser", url);
    g_app_info_launch_default_for_uri(url, NULL, NULL);
}

static void opdesk_server_menu_init(OPDeskServerMenu *menu) {
    
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu), "OctoPrint Server Instance");
    menu->notification_icon = g_themed_icon_new("octoprint-tentacle");
    menu->current_temps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (void(*)(void*))opdesk_server_menu_temp_data_free);
    menu->message_pat = g_regex_new("(\\{[\\w\\w-]*\\})", G_REGEX_MULTILINE, 0, NULL);
    menu->template_var_pat = g_regex_new("\\{(\\w*)-(\\w*)-?(\\w*)?\\}", G_REGEX_MULTILINE, 0, NULL);

    menu->submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu), menu->submenu);

    menu->open_menu = gtk_menu_item_new_with_label("Open OctoPrint");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu->submenu), menu->open_menu);
    g_signal_connect(menu->open_menu, "activate", G_CALLBACK(on_open_activate), menu);

    menu->psu_menu = opdesk_psu_menu_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu->submenu), GTK_WIDGET(menu->psu_menu));

    menu->temp_menu = opdesk_temp_menu_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu->submenu), GTK_WIDGET(menu->temp_menu));

    menu->reconnect_menu = gtk_menu_item_new_with_label("(Re)connect to OctoPrint server");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu->submenu), menu->reconnect_menu);
    g_signal_connect(menu->reconnect_menu, "activate", G_CALLBACK(on_reconnect_activate), menu);

    gtk_widget_show_all(menu->submenu);
}

OPDeskServerMenu *opdesk_server_menu_new(OPDeskConfig *config) {
    return g_object_new(OPDESK_TYPE_SERVER_MENU, 
        "config", config,
        NULL);
}

static void opdesk_server_menu_dispose_config(OPDeskServerMenu *menu) {
    if(!menu->config) return;
    g_message("Shutting down server connection for %s", opdesk_config_get_printer_name(menu->config));

    if (menu->socket) {
        if(octoprint_socket_is_connected(menu->socket)) octoprint_socket_disconnect(menu->socket);
        g_object_unref(menu->socket);
    }
    if (menu->client) g_object_unref(menu->client);
    if (menu->config) g_object_unref(menu->config);

    GValue nv = G_VALUE_INIT;
    g_value_init(&nv, G_TYPE_OBJECT);
    g_value_set_object(&nv, NULL);

    /*
    if(menu->psu_menu) g_object_set_property(G_OBJECT(menu->psu_menu), "socket", &nv);
    if(menu->psu_menu) g_object_set_property(G_OBJECT(menu->psu_menu), "client", &nv);
    if(menu->temp_menu) g_object_set_property(G_OBJECT(menu->temp_menu), "client", &nv);
    */

    menu->socket = NULL;
    menu->client = NULL;
    menu->config = NULL;
}

static void opdesk_server_menu_send_notification(OPDeskServerMenu *menu, GNotificationPriority priority, const char *const id, const char *const message, ...) {
    if(!menu->config) return; // during startup/shutdown

    va_list vl;
    va_start(vl, message);
    gchar *full_msg = g_strdup_vprintf(message, vl);
    va_end(vl);

    g_message(full_msg);

    GApplication *app = g_application_get_default();

    gchar *full_id = g_strdup_printf("octoprint-%s-%s", opdesk_config_get_printer_name(menu->config), id);

    gchar *title = g_strdup_printf("OctoPrint [%s]", opdesk_config_get_printer_name(menu->config));
    GNotification *notification = g_notification_new(title);
    g_notification_set_priority(notification, priority);
    g_notification_set_body(notification, full_msg);
    g_notification_set_icon(notification, menu->notification_icon);
    g_application_send_notification(G_APPLICATION(app), full_id, notification);
    g_free(full_id);
    g_free(title);
    g_object_unref(notification);
    g_free(full_msg);
}


static gboolean message_eval_cb(const GMatchInfo *info, GString *res, gpointer data) {
    OPDeskServerMenu *menu = data;
    gchar *match = g_match_info_fetch(info, 0);
    gchar *val = NULL;

    GMatchInfo *var_info;
    if(!g_regex_match(menu->template_var_pat, match, 0, &var_info)) {
        g_warning("Unrecognized template variable format: %s, expecting '{category-name} or {category-name-detail}", match);
        g_free(match);
        return FALSE;
    }

    gchar *var_cat = g_match_info_fetch(var_info, 1);
    gchar *var_name = g_match_info_fetch(var_info, 2);
    gchar *var_detail = g_match_info_fetch(var_info, 3);

    if(g_strcmp0(var_cat, "printer")==0 && g_strcmp0(var_name, "name")==0) { // {printer-name}
        val = g_strdup(opdesk_config_get_printer_name(menu->config));
    } else if(g_strcmp0(var_cat, "temp")==0) {
        if (g_hash_table_contains(menu->current_temps, var_name)) {
            OPDeskServerMenuTempData *temp = g_hash_table_lookup(menu->current_temps, var_name);
            if(g_strcmp0(var_detail,"actual")==0) {
                val = g_strdup_printf("%0.0f", temp->actual);
            } else if(g_strcmp0(var_detail, "target")==0) {
                val = g_strdup_printf("%0.0f", temp->target);
            } else if(g_strcmp0(var_detail, "offset")==0) {
                val = g_strdup_printf("%0.0f", temp->offset);
            } else {
                g_warning("Unknown detail for temperature value, should be one of: actual, target, offset");
                val = g_strdup("<unk-temp>");
            }
        } else {
            g_warning("Unknown temp variable name: %s", var_name);
            val = g_strdup("<unk-temp>");
        }
    } else if(g_strcmp0(var_cat, "print")==0) {
        if(g_strcmp0(var_name,"filename")==0) {
            val = g_strdup(menu->print_filename);
        } else if(g_strcmp0(var_name, "progress")==0) {
            val = g_strdup_printf("%0.1f%%", menu->print_progress * 100);
        } else if(g_strcmp0(var_name, "timeleft")==0) {
            val = format_time_left(menu->time_left);
        } else if(g_strcmp0(var_name, "currentLayer")==0) {
            if(menu->have_display_layer_progress) {
                val = g_strdup_printf("%d", menu->current_layer);
            } else {
                val = g_strdup("<NULL>");
            }
        } else if(g_strcmp0(var_name, "totalLayers")==0) {
            if(menu->have_display_layer_progress) {
                val = g_strdup_printf("%d", menu->total_layers);
            } else {
                val = g_strdup("<NULL>");
            }
        } else {
            g_warning("Unknown print variable name: %s", var_name);
            val = g_strdup("<unk-print>");
        }
    } else if(g_strcmp0(var_cat, "payload")==0) {
        if(json_object_has_member(menu->event_payload, var_name)) {
            val = g_strdup(json_object_get_string_member(menu->event_payload, var_name));
        } else {
            g_warning("Invalid payload variable: %s", var_name);
            val = g_strdup("<unk-payload>");
        }
    } else {
        g_warning("unknown template variable: %s", match);
        val = g_strdup("<unk>");
    }

    g_string_append(res, val);
    g_match_info_unref(var_info);
    g_free(match);
    g_free(val);
    g_free(var_cat);
    g_free(var_name);
    g_free(var_detail);

    return FALSE;
}

static char *opdesk_server_menu_format_message(OPDeskServerMenu *menu, const char *message) {
    return g_regex_replace_eval(menu->message_pat, message, -1, 0, 0, message_eval_cb, menu, NULL);
}

static gboolean retry_connect(OPDeskServerMenu *menu) {
    if(octoprint_socket_is_connected(menu->socket)) {
        g_warning("Already connected, not retrying!");
        return G_SOURCE_REMOVE;
    }
    octoprint_socket_connect(menu->socket);
    return G_SOURCE_REMOVE;
}

static void on_socket_connected(OctoPrintSocket *socket, JsonObject *connected, OPDeskServerMenu *menu) {
    JsonObject *login = octoprint_client_login(menu->client);

    if(login) {
        const gchar *name = json_object_get_string_member(login, "name");
        const gchar *session = json_object_get_string_member(login, "session");

        octoprint_socket_auth(menu->socket, name, session);
        json_object_unref(login);

        opdesk_server_menu_send_notification(menu, G_NOTIFICATION_PRIORITY_LOW, "socket-connected", "Connected to OctoPrint server");

        menu->have_display_layer_progress = octoprint_client_plugin_enabled(menu->client, "DisplayLayerProgress");

        if (menu->have_display_layer_progress) {
            g_message("Display Layer Progress plugin detected, layer info available");
        } else {
            g_message("Display Layer Progress plugin not detected, layer info not available");
        }

        menu->connected_to_op = TRUE;
        opdesk_temp_menu_build_menus(menu->temp_menu);
    }    
}

static void on_socket_disconnected(OctoPrintSocket *socket, OPDeskServerMenu *menu) {
    opdesk_server_menu_send_notification(menu, G_NOTIFICATION_PRIORITY_URGENT, "socket-disconnect", "Disconnected from OctoPrint Server");
    gtk_widget_set_sensitive(GTK_WIDGET(menu->temp_menu), FALSE);
    menu->connected_to_op = FALSE;

    if(menu->no_retry) {
        menu->no_retry = FALSE;
    } else {
        g_timeout_add_seconds(30, G_SOURCE_FUNC(retry_connect), menu);
    }

    opdesk_server_menu_update_status(menu);
}

static void on_socket_error(OctoPrintSocket *socket, gchar *error, OPDeskServerMenu *menu) {
    opdesk_server_menu_send_notification(menu, G_NOTIFICATION_PRIORITY_URGENT, "socket-error", "OctoPrint server error: %s", error);

    g_timeout_add_seconds(30, G_SOURCE_FUNC(retry_connect), menu);
}

static void on_socket_current(OctoPrintSocket *socket, JsonObject *current, OPDeskServerMenu *menu) {
    JsonObject *state = json_object_get_object_member(current, "state");
    JsonObject *flags = json_object_get_object_member(state, "flags");

    menu->state.operational = json_object_get_boolean_member(flags, "operational");
    menu->state.paused = json_object_get_boolean_member(flags, "paused");
    menu->state.printing = json_object_get_boolean_member(flags, "printing");
    menu->state.pausing = json_object_get_boolean_member(flags, "pausing");
    menu->state.cancelling = json_object_get_boolean_member(flags, "cancelling");
    menu->state.sd_ready = json_object_get_boolean_member(flags, "sdReady");
    menu->state.error = json_object_get_boolean_member(flags, "error");
    menu->state.ready = json_object_get_boolean_member(flags, "ready");    
    

    JsonObject *job = json_object_get_object_member(current, "job");
    JsonObject *file = json_object_get_object_member(job, "file");
    if(file && json_object_has_member(file, "display")) {
        const gchar *filename = json_object_get_string_member(file, "display");

        if (g_strcmp0(menu->print_filename, filename)) {
            g_free(menu->print_filename);
            menu->print_filename = g_strdup(filename);
        }
    }

    JsonObject *progress = json_object_get_object_member(current, "progress");
    gint64 print_time = json_object_get_int_member(progress, "printTime");
    gint64 print_time_left = json_object_get_int_member(progress, "printTimeLeft");

    menu->time_left = print_time_left;

    if (print_time + print_time_left) menu->print_progress = (float)print_time / (float)(print_time + print_time_left);
    else menu->print_progress = .0f;

    if (json_object_has_member(current, "temps")) {
        JsonArray *temps = json_object_get_array_member(current, "temps");
        GList *temp_first = json_array_get_elements(temps);
        GList *temp = temp_first;
        JsonObject *last_temp = NULL;
        guint64 last_temp_time = 0;
        while(temp) {
            JsonObject *t = json_node_get_object(temp->data);
            guint t_time = json_object_get_int_member(t, "time");
            if (last_temp_time < t_time) {
                last_temp = t;
                last_temp_time = t_time;
            }
            temp = temp->next;
        }
        g_list_free(temp_first);

        if (last_temp) {
            GList *temp_names_first = json_object_get_members(last_temp);
            GList *temp_names = temp_names_first;
            while(temp_names) {
                if(g_strcmp0(temp_names->data, "time")==0) {
                    temp_names = temp_names->next;
                    continue;
                }

                JsonObject *data = json_object_get_object_member(last_temp, temp_names->data);
                OPDeskServerMenuTempData *td = g_malloc0(sizeof(OPDeskServerMenuTempData));
                td->name = g_strdup(temp_names->data);
                td->actual = json_object_get_double_member(data, "actual");
                if(json_object_has_member(data, "offset")) td->offset = json_object_get_double_member(data, "offset");
                td->target = json_object_get_double_member(data, "target");

                g_hash_table_insert(menu->current_temps, g_strdup(temp_names->data), td);

                temp_names = temp_names->next;
            }
            g_list_free(temp_names_first);
        }
    }

    opdesk_server_menu_update_status(menu);
}

static void on_socket_plugin(OctoPrintSocket *socket, JsonObject *plugin, OPDeskServerMenu *menu) {
    JsonGenerator *gen = json_generator_new();
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_set_object(node, plugin);
    json_generator_set_root(gen, node);
    gchar *str = json_generator_to_data(gen, NULL);
    g_debug("Got plugin: %s", str);
    json_node_unref(node);
    g_object_unref(gen);
    g_free(str);

    const gchar *plugin_id = json_object_get_string_member(plugin, "plugin");

    if(g_strcmp0(plugin_id, "DisplayLayerProgress-websocket-payload")==0) {
        JsonObject *data = json_object_get_object_member(plugin, "data");
        const gchar *cl = json_object_get_string_member(data, "currentLayer");
        const gchar *tl = json_object_get_string_member(data, "totalLayer");
        menu->current_layer = g_ascii_strtoll(cl, NULL, 10);
        menu->total_layers = g_ascii_strtoll(tl, NULL, 10);
        opdesk_server_menu_update_status(menu);
    }
}

static void on_socket_event(OctoPrintSocket *socket, JsonObject *event, OPDeskServerMenu *menu) {
    const gchar *etype = json_object_get_string_member(event, "type");
    JsonObject *payload = json_object_get_object_member(event, "payload");
    g_debug("Got event of type %s", etype);

    if(g_strcmp0(etype, "Connected")==0) {
        opdesk_temp_menu_build_menus(menu->temp_menu);
    } else if(g_strcmp0(etype, "Disconnected")==0) {
        opdesk_temp_menu_clear_menus(menu->temp_menu);
    }

    if(!opdesk_config_has_event_notification(menu->config, etype)) {
        return;
    }

    char *id = g_strdup_printf("event-%s", etype);
    const char *template = opdesk_config_get_event_notification_template(menu->config, etype);
    GNotificationPriority priority = opdesk_config_get_event_notification_priority(menu->config, etype);

    menu->event_payload = payload;
    char *msg = opdesk_server_menu_format_message(menu, template);
    menu->event_payload = NULL;

    opdesk_server_menu_send_notification(menu, priority, id, msg);

    g_free(id);
    g_free(msg);
}

static void opdesk_server_menu_setup_config(OPDeskServerMenu *menu) {
    g_message("Setting up server connection for %s", opdesk_config_get_printer_name(menu->config));

    const char *url = opdesk_config_get_octoprint_url(menu->config);
    const char *key = opdesk_config_get_octoprint_api_key(menu->config);

    menu->client = octoprint_client_new(url, key);
    menu->socket = octoprint_socket_new(url);

    g_signal_connect(menu->socket, "connected", G_CALLBACK(on_socket_connected), menu);
    g_signal_connect(menu->socket, "disconnected", G_CALLBACK(on_socket_disconnected), menu);
    g_signal_connect(menu->socket, "error", G_CALLBACK(on_socket_error), menu);
    g_signal_connect(menu->socket, "history", G_CALLBACK(on_socket_current), menu);
    g_signal_connect(menu->socket, "current", G_CALLBACK(on_socket_current), menu);
    g_signal_connect(menu->socket, "plugin", G_CALLBACK(on_socket_plugin), menu);
    g_signal_connect(menu->socket, "event", G_CALLBACK(on_socket_event), menu);

    GValue socket = G_VALUE_INIT;
    GValue client = G_VALUE_INIT;
    g_value_init(&socket, G_TYPE_OBJECT);
    g_value_init(&client, G_TYPE_OBJECT);
    g_value_set_object(&socket, menu->socket);
    g_value_set_object(&client, menu->client);

    g_object_set_property(G_OBJECT(menu->psu_menu), "client", &client);
    g_object_set_property(G_OBJECT(menu->psu_menu), "socket", &socket);
    g_object_set_property(G_OBJECT(menu->temp_menu), "client", &client);

    octoprint_socket_connect(menu->socket);
}
