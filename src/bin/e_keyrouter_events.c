#include "e_keyrouter_private.h"

static void _e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev);
static void _e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev);
static void _e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev);
static void _e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode);

static Eina_Bool _e_keyrouter_send_key_events_focus(int type, struct wl_resource *surface, Ecore_Event_Key *ev, struct wl_resource **delivered_surface);

static Eina_Bool _e_keyrouter_is_key_grabbed(int key);
static Eina_Bool _e_keyrouter_check_top_visible_window(E_Client *ec_focus, int arr_idx);

static Eina_Bool
_e_keyrouter_is_key_grabbed(int key)
{
   if (!krt->HardKeys[key].keycode)
     {
        return EINA_FALSE;
     }
   if (krt->HardKeys[key].excl_ptr ||
        krt->HardKeys[key].or_excl_ptr ||
        krt->HardKeys[key].top_ptr ||
        krt->HardKeys[key].shared_ptr)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_keyrouter_event_routed_key_check(Ecore_Event_Key *ev, int type)
{
   Eina_List *l, *l_next;
   int *keycode_data;

   if ((ev->modifiers != 0) && (type == ECORE_EVENT_KEY_DOWN))
     {
        KLDBG("Modifier key delivered to Focus window : Key %s(%d)", ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode);
        keycode_data = E_NEW(int, 1);
        if (keycode_data)
          {
             *keycode_data = ev->keycode;
             krt->ignore_list = eina_list_append(krt->ignore_list, keycode_data);
          }
        return EINA_FALSE;
     }

   EINA_LIST_FOREACH_SAFE(krt->ignore_list, l, l_next, keycode_data)
     {
        if (*keycode_data == ev->keycode)
          {
             KLDBG("Find ignore key, propagate event (%d)\n", ev->keycode);
             E_FREE(keycode_data);
             krt->ignore_list = eina_list_remove_list(krt->ignore_list, l);

             return EINA_FALSE;
          }
     }

   if (krt->max_tizen_hwkeys < ev->keycode)
     {
        KLWRN("The key(%d) is too larger to process keyrouting: Invalid keycode", ev->keycode);
        return EINA_FALSE;
     }

   if (!krt->HardKeys[ev->keycode].keycode) return EINA_FALSE;

   return EINA_TRUE;
}

/* Function for checking the existing grab for a key and sending key event(s) */
Eina_Bool
e_keyrouter_event_process(void *event, int type)
{
   Eina_Bool res = EINA_FALSE;
   Ecore_Event_Key *ev = event;
   E_Keyrouter_Event_Data *key_data;

   KLDBG("[%s] keyname: %s, key: %s, keycode: %d", (type == ECORE_EVENT_KEY_DOWN) ? "KEY_PRESS" : "KEY_RELEASE", ev->keyname, ev->key, ev->keycode);

   e_screensaver_notidle();

   if (!ev->data)
     {
        KLWRN("%s key (%d) %s is not handled by keyrouter\n", ev->keyname, ev->keycode, (type == ECORE_EVENT_KEY_DOWN) ? "press" : "release");
        goto focus_deliver;
     }

   key_data = (E_Keyrouter_Event_Data *)ev->data;

   if (key_data->client || key_data->surface)
     {
        e_keyrouter_wl_key_send(ev, (type==ECORE_EVENT_KEY_DOWN)?EINA_TRUE:EINA_FALSE, key_data->client, key_data->surface, EINA_FALSE);
        return EINA_TRUE;
     }

   if (!_e_keyrouter_event_routed_key_check(event, type))
     {
        goto focus_deliver;
     }

   res = e_keyrouter_intercept_hook_call(E_KEYROUTER_INTERCEPT_HOOK_BEFORE_KEYROUTING, type, ev);
   if (res)
     {
        if (key_data->ignored) goto finish;
        if (key_data->client || key_data->surface)
          {
             e_keyrouter_wl_key_send(ev, (type==ECORE_EVENT_KEY_DOWN)?EINA_TRUE:EINA_FALSE, key_data->client, key_data->surface, EINA_FALSE);
             goto finish;
          }
     }
   else
     {
        goto finish;
     }

   //KLDBG("The key(%d) is going to be sent to the proper wl client(s) !", ev->keycode);
   KLDBG("[%s] keyname: %s, key: %s, keycode: %d", (type == ECORE_EVENT_KEY_DOWN) ? "KEY_PRESS" : "KEY_RELEASE", ev->keyname, ev->key, ev->keycode);
   _e_keyrouter_send_key_events(type, ev);
   return EINA_FALSE;

focus_deliver:
   res = e_comp_wl_key_process(event, type);
finish:
   return res;
}

/* Function for sending key events to wl_client(s) */
static void
_e_keyrouter_send_key_events(int type, Ecore_Event_Key *ev)
{
   if (ECORE_EVENT_KEY_DOWN == type)
     {
        _e_keyrouter_send_key_events_press(type, ev);
     }
  else
     {
        _e_keyrouter_send_key_events_release(type, ev);
     }
}

static void
_e_keyrouter_send_key_events_release(int type, Ecore_Event_Key *ev)
{
   int pid = 0;
   char *pname = NULL, *cmd = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_Bool res_hook = EINA_TRUE;
   E_Keyrouter_Event_Data *key_data = NULL;

   /* Deliver release  clean up pressed key list */
   EINA_LIST_FREE(krt->HardKeys[ev->keycode].press_ptr, key_node_data)
     {
        if (key_node_data->focused == EINA_TRUE)
          {
             res_hook = e_keyrouter_intercept_hook_call(E_KEYROUTER_INTERCEPT_HOOK_DELIVER_FOCUS, type, ev);
             key_data = (E_Keyrouter_Event_Data *)ev->data;

             if (res_hook)
               {
                  if (key_data->ignored)
                    {
                       E_FREE(key_node_data);
                       continue;
                    }
                  if (key_data->surface || key_data->client)
                    {
                       _e_keyrouter_send_key_event(type, key_data->surface, key_data->client, ev,
                                                   EINA_FALSE, TIZEN_KEYROUTER_MODE_PRESSED);

                       pid = e_keyrouter_util_get_pid(key_data->client, key_data->surface);
                       cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                       pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
                       KLINF("Release Hook : %s(%s:%d)(Focus: %d)(Status: %d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                             ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, key_node_data->focused,
                             key_node_data->status, key_data->surface, key_data->client, pid, pname ?: "Unknown");
                       if(pname) E_FREE(pname);
                       if(cmd) E_FREE(cmd);

                       E_FREE(key_node_data);
                       continue;
                    }
               }
          }

        if (!res_hook)
          {
             E_FREE(key_node_data);
             continue;
          }

        if (key_node_data->status == E_KRT_CSTAT_ALIVE)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                         key_node_data->focused, TIZEN_KEYROUTER_MODE_PRESSED);

             pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
             cmd = e_keyrouter_util_cmd_get_from_pid(pid);
             pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
             KLINF("Release Pair : %s(%s:%d)(Focus: %d)(Status: %d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, key_node_data->focused,
                      key_node_data->status, key_node_data->surface, key_node_data->wc, pid, pname ?: "Unknown");
             if(pname) E_FREE(pname);
             if(cmd) E_FREE(cmd);
          }
        else
          {
             KLINF("Release Skip : %s(%s:%d)(Focus: %d)(Status: %d) => wl_surface (%p) wl_client (%p) process is ungrabbed / dead",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, key_node_data->focused,
                      key_node_data->status, key_node_data->surface, key_node_data->wc);
          }

        E_FREE(key_node_data);
     }
   krt->HardKeys[ev->keycode].press_ptr = NULL;
}

static void
_e_keyrouter_send_key_events_press(int type, Ecore_Event_Key *ev)
{
   unsigned int keycode = ev->keycode;
   struct wl_resource *surface_focus = NULL;
   E_Client *ec_focus = NULL;
   struct wl_resource *delivered_surface = NULL;
   Eina_Bool res;
   int ret = 0;
   int pid = 0;
   char *pname = NULL, *cmd = NULL;

   E_Keyrouter_Key_List_NodePtr key_node_data;
   Eina_List *l = NULL;

   ec_focus = e_client_focused_get();
   surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);

   if (krt->isPictureOffEnabled == 1)
     {
       EINA_LIST_FOREACH(krt->HardKeys[keycode].pic_off_ptr, l, key_node_data)
          {
            if (key_node_data)
                {
                 _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev, key_node_data->focused, TIZEN_KEYROUTER_MODE_SHARED);

                 pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
                 cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                 pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
                 KLINF("PICTURE OFF : %s(%d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                       ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keycode, key_node_data->surface, key_node_data->wc, pid, pname ?: "Unknown");
                 if(pname) E_FREE(pname);
                 if(cmd) E_FREE(cmd);
                }
          }
       return;
     }
   if (!_e_keyrouter_is_key_grabbed(ev->keycode))
     {
        _e_keyrouter_send_key_events_focus(type, surface_focus, ev, &delivered_surface);
        return;
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                        key_node_data->focused, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

             pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
             cmd = e_keyrouter_util_cmd_get_from_pid(pid);
             pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
             KLINF("EXCLUSIVE : %s(%s:%d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                      ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                      key_node_data->surface, key_node_data->wc, pid, pname ?: "Unknown");
             if(pname) E_FREE(pname);
             if(cmd) E_FREE(cmd);
             return;
          }
     }

   EINA_LIST_FOREACH(krt->HardKeys[keycode].or_excl_ptr, l, key_node_data)
     {
        if (key_node_data)
          {
             _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev,
                                         key_node_data->focused, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

             pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
             cmd = e_keyrouter_util_cmd_get_from_pid(pid);
             pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
             KLINF("OVERRIDABLE_EXCLUSIVE : %s(%s:%d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                     ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                     key_node_data->surface, key_node_data->wc, pid, pname ?: "Unknown");
             if(pname) E_FREE(pname);
             if(cmd) E_FREE(cmd);

             return;
          }
     }

   // Top position grab must need a focus surface.
   if (surface_focus)
     {
        EINA_LIST_FOREACH(krt->HardKeys[keycode].top_ptr, l, key_node_data)
          {
             if (key_node_data)
               {
                  if ((EINA_FALSE == krt->isWindowStackChanged) && (surface_focus == key_node_data->surface))
                    {
                       pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
                       cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                       pname = e_keyrouter_util_process_name_get_from_cmd(cmd);

                       _e_keyrouter_send_key_event(type, key_node_data->surface, NULL, ev, key_node_data->focused,
                                                   TIZEN_KEYROUTER_MODE_TOPMOST);
                       KLINF("TOPMOST (TOP_POSITION) : %s (%s:%d) => wl_surface (%p) (pid: %d) (pname: %s)",
                                ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                                key_node_data->surface, pid, pname ?: "Unknown");

                       if(pname) E_FREE(pname);
                       if(cmd) E_FREE(cmd);
                       return;
                    }
                  krt->isWindowStackChanged = EINA_FALSE;

                  if (_e_keyrouter_check_top_visible_window(ec_focus, keycode))
                    {
                       E_Keyrouter_Key_List_NodePtr top_key_node_data = eina_list_data_get(krt->HardKeys[keycode].top_ptr);
                       pid = e_keyrouter_util_get_pid(top_key_node_data->wc, top_key_node_data->surface);
                       cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                       pname = e_keyrouter_util_process_name_get_from_cmd(cmd);

                       _e_keyrouter_send_key_event(type, top_key_node_data->surface, NULL, ev, top_key_node_data->focused,
                                                   TIZEN_KEYROUTER_MODE_TOPMOST);
                       KLINF("TOPMOST (TOP_POSITION) : %s (%s:%d) => wl_surface (%p) (pid: %d) (pname: %s)",
                             ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                             top_key_node_data->surface, pid, pname ?: "Unknown");

                       if(pname) E_FREE(pname);
                       if(cmd) E_FREE(cmd);
                       return;
                    }
                  break;
               }
          }
       goto need_shared;
     }

   if (krt->HardKeys[keycode].shared_ptr)
     {
need_shared:
        res = _e_keyrouter_send_key_events_focus(type, surface_focus, ev, &delivered_surface);
        if (delivered_surface)
          {
             ret = e_keyrouter_wl_add_surface_destroy_listener(delivered_surface);
             if (ret != TIZEN_KEYROUTER_ERROR_NONE)
               {
                  KLWRN("Failed to add wl_surface to destroy listener (res: %d)", res);
               }
          }
        if (res)
          {
             EINA_LIST_FOREACH(krt->HardKeys[keycode].shared_ptr, l, key_node_data)
               {
                  if (key_node_data)
                    {
                       if (delivered_surface && key_node_data->surface == delivered_surface)
                         {
                            // Check for already delivered surface
                            // do not deliver double events in this case.
                            continue;
                         }
                       else
                         {
                            _e_keyrouter_send_key_event(type, key_node_data->surface, key_node_data->wc, ev, key_node_data->focused, TIZEN_KEYROUTER_MODE_SHARED);
                            pid = e_keyrouter_util_get_pid(key_node_data->wc, key_node_data->surface);
                            cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                            pname = e_keyrouter_util_process_name_get_from_cmd(cmd);
                            KLINF("SHARED : %s(%s:%d) => wl_surface (%p) wl_client (%p) (pid: %d) (pname: %s)",
                                  ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, key_node_data->surface, key_node_data->wc, pid, pname ?: "Unknown");
                            if(pname) E_FREE(pname);
                            if(cmd) E_FREE(cmd);
                         }
                    }
               }
          }
     }
}

static Eina_Bool
_e_keyrouter_send_key_events_focus(int type, struct wl_resource *surface_focus,  Ecore_Event_Key *ev, struct wl_resource **delivered_surface)
{
   Eina_Bool res = EINA_TRUE;
   int pid = 0;
   char *pname = NULL, *cmd = NULL;
   E_Keyrouter_Event_Data *key_data;

   res = e_keyrouter_intercept_hook_call(E_KEYROUTER_INTERCEPT_HOOK_DELIVER_FOCUS, type, ev);
   key_data = (E_Keyrouter_Event_Data *)ev->data;
   if (res)
     {
        if (key_data->ignored)
          {
             e_keyrouter_prepend_to_keylist(NULL, NULL, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED, EINA_TRUE);
             return EINA_TRUE;
          }
        else if (key_data->surface)
          {
             *delivered_surface = key_data->surface;
             e_keyrouter_prepend_to_keylist(key_data->surface, key_data->client, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED, EINA_TRUE);
             res = e_keyrouter_wl_key_send(ev, (type==ECORE_EVENT_KEY_DOWN)?EINA_TRUE:EINA_FALSE, key_data->client, key_data->surface, EINA_FALSE);

             pid = e_keyrouter_util_get_pid(NULL, key_data->surface);
             cmd = e_keyrouter_util_cmd_get_from_pid(pid);
             pname = e_keyrouter_util_process_name_get_from_cmd(cmd);

             KLINF("FOCUS HOOK : %s(%s:%d) => wl_surface (%p) (pid: %d) (pname: %s)",
                   ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode,
                   key_data->surface, pid, pname ?: "Unknown");

             if(pname) E_FREE(pname);
             if(cmd) E_FREE(cmd);
             return EINA_TRUE;
          }
     }
   else
     {
        e_keyrouter_prepend_to_keylist(NULL, NULL, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED, EINA_TRUE);
        return EINA_FALSE;
     }

   pid = e_keyrouter_util_get_pid(NULL, surface_focus);
   cmd = e_keyrouter_util_cmd_get_from_pid(pid);
   pname = e_keyrouter_util_process_name_get_from_cmd(cmd);

   _e_keyrouter_send_key_event(type, surface_focus, NULL,ev, EINA_TRUE, TIZEN_KEYROUTER_MODE_SHARED);
   KLINF("FOCUS DIRECT : %s(%s:%d) => wl_surface (%p) (pid: %d) (pname: %s)",
         ((ECORE_EVENT_KEY_DOWN == type) ? "Down" : "Up"), ev->keyname, ev->keycode, surface_focus, pid, pname ?: "Unknown");
   *delivered_surface = surface_focus;
   if(pname) E_FREE(pname);
   if(cmd) E_FREE(cmd);
   return res;
}

static Eina_Bool
_e_keyrouter_check_top_visible_window(E_Client *ec_focus, int arr_idx)
{
   E_Client *ec_top = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   ec_top = e_client_top_get();

   while (ec_top)
     {
        if (ec_top->visibility.obscured != E_VISIBILITY_UNOBSCURED)
          {
             if (ec_top == ec_focus)
               {
                  KLDBG("Top e_client (%p) is invisible(%d) but focus client", ec_top, ec_top->visible);
                  return EINA_FALSE;
               }
             ec_top = e_client_below_get(ec_top);
             continue;
          }

        /* TODO: Check this client is located inside a display boundary */

        EINA_LIST_FOREACH_SAFE(krt->HardKeys[arr_idx].top_ptr, l, l_next, key_node_data)
          {
             if (key_node_data)
               {
                  if (ec_top == wl_resource_get_user_data(key_node_data->surface))
                    {
                       krt->HardKeys[arr_idx].top_ptr = eina_list_promote_list(krt->HardKeys[arr_idx].top_ptr, l);
                       KLDBG("Move a client(e_client: %p, wl_surface: %p) to first index of list(key: %d)",
                                ec_top, key_node_data->surface, arr_idx);
                       return EINA_TRUE;
                    }
               }
          }

        if (ec_top == ec_focus)
          {
             KLDBG("The e_client(%p) is a focus client", ec_top);
             return EINA_FALSE;
          }

        ec_top = e_client_below_get(ec_top);
     }
   return EINA_FALSE;
}

/* Function for sending key event to wl_client(s) */
static void
_e_keyrouter_send_key_event(int type, struct wl_resource *surface, struct wl_client *wc, Ecore_Event_Key *ev, Eina_Bool focused, unsigned int mode)
{
   struct wl_client *wc_send;
   Eina_Bool pressed = EINA_FALSE;

   if (surface == NULL) wc_send = wc;
   else wc_send = wl_resource_get_client(surface);

   if (!wc_send)
     {
        KLWRN("wl_surface: %p or wl_client: %p returns null wayland client", surface, wc);
        return;
     }

   if (ECORE_EVENT_KEY_DOWN == type)
     {
        pressed = EINA_TRUE;
        e_keyrouter_prepend_to_keylist(surface, wc, ev->keycode, TIZEN_KEYROUTER_MODE_PRESSED, focused);
     }

   e_keyrouter_wl_key_send(ev, pressed, wc_send, surface, focused);

   return;
}

struct wl_resource *
e_keyrouter_util_get_surface_from_eclient(E_Client *client)
{
   if (!client || !client->comp_data) return NULL;

   return client->comp_data->wl_surface;
}

int
e_keyrouter_util_get_pid(struct wl_client *client, struct wl_resource *surface)
{
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;
   struct wl_client *cur_client = NULL;

   if (client) cur_client = client;
   else if (surface) cur_client = wl_resource_get_client(surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cur_client, 0);

   wl_client_get_credentials(cur_client, &pid, &uid, &gid);

   return pid;
}

char *
e_keyrouter_util_cmd_get_from_pid(int pid)
{
   Eina_List *l;
   E_Comp_Connected_Client_Info *cdata;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);

   EINA_LIST_FOREACH(e_comp->connected_clients, l, cdata)
     {
        if (cdata->pid == pid) return strdup(cdata->name);
     }

   return NULL;
}

typedef struct _keycode_map{
    xkb_keysym_t keysym;
    xkb_keycode_t keycode;
}keycode_map;

static void
find_keycode(struct xkb_keymap *keymap, xkb_keycode_t key, void *data)
{
   keycode_map *found_keycodes = (keycode_map *)data;
   xkb_keysym_t keysym = found_keycodes->keysym;
   int nsyms = 0;
   const xkb_keysym_t *syms_out = NULL;

   nsyms = xkb_keymap_key_get_syms_by_level(keymap, key, 0, 0, &syms_out);
   if (nsyms && syms_out)
     {
        if (*syms_out == keysym)
          {
             found_keycodes->keycode = key;
          }
     }
}

int
_e_keyrouter_keycode_get_from_keysym(struct xkb_keymap *keymap, xkb_keysym_t keysym)
{
   keycode_map found_keycodes = {0,};
   found_keycodes.keysym = keysym;
   xkb_keymap_key_for_each(keymap, find_keycode, &found_keycodes);

   return found_keycodes.keycode;
}

int
e_keyrouter_util_keycode_get_from_string(char * name)
{
   struct xkb_keymap *keymap = NULL;
   xkb_keysym_t keysym = 0x0;
   int keycode = 0;

   keymap = e_comp_wl->xkb.keymap;
   EINA_SAFETY_ON_NULL_GOTO(keymap, finish);

   keysym = xkb_keysym_from_name(name, XKB_KEYSYM_NO_FLAGS);
   EINA_SAFETY_ON_FALSE_GOTO(keysym != XKB_KEY_NoSymbol, finish);

   keycode = _e_keyrouter_keycode_get_from_keysym(keymap, keysym);

   KLDBG("request name: %s, return value: %d", name, keycode);

   return keycode;

finish:
   return 0;
}

char *
e_keyrouter_util_keyname_get_from_keycode(int keycode)
{
   struct xkb_state *state;
   xkb_keysym_t sym = XKB_KEY_NoSymbol;
   char name[256] = {0, };

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl->xkb.state, NULL);

   state = e_comp_wl->xkb.state;
   sym = xkb_state_key_get_one_sym(state, keycode);
   xkb_keysym_get_name(sym, name, sizeof(name));

   return strdup(name);
}

char *
e_keyrouter_util_process_name_get_from_cmd(char *cmd)
{
   int len, i;
   char pbuf = '\0';
   char *pname = NULL;
   if (cmd)
     {
        len = strlen(cmd);
        for (i = 0; i < len; i++)
          {
             pbuf = cmd[len - i - 1];
             if (pbuf == '/')
               {
                  pname = &cmd[len - i];
                  return strdup(pname);
               }
          }
     }
   return NULL;
}

