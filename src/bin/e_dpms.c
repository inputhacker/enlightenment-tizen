#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <tizen-dpms-server-protocol.h>

typedef enum _E_Dpms_Mode
{
   E_DPMS_MODE_ON        = 0,
   E_DPMS_MODE_STANDBY   = 1,
   E_DPMS_MODE_SUSPEND   = 2,
   E_DPMS_MODE_OFF       = 3
} E_Dpms_Mode;

typedef enum _E_Dpms_Manager_Error {
   E_DPMS_MANAGER_ERROR_NONE = 0,
   E_DPMS_MANAGER_ERROR_INVALID_PERMISSION = 1,
   E_DPMS_MANAGER_ERROR_INVALID_PARAMETER = 2,
   E_DPMS_MANAGER_ERROR_NOT_SUPPORTED = 3,
   E_DPMS_MANAGER_ERROR_ALREADY_DONE = 4,
} E_Dpms_Manager_Error;

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define INTERFACE "org.enlightenment.wm.dpms"

static Eldbus_Connection *edbus_conn = NULL;
static Eldbus_Connection_Type edbus_conn_type = ELDBUS_CONNECTION_TYPE_SYSTEM;
Ecore_Event_Handler *dbus_init_done_handler = NULL;
static Eldbus_Service_Interface *iface;

static Eina_List *dpms_list;
static struct wl_global *dpms_global;
Ecore_Timer *delay_timer;
static struct wl_resource *gresource;

typedef struct _E_Dpms
{
   struct wl_resource *output;

   E_Comp_Wl_Output *wl_output;
   E_Output *e_output;
   E_Output_Hook *dpms_change_hook;
   E_Dpms_Mode mode;
} E_Dpms;

static Eldbus_Message *
_e_dpms_set_cb(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Dpms_Mode dpms_value = E_DPMS_MODE_OFF;
   int result = -1;

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

        if (e_output_dpms_set(output, val))
          {
             DBG("set DPMS");
             result = 0;
          }
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
        ERR("error on on_name_request\n");
        return;
     }

   if (!eldbus_message_arguments_get(msg, "u", &reply))
     {
        ERR("error geting arguments on on_name_request\n");
        return;
     }
}

static Eina_Bool
_e_dpms_dbus_init(void *data)
{
   iface = eldbus_service_interface_register(edbus_conn, PATH, &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(iface, failed);

   eldbus_name_request(edbus_conn, BUS, ELDBUS_NAME_REQUEST_FLAG_DO_NOT_QUEUE,
                       _e_dpms_name_request_cb, NULL);

   return ECORE_CALLBACK_CANCEL;

failed:
   if (edbus_conn)
     {
        eldbus_name_release(edbus_conn, BUS, NULL, NULL);
        e_dbus_conn_connection_unref(edbus_conn);
        edbus_conn = NULL;
     }

   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_dpms_cb_dbus_init_done(void *data, int type, void *event)
{
   E_DBus_Conn_Init_Done_Event *e = event;

   if (e->status == E_DBUS_CONN_INIT_SUCCESS && e->conn_type == edbus_conn_type)
     {
        edbus_conn = e_dbus_conn_connection_ref(edbus_conn_type);

        if (edbus_conn)
          _e_dpms_dbus_init(NULL);
     }

   ecore_event_handler_del(dbus_init_done_handler);
   dbus_init_done_handler = NULL;

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_dpms_delay(void *data)
{
   E_Dpms *dpms = NULL;
   E_Dpms_Mode e_output_dpms;

   if (!data) return ECORE_CALLBACK_CANCEL;
   dpms = (E_Dpms *)data;

   delay_timer = NULL;

   if (!gresource) return ECORE_CALLBACK_CANCEL;

   e_output_dpms = e_output_dpms_get(dpms->e_output);
   if (e_output_dpms != dpms->mode)
     {
        WRN("tizen_dpms_manager dpms change timer cb. different dpms!(output:%d, dpms:%d)",
            e_output_dpms, dpms->mode);
        dpms->mode = e_output_dpms;
     }

   tizen_dpms_manager_send_state(gresource, dpms->mode, E_DPMS_MANAGER_ERROR_NONE);
   INF("tizen_dpms_manager send set event by timer(res:%p, output:%p, dpms:%d)", gresource, dpms->e_output, dpms->mode);
   gresource = NULL;

   return ECORE_CALLBACK_CANCEL;
}

static void
_e_tizen_dpms_cb_output_dpms_change(void *data, E_Output *output)
{
   E_Dpms *dpms = NULL;
   E_Dpms_Mode e_output_dpms;

   dpms = (E_Dpms *) data;
   EINA_SAFETY_ON_NULL_RETURN(dpms);
   EINA_SAFETY_ON_FALSE_RETURN(dpms->e_output == output);

   e_output_dpms = e_output_dpms_get(output);

   if (gresource)
     {
        if (delay_timer)
          ecore_timer_del(delay_timer);
        delay_timer = NULL;
        INF("tizen_dpms_manager set dpms async cb(res:%p, output:%p, dpms:%d)",
            gresource, dpms->e_output, e_output_dpms);
        tizen_dpms_manager_send_state(gresource, e_output_dpms, E_DPMS_MANAGER_ERROR_NONE);
        gresource = NULL;
     }
}

static E_Dpms *
_e_tizen_dpms_manager_create(struct wl_resource *output)
{
   E_Dpms *dpms = NULL;
   E_Output_Hook *dpms_hook = NULL;

   dpms = E_NEW(E_Dpms, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dpms, NULL);

   dpms->output = output;
   dpms->wl_output = wl_resource_get_user_data(dpms->output);
   EINA_SAFETY_ON_NULL_GOTO(dpms->wl_output, fail_create);

   dpms->e_output = e_output_find(dpms->wl_output->id);
   EINA_SAFETY_ON_NULL_GOTO(dpms->e_output, fail_create);

   dpms_hook = e_output_hook_add(E_OUTPUT_HOOK_DPMS_CHANGE,
                                 _e_tizen_dpms_cb_output_dpms_change, dpms);
   EINA_SAFETY_ON_NULL_GOTO(dpms_hook, fail_create);

   dpms->dpms_change_hook = dpms_hook;

   dpms_list = eina_list_append(dpms_list, dpms);

   return dpms;

fail_create:
   E_FREE(dpms);

   return NULL;
}

static E_Dpms *
_e_tizen_dpms_manager_find(struct wl_resource *output)
{
   E_Dpms *dpms = NULL;
   Eina_List *l;

   if (eina_list_count(dpms_list) == 0)
     return _e_tizen_dpms_manager_create(output);

   EINA_LIST_FOREACH(dpms_list, l, dpms)
     {
        if (!dpms) continue;

        if (dpms->output == output)
          return dpms;
     }

     return _e_tizen_dpms_manager_create(output);
}

static void
_e_tizen_dpms_manager_cb_set_dpms(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output, uint32_t mode)
{
   E_Dpms *dpms = NULL;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_FALSE_RETURN(mode <= E_DPMS_MODE_OFF);

   dpms = _e_tizen_dpms_manager_find(output);
   if (!dpms)
     {
        ERR("cannot find dpms %p", output);
        tizen_dpms_manager_send_state(resource, E_DPMS_MODE_OFF, E_DPMS_MANAGER_ERROR_INVALID_PARAMETER);
        return;
     }

   if (!e_output_connected(dpms->e_output))
     {
        dpms->mode = mode;
        INF("tizen_dpms_manager set dpms fake(res:%p, output:%p, dpms:%d)", resource, dpms->e_output, mode);

        if (dpms->dpms_change_hook)
          tizen_dpms_manager_send_state(resource, dpms->mode, E_DPMS_MANAGER_ERROR_NONE);

        return;
     }

   gresource = resource;
   dpms->mode = mode;

   if (delay_timer)
     ecore_timer_del(delay_timer);
   delay_timer = ecore_timer_add(1.0, _e_dpms_delay, dpms);

   ret = e_output_dpms_set(dpms->e_output, mode);
   if (ret)
     INF("tizen_dpms_manager set dpms(res:%p, output:%p, dpms:%d, %d)", resource, dpms->e_output, mode, dpms->mode);
   else
     {
        ERR("tizen_dpms_manager set dpms fail(res:%p, output:%p, dpms=%d)", resource, dpms->e_output, mode);
        ecore_timer_del(delay_timer);
        delay_timer = NULL;
        gresource = NULL;
        tizen_dpms_manager_send_state(resource, dpms->mode, E_DPMS_MANAGER_ERROR_INVALID_PERMISSION);
     }
}

static void
_e_tizen_dpms_manager_cb_get_dpms(struct wl_client *client, struct wl_resource *resource, struct wl_resource *output)
{
   E_Dpms *dpms = NULL;

   dpms = _e_tizen_dpms_manager_find(output);
   if (!dpms)
     {
        ERR("cannot find dpms %p", output);
        tizen_dpms_manager_send_state(resource, E_DPMS_MODE_OFF, E_DPMS_MANAGER_ERROR_INVALID_PARAMETER);
        return;
     }

   if (e_output_connected(dpms->e_output))
     dpms->mode = e_output_dpms_get(dpms->e_output);

   INF("tizen_dpms_manager get dpms(res:%p, output:%p, dpms:%d)", resource, dpms->e_output, dpms->mode);
   tizen_dpms_manager_send_state(resource, dpms->mode, E_DPMS_MANAGER_ERROR_NONE);
}

static void
_e_tizen_dpms_manager_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   INF("tizen_dpms_manager cb_destroy (res:%p)", resource);

   if (delay_timer)
     {
        ecore_timer_del(delay_timer);
        delay_timer = NULL;
     }

   if (gresource == resource)
     gresource = NULL;

   wl_resource_destroy(resource);
}

static const struct tizen_dpms_manager_interface _e_tizen_dpms_interface =
{
   _e_tizen_dpms_manager_cb_destroy,
   _e_tizen_dpms_manager_cb_set_dpms,
   _e_tizen_dpms_manager_cb_get_dpms,
};

static void
_e_dpms_wl_cb_destroy(struct wl_resource *resource)
{
   INF("e_dpms_wl cb_destroy (res:%p)", resource);

   if (delay_timer)
     {
        ecore_timer_del(delay_timer);
        delay_timer = NULL;
     }

   if (gresource == resource)
     gresource = NULL;
}

static void
_e_dpms_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &tizen_dpms_manager_interface, MIN(version, 1), id)))
     {
        ERR("Could not create tizen_dpms_manager resource");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_tizen_dpms_interface, NULL, _e_dpms_wl_cb_destroy);
}

EINTERN int
e_dpms_init(void)
{
   dbus_init_done_handler = NULL;

   if (e_dbus_conn_init() > 0)
     {
        dbus_init_done_handler = ecore_event_handler_add(E_EVENT_DBUS_CONN_INIT_DONE, _e_dpms_cb_dbus_init_done, NULL);
        e_dbus_conn_dbus_init(edbus_conn_type);
     }

   if (!e_comp_wl || !e_comp_wl->wl.disp)
     {
        ERR("no e_comp_wl resource. cannot create dpms interface");
        return 1;
     }

   /* try to add dpms_manager to wayland globals */
   dpms_global = wl_global_create(e_comp_wl->wl.disp,
                                  &tizen_dpms_manager_interface, 1, NULL,
                                  _e_dpms_cb_bind);
   if (!dpms_global)
     ERR("Could not add tizen_dpms_manager to wayland globals");

   return 1;
}

EINTERN int
e_dpms_shutdown(void)
{
   E_Dpms *dpms = NULL;
   Eina_List *l;

   if (dpms_global)
     {
        wl_global_destroy(dpms_global);
        dpms_global = NULL;
     }

   if (dbus_init_done_handler)
     {
        ecore_event_handler_del(dbus_init_done_handler);
        dbus_init_done_handler = NULL;
     }

   if (iface)
     {
        eldbus_service_interface_unregister(iface);
        iface = NULL;
     }

   if (edbus_conn)
     {
        eldbus_name_release(edbus_conn, BUS, NULL, NULL);
        e_dbus_conn_connection_unref(edbus_conn);
        edbus_conn = NULL;
     }

   e_dbus_conn_shutdown();

   if (delay_timer)
     {
        ecore_timer_del(delay_timer);
        delay_timer = NULL;
     }
   gresource = NULL;

   EINA_LIST_FOREACH(dpms_list, l, dpms)
     {
        if (dpms->dpms_change_hook)
          e_output_hook_del(dpms->dpms_change_hook);
        free(dpms);
     }

   eina_list_free(dpms_list);
   dpms_list = NULL;

   return 1;
}
