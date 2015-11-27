/* xcbsource
 * Copyright (c) 2015 Charles Lehner
 *
 * Fair License (Fair)
 * Usage of the works is permitted provided that this instrument
 * is retained with the works, so that any entity that uses the
 * works is notified of this instrument.
*/

#ifndef __G_XCB_SOURCE_H__
#define __G_XCB_SOURCE_H__

#include <xcb/xcb.h>

G_BEGIN_DECLS

typedef struct _GXCBSource GXCBSource;

typedef gboolean (*GXCBSourceEventFunc)
    (GXCBSource *source, xcb_generic_event_t *event, gpointer user_data);

GXCBSource *g_xcb_source_new_for_connection(GMainContext *context, xcb_connection_t *connection);

void g_xcb_source_set_event_callback(GXCBSource *source,
        GXCBSourceEventFunc on_event, gpointer user_data);

G_END_DECLS

#endif /* __G_XCB_SOURCE_H__ */
