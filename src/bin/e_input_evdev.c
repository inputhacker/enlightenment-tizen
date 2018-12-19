#include "e.h"
#include "e_input_private.h"

static void  _device_modifiers_update(E_Input_Evdev *edev);

void
_device_calibration_set(E_Input_Evdev *edev)
{
   E_Output *output;
   int w = 0, h = 0;
   int temp;

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   e_output_size_get(output, &w, &h);

   if (output)
     {
        edev->mouse.minx = edev->mouse.miny = 0;
        edev->mouse.maxw = w;
        edev->mouse.maxh = h;

        if (libinput_device_has_capability(edev->device, LIBINPUT_DEVICE_CAP_POINTER))
          {
             edev->seat->ptr.dx = (double)(w / 2);
             edev->seat->ptr.dy = (double)(h / 2);
             edev->seat->ptr.ix = (int)edev->seat->ptr.dx;
             edev->seat->ptr.iy = (int)edev->seat->ptr.dy;
             edev->mouse.dx = edev->seat->ptr.dx;
             edev->mouse.dy = edev->seat->ptr.dy;

             if (output->config.rotation == 90 || output->config.rotation == 270)
               {
                  temp = edev->mouse.minx;
                  edev->mouse.minx = edev->mouse.miny;
                  edev->mouse.miny = temp;

                  temp = edev->mouse.maxw;
                  edev->mouse.maxw = edev->mouse.maxh;
                  edev->mouse.maxh = temp;
               }
          }
     }

//LCOV_EXCL_START
#ifdef _F_E_INPUT_ENABLE_DEVICE_CALIBRATION_
   const char *sysname;
   float cal[6];
   const char *device;
   Eina_List *devices;

   if ((!libinput_device_config_calibration_has_matrix(edev->device)) ||
       (libinput_device_config_calibration_get_default_matrix(edev->device, cal) != 0))
     return;

   sysname = libinput_device_get_sysname(edev->device);

   devices = eeze_udev_find_by_subsystem_sysname("input", sysname);
   if (eina_list_count(devices) < 1) return;

#ifdef _F_E_INPUT_USE_WL_CALIBRATION_
   const char *vals;
   enum libinput_config_status status;

   EINA_LIST_FREE(devices, device)
     {
        vals = eeze_udev_syspath_get_property(device, "WL_CALIBRATION");
        if ((!vals) ||
            (sscanf(vals, "%f %f %f %f %f %f",
                    &cal[0], &cal[1], &cal[2], &cal[3], &cal[4], &cal[5]) != 6))
          goto cont;

        cal[2] /= w;
        cal[5] /= h;

        status =
          libinput_device_config_calibration_set_matrix(edev->device, cal);

        if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
          ERR("Failed to apply calibration");

cont:
        eina_stringshare_del(device);
        continue;
     }
#endif//_F_E_INPUT_USE_WL_CALIBRATION_
#endif//_F_E_INPUT_ENABLE_DEVICE_CALIBRATION_
//LCOV_EXCL_STOP
}

static void
_device_configure(E_Input_Evdev *edev)
{
   if (libinput_device_config_tap_get_finger_count(edev->device) > 0)
     {
        Eina_Bool tap = EINA_FALSE;

        tap = libinput_device_config_tap_get_default_enabled(edev->device);
        libinput_device_config_tap_set_enabled(edev->device, tap);
     }

   _device_calibration_set(edev);
}

static void
_device_keyboard_setup(E_Input_Evdev *edev)
{
   E_Input_Backend *input;
   xkb_mod_index_t xkb_idx;

   if ((!edev) || (!edev->seat)) return;
   if (!(input = edev->seat->input)) return;
   if (!input->dev->xkb_ctx) return;

   /* create keymap from xkb context */
   edev->xkb.keymap = _e_input_device_cached_keymap_get(input->dev->xkb_ctx, NULL, 0);
   if (!edev->xkb.keymap)
     {
        ERR("Failed to create keymap: %m");
        return;
     }

   /* create xkb state */
   if (!(edev->xkb.state = xkb_state_new(edev->xkb.keymap)))
     {
        ERR("Failed to create xkb state: %m");
        return;
     }

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_MOD_NAME_CTRL);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.ctrl_mask = 1 << xkb_idx;
   else
     edev->xkb.ctrl_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_MOD_NAME_ALT);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.alt_mask = 1 << xkb_idx;
   else
     edev->xkb.alt_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_MOD_NAME_SHIFT);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.shift_mask = 1 << xkb_idx;
   else
     edev->xkb.shift_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_MOD_NAME_LOGO);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.win_mask = 1 << xkb_idx;
   else
     edev->xkb.win_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_LED_NAME_SCROLL);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.scroll_mask = 1 << xkb_idx;
   else
     edev->xkb.scroll_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_LED_NAME_NUM);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.num_mask = 1 << xkb_idx;
   else
     edev->xkb.num_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, XKB_MOD_NAME_CAPS);
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.caps_mask = 1 << xkb_idx;
   else
     edev->xkb.caps_mask = 0;

   xkb_idx = xkb_map_mod_get_index(edev->xkb.keymap, "ISO_Level3_Shift");
   if (xkb_idx != XKB_MOD_INVALID)
     edev->xkb.altgr_mask = 1 << xkb_idx;
   else
     edev->xkb.altgr_mask = 0;
}

static int
_device_keysym_translate(xkb_keysym_t keysym, unsigned int modifiers, char *buffer, int bytes)
{
   unsigned long hbytes = 0;
   unsigned char c;

   if (!keysym) return 0;
   hbytes = (keysym >> 8);

   if (!(bytes &&
         ((hbytes == 0) ||
          ((hbytes == 0xFF) &&
           (((keysym >= XKB_KEY_BackSpace) && (keysym <= XKB_KEY_Clear)) ||
            (keysym == XKB_KEY_Return) || (keysym == XKB_KEY_Escape) ||
            (keysym == XKB_KEY_KP_Space) || (keysym == XKB_KEY_KP_Tab) ||
            (keysym == XKB_KEY_KP_Enter) ||
            ((keysym >= XKB_KEY_KP_Multiply) && (keysym <= XKB_KEY_KP_9)) ||
            (keysym == XKB_KEY_KP_Equal) || (keysym == XKB_KEY_Delete))))))
     return 0;

   if (keysym == XKB_KEY_KP_Space)
     c = (XKB_KEY_space & 0x7F);
   else if (hbytes == 0xFF)
     c = (keysym & 0x7F);
   else
     c = (keysym & 0xFF);

   if (modifiers & ECORE_EVENT_MODIFIER_CTRL)
     {
        if (((c >= '@') && (c < '\177')) || c == ' ')
          c &= 0x1F;
        else if (c == '2')
          c = '\000';
        else if ((c >= '3') && (c <= '7'))
          c -= ('3' - '\033');
        else if (c == '8')
          c = '\177';
        else if (c == '/')
          c = '_' & 0x1F;
     }
   buffer[0] = c;
   return 1;
}

static void
_device_modifiers_update_device(E_Input_Evdev *edev, E_Input_Evdev *from)
{
   xkb_mod_mask_t mask;

   edev->xkb.depressed =
     xkb_state_serialize_mods(from->xkb.state, XKB_STATE_DEPRESSED);
   edev->xkb.latched =
     xkb_state_serialize_mods(from->xkb.state, XKB_STATE_LATCHED);
   edev->xkb.locked =
     xkb_state_serialize_mods(from->xkb.state, XKB_STATE_LOCKED);
   edev->xkb.group =
     xkb_state_serialize_mods(from->xkb.state, XKB_STATE_EFFECTIVE);

   mask = (edev->xkb.depressed | edev->xkb.latched);

   if (mask & from->xkb.ctrl_mask)
     edev->xkb.modifiers |= ECORE_EVENT_MODIFIER_CTRL;
   if (mask & from->xkb.alt_mask)
     edev->xkb.modifiers |= ECORE_EVENT_MODIFIER_ALT;
   if (mask & from->xkb.shift_mask)
     edev->xkb.modifiers |= ECORE_EVENT_MODIFIER_SHIFT;
   if (mask & from->xkb.win_mask)
     edev->xkb.modifiers |= ECORE_EVENT_MODIFIER_WIN;
   if (mask & from->xkb.scroll_mask)
     edev->xkb.modifiers |= ECORE_EVENT_LOCK_SCROLL;
   if (mask & from->xkb.num_mask)
     edev->xkb.modifiers |= ECORE_EVENT_LOCK_NUM;
   if (mask & from->xkb.caps_mask)
     edev->xkb.modifiers |= ECORE_EVENT_LOCK_CAPS;
   if (mask & from->xkb.altgr_mask)
     edev->xkb.modifiers |= ECORE_EVENT_MODIFIER_ALTGR;
}

static void
_device_modifiers_update(E_Input_Evdev *edev)
{
   Eina_List *l;
   E_Input_Evdev *ed;

   edev->xkb.modifiers = 0;

   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     _device_modifiers_update_device(edev, edev);
   else
     {
        EINA_LIST_FOREACH(edev->seat->devices, l, ed)
          {
             if (!(ed->caps & E_INPUT_SEAT_KEYBOARD)) continue;
             _device_modifiers_update_device(edev, ed);
          }
     }

}

static int
_device_remapped_key_get(E_Input_Evdev *edev, int code)
{
   void *ret = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, code);
   if (!edev->key_remap_enabled) return code;
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev->key_remap_hash, code);

   ret = eina_hash_find(edev->key_remap_hash, &code);

   if (ret) code = (int)(intptr_t)ret;

   return code;
}

E_API Ecore_Device *
e_input_evdev_get_ecore_device(const char *path, Ecore_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Ecore_Device *dev = NULL;
   const char *identifier;

   if (!path) return NULL;

   dev_list = ecore_device_list();
   if (!dev_list) return NULL;
   EINA_LIST_FOREACH(dev_list, l, dev)
     {
        if (!dev) continue;
        identifier = ecore_device_identifier_get(dev);
        if (!identifier) continue;
        if ((ecore_device_class_get(dev) == clas) && !(strcmp(identifier, path)))
          return dev;
     }
   return NULL;
}

static void
_e_input_event_mouse_move_cb_free(void *data EINA_UNUSED, void *event)
{
   Ecore_Event_Mouse_Move *ev = event;

   if (ev->dev) ecore_device_unref(ev->dev);

   free(ev);
}

static void
_e_input_event_mouse_wheel_cb_free(void *data EINA_UNUSED, void *event)
{
   Ecore_Event_Mouse_Wheel *ev = event;

   if (ev->dev) ecore_device_unref(ev->dev);

   free(ev);
}

static void
_e_input_event_mouse_button_cb_free(void *data EINA_UNUSED, void *event)
{
   Ecore_Event_Mouse_Button *ev = event;

   if (ev->dev) ecore_device_unref(ev->dev);

   free(ev);
}

static void
_e_input_event_key_cb_free(void *data EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;

   if (ev->dev) ecore_device_unref(ev->dev);
   if (ev->data) E_FREE(ev->data);

   free(ev);
}

static void
_device_handle_key(struct libinput_device *device, struct libinput_event_keyboard *event)
{
   E_Input_Evdev *edev;
   E_Input_Backend *input;
   uint32_t timestamp;
   uint32_t code, nsyms;
   const xkb_keysym_t *syms;
   enum libinput_key_state state;
   int key_count;
   xkb_keysym_t sym = XKB_KEY_NoSymbol;
   char key[256], keyname[256], compose_buffer[256];
   Ecore_Event_Key *e;
   char *tmp = NULL, *compose = NULL;
   E_Keyrouter_Event_Data *key_data;
   Ecore_Device *ecore_dev = NULL, *data;
   Eina_List *l;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   if (!(input = edev->seat->input))
     {
        return;
     }

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_KEYBOARD)
               {
                  ecore_dev = data;
                  break;
               }
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_KEYBOARD);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }

   timestamp = libinput_event_keyboard_get_time(event);
   code = libinput_event_keyboard_get_key(event);
   code = _device_remapped_key_get(edev, code) + 8;
   state = libinput_event_keyboard_get_key_state(event);
   key_count = libinput_event_keyboard_get_seat_key_count(event);

   /* ignore key events that are not seat wide state changes */
   if (((state == LIBINPUT_KEY_STATE_PRESSED) && (key_count != 1)) ||
       ((state == LIBINPUT_KEY_STATE_RELEASED) && (key_count != 0)))
     {
        return;
     }

   xkb_state_update_key(edev->xkb.state, code,
                        (state ? XKB_KEY_DOWN : XKB_KEY_UP));

   /* get the keysym for this code */
   nsyms = xkb_key_get_syms(edev->xkb.state, code, &syms);
   if (nsyms == 1) sym = syms[0];

   /* get the keyname for this sym */
   memset(key, 0, sizeof(key));
   xkb_keysym_get_name(sym, key, sizeof(key));

   memset(keyname, 0, sizeof(keyname));
   memcpy(keyname, key, sizeof(keyname));

   if (keyname[0] == '\0')
     snprintf(keyname, sizeof(keyname), "Keycode-%u", code);

   /* if shift is active, we need to transform the key to lower */
   if (xkb_state_mod_index_is_active(edev->xkb.state,
                                     xkb_map_mod_get_index(edev->xkb.keymap,
                                     XKB_MOD_NAME_SHIFT),
                                     XKB_STATE_MODS_EFFECTIVE))
     {
        if (keyname[0] != '\0')
          keyname[0] = tolower(keyname[0]);
     }

   memset(compose_buffer, 0, sizeof(compose_buffer));
   if (_device_keysym_translate(sym, edev->xkb.modifiers,
                                compose_buffer, sizeof(compose_buffer)))
     {
        compose = eina_str_convert("ISO8859-1", "UTF-8", compose_buffer);
        if (!compose)
          {
             ERR("E Input cannot convert input key string '%s' to UTF-8. "
                 "Is Eina built with iconv support?", compose_buffer);
          }
        else
          tmp = compose;
     }

   if (!compose) compose = compose_buffer;

   e = calloc(1, sizeof(Ecore_Event_Key) + strlen(key) + strlen(keyname) +
              ((compose[0] != '\0') ? strlen(compose) : 0) + 3);
   if (!e)
     {
        E_FREE(tmp);
        return;
     }
   key_data = E_NEW(E_Keyrouter_Event_Data, 1);
   if (!key_data)
     {
        E_FREE(tmp);
        E_FREE(e);
        return;
     }

   e->keyname = (char *)(e + 1);
   e->key = e->keyname + strlen(keyname) + 1;
   e->compose = strlen(compose) ? e->key + strlen(key) + 1 : NULL;
   e->string = e->compose;

   strncpy((char *)e->keyname, keyname, strlen(keyname));
   strncpy((char *)e->key, key, strlen(key));
   if (strlen(compose)) strncpy((char *)e->compose, compose, strlen(compose));

   e->window = (Ecore_Window)input->dev->window;
   e->event_window = (Ecore_Window)input->dev->window;
   e->root_window = (Ecore_Window)input->dev->window;
   e->timestamp = timestamp;
   e->same_screen = 1;
   e->keycode = code;
   e->data = key_data;

   _device_modifiers_update(edev);

   e->modifiers = edev->xkb.modifiers;
   e->dev = ecore_device_ref(ecore_dev);

   if (state)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, e, _e_input_event_key_cb_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, e, _e_input_event_key_cb_free, NULL);

   if (tmp) free(tmp);
}

static void
_device_pointer_motion(E_Input_Evdev *edev, struct libinput_event_pointer *event)
{
   E_Input_Backend *input;
   Ecore_Event_Mouse_Move *ev;
   Ecore_Device *ecore_dev = NULL, *data, *detent_data = NULL;
   Eina_List *l;

   if (!(input = edev->seat->input)) return;

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_MOUSE)
               {
                  ecore_dev = data;
                  break;
               }
             else if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_NONE)
               {
                  detent_data = data;
               }
          }
        if (!ecore_dev && e_devicemgr_detent_is_detent(libinput_device_get_name(edev->device)))
          {
             ecore_dev = detent_data;
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_MOUSE);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }
   else if ((detent_data == ecore_dev) || e_devicemgr_detent_is_detent(ecore_device_name_get(ecore_dev)))
     {
        /* Do not process detent device's move events. */
        return;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Mouse_Move)))) return;

   if (edev->seat->ptr.ix < edev->mouse.minx)
     edev->seat->ptr.dx = edev->seat->ptr.ix = edev->mouse.minx;
   else if (edev->seat->ptr.ix >= (edev->mouse.minx + edev->mouse.maxw))
     edev->seat->ptr.dx = edev->seat->ptr.ix = (edev->mouse.minx + edev->mouse.maxw - 1);

   if (edev->seat->ptr.iy < edev->mouse.miny)
     edev->seat->ptr.dy = edev->seat->ptr.iy = edev->mouse.miny;
   else if (edev->seat->ptr.iy >= (edev->mouse.miny + edev->mouse.maxh))
     edev->seat->ptr.dy = edev->seat->ptr.iy = (edev->mouse.miny + edev->mouse.maxh - 1);

   edev->mouse.dx = edev->seat->ptr.dx;
   edev->mouse.dy = edev->seat->ptr.dy;

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   if (event) ev->timestamp = libinput_event_pointer_get_time(event);
   ev->same_screen = 1;

   _device_modifiers_update(edev);
   ev->modifiers = edev->xkb.modifiers;

   ev->x = edev->seat->ptr.ix;
   ev->y = edev->seat->ptr.iy;
   ev->root.x = ev->x;
   ev->root.y = ev->y;

   ev->multi.device = edev->mt_slot;
   ev->multi.radius = 1;
   ev->multi.radius_x = 1;
   ev->multi.radius_y = 1;
   ev->multi.pressure = 1.0;
   ev->multi.angle = 0.0;
   ev->multi.x = ev->x;
   ev->multi.y = ev->y;
   ev->multi.root.x = ev->x;
   ev->multi.root.y = ev->y;
   ev->dev = ecore_device_ref(ecore_dev);

   ecore_event_add(ECORE_EVENT_MOUSE_MOVE, ev, _e_input_event_mouse_move_cb_free, NULL);
}

void
_e_input_pointer_motion_post(E_Input_Evdev *edev)
{
   _device_pointer_motion(edev, NULL);
}

static void
_device_handle_pointer_motion(struct libinput_device *device, struct libinput_event_pointer *event)
{
   E_Input_Evdev *edev;
   double dx, dy, temp;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   dx = libinput_event_pointer_get_dx(event);
   dy = libinput_event_pointer_get_dy(event);

   if (edev->seat->ptr.swap)
     {
         temp = dx;
         dx = dy;
         dy = temp;
     }
   if (edev->seat->ptr.invert_x)
     dx *= -1;
   if (edev->seat->ptr.invert_y)
     dy *= -1;

   edev->seat->ptr.dx += dx;
   edev->seat->ptr.dy += dy;

   edev->mouse.dx = edev->seat->ptr.dx;
   edev->mouse.dy = edev->seat->ptr.dy;

   if (floor(edev->seat->ptr.dx) == edev->seat->ptr.ix &&
       floor(edev->seat->ptr.dy) == edev->seat->ptr.iy)
     {
        return;
     }

   edev->seat->ptr.ix = edev->seat->ptr.dx;
   edev->seat->ptr.iy = edev->seat->ptr.dy;

  _device_pointer_motion(edev, event);
}

static void
_device_handle_pointer_motion_absolute(struct libinput_device *device, struct libinput_event_pointer *event)
{
   E_Input_Evdev *edev;
   int w = 0, h = 0;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen), &w, &h);

   edev->mouse.dx = edev->seat->ptr.dx =
     libinput_event_pointer_get_absolute_x_transformed(event, w);
   edev->mouse.dy = edev->seat->ptr.dy =
     libinput_event_pointer_get_absolute_y_transformed(event, h);

   if (floor(edev->seat->ptr.dx) == edev->seat->ptr.ix &&
       floor(edev->seat->ptr.dy) == edev->seat->ptr.iy)
     {
        return;
     }

   edev->seat->ptr.ix = edev->seat->ptr.dx;
   edev->seat->ptr.iy = edev->seat->ptr.dy;
   _device_pointer_motion(edev, event);
}

static void
_device_handle_button(struct libinput_device *device, struct libinput_event_pointer *event)
{
   E_Input_Evdev *edev;
   E_Input_Backend *input;
   Ecore_Event_Mouse_Button *ev;
   enum libinput_button_state state;
   uint32_t button, timestamp;
   Ecore_Device *ecore_dev = NULL, *detent_data = NULL, *data;
   Eina_List *l;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }
   if (!(input = edev->seat->input))
     {
        return;
     }

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_MOUSE)
               {
                  ecore_dev = data;
                  break;
               }
             else if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_NONE)
               {
                  detent_data = data;
               }
          }
        if (!ecore_dev && e_devicemgr_detent_is_detent(libinput_device_get_name(edev->device)))
          {
             ecore_dev = detent_data;
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_MOUSE);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Mouse_Button))))
     {
        return;
     }

   state = libinput_event_pointer_get_button_state(event);
   button = libinput_event_pointer_get_button(event);
   timestamp = libinput_event_pointer_get_time(event);

   button = ((button & 0x00F) + 1);
   if (button == 3) button = 2;
   else if (button == 2) button = 3;

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   ev->timestamp = timestamp;
   ev->same_screen = 1;

   _device_modifiers_update(edev);
   ev->modifiers = edev->xkb.modifiers;

   ev->x = edev->seat->ptr.ix;
   ev->y = edev->seat->ptr.iy;
   ev->root.x = ev->x;
   ev->root.y = ev->y;

   ev->multi.device = edev->mt_slot;
   ev->multi.radius = 1;
   ev->multi.radius_x = 1;
   ev->multi.radius_y = 1;
   ev->multi.pressure = 1.0;
   ev->multi.angle = 0.0;
   ev->multi.x = ev->x;
   ev->multi.y = ev->y;
   ev->multi.root.x = ev->x;
   ev->multi.root.y = ev->y;
   ev->dev = ecore_device_ref(ecore_dev);

   if (state)
     {
        unsigned int current;

        current = timestamp;
        edev->mouse.did_double = EINA_FALSE;
        edev->mouse.did_triple = EINA_FALSE;

        if (((current - edev->mouse.prev) <= edev->mouse.threshold) &&
            (button == edev->mouse.prev_button))
          {
             edev->mouse.did_double = EINA_TRUE;
             if (((current - edev->mouse.last) <= (2 * edev->mouse.threshold)) &&
                 (button == edev->mouse.last_button))
               {
                  edev->mouse.did_triple = EINA_TRUE;
                  edev->mouse.prev = 0;
                  edev->mouse.last = 0;
                  current = 0;
               }
          }

        edev->mouse.last = edev->mouse.prev;
        edev->mouse.prev = current;
        edev->mouse.last_button = edev->mouse.prev_button;
        edev->mouse.prev_button = button;
     }

   ev->buttons = button;

   if (edev->mouse.did_double)
     ev->double_click = 1;
   if (edev->mouse.did_triple)
     ev->triple_click = 1;

   if (state)
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, ev, _e_input_event_mouse_button_cb_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_UP, ev, _e_input_event_mouse_button_cb_free, NULL);
}

static void
_device_handle_axis(struct libinput_device *device, struct libinput_event_pointer *event)
{
   E_Input_Evdev *edev;
   E_Input_Backend *input;
   Ecore_Event_Mouse_Wheel *ev;
   uint32_t timestamp;
   enum libinput_pointer_axis axis;
   Ecore_Device *ecore_dev = NULL, *detent_data = NULL, *data;
   Eina_List *l;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }
   if (!(input = edev->seat->input))
     {
        return;
     }

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_MOUSE)
               {
                  ecore_dev = data;
                  break;
               }
             else if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_NONE)
               {
                  detent_data = data;
               }
          }
        if (!ecore_dev && e_devicemgr_detent_is_detent(libinput_device_get_name(edev->device)))
          {
             ecore_dev = detent_data;
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_MOUSE);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Mouse_Wheel))))
     {
        return;
     }

   timestamp = libinput_event_pointer_get_time(event);

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   ev->timestamp = timestamp;
   ev->same_screen = 1;

   _device_modifiers_update(edev);
   ev->modifiers = edev->xkb.modifiers;

   ev->x = edev->seat->ptr.ix;
   ev->y = edev->seat->ptr.iy;
   ev->root.x = ev->x;
   ev->root.y = ev->y;
   ev->dev = ecore_device_ref(ecore_dev);

   axis = LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL;
   if (libinput_event_pointer_has_axis(event, axis))
     ev->z = libinput_event_pointer_get_axis_value(event, axis);

   axis = LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL;
   if (libinput_event_pointer_has_axis(event, axis))
     {
        ev->direction = 1;
        ev->z = libinput_event_pointer_get_axis_value(event, axis);
     }

   ecore_event_add(ECORE_EVENT_MOUSE_WHEEL, ev, _e_input_event_mouse_wheel_cb_free, NULL);
}

static void
_device_handle_touch_event_send(E_Input_Evdev *edev, struct libinput_event_touch *event, int state)
{
   E_Input_Backend *input;
   Ecore_Event_Mouse_Button *ev;
   uint32_t timestamp, button = 0;
   Ecore_Device *ecore_dev = NULL, *data;
   Eina_List *l;

   if (!edev) return;
   if (!(input = edev->seat->input)) return;

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_TOUCH)
               {
                  ecore_dev = data;
                  break;
               }
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_TOUCH);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Mouse_Button)))) return;

   timestamp = libinput_event_touch_get_time(event);

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   ev->timestamp = timestamp;
   ev->same_screen = 1;

   _device_modifiers_update(edev);
   ev->modifiers = edev->xkb.modifiers;

   ev->x = edev->seat->ptr.ix;
   ev->y = edev->seat->ptr.iy;
   ev->root.x = ev->x;
   ev->root.y = ev->y;

   ev->multi.device = edev->mt_slot;
   ev->multi.radius = 1;
   ev->multi.radius_x = 1;
   ev->multi.radius_y = 1;
   ev->multi.pressure = 1.0;
   ev->multi.angle = 0.0;
#if LIBINPUT_SUPPORT_EXTRA_TOUCH_EVENT
   if (libinput_event_get_type(libinput_event_touch_get_base_event(event))
       == LIBINPUT_EVENT_TOUCH_DOWN)
     {
        if (libinput_event_touch_has_minor(event))
          ev->multi.radius_x = libinput_event_touch_get_minor(event);
        if (libinput_event_touch_has_major(event))
          ev->multi.radius_y = libinput_event_touch_get_major(event);
        if (libinput_event_touch_has_pressure(event))
          ev->multi.pressure = libinput_event_touch_get_pressure(event);
        if (libinput_event_touch_has_orientation(event))
          ev->multi.angle = libinput_event_touch_get_orientation(event);
     }
#endif
   ev->multi.x = ev->x;
   ev->multi.y = ev->y;
   ev->multi.root.x = ev->x;
   ev->multi.root.y = ev->y;
   ev->dev = ecore_device_ref(ecore_dev);

   if (state == ECORE_EVENT_MOUSE_BUTTON_DOWN)
     {
        unsigned int current;

        current = timestamp;
        edev->mouse.did_double = EINA_FALSE;
        edev->mouse.did_triple = EINA_FALSE;

        if (((current - edev->mouse.prev) <= edev->mouse.threshold) &&
            (button == edev->mouse.prev_button))
          {
             edev->mouse.did_double = EINA_TRUE;
             if (((current - edev->mouse.last) <= (2 * edev->mouse.threshold)) &&
                 (button == edev->mouse.last_button))
               {
                  edev->mouse.did_triple = EINA_TRUE;
                  edev->mouse.prev = 0;
                  edev->mouse.last = 0;
                  current = 0;
               }
          }

        edev->mouse.last = edev->mouse.prev;
        edev->mouse.prev = current;
        edev->mouse.last_button = edev->mouse.prev_button;
        edev->mouse.prev_button = button;
        edev->touch.pressed |= (1 << ev->multi.device);
     }
   else
     {
        edev->touch.pressed &= ~(1 << ev->multi.device);
     }

   ev->buttons = ((button & 0x00F) + 1);

   if (edev->mouse.did_double)
     ev->double_click = 1;
   if (edev->mouse.did_triple)
     ev->triple_click = 1;

   ecore_event_add(state, ev, _e_input_event_mouse_button_cb_free, NULL);
}

static void
_device_handle_touch_motion_send(E_Input_Evdev *edev, struct libinput_event_touch *event)
{
   E_Input_Backend *input;
   Ecore_Event_Mouse_Move *ev;
   Ecore_Device *ecore_dev = NULL, *data;
   Eina_List *l;

   if (!edev) return;
   if (!(input = edev->seat->input)) return;

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_TOUCH)
               {
                  ecore_dev = data;
                  break;
               }
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_TOUCH);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        return;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Mouse_Move)))) return;

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   ev->timestamp = libinput_event_touch_get_time(event);
   ev->same_screen = 1;

   _device_modifiers_update(edev);
   ev->modifiers = edev->xkb.modifiers;

   ev->x = edev->seat->ptr.ix;
   ev->y = edev->seat->ptr.iy;
   ev->root.x = ev->x;
   ev->root.y = ev->y;

   ev->multi.device = edev->mt_slot;
   ev->multi.radius = 1;
   ev->multi.radius_x = 1;
   ev->multi.radius_y = 1;
   ev->multi.pressure = 1.0;
   ev->multi.angle = 0.0;
#if LIBINPUT_SUPPORT_EXTRA_TOUCH_EVENT
   if (libinput_event_touch_has_minor(event))
     ev->multi.radius_x = libinput_event_touch_get_minor(event);
   if (libinput_event_touch_has_major(event))
     ev->multi.radius_y = libinput_event_touch_get_major(event);
   if (libinput_event_touch_has_pressure(event))
     ev->multi.pressure = libinput_event_touch_get_pressure(event);
   if (libinput_event_touch_has_orientation(event))
     ev->multi.angle = libinput_event_touch_get_orientation(event);
#endif
   ev->multi.x = ev->x;
   ev->multi.y = ev->y;
   ev->multi.root.x = ev->x;
   ev->multi.root.y = ev->y;
   ev->dev = ecore_device_ref(ecore_dev);

   ecore_event_add(ECORE_EVENT_MOUSE_MOVE, ev, _e_input_event_mouse_move_cb_free, NULL);
}

static void
_device_handle_touch_down(struct libinput_device *device, struct libinput_event_touch *event)
{
   E_Input_Evdev *edev;
   int w = 0, h = 0;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen), &w, &h);

   edev->mouse.dx = edev->seat->ptr.ix = edev->seat->ptr.dx =
     libinput_event_touch_get_x_transformed(event, w);
   edev->mouse.dy = edev->seat->ptr.iy = edev->seat->ptr.dy =
     libinput_event_touch_get_y_transformed(event, h);

   edev->mt_slot = libinput_event_touch_get_slot(event);

   if (edev->mt_slot < E_INPUT_MAX_SLOTS)
     {
        edev->touch.coords[edev->mt_slot].x = edev->seat->ptr.ix;
        edev->touch.coords[edev->mt_slot].y = edev->seat->ptr.iy;
     }

   _device_handle_touch_motion_send(edev, event);
   _device_handle_touch_event_send(edev, event, ECORE_EVENT_MOUSE_BUTTON_DOWN);
}

static void
_device_handle_touch_motion(struct libinput_device *device, struct libinput_event_touch *event)
{
   E_Input_Evdev *edev;
   int w = 0, h = 0;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen), &w, &h);

   edev->mouse.dx = edev->seat->ptr.dx =
     libinput_event_touch_get_x_transformed(event, w);
   edev->mouse.dy = edev->seat->ptr.dy =
     libinput_event_touch_get_y_transformed(event, h);

   if (floor(edev->seat->ptr.dx) == edev->seat->ptr.ix &&
       floor(edev->seat->ptr.dy) == edev->seat->ptr.iy)
     {
        return;
     }

   edev->seat->ptr.ix = edev->seat->ptr.dx;
   edev->seat->ptr.iy = edev->seat->ptr.dy;

   edev->mt_slot = libinput_event_touch_get_slot(event);

   if (edev->mt_slot < E_INPUT_MAX_SLOTS)
     {
        edev->touch.coords[edev->mt_slot].x = edev->seat->ptr.ix;
        edev->touch.coords[edev->mt_slot].y = edev->seat->ptr.iy;
     }

   _device_handle_touch_motion_send(edev, event);
}

static void
_device_handle_touch_up(struct libinput_device *device, struct libinput_event_touch *event)
{
   E_Input_Evdev *edev;

   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   edev->mt_slot = libinput_event_touch_get_slot(event);

   if (edev->mt_slot < E_INPUT_MAX_SLOTS)
     {
        edev->mouse.dx = edev->seat->ptr.dx = edev->seat->ptr.ix =
          edev->touch.coords[edev->mt_slot].x;
        edev->mouse.dy = edev->seat->ptr.dy = edev->seat->ptr.iy =
          edev->touch.coords[edev->mt_slot].y;
     }

   _device_handle_touch_event_send(edev, event, ECORE_EVENT_MOUSE_BUTTON_UP);
}

static void
_device_handle_touch_frame(struct libinput_device *device EINA_UNUSED, struct libinput_event_touch *event EINA_UNUSED)
{
   /* DBG("Unhandled Touch Frame Event"); */
}

static void
_e_input_aux_data_event_free(void *user_data EINA_UNUSED, void *ev)
{
   Ecore_Event_Axis_Update *e = (Ecore_Event_Axis_Update *)ev;

   if (e->axis) free(e->axis);
   if (e->dev) ecore_device_unref(e->dev);

   free(e);
}

static void
_device_handle_touch_aux_data(struct libinput_device *device, struct libinput_event_touch_aux_data *event)
{
   E_Input_Evdev *edev;
   E_Input_Backend *input;
   Ecore_Event_Axis_Update *ev;
   Ecore_Axis *axis;
   Ecore_Device *ecore_dev = NULL, *data;
   Eina_List *l;

   if (libinput_event_touch_aux_data_get_type(event) != LIBINPUT_TOUCH_AUX_DATA_TYPE_PALM &&
       libinput_event_touch_aux_data_get_value(event) > 0)
      goto end;

   if (!(edev = libinput_device_get_user_data(device))) goto end;
   if (!(input = edev->seat->input)) goto end;

   if (edev->ecore_dev) ecore_dev = edev->ecore_dev;
   else if (edev->ecore_dev_list && eina_list_count(edev->ecore_dev_list) > 0)
     {
        EINA_LIST_FOREACH(edev->ecore_dev_list, l, data)
          {
             if (ecore_device_class_get(data) == ECORE_DEVICE_CLASS_TOUCH)
               {
                  ecore_dev = data;
                  break;
               }
          }
     }
   else
     {
        edev->ecore_dev = e_input_evdev_get_ecore_device(edev->path, ECORE_DEVICE_CLASS_TOUCH);
        ecore_dev = edev->ecore_dev;
     }

   if (!ecore_dev)
     {
        ERR("Failed to get source ecore device from event !\n");
        goto end;
     }

   if (!(ev = calloc(1, sizeof(Ecore_Event_Axis_Update))))goto end;

   ev->window = (Ecore_Window)input->dev->window;
   ev->event_window = (Ecore_Window)input->dev->window;
   ev->root_window = (Ecore_Window)input->dev->window;
   ev->timestamp = libinput_event_touch_aux_data_get_time(event);

   axis = (Ecore_Axis *)calloc(1, sizeof(Ecore_Axis));
   if (axis)
     {
        axis->label = ECORE_AXIS_LABEL_TOUCH_PALM;
        axis->value = libinput_event_touch_aux_data_get_value(event);
        ev->naxis = 1;
     }
   ev->axis = axis;
   ev->dev = ecore_device_ref(ecore_dev);

   ecore_event_add(ECORE_EVENT_AXIS_UPDATE, ev, _e_input_aux_data_event_free, NULL);

end:
   ;
}

E_Input_Evdev *
_e_input_evdev_device_create(E_Input_Seat *seat, struct libinput_device *device)
{
   E_Input_Evdev *edev;
   E_Input_Backend *b_input;

   EINA_SAFETY_ON_NULL_RETURN_VAL(seat, NULL);

   /* try to allocate space for new evdev */
   if (!(edev = calloc(1, sizeof(E_Input_Evdev)))) return NULL;

   edev->seat = seat;
   edev->device = device;
   edev->path = eina_stringshare_printf("%s/%s", e_input_base_dir_get(), libinput_device_get_sysname(device));

   if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD))
     {
        edev->caps |= E_INPUT_SEAT_KEYBOARD;
        _device_keyboard_setup(edev);
     }

   if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER))
     {
        edev->caps |= E_INPUT_SEAT_POINTER;

        /* TODO: make this configurable */
        edev->mouse.threshold = 250;

        b_input = seat->input;
        if (b_input->left_handed == EINA_TRUE)
          {
             if (libinput_device_config_left_handed_set(device, 1) !=
                 LIBINPUT_CONFIG_STATUS_SUCCESS)
               {
                  WRN("Failed to set left hand mode about device: %s\n",
                      libinput_device_get_name(device));
               }
          }
     }

   if (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_TOUCH))
     {
        int palm_code;
        edev->caps |= E_INPUT_SEAT_TOUCH;
        palm_code = libinput_device_touch_aux_data_get_code(LIBINPUT_TOUCH_AUX_DATA_TYPE_PALM);
        if (libinput_device_touch_has_aux_data(device, palm_code))
          {
             libinput_device_touch_set_aux_data(device, palm_code);
          }
     }

   libinput_device_set_user_data(device, edev);
   libinput_device_ref(device);

   /* configure device */
   _device_configure(edev);

   return edev;
}

void
_e_input_evdev_device_destroy(E_Input_Evdev *edev)
{
   Ecore_Device *dev;

   EINA_SAFETY_ON_NULL_RETURN(edev);

   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        if (edev->xkb.state) xkb_state_unref(edev->xkb.state);
        if (edev->xkb.keymap) xkb_map_unref(edev->xkb.keymap);
     }

   if (edev->ecore_dev) ecore_device_del(edev->ecore_dev);
   if (edev->ecore_dev_list)
     EINA_LIST_FREE(edev->ecore_dev_list, dev)
       {
          ecore_device_del(dev);
       }
   if (edev->path) eina_stringshare_del(edev->path);
   if (edev->device) libinput_device_unref(edev->device);
   if (edev->key_remap_hash) eina_hash_free(edev->key_remap_hash);

   free(edev);
}

Eina_Bool
_e_input_evdev_event_process(struct libinput_event *event)
{
   struct libinput_device *device;
   Eina_Bool ret = EINA_TRUE;

   device = libinput_event_get_device(event);
   switch (libinput_event_get_type(event))
     {
      case LIBINPUT_EVENT_KEYBOARD_KEY:
        _device_handle_key(device, libinput_event_get_keyboard_event(event));
        break;
      case LIBINPUT_EVENT_POINTER_MOTION:
        _device_handle_pointer_motion(device,
                                      libinput_event_get_pointer_event(event));
        break;
      case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
        _device_handle_pointer_motion_absolute(device,
                                               libinput_event_get_pointer_event(event));
        break;
      case LIBINPUT_EVENT_POINTER_BUTTON:
        _device_handle_button(device, libinput_event_get_pointer_event(event));
        break;
      case LIBINPUT_EVENT_POINTER_AXIS:
        _device_handle_axis(device, libinput_event_get_pointer_event(event));
        break;
      case LIBINPUT_EVENT_TOUCH_DOWN:
        _device_handle_touch_down(device, libinput_event_get_touch_event(event));
        break;
      case LIBINPUT_EVENT_TOUCH_MOTION:
        _device_handle_touch_motion(device,
                                    libinput_event_get_touch_event(event));
        break;
      case LIBINPUT_EVENT_TOUCH_UP:
        _device_handle_touch_up(device, libinput_event_get_touch_event(event));
        break;
      case LIBINPUT_EVENT_TOUCH_FRAME:
        _device_handle_touch_frame(device, libinput_event_get_touch_event(event));
        break;
      case LIBINPUT_EVENT_TOUCH_AUX_DATA:
        _device_handle_touch_aux_data(device, libinput_event_get_touch_aux_data(event));
        break;
      default:
        ret = EINA_FALSE;
        break;
     }

   return ret;
}

/**
 * @brief Set the axis size of the given device.
 *
 * @param dev The device to set the axis size to.
 * @param w The width of the axis.
 * @param h The height of the axis.
 *
 * This function sets set the width @p w and height @p h of the axis
 * of device @p dev. If @p dev is a relative input device, a width and
 * height must set for it. If its absolute set the ioctl correctly, if
 * not, unsupported device.
 */
EINTERN void
e_input_evdev_axis_size_set(E_Input_Evdev *edev, int w, int h)
{
   const char *sysname;
   float cal[6];
   const char *device;
   Eina_List *devices;
   const char *vals;
   enum libinput_config_status status;

   EINA_SAFETY_ON_NULL_RETURN(edev);
   EINA_SAFETY_ON_TRUE_RETURN((w == 0) || (h == 0));

   if ((!libinput_device_config_calibration_has_matrix(edev->device)) ||
       (libinput_device_config_calibration_get_default_matrix(edev->device, cal) != 0))
     return;

   sysname = libinput_device_get_sysname(edev->device);

   devices = eeze_udev_find_by_subsystem_sysname("input", sysname);
   if (eina_list_count(devices) < 1) return;

   EINA_LIST_FREE(devices, device)
     {
        vals = eeze_udev_syspath_get_property(device, "WL_CALIBRATION");
	if ((!vals) ||
            (sscanf(vals, "%f %f %f %f %f %f",
                    &cal[0], &cal[1], &cal[2], &cal[3], &cal[4], &cal[5]) != 6))
          goto cont;

        cal[2] /= w;
        cal[5] /= h;

        status =
          libinput_device_config_calibration_set_matrix(edev->device, cal);

        if (status != LIBINPUT_CONFIG_STATUS_SUCCESS)
          ERR("Failed to apply calibration");

cont:
        eina_stringshare_del(device);
        continue;
     }
}

E_API const char *
e_input_evdev_name_get(E_Input_Evdev *evdev)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(evdev, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evdev->device, NULL);

   return libinput_device_get_name(evdev->device);
}

EINTERN const char *
e_input_evdev_sysname_get(E_Input_Evdev *evdev)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(evdev, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evdev->device, NULL);

   return libinput_device_get_sysname(evdev->device);
}

EINTERN Eina_Bool
e_input_evdev_key_remap_enable(E_Input_Evdev *edev, Eina_Bool enable)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev->device, EINA_FALSE);

   edev->key_remap_enabled = enable;

   if (enable == EINA_FALSE && edev->key_remap_hash)
     {
        eina_hash_free(edev->key_remap_hash);
        edev->key_remap_hash = NULL;
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_input_evdev_key_remap_set(E_Input_Evdev *edev, int *from_keys, int *to_keys, int num)
{
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev->device, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(from_keys, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(to_keys, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(num <= 0, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(!edev->key_remap_enabled, EINA_FALSE);

   if (edev->key_remap_hash == NULL)
     edev->key_remap_hash = eina_hash_int32_new(NULL);

   if (edev->key_remap_hash == NULL)
     {
        ERR("Failed to set remap key information : creating a hash is failed.");
        return EINA_FALSE;
     }

   for (i = 0; i < num ; i++)
     {
        if (!from_keys[i] || !to_keys[i])
          {
             ERR("Failed to set remap key information : given arguments are invalid.");
             return EINA_FALSE;
          }
     }

   for (i = 0; i < num ; i++)
     {
        eina_hash_add(edev->key_remap_hash, &from_keys[i], (void *)(intptr_t)to_keys[i]);
     }

   return EINA_TRUE;
}

E_API int
e_input_evdev_wheel_click_angle_get(E_Input_Evdev *dev)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, -1);
   return libinput_device_config_scroll_get_wheel_click_angle(dev->device);
}

EINTERN Eina_Bool
e_input_evdev_touch_calibration_set(E_Input_Evdev *edev, float matrix[6])
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev->device, EINA_FALSE);

   if (!libinput_device_config_calibration_has_matrix(edev->device) ||
       !libinput_device_has_capability(edev->device, LIBINPUT_DEVICE_CAP_TOUCH))
     return EINA_FALSE;

   if (libinput_device_config_calibration_set_matrix(edev->device, matrix) !=
       LIBINPUT_CONFIG_STATUS_SUCCESS)
     {
        WRN("Failed to set input transformation about device: %s\n",
            libinput_device_get_name(edev->device));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EAPI Eina_Bool
e_input_evdev_mouse_accel_speed_set(E_Input_Evdev *edev, double speed)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev->device, EINA_FALSE);

   if (!libinput_device_has_capability(edev->device, LIBINPUT_DEVICE_CAP_POINTER))
     return EINA_FALSE;

   if (!libinput_device_config_accel_is_available(edev->device))
     return EINA_FALSE;

   if (libinput_device_config_accel_set_speed(edev->device, speed) !=
       LIBINPUT_CONFIG_STATUS_SUCCESS)
     {
        WRN("Failed to set mouse accel about device: %s\n",
            libinput_device_get_name(edev->device));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN unsigned int
e_input_evdev_touch_pressed_get(E_Input_Evdev *edev)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(edev, 0x0);

   return edev->touch.pressed;
}
