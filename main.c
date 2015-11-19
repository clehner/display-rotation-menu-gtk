/*
 * display-rotation-icon
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

#define VERSION "0.0.0"
#define APPNAME "display-rotation-icon"
#define COPYRIGHT "Copyright (c) 2015 Charles Lehner"
#define COMMENTS "Display Rotation Icon"
#define WEBSITE "https://github.com/clehner/display-rotation-icon"

#define LOGO_ICON "display"

struct screen_info {
    xcb_randr_rotation_t rotation;
    xcb_timestamp_t config_timestamp;
    xcb_window_t root;
    uint16_t sizeID;
    GtkWidget *label_menu_item;
    GtkWidget *rotation_menu_items[32];
    GSList *rotation_menu_group;
};

struct screen_change_event {
    struct screen_info *screen;
    xcb_randr_rotation_t rotation;
};

GtkStatusIcon *status_icon;
GtkWidget *app_menu;
GtkWidget *settings_menu;
GThread *xcb_thread;

xcb_connection_t *conn;

static GSList *screens_info = NULL;

/* static void menu_init_item(struct story *); */
static gpointer xcb_thread_main(gpointer data);
static void handle_screen_change(xcb_randr_screen_change_notify_event_t *);
/*
static void handle_set_screen_config_reply(
        xcb_randr_get_screen_info_reply_t *);
        */
static gboolean add_screen(gpointer data);
static void add_screen_rotation(struct screen_info *screen_info,
        xcb_randr_rotation_t rotation, const gchar *label,
        gboolean is_active);
static void menu_on_item(GtkMenuItem *, struct screen_info *);

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
    g_signal_connect(item, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_menu_shell_append(menu, item);

    xcb_thread = g_thread_new("xcb-randr-menu", xcb_thread_main, NULL);

    gtk_widget_show_all(app_menu);
    gtk_widget_show_all(settings_menu);

    gtk_main();

    return 0;
}

static gboolean update_screen(gpointer data)
{
    struct screen_change_event *change_event = data;
    GtkWidget *item;

    /* Update menu item with new rotation setting */
    printf("rotation: %u\n", change_event->rotation);
    item = change_event->screen->rotation_menu_items[change_event->rotation];
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);

    g_free(change_event);
    return G_SOURCE_REMOVE;
}

static gpointer xcb_thread_main(gpointer data)
{
    (void)data;
    const xcb_query_extension_reply_t *qer;
    uint8_t randr_base;
    const xcb_setup_t *setup;
    xcb_screen_t *screen;
    xcb_screen_iterator_t screen_iter;
    xcb_drawable_t win;
    xcb_generic_event_t *ev;
    guint num_screens;
    // struct screen *screens;
    guint i;
    xcb_generic_error_t *err = NULL;
    // xcb_randr_get_screen_info_cookie_t get_info_cookie;

    /* Open xcb connection */
    conn = xcb_connect (NULL, NULL);
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

        /* Handle the reply in the GTK thread */
        gdk_threads_add_idle(add_screen, reply);
    }

    /* Handle events */
    printf("waiting\n");
    while ((ev = xcb_wait_for_event(conn))) {
        printf("event\n");
        switch (ev->response_type - randr_base) {
            case XCB_RANDR_SELECT_INPUT:
                break;
            case XCB_RANDR_SCREEN_CHANGE_NOTIFY:
                handle_screen_change(
                        (xcb_randr_screen_change_notify_event_t *)ev);
                break;
            case XCB_RANDR_SET_SCREEN_CONFIG: {
                printf("set screen config");
                xcb_randr_get_screen_info_reply_t *reply;
                struct screen_info *screen_info;
                GSList *screens;

                for (screens = screens_info; screens;
                        screens = g_slist_next(screens)) {
                    screen_info = screens->data;
                    // screen_info->timestamp = reply->new_timestamp;
                    screen_info->config_timestamp = reply->config_timestamp;
                }
                /*
                // handle_set_screen_config_reply(
                        // (xcb_randr_get_screen_info_reply_t *)ev);
                xcb_randr_set_screen_config_cookie_t cookie = {ev->sequence};

                cur_info = xcb_randr_set_screen_config_reply(conn,
                        cookie, &err);
                if (err) {
                    g_warning("Error setting screen config\n");
                    err = NULL;
                    continue;
                }
                        */
                reply = (xcb_randr_get_screen_info_reply_t *)ev;
                printf("rotations: %u, rotation: %u\n", reply->rotations,
                        reply->rotation);
                break;
            }
            default:
                printf("unknown event\n");
                break;
        }
        free(ev);
    }

    if (xcb_connection_has_error(conn)) {
        g_error("Display connection closed by server\n");
    }

    xcb_disconnect(conn);
    return NULL;
}

static gboolean add_screen(gpointer data)
{
    xcb_randr_get_screen_info_reply_t *reply = data;
    xcb_randr_rotation_t rotations = reply->rotations;
    xcb_randr_rotation_t rotation = reply->rotation;

    const gchar *title = "Display";
    GtkWidget *item = gtk_menu_item_new_with_label(title);
    GtkMenuShell *menu = GTK_MENU_SHELL(app_menu);
    struct screen_info *info = g_malloc(sizeof *info);

    info->rotation_menu_group = NULL;
    info->rotation = reply->rotation;
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

    free(reply);
    return G_SOURCE_REMOVE;
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

    g_object_set_data(G_OBJECT(item), "rotation", GUINT_TO_POINTER(rotation));
    g_signal_connect(item, "activate", G_CALLBACK(menu_on_item), screen_info);

    if (is_active)
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), TRUE);

    gtk_menu_shell_append(menu, item);
}

static void handle_screen_change(xcb_randr_screen_change_notify_event_t *ev)
{
    struct screen_change_event *change_event;
    GSList *screens;
    struct screen_info *screen_info;

    change_event = g_malloc(sizeof *change_event);
    change_event->rotation = ev->rotation;

    for (screens = screens_info; screens; screens = g_slist_next(screens)) {
        screen_info = screens->data;
        change_event->screen = screen_info;
    }

    gdk_threads_add_idle(update_screen, change_event);
}

static void menu_on_item(GtkMenuItem *item, struct screen_info *screen_info)
{
    xcb_randr_set_screen_config_cookie_t cookie;
    xcb_randr_rotation_t rotation;

    if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
        return;

    rotation = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item), "rotation"));
    printf("set rotation %u\n", rotation);

    cookie = xcb_randr_set_screen_config(conn, screen_info->root,
            XCB_CURRENT_TIME,
            screen_info->config_timestamp,
            screen_info->sizeID, rotation, 0);
    xcb_flush(conn);

    (void)cookie;
}

/* vim: set expandtab ts=4 sw=4: */
