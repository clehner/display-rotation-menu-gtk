/* xcbsource
 * Copyright (c) 2015 Charles Lehner
 *
 * Fair License (Fair)
 * Usage of the works is permitted provided that this instrument
 * is retained with the works, so that any entity that uses the
 * works is notified of this instrument.
*/

#include <glib.h>

#include "xcbsource.h"

struct _GXCBSource {
    GSource source;
    gboolean owned;
    xcb_connection_t *conn;
    gpointer fd;

    GXCBSourceEventFunc on_event_cb;
    gpointer on_event_user_data;
};

static gboolean
_g_xcb_source_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    return FALSE;
}

static gboolean
_g_xcb_source_check(GSource *source)
{
    GXCBSource *self = (GXCBSource *)source;

    return (g_source_query_unix_fd(source, self->fd) > 0);
}

static gboolean
_g_xcb_source_dispatch(GSource *source, GSourceFunc callback_, gpointer user_data)
{
    GXCBSource *self = (GXCBSource *)source;
    xcb_generic_event_t *event;
    gboolean status = G_SOURCE_CONTINUE;

    if (self->conn == NULL) {
        g_assert_not_reached();
        return G_SOURCE_REMOVE;
    }

    event = xcb_poll_for_event(self->conn);
    if (event && self->on_event_cb)
        status = self->on_event_cb(self, event, self->on_event_user_data);

    if (xcb_connection_has_error(self->conn))
        status = G_SOURCE_REMOVE;

    return status;
}

static void
_g_xcb_source_finalize(GSource *source)
{
    GXCBSource *self = (GXCBSource *)source;

    if (self->owned) {
        xcb_disconnect(self->conn);
    }
}

static GSourceFuncs _g_xcb_source_funcs = {
    _g_xcb_source_prepare,
    _g_xcb_source_check,
    _g_xcb_source_dispatch,
    _g_xcb_source_finalize,
    NULL, NULL
};
    
GXCBSource *
g_xcb_source_new_for_connection(GMainContext *context,
        xcb_connection_t *connection)
{
    g_return_val_if_fail(connection != NULL, NULL);

    GSource *source;
    GXCBSource *self;

    source = g_source_new(&_g_xcb_source_funcs, sizeof(GXCBSource));
    self = (GXCBSource *)source;
    self->conn = connection;

    self->fd = g_source_add_unix_fd(source,
            xcb_get_file_descriptor(connection),
            G_IO_IN | G_IO_ERR | G_IO_HUP);

    g_source_attach(source, context);

    return self;
}

void g_xcb_source_set_event_callback(GXCBSource *self,
        GXCBSourceEventFunc cb, gpointer user_data)
{
    self->on_event_user_data = user_data;
    self->on_event_cb = cb;
}

/* vim: set expandtab ts=4 sw=4: */
