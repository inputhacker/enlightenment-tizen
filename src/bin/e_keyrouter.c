#include "e.h"
#include "e_keyrouter.h"
#include "e_keyrouter_private.h"

static int _e_keyrouter_intercept_hooks_delete = 0;
static int _e_keyrouter_intercept_hooks_walking = 0;

static Eina_Inlist *_e_keyrouter_intercept_hooks[] =
{
   [E_KEYROUTER_INTERCEPT_HOOK_BEFORE_KEYROUTING] = NULL,
   [E_KEYROUTER_INTERCEPT_HOOK_DELIVER_FOCUS] = NULL,
};

int _keyrouter_log_dom = -1;

E_API E_Keyrouter_Info e_keyrouter;
E_KeyrouterPtr krt;

E_API E_Keyrouter_Intercept_Hook *
e_keyrouter_intercept_hook_add(E_Keyrouter_Intercept_Hook_Point hookpoint, E_Keyrouter_Intercept_Hook_Cb func, const void *data)
{
   E_Keyrouter_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_KEYROUTER_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Keyrouter_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_keyrouter_intercept_hooks[hookpoint] = eina_inlist_append(_e_keyrouter_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_keyrouter_intercept_hook_del(E_Keyrouter_Intercept_Hook *ch)
{
   EINA_SAFETY_ON_NULL_RETURN(ch);

   ch->delete_me = 1;
   if (_e_keyrouter_intercept_hooks_walking == 0)
     {
        _e_keyrouter_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_keyrouter_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_keyrouter_intercept_hooks_delete++;
}

static void
_e_keyrouter_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Keyrouter_Intercept_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_KEYROUTER_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_keyrouter_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_keyrouter_intercept_hooks[x] = eina_inlist_remove(_e_keyrouter_intercept_hooks[x], EINA_INLIST_GET(ch));
         free(ch);
       }
}

E_API Eina_Bool
e_keyrouter_intercept_hook_call(E_Keyrouter_Intercept_Hook_Point hookpoint, int type, Ecore_Event_Key *event)
{
   E_Keyrouter_Intercept_Hook *ch;
   Eina_Bool res = EINA_TRUE;

   _e_keyrouter_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_keyrouter_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        res = ch->func(ch->data, type, event);
     }
   _e_keyrouter_intercept_hooks_walking--;
   if ((_e_keyrouter_intercept_hooks_walking == 0) && (_e_keyrouter_intercept_hooks_delete > 0))
     _e_keyrouter_intercept_hooks_clean();

   return res;
}

static void
_e_keyrouter_keygrab_status_print(FILE *log_fl, Eina_List *list)
{
   Eina_List *l;
   E_Keyrouter_Key_List_NodePtr kdata;
   int pid;
   char *cmd;

   EINA_LIST_FOREACH(list, l, kdata)
     {
        pid = e_keyrouter_util_get_pid(kdata->wc, kdata->surface);
        cmd = e_keyrouter_util_cmd_get_from_pid(pid);
        fprintf(log_fl, "                [surface: %p, client: %p, pid: %d(%s)]\n", kdata->surface, kdata->wc, pid, cmd ?: "Unknown");
        if(cmd) E_FREE(cmd);
        if (kdata->surface)
          {
             fprintf(log_fl, "                    -- Surface Information --\n");
             fprintf(log_fl, "                        = client: %p\n", wl_resource_get_client(kdata->surface));
             fprintf(log_fl, "                        = resource: %s(%d)\n", wl_resource_get_class(kdata->surface), wl_resource_get_id(kdata->surface));
          }
        else
          {
             fprintf(log_fl, "                    -- Client Information --\n");
             fprintf(log_fl, "                        = connected fd: %d\n", wl_client_get_fd(kdata->wc));
          }
     }
}

static void
_e_keyrouter_info_print(void *data, const char *log_path)
{
   char *keyname;
   int  i;
   FILE *log_fl;

   log_fl = fopen(log_path, "a");
   if (!log_fl)
     {
        KLERR("failed: open file(%s)", log_path);
        return;
     }

   setvbuf(log_fl, NULL, _IOLBF, 512);

   fprintf(log_fl, "\n===== Keyrouter Information =====\n");
   fprintf(log_fl, "    ----- Grabbable Keys -----\n");
   for (i = 8; i <= krt->max_tizen_hwkeys; i++)
     {
        if (!krt->HardKeys[i].keycode) continue;

        keyname = e_keyrouter_util_keyname_get_from_keycode(i);

        fprintf(log_fl, "         Key [%3d], Keyname: %s\n", i, keyname);

        free(keyname);
        keyname = NULL;
     }
   fprintf(log_fl, "    ----- End -----\n\n");

   fclose(log_fl);
   log_fl = NULL;
}

static void
_e_keyrouter_keygrab_print(void *data, const char *log_path)
{
   Eina_List *l;
   E_Keyrouter_Key_List_NodePtr kdata;
   E_Client *ec_focus;
   struct wl_resource *surface_focus;
   struct wl_client *wc_focus;
   int pid_focus, pid, i;
   char *cmd_focus, *cmd, *keyname;
   FILE *log_fl;

   (void) data;

   log_fl = fopen(log_path, "a");
   if (!log_fl)
     {
        KLERR("failed: open file(%s)", log_path);
        return;
     }

   setvbuf(log_fl, NULL, _IOLBF, 512);

   fprintf(log_fl, "\n===== Keygrab Status =====\n");

   ec_focus = e_client_focused_get();
   fprintf(log_fl, "    ----- Focus Window Info -----\n");
   if (ec_focus)
     {
        surface_focus = e_keyrouter_util_get_surface_from_eclient(ec_focus);
        if (surface_focus)
          {
             wc_focus = wl_resource_get_client(surface_focus);
             pid_focus = e_keyrouter_util_get_pid(NULL, surface_focus);
          }
        else
          {
             wc_focus = NULL;
             if (e_object_is_del(E_OBJECT(ec_focus))) pid_focus = 0;
             else pid_focus = ec_focus->netwm.pid;
          }
        cmd_focus = e_keyrouter_util_cmd_get_from_pid(pid_focus);

        fprintf(log_fl, "        Focus Client: E_Client: %p\n", ec_focus);
        fprintf(log_fl, "                      Surface: %p, Client: %p\n", surface_focus, wc_focus);
        fprintf(log_fl, "                      pid: %d, cmd: %s\n", pid_focus, cmd_focus ?: "Unknown");
        if(cmd_focus) E_FREE(cmd_focus);
     }
   else
     {
        fprintf(log_fl, "        No Focus Client\n");
     }
   fprintf(log_fl, "    ----- End -----\n\n");

   fprintf(log_fl, "    ----- Grabbed keys Info -----\n\n");
   for (i = 8; i <= krt->max_tizen_hwkeys; i++)
     {
        if (!krt->HardKeys[i].keycode) continue;
        if (!krt->HardKeys[i].excl_ptr &&
            !krt->HardKeys[i].or_excl_ptr &&
            !krt->HardKeys[i].top_ptr &&
            !krt->HardKeys[i].shared_ptr)
          continue;

        keyname = e_keyrouter_util_keyname_get_from_keycode(i);

        fprintf(log_fl, "        [ Keycode: %d, Keyname: %s ]\n", i, keyname);

        free(keyname);
        keyname = NULL;

        if (krt->HardKeys[i].excl_ptr)
          {
             fprintf(log_fl, "            == Exclusive Grab ==\n");
             EINA_LIST_FOREACH(krt->HardKeys[i].excl_ptr, l, kdata)
               {
                  pid = e_keyrouter_util_get_pid(kdata->wc, kdata->surface);
                  cmd = e_keyrouter_util_cmd_get_from_pid(pid);
                  fprintf(log_fl, "                [surface: %p, client: %p, pid: %d(%s)]\n", kdata->surface, kdata->wc, pid, cmd ?: "Unknown");
                  if(cmd) E_FREE(cmd);
                  if (kdata->surface)
                    {
                       fprintf(log_fl, "                    -- Surface Information --\n");
                       fprintf(log_fl, "                        = wl_client: %p\n", wl_resource_get_client(kdata->surface));
                       fprintf(log_fl, "                        = resource: %s(%d)\n", wl_resource_get_class(kdata->surface), wl_resource_get_id(kdata->surface));
                    }
                  else
                    {
                       fprintf(log_fl, "                    -- Client Information --\n");
                       fprintf(log_fl, "                        = connected fd: %d\n", wl_client_get_fd(kdata->wc));
                    }
               }
            }

        if (krt->HardKeys[i].or_excl_ptr)
          {
             fprintf(log_fl, "            == Overidable Exclusive Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].or_excl_ptr);
          }

        if (krt->HardKeys[i].top_ptr)
          {
             fprintf(log_fl, "            == Top Position Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].top_ptr);
          }

        if (krt->HardKeys[i].shared_ptr)
          {
             fprintf(log_fl, "            == Shared Grab ==\n");
             _e_keyrouter_keygrab_status_print(log_fl, krt->HardKeys[i].shared_ptr);
          }

        fprintf(log_fl, "\n");
     }

   fprintf(log_fl, "    ----- End -----\n\n");

   fclose(log_fl);
   log_fl = NULL;
}

static Eina_Bool
_e_keyrouter_cb_key_down(void *data, int type, void *event)
{
   Ecore_Event_Key *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   ev = (Ecore_Event_Key *)event;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_key_down:KEY_PRESS(%d), ev->keycode);
   TRACE_INPUT_END();

   res = e_keyrouter_event_process(event, type);
   
   return res;
}

static Eina_Bool
_e_keyrouter_cb_key_up(void *data, int type, void *event)
{
   Ecore_Event_Key *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   ev = (Ecore_Event_Key *)event;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_key_down:KEY_RELEASE(%d), ev->keycode);
   TRACE_INPUT_END();

   res = e_keyrouter_event_process(event, type);
   
   return res;
}

static Eina_Bool
_e_keyrouter_client_cb_stack(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) data;
   (void) type;
   (void) event;
   (void) ev;
   (void) ec;

   //KLDBG("ec: %p, visibile: %d, focused: %d, take_focus: %d, want_focus: %d, bordername: %s, input_only: %d",
   //        ec, ec->visible, ec->focused, ec->take_focus, ec->want_focus, ec->bordername, ec->input_only);

   krt->isWindowStackChanged = EINA_TRUE;

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_keyrouter_client_cb_remove(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;

   (void) data;
   (void) type;
   (void) ev;
   (void) ec;

   /* FIXME: Remove this callback or do something others.
    *             It was moved to _e_keyrouter_wl_surface_cb_destroy() where it had here.
    */

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_keyrouter_init_handlers(void)
{
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_STACK, _e_keyrouter_client_cb_stack, NULL);
   E_LIST_HANDLER_APPEND(krt->handlers, E_EVENT_CLIENT_REMOVE, _e_keyrouter_client_cb_remove, NULL);
   E_LIST_HANDLER_APPEND(krt->handlers, ECORE_EVENT_KEY_DOWN, _e_keyrouter_cb_key_down, NULL);
   E_LIST_HANDLER_APPEND(krt->handlers, ECORE_EVENT_KEY_UP, _e_keyrouter_cb_key_up, NULL);

   e_info_server_hook_set("keyrouter", _e_keyrouter_info_print, NULL);
   e_info_server_hook_set("keygrab", _e_keyrouter_keygrab_print, NULL);
}

static void
_e_keyrouter_deinit_handlers(void)
{
   Ecore_Event_Handler *h = NULL;

   if (!krt ||  !krt->handlers) return;

   EINA_LIST_FREE(krt->handlers, h)
     ecore_event_handler_del(h);

   e_info_server_hook_set("keyrouter", NULL, NULL);
   e_info_server_hook_set("keygrab", NULL, NULL);
}

static Eina_Bool
_e_keyrouter_query_tizen_key_table(void)
{
   E_Keyrouter_Conf_Edd *kconf = krt->conf->conf;
   Eina_List *l;
   E_Keyrouter_Tizen_HWKey *data;
   int res;
   struct xkb_rule_names names={0,};

   /* TODO: Make struct in HardKeys to pointer.
                  If a key is defined, allocate memory to pointer,
                  that makes to save unnecessary memory */
   krt->HardKeys = E_NEW(E_Keyrouter_Grabbed_Key, kconf->max_keycode + 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(krt->HardKeys, EINA_FALSE);

   krt->numTizenHWKeys = kconf->num_keycode;
   krt->max_tizen_hwkeys = kconf->max_keycode;

   EINA_LIST_FOREACH(kconf->KeyList, l, data)
     {
        if (!data) continue;

        if (0 > data->keycode || krt->max_tizen_hwkeys < data->keycode)
          {
             KLWRN("Given keycode(%d) is invalid. It must be bigger than zero, smaller than the maximum value(%d) or equal to it.", data->keycode, kconf->max_keycode);
             continue;
          }

        KLINF("keycode: %d, name: %s, no_priv: %d, repeat: %d\n", data->keycode, data->name, data->no_privcheck, data->repeat);

        krt->HardKeys[data->keycode].keycode = data->keycode;
        krt->HardKeys[data->keycode].keyname = (char *)eina_stringshare_add(data->name);
        krt->HardKeys[data->keycode].no_privcheck = data->no_privcheck ? EINA_TRUE : EINA_FALSE;
        krt->HardKeys[data->keycode].repeat = data->repeat ? EINA_TRUE : EINA_FALSE;

        if (e_comp_wl_input_keymap_cache_file_use_get() == EINA_FALSE)
          {
             if (krt->HardKeys[data->keycode].repeat == EINA_FALSE)
               {
                  res = xkb_keymap_key_set_repeats(e_comp_wl->xkb.keymap, data->keycode, 0);
                  if (!res)
                    {
                       KLWRN("Failed to set repeat key(%d), value(%d)", data->keycode, 0);
                    }
               }
          }
     }

   if (e_comp_wl_input_keymap_cache_file_use_get() == EINA_FALSE)
     {
        KLINF("Server create a new cache file: %s", e_comp_wl_input_keymap_path_get(names));
        res = unlink(e_comp_wl_input_keymap_path_get(names));

        e_comp_wl_input_keymap_set(NULL, NULL, NULL, NULL, NULL, xkb_context_ref(e_comp_wl->xkb.context), xkb_keymap_ref(e_comp_wl->xkb.keymap));
     }
   else
     KLINF("Currently cache file is exist. Do not change it.");

   return EINA_TRUE;
}

static void *
_e_keyrouter_keygrab_list_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(krt, NULL);
   return krt->HardKeys;
}

static int
_e_keyrouter_max_keycode_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(krt, 0);
   return krt->max_tizen_hwkeys;
}

E_API int
e_keyrouter_init(void)
{
   E_Keyrouter_Config_Data *kconfig = NULL;
   Eina_Bool res = EINA_FALSE;

   TRACE_INPUT_BEGIN(e_keyrouter_init);

   EINA_SAFETY_ON_NULL_GOTO(e_comp, err);

   _keyrouter_log_dom = eina_log_domain_register("e-keyrouter", EINA_COLOR_RED);
   EINA_SAFETY_ON_FALSE_GOTO(_keyrouter_log_dom >= 0, err);
   eina_log_domain_level_set("e-keyrouter", EINA_LOG_LEVEL_INFO);

   krt = E_NEW(E_Keyrouter, 1);
   EINA_SAFETY_ON_NULL_GOTO(krt, err);

   kconfig = E_NEW(E_Keyrouter_Config_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(kconfig, err);

   e_keyrouter_conf_init(kconfig);
   EINA_SAFETY_ON_NULL_GOTO(kconfig->conf, err);
   krt->conf = kconfig;
   krt->pictureoff_disabled = !!kconfig->conf->pictureoff_disabled;

   res = e_keyrouter_wl_init();
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   /* Get keyname and keycode pair from Tizen Key Layout file */
   res = _e_keyrouter_query_tizen_key_table();
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   //ecore handler add for power callback registration
//   if (!krt->pictureoff_disabled)
//     ecore_idle_enterer_add(_e_keyrouter_cb_idler, NULL);
   _e_keyrouter_init_handlers();

   e_keyrouter.keygrab_list_get = _e_keyrouter_keygrab_list_get;
   e_keyrouter.max_keycode_get = _e_keyrouter_max_keycode_get;

   TRACE_INPUT_END();
   return EINA_TRUE;

err:
   if (kconfig)
     {
        e_keyrouter_conf_deinit(kconfig);
        E_FREE(kconfig);
     }
   _e_keyrouter_deinit_handlers();
   e_keyrouter_wl_shutdown();
   eina_log_domain_unregister(_keyrouter_log_dom);
   _keyrouter_log_dom = -1;
   if (krt) E_FREE(krt);

   TRACE_INPUT_END();
   return EINA_FALSE;
}

E_API int
e_keyrouter_shutdown(void)
{
   int i;
   int *keycode_data;
   E_Keyrouter_Config_Data *kconfig = krt->conf;

   e_keyrouter_conf_deinit(kconfig);
   E_FREE(kconfig);

   _e_keyrouter_deinit_handlers();

   for (i = 0; i <= krt->max_tizen_hwkeys; i++)
     {
        if (krt->HardKeys[i].keyname)
          eina_stringshare_del(krt->HardKeys[i].keyname);
     }
   E_FREE(krt->HardKeys);

   EINA_LIST_FREE(krt->ignore_list, keycode_data)
     E_FREE(keycode_data);

   e_keyrouter_wl_shutdown();

   E_FREE(krt);
   /* TODO: free allocated memory */

   eina_log_domain_unregister(_keyrouter_log_dom);

   return EINA_TRUE;
}
