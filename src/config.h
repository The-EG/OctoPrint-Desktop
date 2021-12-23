#pragma once
#include <glib-object.h>

G_BEGIN_DECLS

#define OPDESK_TYPE_CONFIG opdesk_config_get_type()
G_DECLARE_FINAL_TYPE (OPDeskConfig, opdesk_config, OPDESK, CONFIG, GObject)

GList *opdesk_config_load_from_file(const char *file_path);

const char *opdesk_config_get_printer_name(OPDeskConfig *config);
void opdesk_config_set_printer_name(OPDeskConfig *config, const char *printer_name);

const char *opdesk_config_get_octoprint_url(OPDeskConfig *config);
void opdesk_config_set_octoprint_url(OPDeskConfig *config, const char *url);

const char *opdesk_config_get_octoprint_api_key(OPDeskConfig *config);
void opdesk_config_set_octoprint_api_key(OPDeskConfig *config, const char *api_key);

enum OPDeskConfigStatusTemplateType {
    STATUS_TEMPLATE_NOT_CONNECTED,
    STATUS_TEMPLATE_OFFLINE,
    STATUS_TEMPLATE_OFFLINE_ERROR,
    STATUS_TEMPLATE_READY,
    STATUS_TEMPLATE_CANCELLING,
    STATUS_TEMPLATE_PAUSING,
    STATUS_TEMPLATE_PAUSED,
    STATUS_TEMPLATE_PRINTING
};
typedef enum OPDeskConfigStatusTemplateType OPDeskConfigStatusTemplateType;

const char *opdesk_config_get_status_template(OPDeskConfig *config, OPDeskConfigStatusTemplateType template_type);

gboolean opdesk_config_has_event_notification(OPDeskConfig *config, const char *const event_name);
const char *opdesk_config_get_event_notification_template(OPDeskConfig *config, const char *const event_name);
GNotificationPriority opdesk_config_get_event_notification_priority(OPDeskConfig *config, const char *const event_name);

G_END_DECLS