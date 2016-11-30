#define EXECUTIVE_MODE_ENABLED
#include "e.h"
#include <sys/mman.h>
#include <Ecore_Drm.h>

E_API int E_EVENT_TEXT_INPUT_PANEL_VISIBILITY_CHANGE = -1;
E_API int E_EVENT_SEAT_BOUND = 1;
static Eina_Bool dont_set_ecore_drm_keymap = EINA_FALSE;
static Eina_Bool dont_use_xkb_cache = EINA_FALSE;
static Eina_Bool use_cache_keymap = EINA_FALSE;

static void
_e_comp_wl_input_update_seat_caps(E_Comp_Wl_Seat *seat)
{
   Eina_List *l;
   struct wl_resource *res;
   enum wl_seat_capability caps = 0;

   EINA_SAFETY_ON_NULL_RETURN(seat);

   if (seat->ptr.enabled)
     caps |= WL_SEAT_CAPABILITY_POINTER;
   if (seat->kbd.enabled)
     caps |= WL_SEAT_CAPABILITY_KEYBOARD;
   if (seat->touch.enabled)
     caps |= WL_SEAT_CAPABILITY_TOUCH;

   EINA_LIST_FOREACH(seat->resources, l, res)
        wl_seat_send_capabilities(res, caps);
}

static void
_e_comp_wl_input_pointer_map(struct wl_resource *resource)
{
   E_Client *ec;
   E_Pointer *ptr;
   struct wl_client *wc;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->seat) return;
   if (!ec->seat->ptr.enabled) return;

   if (!ec->seat->ptr.ec || !ec->seat->ptr.ec->comp_data || !ec->seat->ptr.ec->comp_data->surface) return;
   wc = wl_resource_get_client(resource);
   if (wc != wl_resource_get_client(ec->seat->ptr.ec->comp_data->surface)) return;

   if ((ptr = ec->seat->pointer))
     e_pointer_object_set(ptr, ec->frame, ptr->hot.x, ptr->hot.y);
}

static void
_e_comp_wl_input_pointer_configure(struct wl_resource *resource,
                                   Evas_Coord x, Evas_Coord y,
                                   Evas_Coord w, Evas_Coord h)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_client_util_resize_without_frame(ec, w, h);
}

static void
_e_comp_wl_input_cb_resource_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_input_pointer_cb_cursor_set(struct wl_client *client, struct wl_resource *resource, uint32_t serial EINA_UNUSED, struct wl_resource *surface_resource, int32_t x, int32_t y)
{
   E_Client *ec;
   Eina_Bool got_mouse = EINA_FALSE;
   E_Comp_Wl_Seat *seat;

   E_CLIENT_FOREACH(ec)
     {
       if (e_object_is_del(E_OBJECT(ec))) continue;
       if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) continue;
       if (!ec->comp_data->surface) continue;
       if (client != wl_resource_get_client(ec->comp_data->surface)) continue;
       if (ec->mouse.in)
         {
            got_mouse = EINA_TRUE;
            break;
         }
     }
   if (!got_mouse) return;

   seat = wl_resource_get_user_data(resource);
   if (!seat) return;
   if (!surface_resource)
     {
        e_pointer_object_set(seat->pointer, NULL, x, y);
        return;
     }
   ec = wl_resource_get_user_data(surface_resource);
   if (!ec->re_manage)
     {
        ec->re_manage = 1;
        ec->ignored = 0;

        ec->lock_focus_out = ec->layer_block = ec->visible = 1;
        if (!e_config->show_cursor)  ec->override = 1;
        ec->icccm.title = eina_stringshare_add("Cursor");
        e_client_window_role_set(ec, "wl_pointer-cursor");
        evas_object_pass_events_set(ec->frame, 1);
        e_client_focus_stack_set(eina_list_remove(e_client_focus_stack_get(), ec));
        EC_CHANGED(ec);

        /* Set fuctions to prevent unwanted handling by shell */
        ec->comp_data->shell.surface = surface_resource;
        ec->comp_data->shell.configure = _e_comp_wl_input_pointer_configure;
        ec->comp_data->shell.map = _e_comp_wl_input_pointer_map;

        evas_object_layer_set(ec->frame, E_LAYER_MAX - 1);
     }

   /* ignore cursor changes during resize/move I guess */
   if (e_client_action_get()) return;

   ec->seat = seat;
   if (!ec->seat->ptr.enabled) return;
   e_pointer_object_set(ec->seat->pointer, ec->frame, x, y);
}

static const struct wl_pointer_interface _e_pointer_interface =
{
   _e_comp_wl_input_pointer_cb_cursor_set,
   _e_comp_wl_input_cb_resource_destroy
};

static const struct wl_keyboard_interface _e_keyboard_interface =
{
   _e_comp_wl_input_cb_resource_destroy
};

static const struct wl_touch_interface _e_touch_interface =
{
   _e_comp_wl_input_cb_resource_destroy
};

static void
_e_comp_wl_input_cb_pointer_unbind(struct wl_resource *resource)
{
   E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

   seat->ptr.resources =
     eina_list_remove(seat->ptr.resources, resource);
}

static void
_e_comp_wl_input_cb_pointer_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wl_resource *res;
   E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

   /* try to create pointer resource */
   res = wl_resource_create(client, &wl_pointer_interface,
                            wl_resource_get_version(resource), id);

   if (!res)
     {
        ERR("Could not create pointer on seat %s: %m",
            seat->name);
        wl_client_post_no_memory(client);
        return;
     }

   seat->ptr.resources =
     eina_list_append(seat->ptr.resources, res);
   wl_resource_set_implementation(res, &_e_pointer_interface,
                                  seat,
                                 _e_comp_wl_input_cb_pointer_unbind);
}

static void
_e_comp_wl_input_cb_keyboard_unbind(struct wl_resource *resource)
{
   Eina_List *l, *ll;
   struct wl_resource *res;
   E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

   seat->kbd.resources =
     eina_list_remove(seat->kbd.resources, resource);
   EINA_LIST_FOREACH_SAFE(seat->kbd.focused, l, ll, res)
     if (res == resource)
       seat->kbd.focused =
         eina_list_remove_list(seat->kbd.focused, l);
}

void
e_comp_wl_input_keyboard_enter_send(E_Client *ec)
{
   struct wl_resource *res;
   Eina_List *l;
   uint32_t serial;

   if (!ec->comp_data->surface) return;
   if (!ec->seat) return;

   e_comp_wl_input_keyboard_modifiers_serialize(ec->seat);

   if (!ec->seat->kbd.focused) return;
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(ec->seat->kbd.focused, l, res)
     {
        wl_keyboard_send_enter(res, serial, ec->comp_data->surface,
                               &ec->seat->kbd.keys);
        wl_keyboard_send_modifiers(res, serial,
                                   ec->seat->kbd.mod_depressed,
                                   ec->seat->kbd.mod_latched,
                                   ec->seat->kbd.mod_locked,
                                   ec->seat->kbd.mod_group);
     }
}

static void
_e_comp_wl_input_cb_keyboard_get(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   E_Client *focused;
   struct wl_resource *res;
   E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

   /* try to create keyboard resource */
   res = wl_resource_create(client, &wl_keyboard_interface,
                            wl_resource_get_version(resource), id);
   if (!res)
     {
        ERR("Could not create keyboard on seat %s: %m",
            seat->name);
        wl_client_post_no_memory(client);
        return;
     }

   seat->kbd.resources =
     eina_list_append(seat->kbd.resources, res);
   wl_resource_set_implementation(res, &_e_keyboard_interface,
                                  seat,
                                  _e_comp_wl_input_cb_keyboard_unbind);
   ERR("_e_comp_wl_input_cb_keyboard_get client:%p, seat:%s, resources_num:%d", client, seat->name ? : "NULL", eina_list_count(seat->kbd.resources));

   /* send current repeat_info */
   if (wl_resource_get_version(res) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
     wl_keyboard_send_repeat_info(res, e_config->keyboard.repeat_rate, e_config->keyboard.repeat_delay);

   /* send current keymap */
   TRACE_INPUT_BEGIN(wl_keyboard_send_keymap);
   wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                           e_comp_wl->xkb.fd,
                           e_comp_wl->xkb.size);
   TRACE_INPUT_END();

   /* if the client owns the focused surface, we need to send an enter */
   focused = e_client_focused_get();
   if ((!focused) || (e_object_is_del(E_OBJECT(focused))) ||
       (!focused->comp_data) || (!focused->comp_data->surface)) return;

   if (client != wl_resource_get_client(focused->comp_data->surface)) return;
   seat->kbd.focused = eina_list_append(seat->kbd.focused, res);

   e_comp_wl_input_keyboard_enter_send(focused);
}

static void
_e_comp_wl_input_cb_touch_unbind(struct wl_resource *resource)
{
   E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

   seat->touch.resources =
     eina_list_remove(seat->touch.resources, resource);
}

static void
_e_comp_wl_input_cb_touch_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id EINA_UNUSED)
{
    struct wl_resource *res;
    E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;

    /* try to create pointer resource */
    res = wl_resource_create(client, &wl_touch_interface,
                             wl_resource_get_version(resource), id);
    if (!res)
      {
         ERR("Could not create touch on seat %s: %m",
             seat->name);
         wl_client_post_no_memory(client);
         return;
      }

    seat->touch.resources =
     eina_list_append(seat->touch.resources, res);
    wl_resource_set_implementation(res, &_e_touch_interface,
                                   seat,
                                  _e_comp_wl_input_cb_touch_unbind);
}

static const struct wl_seat_interface _e_seat_interface =
{
   _e_comp_wl_input_cb_pointer_get,
   _e_comp_wl_input_cb_keyboard_get,
   _e_comp_wl_input_cb_touch_get,
};

static void
_e_comp_wl_input_cb_unbind_seat(struct wl_resource *resource)
{
    E_Comp_Wl_Seat *seat;

   if (!(seat = wl_resource_get_user_data(resource))) return;
   seat->resources =
     eina_list_remove(seat->resources, resource);
}

static void
_e_seat_bound_event_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Seat_Bound *ev = event;
   e_object_unref(E_OBJECT(ev->client));
   e_object_unref(E_OBJECT(ev->seat));
   free(ev);
}

static void
_e_comp_wl_input_cb_bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   E_Comp_Wl_Seat *seat;
   E_Event_Seat_Bound *ev;

   ERR("_e_comp_wl_input_cb_bind_seat()");
   if (!(seat = data)) return;
   res = wl_resource_create(client, &wl_seat_interface, version, id);
   if (!res)
     {
        ERR("Could not create seat resource: %m");
        return;
     }

   /* store version of seat interface for reuse in updating capabilities */
   seat->version = version;
   seat->resources =
     eina_list_append(seat->resources, res);

   wl_resource_set_implementation(res, &_e_seat_interface,
                                  seat,
                                  _e_comp_wl_input_cb_unbind_seat);
   wl_resource_set_user_data(res, seat);

   _e_comp_wl_input_update_seat_caps(seat);
   if (seat->version >= WL_SEAT_NAME_SINCE_VERSION)
     wl_seat_send_name(res, seat->name);

   ev = E_NEW(E_Event_Seat_Bound, 1);
   ev->client = client;
   e_object_ref(E_OBJECT(client));
   ev->seat = seat;
   e_object_ref(E_OBJECT(seat));
   ecore_event_add(E_EVENT_SEAT_BOUND, ev, (Ecore_End_Cb)_e_seat_bound_event_free, NULL);
}

static void
_e_comp_wl_input_keymap_cache_create(const char *keymap_path, char *keymap_data)
{
   FILE *file = NULL;
   TRACE_INPUT_BEGIN(_e_comp_wl_input_keymap_cache_create);

   if ((EINA_FALSE == e_config->xkb.use_cache) && !dont_use_xkb_cache)
     {
        TRACE_INPUT_END();
        return;
     }

   if (keymap_path)
     {
        file = fopen(keymap_path, "w");
        EINA_SAFETY_ON_NULL_RETURN(file);

        if (fputs(keymap_data, file) < 0)
          {
             WRN("Failed  to write keymap file: %s\n", keymap_path);
             fclose(file);
             unlink(keymap_path);
          }
        else
          {
             INF("Success to make keymap file: %s\n", keymap_path);
             fclose(file);
          }
     }
   TRACE_INPUT_END();
}

static int
_e_comp_wl_input_keymap_fd_get(off_t size)
{
   int fd = 0, blen = 0, len = 0;
   const char *path;
   char tmp[PATH_MAX];
   long flags;

   blen = sizeof(tmp) - 1;

   if (!(path = getenv("XDG_RUNTIME_DIR"))) return -1;

   len = strlen(path);
   if (len < blen)
     {
        strncpy(tmp, path, len + 1);
        strncat(tmp, "/e-wl-keymap-XXXXXX", 19);
     }
   else
     {
        return -1;
     }

   if ((fd = mkstemp(tmp)) < 0)
     {
        return -1;
     }

   flags = fcntl(fd, F_GETFD);
   if (flags < 0)
     {
        close(fd);
        return -1;
     }

   if (fcntl(fd, F_SETFD, (flags | FD_CLOEXEC)) == -1)
     {
        close(fd);
        return -1;
     }

   if (ftruncate(fd, size) < 0)
     {
        close(fd);
        return -1;
     }

   unlink(tmp);
   return fd;
}

static void
_e_comp_wl_input_keymap_update(struct xkb_keymap *keymap, const char *keymap_path)
{
   char *tmp;
   xkb_mod_mask_t latched = 0, locked = 0, group = 0;
   struct wl_resource *res;
   Eina_List *l;
   E_Comp_Wl_Seat *seat;

   /* unreference any existing keymap */
   if (e_comp_wl->xkb.keymap)
     xkb_map_unref(e_comp_wl->xkb.keymap);

   /* unmap any existing keyboard area */
   if (e_comp_wl->xkb.area)
     munmap(e_comp_wl->xkb.area, e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd >= 0) close(e_comp_wl->xkb.fd);

   /* unreference any existing keyboard state */
   if (e_comp_wl->xkb.state)
     {
        latched =
          xkb_state_serialize_mods(e_comp_wl->xkb.state,
                                   XKB_STATE_MODS_LATCHED);
        locked =
          xkb_state_serialize_mods(e_comp_wl->xkb.state,
                                   XKB_STATE_MODS_LOCKED);
        group =
          xkb_state_serialize_layout(e_comp_wl->xkb.state,
                                     XKB_STATE_LAYOUT_EFFECTIVE);
        xkb_state_unref(e_comp_wl->xkb.state);
     }

   /* create a new xkb state */
   e_comp_wl->xkb.state = xkb_state_new(keymap);

   if (!e_comp_wl->xkb.state) return;

   if ((latched) || (locked) || (group))
     xkb_state_update_mask(e_comp_wl->xkb.state, 0,
                           latched, locked, 0, 0, group);

   /* increment keymap reference */
   e_comp_wl->xkb.keymap = keymap;

   EINA_LIST_FOREACH(e_comp_wl->seats, l, seat)
     {
   /* fetch updated modifiers */
   seat->kbd.mod_shift =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
   seat->kbd.mod_caps =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
   seat->kbd.mod_ctrl =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
   seat->kbd.mod_alt =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_ALT);
   seat->kbd.mod_super =
     xkb_map_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
     }

   if (!(tmp = xkb_map_get_as_string(keymap)))
     {
        ERR("Could not get keymap string");
        return;
     }

   e_comp_wl->xkb.size = strlen(tmp) + 1;
   e_comp_wl->xkb.fd =
     _e_comp_wl_input_keymap_fd_get(e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd < 0)
     {
        ERR("Could not create keymap file");
        free(tmp);
        return;
     }

   _e_comp_wl_input_keymap_cache_create(keymap_path, tmp);

   e_comp_wl->xkb.area =
     mmap(NULL, e_comp_wl->xkb.size, (PROT_READ | PROT_WRITE),
          MAP_SHARED, e_comp_wl->xkb.fd, 0);
   if (e_comp_wl->xkb.area == MAP_FAILED)
     {
        ERR("Failed to mmap keymap area: %m");
        free(tmp);
        return;
     }

   strncpy(e_comp_wl->xkb.area, tmp, e_comp_wl->xkb.size);
   free(tmp);

   /* send updated keymap */
   TRACE_INPUT_BEGIN(wl_keyboard_send_keymap_update);

   Eina_List *ll;

   EINA_LIST_FOREACH(e_comp_wl->seats, ll, seat)
     {
   EINA_LIST_FOREACH(seat->kbd.resources, l, res)
     wl_keyboard_send_keymap(res, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1,
                             e_comp_wl->xkb.fd,
                             e_comp_wl->xkb.size);
   /* update modifiers */
   e_comp_wl_input_keyboard_modifiers_update(seat);
     }

   TRACE_INPUT_END();
}

EINTERN Eina_Bool
e_comp_wl_input_add(const char *seatname)
{
   E_Comp_Wl_Seat *seat;

   /* set default seat name */
   seat = E_NEW(E_Comp_Wl_Seat, 1);
   if(!seat) return EINA_FALSE;

   ERR("e_comp_wl_input_init() seatname:%s", seatname ? : "NULL");

   if (!seatname) seat->name = "default";
   else
     {
        seat->name = malloc(strlen(seatname) + 1);
        strcpy(seat->name, seatname);
     }

   /* get default keyboard repeat rate/delay from configuration */
   seat->kbd.repeat_delay = e_config->keyboard.repeat_delay;
   seat->kbd.repeat_rate = e_config->keyboard.repeat_rate;

   /* check for valid repeat_delay and repeat_rate value */
   /* if invalid, set the default value of repeat delay and rate value */
   if (seat->kbd.repeat_delay < 0) seat->kbd.repeat_delay = 400;
   if (seat->kbd.repeat_delay < 0) seat->kbd.repeat_rate = 25;

   /* create the global resource for input seat */
   seat->global =
     wl_global_create(e_comp_wl->wl.disp, &wl_seat_interface, 4,
                      seat, _e_comp_wl_input_cb_bind_seat);
   if (!seat->global)
     {
        ERR("Could not create global for seat: %m");
        return EINA_FALSE;
     }

   wl_array_init(&seat->kbd.keys);
   wl_array_init(&seat->kbd.routed_keys);

   seat->resources = NULL;
   seat->kbd.resources = NULL;
   seat->ptr.resources = NULL;
   seat->touch.resources = NULL;
   seat->kbd.num_devices = 0;
   seat->ptr.num_devices = 0;
   seat->touch.num_devices = 0;

   seat->device_list = NULL;
   if ((seat->pointer = e_pointer_canvas_new(e_comp->ee, EINA_TRUE)))
     {
        e_pointer_hide(seat->pointer);
     }

   e_comp_wl->seats = eina_list_append(e_comp_wl->seats, seat);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_input_del(const char *seatname)
{
   struct wl_resource *res;
   E_Comp_Wl_Seat *seat;
   E_Comp_Wl_Input_Device *dev;
   e_devicemgr_input_device_user_data *device_user_data;

   seat = e_comp_wl_input_seat_get(seatname);
   if (!seat) return EINA_FALSE;

   /* destroy pointer resources */
   EINA_LIST_FREE(seat->ptr.resources, res)
     wl_resource_destroy(res);

   /* destroy keyboard resources */
   EINA_LIST_FREE(seat->kbd.resources, res)
     wl_resource_destroy(res);
   seat->kbd.resources = eina_list_free(seat->kbd.resources);

   /* destroy touch resources */
   EINA_LIST_FREE(seat->touch.resources, res)
     wl_resource_destroy(res);

   /* destroy e_comp_wl->kbd.keys array */
   wl_array_release(&seat->kbd.keys);
   wl_array_release(&seat->kbd.routed_keys);

   /* destroy the global seat resource */
   if (seat->global)
     wl_global_destroy(seat->global);
   seat->global = NULL;

   EINA_LIST_FREE(seat->device_list, dev)
     {
        if (dev->name) eina_stringshare_del(dev->name);
        if (dev->identifier) eina_stringshare_del(dev->identifier);
        EINA_LIST_FREE(dev->resources, res)
          {
             device_user_data = wl_resource_get_user_data(res);
             if (device_user_data)
               {
                  device_user_data->dev = NULL;
                  device_user_data->dev_mgr_res = NULL;
                  device_user_data->seat_res = NULL;
                  E_FREE(device_user_data);
               }

             wl_resource_set_user_data(res, NULL);
          }

        free(dev);
     }
   E_FREE_FUNC(seat->pointer, e_object_del);

   free(seat);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_input_init(void)
{
   e_comp_wl->xkb.fd = -1;
   dont_set_ecore_drm_keymap = getenv("NO_ECORE_DRM_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;
   dont_use_xkb_cache = getenv("NO_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;

   E_EVENT_TEXT_INPUT_PANEL_VISIBILITY_CHANGE = ecore_event_type_new();
   E_EVENT_SEAT_BOUND = ecore_event_type_new();

    return EINA_TRUE;
}

EINTERN void
e_comp_wl_input_shutdown(void)
{
   /* unmap any existing keyboard area */
   if (e_comp_wl->xkb.area)
     munmap(e_comp_wl->xkb.area, e_comp_wl->xkb.size);
   if (e_comp_wl->xkb.fd >= 0) close(e_comp_wl->xkb.fd);

   /* unreference any existing keyboard state */
   if (e_comp_wl->xkb.state)
     xkb_state_unref(e_comp_wl->xkb.state);

   /* unreference any existing keymap */
   if (e_comp_wl->xkb.keymap)
     xkb_map_unref(e_comp_wl->xkb.keymap);

   /* unreference any existing context */
   if (e_comp_wl->xkb.context)
     xkb_context_unref(e_comp_wl->xkb.context);

   dont_set_ecore_drm_keymap = EINA_FALSE;
   dont_use_xkb_cache = EINA_FALSE;
}

EINTERN Eina_Bool
e_comp_wl_input_pointer_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_pointer_interface,
                                  &_e_pointer_interface);
}

EINTERN Eina_Bool
e_comp_wl_input_keyboard_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_keyboard_interface,
                                  &_e_keyboard_interface);
}

EINTERN Eina_Bool
e_comp_wl_input_keyboard_modifiers_serialize(E_Comp_Wl_Seat *seat)
{
   Eina_Bool changed = EINA_FALSE;
   xkb_mod_mask_t mod;
   xkb_layout_index_t grp;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_DEPRESSED);
   changed |= mod != seat->kbd.mod_depressed;
   seat->kbd.mod_depressed = mod;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_MODS_LATCHED);
   changed |= mod != seat->kbd.mod_latched;
   seat->kbd.mod_latched = mod;

   mod = xkb_state_serialize_mods(e_comp_wl->xkb.state,
                              XKB_STATE_MODS_LOCKED);
   changed |= mod != seat->kbd.mod_locked;
   seat->kbd.mod_locked = mod;

   grp = xkb_state_serialize_layout(e_comp_wl->xkb.state,
                                XKB_STATE_LAYOUT_EFFECTIVE);
   changed |= grp != seat->kbd.mod_group;
   seat->kbd.mod_group = grp;
   return changed;
}

EINTERN void
e_comp_wl_input_keyboard_modifiers_update(E_Comp_Wl_Seat *seat)
{
   uint32_t serial;
   struct wl_resource *res;
   Eina_List *l;

   if (!e_comp_wl_input_keyboard_modifiers_serialize(seat)) return;

   if (!seat->kbd.focused) return;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(seat->kbd.focused, l, res)
     wl_keyboard_send_modifiers(res, serial,
                                seat->kbd.mod_depressed,
                                seat->kbd.mod_latched,
                                seat->kbd.mod_locked,
                                seat->kbd.mod_group);
}

EINTERN void
e_comp_wl_input_keyboard_state_update(E_Comp_Wl_Seat *seat, uint32_t keycode, Eina_Bool pressed)
{
   enum xkb_key_direction dir;

   if (!e_comp_wl->xkb.state) return;

   if (pressed) dir = XKB_KEY_DOWN;
   else dir = XKB_KEY_UP;

   seat->kbd.mod_changed =
     xkb_state_update_key(e_comp_wl->xkb.state, keycode + 8, dir);

   e_comp_wl_input_keyboard_modifiers_update(seat);
}

E_API void
e_comp_wl_input_pointer_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!seat)
     {
        ERR("No seat");
        return;
     }

   seat->ptr.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(seat);
}

E_API void
e_comp_wl_input_keyboard_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!seat)
     {
        ERR("No seat");
        return;
     }

   seat->kbd.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(seat);
}

E_API Eina_Bool
e_comp_wl_input_keymap_cache_file_use_get(void)
{
   return use_cache_keymap;
}

E_API Eina_Stringshare *
e_comp_wl_input_keymap_path_get(struct xkb_rule_names names)
{
   return eina_stringshare_printf("/var/lib/xkb/%s-%s-%s-%s-%s.xkb",
            names.rules ? names.rules : "evdev",
            names.model ? names.model : "pc105",
            names.layout ? names.layout : "us",
            names.variant ? names.variant : "",
            names.options ? names.options : "");
}

E_API struct xkb_keymap *
e_comp_wl_input_keymap_compile(struct xkb_context *ctx, struct xkb_rule_names names, char **keymap_path)
{
   struct xkb_keymap *keymap;
   char *cache_path = NULL;
   FILE *file = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ctx, NULL);

   TRACE_INPUT_BEGIN(e_comp_wl_input_keymap_compile);

   if (e_config->xkb.use_cache && !dont_use_xkb_cache)
     {
        cache_path = (char *)e_comp_wl_input_keymap_path_get(names);
        file = fopen(cache_path, "r");
     }

   if (!file)
     {
        INF("There is a no keymap file (%s). Generate keymap using rmlvo\n", cache_path);

        /* fetch new keymap based on names */
        keymap = xkb_map_new_from_names(ctx, &names, 0);
        use_cache_keymap = EINA_FALSE;
     }
   else
     {
        INF("Keymap file (%s) has been found. xkb_keymap is going to be generated with it.\n", cache_path);
        keymap = xkb_map_new_from_file(ctx, file, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
        if (!keymap)
          {
             WRN("Keymap file is exist (%s) but it is invaild file. Generate keymap using rmlvo\n", cache_path);
             fclose(file);
             remove(cache_path);
             keymap = xkb_map_new_from_names(ctx, &names, 0);
             use_cache_keymap = EINA_FALSE;
          }
        else
          {
             eina_stringshare_del(cache_path);
             cache_path = NULL;
             fclose(file);
             use_cache_keymap = EINA_TRUE;
          }
     }

   *keymap_path = cache_path;
   EINA_SAFETY_ON_NULL_RETURN_VAL(keymap, NULL);

   TRACE_INPUT_END();

   return keymap;
}

E_API void
e_comp_wl_input_keymap_set(const char *rules, const char *model, const char *layout,
                           const char *variant, const char *options,
                           struct xkb_context *dflt_ctx, struct xkb_keymap *dflt_map)
{
   struct xkb_keymap *keymap;
   struct xkb_rule_names names;
   char *keymap_path = NULL;
   Eina_Bool use_dflt_xkb = EINA_FALSE;
   const char *default_rules, *default_model, *default_layout, *default_variant, *default_options;

    INF("COMP_WL: Keymap Set: %s %s %s", rules, model, layout); 
   TRACE_INPUT_BEGIN(e_comp_wl_input_keymap_set);

   if (dflt_ctx && dflt_map) use_dflt_xkb = EINA_TRUE;

   /* unreference any existing context */
   if (e_comp_wl->xkb.context)
     xkb_context_unref(e_comp_wl->xkb.context);

   /* create a new xkb context */
   if (use_dflt_xkb) e_comp_wl->xkb.context = dflt_ctx;
   else e_comp_wl->xkb.context = xkb_context_new(0);

   if (!e_comp_wl->xkb.context)
     {
        TRACE_INPUT_END();
        return;
     }

   if (e_config->xkb.use_cache && !dont_set_ecore_drm_keymap)
     ecore_drm_device_keyboard_cached_context_set(e_comp_wl->xkb.context);

   /* assemble xkb_rule_names so we can fetch keymap */
   memset(&names, 0, sizeof(names));
   if (rules) names.rules = strdup(rules);
   else
     {
        default_rules = e_comp_wl_input_keymap_default_rules_get();
        names.rules = strdup(default_rules);
     }
   if (model) names.model = strdup(model);
   else
     {
        default_model = e_comp_wl_input_keymap_default_model_get();
        names.model = strdup(default_model);
     }
   if (layout) names.layout = strdup(layout);
   else
     {
        default_layout = e_comp_wl_input_keymap_default_layout_get();
        names.layout = strdup(default_layout);
     }
   if (variant) names.variant = strdup(variant);
   else
     {
        default_variant = e_comp_wl_input_keymap_default_variant_get();
        if (default_variant) names.variant = strdup(default_variant);
     }
   if (options) names.options = strdup(options);
   else
     {
        default_options = e_comp_wl_input_keymap_default_options_get();
        if (default_options) names.options = strdup(default_options);
     }

   TRACE_INPUT_BEGIN(e_comp_wl_input_keymap_set_keymap_compile);
   if (use_dflt_xkb)
     {
        keymap = dflt_map;
        keymap_path = (char *)e_comp_wl_input_keymap_path_get(names);
        if (access(keymap_path, R_OK) == 0)
          {
             eina_stringshare_del(keymap_path);
             keymap_path = NULL;
          }
     }
   else
     keymap = e_comp_wl_input_keymap_compile(e_comp_wl->xkb.context, names, &keymap_path);
   TRACE_INPUT_END();

   /* update compositor keymap */
   _e_comp_wl_input_keymap_update(keymap, keymap_path);

   if (e_config->xkb.use_cache && !dont_set_ecore_drm_keymap)
     ecore_drm_device_keyboard_cached_keymap_set(keymap);

   /* cleanup */
   if (keymap_path) eina_stringshare_del(keymap_path);
   free((char *)names.rules);
   free((char *)names.model);
   free((char *)names.layout);
   if (names.variant) free((char *)names.variant);
   if (names.options) free((char *)names.options);
   TRACE_INPUT_END();
}

E_API const char*
e_comp_wl_input_keymap_default_rules_get(void)
{
   const char *rules;
   if (e_config->xkb.default_rmlvo.rules)
     return e_config->xkb.default_rmlvo.rules;
   else
     {
        rules = getenv("E_DEFAULT_XKB_RULES");
        if (rules) return rules;
        else return "evdev";
     }
}

E_API const char*
e_comp_wl_input_keymap_default_model_get(void)
{
   const char *model;
   if (e_config->xkb.default_rmlvo.model)
     return e_config->xkb.default_rmlvo.model;
   else
     {
        model = getenv("E_DEFAULT_XKB_MODEL");
        if (model) return model;
        else return "pc105";
     }
}

E_API const char*
e_comp_wl_input_keymap_default_layout_get(void)
{
   const char *layout;
   if (e_config->xkb.default_rmlvo.layout)
     return e_config->xkb.default_rmlvo.layout;
   else
     {
        layout = getenv("E_DEFAULT_XKB_LAYOUT");
        if (layout) return layout;
        else return "us";
     }
}

E_API const char*
e_comp_wl_input_keymap_default_variant_get(void)
{
   const char *variant;
   if (e_config->xkb.default_rmlvo.variant)
     return e_config->xkb.default_rmlvo.variant;
   else
     {
        variant = getenv("E_DEFAULT_XKB_VARIANT");
        if (variant) return variant;
        else return NULL;
     }
}

E_API const char*
e_comp_wl_input_keymap_default_options_get(void)
{
   const char *options;
   if (e_config->xkb.default_rmlvo.options)
     return e_config->xkb.default_rmlvo.options;
   else
     {
        options = getenv("E_DEFAULT_XKB_OPTIONS");
        if (options) return options;
        else return NULL;
     }
}

E_API void
e_comp_wl_input_touch_enabled_set(E_Comp_Wl_Seat *seat, Eina_Bool enabled)
{
   /* check for valid compositor data */
   if (!seat)
     {
        ERR("No seat");
        return;
     }

   seat->touch.enabled = !!enabled;
   _e_comp_wl_input_update_seat_caps(seat);
}

EINTERN Eina_Bool
e_comp_wl_input_touch_check(struct wl_resource *res)
{
   return wl_resource_instance_of(res, &wl_touch_interface,
                                  &_e_touch_interface);
}

E_API E_Comp_Wl_Seat *
e_comp_wl_input_seat_get(const char *seatname)
{
   E_Comp_Wl_Seat *seat;
   Eina_List *l;

//   EINA_SAFETY_ON_NULL_RETURN_VAL(seatname, NULL);
   if (!seatname) seatname = "default";

   EINA_LIST_FOREACH(e_comp_wl->seats, l, seat)
     if (eina_streq(seat->name, seatname))
       return seat;

   return NULL;
}
