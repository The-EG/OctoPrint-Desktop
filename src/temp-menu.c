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

#include "temp-menu.h"

#include "octoprint/client.h"


// Individual Menu Items, ie 'Bed' or 'Hotend 0'
// Declared here because it's not needed outside of the Temperature Menu

typedef enum {
    HEATER_TYPE_BED,
    HEATER_TYPE_CHAMBER,
    HEATER_TYPE_TOOL
} HeaterType;

G_BEGIN_DECLS

#define OPDESK_TYPE_TEMP_MENU_ITEM (opdesk_temp_menu_item_get_type())
G_DECLARE_FINAL_TYPE(OPDeskTempMenuItem, opdesk_temp_menu_item, OPDESK, TEMP_MENU_ITEM, GtkMenuItem)

GtkWidget *opdesk_temp_menu_item_new(OctoPrintClient *client, HeaterType heater_type, gint heater_num);

G_END_DECLS



struct _OPDeskTempMenu {
    GtkMenuItem parent_inst;

    OctoPrintClient *client;

    GtkWidget *sub_menu_root;
};

G_DEFINE_TYPE(OPDeskTempMenu, opdesk_temp_menu, GTK_TYPE_MENU_ITEM);

typedef enum {
    MENU_PROP_CLIENT = 1,
    N_PROPERTIES
} OPDeskTempMenuProperties;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void opdesk_temp_menu_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OPDeskTempMenu *self = OPDESK_TEMP_MENU(object);

    switch ((OPDeskTempMenuProperties)property_id) {
    case MENU_PROP_CLIENT:
        if(self->client) g_object_unref(self->client);
        self->client = g_value_get_object(value);
        if(self->client) g_object_ref(self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_temp_menu_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OPDeskTempMenu *self = OPDESK_TEMP_MENU(object);
    switch ((OPDeskTempMenuProperties)property_id) {
    case MENU_PROP_CLIENT:
        g_value_set_object(value, self->client);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_temp_menu_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_temp_menu_parent_class)->dispose(object);
}

static void opdesk_temp_menu_finalize(GObject *object) {
    OPDeskTempMenu *self = OPDESK_TEMP_MENU(object);

    if(self->client) g_object_unref(self->client);
    G_OBJECT_CLASS(opdesk_temp_menu_parent_class)->finalize(object);
}

static void opdesk_temp_menu_class_init(OPDeskTempMenuClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = opdesk_temp_menu_get_property;
    object_class->set_property = opdesk_temp_menu_set_property;
    object_class->dispose = opdesk_temp_menu_dispose;
    object_class->finalize = opdesk_temp_menu_finalize;

    obj_properties[MENU_PROP_CLIENT] = g_param_spec_object("client", "client", "OctoPrint Client instance", OCTOPRINT_TYPE_CLIENT, G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);
}

static void opdesk_temp_menu_init(OPDeskTempMenu *temp_menu) {
    temp_menu->sub_menu_root = gtk_menu_new();
    gtk_menu_item_set_label(GTK_MENU_ITEM(temp_menu), "Set Temperature");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(temp_menu), temp_menu->sub_menu_root);
}

OPDeskTempMenu *opdesk_temp_menu_new(OctoPrintClient *client) {
    return g_object_new(OPDESK_TYPE_TEMP_MENU, 
        "client", client,
        NULL);
}

static void clear_menus_destroy_item(GtkWidget *widget, gpointer data) {
    gtk_widget_destroy(widget);
}

void opdesk_temp_menu_clear_menus(OPDeskTempMenu *temp_menu) {
    gtk_container_foreach(GTK_CONTAINER(temp_menu->sub_menu_root), clear_menus_destroy_item, NULL);
}

void opdesk_temp_menu_build_menus(OPDeskTempMenu *temp_menu) {
    opdesk_temp_menu_clear_menus(temp_menu);
    gchar *profile_id = octoprint_client_get_current_profile(temp_menu->client);
    JsonObject *profile = octoprint_client_get_printer_profile(temp_menu->client, profile_id);
    g_free(profile_id);

    if(!profile) return;

    gboolean has_bed = json_object_get_boolean_member_with_default(profile, "heatedBed", FALSE);
    gboolean has_chamber = json_object_get_boolean_member_with_default(profile, "heatedChamber", FALSE);

    JsonObject *extruder = json_object_get_object_member(profile, "extruder");
    gint64 hotends = json_object_get_int_member(extruder, "count");

    if(has_bed) {
        GtkWidget *bed_item = opdesk_temp_menu_item_new(temp_menu->client, HEATER_TYPE_BED, 0);
        gtk_menu_item_set_label(GTK_MENU_ITEM(bed_item), "Bed");
        gtk_menu_shell_append(GTK_MENU_SHELL(temp_menu->sub_menu_root), bed_item);
        gtk_widget_show(bed_item);
    }

    if(has_chamber) {
        GtkWidget *chamber_item = opdesk_temp_menu_item_new(temp_menu->client, HEATER_TYPE_CHAMBER, 0);
        gtk_menu_item_set_label(GTK_MENU_ITEM(chamber_item), "Bed");
        gtk_menu_shell_append(GTK_MENU_SHELL(temp_menu->sub_menu_root), chamber_item);
        gtk_widget_show(chamber_item);
    }

    for(gint64 t=0;t<hotends;t++) {
        gchar *lbl = g_strdup_printf("Hotend %d", t);
        GtkWidget *tool_item = opdesk_temp_menu_item_new(temp_menu->client, HEATER_TYPE_TOOL, t);
        gtk_menu_item_set_label(GTK_MENU_ITEM(tool_item), lbl);
        g_free(lbl);
        gtk_menu_shell_append(GTK_MENU_SHELL(temp_menu->sub_menu_root), tool_item);
        gtk_widget_show(tool_item);
    }

    json_object_unref(profile);
}


/* Temp Menu Item */

struct _OPDeskTempMenuItem {
    GtkMenuItem parent_inst;

    OctoPrintClient *client;

    HeaterType heater_type;
    gint heater_num;
};

G_DEFINE_TYPE(OPDeskTempMenuItem, opdesk_temp_menu_item, GTK_TYPE_MENU_ITEM);

typedef enum {
    MENU_ITEM_PROP_CLIENT = 1,
    MENU_ITEM_PROP_HEATER_TYPE,
    MENU_ITEM_PROP_HEATER_NUM,
    MENU_ITEM_N_PROPERTIES
} OPDeskTempMenuItemProperties;

static GParamSpec *item_obj_properties[MENU_ITEM_N_PROPERTIES] = { NULL, };

static void opdesk_temp_menu_item_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OPDeskTempMenuItem *self = OPDESK_TEMP_MENU_ITEM(object);

    switch ((OPDeskTempMenuItemProperties)property_id) {
    case MENU_ITEM_PROP_CLIENT:
        if(self->client) g_object_unref(self->client);
        self->client = g_value_get_object(value);
        if(self->client) g_object_ref(self->client);
        break;
    case MENU_ITEM_PROP_HEATER_TYPE:
        self->heater_type = g_value_get_int(value);
        break;
    case MENU_ITEM_PROP_HEATER_NUM:
        self->heater_num = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_temp_menu_item_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OPDeskTempMenuItem *self = OPDESK_TEMP_MENU_ITEM(object);
    switch ((OPDeskTempMenuItemProperties)property_id) {
    case MENU_ITEM_PROP_CLIENT:
        g_value_set_object(value, self->client);
        break;
    case MENU_ITEM_PROP_HEATER_TYPE:
        g_value_set_int(value, self->heater_type);
        break;
    case MENU_ITEM_PROP_HEATER_NUM:
        g_value_set_int(value, self->heater_num);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void opdesk_temp_menu_item_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_temp_menu_parent_class)->dispose(object);
}

static void opdesk_temp_menu_item_finalize(GObject *object) {
    OPDeskTempMenuItem *self = OPDESK_TEMP_MENU_ITEM(object);

    if(self->client) g_object_unref(self->client);
    G_OBJECT_CLASS(opdesk_temp_menu_parent_class)->finalize(object);
}

static void opdesk_temp_menu_item_class_init(OPDeskTempMenuItemClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = opdesk_temp_menu_item_get_property;
    object_class->set_property = opdesk_temp_menu_item_set_property;
    object_class->dispose = opdesk_temp_menu_item_dispose;
    object_class->finalize = opdesk_temp_menu_item_finalize;

    item_obj_properties[MENU_ITEM_PROP_CLIENT] = g_param_spec_object("client", "client", "OctoPrint Client instance", OCTOPRINT_TYPE_CLIENT, G_PARAM_READWRITE);
    item_obj_properties[MENU_ITEM_PROP_HEATER_TYPE] = g_param_spec_int("heater-type", "heater type", "Heater Type", 0, 2, 0, G_PARAM_READWRITE);
    item_obj_properties[MENU_ITEM_PROP_HEATER_NUM] = g_param_spec_int("heater-num", "heater num", "Heater number", 0, 99, 0, G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, MENU_ITEM_N_PROPERTIES, item_obj_properties);
}

static void opdesk_temp_menu_item_on_activate(OPDeskTempMenuItem *mi, gpointer data) {
    

    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Set Target Temp",
        NULL,
        GTK_DIALOG_MODAL,
        "OK", GTK_RESPONSE_OK,
        "Cance", GTK_RESPONSE_CANCEL, NULL
    );

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    char *heater_name_str = NULL;

    switch(mi->heater_type) {
    case HEATER_TYPE_BED:
        heater_name_str = g_strdup("Bed");
        break;
    case HEATER_TYPE_CHAMBER:
        heater_name_str = g_strdup("Chamber");
        break;
    case HEATER_TYPE_TOOL:
        heater_name_str = g_strdup_printf("Hotend %d", mi->heater_num);
        break;
    default:
        heater_name_str = g_strdup("Error");
        break;
    }

    GtkWidget *lbl = gtk_label_new(heater_name_str);
    g_free(heater_name_str);

    GtkAdjustment *adjustment = gtk_adjustment_new(0, 0, 300, 1, 10, 0);
    GtkWidget *spinner = gtk_spin_button_new(adjustment, 10, 0);

    gtk_container_add(GTK_CONTAINER(content), lbl);
    gtk_container_add(GTK_CONTAINER(content), spinner);

    gtk_widget_show_all(dialog);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));

    if(response==GTK_RESPONSE_OK) {
        gint target = gtk_adjustment_get_value(adjustment);
        switch(mi->heater_type) {
        case HEATER_TYPE_BED:
            g_message("Bed target = %d", target);
            octoprint_client_set_bed_target(mi->client, target);
            break;
        case HEATER_TYPE_CHAMBER:
            g_message("Chamber target = %d", target);
            octoprint_client_set_chamber_target(mi->client, target);
            break;
        case HEATER_TYPE_TOOL:
            g_message("Hotend %d target = %d", mi->heater_num, target);
            octoprint_client_set_tool_target(mi->client, mi->heater_num, target);
            break;
        default:
            g_critical("invalid heater type");
            break;
        }
    }
    gtk_widget_destroy(dialog);
}

static void opdesk_temp_menu_item_init(OPDeskTempMenuItem *mi) {
    g_signal_connect(mi, "activate", G_CALLBACK(opdesk_temp_menu_item_on_activate), NULL);
}

GtkWidget *opdesk_temp_menu_item_new(OctoPrintClient *client, HeaterType heater_type, gint heater_num) {
    return g_object_new(OPDESK_TYPE_TEMP_MENU_ITEM, 
        "client", client,
        "heater-type", heater_type,
        "heater-num", heater_num,
        NULL);
}