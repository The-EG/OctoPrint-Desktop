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

#include "server-menu.h"
#include "temp-menu.h"
#include "psu-menu.h"

#include "octoprint/client.h"
#include "octoprint/socket.h"

struct _OPDeskApp {
    GtkApplication parent_inst;

    GtkStatusIcon *tray_icon;
    GIcon *notification_icon;

    GtkWidget *menu_root;
    GtkWidget *quit_mi;

    char *config_path;
};

G_DEFINE_TYPE(OPDeskApp, opdesk_app, GTK_TYPE_APPLICATION);


static void on_tray_menu_map(GtkWidget *menu, OPDeskApp *app) {
    
}

static void on_menu_quit(GtkWidget *widget, OPDeskApp *app) {
    // release our hold, allowing the app to close
    g_application_release(G_APPLICATION(app));
}

static void on_tray_icon_click(GtkWidget *widget, OPDeskApp *app) {
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    gtk_menu_popup(GTK_MENU(app->menu_root), NULL, NULL, gtk_status_icon_position_menu, app->tray_icon, 0, gtk_get_current_event_time());
    #pragma GCC disagnostic pop
}

static void opdesk_app_activate(OPDeskApp *app, gpointer user_data) {
    
}

static void opdesk_app_startup(OPDeskApp *app, gpointer user_data) {
    g_message("OctoPrint-Desktop Copyright © 2021 Taylor Talkington");
    g_message("This program comes with ABSOLUTELY NO WARRANTY; ");
    g_message("for details see LICENSE");
    g_message("====================================================");
    g_message("      OctoPrint Desktop Application Startup         ");
    g_message("----------------------------------------------------");

    app->notification_icon = g_themed_icon_new("octoprint-tentacle");

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

    /* tray icon menu */
    app->menu_root = gtk_menu_new();

    

    g_signal_connect(app->tray_icon, "activate", G_CALLBACK(on_tray_icon_click), app);

    const char *home_dir = g_get_home_dir();
    const char *conf_name = ".op-desktop.json";
    char *conf_path = g_build_path(G_DIR_SEPARATOR_S, home_dir, conf_name, NULL);   
    if (app->config_path) {
        g_free(conf_path);
        conf_path = app->config_path;
    }

    g_message("Loading config from %s", conf_path);
    GList *servers = opdesk_config_load_from_file(conf_path);
    
    while(servers) {
        OPDeskServerMenu *smi = opdesk_server_menu_new(servers->data);
        gtk_menu_shell_append(GTK_MENU_SHELL(app->menu_root), GTK_WIDGET(smi));
        servers = servers->next;
    }
    servers = g_list_first(servers);
    g_list_free(servers);

    GtkWidget *sep = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu_root), sep);

    app->quit_mi = gtk_menu_item_new_with_label("Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(app->menu_root), app->quit_mi);
    g_signal_connect(app->quit_mi, "activate", G_CALLBACK(on_menu_quit), app);

    gtk_widget_show_all(app->menu_root);
}

static void opdesk_app_shutdown(OPDeskApp *app, gpointer user_data) {
    g_object_unref(app->notification_icon);
    gtk_widget_destroy(GTK_WIDGET(app->menu_root));

    g_message("----------------------------------------------------");
    g_message("       OctoPrint Desktop Application Shutdown       ");
    g_message("====================================================");
}

static void opdesk_app_init(OPDeskApp *app) {
    g_signal_connect(app, "activate", G_CALLBACK(opdesk_app_activate), NULL);
    g_signal_connect(app, "startup", G_CALLBACK(opdesk_app_startup), NULL);
    g_signal_connect(app, "shutdown", G_CALLBACK(opdesk_app_shutdown), NULL);

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