#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"

typedef enum _E_Dpms_Mode
{
   E_DPMS_MODE_ON      = 0,
   E_DPMS_MODE_STANDBY = 1,
   E_DPMS_MODE_SUSPEND = 2,
   E_DPMS_MODE_OFF     = 3
} E_Dpms_Mode;

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define INTERFACE "org.enlightenment.wm.dpms"

static Eldbus_Connection *conn;
static Eldbus_Service_Interface *iface;

static Eldbus_Message *
_e_dpms_set_cb(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Dpms_Mode dpms_value = E_DPMS_MODE_OFF;
   int result = -1;
   E_Zone *zone = NULL;
   Eina_List *zl = NULL;

   DBG("got DPMS request");

   if (eldbus_message_arguments_get(msg, "u", &dpms_value) && dpms_value <= E_DPMS_MODE_OFF)
     {
        E_Output *output = e_output_find_by_index(0);
        E_OUTPUT_DPMS val;

        INF("DPMS value: %d", dpms_value);

        if (dpms_value == E_DPMS_MODE_ON) val = E_OUTPUT_DPMS_ON;
        else if (dpms_value == E_DPMS_MODE_STANDBY) val = E_OUTPUT_DPMS_STANDBY;
        else if (dpms_value == E_DPMS_MODE_SUSPEND) val = E_OUTPUT_DPMS_SUSPEND;
        else val = E_OUTPUT_DPMS_OFF;

        EINA_LIST_FOREACH(e_comp->zones, zl, zone)
          {
             if (dpms_value == E_DPMS_MODE_ON)
               e_zone_display_state_set(zone, E_ZONE_DISPLAY_STATE_ON);
             else if (dpms_value == E_DPMS_MODE_OFF)
               e_zone_display_state_set(zone, E_ZONE_DISPLAY_STATE_OFF);
          }

        if (e_output_dpms_set(output, val))
          DBG("set DPMS");
     }

   eldbus_message_arguments_append(reply, "i", result);

   return reply;
}

static Eldbus_Message *
_e_dpms_get_cb(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Output *output = e_output_find_by_index(0);
   E_Dpms_Mode dpms_value = E_DPMS_MODE_OFF;

   if (output)
     {
        E_OUTPUT_DPMS val = e_output_dpms_get(output);

        if (val == E_OUTPUT_DPMS_ON) dpms_value = E_DPMS_MODE_ON;
        else if (val == E_OUTPUT_DPMS_STANDBY) dpms_value = E_DPMS_MODE_STANDBY;
        else if (val == E_OUTPUT_DPMS_SUSPEND) dpms_value = E_DPMS_MODE_SUSPEND;
        else dpms_value = E_DPMS_MODE_OFF;
     }

   DBG("got DPMS 'get' request: %d", dpms_value);

   eldbus_message_arguments_append(reply, "i", dpms_value);

   return reply;
}

static const Eldbus_Method methods[] =
{
   {"set", ELDBUS_ARGS({"u", "uint32"}), ELDBUS_ARGS({"i", "int32"}), _e_dpms_set_cb, 0},
   {"get", NULL, ELDBUS_ARGS({"i", "int32"}), _e_dpms_get_cb, 0},
   {}
};

static const Eldbus_Service_Interface_Desc iface_desc =
{
   INTERFACE, methods, NULL, NULL, NULL, NULL
};

static void
_e_dpms_name_request_cb(void *data, const Eldbus_Message *msg, Eldbus_Pending *pending EINA_UNUSED)
{
   unsigned int reply;

   if (eldbus_message_error_get(msg, NULL, NULL))
     {
        printf("error on on_name_request\n");
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &reply))
     {
        printf("error geting arguments on on_name_request\n");
        return;
     }
}

static Eina_Bool
_e_dpms_dbus_init(void *data)
{
   if (conn)
     return ECORE_CALLBACK_CANCEL;

   if (!conn)
     conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);

   if (!conn)
     {
        ERR("eldbus_connection_get fail..");
        ecore_timer_add(1.0, _e_dpms_dbus_init, NULL);
        return ECORE_CALLBACK_CANCEL;
     }

   INF("eldbus_connection_get success..");

   iface = eldbus_service_interface_register(conn, PATH, &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(iface, failed);

   eldbus_name_request(conn, BUS, ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _e_dpms_name_request_cb, NULL);

   return ECORE_CALLBACK_CANCEL;
failed:
   if (conn)
     {
        eldbus_name_release(conn, BUS, NULL, NULL);
        eldbus_connection_unref(conn);
        conn = NULL;
     }

   return ECORE_CALLBACK_CANCEL;
}

EINTERN int
e_dpms_init(void)
{
   if (eldbus_init() == 0) return 0;

   _e_dpms_dbus_init(NULL);

   return 1;
}

EINTERN int
e_dpms_shutdown(void)
{
   if (iface)
     {
        eldbus_service_interface_unregister(iface);
        iface = NULL;
     }
   if (conn)
     {
        eldbus_name_release(conn, BUS, NULL, NULL);
        eldbus_connection_unref(conn);
        conn = NULL;
     }

   eldbus_shutdown();

   return 1;
}
