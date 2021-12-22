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
#define G_LOG_DOMAIN "opdesk-app"
#include <glib.h>

#include "app.h"
#include "config.h"

#include "temp-menu.h"
#include "psu-menu.h"

#include "octoprint/client.h"
#include "octoprint/socket.h"

struct OPDeskAppTempData {
    char *name;
    float target;
    float actual;
    float offset;
};
typedef struct OPDeskAppTempData OPDeskAppTempData;

struct _OPDeskApp {
    GtkApplication parent_inst;

    GtkStatusIcon *tray_icon;
    GIcon *notification_icon;

    gboolean connected_to_op;
    gboolean no_retry;

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

    char *print_filename;
    float print_progress;
    float time_left;
    char *status_text;

    GHashTable *current_temps;

    struct {
        GtkWidget *menu_root;
        GtkWidget *label_item;

        GtkWidget *reload_socket;

        OPDeskPSUMenu *psu_menu;
        OPDeskTempMenu *temp_menu;

        GtkWidget *quit_item;        
    } tray_menu;

    gboolean have_psu_control;
    gboolean psu_is_on;

    gboolean have_display_layer_progress;
    gint64 current_layer;
    gint64 total_layers;

    JsonObject *event_payload;

    GSimpleAction *quit_action;

    OctoPrintClient *client;
    OctoPrintSocket *socket;

    char *config_path;
    OPDeskConfig *config;

    GRegex *message_pat;
    GRegex *template_var_pat;
};

G_DEFINE_TYPE(OPDeskApp, opdesk_app, GTK_TYPE_APPLICATION);

static void opdesk_app_temp_data_free(OPDeskAppTempData *data) {
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

static void on_socket_connected(OctoPrintSocket *socket, JsonObject *connected, OPDeskApp *app) {
    JsonObject *login = octoprint_client_login(app->client);

    if(login) {
        const gchar *name = json_object_get_string_member(login, "name");
        const gchar *session = json_object_get_string_member(login, "session");

        octoprint_socket_auth(app->socket, name, session);
        json_object_unref(login);

        opdesk_app_send_notification(app, G_NOTIFICATION_PRIORITY_LOW, "octoprint-socket-connected", "Connected to OctoPrint server");

        app->have_display_layer_progress = octoprint_client_plugin_enabled(app->client, "DisplayLayerProgress");

        if (app->have_display_layer_progress) {
            g_message("Display Layer Progress plugin detected, layer info available");
        } else {
            g_message("Display Layer Progress plugin not detected, layer info not available");
        }

        app->connected_to_op = TRUE;
        opdesk_temp_menu_build_menus(app->tray_menu.temp_menu);
    }    
}

static void on_tray_menu_map(GtkWidget *menu, OPDeskApp *app) {
    
}

static gboolean message_eval_cb(const GMatchInfo *info, GString *res, gpointer data) {
    OPDeskApp *app = data;
    gchar *match = g_match_info_fetch(info, 0);
    gchar *val = NULL;

    GMatchInfo *var_info;
    if(!g_regex_match(app->template_var_pat, match, 0, &var_info)) {
        g_warning("Unrecognized template variable format: %s, expecting '{category-name} or {category-name-detail}", match);
        g_free(match);
        return FALSE;
    }

    gchar *var_cat = g_match_info_fetch(var_info, 1);
    gchar *var_name = g_match_info_fetch(var_info, 2);
    gchar *var_detail = g_match_info_fetch(var_info, 3);

    if(g_strcmp0(var_cat, "printer")==0 && g_strcmp0(var_name, "name")==0) { // {printer-name}
        val = g_strdup(opdesk_config_get_printer_name(app->config));
    } else if(g_strcmp0(var_cat, "temp")==0) {
        if (g_hash_table_contains(app->current_temps, var_name)) {
            OPDeskAppTempData *temp = g_hash_table_lookup(app->current_temps, var_name);
            if(g_strcmp0(var_detail,"actual")==0) {
                val = g_strdup_printf("%0.1f", temp->actual);
            } else if(g_strcmp0(var_detail, "target")==0) {
                val = g_strdup_printf("%0.1f", temp->target);
            } else if(g_strcmp0(var_detail, "offset")==0) {
                val = g_strdup_printf("%0.1f", temp->offset);
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
            val = g_strdup(app->print_filename);
        } else if(g_strcmp0(var_name, "progress")==0) {
            val = g_strdup_printf("%0.1f%%", app->print_progress * 100);
        } else if(g_strcmp0(var_name, "timeleft")==0) {
            val = format_time_left(app->time_left);
        } else if(g_strcmp0(var_name, "currentLayer")==0) {
            if(app->have_display_layer_progress) {
                val = g_strdup_printf("%d", app->current_layer);
            } else {
                val = g_strdup("<NULL>");
            }
        } else if(g_strcmp0(var_name, "totalLayers")==0) {
            if(app->have_display_layer_progress) {
                val = g_strdup_printf("%d", app->total_layers);
            } else {
                val = g_strdup("<NULL>");
            }
        } else {
            g_warning("Unknown print variable name: %s", var_name);
            val = g_strdup("<unk-print>");
        }
    } else if(g_strcmp0(var_cat, "payload")==0) {
        if(json_object_has_member(app->event_payload, var_name)) {
            val = g_strdup(json_object_get_string_member(app->event_payload, var_name));
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

static char *opdesk_app_format_message(OPDeskApp *app, const char *message) {
    return g_regex_replace_eval(app->message_pat, message, -1, 0, 0, message_eval_cb, app, NULL);
}

static void opdesk_app_update_status(OPDeskApp *app) {
    gchar *new_status;
    OPDeskConfigStatusTemplateType template_type;
    
    if (!app->connected_to_op) {
        template_type = STATUS_TEMPLATE_NOT_CONNECTED;
    } else if (!app->state.operational) {
        if (app->state.error) {
            template_type = STATUS_TEMPLATE_OFFLINE_ERROR;
        } else {
            template_type = STATUS_TEMPLATE_OFFLINE;
        }
        gtk_widget_set_sensitive(GTK_WIDGET(app->tray_menu.temp_menu), FALSE);
    } else {
        if (app->state.cancelling) {
            template_type = STATUS_TEMPLATE_CANCELLING;
        } else if (app->state.pausing) {
            template_type = STATUS_TEMPLATE_PAUSING;
        } else if (app->state.paused) {
            template_type = STATUS_TEMPLATE_PAUSED;
        } else if (app->state.printing) {
            template_type = STATUS_TEMPLATE_PRINTING;
        } else {
            template_type = STATUS_TEMPLATE_READY;
        }
        gtk_widget_set_sensitive(GTK_WIDGET(app->tray_menu.temp_menu), TRUE);
    }
    const gchar *template = opdesk_config_get_status_template(app->config, template_type);
    new_status = opdesk_app_format_message(app, template);

    if (g_strcmp0(app->status_text, new_status)==0) {
        // no change in status
        g_free(new_status);
        return;
    }

    g_free(app->status_text);
    app->status_text = new_status;

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_status_icon_set_tooltip_text(GTK_STATUS_ICON(app->tray_icon), app->status_text);
    #pragma GCC diagnostic pop
    gtk_menu_item_set_label(GTK_MENU_ITEM(app->tray_menu.label_item), app->status_text);
}

static gboolean retry_connect(OPDeskApp *app) {
    if(octoprint_socket_is_connected(app->socket)) {
        g_warning("Already connected, not retrying!");
        return G_SOURCE_REMOVE;
    }
    octoprint_socket_connect(app->socket);
    return G_SOURCE_REMOVE;
}

static void on_socket_disconnected(OctoPrintSocket *socket, OPDeskApp *app) {
    opdesk_app_send_notification(app, G_NOTIFICATION_PRIORITY_URGENT, "octoprint-socket-disconnect", "Disconnected from OctoPrint Server");
    gtk_widget_set_sensitive(GTK_WIDGET(app->tray_menu.temp_menu), FALSE);
    app->connected_to_op = FALSE;

    if(app->no_retry) {
        app->no_retry = FALSE;
    } else {
        g_timeout_add_seconds(30, G_SOURCE_FUNC(retry_connect), app);
    }

    opdesk_app_update_status(app);
}

static void on_socket_error(OctoPrintSocket *socket, gchar *error, OPDeskApp *app) {
    opdesk_app_send_notification(app, G_NOTIFICATION_PRIORITY_URGENT, "octoprint-socket-error", "OctoPrint server error: %s", error);

    g_timeout_add_seconds(30, G_SOURCE_FUNC(retry_connect), app);
}

static void on_socket_current(OctoPrintSocket *socket, JsonObject *current, OPDeskApp *app) {
    JsonObject *state = json_object_get_object_member(current, "state");
    JsonObject *flags = json_object_get_object_member(state, "flags");

    app->state.operational = json_object_get_boolean_member(flags, "operational");
    app->state.paused = json_object_get_boolean_member(flags, "paused");
    app->state.printing = json_object_get_boolean_member(flags, "printing");
    app->state.pausing = json_object_get_boolean_member(flags, "pausing");
    app->state.cancelling = json_object_get_boolean_member(flags, "cancelling");
    app->state.sd_ready = json_object_get_boolean_member(flags, "sdReady");
    app->state.error = json_object_get_boolean_member(flags, "error");
    app->state.ready = json_object_get_boolean_member(flags, "ready");    
    

    JsonObject *job = json_object_get_object_member(current, "job");
    JsonObject *file = json_object_get_object_member(job, "file");
    if(file && json_object_has_member(file, "display")) {
        const gchar *filename = json_object_get_string_member(file, "display");

        if (g_strcmp0(app->print_filename, filename)) {
            g_free(app->print_filename);
            app->print_filename = g_strdup(filename);
        }
    }

    JsonObject *progress = json_object_get_object_member(current, "progress");
    gint64 print_time = json_object_get_int_member(progress, "printTime");
    gint64 print_time_left = json_object_get_int_member(progress, "printTimeLeft");

    app->time_left = print_time_left;

    if (print_time + print_time_left) app->print_progress = (float)print_time / (float)(print_time + print_time_left);
    else app->print_progress = .0f;

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
                OPDeskAppTempData *td = g_malloc0(sizeof(OPDeskAppTempData));
                td->name = g_strdup(temp_names->data);
                td->actual = json_object_get_double_member(data, "actual");
                if(json_object_has_member(data, "offset")) td->offset = json_object_get_double_member(data, "offset");
                td->target = json_object_get_double_member(data, "target");

                g_hash_table_insert(app->current_temps, g_strdup(temp_names->data), td);

                temp_names = temp_names->next;
            }
            g_list_free(temp_names_first);
        }
    }

    opdesk_app_update_status(app);
}

static void on_socket_plugin(OctoPrintSocket *socket, JsonObject *plugin, OPDeskApp *app) {
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
        app->current_layer = g_ascii_strtoll(cl, NULL, 10);
        app->total_layers = g_ascii_strtoll(tl, NULL, 10);
        opdesk_app_update_status(app);
    }
}

static void on_socket_event(OctoPrintSocket *socket, JsonObject *event, OPDeskApp *app) {
    const gchar *etype = json_object_get_string_member(event, "type");
    JsonObject *payload = json_object_get_object_member(event, "payload");
    g_debug("Got event of type %s", etype);

    if(g_strcmp0(etype, "Connected")==0) {
        opdesk_temp_menu_build_menus(app->tray_menu.temp_menu);
    } else if(g_strcmp0(etype, "Disconnected")==0) {
        opdesk_temp_menu_clear_menus(app->tray_menu.temp_menu);
    }

    if(!opdesk_config_has_event_notification(app->config, etype)) return;

    char *id = g_strdup_printf("octoprint-event-%s", etype);
    const char *template = opdesk_config_get_event_notification_template(app->config, etype);
    GNotificationPriority priority = opdesk_config_get_event_notification_priority(app->config, etype);

    app->event_payload = payload;
    char *msg = opdesk_app_format_message(app, template);
    app->event_payload = NULL;

    opdesk_app_send_notification(app, priority, id, msg);

    g_free(id);
    g_free(msg);
}

static void on_menu_quit(GtkWidget *widget, OPDeskApp *app) {
    // release our hold, allowing the app to close
    g_application_release(G_APPLICATION(app));
}

static void on_tray_icon_click(GtkWidget *widget, OPDeskApp *app) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_menu_popup(GTK_MENU(app->tray_menu.menu_root), NULL, NULL, gtk_status_icon_position_menu, app->tray_icon, 0, gtk_get_current_event_time());
    #pragma GCC disagnostic pop
}

static void on_reload_socket_activate(GtkWidget *widget, OPDeskApp *app) {
    if(octoprint_socket_is_connected(app->socket)) {
        app->no_retry = TRUE;
        octoprint_socket_disconnect(app->socket);
    } 
    octoprint_socket_connect(app->socket);
}

static void opdesk_app_activate(OPDeskApp *app, gpointer user_data) {
    g_message("Setting up application");

    // hold the application open without a window
    g_application_hold(G_APPLICATION(app));

    // Yes, we know GtkStatusIcon is deprecated because if GnomeShell
    // doesn't need it, then neither does anyone else, apparently 
    // (We do)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    app->tray_icon = gtk_status_icon_new_from_icon_name("octoprint-tentacle");
    gtk_status_icon_set_tooltip_text(GTK_STATUS_ICON(app->tray_icon), "OctoPrint Desktop");
    #pragma GCC diagnostic pop

    app->socket = octoprint_socket_new(opdesk_config_get_octoprint_url(app->config));
    app->client = octoprint_client_new(opdesk_config_get_octoprint_url(app->config), opdesk_config_get_octoprint_api_key(app->config));
    g_signal_connect(app->socket, "connected", G_CALLBACK(on_socket_connected), app);
    g_signal_connect(app->socket, "disconnected", G_CALLBACK(on_socket_disconnected), app);
    g_signal_connect(app->socket, "error", G_CALLBACK(on_socket_error), app);
    g_signal_connect(app->socket, "current", G_CALLBACK(on_socket_current), app);
    g_signal_connect(app->socket, "history", G_CALLBACK(on_socket_current), app);
    g_signal_connect(app->socket, "event", G_CALLBACK(on_socket_event), app);
    g_signal_connect(app->socket, "plugin", G_CALLBACK(on_socket_plugin), app);

    /* tray icon menu */
    app->tray_menu.menu_root = gtk_menu_new();
    g_signal_connect(app->tray_menu.menu_root, "map", G_CALLBACK(on_tray_menu_map), app);
    app->tray_menu.label_item = gtk_menu_item_new_with_label(opdesk_config_get_printer_name(app->config));  

    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), app->tray_menu.label_item);
    gtk_widget_set_sensitive(app->tray_menu.label_item, FALSE);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), sep);

    app->tray_menu.reload_socket = gtk_menu_item_new_with_label("(Re)connect to OctoPrint Server");
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), app->tray_menu.reload_socket);
    g_signal_connect(app->tray_menu.reload_socket, "activate", G_CALLBACK(on_reload_socket_activate), app);

    app->tray_menu.psu_menu = opdesk_psu_menu_new(app->client, app->socket);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), GTK_WIDGET(app->tray_menu.psu_menu));

    app->tray_menu.temp_menu = opdesk_temp_menu_new(app->client);
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), GTK_WIDGET(app->tray_menu.temp_menu));
    gtk_widget_set_sensitive(GTK_WIDGET(app->tray_menu.temp_menu), FALSE);

    sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), sep);

    app->tray_menu.quit_item = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(app->tray_menu.menu_root), app->tray_menu.quit_item);
    g_signal_connect(app->tray_menu.quit_item, "activate", G_CALLBACK(on_menu_quit), app);

    gtk_widget_show_all(app->tray_menu.menu_root);

    g_signal_connect(app->tray_icon, "activate", G_CALLBACK(on_tray_icon_click), app);

    opdesk_app_update_status(app);

    g_message("Connecting to OctoPrint server at %s", opdesk_config_get_octoprint_url(app->config));
    octoprint_socket_connect(app->socket);
}

static void opdesk_app_startup(OPDeskApp *app, gpointer user_data) {
    g_message("OctoPrint-Desktop Copyright © 2021 Taylor Talkington");
    g_message("This program comes with ABSOLUTELY NO WARRANTY; ");
    g_message("for details see LICENSE or ");
    g_message("====================================================");
    g_message("      OctoPrint Desktop Application Startup         ");
    g_message("----------------------------------------------------");

    app->notification_icon = g_themed_icon_new("octoprint-tentacle");

    const char *home_dir = g_get_home_dir();
    const char *conf_name = ".op-desktop.json";
    char *conf_path = g_build_path(G_DIR_SEPARATOR_S, home_dir, conf_name, NULL);   
    if (app->config_path) {
        g_free(conf_path);
        conf_path = app->config_path;
    }

    g_message("Loading config from %s", conf_path);
    app->config = opdesk_config_new_from_file(conf_path);

    app->current_temps = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (void(*)(void*))opdesk_app_temp_data_free);    
}

static void opdesk_app_shutdown(OPDeskApp *app, gpointer user_data) {
    g_message("----------------------------------------------------");
    g_message("       OctoPrint Desktop Application Shutdown       ");
    g_message("====================================================");
    g_object_unref(app->notification_icon);    
}

static void opdesk_app_init(OPDeskApp *app) {
    g_signal_connect(app, "activate", G_CALLBACK(opdesk_app_activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(opdesk_app_startup), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(opdesk_app_shutdown), NULL);

    app->message_pat = g_regex_new("(\\{[\\w\\w-]*\\})", G_REGEX_MULTILINE, 0, NULL);
    app->template_var_pat = g_regex_new("\\{(\\w*)-(\\w*)-?(\\w*)?\\}", G_REGEX_MULTILINE, 0, NULL);

    const GOptionEntry options[] = {
        {
            .long_name = "config",
            .description = "Configuration file path",
            .arg = G_OPTION_ARG_STRING,
            .arg_data = &app->config_path,
        },
        {NULL}
    };

    g_application_add_main_option_entries(G_APPLICATION(app), options);
}

static void opdesk_app_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_app_parent_class)->dispose(object);
}

static void opdesk_app_finalize(GObject *object) {
    OPDeskApp *self = OPDESK_APP_APPLICATION(object);

    g_object_unref(self->config);
    g_hash_table_destroy(self->current_temps);
    g_free(self->print_filename);
    g_free(self->status_text);
    g_regex_unref(self->message_pat);
    g_regex_unref(self->template_var_pat);
    G_OBJECT_CLASS(opdesk_app_parent_class)->finalize(object);
}

static void opdesk_app_class_init(OPDeskAppClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->finalize = opdesk_app_finalize;
    object_class->dispose = opdesk_app_dispose;
}

OPDeskApp *opdesk_app_new() {
    return g_object_new(OPDESK_TYPE_APP, 
        "application-id","io.github.the-eg.octoprint-desktop",
        "flags", G_APPLICATION_NON_UNIQUE,
        NULL);
}

void opdesk_app_send_notification(OPDeskApp *app, GNotificationPriority priority, const char *const id, const char *const message, ...) {
    va_list vl;
    va_start(vl, message);
    gchar *full_msg = g_strdup_vprintf(message, vl);
    va_end(vl);

    g_message(full_msg);

    gchar *title = g_strdup_printf("OctoPrint [%s]", opdesk_config_get_printer_name(app->config));
    GNotification *notification = g_notification_new(title);
    g_notification_set_priority(notification, priority);
    g_notification_set_body(notification, full_msg);
    g_notification_set_icon(notification, app->notification_icon);
    g_application_send_notification(G_APPLICATION(app), id, notification);
    g_free(title);
    g_object_unref(notification);
    g_free(full_msg);
}