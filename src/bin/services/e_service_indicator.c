#include "e.h"
#include "services/e_service_indicator.h"
#include "e_policy_wl.h"

static E_Client *_ind_server = NULL;
static E_Client *_ind_owner = NULL;

/* event handler */
static Eina_List *_ind_handlers = NULL;
static Eina_List *_ind_hooks = NULL;
static int _ind_angle = -1;

static Eina_Bool
_indicator_cb_rot_begin(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client_Rotation_Change_Begin *ev = NULL;

   ev = event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (ec == _ind_owner)
     {
        if (ec->e.state.rot.ang.next != -1)
          {
             ELOGF("TZ_IND", "Rotation begins.(ec:%s) Set indicator's angle to next angle:%d", ec, ec->icccm.name?:"NULL", ec->e.state.rot.ang.next);
             _ind_angle = ec->e.state.rot.ang.next;
             e_tzsh_indicator_srv_property_change_send(ec, ec->e.state.rot.ang.next);
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_indicator_cb_rot_done(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client_Rotation_Change_End *ev = NULL;

   ev = event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (ec == _ind_owner)
     {
        if (_ind_angle != ec->e.state.rot.ang.curr)
          {
             ELOGF("TZ_IND", "Rotation is done.(ec:%s) Set indicator's angle to curr angle:%d", ec, ec->icccm.name?:"NULL", ec->e.state.rot.ang.curr);
             _ind_angle = ec->e.state.rot.ang.curr;
             e_tzsh_indicator_srv_property_change_send(ec, ec->e.state.rot.ang.curr);
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_indicator_client_unset(void)
{
   E_FREE_LIST(_ind_handlers, ecore_event_handler_del);
   _ind_handlers = NULL;

   E_FREE_LIST(_ind_hooks, e_client_hook_del);
   _ind_hooks = NULL;

   _ind_server = NULL;
}

static void
_indicator_cb_client_del(void *d EINA_UNUSED, E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (_ind_server != ec) return;

   _indicator_client_unset();
}

EINTERN Eina_Bool
e_mod_indicator_client_set(E_Client *ec)
{
   if (!ec)
     {
        if (_ind_server)
          _indicator_client_unset();

        return EINA_TRUE;
     }

   if (_ind_server)
     {
        ERR("Indicater service is already registered."
            "Multi indicator service is not supported.");
        return EINA_FALSE;
     }

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ELOGF("TZ_IND", "Set indicator service", ec);

   e_client_window_role_set(ec, "indicator");

   E_LIST_HANDLER_APPEND(_ind_handlers, E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN, _indicator_cb_rot_begin, NULL);
   E_LIST_HANDLER_APPEND(_ind_handlers, E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL, _indicator_cb_rot_done, NULL);
   E_LIST_HANDLER_APPEND(_ind_handlers, E_EVENT_CLIENT_ROTATION_CHANGE_END, _indicator_cb_rot_done, NULL);
   E_LIST_HOOK_APPEND(_ind_hooks, E_CLIENT_HOOK_DEL, _indicator_cb_client_del, NULL);

   _ind_server = ec;
   if (!_ind_owner)
     _ind_owner = e_client_focused_get();

   return EINA_TRUE;
}

EINTERN void
e_mod_indicator_owner_set(E_Client *ec)
{
   _ind_owner = ec;
}

EINTERN E_Client *
e_mod_indicator_owner_get(void)
{
   return _ind_owner;
}


