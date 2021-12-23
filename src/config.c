#define G_LOG_USE_STRUCTURED
#define G_LOG_DOMAIN "opdesk-config"
#include <glib.h>
#include <json-glib/json-glib.h>

#include <stdarg.h>
#include "config.h"

#define PRINTER_NAME_DEFAULT     "3d printer"
#define OCTOPRINT_URL_DEFAULT    "http://octopi.local"
#define OCTOPRINT_APIKEY_DEFAULT "invalidapikey"

#define STATUS_NOTCONNECTED_DEFAULT "{printer-name}\nNot connected to OctoPrint"
#define STATUS_OFFLINE_DEFAULT      "{printer-name}\nPrinter offline"
#define STATUS_OFFLINEERROR_DEFAULT "{printer-name}\nPrinter offline after error"
#define STATUS_READY_DEFAULT        "{printer-name}\nPrinter ready\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
#define STATUS_CANCELLING_DEFAULT   "{printer-name}\nCancelling print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
#define STATUS_PAUSING_DEFAULT      "{printer-name}\nPausing print: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
#define STATUS_PAUSED_DEFAULT       "{printer-name}\nPrint Paused at {print-progress}: {print-filename}\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"
#define STATUS_PRINTING_DEFAULT     "{printer-name}\nPrinting: {print-filename}\n{print-progress}, {print-timeleft} remaining\nB: {temp-bed-actual} -> {temp-bed-target}\nT0: {temp-tool0-actual} -> {temp-tool0-target}"

struct _OPDeskConfig {
    GObject parent_instance;
    
    gchar *printer_name;
    gchar *octoprint_url;
    gchar *octoprint_api_key;

    struct {
        char *not_connected;
        char *offline;
        char *offline_error;
        char *ready;
        char *cancelling;
        char *pausing;
        char *paused;
        char *printing;
    } status_templates;

    GHashTable *event_notifications;
};

G_DEFINE_TYPE (OPDeskConfig, opdesk_config, G_TYPE_OBJECT)

typedef enum {
    CHANGED,
    N_SIGNALS
} OPDeskConfigSignals;

static guint obj_signals[N_SIGNALS] = { 0, };

struct OPDeskConfigEventNotification {
    char *event;
    GNotificationPriority priority;
    char *template;
};
typedef struct OPDeskConfigEventNotification OPDeskConfigEventNotification;

static OPDeskConfigEventNotification *opdesk_config_event_notification_new(const char *event, const gchar *priority, const char *template) {
    OPDeskConfigEventNotification *note = g_malloc0(sizeof(OPDeskConfigEventNotification));

    GNotificationPriority p;

    if(g_strcmp0(priority, "low")==0) p = G_NOTIFICATION_PRIORITY_LOW;
    else if (g_strcmp0(priority, "high")==0) p = G_NOTIFICATION_PRIORITY_HIGH;
    else if (g_strcmp0(priority, "normal")==0) p = G_NOTIFICATION_PRIORITY_NORMAL;
    else if (g_strcmp0(priority, "urgent")==0) p = G_NOTIFICATION_PRIORITY_URGENT;
    else {
        g_warning("Unknown notification priority: %s", priority);
        p = G_NOTIFICATION_PRIORITY_NORMAL;
    }

    note->event = g_strdup(event);
    note->template = g_strdup(template);
    note->priority = p;

    return note;
}

static void opdesk_config_event_notification_free(OPDeskConfigEventNotification *data) {
    g_free(data->event);
    g_free(data->template);
    g_free(data);
}


static void opdesk_config_dispose(GObject *object) {

    G_OBJECT_CLASS(opdesk_config_parent_class)->dispose(object);
}

static void opdesk_config_finalize(GObject *object) {
    OPDeskConfig *self = OPDESK_CONFIG(object);

    g_free(self->printer_name);
    g_free(self->octoprint_url);
    g_free(self->octoprint_api_key);

    g_free(self->status_templates.not_connected);
    g_free(self->status_templates.offline);
    g_free(self->status_templates.offline_error);
    g_free(self->status_templates.ready);
    g_free(self->status_templates.cancelling);
    g_free(self->status_templates.pausing);
    g_free(self->status_templates.paused);
    g_free(self->status_templates.printing);

    g_hash_table_destroy(self->event_notifications);
    
    G_OBJECT_CLASS(opdesk_config_parent_class)->finalize(object);
}

#define new_signal(a, b, c, ...) \
        g_signal_new( \
            a, \
            G_TYPE_FROM_CLASS(b), \
            G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS, \
            0, \
            NULL, \
            NULL, \
            NULL, \
            G_TYPE_NONE, \
            c, \
            __VA_ARGS__)

static void opdesk_config_class_init(OPDeskConfigClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = opdesk_config_dispose;
    object_class->finalize = opdesk_config_finalize;

    obj_signals[CHANGED] = new_signal("changed", object_class, 1, G_TYPE_STRING);
}

static void opdesk_config_init(OPDeskConfig *config) {
    config->printer_name = g_strdup(PRINTER_NAME_DEFAULT);
    config->octoprint_url = g_strdup(OCTOPRINT_URL_DEFAULT);
    config->octoprint_api_key = g_strdup(OCTOPRINT_APIKEY_DEFAULT);

    config->status_templates.not_connected = g_strdup(STATUS_NOTCONNECTED_DEFAULT);
    config->status_templates.offline = g_strdup(STATUS_OFFLINE_DEFAULT);
    config->status_templates.offline_error = g_strdup(STATUS_OFFLINEERROR_DEFAULT);
    config->status_templates.ready = g_strdup(STATUS_READY_DEFAULT);
    config->status_templates.cancelling = g_strdup(STATUS_CANCELLING_DEFAULT);
    config->status_templates.pausing = g_strdup(STATUS_PAUSING_DEFAULT);
    config->status_templates.paused = g_strdup(STATUS_PAUSED_DEFAULT);
    config->status_templates.printing = g_strdup(STATUS_PRINTING_DEFAULT);

    config->event_notifications = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (void(*)(void*))opdesk_config_event_notification_free);
}

OPDeskConfig *opdesk_config_new() {
    return g_object_new(OPDESK_TYPE_CONFIG, NULL);
}

#define load_if_present_string(a, b, c) if(json_object_has_member(a, b)) { g_free(c); c = g_strdup(json_object_get_string_member(a, b)); }

gboolean opdesk_config_load_from_json(OPDeskConfig *config, JsonObject *conf) {
    load_if_present_string(conf, "printerName", config->printer_name);
    load_if_present_string(conf, "octoprintURL", config->octoprint_url);
    load_if_present_string(conf, "apiKey", config->octoprint_api_key);

    if(json_object_has_member(conf, "statusText")) {
        JsonObject *status_text = json_object_get_object_member(conf, "statusText");

        load_if_present_string(status_text, "notConnected", config->status_templates.not_connected);
        load_if_present_string(status_text, "offline", config->status_templates.offline);
        load_if_present_string(status_text, "offlineError", config->status_templates.offline_error);
        load_if_present_string(status_text, "ready", config->status_templates.ready);
        load_if_present_string(status_text, "cancelling", config->status_templates.cancelling);
        load_if_present_string(status_text, "pausing", config->status_templates.pausing);
        load_if_present_string(status_text, "paused", config->status_templates.paused);
        load_if_present_string(status_text, "printing", config->status_templates.printing);
    }

    if(json_object_has_member(conf, "eventNotification")) {
        JsonArray *events = json_object_get_array_member(conf, "eventNotification");
        GList *event_ele_first = json_array_get_elements(events);
        GList *event_ele = event_ele_first;
        while(event_ele) {
            JsonObject *event_conf = json_node_get_object(event_ele->data);
            const gchar *event = json_object_get_string_member(event_conf, "event");
            const gchar *template = json_object_get_string_member(event_conf, "template");
            const gchar *priority = json_object_get_string_member(event_conf, "priority");
            OPDeskConfigEventNotification *notification = opdesk_config_event_notification_new(event, priority, template);
            g_hash_table_insert(config->event_notifications, g_strdup(event), notification);
            event_ele = event_ele->next;
        }
        g_list_free(event_ele_first);
    }

    
    
   return TRUE;
}

GList *opdesk_config_load_from_file(const char *file_path) {
    GList *servers = NULL;

    GError *error = NULL;
    JsonParser *parser = json_parser_new();

    if(!json_parser_load_from_file(parser, file_path, &error)) {
        g_critical("Couldn't load configuration from %s: %s", file_path, error->message);
        return FALSE;
    }

    JsonNode *root = json_parser_get_root(parser);

    if(JSON_NODE_HOLDS_ARRAY(root)) {
        JsonArray *arr = json_node_get_array(root);
        GList *server_list = json_array_get_elements(arr);
        GList *server = server_list;
        while(server) {
            OPDeskConfig *c = opdesk_config_new();
            JsonObject *o = json_node_get_object(server->data);
            opdesk_config_load_from_json(c, o);
            servers = g_list_append(servers, c);
            server = server->next;
        }
        g_list_free(server_list);
    } else if (JSON_NODE_HOLDS_OBJECT(root)) {
        OPDeskConfig *c = opdesk_config_new();
        JsonObject *o = json_node_get_object(root);
        opdesk_config_load_from_json(c, o);
        servers = g_list_append(servers, c);
    } else {
        g_critical("Couldn't load configuration from %s: root node must be an array or object.", file_path);
    }

    g_object_unref(parser);

    return servers;
}

#define change_and_emit(config, var, val, name) g_free(var); var = g_strdup(val); g_signal_emit(config, obj_signals[CHANGED], 0, name)

const char *opdesk_config_get_printer_name(OPDeskConfig *config) {
    return config->printer_name;
}
void opdesk_config_set_printer_name(OPDeskConfig *config, const char *printer_name) {
    change_and_emit(config, config->printer_name, printer_name, "printer_name");
}

const char *opdesk_config_get_octoprint_url(OPDeskConfig *config) {
    return config->octoprint_url;
}

void opdesk_config_set_octoprint_url(OPDeskConfig *config, const char *url) {
    change_and_emit(config, config->octoprint_url, url, "octoprint_url");
}

const char *opdesk_config_get_octoprint_api_key(OPDeskConfig *config) {
    return config->octoprint_api_key;
}

void opdesk_config_set_octoprint_api_key(OPDeskConfig *config, const char *api_key) {
    change_and_emit(config, config->octoprint_api_key, api_key, "octoprint_api_key");
}

const char *opdesk_config_get_status_template(OPDeskConfig *config, OPDeskConfigStatusTemplateType template_type) {
    switch(template_type) {
    case STATUS_TEMPLATE_NOT_CONNECTED: return config->status_templates.not_connected;
    case STATUS_TEMPLATE_OFFLINE:       return config->status_templates.offline;
    case STATUS_TEMPLATE_OFFLINE_ERROR: return config->status_templates.offline_error;
    case STATUS_TEMPLATE_READY:         return config->status_templates.ready;
    case STATUS_TEMPLATE_CANCELLING:    return config->status_templates.cancelling;
    case STATUS_TEMPLATE_PAUSING:       return config->status_templates.pausing;
    case STATUS_TEMPLATE_PAUSED:        return config->status_templates.paused;
    case STATUS_TEMPLATE_PRINTING:      return config->status_templates.printing;
    default: return NULL;
    }
}

gboolean opdesk_config_has_event_notification(OPDeskConfig *config, const char *const event_name) {
    return g_hash_table_contains(config->event_notifications, event_name);
}

const char *opdesk_config_get_event_notification_template(OPDeskConfig *config, const char *const event_name) {
    if (!opdesk_config_has_event_notification(config, event_name)) return NULL;

    OPDeskConfigEventNotification *notify = g_hash_table_lookup(config->event_notifications, event_name);
    return notify->template;
}

GNotificationPriority opdesk_config_get_event_notification_priority(OPDeskConfig *config, const char *const event_name) {
    if (!opdesk_config_has_event_notification(config, event_name)) return -1;

    OPDeskConfigEventNotification *notify = g_hash_table_lookup(config->event_notifications, event_name);
    return notify->priority;
}