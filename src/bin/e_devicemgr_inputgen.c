#include "e_devicemgr_private.h"

static Eina_List **
_e_devicemgr_inputgen_list_get(Ecore_Device_Class clas)
{
   switch (clas)
     {
        case ECORE_DEVICE_CLASS_KEYBOARD:
          return &e_devicemgr->inputgen.kbd_list;
          break;
        case ECORE_DEVICE_CLASS_MOUSE:
          return &e_devicemgr->inputgen.ptr_list;
          break;
        case ECORE_DEVICE_CLASS_TOUCH:
          return &e_devicemgr->inputgen.touch_list;
          break;
        default:
          return NULL;
     }
}

static char *
_e_devicemgr_inputgen_name_get(struct wl_resource *resource)
{
   Eina_List *l;
   E_Devicemgr_Inputgen_Resource_Data *rdata;

   EINA_LIST_FOREACH(e_devicemgr->inputgen.resource_list, l, rdata)
     {
        if (rdata->resource == resource) return rdata->name;
     }

   return NULL;
}

static Eina_Bool
_e_devicemgr_inputgen_device_check(char *name, Ecore_Device_Class clas)
{
   Eina_List **dev_list, *l;
   E_Devicemgr_Inputgen_Device_Data *ddata;

   if (!name) return EINA_FALSE;
   dev_list = _e_devicemgr_inputgen_list_get(clas);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev_list, EINA_FALSE);

   EINA_LIST_FOREACH(*dev_list, l, ddata)
     {
        if (!strncmp(ddata->name, name, UINPUT_MAX_NAME_SIZE))
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_devicemgr_inputgen_key_event_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Key *e = ev;

   eina_stringshare_del(e->keyname);
   eina_stringshare_del(e->key);
   eina_stringshare_del(e->compose);

   if (e->dev) ecore_device_unref(e->dev);
   if (e->data) E_FREE(e->data);

   free(e);
}

int
_e_devicemgr_inputgen_key_event_add(const char *key, Eina_Bool pressed, char *identifier)
{
   Ecore_Event_Key *e;
   unsigned int keycode;
   E_Keyrouter_Event_Data *key_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(key, TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER);

   e = E_NEW(Ecore_Event_Key, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES);

   keycode = e_devicemgr_keycode_from_string(key);
   if (keycode <= 0) goto finish;

   e->keyname = eina_stringshare_add(key);
   e->key = eina_stringshare_add(key);
   e->compose = eina_stringshare_add(key);
   e->string = e->compose;

   e->window = e_comp->ee_win;
   e->event_window = e_comp->ee_win;
   e->root_window = e_comp->ee_win;
   e->timestamp = (int)(ecore_time_get() * 1000);
   e->same_screen = 1;
   e->keycode = keycode;
   key_data = E_NEW(E_Keyrouter_Event_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(key_data, finish);
   e->data = key_data;

   e->modifiers = 0;
   e->dev = ecore_device_ref(e_input_evdev_get_ecore_device(identifier, ECORE_DEVICE_CLASS_KEYBOARD));

   DMDBG("Generate key event: key: %s, keycode: %d, iden: %s\n", e->key, e->keycode, identifier);

   if (pressed)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, e, _e_devicemgr_inputgen_key_event_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, e, _e_devicemgr_inputgen_key_event_free, NULL);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

finish:
    if(e) E_FREE(e);
    return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
}

static void
_e_devicemgr_inputgen_mouse_button_event_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Mouse_Button *e = ev;

   if (e->dev) ecore_device_unref(e->dev);

   free(e);
}

int
_e_devicemgr_inputgen_mouse_button_event(Eina_Bool state, int x, int y, int buttons, char *identifier)
{
   Ecore_Event_Mouse_Button *e;

   e = calloc(1, sizeof(Ecore_Event_Mouse_Button));
   EINA_SAFETY_ON_NULL_RETURN_VAL(e, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES);

   e->window = e_comp->ee_win;
   e->event_window = e_comp->ee_win;
   e->root_window = e_comp->ee_win;
   e->timestamp = (int)(ecore_time_get() * 1000);
   e->same_screen = 1;

   e->x = x;
   e->y = y;
   e->root.x = e->x;
   e->root.y = e->y;

   e->multi.device = 0;
   e->multi.radius = 1;
   e->multi.radius_x = 1;
   e->multi.radius_y = 1;
   e->multi.pressure = 1.0;
   e->multi.angle = 0.0;

   e->multi.x = e->x;
   e->multi.y = e->y;
   e->multi.root.x = e->x;
   e->multi.root.y = e->y;
   e->dev = ecore_device_ref(e_input_evdev_get_ecore_device(identifier, ECORE_DEVICE_CLASS_MOUSE));
   e->buttons = buttons;

   DMDBG("Generate mouse button event: button: %d (state: %d)\n", buttons, state);

   if (state)
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, e, _e_devicemgr_inputgen_mouse_button_event_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_UP, e, _e_devicemgr_inputgen_mouse_button_event_free, NULL);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

static void
_e_devicemgr_inputgen_mouse_move_event_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Mouse_Move *e = ev;

   if (e->dev) ecore_device_unref(e->dev);

   free(e);
}

int
_e_devicemgr_inputgen_mouse_move_event(int x, int y, char *identifier)
{
   Ecore_Event_Mouse_Move *e;

   DMERR("Try\n");

   e = calloc(1, sizeof(Ecore_Event_Mouse_Move));
   if (!e) return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES;

   e->window = e_comp->ee_win;
   e->event_window = e_comp->ee_win;
   e->root_window = e_comp->ee_win;
   e->timestamp = (int)(ecore_time_get() * 1000);
   e->same_screen = 1;

   e->x = x;
   e->y = y;
   e->root.x = e->x;
   e->root.y = e->y;

   e->multi.device = 0;
   e->multi.radius = 1;
   e->multi.radius_x = 1;
   e->multi.radius_y = 1;
   e->multi.pressure = 1.0;
   e->multi.angle = 0.0;

   e->multi.x = e->x;
   e->multi.y = e->y;
   e->multi.root.x = e->x;
   e->multi.root.y = e->y;
   e->dev = ecore_device_ref(e_input_evdev_get_ecore_device(identifier, ECORE_DEVICE_CLASS_MOUSE));

   DMDBG("Generate mouse move event: (%d, %d)\n", e->x, e->y);

   ecore_event_add(ECORE_EVENT_MOUSE_MOVE, e, _e_devicemgr_inputgen_mouse_move_event_free, NULL);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

static int
_e_devicemgr_inputgen_touch_event(uint32_t type, uint32_t x, uint32_t y, uint32_t finger, char *identifier)
{
   Ecore_Event_Mouse_Button *e;

   e = calloc(1, sizeof(Ecore_Event_Mouse_Button));
   EINA_SAFETY_ON_NULL_RETURN_VAL(e, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES);

   e->window = e_comp->ee_win;
   e->event_window = e_comp->ee_win;
   e->root_window = e_comp->ee_win;
   e->timestamp = (int)(ecore_time_get() * 1000);
   e->same_screen = 1;

   e->x = x;
   e->y = y;
   e->root.x = e->x;
   e->root.y = e->y;

   e->multi.device = finger;
   e->multi.radius = 1;
   e->multi.radius_x = 1;
   e->multi.radius_y = 1;
   e->multi.pressure = 1.0;
   e->multi.angle = 0.0;

   e->multi.x = e->x;
   e->multi.y = e->y;
   e->multi.root.x = e->x;
   e->multi.root.y = e->y;
   e->dev = ecore_device_ref(e_input_evdev_get_ecore_device(identifier, ECORE_DEVICE_CLASS_TOUCH));
   e->buttons = 1;

   DMDBG("Generate touch event: device: %d (%d, %d)\n", e->multi.device, e->x, e->y);

   if (type == TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_BEGIN)
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_DOWN, e, _e_devicemgr_inputgen_mouse_button_event_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_MOUSE_BUTTON_UP, e, _e_devicemgr_inputgen_mouse_button_event_free, NULL);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

static int
_e_devicemgr_inputgen_touch_update_event(uint32_t x, uint32_t y, uint32_t finger, char *identifier)
{
   Ecore_Event_Mouse_Move *e;

   e = calloc(1, sizeof(Ecore_Event_Mouse_Move));
   EINA_SAFETY_ON_NULL_RETURN_VAL(e, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES);

   e->window = e_comp->ee_win;
   e->event_window = e_comp->ee_win;
   e->root_window = e_comp->ee_win;
   e->timestamp = (int)(ecore_time_get() * 1000);
   e->same_screen = 1;

   e->x = x;
   e->y = y;
   e->root.x = e->x;
   e->root.y = e->y;

   e->multi.device = finger;
   e->multi.radius = 1;
   e->multi.radius_x = 1;
   e->multi.radius_y = 1;
   e->multi.pressure = 1.0;
   e->multi.angle = 0.0;

   e->multi.x = e->x;
   e->multi.y = e->y;
   e->multi.root.x = e->x;
   e->multi.root.y = e->y;
   e->dev = ecore_device_ref(e_input_evdev_get_ecore_device(identifier, ECORE_DEVICE_CLASS_TOUCH));

   DMDBG("Generate touch move event: device: %d (%d, %d)\n", e->multi.device, e->x, e->y);

   ecore_event_add(ECORE_EVENT_MOUSE_MOVE, e, _e_devicemgr_inputgen_mouse_move_event_free, NULL);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

static void
_e_devicemgr_inputgen_remove_device(E_Devicemgr_Inputgen_Device_Data *device)
{
   int i = 0, ret;
   Eina_List *l, *l_next;
   Eina_Stringshare *str_data;

   if (!device || device->uinp_fd < 0)
     {
        DMWRN("There are no devices created for input generation.\n");
        return;
     }

   switch (device->clas)
     {
        case ECORE_DEVICE_CLASS_TOUCH:
          while (device->touch.pressed)
            {
               if (device->touch.pressed & (1 << i))
                 {
                    ret = _e_devicemgr_inputgen_touch_event(
                            TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_END,
                            device->touch.coords[i].x,
                            device->touch.coords[i].y,
                            i, device->identifier);
                    if (ret != TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE)
                      DMWRN("Failed to generate touch up event: %d\n", ret);
                    device->touch.pressed &= ~(1 << i);
                 }
               i++;
               if (i >= INPUTGEN_MAX_TOUCH) break;
            }
          break;
        case ECORE_DEVICE_CLASS_MOUSE:
          while (device->mouse.pressed)
            {
               if (device->mouse.pressed & (1 << i))
                 {
                    ret = _e_devicemgr_inputgen_mouse_button_event(
                            EINA_FALSE,
                            device->mouse.coords.x,
                            device->mouse.coords.y,
                            i + 1, device->identifier);
                    if (ret != TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE)
                      DMWRN("Failed to generate mouse button up event: %d\n", ret);
                    device->mouse.pressed &= ~(1 << i);
                 }
               i++;
               if (i >= INPUTGEN_MAX_BTN) break;
            }
          break;
        case ECORE_DEVICE_CLASS_KEYBOARD:
          EINA_LIST_FOREACH_SAFE(device->key.pressed, l, l_next, str_data)
            {
               ret = _e_devicemgr_inputgen_key_event_add(str_data, EINA_FALSE, device->identifier);
               if (ret != TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE)
                 DMWRN("Failed to generate key up event: %d\n", ret);
               eina_stringshare_del(str_data);
               device->key.pressed = eina_list_remove_list(device->key.pressed, l);
            }
          break;
        default:
          break;
     }

   e_devicemgr_destroy_virtual_device(device->uinp_fd);
   device->uinp_fd = -1;
   eina_stringshare_del(device->identifier);
   device->identifier = NULL;
}

static void
_e_devicemgr_inputgen_client_device_remove(struct wl_client *client, Ecore_Device_Class clas)
{
   Eina_List **dev_list, *l, *l_next, *ll, *ll_next;
   E_Devicemgr_Inputgen_Device_Data *ddata;
   E_Devicemgr_Inputgen_Client_Data *cdata;

   dev_list = _e_devicemgr_inputgen_list_get(clas);
   EINA_SAFETY_ON_NULL_RETURN(dev_list);

   EINA_LIST_FOREACH_SAFE(*dev_list, l, l_next, ddata)
     {
        EINA_LIST_FOREACH_SAFE(ddata->clients, ll, ll_next, cdata)
          {
             if (cdata->client == client)
               {
                  ddata->clients = eina_list_remove_list(ddata->clients, ll);
                  E_FREE(cdata);
               }
          }
        if (eina_list_count(ddata->clients) == 0)
          {
             _e_devicemgr_inputgen_remove_device(ddata);
             *dev_list = eina_list_remove_list(*dev_list, l);
             E_FREE(ddata);
          }
     }
}

static void
_e_devicemgr_inputgen_client_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_client *client = (struct wl_client *)data;
   E_Devicemgr_Inputgen_Resource_Data *rdata;
   E_Devicemgr_Inputgen_Client_Global_Data *gdata;
   Eina_List *list, *l_next;

   EINA_SAFETY_ON_NULL_RETURN(l);

   wl_list_remove(&l->link);
   E_FREE(l);

   EINA_LIST_FOREACH_SAFE(e_devicemgr->watched_clients, list, l_next, gdata)
     {
        if (gdata->client == client)
          {
             e_devicemgr->watched_clients =
               eina_list_remove_list(e_devicemgr->watched_clients, list);
             E_FREE(gdata);

             break;
          }
     }

   _e_devicemgr_inputgen_client_device_remove(client, ECORE_DEVICE_CLASS_KEYBOARD);
   _e_devicemgr_inputgen_client_device_remove(client, ECORE_DEVICE_CLASS_MOUSE);
   _e_devicemgr_inputgen_client_device_remove(client, ECORE_DEVICE_CLASS_TOUCH);

   EINA_LIST_FOREACH_SAFE(e_devicemgr->inputgen.resource_list, list, l_next, rdata)
     {
        if (wl_resource_get_client(rdata->resource) == client)
          {
             e_devicemgr->inputgen.resource_list =
               eina_list_remove_list(e_devicemgr->inputgen.resource_list, list);
             E_FREE(rdata);
          }
     }
}

static void
_e_devicemgr_inputgen_client_add(struct wl_client *client, unsigned int clas)
{
   struct wl_listener *destroy_listener;
   E_Devicemgr_Inputgen_Client_Global_Data *data;
   Eina_List *l;

   EINA_LIST_FOREACH(e_devicemgr->watched_clients, l, data)
     {
        if (data->client == client)
          {
             data->clas |= clas;
             return;
          }
     }
   data = NULL;

   destroy_listener = E_NEW(struct wl_listener, 1);
   EINA_SAFETY_ON_NULL_GOTO(destroy_listener, failed);
   destroy_listener->notify = _e_devicemgr_inputgen_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);

   data = E_NEW(E_Devicemgr_Inputgen_Client_Global_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(data, failed);

   data->client = client;
   data->clas = clas;

   e_devicemgr->watched_clients =
      eina_list_append(e_devicemgr->watched_clients, data);

   return;

failed:
   if (destroy_listener)
     {
        wl_list_remove(&destroy_listener->link);
        E_FREE(destroy_listener);
     }
}

static void
_e_devicemgr_inputgen_client_del(struct wl_client *client, unsigned int clas)
{
   E_Devicemgr_Inputgen_Client_Global_Data *data;
   E_Devicemgr_Inputgen_Client_Data *cdata;
   E_Devicemgr_Inputgen_Device_Data *ddata;
   Eina_List *l, *ll, *l_next, *ll_next, **list;

   list = _e_devicemgr_inputgen_list_get(clas);
   EINA_SAFETY_ON_NULL_RETURN(list);

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, ddata)
     {
        EINA_LIST_FOREACH_SAFE(ddata->clients, ll, ll_next, cdata)
          {
             if (cdata->client == client)
               {
                  cdata->ref--;
                  if (cdata->ref <= 0)
                    {
                       ddata->clients = eina_list_remove_list(ddata->clients, ll);
                       E_FREE(cdata);
                    }
               }
          }

        if (eina_list_count(ddata->clients) == 0)
          {
             ddata->clients = NULL;
             _e_devicemgr_inputgen_remove_device(ddata);
             *list = eina_list_remove_list(*list, l);
             E_FREE(ddata);
          }
     }

   EINA_LIST_FOREACH(e_devicemgr->watched_clients, l, data)
     {
        if (data->client == client)
          {
             data->clas &= ~clas;
          }
     }
}

static void
_e_devicemgr_inputgen_resource_add(struct wl_resource *resource, const char *name)
{
   E_Devicemgr_Inputgen_Resource_Data *rdata;
   Eina_List *l;

   EINA_LIST_FOREACH(e_devicemgr->inputgen.resource_list, l, rdata)
     {
        if (rdata->resource == resource) return;
     }

   rdata = NULL;
   rdata = E_NEW(E_Devicemgr_Inputgen_Resource_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN(rdata);

   rdata->resource = resource;
   strncpy(rdata->name, name, UINPUT_MAX_NAME_SIZE - 1);

   e_devicemgr->inputgen.resource_list = eina_list_append(e_devicemgr->inputgen.resource_list, rdata);
}

static void
_e_devicemgr_inputgen_resource_del(struct wl_resource *resource)
{
   E_Devicemgr_Inputgen_Resource_Data *rdata;
   Eina_List *l, *l_next;

   EINA_LIST_FOREACH_SAFE(e_devicemgr->inputgen.resource_list, l, l_next, rdata)
     {
        if (rdata->resource == resource)
          {
             e_devicemgr->inputgen.resource_list =
               eina_list_remove_list(e_devicemgr->inputgen.resource_list, l);
             E_FREE(rdata);
          }
     }
}

int
_e_devicemgr_inputgen_create_device(Ecore_Device_Class clas, struct wl_client *client, const char *device_name)
{
   int uinp_fd = -1;
   Eina_List **dev_list, *l, *ll;
   E_Devicemgr_Inputgen_Client_Data *cdata;
   E_Devicemgr_Inputgen_Device_Data *ddata, *device = NULL;
   Eina_Bool exist_device_flag = EINA_FALSE;
   int i;

   dev_list = _e_devicemgr_inputgen_list_get(clas);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev_list, EINA_FALSE);

   EINA_LIST_FOREACH(*dev_list, l, ddata)
     {
        if (!strncmp(ddata->name, device_name, UINPUT_MAX_NAME_SIZE))
          {
             EINA_LIST_FOREACH(ddata->clients, ll, cdata)
               {
                  if (cdata->client == client)
                    {
                       cdata->ref++;
                       return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
                    }
               }

             device = ddata;
             exist_device_flag = EINA_TRUE;
             break;
          }
     }

   if (!device)
     {
        device = E_NEW(E_Devicemgr_Inputgen_Device_Data, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(device, TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES);
        strncpy(device->name, device_name, UINPUT_MAX_NAME_SIZE - 1);
     }

   cdata = E_NEW(E_Devicemgr_Inputgen_Client_Data, 1);
   if(!cdata)
     {
        if (!exist_device_flag) E_FREE(device);
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES;
     }

   cdata->ref = 1;
   cdata->client = client;

   device->clients = eina_list_append(device->clients, cdata);

   if (exist_device_flag)
     return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

   uinp_fd = e_devicemgr_create_virtual_device(clas, device_name);

   if (uinp_fd < 0)
     goto fail_create_device;

   device->uinp_fd = uinp_fd;
   *dev_list = eina_list_append(*dev_list, device);

   device->clas = clas;

   for (i = 0; i < INPUTGEN_MAX_TOUCH; i++)
     {
        device->touch.coords[i].x = -1;
        device->touch.coords[i].y = -1;
     }

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

fail_create_device:
   EINA_LIST_FREE(device->clients, cdata)
     E_FREE(cdata);
   E_FREE(device);
   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NO_SYSTEM_RESOURCES;
}

int
e_devicemgr_inputgen_add(struct wl_client *client, struct wl_resource *resource, uint32_t clas, const char *name)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;

   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD)
     ret = _e_devicemgr_inputgen_create_device(ECORE_DEVICE_CLASS_KEYBOARD, client, name);
   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE)
     ret = _e_devicemgr_inputgen_create_device(ECORE_DEVICE_CLASS_MOUSE, client, name);
   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN)
     ret = _e_devicemgr_inputgen_create_device(ECORE_DEVICE_CLASS_TOUCH, client, name);

   if (ret == TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE)
     {
        _e_devicemgr_inputgen_client_add(client, clas);
        _e_devicemgr_inputgen_resource_add(resource, name);
     }

   return ret;
}

void
e_devicemgr_inputgen_remove(struct wl_client *client, struct wl_resource *resource, uint32_t clas)
{
   Eina_List *l, *l_next;
   E_Devicemgr_Inputgen_Client_Global_Data *data;
   struct wl_listener *listener;

   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD)
     _e_devicemgr_inputgen_client_del(client, TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD);
   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE)
     _e_devicemgr_inputgen_client_del(client, TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE);
   if (clas & TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN)
     _e_devicemgr_inputgen_client_del(client, TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN);

   EINA_LIST_FOREACH_SAFE(e_devicemgr->watched_clients, l, l_next, data)
     {
        if (data && data->client == client && !data->clas)
          {
             listener = wl_client_get_destroy_listener(client,
                        _e_devicemgr_inputgen_client_cb_destroy);
             if (listener)
               {
                  wl_list_remove(&listener->link);
                  E_FREE(listener);
               }

             e_devicemgr->watched_clients =
                eina_list_remove_list(e_devicemgr->watched_clients, l);
             E_FREE(data);

             break;
          }
     }

   _e_devicemgr_inputgen_resource_del(resource);
}


int
e_devicemgr_inputgen_generate_key(struct wl_client *client, struct wl_resource *resource, const char *keyname, Eina_Bool pressed)
{
   Eina_List *l, *l_next;
   E_Devicemgr_Inputgen_Device_Data *ddata;
   char *name, *identifier = NULL;
   Eina_Stringshare *name_data;
   int ret;

   name = _e_devicemgr_inputgen_name_get(resource);

   if (!_e_devicemgr_inputgen_device_check(name, ECORE_DEVICE_CLASS_KEYBOARD))
     {
        DMWRN("generate is not init\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }
   if (!e_comp_wl->xkb.keymap)
     {
        DMWRN("keymap is not ready\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }

   EINA_LIST_FOREACH(e_devicemgr->inputgen.kbd_list, l, ddata)
     {
        if (!strncmp(ddata->name, name, UINPUT_MAX_NAME_SIZE))
          {
             identifier = ddata->identifier;
             break;
          }
     }

   if (ddata)
     {
        if (pressed)
          {
             ddata->key.pressed = eina_list_append(ddata->key.pressed, eina_stringshare_add(keyname));
          }
        else
          {
             EINA_LIST_FOREACH_SAFE(ddata->key.pressed, l, l_next, name_data)
               {
                  if (e_devicemgr_strcmp(name_data, keyname))
                    {
                       ddata->key.pressed = eina_list_remove_list(ddata->key.pressed, l);
                       eina_stringshare_del(name_data);
                    }
               }
          }
     }

   ret = _e_devicemgr_inputgen_key_event_add(keyname, pressed, identifier);
   return ret;
}

int
e_devicemgr_inputgen_generate_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t button)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
   Eina_Bool state;
   Eina_List *l;
   E_Devicemgr_Inputgen_Device_Data *ddata = NULL;
   char *name, *identifier = NULL;

   if (0 >= button || button >= INPUTGEN_MAX_BTN)
     {
        DMWRN("Invalid button: %d\n", button);
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }

   name = _e_devicemgr_inputgen_name_get(resource);

   if (!_e_devicemgr_inputgen_device_check(name, ECORE_DEVICE_CLASS_MOUSE))
     {
        DMWRN("generate is not init\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }

   EINA_LIST_FOREACH(e_devicemgr->inputgen.ptr_list, l, ddata)
     {
        if (!strncmp(ddata->name, name, UINPUT_MAX_NAME_SIZE))
          {
             identifier = ddata->identifier;
             break;
          }
     }

   if (type == TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_UPDATE)
     {
        ret = _e_devicemgr_inputgen_mouse_move_event(x, y, identifier);
        if (ddata)
          {
             ddata->mouse.coords.x = x;
             ddata->mouse.coords.y = y;
          }
     }
   else
     {
        state = (type == TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_BEGIN) ?
                EINA_TRUE : EINA_FALSE;
        if (ddata)
          {
             if (state)  ddata->mouse.pressed |= 1 << (button - 1);
             else ddata->touch.pressed &= ~(1 << (button - 1));
             ddata->mouse.coords.x = x;
             ddata->mouse.coords.y = y;
          }
        ret = _e_devicemgr_inputgen_mouse_button_event(state, x, y, button, identifier);
     }

   return ret;
}

int
e_devicemgr_inputgen_generate_touch(struct wl_client *client, struct wl_resource *resource, uint32_t type, uint32_t x, uint32_t y, uint32_t finger)
{
   int ret = TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
   Eina_List *l;
   E_Devicemgr_Inputgen_Device_Data *ddata = NULL;
   char *name, *identifier = NULL;

   name = _e_devicemgr_inputgen_name_get(resource);

   if (finger >= INPUTGEN_MAX_TOUCH)
     {
        DMWRN("Invalid fingers: %d\n", finger);
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }

   if (!_e_devicemgr_inputgen_device_check(name, ECORE_DEVICE_CLASS_TOUCH))
     {
        DMWRN("generate is not init\n");
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_PARAMETER;
     }

   EINA_LIST_FOREACH(e_devicemgr->inputgen.touch_list, l, ddata)
     {
        if (!strncmp(ddata->name, name, UINPUT_MAX_NAME_SIZE))
          {
             identifier = ddata->identifier;
             break;
          }
     }

   switch(type)
     {
        case TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_BEGIN:
           ret = _e_devicemgr_inputgen_touch_update_event(x, y, finger, identifier);
           ret = _e_devicemgr_inputgen_touch_event(type, x, y, finger, identifier);
           if (ddata)
             {
                ddata->touch.pressed |= 1 << finger;
                ddata->touch.coords[finger].x = x;
                ddata->touch.coords[finger].y = y;
             }
           break;
        case TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_END:
           ret = _e_devicemgr_inputgen_touch_event(type, x, y, finger, identifier);
           if (ddata)
             {
                ddata->touch.pressed &= ~(1 << finger);
                ddata->touch.coords[finger].x = -1;
                ddata->touch.coords[finger].y = -1;
             }
           break;
        case TIZEN_INPUT_DEVICE_MANAGER_POINTER_EVENT_TYPE_UPDATE:
           ret = _e_devicemgr_inputgen_touch_update_event(x, y, finger, identifier);
           if (ddata)
             {
                ddata->touch.coords[finger].x = x;
                ddata->touch.coords[finger].y = y;
             }
           break;
     }

   return ret;
}


void
e_devicemgr_inputgen_get_device_info(E_Devicemgr_Input_Device *dev)
{
   E_Devicemgr_Inputgen_Device_Data *ddata;
   Eina_List **dev_list, *l;

   if (dev->clas == ECORE_DEVICE_CLASS_NONE ||
       dev->clas == ECORE_DEVICE_CLASS_SEAT)
     return;

   dev_list = _e_devicemgr_inputgen_list_get(dev->clas);
   EINA_SAFETY_ON_NULL_RETURN(dev_list);

   EINA_LIST_FOREACH(*dev_list, l, ddata)
     {
        if (!strncmp(ddata->name, dev->name, strlen(dev->name)))
          {
             if (!ddata->identifier)
               {
                  ddata->identifier = (char *)eina_stringshare_add(dev->identifier);
               }
          }
     }
}


#define DM_IOCTL_SET_BIT(fd, bit, val) \
    ret = ioctl(fd, bit, val); \
    if (ret) DMWRN("Failed to set %s to fd(%d) (ret: %d)\n", #val, fd, ret)

int
e_devicemgr_create_virtual_device(Ecore_Device_Class clas, const char *name)
{
   int ret;
   int uinp_fd = -1;
   struct uinput_user_dev uinp;

   memset(&uinp, 0, sizeof(uinp));
   strncpy(uinp.name, name, UINPUT_MAX_NAME_SIZE - 1);
   uinp.id.version = 4;
   uinp.id.bustype = BUS_VIRTUAL;

   uinp_fd = open("/dev/uinput", O_WRONLY | O_NDELAY);
   if (uinp_fd < 0)
     {
        DMWRN("Failed to open /dev/uinput: (%d)\n", uinp_fd);
        goto fail_create_device;
     }

   if (ECORE_DEVICE_CLASS_KEYBOARD == clas)
     {
       /* key device setup */
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_KEY);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_SYN);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_MSC);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_MSCBIT, MSC_SCAN);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_KEYBIT, KEY_ESC);
     }
   else if (ECORE_DEVICE_CLASS_MOUSE == clas)
     {
       /* mouse device setup */
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_KEY);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_SYN);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_MSC);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_REL);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_MSCBIT, MSC_SCAN);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_KEYBIT, BTN_LEFT);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_RELBIT, BTN_RIGHT);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_RELBIT, BTN_MIDDLE);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_RELBIT, REL_X);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_RELBIT, REL_Y);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_RELBIT, REL_WHEEL);
     }
   else if (ECORE_DEVICE_CLASS_TOUCH == clas)
     {
       /* touch device setup */
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_KEY);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_SYN);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_MSC);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_EVBIT, EV_ABS);

       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_KEYBIT, BTN_TOUCH);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_X);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_Y);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_SLOT);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MAJOR);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_TOUCH_MINOR);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_WIDTH_MAJOR);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
       DM_IOCTL_SET_BIT(uinp_fd, UI_SET_MSCBIT, MSC_SCAN);

       uinp.absmin[ABS_X] = 0;
       uinp.absmax[ABS_X] = 1000;
       uinp.absmin[ABS_Y] = 0;
       uinp.absmax[ABS_Y] = 1000;
       uinp.absmin[ABS_MT_SLOT] = 0;
       uinp.absmax[ABS_MT_SLOT] = INPUTGEN_MAX_TOUCH - 1;
       uinp.absmin[ABS_MT_TOUCH_MAJOR] = 0;
       uinp.absmax[ABS_MT_TOUCH_MAJOR] = 255;
       uinp.absmin[ABS_MT_TOUCH_MINOR] = 0;
       uinp.absmax[ABS_MT_TOUCH_MINOR] = 255;
       uinp.absmin[ABS_MT_WIDTH_MAJOR] = 0;
       uinp.absmax[ABS_MT_WIDTH_MAJOR] = 255;
       uinp.absmin[ABS_MT_POSITION_X] = 0;
       uinp.absmax[ABS_MT_POSITION_X] = 1000;
       uinp.absmin[ABS_MT_POSITION_Y] = 0;
       uinp.absmax[ABS_MT_POSITION_Y] = 1000;
       uinp.absmin[ABS_MT_TRACKING_ID] = 0;
       uinp.absmax[ABS_MT_TRACKING_ID] = 65535;
       uinp.absmin[ABS_MT_ORIENTATION] = 0;
       uinp.absmax[ABS_MT_ORIENTATION] = 2;
     }
   else
     goto fail_create_device;

   ret = write(uinp_fd, &uinp, sizeof(struct uinput_user_dev));

   if (ret < 0)
     {
        DMWRN("Failed to write to uinput fd ! (fd:%d, type:%d, name:%s)\n", uinp_fd, clas, name);
        goto fail_create_device;
     }

   if (ioctl(uinp_fd, UI_DEV_CREATE))
     {
       DMWRN("Failed to create a virtual device ! (type:%d, name:%s)\n", clas, name);
       goto fail_create_device;
     }

   return uinp_fd;

fail_create_device:

   if (uinp_fd >= 0)
     close(uinp_fd);

   return -1;
}

void
e_devicemgr_destroy_virtual_device(int uinp_fd)
{
   int ret;
   ret = ioctl(uinp_fd, UI_DEV_DESTROY, NULL);
   if (ret) DMWRN("Failed destroy fd: %d (ret: %d)\n", uinp_fd, ret);
   close(uinp_fd);
}
