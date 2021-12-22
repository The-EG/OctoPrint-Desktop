#include "app.h"

int main(int argc, char *argv[]) {

    OPDeskApp *app = opdesk_app_new();
    int res = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);

    return res;
}