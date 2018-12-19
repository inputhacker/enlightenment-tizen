#include "e.h"
#include "e_input_private.h"

static char *
_e_input_ecore_device_class_to_string(Ecore_Device_Class clas)
{
   switch (clas)
     {
        case ECORE_DEVICE_CLASS_NONE:
          return "None";
          break;
        case ECORE_DEVICE_CLASS_SEAT:
          return "Seat";
          break;
        case ECORE_DEVICE_CLASS_KEYBOARD:
          return "Keyboard";
          break;
        case ECORE_DEVICE_CLASS_MOUSE:
          return "Mouse";
          break;
        case ECORE_DEVICE_CLASS_TOUCH:
          return "Touch";
          break;
        case ECORE_DEVICE_CLASS_PEN:
          return "Pen";
          break;
        case ECORE_DEVICE_CLASS_WAND:
          return "Wand";
          break;
        case ECORE_DEVICE_CLASS_GAMEPAD:
          return "Gamepad";
          break;
        default:
          return "Unknown";
     }
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

static void
_e_input_ecore_device_info_free(void *data EINA_UNUSED, void *ev)
{
   Ecore_Event_Device_Info *e;

   e = ev;
   eina_stringshare_del(e->name);
   eina_stringshare_del(e->identifier);
   eina_stringshare_del(e->seatname);

   free(e);
}

void
_e_input_ecore_device_event(Ecore_Device *dev, Eina_Bool flag)
{
   Ecore_Event_Device_Info *e;
   E_Input *e_input;

   if (!(e = calloc(1, sizeof(Ecore_Event_Device_Info)))) return;

   e_input = e_input_get();

   e->window = e_input?e_input->window:(Ecore_Window)0;
   e->name = eina_stringshare_add(ecore_device_name_get(dev));
   e->identifier = eina_stringshare_add(ecore_device_identifier_get(dev));
   e->seatname = eina_stringshare_add(ecore_device_name_get(dev));
   e->clas = ecore_device_class_get(dev);
   e->subclas = ecore_device_subclass_get(dev);

   if (flag)
     ecore_event_add(ECORE_EVENT_DEVICE_ADD, e, _e_input_ecore_device_info_free, NULL);
   else
     ecore_event_add(ECORE_EVENT_DEVICE_DEL, e, _e_input_ecore_device_info_free, NULL);
}

static E_Input_Seat *
_seat_create(E_Input_Backend *input, const char *seat)
{
   E_Input_Seat *s;
   Ecore_Device *ecore_dev = NULL;

   /* create an evas device of a seat */
   ecore_dev = ecore_device_add();
   if (!ecore_dev)
     {
        ERR("Failed to create an ecore device for a seat !\n");
		return NULL;
     }

   ecore_device_name_set(ecore_dev, seat);
   ecore_device_identifier_set(ecore_dev, "Enlightenment seat");
   ecore_device_class_set(ecore_dev, ECORE_DEVICE_CLASS_SEAT);
   ecore_device_subclass_set(ecore_dev, ECORE_DEVICE_SUBCLASS_NONE);

   /* try to allocate space for new seat */
   if (!(s = calloc(1, sizeof(E_Input_Seat))))
     {
        ecore_device_del(ecore_dev);
        return NULL;
     }

   s->input = input;
   s->name = eina_stringshare_add(seat);
   s->ecore_dev = ecore_dev;

   /* add this new seat to list */
   input->dev->seats = eina_list_append(input->dev->seats, s);

   ecore_event_add(E_INPUT_EVENT_SEAT_ADD, NULL, NULL, NULL);

   _e_input_ecore_device_event(ecore_dev, EINA_TRUE);

   return s;
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

static Eina_Bool
_e_input_add_ecore_device(E_Input_Evdev *edev, Ecore_Device_Class clas)
{
   const Eina_List *dev_list = NULL;
   const Eina_List *l;
   Ecore_Device *dev = NULL;
   const char *identifier;

   if (!edev || !edev->path) return EINA_FALSE;

   dev_list = ecore_device_list();
   if (dev_list)
     {
        EINA_LIST_FOREACH(dev_list, l, dev)
          {
             if (!dev) continue;
             identifier = ecore_device_description_get(dev);
             if (!identifier) continue;
             if ((ecore_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
               return EINA_FALSE;
          }
     }

   dev = ecore_device_add();
   if (!dev)
     {
        edev->ecore_dev = NULL;
        return EINA_FALSE;
     }

   ecore_device_name_set(dev, libinput_device_get_name(edev->device));
   ecore_device_identifier_set(dev, edev->path);
   ecore_device_class_set(dev, clas);
   ecore_device_subclass_set(dev, ECORE_DEVICE_SUBCLASS_NONE);

   if (!edev->ecore_dev)
     {
        if (!edev->ecore_dev_list || (eina_list_count(edev->ecore_dev_list) == 0))
          {
             /* 1st Ecore_Device is added */
             edev->ecore_dev = ecore_device_ref(dev);
          }
        else
          {
             /* 3rd or more Ecore_Device is added */
             edev->ecore_dev_list = eina_list_append(edev->ecore_dev_list, ecore_device_ref(dev));
          }
     }
   else
     {
        /* 2nd Ecore_Device is added */
        edev->ecore_dev_list = eina_list_append(edev->ecore_dev_list, edev->ecore_dev);
        edev->ecore_dev = NULL;

        edev->ecore_dev_list = eina_list_append(edev->ecore_dev_list, ecore_device_ref(dev));
     }

   _e_input_ecore_device_event(dev, EINA_TRUE);

   return EINA_TRUE;
}

static Eina_Bool
_e_input_remove_ecore_device(E_Input_Evdev *edev, Ecore_Device_Class clas)
{
   const Eina_List *dev_list = NULL, *l;
   Eina_List *ll, *ll_next;
   Ecore_Device *dev = NULL, *data;
   const char *identifier;

   if (!edev->path) return EINA_FALSE;

   dev_list = ecore_device_list();
   if (!dev_list) return EINA_FALSE;
   EINA_LIST_FOREACH(dev_list, l, dev)
      {
         if (!dev) continue;
         identifier = ecore_device_description_get(dev);
         if (!identifier) continue;
         if ((ecore_device_class_get(dev) == clas) && (!strcmp(identifier, edev->path)))
           {
              if (edev->ecore_dev) edev->ecore_dev = NULL;
              else if (edev->ecore_dev_list)
                {
                   EINA_LIST_FOREACH_SAFE(edev->ecore_dev_list, ll, ll_next, data)
                     {
                        if (data == dev)
                          {
                             edev->ecore_dev_list = eina_list_remove_list(edev->ecore_dev_list, ll);
                          }
                     }
                }
              ecore_device_del(dev);
              _e_input_ecore_device_event(dev, EINA_FALSE);
              return EINA_TRUE;
           }
      }
   return EINA_FALSE;
}

Eina_Bool
_e_input_device_add(E_Input_Evdev *edev)
{
   Eina_Bool ret = EINA_FALSE;
   Ecore_Device_Class clas = ECORE_DEVICE_CLASS_NONE;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        if (!e_devicemgr_detent_is_detent(libinput_device_get_name(edev->device)))
          clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_POINTER);
        ret = _e_input_add_ecore_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_KEYBOARD);
        ret = _e_input_add_ecore_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_TOUCH);
        ret = _e_input_add_ecore_device(edev, clas);
     }

   return ret;
}

void
_e_input_device_remove(E_Input_Evdev *edev)
{
   Ecore_Device_Class clas = ECORE_DEVICE_CLASS_NONE;
   Ecore_Device *data;

   if (edev->caps & E_INPUT_SEAT_POINTER)
     {
        if (!e_devicemgr_detent_is_detent(libinput_device_get_name(edev->device)))
          clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_POINTER);
        _e_input_remove_ecore_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_KEYBOARD)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_KEYBOARD);
        _e_input_remove_ecore_device(edev, clas);
     }
   if (edev->caps & E_INPUT_SEAT_TOUCH)
     {
        clas = _e_input_seat_cap_to_ecore_device_class(E_INPUT_SEAT_TOUCH);
        _e_input_remove_ecore_device(edev, clas);
     }

   if (edev->ecore_dev_list)
     {
        if (eina_list_count(edev->ecore_dev_list) > 0)
          {
             EINA_LIST_FREE(edev->ecore_dev_list, data)
               {
                  WRN("Invalid device is left. name: %s, identifier: %s, clas: %s\n",
                      ecore_device_name_get(data), ecore_device_description_get(data),
                      _e_input_ecore_device_class_to_string(ecore_device_class_get(data)));
                  ecore_device_del(data);
               }
          }
        edev->ecore_dev_list = NULL;
     }
}

static void
_device_added(E_Input_Backend *input, struct libinput_device *device)
{
   struct libinput_seat *libinput_seat;
   const char *seat_name;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;

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

   /* append this device to the seat */
   seat->devices = eina_list_append(seat->devices, edev);

   if (EINA_FALSE == _e_input_device_add(edev))
     {
        ERR("Failed to create evas device !\n");
        return;
     }
}

static void
_device_removed(E_Input_Backend *input, struct libinput_device *device)
{
   E_Input_Evdev *edev;

   /* try to get the evdev structure */
   if (!(edev = libinput_device_get_user_data(device)))
     {
        return;
     }

   _e_input_device_remove(edev);

   /* remove this evdev from the seat's list of devices */
   edev->seat->devices = eina_list_remove(edev->seat->devices, edev);

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
