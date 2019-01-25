#include "e_devicemgr_private.h"

Eina_Bool
e_devicemgr_strcmp(const char *dst, const char *src)
{
   int dst_len, src_len, str_len;

   dst_len = strlen(dst);
   src_len = strlen(src);

   if (src_len > dst_len) str_len = src_len;
   else str_len = dst_len;

   if (!strncmp(dst, src, str_len))
     return EINA_TRUE;
   else
     return EINA_FALSE;
}

static int
_e_devicemgr_input_pointer_warp(int x, int y)
{
   e_input_device_pointer_warp(NULL, x, y);
   DMDBG("The pointer warped to (%d, %d) !\n", x, y);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

int
e_devicemgr_input_pointer_warp(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, wl_fixed_t x, wl_fixed_t y)
{
   E_Client *ec;
   int new_x, new_y;
   int ret;

   if (!(ec = wl_resource_get_user_data(surface)) || !ec->visible)
     {
        DMDBG("The given surface is invalid or invisible !\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_SURFACE;
     }

   if (ec != e_comp_wl->ptr.ec)
     {
        DMDBG("Pointer is not on the given surface  !\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_SURFACE;
     }

   if (e_pointer_is_hidden(e_comp->pointer))
     {
        DMDBG("Pointer is hidden");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_POINTER_AVAILABLE;
     }

   new_x = wl_fixed_to_int(x);
   new_y = wl_fixed_to_int(y);
   if (e_client_transform_core_enable_get(ec))
     e_client_transform_core_input_inv_rect_transform(ec, wl_fixed_to_int(x), wl_fixed_to_int(y), &new_x, &new_y);

   ret = _e_devicemgr_input_pointer_warp(ec->client.x + new_x, ec->client.y + new_y);
   return ret;
}

typedef struct _keycode_map{
    xkb_keysym_t keysym;
    xkb_keycode_t keycode;
} keycode_map;

static void
find_keycode(struct xkb_keymap *keymap, xkb_keycode_t key, void *data)
{
   keycode_map *found_keycodes = (keycode_map *)data;
   xkb_keysym_t keysym = found_keycodes->keysym;
   int nsyms = 0;
   const xkb_keysym_t *syms_out = NULL;

   if (found_keycodes->keycode) return;

   nsyms = xkb_keymap_key_get_syms_by_level(keymap, key, 0, 0, &syms_out);
   if (nsyms && syms_out)
     {
        if (*syms_out == keysym)
          {
             found_keycodes->keycode = key;
          }
     }
}

static void
_e_devicemgr_keycode_from_keysym(struct xkb_keymap *keymap, xkb_keysym_t keysym, xkb_keycode_t *keycode)
{
    keycode_map found_keycodes = {0,};
    found_keycodes.keysym = keysym;
    xkb_keymap_key_for_each(keymap, find_keycode, &found_keycodes);

    *keycode = found_keycodes.keycode;
}

int
e_devicemgr_keycode_from_string(const char *keyname)
{
   xkb_keysym_t keysym = 0x0;
   xkb_keycode_t keycode = 0;

   if (!strncmp(keyname, "Keycode-", sizeof("Keycode-")-1))
     {
        keycode = atoi(keyname+8);
     }
   else
     {
        keysym = xkb_keysym_from_name(keyname, XKB_KEYSYM_NO_FLAGS);
        _e_devicemgr_keycode_from_keysym(e_comp_wl->xkb.keymap, keysym, &keycode);
     }

   return keycode;
}

Eina_Bool
e_devicemgr_detent_is_detent(const char *name)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

   if (!strncmp(name, DETENT_DEVICE_NAME, sizeof(DETENT_DEVICE_NAME)))
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_devicemgr_detent_set_info(E_Devicemgr_Input_Device *dev)
{
   Eina_List *dev_list, *l, *ll, *lll;
   E_Input_Device *device_data;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;

   if ((!e_devicemgr->detent.identifier) &&
       (dev->name && !strncmp(dev->name, DETENT_DEVICE_NAME, sizeof(DETENT_DEVICE_NAME))))
     {
        e_devicemgr->detent.identifier = (char *)eina_stringshare_add(dev->identifier);
        dev_list = (Eina_List *)e_input_devices_get();
        EINA_LIST_FOREACH(dev_list, l, device_data)
          {
             EINA_LIST_FOREACH(device_data->seats, ll, seat)
               {
                  EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), lll, edev)
                    {
                       if (!strncmp(e_input_evdev_name_get(edev), DETENT_DEVICE_NAME, sizeof(DETENT_DEVICE_NAME)))
                         {
                            e_devicemgr->detent.wheel_click_angle = e_input_evdev_wheel_click_angle_get(edev);
                         }
                    }
               }
          }
     }
}

static void
_e_devicemgr_detent_unset_info(E_Devicemgr_Input_Device *dev)
{
   if ((e_devicemgr->detent.identifier) &&
       (dev->name && (!strncmp(dev->name, DETENT_DEVICE_NAME, sizeof(DETENT_DEVICE_NAME)))))
     {
        eina_stringshare_del(e_devicemgr->detent.identifier);
     }
}

static void
_e_devicemgr_input_keyevent_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Key *e = ev;

   eina_stringshare_del(e->keyname);
   eina_stringshare_del(e->key);
   eina_stringshare_del(e->compose);

   E_FREE(e->data);
   E_FREE(e);
}

static Eina_Bool
_e_devicemgr_input_mouse_button_remap(Ecore_Event_Mouse_Button *ev, Eina_Bool pressed)
{
   Ecore_Event_Key *ev_key;
   E_Keyrouter_Event_Data *key_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl->xkb.keymap, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   if (ev->buttons != 3) return ECORE_CALLBACK_PASS_ON;

   ev_key = E_NEW(Ecore_Event_Key, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev_key, ECORE_CALLBACK_PASS_ON);

   key_data = E_NEW(E_Keyrouter_Event_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(key_data, failed);

   ev_key->key = (char *)eina_stringshare_add("XF86Back");
   ev_key->keyname = (char *)eina_stringshare_add(ev_key->key);
   ev_key->compose = (char *)eina_stringshare_add(ev_key->key);
   ev_key->timestamp = (int)(ecore_time_get()*1000);
   ev_key->same_screen = 1;

   ev_key->window = e_comp->ee_win;
   ev_key->event_window = e_comp->ee_win;
   ev_key->root_window = e_comp->ee_win;
   ev_key->keycode = e_devicemgr->dconfig->conf->input.back_keycode;
   ev_key->data = key_data;

   if (pressed)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, ev_key, _e_devicemgr_input_keyevent_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, ev_key, _e_devicemgr_input_keyevent_free, NULL);

   return ECORE_CALLBACK_DONE;

failed:
   E_FREE(ev_key);
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_devicemgr_input_device_add(const char *name, const char *identifier, const char *seatname, Ecore_Device_Class clas, Ecore_Device_Subclass subclas)
{
   E_Devicemgr_Input_Device *dev;
   Eina_List *l;

   EINA_LIST_FOREACH(e_devicemgr->device_list, l, dev)
     {
        if ((dev->clas == clas) && (!strcmp(dev->identifier, identifier)))
          {
             return;
          }
     }

   if (!(dev = E_NEW(E_Devicemgr_Input_Device, 1))) return;
   dev->name = eina_stringshare_add(name);
   dev->identifier = eina_stringshare_add(identifier);
   dev->clas = clas;
   dev->subclas = subclas;

   e_devicemgr->device_list = eina_list_append(e_devicemgr->device_list, dev);

   e_devicemgr_wl_device_add(dev);
   e_devicemgr_inputgen_get_device_info(dev);
   _e_devicemgr_detent_set_info(dev);

   if (dev->clas == ECORE_DEVICE_CLASS_MOUSE)
     e_devicemgr->last_device_ptr = dev;

   if (!e_devicemgr->last_device_touch && dev->clas == ECORE_DEVICE_CLASS_TOUCH)
     e_devicemgr->last_device_touch = dev;

   if (!e_devicemgr->last_device_kbd && dev->clas == ECORE_DEVICE_CLASS_KEYBOARD)
     e_devicemgr->last_device_kbd = dev;
}

static void
_e_devicemgr_input_device_del(const char *name, const char *identifier, const char *seatname, Ecore_Device_Class clas, Ecore_Device_Subclass subclas)
{
   E_Devicemgr_Input_Device *dev;
   Eina_List *l;

   EINA_LIST_FOREACH(e_devicemgr->device_list, l, dev)
     {
        if ((dev->clas == clas) && (dev->subclas == subclas) &&
            (dev->name && (!strcmp(dev->name, name))) &&
            (dev->identifier && (!strcmp(dev->identifier, identifier))))
          break;
     }
   if (!dev)
     {
        return;
     }

   _e_devicemgr_detent_unset_info(dev);
   e_devicemgr_wl_device_del(dev);

   if (dev->name) eina_stringshare_del(dev->name);
   if (dev->identifier) eina_stringshare_del(dev->identifier);

   e_devicemgr->device_list = eina_list_remove(e_devicemgr->device_list, dev);

   if (e_devicemgr->last_device_ptr == dev)
     e_devicemgr->last_device_ptr = NULL;

   if (e_devicemgr->last_device_touch == dev)
     e_devicemgr->last_device_touch = NULL;

   if (e_devicemgr->last_device_kbd == dev)
     e_devicemgr->last_device_kbd = NULL;

   E_FREE(dev);
}

static void
_e_devicemgr_input_device_update(Ecore_Device *dev)
{
   Eina_List *l;
   E_Devicemgr_Input_Device *data;
   char *dev_identifier;

   EINA_SAFETY_ON_NULL_RETURN(dev);

   dev_identifier = (char *)ecore_device_identifier_get(dev);
   EINA_SAFETY_ON_NULL_RETURN(dev_identifier);

   EINA_LIST_FOREACH(e_devicemgr->device_list, l, data)
     {
        if (data->clas == ecore_device_class_get(dev) && data->identifier)
          {
             if (e_devicemgr_strcmp(dev_identifier, data->identifier))
               {
                  data->subclas = ecore_device_subclass_get(dev);

                  e_devicemgr_wl_device_update(data);
                  return;
               }
          }
     }
}

static Eina_Bool
_e_devicemgr_input_cb_mouse_button_down(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Button *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, res);
   ev = (Ecore_Event_Mouse_Button *)event;

   res = e_devicemgr_block_check_button(ev, EINA_TRUE);
   if (res == ECORE_CALLBACK_DONE) return res;

   if (e_devicemgr->dconfig->conf->input.button_remap_enable)
     res = _e_devicemgr_input_mouse_button_remap(ev, EINA_TRUE);

   return res;
}

static Eina_Bool
_e_devicemgr_input_cb_mouse_button_up(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Button *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, res);
   ev = (Ecore_Event_Mouse_Button *)event;

   res = e_devicemgr_block_check_button(ev, EINA_FALSE);
   if (res == ECORE_CALLBACK_DONE) return res;

   if (e_devicemgr->dconfig->conf->input.button_remap_enable)
     res = _e_devicemgr_input_mouse_button_remap(ev, EINA_FALSE);

   return res;
}

static Eina_Bool
_e_devicemgr_input_cb_mouse_move(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Move *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, res);
   ev = (Ecore_Event_Mouse_Move *)event;

   res = e_devicemgr_block_check_move(ev);

   return res;
}

static Eina_Bool
_e_devicemgr_input_cb_mouse_wheel(void *data, int type, void *event)
{
   Ecore_Event_Mouse_Wheel *ev;
   int detent;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, res);
   ev = (Ecore_Event_Mouse_Wheel *)event;

   if (!ev->dev) return ECORE_CALLBACK_PASS_ON;

   if (!e_devicemgr_detent_is_detent(ecore_device_name_get(ev->dev)))
     return ECORE_CALLBACK_PASS_ON;

   detent = (int)(ev->z / (e_devicemgr->detent.wheel_click_angle
                           ? e_devicemgr->detent.wheel_click_angle
                           : 1));

   if (detent == 2 || detent == -2)
     {
        detent = (detent / 2)*(-1);
        e_devicemgr_wl_detent_send_event(detent);
     }

   return ECORE_CALLBACK_DONE;
}


static Eina_Bool
_e_devicemgr_input_cb_device_add(void *data, int type, void *event)
{
   Ecore_Event_Device_Info *ev;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);
   ev = (Ecore_Event_Device_Info *)event;

   _e_devicemgr_input_device_add(ev->name, ev->identifier, ev->seatname, ev->clas, ev->subclas);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_devicemgr_input_cb_device_del(void *data, int type, void *event)
{
   Ecore_Event_Device_Info *ev;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);
   ev = (Ecore_Event_Device_Info *)event;

   _e_devicemgr_input_device_del(ev->name, ev->identifier, ev->seatname, ev->clas, ev->subclas);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_devicemgr_input_cb_device_update(void *data, int type, void *event)
{
   Ecore_Event_Device_Update *ev;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   ev = (Ecore_Event_Device_Update *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->dev, ECORE_CALLBACK_PASS_ON);

   _e_devicemgr_input_device_update(ev->dev);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_devicemgr_input_cb_key_down(void *data, int type, void *event)
{
   Ecore_Event_Key *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);
   ev = (Ecore_Event_Key *)event;

   res = e_devicemgr_block_check_keyboard(ev, EINA_TRUE);

   return res;
}

static Eina_Bool
_e_devicemgr_input_cb_key_up(void *data, int type, void *event)
{
   Ecore_Event_Key *ev;
   Eina_Bool res = ECORE_CALLBACK_PASS_ON;

   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);
   ev = (Ecore_Event_Key *)event;

   res = e_devicemgr_block_check_keyboard(ev, EINA_FALSE);

   return res;
}

Eina_Bool
e_devicemgr_input_init(void)
{
   e_devicemgr->virtual_mouse_device_fd = -1;
   e_devicemgr->virtual_key_device_fd = -1;

   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_devicemgr_input_cb_mouse_button_down, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_MOUSE_BUTTON_UP, _e_devicemgr_input_cb_mouse_button_up, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_MOUSE_MOVE, _e_devicemgr_input_cb_mouse_move, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_MOUSE_WHEEL, _e_devicemgr_input_cb_mouse_wheel, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_KEY_DOWN, _e_devicemgr_input_cb_key_down, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_KEY_UP, _e_devicemgr_input_cb_key_up, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_DEVICE_ADD, _e_devicemgr_input_cb_device_add, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_DEVICE_DEL, _e_devicemgr_input_cb_device_del, NULL);
   E_LIST_HANDLER_PREPEND(e_devicemgr->handlers, ECORE_EVENT_DEVICE_SUBCLASS_UPDATE, _e_devicemgr_input_cb_device_update, NULL);

   if (e_devicemgr->dconfig->conf->input.virtual_key_device_enable)
     {
        e_devicemgr->virtual_key_device_fd = e_devicemgr_create_virtual_device(ECORE_DEVICE_CLASS_KEYBOARD, "Virtual Key Device");

        if (e_devicemgr->virtual_key_device_fd >= 0)
          DMINF("input.virtual_key_device_enable: device fd : %d\n", e_devicemgr->virtual_key_device_fd);
        else
          DMWRN("input.virtual_key_device_enable: but failed to create device !\n");
     }

   if (e_devicemgr->dconfig->conf->input.virtual_mouse_device_enable)
     {
        e_devicemgr->virtual_mouse_device_fd = e_devicemgr_create_virtual_device(ECORE_DEVICE_CLASS_MOUSE, "Virtual Mouse Device");

        if (e_devicemgr->virtual_mouse_device_fd >= 0)
          DMINF("input.virtual_mouse_device_enable: device fd : %d\n", e_devicemgr->virtual_mouse_device_fd);
        else
          DMWRN("input.virtual_mouse_device_enable: but failed to create device !\n");
     }

   return EINA_TRUE;
}

void
e_devicemgr_input_shutdown(void)
{
   Ecore_Event_Handler *h = NULL;

   EINA_LIST_FREE(e_devicemgr->handlers, h)
     ecore_event_handler_del(h);

   if (e_devicemgr->virtual_key_device_fd)
     {
        e_devicemgr_destroy_virtual_device(e_devicemgr->virtual_key_device_fd);
        e_devicemgr->virtual_key_device_fd = -1;
     }
   if (e_devicemgr->virtual_mouse_device_fd)
     {
        e_devicemgr_destroy_virtual_device(e_devicemgr->virtual_mouse_device_fd);
        e_devicemgr->virtual_mouse_device_fd = -1;
     }
}
