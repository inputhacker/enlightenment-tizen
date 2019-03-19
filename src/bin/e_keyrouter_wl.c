#include "e_keyrouter_private.h"

static void
_e_keyrouter_event_surface_send(struct wl_resource *surface, int key, int mode)
{
   Eina_List *l;
   struct wl_resource *res_data;
   struct wl_client *wc;

   EINA_SAFETY_ON_NULL_RETURN(krt);
   EINA_SAFETY_ON_NULL_RETURN(surface);

   wc = wl_resource_get_client(surface);
   EINA_SAFETY_ON_NULL_RETURN(wc);

   EINA_LIST_FOREACH(krt->resources, l, res_data)
     {
        if (wl_resource_get_client(res_data) != wc) continue;
        if (wl_resource_get_version(res_data) < 2) continue;

        tizen_keyrouter_send_event_surface(res_data, surface, key, mode);
     }
}

static void
_e_keyrouter_wl_key_send(Ecore_Event_Key *ev, enum wl_keyboard_key_state state, Eina_List *key_list, Eina_Bool focused, struct wl_client *client, struct wl_resource *surface)
{
   struct wl_resource *res;
   Eina_List *l;
   uint32_t serial, keycode;
   struct wl_client *wc;
   E_Comp_Config *comp_conf = NULL;

   keycode = (ev->keycode - 8);

   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   comp_conf = e_comp_config_get();

   if (surface && !focused)
     {
        _e_keyrouter_event_surface_send(surface, ev->keycode, TIZEN_KEYROUTER_MODE_NONE);
     }

   EINA_LIST_FOREACH(key_list, l, res)
     {
        wc = wl_resource_get_client(res);
        if (!focused && wc != client) continue;
        TRACE_INPUT_BEGIN(_e_comp_wl_key_send);
        e_comp_wl_send_event_device(client, ev->timestamp, ev->dev, serial);

        if (comp_conf && comp_conf->input_log_enable)
          INF("[Server] Routed Key %s (time: %d)\n", (state ? "Down" : "Up"), ev->timestamp);

        wl_keyboard_send_key(res, serial, ev->timestamp,
                             keycode, state);
        TRACE_INPUT_END();
     }
}

Eina_Bool
e_keyrouter_wl_key_send(Ecore_Event_Key *ev, Eina_Bool pressed, struct wl_client *client, struct wl_resource *surface, Eina_Bool focused)
{
   E_Client *ec = NULL;
   struct wl_client *wc = NULL;
   uint32_t keycode;
   enum wl_keyboard_key_state state;

   if ((e_comp->comp_type != E_PIXMAP_TYPE_WL) || (ev->window != e_comp->ee_win))
     {
        return EINA_FALSE;
     }

   keycode = (ev->keycode - 8);
   if (!(e_comp_wl = e_comp->wl_comp_data))
     {
        return EINA_FALSE;
     }

#ifndef E_RELEASE_BUILD
   if ((ev->modifiers & ECORE_EVENT_MODIFIER_CTRL) &&
       ((ev->modifiers & ECORE_EVENT_MODIFIER_ALT) ||
       (ev->modifiers & ECORE_EVENT_MODIFIER_ALTGR)) &&
       eina_streq(ev->key, "BackSpace"))
     {
        exit(0);
     }
#endif

   if (pressed) state = WL_KEYBOARD_KEY_STATE_PRESSED;
   else state = WL_KEYBOARD_KEY_STATE_RELEASED;

   if (!focused)
     {
        _e_keyrouter_wl_key_send(ev, state, e_comp_wl->kbd.resources, EINA_FALSE, client, surface);
        return EINA_FALSE;
     }

   if ((!e_client_action_get()) && (!e_comp->input_key_grabs))
     {
        ec = e_client_focused_get();
        if (ec && ec->comp_data && ec->comp_data->surface)
          {
             if (e_comp_wl->kbd.focused)
               {
                  wc = wl_resource_get_client(ec->comp_data->surface);
                  _e_keyrouter_wl_key_send(ev, state, e_comp_wl->kbd.focused, EINA_TRUE, wc, surface);
               }

             /* update modifier state */
             e_comp_wl_input_keyboard_state_update(keycode, pressed);
          }
     }
   return !!ec;
}

void
e_keyrouter_keycancel_send(struct wl_client *client, struct wl_resource *surface, unsigned int key)
{
   Eina_List *l;
   struct wl_resource *resource = NULL;
   struct wl_client *wc = NULL;
   E_Keyrouter_Key_List_NodePtr data;

   if (surface) wc = wl_resource_get_client(surface);
   else wc = client;

   EINA_SAFETY_ON_NULL_RETURN(wc);

   EINA_LIST_FOREACH(krt->HardKeys[key].press_ptr, l, data)
     {
        if (surface)
          {
             if (surface == data->surface)
               {
                  EINA_LIST_FOREACH(krt->resources, l, resource)
                    {
                       if (wl_resource_get_client(resource) != wc) continue;

                       tizen_keyrouter_send_key_cancel(resource, key-8);
                    }
               }
          }
        else if (client == data->wc)
          {
             EINA_LIST_FOREACH(krt->resources, l, resource)
               {
                  if (wl_resource_get_client(resource) != wc) continue;

                  tizen_keyrouter_send_key_cancel(resource, key-8);
               }
          }
     }
}

static int
_e_keyrouter_wl_array_length(const struct wl_array *array)
{
   int *data = NULL;
   int count = 0;

   wl_array_for_each(data, array)
     {
        count++;
     }

   return count;
}

/* tizen_keyrouter_set_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key, uint32_t mode)
{
   int res = 0;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_set);

   res = e_keyrouter_keygrab_set(client, surface, key, mode);

   TRACE_INPUT_END();

   if (res == TIZEN_KEYROUTER_ERROR_NONE)
     {
        if (mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE)
          {
             KLINF("Success to %d key %s grab request (wl_client: %p, wl_surface: %p, pid: %d)", key, e_keyrouter_mode_to_string(mode),
                client, surface, e_keyrouter_util_get_pid(client, surface));
          }
        else
          {
             KLDBG("Success to %d key %s grab request (wl_client: %p, wl_surface: %p, pid: %d)", key, e_keyrouter_mode_to_string(mode),
                client, surface, e_keyrouter_util_get_pid(client, surface));
          }
     }
   else
     KLINF("Failed to %d key %s grab request (wl_client: %p, wl_surface: %p, pid: %d): res: %d", key, e_keyrouter_mode_to_string(mode),
        client, surface, e_keyrouter_util_get_pid(client, surface), res);
   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, res);
}

/* tizen_keyrouter unset_keygrab request handler */
static void
_e_keyrouter_cb_keygrab_unset(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
   int res = 0;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_unset);

   res = e_keyrouter_keygrab_unset(client, surface, key);

   TRACE_INPUT_END();

   if (res == TIZEN_KEYROUTER_ERROR_NONE)
     KLDBG("Success to %d key ungrab request (wl_client: %p, wl_surface: %p, pid: %d)", key, client, surface,
           e_keyrouter_util_get_pid(client, surface));
   else
     KLINF("Failed to %d key ungrab request (wl_client: %p, wl_surface: %p, pid: %d): res: %d", key, client, surface,
           e_keyrouter_util_get_pid(client, surface), res);
   tizen_keyrouter_send_keygrab_notify(resource, surface, key, TIZEN_KEYROUTER_MODE_NONE, res);
}

/* tizen_keyrouter get_keygrab_status request handler */
static void
_e_keyrouter_cb_get_keygrab_status(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t key)
{
   (void) client;
   (void) resource;
   (void) surface;
   (void) key;
   int mode = TIZEN_KEYROUTER_MODE_NONE;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_get_keygrab_status);
   mode = e_keyrouter_find_key_in_list(surface, client, key);

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify(resource, surface, key, mode, TIZEN_KEYROUTER_ERROR_NONE);
}

static void
_e_keyrouter_cb_keygrab_set_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *grab_list)
{
   E_Keyrouter_Grab_Request *grab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;
   int array_len = 0;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_set_list);

   array_len = _e_keyrouter_wl_array_length(grab_list);

   if (0 != (array_len % 3))
     {
        /* FIX ME: Which way is effectively to notify invalid pair to client */
        KLWRN("Invalid keycode and grab mode pair. Check arguments in a list");
        TRACE_INPUT_END();
        tizen_keyrouter_send_keygrab_notify_list(resource, surface, NULL);
        return;
     }

   wl_array_for_each(grab_request, grab_list)
     {
        res = e_keyrouter_keygrab_set(client, surface, grab_request->key, grab_request->mode);
        grab_request->err = res;
        if (res == TIZEN_KEYROUTER_ERROR_NONE)
          KLDBG("Success to %d key %s grab using list(wl_client: %p, wl_surface: %p, pid: %d)",
                grab_request->key, e_keyrouter_mode_to_string(grab_request->mode),
                client, surface, e_keyrouter_util_get_pid(client, surface));
        else
          KLINF("Failed to %d key %s grab using list(wl_client: %p, wl_surface: %p, pid: %d): res: %d",
                grab_request->key, e_keyrouter_mode_to_string(grab_request->mode),
                client, surface, e_keyrouter_util_get_pid(client, surface), grab_request->err);
     }


   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify_list(resource, surface, grab_list);
}

static void
_e_keyrouter_cb_keygrab_unset_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, struct wl_array *ungrab_list)
{
   E_Keyrouter_Ungrab_Request *ungrab_request = NULL;
   int res = TIZEN_KEYROUTER_ERROR_NONE;
   int array_len = 0;

   TRACE_INPUT_BEGIN(_e_keyrouter_cb_keygrab_unset_list);

   array_len = _e_keyrouter_wl_array_length(ungrab_list);

   if (0 != (array_len % 2))
     {
        /* FIX ME: Which way is effectively to notify invalid pair to client */
        KLWRN("Invalid keycode and error pair. Check arguments in a list");
        TRACE_INPUT_END();
        tizen_keyrouter_send_keygrab_notify_list(resource, surface, ungrab_list);
        return;
     }

   wl_array_for_each(ungrab_request, ungrab_list)
     {
        res = e_keyrouter_keygrab_unset(client, surface, ungrab_request->key);
        ungrab_request->err = res;
        if (res == TIZEN_KEYROUTER_ERROR_NONE)
          KLDBG("Success to ungrab using list: %d key (wl_client: %p, wl_surface: %p, pid: %d)",
                ungrab_request->key, client, surface, e_keyrouter_util_get_pid(client, surface));
        else
          KLINF("Failed to ungrab using list: %d key (wl_client: %p, wl_surface: %p, pid: %d): res: %d",
                ungrab_request->key, client, surface, e_keyrouter_util_get_pid(client, surface), ungrab_request->err);
     }

   TRACE_INPUT_END();
   tizen_keyrouter_send_keygrab_notify_list(resource, surface, ungrab_list);
}

static void
_e_keyrouter_cb_keygrab_get_list(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface)
{
   (void) client;

   tizen_keyrouter_send_getgrab_notify_list(resource, surface, NULL);
}

static void
_e_keyrouter_cb_set_register_none_key(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, uint32_t data)
{
   (void) client;
   (void) data;

   tizen_keyrouter_send_set_register_none_key_notify(resource, NULL, 0);
}

static void
_e_keyrouter_cb_get_keyregister_status(struct wl_client *client, struct wl_resource *resource, uint32_t key)
{
   (void) client;
   (void) key;

   tizen_keyrouter_send_keyregister_notify(resource, (int)EINA_FALSE);
}

static void
_e_keyrouter_cb_set_input_config(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *surface EINA_UNUSED, uint32_t config_mode EINA_UNUSED, uint32_t value EINA_UNUSED)
{
   tizen_keyrouter_send_set_input_config_notify(resource, 0);
}

static void
_e_keyrouter_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_keyrouter_interface _e_keyrouter_implementation = {
   _e_keyrouter_cb_keygrab_set,
   _e_keyrouter_cb_keygrab_unset,
   _e_keyrouter_cb_get_keygrab_status,
   _e_keyrouter_cb_keygrab_set_list,
   _e_keyrouter_cb_keygrab_unset_list,
   _e_keyrouter_cb_keygrab_get_list,
   _e_keyrouter_cb_set_register_none_key,
   _e_keyrouter_cb_get_keyregister_status,
   _e_keyrouter_cb_set_input_config,
   _e_keyrouter_cb_destroy,
};

static void
_e_keyrouter_cb_unbind(struct wl_resource *resource)
{
   krt->resources = eina_list_remove(krt->resources, resource);
}

/* tizen_keyrouter global object bind function */
static void
_e_keyrouter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_KeyrouterPtr krt_instance = data;
   struct wl_resource *resource;

   resource = wl_resource_create(client, &tizen_keyrouter_interface, version, id);

   KLDBG("wl_resource_create(...,&tizen_keyrouter_interface,...)");

   if (!resource)
     {
        KLERR("Failed to create resource ! (version :%d, id:%d)", version, id);
        wl_client_post_no_memory(client);
	 return;
     }

   krt->resources = eina_list_append(krt->resources, resource);

   wl_resource_set_implementation(resource, &_e_keyrouter_implementation, krt_instance, _e_keyrouter_cb_unbind);
}

static void
_e_keyrouter_wl_client_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_client *client = data;

   KLDBG("Listener(%p) called: wl_client: %p is died", l, client);
   e_keyrouter_remove_client_from_list(NULL, client);

   wl_list_remove(&l->link);
   E_FREE(l);

   krt->grab_client_list = eina_list_remove(krt->grab_client_list, client);
}

static void
_e_keyrouter_wl_surface_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_resource *surface = (struct wl_resource *)data;

   KLDBG("Listener(%p) called: surface: %p is died", l, surface);
   e_keyrouter_remove_client_from_list(surface, NULL);

   wl_list_remove(&l->link);
   E_FREE(l);

   krt->grab_surface_list = eina_list_remove(krt->grab_surface_list, surface);
}

int
e_keyrouter_wl_add_client_destroy_listener(struct wl_client *client)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_client *wc_data;

   if (!client) return TIZEN_KEYROUTER_ERROR_NONE;

   EINA_LIST_FOREACH(krt->grab_client_list, l, wc_data)
     {
        if (wc_data)
          {
             if (wc_data == client)
               {
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLERR("Failed to allocate memory for wl_client destroy listener !");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);
   krt->grab_client_list = eina_list_append(krt->grab_client_list, client);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* Function for registering wl_surface destroy listener */
int
e_keyrouter_wl_add_surface_destroy_listener(struct wl_resource *surface)
{
   struct wl_listener *destroy_listener = NULL;
   Eina_List *l;
   struct wl_resource *surface_data;

   if (!surface) return TIZEN_KEYROUTER_ERROR_NONE;

   EINA_LIST_FOREACH(krt->grab_surface_list, l, surface_data)
     {
        if (surface_data)
          {
             if (surface_data == surface)
               {
                  return TIZEN_KEYROUTER_ERROR_NONE;
               }
          }
     }

   destroy_listener = E_NEW(struct wl_listener, 1);

   if (!destroy_listener)
     {
        KLERR("Failed to allocate memory for wl_surface destroy listener !");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   destroy_listener->notify = _e_keyrouter_wl_surface_cb_destroy;
   wl_resource_add_destroy_listener(surface, destroy_listener);
   krt->grab_surface_list = eina_list_append(krt->grab_surface_list, surface);

   return TIZEN_KEYROUTER_ERROR_NONE;
}

#ifdef HAVE_CYNARA
static void
_e_keyrouter_wl_util_cynara_log(const char *func_name, int err)
{
#define CYNARA_BUFSIZE 128
   char buf[CYNARA_BUFSIZE] = "\0";
   int ret;

   ret = cynara_strerror(err, buf, CYNARA_BUFSIZE);
   if (ret != CYNARA_API_SUCCESS)
     {
        KLWRN("Failed to cynara_strerror: %d (error log about %s: %d)", ret, func_name, err);
        return;
     }
   KLWRN("%s is failed: %s", func_name, buf);
}

Eina_Bool
e_keyrouter_wl_util_do_privilege_check(struct wl_client *client, uint32_t mode, uint32_t keycode)
{
   int ret, retry_cnt=0, len=0;
   char *clientSmack=NULL, *client_session=NULL, uid2[16]={0, };
   Eina_Bool res = EINA_FALSE;
   Eina_List *l;
   struct wl_client *wc_data;
   static Eina_Bool retried = EINA_FALSE;
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;

   /* Top position grab is always allowed. This mode do not need privilege.*/
   if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
     return EINA_TRUE;

   if (krt->HardKeys[keycode].no_privcheck == EINA_TRUE &&
       mode == TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE)
     return EINA_TRUE;

   if (!client) return EINA_FALSE;

   /* If initialize cynara is failed, allow keygrabs regardless of the previlege permition. */
   if (krt->p_cynara == NULL)
     {
        if (retried == EINA_FALSE)
          {
             retried = EINA_TRUE;
             for(retry_cnt = 0; retry_cnt < 5; retry_cnt++)
               {
                  KLDBG("Retry cynara initialize: %d", retry_cnt+1);
                  ret = cynara_initialize(&krt->p_cynara, NULL);
                  if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret))
                    {
                      _e_keyrouter_wl_util_cynara_log("cynara_initialize", ret);
                       krt->p_cynara = NULL;
                    }
                  else
                    {
                       KLDBG("Success cynara initialize to try %d times", retry_cnt+1);
                       break;
                    }
               }
          }
        return EINA_TRUE;
     }

   EINA_LIST_FOREACH(krt->grab_client_list, l, wc_data)
     {
        if (wc_data == client)
          {
             res = EINA_TRUE;
             goto finish;
          }
     }

   wl_client_get_credentials(client, &pid, &uid, &gid);

   len = smack_new_label_from_process((int)pid, &clientSmack);
   if (len <= 0) goto finish;

   snprintf(uid2, 15, "%d", (int)uid);
   client_session = cynara_session_from_pid(pid);

   ret = cynara_check(krt->p_cynara, clientSmack, client_session, uid2, "http://tizen.org/privilege/keygrab");
   if (CYNARA_API_ACCESS_ALLOWED == ret)
     {
        res = EINA_TRUE;
     }
   else
     {
        KLINF("Fail to check cynara,  error : %d (pid : %d)", ret, pid);
     }
finish:
   if (client_session) E_FREE(client_session);
   if (clientSmack) E_FREE(clientSmack);

   return res;
}
#endif

Eina_Bool
e_keyrouter_wl_init(void)
{
   int ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(krt, EINA_FALSE);

   krt->global = wl_global_create(e_comp_wl->wl.disp, &tizen_keyrouter_interface, 2, krt, _e_keyrouter_cb_bind);
   EINA_SAFETY_ON_NULL_RETURN_VAL(krt->global, EINA_FALSE);

#ifdef HAVE_CYNARA
   ret = cynara_initialize(&krt->p_cynara, NULL);
   if (EINA_UNLIKELY(CYNARA_API_SUCCESS != ret))
     {
        _e_keyrouter_wl_util_cynara_log("cynara_initialize", ret);
        krt->p_cynara = NULL;
     }
#endif

   return EINA_TRUE;
}

void
e_keyrouter_wl_shutdown(void)
{
   Eina_List *l, *l_next;
   struct wl_resource *resource;
   struct wl_client *client;
   struct wl_listener *destroy_listener;

   EINA_SAFETY_ON_NULL_RETURN(krt);

   EINA_LIST_FOREACH_SAFE(krt->grab_client_list, l, l_next, client)
     {
        destroy_listener = wl_client_get_destroy_listener(client, _e_keyrouter_wl_client_cb_destroy);
        if (destroy_listener)
          {
             wl_list_remove(&destroy_listener->link);
             E_FREE(destroy_listener);
          }
        krt->grab_client_list = eina_list_remove(krt->grab_client_list, client);
     }
   EINA_LIST_FOREACH_SAFE(krt->grab_surface_list, l, l_next, resource)
     {
        destroy_listener = wl_resource_get_destroy_listener(resource, _e_keyrouter_wl_surface_cb_destroy);
        if (destroy_listener)
          {
             wl_list_remove(&destroy_listener->link);
             E_FREE(destroy_listener);
          }
        krt->grab_surface_list = eina_list_remove(krt->grab_surface_list, client);
     }

   EINA_LIST_FREE(krt->resources, resource)
     wl_resource_destroy(resource);
 
   if (krt->global) wl_global_destroy(krt->global);
   
#ifdef HAVE_CYNARA
   if (krt->p_cynara) cynara_finish(krt->p_cynara);
#endif
}
