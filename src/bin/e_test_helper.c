#include "e.h"
#include "e_policy_wl.h"

#define BUS "org.enlightenment.wm"
#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.Test"

#define E_TH_SIGN_WIN_INFO  "usiiiiibbbiibbbbbi"

typedef struct _Test_Helper_Data
{
   Eldbus_Connection *conn;
   Eldbus_Service_Interface *iface;
   Ecore_Event_Handler *dbus_init_done_h;

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

static Eldbus_Message *_e_test_helper_cb_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_deregister_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_reset_register_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_change_stack(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_activate_window(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_change_iconic_state(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_set_transient_for(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_unset_transient_for(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_set_noti_level(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_set_focus_skip(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_get_client_info(const Eldbus_Service_Interface *iface EINA_UNUSED, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_get_clients(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_get_noti_level(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_dpms(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_ev_freeze(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_ev_mouse(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_ev_key(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_hwc(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_zone_rot_change(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);
static Eldbus_Message *_e_test_helper_cb_zone_rot_get(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg);

enum
{
   E_TEST_HELPER_SIGNAL_CHANGE_VISIBILITY = 0,
   E_TEST_HELPER_SIGNAL_RESTACK,
   E_TEST_HELPER_SIGNAL_WINDOW_ROTATION_CHANGED,
   E_TEST_HELPER_SIGNAL_FOCUS_CHANGED,
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
     [E_TEST_HELPER_SIGNAL_WINDOW_ROTATION_CHANGED] =
       {
          "WinRotationChanged",
          ELDBUS_ARGS({"ui", "a window id was rotated to given angle"}),
          0
       },
     [E_TEST_HELPER_SIGNAL_FOCUS_CHANGED] =
       {
          "FocusChanged",
          ELDBUS_ARGS({"u", "window id of focus changed"}),
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
          "ResetRegisterWindow",
          NULL,
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_reset_register_window, 0
       },
       {
          "SetWindowStack",
          ELDBUS_ARGS({"uui", "window id to restack, above or below, stacking type"}),
          NULL,
          _e_test_helper_cb_change_stack, 0
       },
       {
          "SetWindowActivate",
          ELDBUS_ARGS({"u", "window id to activate"}),
          NULL,
          _e_test_helper_cb_activate_window, 0
       },
       {
          "SetWindowIconify",
          ELDBUS_ARGS({"ub", "window id to change iconic state, iconify or uniconify"}),
          NULL,
          _e_test_helper_cb_change_iconic_state, 0
       },
       {
          "SetWindowTransientFor",
          ELDBUS_ARGS({"uu", "child window id to set transient_for, parent window id to set transient_for"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_set_transient_for, 0
       },
       {
          "UnsetWindowTransientFor",
          ELDBUS_ARGS({"u", "child window id to set transient_for"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_unset_transient_for, 0
       },
       {
          "SetWindowNotiLevel",
          ELDBUS_ARGS({"ui", "window id to set notification level, notification level"}),
          NULL,
          _e_test_helper_cb_set_noti_level, 0
       },
       {
          "SetWindowFocusSkip",
          ELDBUS_ARGS({"ub", "window id to set focus skip, skip set or skip unset"}),
          NULL,
          _e_test_helper_cb_set_focus_skip, 0
       },
       {
          "GetWinInfo",
          ELDBUS_ARGS({"u", "window id"}),
          ELDBUS_ARGS({E_TH_SIGN_WIN_INFO, "information of ec"}),
          _e_test_helper_cb_get_client_info, 0
       },
       {
          "GetWinsInfo",
          NULL,
          ELDBUS_ARGS({"ua("E_TH_SIGN_WIN_INFO")", "array of ec"}),
          _e_test_helper_cb_get_clients, 0
       },
       {
          "GetWindowNotiLevel",
          ELDBUS_ARGS({"u", "window id to get notification level"}),
          ELDBUS_ARGS({"i", "notification level"}),
          _e_test_helper_cb_get_noti_level, 0
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
       {
          "HWC",
          ELDBUS_ARGS({"u", "0=off or 1=on"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_hwc, 0
       },
       {
          "GetCurrentZoneRotation",
          NULL,
          ELDBUS_ARGS({"i", "a angle of current zone"}),
          _e_test_helper_cb_zone_rot_get, 0,
       },
       {
          "ChangeZoneRotation",
          ELDBUS_ARGS({"i", "(0, 90, 180, 270) = specific angle, -1 = unknown state"}),
          ELDBUS_ARGS({"b", "accept or not"}),
          _e_test_helper_cb_zone_rot_change, 0,
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
_e_test_helper_message_append_client(Eldbus_Message_Iter *iter, E_Client *ec)
{
   eldbus_message_iter_arguments_append
      (iter, E_TH_SIGN_WIN_INFO,
       e_pixmap_res_id_get(ec->pixmap),
       e_client_util_name_get(ec) ?: "NO NAME",

       /* geometry */
       ec->x, ec->y, ec->w, ec->h,

       /* layer */
       evas_object_layer_get(ec->frame),

       /* effect */
       evas_object_data_get(ec->frame, "effect_running"),

       /* visibility & iconify */
       ec->visible,
       evas_object_visible_get(ec->frame),
       ec->visibility.opaque,
       ec->visibility.obscured,
       ec->visibility.skip,
       ec->iconic,

       /* color depth */
       ec->argb,

       /* focus */
       ec->focused,
       evas_object_focus_get(ec->frame),

       /* rotation */
       ec->e.state.rot.ang.curr);
}

static Eina_Bool
_e_test_helper_cb_dbus_init_done(void *data EINA_UNUSED, int type, void *event)
{
   E_DBus_Conn_Init_Done_Event *e = event;

   if ((e->status == E_DBUS_CONN_INIT_SUCCESS) && (e->conn_type == ELDBUS_CONNECTION_TYPE_SYSTEM))
     {
        th_data->conn = e_dbus_conn_connection_ref(ELDBUS_CONNECTION_TYPE_SYSTEM);

        if (th_data->conn)
          th_data->iface = eldbus_service_interface_register(th_data->conn, PATH, &iface_desc);
     }

   ecore_event_handler_del(th_data->dbus_init_done_h);
   th_data->dbus_init_done_h = NULL;

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_test_helper_message_append_client_info_by_window_id(Eldbus_Message_Iter *iter, Ecore_Window win)
{
   E_Client *ec;

   ec = e_pixmap_find_client_by_res_id(win);
   if (!ec)
     return;

   _e_test_helper_message_append_client(iter, ec);
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

   eldbus_message_iter_arguments_append(iter, "ua("E_TH_SIGN_WIN_INFO")", th_data->registrant.win, &array_of_ec);

   // append clients.
   for (o = evas_object_top_get(comp->evas); o; o = evas_object_below_get(o))
     {
        Eldbus_Message_Iter* struct_of_ec;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_client_util_ignored_get(ec)) continue;

        eldbus_message_iter_arguments_append(array_of_ec, "("E_TH_SIGN_WIN_INFO")", &struct_of_ec);
        _e_test_helper_message_append_client(struct_of_ec, ec);
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
   if (!th_data->registrant.win)
     {
        th_data->registrant.win = id;
        th_data->registrant.ec = e_pixmap_find_client_by_res_id(id);
     }

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
_e_test_helper_cb_hwc(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool r, accept = EINA_FALSE;
   unsigned int on;

   r = eldbus_message_arguments_get(msg, "u", &on);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(r, reply);

   if (e_comp->hwc)
     {
        switch (on)
          {
           case 0: accept = EINA_TRUE; e_comp_hwc_deactive_set(EINA_TRUE); break;
           case 1: accept = EINA_TRUE; e_comp_hwc_deactive_set(EINA_FALSE); break;
           default: break;
          }
     }

   eldbus_message_arguments_append(reply, "b", accept);

   return reply;
}

static void
_e_test_helper_event_zone_rot_change_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Info_Rotation_Message *ev = event;

   e_object_unref(E_OBJECT(ev->zone));
   free(ev);
}


static Eldbus_Message *
_e_test_helper_cb_zone_rot_change(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   E_Event_Info_Rotation_Message *ev;
   E_Zone *zone;
   Eina_Bool r, accept = EINA_FALSE;
   int rot;

   r = eldbus_message_arguments_get(msg, "i", &rot);
   EINA_SAFETY_ON_FALSE_GOTO(r, end);

   if ((rot < 0) || (rot > 270) || ((rot % 90) != 0))
     goto end;

   zone = e_zone_current_get();
   if (!zone)
     goto end;

   ev = E_NEW(E_Event_Info_Rotation_Message, 1);
   if (EINA_UNLIKELY(!ev))
     {
        ERR("Failed to allocate 'E_Event_Info_Rotation_Message'");
        goto end;
     }

   e_object_ref(E_OBJECT(zone));
   ev->zone = zone;
   ev->message = E_INFO_ROTATION_MESSAGE_SET;
   ev->rotation = rot;
   ecore_event_add(E_EVENT_INFO_ROTATION_MESSAGE, ev, _e_test_helper_event_zone_rot_change_free, NULL);

   accept = EINA_TRUE;

end:
   eldbus_message_arguments_append(reply, "b", accept);

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_zone_rot_get(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   E_Zone *zone;
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Eina_Bool r;
   int rot = -1;

   r = eldbus_message_arguments_get(msg, "i", &rot);
   EINA_SAFETY_ON_FALSE_GOTO(r, end);

   if ((rot < 0) || (rot > 270) || ((rot % 90) != 0))
     goto end;

   zone = e_zone_current_get();
   if (!zone)
     goto end;

   eldbus_message_arguments_append(reply, "b", zone->rot.curr);
end:
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

static Eldbus_Message *
_e_test_helper_cb_reset_register_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                        const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   eldbus_message_arguments_append(reply, "b", !th_data->registrant.win);

   if (th_data->registrant.win)
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

static Eldbus_Message*
_e_test_helper_cb_activate_window(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                  const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win = 0x0;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &win))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        return reply;
     }

   if (win)
     {
        ec = e_pixmap_find_client_by_res_id(win);
        e_policy_wl_activate(ec);
     }

   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_change_iconic_state(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                      const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win = 0x0;
   Eina_Bool iconic = EINA_FALSE;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "ub", &win, &iconic))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        return reply;
     }

   if (win)
     {
        ec = e_pixmap_find_client_by_res_id(win);
        if (iconic)
          e_policy_wl_iconify(ec);
        else
          e_policy_wl_uniconify(ec);
     }

   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_set_transient_for(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                    const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window parent = 0x0, child = 0x0;
   E_Client *ec = NULL, *pec = NULL;
   Eina_Bool accept = EINA_FALSE;

   EINA_SAFETY_ON_NULL_GOTO(th_data, fin);

   if (!eldbus_message_arguments_get(msg, "uu", &child, &parent))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        goto fin;
     }

   if (parent && child)
     {
        pec = e_pixmap_find_client_by_res_id(parent);
        EINA_SAFETY_ON_NULL_GOTO(pec, fin);

        ec = e_pixmap_find_client_by_res_id(child);
        EINA_SAFETY_ON_NULL_GOTO(ec, fin);

        e_policy_stack_transient_for_set(ec, pec);
        accept = EINA_TRUE;
     }

fin:
   eldbus_message_arguments_append(reply, "b", accept);
   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_unset_transient_for(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                      const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window child = 0x0;
   E_Client *ec = NULL;
   Eina_Bool accept = EINA_FALSE;

   EINA_SAFETY_ON_NULL_GOTO(th_data, fin);

   if (!eldbus_message_arguments_get(msg, "u", &child))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        goto fin;
     }

   if (child)
     {
        ec = e_pixmap_find_client_by_res_id(child);
        EINA_SAFETY_ON_NULL_GOTO(ec, fin);

        e_policy_stack_transient_for_set(ec, NULL);
        accept = EINA_TRUE;
     }

fin:
   eldbus_message_arguments_append(reply, "b", accept);
   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_set_noti_level(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                 const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win = 0x0;
   int layer = 0;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "ui", &win, &layer))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        return reply;
     }

   if (win)
     {
        ec = e_pixmap_find_client_by_res_id(win);
        if (!ec) return reply;

        evas_object_layer_set(ec->frame, layer);
     }

   return reply;
}

static Eldbus_Message*
_e_test_helper_cb_set_focus_skip(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                 const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win = 0x0;
   Eina_Bool skip_set = EINA_FALSE;
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "ub", &win, &skip_set))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        return reply;
     }

   if (win)
     {
        ec = e_pixmap_find_client_by_res_id(win);
        if (!ec) return reply;

        ec->icccm.accepts_focus = ec->icccm.take_focus = !skip_set;
        ec->changes.accepts_focus = 1;
        ec->changed = 1;
     }

   return reply;
}

static Eldbus_Message *
_e_test_helper_cb_get_client_info(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                  const Eldbus_Message *msg)
{
   Eldbus_Message *reply;
   Ecore_Window win;

   reply = eldbus_message_method_return_new(msg);
   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, reply);

   if (!eldbus_message_arguments_get(msg, "u", &win))
     {
        ERR("error getting window ID");
        return reply;
     }

   _e_test_helper_message_append_client_info_by_window_id(eldbus_message_iter_get(reply), win);

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

static Eldbus_Message *
_e_test_helper_cb_get_noti_level(const Eldbus_Service_Interface *iface EINA_UNUSED,
                                 const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   Ecore_Window win = 0x0;
   E_Client *ec = NULL;
   int layer = -1;

   if (!eldbus_message_arguments_get(msg, "u", &win))
     {
        ERR("error on eldbus_message_arguments_get()\n");
        goto fin;
     }

   ec = e_pixmap_find_client_by_res_id(win);
   if (!ec) goto fin;

   layer = ec->layer;

fin:
   eldbus_message_arguments_append(reply, "i", layer);
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
_e_test_helper_cb_client_rotation_end(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   Eldbus_Message *sig;
   Ecore_Window win;
   int rot;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   if(!th_data->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;

   win = e_pixmap_res_id_get(ec->pixmap);
   rot = ec->e.state.rot.ang.curr;

   if (win)
     {
        sig = eldbus_service_signal_new(th_data->iface, E_TEST_HELPER_SIGNAL_WINDOW_ROTATION_CHANGED);
        eldbus_message_arguments_append(sig, "ui", win, rot);
        eldbus_service_signal_send(th_data->iface, sig);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_test_helper_cb_client_focus_in(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   Eldbus_Message *sig = NULL;
   Ecore_Window win = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(th_data, ECORE_CALLBACK_PASS_ON);

   if(!th_data->registrant.ec) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;

   win = e_pixmap_res_id_get(ec->pixmap);

   if (win)
     {
        sig = eldbus_service_signal_new(th_data->iface, E_TEST_HELPER_SIGNAL_FOCUS_CHANGED);
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
   Eina_Bool res = EINA_FALSE;

   EINA_SAFETY_ON_TRUE_GOTO((e_dbus_conn_init() <= 0), err);

   th_data = E_NEW(Test_Helper_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(th_data, err);

   th_data->dbus_init_done_h = ecore_event_handler_add(E_EVENT_DBUS_CONN_INIT_DONE, _e_test_helper_cb_dbus_init_done, NULL);
   EINA_SAFETY_ON_NULL_GOTO(th_data->dbus_init_done_h, err);

   res = e_dbus_conn_dbus_init(ELDBUS_CONNECTION_TYPE_SYSTEM);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_test_helper_cb_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_test_helper_cb_client_remove, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_STACK,
                        _e_test_helper_cb_client_restack, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_ROTATION_CHANGE_END,
                         _e_test_helper_cb_client_rotation_end, NULL);
   E_LIST_HANDLER_APPEND(th_data->hdlrs, E_EVENT_CLIENT_FOCUS_IN,
                         _e_test_helper_cb_client_focus_in, NULL);

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

        if (th_data->dbus_init_done_h)
          {
             ecore_event_handler_del(th_data->dbus_init_done_h);
             th_data->dbus_init_done_h = NULL;
          }

        if (th_data->conn)
          {
             if (th_data->iface)
               eldbus_service_interface_unregister(th_data->iface);

             e_dbus_conn_connection_unref(th_data->conn);
             th_data->conn = NULL;
          }

        E_FREE(th_data);
     }

   return 1;
}
