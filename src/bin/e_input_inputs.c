#include "e.h"
#include "e_input_private.h"

static E_Input_Seat *
_seat_create(E_Input_Backend *input, const char *seat)
{
   E_Input_Seat *s;

   /* try to allocate space for new seat */
   if (!(s = calloc(1, sizeof(E_Input_Seat))))
     return NULL;

   s->input = input;
   s->name = eina_stringshare_add(seat);

   /* add this new seat to list */
   input->dev->seats = eina_list_append(input->dev->seats, s);

   ecore_event_add(E_INPUT_EVENT_SEAT_ADD, NULL, NULL, NULL);

   return s;
}

static void
_e_input_event_input_device_add_free(void *data EINA_UNUSED, void *ev)
{
   E_Input_Event_Input_Device_Add *e;

   e = ev;
   eina_stringshare_del(e->name);
   eina_stringshare_del(e->sysname);
   eina_stringshare_del(e->seatname);

   free(e);
}

static void
_e_input_event_input_device_del_free(void *data EINA_UNUSED, void *ev)
{
   E_Input_Event_Input_Device_Del *e;

   e = ev;
   eina_stringshare_del(e->name);
   eina_stringshare_del(e->sysname);
   eina_stringshare_del(e->seatname);

   free(e);
}

static E_Input_Seat *
_seat_get(E_Input_Backend *input, const char *seat)
{
   E_Input_Seat *s;
   Eina_List *l;

   /* search for this name in existing seats */
   EINA_LIST_FOREACH(input->dev->seats, l, s)
     if (!strcmp(s->name, seat))
       return s;

   return _seat_create(input, seat);
}

static void
_ecore_event_device_info_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Device_Info *e;

   e = ev;
   eina_stringshare_del(e->name);
   eina_stringshare_del(e->identifier);
   eina_stringshare_del(e->seatname);

   free(e);
}

static Ecore_Device_Class
_e_input_seat_cap_to_ecore_device_class(unsigned int cap)
{
   switch(cap)
     {
      case E_INPUT_SEAT_POINTER:
         return ECORE_DEVICE_CLASS_MOUSE;
      case E_INPUT_SEAT_KEYBOARD:
         return ECORE_DEVICE_CLASS_KEYBOARD;
      case E_INPUT_SEAT_TOUCH:
         return ECORE_DEVICE_CLASS_TOUCH;
      default:
         return ECORE_DEVICE_CLASS_NONE;
     }
   return ECORE_DEVICE_CLASS_NONE;
}

void
_e_input_send_device_info(unsigned int window, E_Input_Evdev *edev, Ecore_Device_Class clas, Ecore_Device_Subclass subclas, Eina_Bool flag)
{
   Ecore_Event_Device_Info *e;

   if (!(e = calloc(1, sizeof(Ecore_Event_Device_Info)))) return;

   e->name = eina_stringshare_add(libinput_device_get_name(edev->device));
   e->identifier = eina_stringshare_add(edev->path);
   e->seatname = eina_stringshare_add(edev->seat->name);
   e->clas = clas;
   e->subclas = subclas;
   e->window = window;

   if (flag)
     ecore_event_add(ECORE_EVENT_DEVICE_ADD, e, _ecore_event_device_info_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_DEVICE_DEL, e, _ecore_event_device_info_free, NULL);
}

static Eina_Bool
_e_input_add_ecore_device(E_Input_Evdev *edev, Ecore_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Ecore_Device *dev = NULL;
   const char *identifier;

   if (!edev->path) return EINA_FALSE;

   dev_list = ecore_device_list();
   if (dev_list)
     {
        EINA_LIST_FOREACH(dev_list, l, dev)
          {
             if (!dev) continue;
             identifier = ecore_device_identifier_get(dev);
             if (!identifier) continue;
             if ((ecore_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
                return EINA_FALSE;
          }
     }

   if(!(dev = ecore_device_add())) return EINA_FALSE;

   ecore_device_name_set(dev, libinput_device_get_name(edev->device));
   ecore_device_description_set(dev, libinput_device_get_name(edev->device));
   ecore_device_identifier_set(dev, edev->path);
   ecore_device_class_set(dev, clas);
   return EINA_TRUE;
}

static Eina_Bool
_e_input_remove_ecore_device(E_Input_Evdev *edev, Ecore_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Ecore_Device *dev = NULL;
   const char *identifier;

   if (!edev->path) return EINA_FALSE;

   dev_list = ecore_device_list();
   if (!dev_list) return EINA_FALSE;
   EINA_LIST_FOREACH(dev_list, l, dev)
      {
         if (!dev) continue;
         identifier = ecore_device_identifier_get(dev);
         if (!identifier) continue;
         if ((ecore_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
           {
              ecore_device_del(dev);
              return EINA_TRUE;
           }
      }
   return EINA_FALSE;
}

void
_e_input_device_add(unsigned int window, E_Input_Evdev *edev)
{
   Eina_Bool ret = EINA_FALSE;
   Ecore_Device_Class clas;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_POINTER);
        ret = _e_input_add_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 1);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_KEYBOARD);
        ret = _e_input_add_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 1);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_TOUCH);
        ret = _e_input_add_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 1);
     }
}

void
_e_input_device_remove(unsigned int window, E_Input_Evdev *edev)
{
   Eina_Bool ret = EINA_FALSE;
   Ecore_Device_Class clas;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_POINTER);
        ret = _e_input_remove_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 0);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_KEYBOARD);
        ret = _e_input_remove_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 0);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_TOUCH);
        ret = _e_input_remove_ecore_device(edev, clas);
        if (ret) _e_input_send_device_info(window, edev, clas, ECORE_DEVICE_SUBCLASS_NONE, 0);
     }
}

static void
_device_added(E_Input_Backend *input, struct libinput_device *device)
{
   struct libinput_seat *libinput_seat;
   const char *seat_name;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;
   E_Input_Event_Input_Device_Add *ev;

   libinput_seat = libinput_device_get_seat(device);
   seat_name = libinput_seat_get_logical_name(libinput_seat);

   /* try to get a seat */
   if (!(seat = _seat_get(input, seat_name)))
     {
        ERR("Could not get matching seat: %s", seat_name);
        return;
     }

   /* try to create a new evdev device */
   if (!(edev = _e_input_evdev_device_create(seat, device)))
     {
        ERR("Failed to create new evdev device");
        return;
     }

   edev->fd = (int)(intptr_t)eina_hash_find(input->dev->fd_hash, edev->path);

   /* append this device to the seat */
   seat->devices = eina_list_append(seat->devices, edev);

   ev = calloc(1, sizeof(E_Input_Event_Input_Device_Add));
   if (!ev)
     {
        return;
     }

   ev->name = eina_stringshare_add(libinput_device_get_name(device));
   ev->sysname = eina_stringshare_add(edev->path);
   ev->seatname = eina_stringshare_add(edev->seat->name);
   ev->caps = edev->caps;

   ecore_event_add(E_INPUT_EVENT_INPUT_DEVICE_ADD,
                   ev,
                   _e_input_event_input_device_add_free,
                   NULL);

   if (input->dev->window != -1)
     _e_input_device_add(input->dev->window, edev);
}

static void
_device_removed(E_Input_Backend *input, struct libinput_device *device)
{
   E_Input_Evdev *edev;
   E_Input_Event_Input_Device_Del *ev;

   /* try to get the evdev structure */
   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   ev = calloc(1, sizeof(E_Input_Event_Input_Device_Del));
   if (!ev)
     {
        return;
     }

   ev->name = eina_stringshare_add(libinput_device_get_name(device));
   ev->sysname = eina_stringshare_add(edev->path);
   ev->seatname = eina_stringshare_add(edev->seat->name);
   ev->caps = edev->caps;

   ecore_event_add(E_INPUT_EVENT_INPUT_DEVICE_DEL,
                   ev,
                   _e_input_event_input_device_del_free,
                   NULL);

   if (input->dev->window != -1)
     _e_input_device_remove(input->dev->window, edev);

   /* remove this evdev from the seat's list of devices */
   edev->seat->devices = eina_list_remove(edev->seat->devices, edev);

   if (input->dev->fd_hash)
     eina_hash_del_by_key(input->dev->fd_hash, edev->path);

   /* tell launcher to release device */
   if (edev->fd >= 0)
     {
        close(edev->fd);
        edev->fd = -1;
     }

   /* destroy this evdev */
   _e_input_evdev_device_destroy(edev);
}

static int
_udev_event_process(struct libinput_event *event)
{
   struct libinput *libinput;
   struct libinput_device *device;
   E_Input_Backend *input;
   Eina_Bool ret = EINA_TRUE;

   libinput = libinput_event_get_context(event);
   input = libinput_get_user_data(libinput);
   device = libinput_event_get_device(event);

   switch (libinput_event_get_type(event))
     {
      case LIBINPUT_EVENT_DEVICE_ADDED:
        _device_added(input, device);
        break;
      case LIBINPUT_EVENT_DEVICE_REMOVED:
        _device_removed(input, device);
        break;
      default:
        ret = EINA_FALSE;
     }

   return ret;
}

static void
_input_event_process(struct libinput_event *event)
{
   if (_udev_event_process(event)) return;
   if (_e_input_evdev_event_process(event)) return;
}

void
_input_events_process(E_Input_Backend *input)
{
   struct libinput_event *event;

   while ((event = libinput_get_event(input->libinput)))
     {
        _input_event_process(event);
        libinput_event_destroy(event);
     }
}

static Eina_Bool
_cb_input_dispatch(void *data, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   E_Input_Backend *input;

   if (!(input = data)) return EINA_TRUE;

   if (libinput_dispatch(input->libinput) != 0)
     ERR("Failed to dispatch libinput events: %m");

   /* process pending events */
   _input_events_process(input);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_input_enable_input(E_Input_Backend *input)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(input, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(input->libinput, EINA_FALSE);

   input->fd = libinput_get_fd(input->libinput);

   if (!input->hdlr)
     {
        input->hdlr =
          ecore_main_fd_handler_add(input->fd, ECORE_FD_READ,
                                    _cb_input_dispatch, input, NULL, NULL);
     }

   if (input->suspended)
     {
        if (libinput_resume(input->libinput) != 0)
          goto err;

        input->suspended = EINA_FALSE;

        /* process pending events */
        _input_events_process(input);
     }

   input->enabled = EINA_TRUE;
   input->suspended = EINA_FALSE;

   return EINA_TRUE;

err:
   input->enabled = EINA_FALSE;
   if (input->hdlr)
     ecore_main_fd_handler_del(input->hdlr);
   input->hdlr = NULL;

   return EINA_FALSE;
}

EINTERN void
e_input_disable_input(E_Input_Backend *input)
{
   EINA_SAFETY_ON_NULL_RETURN(input);
   EINA_SAFETY_ON_TRUE_RETURN(input->suspended);

   /* suspend this input */
   libinput_suspend(input->libinput);

   /* process pending events */
   _input_events_process(input);

   input->suspended = EINA_TRUE;
}

E_API Eina_List *
e_input_seat_evdev_list_get(E_Input_Seat *seat)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(seat, NULL);
   return seat->devices;
}