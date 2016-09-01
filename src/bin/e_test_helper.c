#include "e.h"

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.Test"

typedef struct _Test_Helper_Data
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;

   Eina_List *hdlrs;

   struct
     {
        Ecore_Window win;
        E_Client *ec;
        int vis;
        Eina_Bool disuse;
     } registrant;

} Test_Helper_Data;

static Test_Helper_Data *th_data = NULL;

static Eina_Bool _e_test_helper_cb_property_get(const Eldbus_Service_Interface *iface, const char *name, Eldbus_Message_Iter *iter, const Eldbus_Message *msg, Eldbus_Message **err);

static Eldbus_Message* _e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_change_stack(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_dpms(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_ev_freeze(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_ev_mouse(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message* _e_test_helper_cb_ev_key(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);

enum
{
   E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY = 0,
   E_TEST_HELPER_SIGNAL_RESTACK,
};

static const Eldbus_Signal signals[] = {
     [E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY] =
       {
          "VisibilityChanged",
          ELDBUS_ARGS({"ub", "window id, visibility"}),
          0
       },
     [E_TEST_HELPER_SIGNAL_RESTACK] =
       {
          "StackChanged",
          ELDBUS_ARGS({"u", "window id was restacked"}),
          0
       },
       { }
};

static const Eldbus_Method methods[] ={
       {
          "RegisterWindow",
          ELDBUS_ARGS({"u", "window id to be registered"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_register_window, 0
       },
       {
          "DeregisterWindow",
          ELDBUS_ARGS({"u", "window id to be deregistered"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_deregister_window, 0
       },
       {
          "SetWindowStack",
          ELDBUS_ARGS({"uui", "window id to restack, above or below, stacking type"}),
          NULL,
          _e_test_helper_cb_change_stack, 0
       },
       {
          "GetWindowInfo",
          NULL,
          ELDBUS_ARGS({"ua(usiiiiibbbiibbbbb)", "array of ec"}),
          _e_test_helper_cb_get_clients, 0
       },
       {
          "DPMS",
          ELDBUS_ARGS({"u", "DPMS 0=off or 1=on"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_dpms, 0
       },
       {
          "EventFreeze",
          ELDBUS_ARGS({"u", "0=events will start to be processed or 1=freeze input events processing"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_ev_freeze, 0
       },
       {
          "EventMouse",
          ELDBUS_ARGS({"uii", "type 0=down 1=move 2=up, x position, y position"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_ev_mouse, 0
       },
       {
          "EventKey",
          ELDBUS_ARGS({"us", "type 0=down 1=up, key name"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_ev_key, 0
       },
       { }
};

static const Eldbus_Property properties[] = {
       { "Registrant", "u", NULL, NULL, 0 },
       { }
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, signals, properties, _e_test_helper_cb_property_get, NULL
};

static void
_e_test_helper_registrant_clear(void)
{
   EINA_SAFETY_ON_NULL_RETURN(th_data);

   th_data->registrant.win = 0;
   th_data->registrant.vis = -1;
   th_data->registrant.ec = NULL;
   th_data->registrant.disuse = EINA_FALSE;
}

static void
_e_test_helper_message_append_clients(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *array_of_ec;
   E_Client *ec;
   Evas_Object *o;
   E_Comp *comp;

   EINA_SAFETY_ON_NULL_RETURN(th_data);

   if (!(comp = e_comp)) return;

   eldbus_message_iter_arguments_append(iter, "ua(usiiiiibbbiibbbbb)", th_data->registrant.win, &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;
        Ecore_Window win;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        win = e_pixmap_res_id_get(ec->pixmap);

        eldbus_message_iter_arguments_append(array_of_ec, "(usiiiiibbbiibbbbb)", &struct_of_ec);
        eldbus_message_iter_arguments_append
           (struct_of_ec, "usiiiiibbbiibbbbb",
            win,
            e_client_util_name_get(ec) ?: "NO NAME",

            /* geometry */
            ec->x, ec->y, ec->w, ec->h,

            /* layer */
            evas_object_layer_get(o),

            /* effect */
            evas_object_data_get(o, "effect_running"),

            /* visibility & iconify */
            ec->visible,
            evas_object_visible_get(o),
            ec->visibility.opaque,
            ec->visibility.obscured,
            ec->visibility.skip,
            ec->iconic,

            /* color depth */
            ec->argb,

            /* focus */
            ec->focused,
            evas_object_focus_get(o));
        eldbus_message_iter_container_close(array_of_ec, struct_of_ec);
     }

   eldbus_message_iter_container_close(iter, array_of_ec);
}

static void
_e_test_helper_restack(Ecore_Window win, Ecore_Window target, int above)
{
   E_Client *ec = NULL, *tec = NULL;

   ec = e_pixmap_find_client_by_res_id(win);
   tec = e_pixmap_find_client_by_res_id(target);

   if (!ec) return;

   if(!tec)
     {
        if (above)
          evas_object_raise(ec->frame);
        else
          evas_object_lower(ec->frame);
     }
   else
     {

        if (above)
          evas_object_stack_above(ec->frame, tec->frame);
        else
          evas_object_stack_below(ec->frame, tec->frame);
     }
}

/* Signal senders */
static void
_e_test_helper_send_change_visibility(Ecore_Window win, Eina_Bool vis)
{
   Eldbus_Message *signal;

   EINA_SAFETY_ON_NULL_RETURN(th_data);

   signal = eldbus_service_signal_new(th_data->iface,
                                      E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY);
   eldbus_message_arguments_append(signal, "ub", win, vis);
   eldbus_service_signal_send(th_data->iface, signal);
}

/* Method Handlers */
static Eldbus_Message *
_e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                  const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   eldbus_message_arguments_append(reply, "b", !th_data->registrant.win);
   if (!th_data->registrant.win) th_data->registrant.win = id;

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_dpms(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool accept = EINA_FALSE;
   unsigned int dpms;

   if (!eldbus_message_arguments_get(msg, "u", &dpms))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   /* TODO */
   switch (dpms)
     {
      case 0:
         /* dpms off */
         accept = EINA_TRUE;
         break;
      case 1:
         /* dpms on */
         accept = EINA_TRUE;
         break;
      default:
         break;
     }

   eldbus_message_arguments_append(reply, "b", accept);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_ev_freeze(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool accept = EINA_TRUE;
   unsigned int freeze;

   if (!eldbus_message_arguments_get(msg, "u", &freeze))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   switch (freeze)
     {
      case 0: e_comp_all_thaw(); break;
      case 1: e_comp_all_freeze(); break;
      default: accept = EINA_FALSE; break;
     }

   eldbus_message_arguments_append(reply, "b", accept);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_ev_mouse(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool accept = EINA_TRUE;
   Ecore_Event_Mouse_Button *ev = NULL;
   unsigned int type;
   int x, y;

   if (!eldbus_message_arguments_get(msg, "uii", &type, &x, &y))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   ev = E_NEW(Ecore_Event_Mouse_Button, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, reply);

   ev->timestamp = (unsigned int)(ecore_time_unix_get() * (double)1000);
   ev->same_screen = 1;
   ev->x = ev->root.x = x;
   ev->y = ev->root.y = y;
   ev->buttons = 1;

   switch (type)
     {
      case 0: ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, ev, NULL, NULL); break;
      case 1: ecore_event_add(ECORE_EVENT_MOUSE_MOVE, ev, NULL, NULL); break;
      case 2: ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_UP, ev, NULL, NULL); break;
      default: accept = EINA_FALSE; break;
     }

   eldbus_message_arguments_append(reply, "b", accept);

   if (!accept)
     E_FREE(ev);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_ev_key(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool accept = EINA_FALSE;
   unsigned int type;
   char *key;

   if (!eldbus_message_arguments_get(msg, "us", &type, &key))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }

   /* TODO */
   switch (type)
     {
      case 0:
         /* key down */
         accept = EINA_TRUE;
         break;
      case 1:
         /* key up */
         accept = EINA_TRUE;
         break;
      default:
         break;
     }

   eldbus_message_arguments_append(reply, "b", accept);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                    const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window id;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &id))
     {
        ERR("Error on eldbus_message_arguments_get()\n");
        return reply;
     }
   eldbus_message_arguments_append(reply, "b", ((!th_data->registrant.win) ||
                                                (th_data->registrant.win == id)));

   if (th_data->registrant.win == id)
     {
        _e_test_helper_registrant_clear();
     }

   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_change_stack(const Eldbus_Service_Interface *iface EINA_UNUSED,
                               const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win, target;
   int above = -1;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "uui", &win, &target, &above))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        return reply;
     }

   if ((win) && (above != -1))
     _e_test_helper_restack(win, target, above);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface EINA_UNUSED,
                              const Eldbus_Message *msg)
{
   Eldbus_Message *reply;

   reply = eldbus_message_method_return_new(msg);
   _e_test_helper_message_append_clients(eldbus_message_iter_get(reply));

   return reply;
}

static Eina_Bool
_e_test_helper_cb_visibility_change(void *data EINA_UNUSED,
                                    int type EINA_UNUSED,
                                    void *event)
{
   E_Client *ec;
   Ecore_Window win = 0;
   E_Event_Client *ev = event;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);
   if (!th_data->registrant.win) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   win = e_pixmap_res_id_get(ec->pixmap);

   if (win != th_data->registrant.win) return ECORE_CALLBACK_PASS_ON;

   if (!th_data->registrant.ec)
     th_data->registrant.ec = ec;

   if (th_data->registrant.vis != !ec->visibility.obscured)
     _e_test_helper_send_change_visibility(th_data->registrant.win, !ec->visibility.obscured);

   th_data->registrant.vis = !ec->visibility.obscured;

   if ((th_data->registrant.disuse) && (!th_data->registrant.vis))
     _e_test_helper_registrant_clear();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_helper_cb_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   if (!th_data->registrant.ec) return ECORE_CALLBACK_PASS_ON;
   if (ec != th_data->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   _e_test_helper_registrant_clear();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_helper_cb_client_restack(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   Eldbus_Message *sig;
   Ecore_Window win;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   if(!th_data->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;

   win = e_pixmap_res_id_get(ec->pixmap);

   if (win)
     {
        sig = eldbus_service_signal_new(th_data->iface, E_TEST_HELPER_SIGNAL_RESTACK);
        eldbus_message_arguments_append(sig, "u", win);
        eldbus_service_signal_send(th_data->iface, sig);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_helper_cb_property_get(const Eldbus_Service_Interface *iface EINA_UNUSED, const char *name, Eldbus_Message_Iter *iter, const Eldbus_Message *msg EINA_UNUSED, Eldbus_Message **err EINA_UNUSED)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, EINA_FALSE);

   if (!e_util_strcmp(name, "Registrant"))
     {
        eldbus_message_iter_basic_append(iter, 'u', th_data->registrant.win);
     }

   return EINA_TRUE;
}

/* externally accessible functions */
EINTERN int
e_test_helper_init(void)
{
   eldbus_init();

   th_data = E_NEW(Test_Helper_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(th_data, err);

   th_data->conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_NULL_GOTO(th_data->conn, err);

   th_data->iface = eldbus_service_interface_register(th_data->conn, PATH, &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(th_data->iface, err);

   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_test_helper_cb_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_test_helper_cb_client_remove, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_STACK,
                        _e_test_helper_cb_client_restack, NULL);

   th_data->registrant.vis = -1;

   return 1;

err:
   e_test_helper_shutdown();
   return 0;
}

EINTERN int
e_test_helper_shutdown(void)
{
   if (th_data)
     {
        E_FREE_LIST(th_data->hdlrs, ecore_event_handler_del);

        eldbus_service_interface_unregister(th_data->iface);
        eldbus_connection_unref(th_data->conn);
        eldbus_shutdown();

        E_FREE(th_data);
     }

   return 1;
}
