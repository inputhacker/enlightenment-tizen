#include "e.h"

static E_Process *_e_process_find(E_Process_Manager *pm, pid_t pid);
static E_Process *_e_process_new(pid_t pid);
static void       _e_process_del(E_Process *pinfo);

static Eina_Bool  _e_process_client_info_add(E_Client *ec);
static void       _e_process_client_info_del(E_Client *ec);

static Eina_Bool  _e_process_cb_client_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool  _e_process_cb_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool  _e_process_cb_client_iconify(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool  _e_process_cb_client_uniconify(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool  _e_process_cb_client_visibility_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool  _e_process_cb_client_focus_in(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);

static void       _e_process_cb_hook_visibility(void *d EINA_UNUSED, E_Client *ec);

static Eina_Bool  _e_process_windows_visible_get(pid_t pid, Eina_Bool *visible);
static void       _e_process_windows_act_no_visible_update(pid_t pid);

static Eina_Bool  _e_process_freeze_condition_check(pid_t pid);
static Eina_Bool  _e_process_freeze(pid_t pid);
static Eina_Bool  _e_process_thaw(pid_t pid);

static void       _e_process_action_change(E_Process *epro, E_Process_Action act);
static void       _e_process_state_change(E_Process *epro, E_Process_State state, Eina_Bool send_event);


static void       _e_process_hooks_clean(void);
static void       _e_process_hook_call(E_Process_Hook_Point hookpoint, E_Process *epro, void *user_data);


static Eina_Inlist *_e_process_hooks[] =
{
   [E_PROCESS_HOOK_STATE_CHANGE] = NULL,
   [E_PROCESS_HOOK_ACTION_CHANGE] = NULL,
};

static int _e_process_hooks_delete = 0;
static int _e_process_hooks_walking = 0;

static Eina_List *_e_process_ec_handlers = NULL;
static Eina_List *_e_process_ec_hooks = NULL;

E_Process_Manager *_e_process_manager;


static E_Process *
_e_process_find(E_Process_Manager *pm, pid_t pid)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(pm, NULL);
   return eina_hash_find(pm->pids_hash, &pid);
}

static E_Process *
_e_process_new(pid_t pid)
{
   E_Process  *pinfo = NULL;

   if (pid <= 0) return NULL;

   pinfo = E_NEW(E_Process, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, NULL);

   pinfo->pid = pid;
   pinfo->state = E_PROCESS_STATE_UNKNOWN;

   eina_hash_add(_e_process_manager->pids_hash, &pid, pinfo);
   _e_process_manager->process_list = eina_inlist_append(_e_process_manager->process_list, EINA_INLIST_GET(pinfo));

   return pinfo;
}

static void
_e_process_del(E_Process *pinfo)
{
   pid_t pid;

   EINA_SAFETY_ON_NULL_RETURN(pinfo);

   pid = pinfo->pid;

   _e_process_manager->process_list = eina_inlist_remove(_e_process_manager->process_list, EINA_INLIST_GET(pinfo));
   eina_hash_del_by_key(_e_process_manager->pids_hash, &pid);

   E_FREE(pinfo);
}

static Eina_Bool
_e_process_client_info_add(E_Client *ec)
{
   E_Process *pinfo = NULL;
   pid_t pid;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   pid = ec->netwm.pid;
   if (pid <= 0) return EINA_FALSE;

   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo)
     {
        pinfo = _e_process_new(pid);
        EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, EINA_FALSE);
     }

   if (!eina_list_data_find(pinfo->ec_list, ec))
     pinfo->ec_list = eina_list_append(pinfo->ec_list, ec);

   return EINA_TRUE;
}

static void
_e_process_client_info_del(E_Client *ec)
{
   E_Process *pinfo = NULL;
   pid_t pid;
   Eina_Bool visible;

   if (!ec) return;

   pid = ec->netwm.pid;
   if (pid <=0) return;

   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo) return;

   if (_e_process_manager->active_win == ec)
     {
        _e_process_manager->active_win = NULL;
        ELOGF("PROCESS", "ACTION DEACTIVATE. PID:%d", NULL, pid);
        _e_process_action_change(pinfo, E_PROCESS_ACT_DEACTIVATE);
     }

   if (pinfo->state != E_PROCESS_STATE_BACKGROUND)
     {
        if (_e_process_windows_visible_get(pid, &visible))
          {
             if (!visible)
               _e_process_windows_act_no_visible_update(pid);
          }
     }

   pinfo->ec_list = eina_list_remove(pinfo->ec_list, ec);

   if (!pinfo->ec_list)
     _e_process_del(pinfo);

   return;
}

static Eina_Bool
_e_process_cb_client_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   _e_process_client_info_add(ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_process_cb_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   _e_process_client_info_del(ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_process_cb_client_iconify(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Process *pinfo;
   pid_t pid;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   pid = ec->netwm.pid;
   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo)
     {
        Eina_Bool ret = EINA_FALSE;
        ret = _e_process_client_info_add(ec);
        if (!ret)
          return ECORE_CALLBACK_PASS_ON;
     }

   // check all ECs of its pid, if yes, freeze
   if (_e_process_freeze_condition_check(pid))
     _e_process_freeze(pid);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_process_cb_client_uniconify(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Process *pinfo;
   pid_t pid;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   pid = ec->netwm.pid;
   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo)
     {
        Eina_Bool ret = EINA_FALSE;
        ret = _e_process_client_info_add(ec);
        if (!ret)
          return ECORE_CALLBACK_PASS_ON;
     }

   if (ec->visible)
     _e_process_thaw(pid);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_process_cb_client_visibility_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Process *pinfo;
   pid_t pid;
   Eina_Bool visible;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   pid = ec->netwm.pid;
   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo)
     {
        Eina_Bool ret = EINA_FALSE;
        ret = _e_process_client_info_add(ec);
        if (!ret)
          return ECORE_CALLBACK_PASS_ON;
     }

   if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
     _e_process_thaw(pid);
   else if (ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        if (!ec->visible)
          {
             if (_e_process_windows_visible_get(pid, &visible))
               {
                  if (!visible)
                    _e_process_windows_act_no_visible_update(pid);
               }
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_process_cb_client_focus_in(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Client *ec_deactive;
   E_Process *pinfo  = NULL;
   E_Process *pinfo_deactive = NULL;
   Eina_Bool change_active = EINA_FALSE;
   pid_t pid = -1;
   pid_t pid_deactivate = -1;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   pid = ec->netwm.pid;
   if (pid <= 0) return ECORE_CALLBACK_PASS_ON;

   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo)
     {
        Eina_Bool ret = EINA_FALSE;
        ret = _e_process_client_info_add(ec);
        if (!ret)
          return ECORE_CALLBACK_PASS_ON;
     }

   ec_deactive = _e_process_manager->active_win;
   _e_process_manager->active_win = ec;

   if (!ec_deactive)
     {
        change_active = EINA_TRUE;
     }
   else
     {
        pid_deactivate = ec_deactive->netwm.pid;
        if (pid_deactivate != pid)
          {
             change_active = EINA_TRUE;
          }
     }

   if (change_active)
     {
        ELOGF("PROCESS", "ACTION ACTIVATE. PID:%d", NULL, pid);
        _e_process_action_change(pinfo, E_PROCESS_ACT_ACTIVATE);

        if (ec_deactive)
          {
             pinfo_deactive = _e_process_find(_e_process_manager, ec_deactive->netwm.pid);
             if (pinfo_deactive)
               {
                  ELOGF("PROCESS", "ACTION DEACTIVATE. PID:%d", NULL, pinfo_deactive->pid);
                  _e_process_action_change(pinfo_deactive, E_PROCESS_ACT_DEACTIVATE);
               }
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_process_cb_hook_visibility(void *d EINA_UNUSED, E_Client *ec)
{
   if (ec->visibility.changed)
     {
        if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
          {
             _e_process_thaw(ec->netwm.pid);
          }
     }
}

static Eina_Bool
_e_process_windows_visible_get(pid_t pid, Eina_Bool *visible)
{
   E_Process *pinfo = NULL;
   E_Client *ec = NULL;
   Eina_Bool exist_visible = EINA_FALSE;
   Eina_List *l;

   if (pid <= 0) return EINA_FALSE;
   if (!visible) return EINA_FALSE;

   pinfo = _e_process_find(_e_process_manager, pid);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, EINA_FALSE);

   if (!pinfo->ec_list) return EINA_FALSE;

   EINA_LIST_FOREACH(pinfo->ec_list, l, ec)
     {
        if (ec->visible && !ec->iconic)
          {
             exist_visible = EINA_TRUE;
             break;
          }
     }

   *visible = exist_visible;
   return EINA_TRUE;
}

static void
_e_process_windows_act_no_visible_update(pid_t pid)
{
   E_Process *pinfo = NULL;

   if (pid <= 0) return;

   pinfo = _e_process_find(_e_process_manager, pid);
   EINA_SAFETY_ON_NULL_RETURN(pinfo);

   _e_process_state_change(pinfo, E_PROCESS_STATE_BACKGROUND, EINA_FALSE);

   ELOGF("PROCESS", "ACTION WINDOWS_HIDDEN. PID:%d", NULL, pinfo->pid);
   _e_process_action_change(pinfo, E_PROCESS_ACT_NO_VISIBLE_WINDOWS);
}

static Eina_Bool
_e_process_freeze_condition_check(pid_t pid)
{
   E_Process *pinfo  = NULL;
   E_Client *ec = NULL;
   Eina_Bool freeze = EINA_TRUE;
   Eina_List *l;

   if (pid <= 0) return EINA_FALSE;

   pinfo = _e_process_find(_e_process_manager, pid);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, EINA_FALSE);

   if (pinfo->state == E_PROCESS_STATE_BACKGROUND) return EINA_FALSE;
   if (!pinfo->ec_list) return EINA_FALSE;

   EINA_LIST_FOREACH(pinfo->ec_list, l, ec)
     {
        if (ec->comp_data &&
            ec->comp_data->sub.data &&
            ec->comp_data->sub.data->parent)
          continue;

        if (ec->visible && !ec->iconic)
          {
             freeze = EINA_FALSE;
             break;
          }
     }

   return freeze;
}

static Eina_Bool
_e_process_freeze(pid_t pid)
{
   E_Process  *pinfo  = NULL;

   if (pid <= 0) return EINA_FALSE;

   pinfo = _e_process_find(_e_process_manager, pid);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, EINA_FALSE);

   if (pinfo->state != E_PROCESS_STATE_BACKGROUND)
     {
        ELOGF("PROCESS", "STATE  BACKGROUND. PID:%d", NULL, pid);
        _e_process_state_change(pinfo, E_PROCESS_STATE_BACKGROUND, EINA_TRUE);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_process_thaw(pid_t pid)
{
   E_Process  *pinfo  = NULL;

   if (pid <= 0) return EINA_FALSE;

   pinfo = _e_process_find(_e_process_manager, pid);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pinfo, EINA_FALSE);

   if (pinfo->state != E_PROCESS_STATE_FOREGROUND)
     {
        ELOGF("PROCESS", "STATE  FOREGROUND. PID:%d", NULL, pid);
        _e_process_state_change(pinfo, E_PROCESS_STATE_FOREGROUND, EINA_TRUE);
     }

   return EINA_TRUE;
}

static void
_e_process_action_change(E_Process *epro, E_Process_Action act)
{
   EINA_SAFETY_ON_NULL_RETURN(epro);

   _e_process_hook_call(E_PROCESS_HOOK_ACTION_CHANGE, epro, (E_Process_Action *)&act);
}

static void
_e_process_state_change(E_Process *epro, E_Process_State state, Eina_Bool send_event)
{
   EINA_SAFETY_ON_NULL_RETURN(epro);

   if (epro->state != state)
     {
        epro->state = state;
        _e_process_hook_call(E_PROCESS_HOOK_STATE_CHANGE, epro, NULL);

        if (!send_event)
          return;

        if (state == E_PROCESS_STATE_FOREGROUND)
          {
             ELOGF("PROCESS", "ACTION FOREGROUND. PID:%d", NULL, epro->pid);
             _e_process_action_change(epro, E_PROCESS_ACT_FOREGROUND);
          }
        else if (state == E_PROCESS_STATE_BACKGROUND)
          {
             ELOGF("PROCESS", "ACTION WINDOWS_HIDDEN. PID:%d", NULL, epro->pid);
             _e_process_action_change(epro, E_PROCESS_ACT_NO_VISIBLE_WINDOWS);
             ELOGF("PROCESS", "ACTION BACKGROUND. PID:%d", NULL, epro->pid);
             _e_process_action_change(epro, E_PROCESS_ACT_BACKGROUND);
          }
     }
}

static void
_e_process_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Process_Hook *ph;
   unsigned int x;

   for (x = 0; x < E_PROCESS_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_process_hooks[x], l, ph)
       {
          if (!ph->delete_me) continue;
          _e_process_hooks[x] = eina_inlist_remove(_e_process_hooks[x],
                                                   EINA_INLIST_GET(ph));
          free(ph);
       }
}

static void
_e_process_hook_call(E_Process_Hook_Point hookpoint, E_Process *epro, void *user_data)
{
   E_Process_Hook *ph;

   _e_process_hooks_walking++;
   EINA_INLIST_FOREACH(_e_process_hooks[hookpoint], ph)
     {
        if (ph->delete_me) continue;
        ph->func(ph->data, epro, user_data);
     }
   _e_process_hooks_walking--;
   if ((_e_process_hooks_walking == 0) && (_e_process_hooks_delete > 0))
     _e_process_hooks_clean();
}


static E_Process_Manager *
_e_process_manager_new(void)
{
   E_Process_Manager *pm;

   pm = E_NEW(E_Process_Manager, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pm, NULL);

   pm->pids_hash = eina_hash_pointer_new(NULL);
   if (!pm->pids_hash) goto error;

   return pm;

error:

   E_FREE(pm);
   return NULL;

}

static void
_e_process_manager_del(E_Process_Manager *pm)
{
   E_Process *pinfo = NULL;

   if (!pm) return;

   EINA_INLIST_FREE(pm->process_list, pinfo)
     {
        pm->process_list = eina_inlist_remove(pm->process_list, EINA_INLIST_GET(pinfo));
        eina_list_free(pinfo->ec_list);
        eina_hash_del_by_key(pm->pids_hash, &pinfo->pid);
        E_FREE(pinfo);
     }

   if (pm->pids_hash)
     {
        eina_hash_free(pm->pids_hash);
        pm->pids_hash = NULL;
     }

   E_FREE(pm);
}


E_API Eina_Bool
e_process_init(void)
{
   E_Process_Manager *e_pm;
   E_Client_Hook *hook;

   e_pm = _e_process_manager_new();
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_pm, EINA_FALSE);

   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_ADD, _e_process_cb_client_add, NULL);
   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_REMOVE, _e_process_cb_client_remove, NULL);
   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_ICONIFY, _e_process_cb_client_iconify, NULL);
   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_UNICONIFY, _e_process_cb_client_uniconify, NULL);
   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_VISIBILITY_CHANGE, _e_process_cb_client_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(_e_process_ec_handlers, E_EVENT_CLIENT_FOCUS_IN, _e_process_cb_client_focus_in, NULL);

   hook = e_client_hook_add(E_CLIENT_HOOK_EVAL_VISIBILITY, _e_process_cb_hook_visibility, NULL);
   if (hook) _e_process_ec_hooks = eina_list_append(_e_process_ec_hooks, hook);

   _e_process_manager = e_pm;

   return EINA_TRUE;
}

E_API int
e_process_shutdown(void)
{
   E_Client_Hook *hook;

   if (!_e_process_manager) return 0;

   E_FREE_LIST(_e_process_ec_handlers, ecore_event_handler_del);
   EINA_LIST_FREE(_e_process_ec_hooks, hook)
      e_client_hook_del(hook);

   _e_process_manager_del(_e_process_manager);
   _e_process_manager = NULL;

   return 1;
}

E_API E_Process_Hook *
e_process_hook_add(E_Process_Hook_Point hookpoint, E_Process_Hook_Cb func, const void *data)
{
   E_Process_Hook *ph;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_PROCESS_HOOK_LAST, NULL);

   ph = E_NEW(E_Process_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ph, NULL);

   ph->hookpoint = hookpoint;
   ph->func = func;
   ph->data = (void*)data;

   _e_process_hooks[hookpoint] = eina_inlist_append(_e_process_hooks[hookpoint], EINA_INLIST_GET(ph));
   return ph;
}

E_API void
e_process_hook_del(E_Process_Hook *ph)
{
   ph->delete_me = 1;
   if (_e_process_hooks_walking == 0)
     {
        _e_process_hooks[ph->hookpoint] = eina_inlist_remove(_e_process_hooks[ph->hookpoint], EINA_INLIST_GET(ph));
        free(ph);
     }
   else
     _e_process_hooks_delete++;
}

E_API E_Process_State
e_process_state_get(pid_t pid)
{
   E_Process *pinfo = NULL;

   if (!_e_process_manager) return E_PROCESS_STATE_UNKNOWN;
   if (pid <= 0) return E_PROCESS_STATE_UNKNOWN;

   pinfo = _e_process_find(_e_process_manager, pid);
   if (!pinfo) return E_PROCESS_STATE_UNKNOWN;

   ELOGF("PROCESS", "GET STATE. PID:%d, state:%d", NULL, pid, pinfo->state);
   return pinfo->state;
}
