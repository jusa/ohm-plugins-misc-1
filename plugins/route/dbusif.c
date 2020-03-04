/*************************************************************************
Copyright (C) 2010 Nokia Corporation.
              2016 Jolla Ltd.

These OHM Modules are free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "plugin.h"
#include "dbusif.h"
#include "route.h"
#include "org.nemomobile.Route.Manager.xml.h"
#include "ohm-ext/route.h"

#define DBUSIF_INTERFACE_VERSION            (2)

/* D-Bus errors */
#define DBUS_NEMOMOBILE_ERROR_PREFIX        "org.nemomobile.Error"
#define DBUS_NEMOMOBILE_ERROR_FAILED        DBUS_NEMOMOBILE_ERROR_PREFIX ".Failed"
#define DBUS_NEMOMOBILE_ERROR_DENIED        DBUS_NEMOMOBILE_ERROR_PREFIX ".RequestDenied"
#define DBUS_NEMOMOBILE_ERROR_UNKNOWN       DBUS_NEMOMOBILE_ERROR_PREFIX ".Unknown"

#define DIM(a) (sizeof(a) / sizeof(a[0]))

typedef struct {
    const char    *member;
    DBusMessage *(*call)(DBusMessage *);
} method_t;

static DBusConnection  *dbus_connection;    /* connection for D-Bus system bus */

static void system_bus_init(void);
static void system_bus_cleanup(void);

static DBusHandlerResult method(DBusConnection *conn, DBusMessage *msg, void *ud);

static DBusMessage *handle_interface_version(DBusMessage *msg);
static DBusMessage *handle_introspect(DBusMessage *msg);
static DBusMessage *handle_get_all(DBusMessage *msg, int version);
static DBusMessage *handle_get_all1(DBusMessage *msg);
static DBusMessage *set_feature(DBusMessage *msg, int enable);
static DBusMessage *handle_enable(DBusMessage *msg);
static DBusMessage *handle_disable(DBusMessage *msg);
/* Since InterfaceVersion 2 */
static DBusMessage *handle_features(DBusMessage *msg);
static DBusMessage *handle_features_allowed(DBusMessage *msg);
static DBusMessage *handle_features_enabled(DBusMessage *msg);
static DBusMessage *handle_routes(DBusMessage *msg);
/* Since InterfaceVersion 3 */
static DBusMessage *handle_get_all3(DBusMessage *msg);
static DBusMessage *handle_available_routes(DBusMessage *msg);
static DBusMessage *handle_prefer(DBusMessage *msg);

static void send_signal(DBusMessage *msg);


void dbusif_init(OhmPlugin *plugin)
{
    (void)plugin;

    system_bus_init();
}

void dbusif_exit(OhmPlugin *plugin)
{
    (void)plugin;

    system_bus_cleanup();
}

void dbusif_signal_route_changed(const char *device, unsigned int device_type)
{
    DBusMessage    *msg;
    int             success;

    msg = dbus_message_new_signal(OHM_EXT_ROUTE_MANAGER_PATH,
                                  OHM_EXT_ROUTE_MANAGER_INTERFACE,
                                  OHM_EXT_ROUTE_CHANGED_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("route [%s]: failed to create message", __FUNCTION__);
    else {
        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &device,
                                           DBUS_TYPE_UINT32, &device_type,
                                           DBUS_TYPE_INVALID);

        if (success)
            send_signal(msg);
        else
            OHM_ERROR("route [%s]: failed to build message", __FUNCTION__);
    }
}

void dbusif_signal_feature_changed(const char *name,
                                   unsigned int allowed,
                                   unsigned int enabled)
{
    DBusMessage    *msg;
    int             success;

    msg = dbus_message_new_signal(OHM_EXT_ROUTE_MANAGER_PATH,
                                  OHM_EXT_ROUTE_MANAGER_INTERFACE,
                                  OHM_EXT_ROUTE_FEATURE_CHANGED_SIGNAL);

    if (msg == NULL)
        OHM_ERROR("route [%s]: failed to create message", __FUNCTION__);
    else {
        success = dbus_message_append_args(msg,
                                           DBUS_TYPE_STRING, &name,
                                           DBUS_TYPE_UINT32, &allowed,
                                           DBUS_TYPE_UINT32, &enabled,
                                           DBUS_TYPE_INVALID);

        if (success)
            send_signal(msg);
        else
            OHM_ERROR("route [%s]: failed to build message", __FUNCTION__);
    }
}

static void system_bus_init(void)
{
    static struct DBusObjectPathVTable media_method = {
        .message_function = method
    };

    DBusError   err;
    int         ret;
    int         success;

    dbus_error_init(&err);

    if ((dbus_connection = dbus_bus_get(DBUS_BUS_SYSTEM , &err)) == NULL) {
        if (dbus_error_is_set(&err))
            OHM_ERROR("route [%s]: Can't get system D-Bus connection: %s",
                      __FUNCTION__, err.message);
        else
            OHM_ERROR("route [%s]: Can't get system D-Bus connection", __FUNCTION__);
        exit(1);
    }

    dbus_connection_setup_with_g_main(dbus_connection, NULL);

    success = dbus_connection_register_object_path(dbus_connection,
                                                   OHM_EXT_ROUTE_MANAGER_PATH,
                                                   &media_method, NULL);
    if (!success) {
        OHM_ERROR("route [%s]: Can't register object path %s",
                  __FUNCTION__, OHM_EXT_ROUTE_MANAGER_PATH);
        exit(1);
    }

    ret = dbus_bus_request_name(dbus_connection, OHM_EXT_ROUTE_MANAGER_INTERFACE,
                                DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
        if (dbus_error_is_set(&err)) {
            OHM_ERROR("route [%s]: Can't be the primary owner for name %s: %s",
                      __FUNCTION__, OHM_EXT_ROUTE_MANAGER_INTERFACE, err.message);
            dbus_error_free(&err);
        }
        else {
            OHM_ERROR("route [%s]: Can't be the primary owner for name %s",
                      __FUNCTION__, OHM_EXT_ROUTE_MANAGER_INTERFACE);
        }
        exit(1);
    }

    OHM_INFO("route: successfully connected to system bus");
}


static void system_bus_cleanup(void)
{
    if (dbus_connection != NULL) {
        OHM_INFO("route: cleaning up system bus connection");

        dbus_bus_release_name(dbus_connection, OHM_EXT_ROUTE_MANAGER_INTERFACE, NULL);

        dbus_connection_unregister_object_path(dbus_connection,
                                               OHM_EXT_ROUTE_MANAGER_PATH);

        dbus_connection_unref(dbus_connection);
        dbus_connection = NULL;
    }
}

static DBusHandlerResult method(DBusConnection *conn, DBusMessage *msg, void *ud)
{
    (void)conn;
    (void)ud;

    static method_t   methods[] = {
        { OHM_EXT_ROUTE_INTERFACE_VERSION_METHOD    ,   handle_interface_version    },
        { OHM_EXT_ROUTE_GET_ALL1_METHOD             ,   handle_get_all1             },
        { OHM_EXT_ROUTE_ENABLE_METHOD               ,   handle_enable               },
        { OHM_EXT_ROUTE_DISABLE_METHOD              ,   handle_disable              },
        /* Since InterfaceVersion 2 */
        { OHM_EXT_ROUTE_FEATURES_METHOD             ,   handle_features             },
        { OHM_EXT_ROUTE_FEATURES_ALLOWED_METHOD     ,   handle_features_allowed     },
        { OHM_EXT_ROUTE_FEATURES_ENABLED_METHOD     ,   handle_features_enabled     },
        { OHM_EXT_ROUTE_ROUTES_METHOD               ,   handle_routes               },
        { OHM_EXT_ROUTE_AVAILABLE_ROUTES_METHOD     ,   handle_available_routes     },
        /* Since InterfaceVersion 3 */
        { OHM_EXT_ROUTE_GET_ALL3_METHOD             ,   handle_get_all3             },
        { OHM_EXT_ROUTE_PREFER_METHOD               ,   handle_prefer               },
    };

    int               type;
    const char       *interface;
    const char       *member;
    method_t         *method;
    dbus_uint32_t     serial;
    DBusMessage      *reply = NULL;
    unsigned int      i;

    type      = dbus_message_get_type(msg);
    interface = dbus_message_get_interface(msg);
    member    = dbus_message_get_member(msg);
    serial    = dbus_message_get_serial(msg);

    if (interface == NULL || member == NULL) {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    OHM_DEBUG(DBG_DBUS, "got D-Bus message on interface '%s'", interface);

    if (type != DBUS_MESSAGE_TYPE_METHOD_CALL)
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (!strcmp(interface, OHM_EXT_ROUTE_MANAGER_INTERFACE))
    {
        for (i = 0; i < DIM(methods); i++) {
            method = methods + i;

            if (!strcmp(member, method->member)) {
                reply  = method->call(msg);
                break;
            }
        }

        if (!reply)
            reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_METHOD, NULL);

    } else if (!strcmp(interface, "org.freedesktop.DBus.Introspectable"))
        reply = handle_introspect(msg);
    else
        reply = dbus_message_new_error(msg, DBUS_ERROR_UNKNOWN_INTERFACE, NULL);

    if (reply) {
        dbus_connection_send(conn, reply, &serial);
        dbus_message_unref(reply);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusMessage *handle_interface_version(DBusMessage *msg)
{
    DBusMessage *reply;
    dbus_uint32_t version = DBUSIF_INTERFACE_VERSION;

    reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_UINT32, &version,
                             DBUS_TYPE_INVALID);

    return reply;
}

static DBusMessage *handle_introspect(DBusMessage *msg)
{
    DBusMessage *reply = NULL;

    OHM_DEBUG(DBG_DBUS, "Introspect was called from %s", dbus_message_get_sender(msg));

    reply = dbus_message_new_method_return(msg);
    dbus_message_append_args(reply,
                             DBUS_TYPE_STRING, &route_plugin_introspect_string,
                             DBUS_TYPE_INVALID);

    return reply;
}

static DBusMessage *msg_append_active_routes(DBusMessage *msg, DBusMessageIter *append)
{
    DBusMessage *reply;
    const char *sink;
    const char *source;
    unsigned int sink_type;
    unsigned int source_type;

    if (!route_query_active(&sink, &sink_type, &source, &source_type)) {
        reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                       "Policy error");
    } else {
        reply = dbus_message_new_method_return(msg);
        dbus_message_iter_init_append(reply, append);

        dbus_message_iter_append_basic(append, DBUS_TYPE_STRING, &sink);
        dbus_message_iter_append_basic(append, DBUS_TYPE_UINT32, &sink_type);
        dbus_message_iter_append_basic(append, DBUS_TYPE_STRING, &source);
        dbus_message_iter_append_basic(append, DBUS_TYPE_UINT32, &source_type);
    }

    return reply;
}

static DBusMessage *handle_get_all1(DBusMessage *msg)
{
    return handle_get_all(msg, 1);
}

static DBusMessage *handle_get_all3(DBusMessage *msg)
{
    return handle_get_all(msg, 3);
}

static DBusMessage *handle_get_all(DBusMessage *msg, int version)
{
    DBusMessage *reply;
    DBusMessageIter append;
    DBusMessageIter entry;
    DBusMessageIter struct_entry;
    const struct audio_feature *feature;
    const GSList *i;

    reply = msg_append_active_routes(msg, &append);

    if (dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR) {
        dbus_message_iter_open_container(&append,
                                         DBUS_TYPE_ARRAY,
                                         DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                           DBUS_TYPE_STRING_AS_STRING
                                           DBUS_TYPE_UINT32_AS_STRING
                                           DBUS_TYPE_UINT32_AS_STRING
                                         DBUS_STRUCT_END_CHAR_AS_STRING,
                                         &struct_entry);

        for (i = route_get_features(); i; i = g_slist_next(i)) {
            feature = i->data;

            dbus_message_iter_open_container(&struct_entry,
                                             DBUS_TYPE_STRUCT,
                                             NULL,
                                             &entry);

            dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &feature->name);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &feature->allowed);
            dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &feature->enabled);
            dbus_message_iter_close_container(&struct_entry, &entry);
        }

        dbus_message_iter_close_container(&append, &struct_entry);

        if (version >= 3) {
            dbus_message_iter_open_container(&append,
                                             DBUS_TYPE_ARRAY,
                                             DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                               DBUS_TYPE_STRING_AS_STRING
                                               DBUS_TYPE_UINT32_AS_STRING
                                             DBUS_STRUCT_END_CHAR_AS_STRING,
                                             &struct_entry);
        }
    }

    return reply;
}

static DBusMessage *handle_prefer(DBusMessage *msg)
{
    DBusMessage *reply;
    const char  *route;
    uint32_t     type;
    uint32_t     set;
    int          ret;
    int          success = FALSE;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &route,
                                    DBUS_TYPE_UINT32, &type,
                                    DBUS_TYPE_UINT32, &set,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed prefer request");
        return dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                      "Invalid message format");
    }

    if (!(type & OHM_EXT_ROUTE_TYPE_OUTPUT)) {
        reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                       "Bad type");
        goto done;
    }

    OHM_DEBUG(DBG_DBUS, "prefer request: route=%s set=%u", route, set);

    ret = route_prefer_request(route, type, set);

    switch (ret)
    {
        case PREFER_RESULT_SUCCESS:
            reply = dbus_message_new_method_return(msg);
            break;

        case PREFER_RESULT_UNKNOWN:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_UNKNOWN,
                                           "Unknown route");
            break;

        case PREFER_RESULT_DENIED:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_DENIED,
                                           "Operation not allowed at this time");
            break;

        case PREFER_RESULT_ERROR:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                           "Policy error");
            break;

        default:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                           "Unknown error");
            break;
    }

done:
    return reply;
}

static DBusMessage *set_feature(DBusMessage *msg, int enable)
{
    DBusMessage *reply;
    const char  *feature;
    int          ret;
    int          success = FALSE;

    success = dbus_message_get_args(msg, NULL,
                                    DBUS_TYPE_STRING, &feature,
                                    DBUS_TYPE_INVALID);

    if (!success) {
        OHM_DEBUG(DBG_DBUS, "malformed feature %s request", enable ? "Enable" : "Disable" );
        return dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                      "Invalid message format");
    }

    OHM_DEBUG(DBG_DBUS, "%s feature request: feature=%s",
              enable ? "enable" : "disable", feature);

    ret = route_feature_request(feature, enable);

    switch (ret)
    {
        case FEATURE_RESULT_SUCCESS:
            reply = dbus_message_new_method_return(msg);
            break;

        case FEATURE_RESULT_DENIED:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_DENIED,
                                           "Operation not allowed at this time");
            break;

        case FEATURE_RESULT_UNKNOWN:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_UNKNOWN,
                                           "Unknown feature");
            break;

        case FEATURE_RESULT_ERROR:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                           "Policy error");
            break;

        default:
            reply = dbus_message_new_error(msg, DBUS_NEMOMOBILE_ERROR_FAILED,
                                           "Unknown error");
            break;
    }

    return reply;
}

static DBusMessage *handle_enable(DBusMessage *msg)
{
    return set_feature(msg, TRUE);
}

static DBusMessage *handle_disable(DBusMessage *msg)
{
    return set_feature(msg, FALSE);
}

static DBusMessage *feature_lists(DBusMessage *msg, int allowed, int enabled)
{
    DBusMessage *reply;
    DBusMessageIter append;
    DBusMessageIter array;
    const struct audio_feature *feature;
    const GSList *i;

    reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &append);

    dbus_message_iter_open_container(&append,
                                     DBUS_TYPE_ARRAY,
                                       DBUS_TYPE_STRING_AS_STRING,
                                     &array);

    for (i = route_get_features(); i; i = g_slist_next(i)) {
        feature = i->data;

        if ((allowed && feature->allowed) ||
            (enabled && feature->enabled) ||
            (!allowed && !enabled))
            dbus_message_iter_append_basic(&array, DBUS_TYPE_STRING, &feature->name);
    }

    dbus_message_iter_close_container(&append, &array);

    return reply;
}

static DBusMessage *handle_features(DBusMessage *msg)
{
    return feature_lists(msg, 0, 0);
}

static DBusMessage *handle_features_allowed(DBusMessage *msg)
{
    return feature_lists(msg, 1, 0);
}

static DBusMessage *handle_features_enabled(DBusMessage *msg)
{
    return feature_lists(msg, 0, 1);
}

static DBusMessage *handle_routes_filter(DBusMessage *msg, uint32_t filter)
{
    DBusMessage *reply;
    DBusMessageIter append;
    DBusMessageIter struct_entry;
    DBusMessageIter entry;
    const struct audio_device_mapping *mapping;
    const char *m_name;
    int m_type;
    const GSList *i;

    reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &append);


    dbus_message_iter_open_container(&append,
                                     DBUS_TYPE_ARRAY,
                                     DBUS_STRUCT_BEGIN_CHAR_AS_STRING
                                       DBUS_TYPE_STRING_AS_STRING
                                       DBUS_TYPE_UINT32_AS_STRING
                                     DBUS_STRUCT_END_CHAR_AS_STRING,
                                     &struct_entry);

    for (i = route_get_mappings(); i; i = g_slist_next(i)) {
        mapping = i->data;
        m_name = route_mapping_name(mapping);
        m_type = route_mapping_type(mapping);

        if (filter && !(m_type & filter))
            continue;

        dbus_message_iter_open_container(&struct_entry,
                                         DBUS_TYPE_STRUCT,
                                         NULL,
                                         &entry);

        dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &m_name);
        dbus_message_iter_append_basic(&entry, DBUS_TYPE_UINT32, &m_type);

        dbus_message_iter_close_container(&struct_entry, &entry);
    }

    dbus_message_iter_close_container(&append, &struct_entry);

    return reply;
}

static DBusMessage *handle_routes(DBusMessage *msg)
{
    return handle_routes_filter(msg, 0);
}

static DBusMessage *handle_available_routes(DBusMessage *msg)
{
    return handle_routes_filter(msg, OHM_EXT_ROUTE_TYPE_AVAILABLE);
}

static void send_signal(DBusMessage *msg)
{
    if (!dbus_connection_send(dbus_connection, msg, NULL))
        OHM_ERROR("route [%s]: failed to send D-Bus signal", __FUNCTION__);

    dbus_message_unref(msg);
}
