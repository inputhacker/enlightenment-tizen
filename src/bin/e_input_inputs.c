#include "e.h"
#include "e_input_private.h"

static void
_device_close(const char *device, int fd)
{
   if (fd >= 0)
     close(fd);
}

static E_Input_Seat *
_seat_create(E_Input_Backend *input, const char *seat)
{
   E_Input_Seat *s;
   Evas *evs = NULL;
   E_Input *ei = NULL;
   Ecore_Evas *ee = NULL;
   Evas_Device *evas_dev = NULL;

   ei = e_input_get();
   if (!ei) return NULL;

   ee = e_input_ecore_evas_get(ei);
   if (!ee) return NULL;

   evs = ecore_evas_get(ee);
   if (!evs) return NULL;

   /* create an evas device of a seat */
   evas_dev = evas_device_add_full(evs, "Enlightenment seat", s->name, NULL, NULL,
                                   EVAS_DEVICE_CLASS_SEAT, EVAS_DEVICE_SUBCLASS_NONE);
   if (!evas_dev)
     {
        ERR("Failed to create an evas device for a seat !\n");
		return NULL;
     }

   /* try to allocate space for new seat */
   if (!(s = calloc(1, sizeof(E_Input_Seat))))
     {
        evas_device_del(evas_dev);
        return NULL;
     }

   s->input = input;
   s->name = eina_stringshare_add(seat);
   s->evas_dev = evas_dev;

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
   eina_stringshare_del(e->identifier);

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
   eina_stringshare_del(e->identifier);

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

static Evas_Device_Class
_e_input_seat_cap_to_evas_device_class(unsigned int cap)
{
   switch(cap)
     {
      case E_INPUT_SEAT_POINTER:
         return EVAS_DEVICE_CLASS_MOUSE;
      case E_INPUT_SEAT_KEYBOARD:
         return EVAS_DEVICE_CLASS_KEYBOARD;
      case E_INPUT_SEAT_TOUCH:
         return EVAS_DEVICE_CLASS_TOUCH;
      default:
         return EVAS_DEVICE_CLASS_NONE;
     }
   return EVAS_DEVICE_CLASS_NONE;
}

static Eina_Bool
_e_input_add_evas_device(E_Input_Evdev *edev, Evas_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Evas_Device *dev = NULL;
   const char *identifier;

   Ecore_Evas *ee = NULL;
   E_Input *ei = NULL;
   Evas *evs = NULL;

   if (!edev || !edev->path) return EINA_FALSE;

   dev_list = evas_device_list(e_comp->evas, NULL);
   if (dev_list)
     {
        EINA_LIST_FOREACH(dev_list, l, dev)
          {
             if (!dev) continue;
             identifier = evas_device_description_get(dev);
             if (!identifier) continue;
             if ((evas_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
                return EINA_FALSE;
          }
     }

   ei = e_input_get();
   if (!ei) return EINA_FALSE;

   ee = e_input_ecore_evas_get(ei);
   if (!ee) return EINA_FALSE;

   evs = ecore_evas_get(ee);
   if (!evs) return EINA_FALSE;

   dev = evas_device_add_full(evs,libinput_device_get_name(edev->device),
                              edev->path, edev->seat->evas_dev , NULL, clas, EVAS_DEVICE_SUBCLASS_NONE);
   if (!dev)
     {
        edev->evas_dev = NULL;
        return EINA_FALSE;
     }

   edev->evas_dev = dev;

   return EINA_TRUE;
}

static Eina_Bool
_e_input_remove_evas_device(E_Input_Evdev *edev, Evas_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Evas_Device *dev = NULL;
   const char *identifier;

   if (!edev->path) return EINA_FALSE;

   dev_list = evas_device_list(e_comp->evas, NULL);
   if (!dev_list) return EINA_FALSE;
   EINA_LIST_FOREACH(dev_list, l, dev)
      {
         if (!dev) continue;
         identifier = evas_device_description_get(dev);
         if (!identifier) continue;
         if ((evas_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
           {
              evas_device_del(dev);
			  edev->evas_dev = NULL;
              return EINA_TRUE;
           }
      }
   return EINA_FALSE;
}

Eina_Bool
_e_input_device_add(E_Input_Evdev *edev)
{
   Eina_Bool ret = EINA_FALSE;
   Evas_Device_Class clas;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_POINTER);
        ret = _e_input_add_evas_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_KEYBOARD);
        ret = _e_input_add_evas_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_TOUCH);
        ret = _e_input_add_evas_device(edev, clas);
     }

   return ret;
}

void
_e_input_device_remove(E_Input_Evdev *edev)
{
   Eina_Bool ret = EINA_FALSE;
   Evas_Device_Class clas;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_POINTER);
        _e_input_remove_evas_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_KEYBOARD);
        _e_input_remove_evas_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_evas_device_class(E_INPUT_SEAT_TOUCH);
        _e_input_remove_evas_device(edev, clas);
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

   if (EINA_FALSE == _e_input_device_add(edev))
     {
        ERR("Failed to create evas device !\n");
		return;
     }

   ev->name = eina_stringshare_add(libinput_device_get_name(device));
   ev->sysname = eina_stringshare_add(edev->path);
   ev->seatname = eina_stringshare_add(edev->seat->name);
   ev->caps = edev->caps;
   ev->clas = evas_device_class_get(edev->evas_dev);
   ev->identifier = eina_stringshare_add(edev->path);
   ev->subclas = evas_device_subclass_get(edev->evas_dev);

   ecore_event_add(E_INPUT_EVENT_INPUT_DEVICE_ADD,
                   ev,
                   _e_input_event_input_device_add_free,
                   NULL);
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
   ev->clas = evas_device_class_get(edev->evas_dev);
   ev->identifier = eina_stringshare_add(edev->path);
   ev->subclas = evas_device_subclass_get(edev->evas_dev);

   ecore_event_add(E_INPUT_EVENT_INPUT_DEVICE_DEL,
                   ev,
                   _e_input_event_input_device_del_free,
                   NULL);

   _e_input_device_remove(edev);

   /* remove this evdev from the seat's list of devices */
   edev->seat->devices = eina_list_remove(edev->seat->devices, edev);

   if (input->dev->fd_hash)
     eina_hash_del_by_key(input->dev->fd_hash, edev->path);

   /* tell launcher to release device */
   if (edev->fd >= 0)
     _device_close(edev->path, edev->fd);

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
