#include <stdio.h>
#include <string.h>

#include <gmodule.h>
#include <glib.h>
#include <dbus/dbus.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>
#include <ohm/ohm-plugin-debug.h>

typedef gboolean (*hal_cb) (OhmFact *hal_fact, gchar *capability, gboolean added, gboolean removed, void *user_data);

typedef struct dres_arg {
    char sig;
    char *key;
    union {
        int i_value;
        double *f_value;
        char *s_value;
    } value;
} dres_arg_t;

/* common services */
int dres_accessory_request(const char *, int, int);
int dres_all(void);
gboolean run_policy_hook(const char *hook, unsigned int nargs, dres_arg_t args[]);
gboolean local_set_observer(gchar *capability, hal_cb cb, void *userdata);
gboolean local_unset_observer(void *userdata);

/* headset */
gboolean headset_init(OhmPlugin *, int);
gboolean headset_deinit(OhmPlugin *);

/* bluetooth */
gboolean bluetooth_init(OhmPlugin *, int);
gboolean bluetooth_deinit(OhmPlugin *);
/* callbacks */
DBusHandlerResult check_bluez(DBusConnection * c, DBusMessage * msg, void *user_data);
DBusHandlerResult bt_device_removed(DBusConnection *c, DBusMessage * msg, void *data);
DBusHandlerResult hsp_property_changed(DBusConnection *c, DBusMessage * msg, void *data);
DBusHandlerResult a2dp_property_changed(DBusConnection *c, DBusMessage * msg, void *data);

/*
gboolean set_observer(gchar *capability, hal_cb cb, void *userdata);
gboolean unset_observer(void *userdata);
*/
