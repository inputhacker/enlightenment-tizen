#include "e_devicemgr_private.h"

Eina_Bool
e_devicemgr_block_check_keyboard(Ecore_Event_Key *ev, Eina_Bool pressed)
{
   Eina_List *l, *l_next;
   int *keycode, *data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   if (e_devicemgr->block.devtype & TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD)
     {
        if (!pressed)
          {
             EINA_LIST_FOREACH_SAFE(e_devicemgr->pressed_keys, l, l_next, data)
               {
                  if (ev->keycode == *data)
                    {
                       DMERR("%d is already press key. Propagate this key event.\n", *data);
                       e_devicemgr->pressed_keys = eina_list_remove_list(e_devicemgr->pressed_keys, l);
                       E_FREE(data);
                       return ECORE_CALLBACK_PASS_ON;
                    }
               }
          }
        return ECORE_CALLBACK_DONE;
     }

   if (pressed)
     {
        keycode = E_NEW(int, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(keycode, ECORE_CALLBACK_PASS_ON);

        *keycode = ev->keycode;

        EINA_LIST_FOREACH(e_devicemgr->pressed_keys, l, data)
          {
             if (*data == *keycode)
               {
                  E_FREE(keycode);
                  return ECORE_CALLBACK_PASS_ON;
               }
          }
        e_devicemgr->pressed_keys = eina_list_append(e_devicemgr->pressed_keys, keycode);
     }
   else
     {
        EINA_LIST_FOREACH_SAFE(e_devicemgr->pressed_keys, l, l_next, data)
          {
             if (ev->keycode == *data)
               {
                  e_devicemgr->pressed_keys = eina_list_remove_list(e_devicemgr->pressed_keys, l);
                  E_FREE(data);
               }
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

Eina_Bool
e_devicemgr_block_check_button(Ecore_Event_Mouse_Button *ev, Eina_Bool pressed)
{
   Ecore_Device *dev;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   dev = ev->dev;
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, ECORE_CALLBACK_PASS_ON);

   if (ecore_device_class_get(dev) == ECORE_DEVICE_CLASS_MOUSE)
     {
        if (e_devicemgr->block.devtype & TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE)
          {
             if (!pressed)
               {
                  if (e_devicemgr->pressed_button & (1 << ev->buttons))
                    {
                       e_devicemgr->pressed_button &= ~(1 << ev->buttons);
                       return ECORE_CALLBACK_PASS_ON;
                    }
               }
             return ECORE_CALLBACK_DONE;
          }

        if (pressed)
          {
             e_devicemgr->pressed_button |= (1 << ev->buttons);
          }
        else
          e_devicemgr->pressed_button &= ~(1 << ev->buttons);
     }
   else if (ecore_device_class_get(dev) == ECORE_DEVICE_CLASS_TOUCH)
     {
        if (e_devicemgr->block.devtype & TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN)
          {
             if (!pressed)
               {
                  if (e_devicemgr->pressed_finger & (1 << ev->multi.device))
                    {
                       e_devicemgr->pressed_finger &= ~(1 << ev->multi.device);
                       return ECORE_CALLBACK_PASS_ON;
                    }
               }
             return ECORE_CALLBACK_DONE;
          }

        if (pressed)
          {
             e_devicemgr->pressed_finger |= (1 << ev->multi.device);
          }
        else
          e_devicemgr->pressed_finger &= ~(1 << ev->multi.device);
     }

   return ECORE_CALLBACK_PASS_ON;
}

Eina_Bool
e_devicemgr_block_check_move(Ecore_Event_Mouse_Move *ev)
{
   Ecore_Device *dev;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   dev = ev->dev;
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, ECORE_CALLBACK_PASS_ON);

   if (ecore_device_class_get(dev) == ECORE_DEVICE_CLASS_MOUSE)
     {
        if (e_devicemgr->block.devtype & TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE)
          {
             return ECORE_CALLBACK_DONE;
          }
     }
   else if (ecore_device_class_get(dev) == ECORE_DEVICE_CLASS_TOUCH)
     {
        if (e_devicemgr->block.devtype & TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN)
          {
             return ECORE_CALLBACK_DONE;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_devicemgr_block_client_remove(struct wl_client *client)
{
   if (client != e_devicemgr->block.client) return;

   e_devicemgr->block.devtype = 0x0;
   if (e_devicemgr->block.duration_timer)
     {
        ecore_timer_del(e_devicemgr->block.duration_timer);
        e_devicemgr->block.duration_timer = NULL;
     }
   e_devicemgr->block.client = NULL;
}

static Eina_Bool
_e_devicemgr_block_timer(void *data)
{
   struct wl_resource *resource = (struct wl_resource *)data;
   struct wl_client *client = wl_resource_get_client(resource);

   if ((e_devicemgr->block.client) && (e_devicemgr->block.client != client))
     {
        return ECORE_CALLBACK_CANCEL;
     }

   _e_devicemgr_block_client_remove(client);
   e_devicemgr_wl_block_send_expired(resource);

   return ECORE_CALLBACK_CANCEL;
}

static void
_e_devicemgr_block_client_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_client *client = (struct wl_client *)data;

   if (!e_devicemgr->block.client) return;

   wl_list_remove(&l->link);
   E_FREE(l);

   _e_devicemgr_block_client_remove(client);
}

static void
_e_devicemgr_block_client_add(struct wl_client *client, struct wl_resource *resource, uint32_t clas, uint32_t duration)
{
   struct wl_listener *destroy_listener = NULL;
   double milli_duration = (double)(duration) / 1000.0;

   /* Last request of block can renew timer time */
   if (e_devicemgr->block.duration_timer)
     ecore_timer_del(e_devicemgr->block.duration_timer);
   e_devicemgr->block.duration_timer = ecore_timer_add(milli_duration, _e_devicemgr_block_timer, resource);

   e_devicemgr->block.devtype |= clas;

   if (e_devicemgr->block.client) return;
   e_devicemgr->block.client = client;

   destroy_listener = E_NEW(struct wl_listener, 1);
   EINA_SAFETY_ON_NULL_GOTO(destroy_listener, failed);
   destroy_listener->notify = _e_devicemgr_block_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);

   return;

failed:
   ecore_timer_del(e_devicemgr->block.duration_timer);
   e_devicemgr->block.duration_timer = NULL;
   e_devicemgr->block.client = NULL;
}

int
e_devicemgr_block_add(struct wl_client *client, struct wl_resource *resource, uint32_t clas, uint32_t duration)
{
   uint32_t all_class = TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE |
                        TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD |
                        TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN;

   if ((e_devicemgr->block.client) && (e_devicemgr->block.client != client))
     {
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_BLOCKED_ALREADY;
     }
   if (!(clas & all_class))
     {
        return TIZEN_INPUT_DEVICE_MANAGER_ERROR_INVALID_CLASS;
     }

   _e_devicemgr_block_client_add(client, resource, clas, duration);

   /* TODO: Release pressed button or key */

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}

int
e_devicemgr_block_remove(struct wl_client *client)
{
   if ((e_devicemgr->block.client) && (e_devicemgr->block.client != client))
    {
       return TIZEN_INPUT_DEVICE_MANAGER_ERROR_BLOCKED_ALREADY;
    }

   _e_devicemgr_block_client_remove(client);

   return TIZEN_INPUT_DEVICE_MANAGER_ERROR_NONE;
}
