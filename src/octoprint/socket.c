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
#define G_LOG_DOMAIN "octosocket"
#include <glib.h>
#include <stdarg.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include "socket.h"

struct _OctoPrintSocket {
    GObject parent_instance;

    char *url;
    SoupSession *session;
    SoupWebsocketConnection *websocket;

    gboolean connected;
};

G_DEFINE_TYPE (OctoPrintSocket, octoprint_socket, G_TYPE_OBJECT)

typedef enum {
    CONNECTED,
    DISCONNECTED,
    ERROR,
    HISTORY,
    CURRENT,
    EVENT,
    PLUGIN,
    TIMELAPSE,
    RENDERPROGRESS,
    N_SIGNALS
} OctoPrintSocketSignal;

static guint obj_signals[N_SIGNALS] = { 0, };

typedef enum {
    PROP_URL = 1,
    N_PROPERTIES
} OctoPrintSocketProperty;

static GParamSpec *obj_properties[N_PROPERTIES] = { NULL, };

static void octoprint_socket_set_property(GObject *object, guint property_id, const GValue *value, GParamSpec *pspec) {
    OctoPrintSocket *self = OCTOPRINT_SOCKET(object);

    switch ((OctoPrintSocketProperty)property_id) {
    case PROP_URL:
        g_free(self->url);
        self->url = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void octoprint_socket_get_property(GObject *object, guint property_id, GValue *value, GParamSpec *pspec) {
    OctoPrintSocket *self = OCTOPRINT_SOCKET(object);
    switch ((OctoPrintSocketProperty)property_id) {
    case PROP_URL:
        g_value_set_string(value, self->url);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, property_id, pspec);
        break;
    }
}

static void octoprint_socket_dispose(GObject *object) {

    G_OBJECT_CLASS(octoprint_socket_parent_class)->dispose(object);
}

static void octoprint_socket_finalize(GObject *object) {
    OctoPrintSocket *self = OCTOPRINT_SOCKET(object);

    g_free(self->url);
    g_object_unref(self->session);
    if(self->websocket) g_object_unref(self->websocket);
    G_OBJECT_CLASS(octoprint_socket_parent_class)->finalize(object);
}

#define octoprint_socket_signal(a, b, c, ...) \
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

static void octoprint_socket_class_init(OctoPrintSocketClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->get_property = octoprint_socket_get_property;
    object_class->set_property = octoprint_socket_set_property;
    object_class->dispose = octoprint_socket_dispose;
    object_class->finalize = octoprint_socket_finalize;

    obj_properties[PROP_URL] = g_param_spec_string("url", "URL", "The OctoPrint base URL.", NULL, G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_properties (object_class, N_PROPERTIES, obj_properties);

    obj_signals[CONNECTED] = octoprint_socket_signal("connected", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[DISCONNECTED] = octoprint_socket_signal("disconnected", object_class, 0, NULL);
    obj_signals[ERROR] = octoprint_socket_signal("error", object_class, 1, G_TYPE_STRING);
    obj_signals[HISTORY] = octoprint_socket_signal("history", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[CURRENT] = octoprint_socket_signal("current", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[EVENT] = octoprint_socket_signal("event", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[PLUGIN] = octoprint_socket_signal("plugin", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[TIMELAPSE] = octoprint_socket_signal("timelapse", object_class, 1, JSON_TYPE_OBJECT);
    obj_signals[RENDERPROGRESS] = octoprint_socket_signal("renderProgress", object_class, 1, JSON_TYPE_OBJECT);
}

static void octoprint_socket_init(OctoPrintSocket *socket) {
    socket->session = soup_session_new();
}

OctoPrintSocket *octoprint_socket_new(const char *const url) {
    return g_object_new(OCTOPRINT_TYPE_SOCKET, 
        "url", url,
        NULL);
}

static void octoprint_socket_on_ws_closed(SoupWebsocketConnection *ws, OctoPrintSocket *socket) {
    gushort code = soup_websocket_connection_get_close_code(ws);
    gchar *error_msg;

    switch(code) {
    case SOUP_WEBSOCKET_CLOSE_NORMAL:
        error_msg = "normal close";
        break;
    case SOUP_WEBSOCKET_CLOSE_GOING_AWAY:
        error_msg = "going away";
        break;
    case SOUP_WEBSOCKET_CLOSE_PROTOCOL_ERROR:
        error_msg = "protocol error";
        break;
    case SOUP_WEBSOCKET_CLOSE_UNSUPPORTED_DATA:
        error_msg = "unsupported data";
        break;
    case SOUP_WEBSOCKET_CLOSE_NO_STATUS:
        error_msg = "no status";
        break;
    case SOUP_WEBSOCKET_CLOSE_ABNORMAL:
        error_msg = "abnormal";
        break;
    case SOUP_WEBSOCKET_CLOSE_BAD_DATA:
        error_msg = "bad data";
        break;
    case SOUP_WEBSOCKET_CLOSE_POLICY_VIOLATION:
        error_msg = "policy violation";
        break;
    case SOUP_WEBSOCKET_CLOSE_TOO_BIG:
        error_msg = "too big";
        break;
    case SOUP_WEBSOCKET_CLOSE_NO_EXTENSION:
        error_msg = "no extension";
        break;
    case SOUP_WEBSOCKET_CLOSE_SERVER_ERROR:
        error_msg = "server error";
        break;
    case SOUP_WEBSOCKET_CLOSE_TLS_HANDSHAKE:
        error_msg = "tls handshake";
        break;
    default:
        error_msg = "unknown message";
        break;

    }
    socket->connected = FALSE;
    g_warning("Disconnected from socket: %s, %s", error_msg, soup_websocket_connection_get_close_data(ws));
    g_signal_emit(socket, obj_signals[DISCONNECTED], 0);
}

static void octoprint_socket_on_ws_message(SoupWebsocketConnection *ws, gint type, GBytes *message, OctoPrintSocket *socket) {
    if(type!=SOUP_WEBSOCKET_DATA_TEXT) return;

    gsize sz;
    const gchar *ptr;

    ptr = g_bytes_get_data(message, &sz);

    if(sz==0) return;

    if(ptr[0]=='a') {
        JsonParser *parser = json_parser_new();
        json_parser_load_from_data(parser, ptr+1, -1, NULL);

        JsonNode *root = json_parser_get_root(parser);
        if(JSON_NODE_HOLDS_ARRAY(root)) {
            JsonArray *arr = json_node_get_array(root);
            GList *arr_list_first = json_array_get_elements(arr);
            GList *arr_list = arr_list_first;
            while(arr_list) {
                JsonObject *msg = json_node_get_object(arr_list->data);
                GList *msg_keys_first = json_object_get_members(msg);
                GList *msg_keys = msg_keys_first;
                while(msg_keys) {                    
                    JsonObject *data = json_object_get_object_member(msg, msg_keys->data);
                    g_signal_emit_by_name(socket, msg_keys->data, data);
                    msg_keys = msg_keys->next;
                }
                arr_list = arr_list->next;
                g_list_free(msg_keys_first);
            }
            g_list_free(arr_list_first);
        }

        g_object_unref(parser);
    } else if (ptr[0]=='h') { g_message("Socket Heartbeat \U0001F49A"); }
}

static void octoprint_socket_on_connect(SoupSession *session, GAsyncResult *res, OctoPrintSocket *socket) {
    GError *err = NULL;
    if (socket->websocket = soup_session_websocket_connect_finish(session, res, &err)) {

        // don't limit incoming payload size
        // 0 = unlimited
        GValue max = G_VALUE_INIT;
        g_value_init(&max, G_TYPE_UINT64);
        g_value_set_uint64(&max, 0);
        g_object_set_property(G_OBJECT(socket->websocket), "max-incoming-payload-size", &max);

        socket->connected = TRUE;
        g_signal_connect(socket->websocket, "message", G_CALLBACK(octoprint_socket_on_ws_message), socket);
        g_signal_connect(socket->websocket, "closed", G_CALLBACK(octoprint_socket_on_ws_closed), socket);
        g_debug("Socket connected to %s", socket->url);
    } else {
        g_warning("Couldn't connect socket to %s: %s", socket->url, err->message);
        g_signal_emit(socket, obj_signals[ERROR], 0, err->message);
    }
}

void octoprint_socket_connect(OctoPrintSocket *socket) {
    // create a session id, 16 random lower case letters
    char session[17];
    for(int i=0;i<16;i++) session[i] = 97 + ((char)rand() & 0x19);
    session[16] = 0;
    g_debug("New session id: %s", session);

    // server code
    GRand *r = g_rand_new();
    gint32 srv = g_rand_int_range(r, 0, 1000);
    char *server;
    server = g_strdup_printf("%03d", srv);
    g_debug("New server id: %s", server);

    // build the full url
    char *full_url;
    full_url = g_strdup_printf("%s/sockjs/%s/%s/websocket", socket->url, server, session);
    g_debug("Connecting, full URL: %s", full_url);

    SoupMessage *msg = soup_message_new("GET", full_url);
    soup_session_websocket_connect_async(socket->session, msg, NULL, NULL, NULL, (GAsyncReadyCallback)octoprint_socket_on_connect, socket);
    g_rand_free(r);
    g_free(server);
    g_free(full_url);
}

void octoprint_socket_disconnect(OctoPrintSocket *socket) {
    if(!socket->connected) return;
    socket->connected = FALSE;
    soup_websocket_connection_close(socket->websocket, SOUP_WEBSOCKET_CLOSE_NORMAL, NULL);
}

static void octoprint_socket_send_message(OctoPrintSocket *socket, const char* message) {
    // each message string goes into an array
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_array(builder);
    json_builder_add_string_value(builder, message);
    json_builder_end_array(builder);

    // convert that array to a string...
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);

    // now we get our message text
    char *msg = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(builder);
    json_node_unref(root);

    g_debug("Sending message: %s", msg);

    soup_websocket_connection_send_text(socket->websocket, msg);

    g_free(msg);
}

gboolean octoprint_socket_is_connected(OctoPrintSocket *socket) {
    return socket->connected;
}

void octoprint_socket_auth(OctoPrintSocket *socket, const char *const user, const char *const session) {
    
    // build the body : {auth: "user:session"}
    gchar *value = g_strdup_printf("%s:%s", user, session);
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "auth");
    json_builder_add_string_value(builder, value);
    json_builder_end_object(builder);

    g_free(value);

    // convert it to a string
    JsonNode *root = json_builder_get_root(builder);
    JsonGenerator *gen = json_generator_new();
    json_generator_set_root(gen, root);    
    gchar *msg = json_generator_to_data(gen, NULL);

    g_object_unref(gen);
    g_object_unref(builder);
    json_node_unref(root);

    octoprint_socket_send_message(socket, msg);

    g_free(msg);
}