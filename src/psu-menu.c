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
#define G_LOG_DOMAIN "opdesk-psu-menu"
#include <glib.h>

#include "psu-menu.h"

struct _OPDeskPSUMenu {
    GtkMenuItem parent_inst;

    gulong on_connected_instance;
    gulong on_disconnected_instance;
    gulong on_plugin_instance;

    // plugins supported
    gboolean have_psu_control;

    gboolean psu_is_on;

    OctoPrintClient *client;
    OctoPrintSocket *socket;
};

G_DEFINE_TYPE(OPDeskPSUMenu, opdesk_psu_menu, GTK_TYPE_MENU_ITEM);

typedef enum {
    MENU_PROP_CLIENT = 1,
    MENU_PROP_SOCKET,
    N_PROPERTIES
} OPDeskPSUMenuProperties;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void opdesk_psu_menu_socket_connected(OctoPrintSocket *socket, JsonObject *data, OPDeskPSUMenu *menu);
static void opdesk_psu_menu_socket_disconnected(OctoPrintSocket *socket, OPDeskPSUMenu *menu);
static void opdesk_psu_menu_socket_plugin(OctoPrintSocket *socket, JsonObject *plugin, OPDeskPSUMenu *menu);

static void opdesk_psu_menu_activate(OPDeskPSUMenu *menu, gpointer data);

static void opdesk_psu_menu_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OPDeskPSUMenu *self = OPDESK_PSU_MENU(object);

    switch ((OPDeskPSUMenuProperties)property_id) {
    case MENU_PROP_CLIENT:
        if(self->client) g_object_unref(self->client);
        self->client = g_value_get_object(value);
        if(self->client) g_object_ref(self->client);
        break;
    case MENU_PROP_SOCKET:
        if(self->on_connected_instance) g_signal_handler_disconnect(self->socket, self->on_connected_instance);
        if(self->on_disconnected_instance) g_signal_handler_disconnect(self->socket, self->on_disconnected_instance);
        if(self->on_plugin_instance) g_signal_handler_disconnect(self->socket, self->on_plugin_instance);
        if(self->socket) g_object_unref(self->socket);
        self->socket = g_value_get_object(value);
        if(self->socket) {
            g_object_ref(self->socket);
            self->on_connected_instance = g_signal_connect(self->socket, "connected", G_CALLBACK(opdesk_psu_menu_socket_connected), self);
            self->on_disconnected_instance = g_signal_connect(self->socket, "disconnected", G_CALLBACK(opdesk_psu_menu_socket_disconnected), self);
            self->on_plugin_instance = g_signal_connect(self->socket, "plugin", G_CALLBACK(opdesk_psu_menu_socket_plugin), self);
        }
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_psu_menu_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OPDeskPSUMenu *self = OPDESK_PSU_MENU(object);
    switch ((OPDeskPSUMenuProperties)property_id) {
    case MENU_PROP_CLIENT:
        g_value_set_object(value, self->client);
        break;
    case MENU_PROP_SOCKET:
        g_value_set_object(value, self->socket);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_psu_menu_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_psu_menu_parent_class)->dispose(object);
}

static void opdesk_psu_menu_finalize(GObject *object) {
    OPDeskPSUMenu *self = OPDESK_PSU_MENU(object);

    if(self->client) g_object_unref(self->client);
    if(self->socket) g_object_unref(self->socket);
    G_OBJECT_CLASS(opdesk_psu_menu_parent_class)->finalize(object);
}

static void opdesk_psu_menu_class_init(OPDeskPSUMenuClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = opdesk_psu_menu_get_property;
    object_class->set_property = opdesk_psu_menu_set_property;
    object_class->dispose = opdesk_psu_menu_dispose;
    object_class->finalize = opdesk_psu_menu_finalize;

    obj_properties[MENU_PROP_CLIENT] = g_param_spec_object("client", "client", "OctoPrint Client instance", OCTOPRINT_TYPE_CLIENT, G_PARAM_READWRITE);
    obj_properties[MENU_PROP_SOCKET] = g_param_spec_object("socket", "socket", "OctoPrint Socket instance", OCTOPRINT_TYPE_SOCKET, G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

static void opdesk_psu_menu_init(OPDeskPSUMenu *menu) {
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu), "PSU Toggle");
    gtk_widget_set_sensitive(GTK_WIDGET(menu), FALSE);
    g_signal_connect(menu, "activate", G_CALLBACK(opdesk_psu_menu_activate), NULL);
}

OPDeskPSUMenu *opdesk_psu_menu_new(OctoPrintClient *client, OctoPrintSocket *socket) {
    return g_object_new(OPDESK_TYPE_PSU_MENU, 
        "client", client,
        "socket", socket,
        NULL);
}

static void opdesk_psu_menu_socket_connected(OctoPrintSocket *socket, JsonObject *data, OPDeskPSUMenu *menu) {
    g_debug("got socket connected");
    menu->have_psu_control = octoprint_client_plugin_enabled(menu->client, "psucontrol");

    // eventually check for other plugins, but for now PSU control
    if(menu->have_psu_control) {
        g_message("PSU Control plugin detected, PSU menu item enabled");
        gtk_widget_show(GTK_WIDGET(menu));
        // the menu item will be 'disabled' until we know if the PSU is on or off
        gtk_widget_set_sensitive(GTK_WIDGET(menu), FALSE);
    } else {
        g_message("No compatible PSU plugins found, PSU menu item disabled");
    }
}

static void opdesk_psu_menu_socket_disconnected(OctoPrintSocket *socket, OPDeskPSUMenu *menu) {
    // forget what we know about plugins
    menu->have_psu_control = FALSE;
    menu->psu_is_on = FALSE;

    gtk_widget_hide(GTK_WIDGET(menu));
    gtk_widget_set_sensitive(GTK_WIDGET(menu), FALSE);
}

static void opdesk_psu_menu_socket_plugin(OctoPrintSocket *socket, JsonObject *plugin, OPDeskPSUMenu *menu) {
    const gchar *plugin_id = json_object_get_string_member(plugin, "plugin");

    if(g_strcmp0(plugin_id, "psucontrol")==0) {
        JsonObject *data = json_object_get_object_member(plugin, "data");
        menu->psu_is_on = json_object_get_boolean_member(data, "isPSUOn");
    } else {
        return;
    }

    g_debug("PSU status = %s", menu->psu_is_on ? "ON" : "OFF");

    gtk_widget_set_sensitive(GTK_WIDGET(menu), TRUE);
    char *lbl = g_strdup_printf("Turn PSU %s", menu->psu_is_on ? "OFF" : "ON");
    gtk_menu_item_set_label(GTK_MENU_ITEM(menu), lbl);
    g_free(lbl);
}

static void opdesk_psu_menu_activate(OPDeskPSUMenu *menu, gpointer data) {

    // we shouldn't be able to get here without having support...but just in case
    if(!menu->have_psu_control) return;

    if(menu->psu_is_on) {
        char *yes = "Yes";
        char *no = "No";
        char *msg = "Are you sure you want to turn the printer off? If you have a print in progress, it will be ruined and it won't be recoverable.";

        GtkWidget *dialog = gtk_dialog_new_with_buttons("PSU Control", NULL, 0, yes, GTK_RESPONSE_YES, no, GTK_RESPONSE_NO, NULL);

        GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        GtkWidget *msg_lbl = gtk_label_new(msg);
        gtk_container_add(GTK_CONTAINER(content), msg_lbl);

        gtk_widget_show_all(dialog);

        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        if(response==GTK_RESPONSE_YES) {
            g_message("Turning PSU OFF");
            octoprint_client_psucontrol_turn_off(menu->client);
        }

        gtk_widget_destroy(dialog);
    } else {
        g_message("Turning PSU ON");
        octoprint_client_psucontrol_turn_on(menu->client);
    }
}