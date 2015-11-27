/*
 * display-rotation-menu
 * Copyright (c) 2015 Charles Lehner
 * Fair License (Fair)
 */
#define LICENSE "\
Fair License (Fair)\
\n\n\
Usage of the works is permitted provided that this instrument \
is retained with the works, so that any entity that uses the \
works is notified of this instrument.\
\n\n\
DISCLAIMER: THE WORKS ARE WITHOUT WARRANTY."

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdlib.h>

#include <xcb/randr.h>

#include "xcbsource.h"

#define VERSION "0.0.0"
#define APPNAME "display-rotation-menu"
#define COPYRIGHT "Copyright (c) 2015 Charles Lehner"
#define COMMENTS "Display Rotation Icon"
#define WEBSITE "https://github.com/clehner/display-rotation-menu"

#define LOGO_ICON "display"

struct screen_info {
    xcb_randr_rotation_t rotation;
    xcb_timestamp_t config_timestamp;
    xcb_window_t root;
    uint16_t sizeID;
    GtkWidget *label_menu_item;
    GtkWidget *rotation_menu_items[33];
    GSList *rotation_menu_group;
};

uint8_t randr_base;

GtkStatusIcon *status_icon;
GtkWidget *app_menu;
GtkWidget *settings_menu;
GXCBSource *xcb_source;

xcb_connection_t *conn;

static GSList *screens_info = NULL;

/* static void menu_init_item(struct story *); */
static void init_xcb();
static gboolean on_xcb_event(GXCBSource *, xcb_generic_event_t *, gpointer);
static void on_screen_change(xcb_randr_screen_change_notify_event_t *);
static void on_set_screen(xcb_randr_set_screen_config_reply_t *);
static gchar *get_output_name(xcb_randr_get_output_info_reply_t *);
static void add_screen(xcb_randr_get_screen_info_reply_t *reply);
static void add_screen_rotation(struct screen_info *screen_info,
        xcb_randr_rotation_t rotation, const gchar *label,
        gboolean is_active);
static void menu_on_item(GtkMenuItem *, struct screen_info *);

static struct screen_info *get_screen_info(xcb_window_t window)
{
    struct screen_info *screen_info;
    GSList *screens;

    for (screens = screens_info; screens; screens = g_slist_next(screens)) {
        screen_info = screens->data;
        if (screen_info->root == window)
            return screen_info;
    }
    return NULL;
}

static void menu_on_about(GtkMenuItem *menuItem, gpointer userData)
{
    gtk_show_about_dialog(NULL,
            "program-name", APPNAME,
            "version", VERSION,
            "logo-icon-name", LOGO_ICON,
            "copyright", COPYRIGHT,
            "comments", COMMENTS,
            "website", WEBSITE,
            "license", LICENSE,
            "wrap-license", TRUE,
            NULL);
}

static void menu_on_quit(GtkMenuItem *item, gpointer userData)
{
    xcb_disconnect(conn);
    gtk_main_quit();
}

static gboolean status_icon_on_button_press(GtkStatusIcon *status_icon,
    GdkEventButton *event, gpointer user_data)
{
    /* Show the app menu on left click */
    GtkMenu *menu = GTK_MENU(event->button == 1 ? app_menu : settings_menu);

    gtk_menu_popup(menu, NULL, NULL, gtk_status_icon_position_menu,
            status_icon, event->button, event->time);

    return TRUE;
}

int main(int argc, char *argv[])
{
    GtkWidget *item;
    GtkMenuShell *menu;

    gtk_init(&argc, &argv);

    /* Status icon */
    status_icon = gtk_status_icon_new_from_icon_name(LOGO_ICON);
    gtk_status_icon_set_visible(status_icon, TRUE);

    g_signal_connect(G_OBJECT(status_icon), "button_press_event",
        G_CALLBACK(status_icon_on_button_press), NULL);

    /* App menu */
    app_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(app_menu);

    /* Settings menu */
    settings_menu = gtk_menu_new();
    menu = GTK_MENU_SHELL(settings_menu);

    /* About */
    item = gtk_menu_item_new_with_mnemonic(_("_About"));
    g_signal_connect(item, "activate", G_CALLBACK(menu_on_about), NULL);
    gtk_menu_shell_append(menu, item);

    /* Quit */
    item = gtk_menu_item_new_with_mnemonic(_("_Quit"));
    g_signal_connect(item, "activate", G_CALLBACK(menu_on_quit), NULL);

    gtk_menu_shell_append(menu, item);

    init_xcb();

    gtk_widget_show_all(app_menu);
    gtk_widget_show_all(settings_menu);

    gtk_main();

    return 0;
}

static void init_xcb()
{
    const xcb_query_extension_reply_t *qer;
    const xcb_setup_t *setup;
    xcb_screen_t *screen;
    xcb_screen_iterator_t screen_iter;
    xcb_drawable_t win;
    guint num_screens;
    guint i;
    xcb_generic_error_t *err = NULL;

    /* Open xcb connection */
    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        g_error("Failed to connect to display\n");
        exit(EXIT_FAILURE);
    }

    qer = xcb_get_extension_data(conn, &xcb_randr_id);
    if (!qer || !qer->present) {
        g_error("RandR extension missing\n");
        exit(EXIT_FAILURE);
    }
    randr_base = qer->first_event;

    xcb_source = g_xcb_source_new_for_connection(NULL, conn);
    g_xcb_source_set_event_callback(xcb_source, on_xcb_event, NULL);

    /* get the screens */
    setup = xcb_get_setup(conn);
    screen_iter = xcb_setup_roots_iterator(setup);

    num_screens = setup->roots_len;

    /* Set up space for cookies */
    xcb_randr_get_screen_info_cookie_t get_screen_info_cookies[num_screens];

    for (i = 0; i < num_screens; i++) {
        /* Get root window */
        screen = screen_iter.data;
        win = screen->root;

        /* Register for screen change events */
        xcb_randr_select_input(conn, win, XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE);

        /* Get screen info */
        get_screen_info_cookies[i] =
            xcb_randr_get_screen_info_unchecked(conn, win);

        xcb_screen_next(&screen_iter);
    }
    /* TODO: detect adding and removal of screens */

    xcb_flush(conn);

    /* Get screen info replies */
    for (i = 0; i < num_screens; i++) {
        xcb_randr_get_screen_info_reply_t *reply =
            xcb_randr_get_screen_info_reply(conn,
                    get_screen_info_cookies[i], &err);
        if (err) {
            g_warning("Error getting info for screen %u\n", i);
            err = NULL;
            continue;
        }

        add_screen(reply);
        free(reply);
    }
    xcb_flush(conn);
}

static void add_screen(xcb_randr_get_screen_info_reply_t *reply)
{
    const gchar *title = "Display";
    GtkWidget *item = gtk_menu_item_new_with_label(title);
    GtkMenuShell *menu = GTK_MENU_SHELL(app_menu);
    struct screen_info *info = g_malloc(sizeof *info);
    xcb_randr_rotation_t rotation = reply->rotation;
    xcb_randr_rotation_t rotations = reply->rotations;

    info->label_menu_item = item;
    info->rotation_menu_group = NULL;
    info->rotation = rotation;
    info->config_timestamp = reply->config_timestamp;
    info->root = reply->root;
    info->sizeID = reply->sizeID;
    screens_info = g_slist_append(screens_info, info);

    gtk_widget_set_sensitive(item, FALSE);
    gtk_menu_shell_append(menu, item);

#define R(rot, title) \
    if (rotations & XCB_RANDR_ROTATION_##rot) \
        add_screen_rotation(info, XCB_RANDR_ROTATION_##rot, title, \
                XCB_RANDR_ROTATION_##rot == rotation)
    R(ROTATE_0, "Landscape");
    R(ROTATE_90, "Portrait");
    R(ROTATE_180, "Landscape Flipped");
    R(ROTATE_270, "Portrait Flipped");
    R(REFLECT_X, "Reflected X");
    R(REFLECT_Y, "Reflected Y");
#undef R

    gtk_menu_shell_append(menu, gtk_separator_menu_item_new());
    gtk_widget_show_all(app_menu);

    /* Get screen resources */
    xcb_randr_get_screen_resources_current_cookie_t resources_cookie;
    xcb_randr_get_screen_resources_current_reply_t *resources_reply;
    xcb_generic_error_t *err = NULL;

    resources_cookie = xcb_randr_get_screen_resources_current(conn,
            info->root);
    resources_reply = xcb_randr_get_screen_resources_current_reply(conn,
            resources_cookie, &err);
    if (err) {
        g_warning("Get Screen Resources returned error %u\n", err->error_code);
        return;
    }

    /* Get screen outputs */
    xcb_randr_output_t *outputs;
    guint i;
    gchar *output_name;

    outputs = xcb_randr_get_screen_resources_current_outputs(resources_reply);
    for (i = 0; i < resources_reply->num_outputs; i++) {
        xcb_randr_get_output_info_reply_t *output_info_reply;
        xcb_randr_get_output_info_cookie_t output_info_cookie =
            xcb_randr_get_output_info_unchecked(conn, outputs[i],
                    resources_reply->config_timestamp);
        output_info_reply =
            xcb_randr_get_output_info_reply(conn, output_info_cookie, NULL);
        /* Show only if connected */
        switch (output_info_reply->connection) {
            case XCB_RANDR_CONNECTION_DISCONNECTED:
            case XCB_RANDR_CONNECTION_UNKNOWN:
                continue;
            case XCB_RANDR_CONNECTION_CONNECTED:
                break;
        }
        output_name = get_output_name(output_info_reply);
        /* Put output names on the menu */
        gtk_menu_item_set_label(GTK_MENU_ITEM(item), output_name);
        g_free(output_name);
        /* TODO: concatenate multiple names or pick them intelligently */
    }
}

static void add_screen_rotation(struct screen_info *screen_info,
        xcb_randr_rotation_t rotation, const gchar *label,
        gboolean is_active)
{
    GtkMenuShell *menu = GTK_MENU_SHELL(app_menu);
    GtkWidget *item = gtk_radio_menu_item_new_with_label(
            screen_info->rotation_menu_group, label);
    screen_info->rotation_menu_group =
        gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
    screen_info->rotation_menu_items[rotation] = item;

    if (is_active)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);

    g_object_set_data(G_OBJECT(item), "rotation", GUINT_TO_POINTER(rotation));
    g_signal_connect(item, "activate", G_CALLBACK(menu_on_item), screen_info);

    gtk_menu_shell_append(menu, item);
}

static void on_screen_change(xcb_randr_screen_change_notify_event_t *ev)
{
    GtkWidget *item;
    struct screen_info *screen_info = get_screen_info(ev->root);

    if (!screen_info) {
        g_warning("Unable to find screen\n");
        return;
    }

    /* Update menu item with new rotation setting */
    item = screen_info->rotation_menu_items[ev->rotation];
    screen_info->config_timestamp = ev->config_timestamp;
    printf("got rotation: %u. timestamp: %u\n", ev->rotation,
            screen_info->config_timestamp);

	g_signal_handlers_block_by_func(G_OBJECT(item),
            G_CALLBACK(menu_on_item), (gpointer)screen_info);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);
	g_signal_handlers_unblock_by_func(G_OBJECT(item),
            G_CALLBACK(menu_on_item), (gpointer)screen_info);
}

static void menu_on_item(GtkMenuItem *item, struct screen_info *screen_info)
{
    xcb_randr_rotation_t rotation;

    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
        return;

    rotation = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "rotation"));
    printf("set rotation %u. timestamp: %u. size: %u\n", rotation,
            screen_info->config_timestamp, screen_info->sizeID);

    xcb_randr_set_screen_config_unchecked(conn, screen_info->root,
            XCB_CURRENT_TIME, screen_info->config_timestamp,
            screen_info->sizeID, rotation, 0);

    xcb_flush(conn);
}

static gboolean on_xcb_event(GXCBSource *source, xcb_generic_event_t *ev,
        gpointer user_data) {

    switch (ev->response_type) {
        case 0: {
            xcb_generic_error_t *err = (xcb_generic_error_t *)ev;
            printf("Received X11 error %d\n", err->error_code);
            free(err);
            return G_SOURCE_CONTINUE;
        }
    }

    printf("event\n");
    switch (ev->response_type - randr_base) {
        case XCB_RANDR_SELECT_INPUT:
            break;
        case XCB_RANDR_SCREEN_CHANGE_NOTIFY:
            on_screen_change((xcb_randr_screen_change_notify_event_t *)ev);
            break;
        case XCB_RANDR_SET_SCREEN_CONFIG:
            on_set_screen((xcb_randr_set_screen_config_reply_t *)ev);
            break;
        default:
            printf("unknown event %u (%u)\n", ev->response_type, randr_base);
            break;
    }
    free(ev);
    return G_SOURCE_CONTINUE;
}

static void on_set_screen(xcb_randr_set_screen_config_reply_t *reply)
{
    struct screen_info *screen_info = get_screen_info(reply->root);
    if (!screen_info) {
        g_warning("Unknown screen\n");
        return;
    }

    // screen_info->timestamp = reply->new_timestamp;
    screen_info->config_timestamp = reply->config_timestamp;

    printf("set screen config. timestamp: %u\n", reply->config_timestamp);
}

static gchar *get_output_name(xcb_randr_get_output_info_reply_t *reply)
{
    gchar *name;
    uint8_t *nbuf;

    nbuf = xcb_randr_get_output_info_name(reply);
    name = reply->name_len > 0 ?
        g_strndup((gchar *)nbuf, reply->name_len) : NULL;
    return name;
}

/* vim: set expandtab ts=4 sw=4: */
