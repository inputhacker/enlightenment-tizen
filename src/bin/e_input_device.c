#include "e.h"
#include "e_input_private.h"

/* e_input_device private variable */
static Eina_List *einput_devices;
static E_Input_Device *e_input_device_default = NULL;

static int
_device_open_no_pending(const char *device, int flags)
{
   int fd = -1;
   struct stat s;

   fd = open(device, flags | O_CLOEXEC);

   if (fd < 0) return fd;
   if (fstat(fd, &s) == -1)
     {
        close(fd);
        return -1;
     }

   return fd;
}

static void
_device_close(const char *device, int fd)
{
   if (fd >= 0)
     close(fd);
}

/* local functions */
static int
_e_input_device_cb_open_restricted(const char *path, int flags, void *data)
{
   E_Input_Backend *input;
   int fd = -1;

   if (!(input = data)) return -1;

   /* try to open the device */
   fd = _device_open_no_pending(path, flags);

   if (fd < 0)
     {
        ERR("Could not open device");
        return -1;
     }

   if (input->dev->fd_hash)
     eina_hash_add(input->dev->fd_hash, path, (void *)(intptr_t)fd);

   return fd;
}

static void
_e_input_device_cb_close_restricted(int fd, void *data)
{
   E_Input_Backend *input;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;
   Eina_List *l, *ll;

   if (!(input = data)) return;

   EINA_LIST_FOREACH(input->dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(seat->devices, ll, edev)
          {
             if (edev->fd == fd)
               {
                  _device_close(edev->path, fd);

                  /* re-initialize fd after closing */
                  edev->fd = -1;
                  return;
               }
          }
     }

   if (fd >= 0) close(fd);
}

const struct libinput_interface _input_interface =
{
   _e_input_device_cb_open_restricted,
   _e_input_device_cb_close_restricted,
};

static E_Input_Device *
_e_input_device_default_get(void)
{
   return e_input_device_default;
}

struct xkb_context *
_e_input_device_cached_context_get(enum xkb_context_flags flags)
{
   if (!cached_context)
     return xkb_context_new(flags);
   else
     return xkb_context_ref(cached_context);
}

struct xkb_keymap *
_e_input_device_cached_keymap_get(struct xkb_context *ctx,
                       const struct xkb_rule_names *names,
                       enum xkb_keymap_compile_flags flags)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ctx, NULL);

   if (!cached_keymap)
     return xkb_map_new_from_names(ctx, names, flags);
   else
     return xkb_map_ref(cached_keymap);
}

void
_e_input_device_cached_context_update(struct xkb_context *ctx)
{
   Eina_List *l;
   E_Input_Device *dev;

   EINA_LIST_FOREACH(einput_devices, l, dev)
     {
        xkb_context_unref(dev->xkb_ctx);
        dev->xkb_ctx = xkb_context_ref(ctx);
     }
}

void
_e_input_device_cached_keymap_update(struct xkb_keymap *map)
{
   Eina_List *l, *l2, *l3;
   E_Input_Device *dev;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;

   EINA_LIST_FOREACH(einput_devices, l, dev)
     EINA_LIST_FOREACH(dev->seats, l2, seat)
       EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), l3, edev)
         {
            xkb_keymap_unref(edev->xkb.keymap);
            edev->xkb.keymap = xkb_keymap_ref(map);
            xkb_state_unref(edev->xkb.state);
            edev->xkb.state = xkb_state_new(map);
         }
}

EINTERN void
e_input_device_keyboard_cached_context_set(struct xkb_context *ctx)
{
   EINA_SAFETY_ON_NULL_RETURN(ctx);

   if (cached_context == ctx) return;

   if (cached_context)
     _e_input_device_cached_context_update(ctx);

   cached_context = ctx;
}

EINTERN void
e_input_device_keyboard_cached_keymap_set(struct xkb_keymap *map)
{
   EINA_SAFETY_ON_NULL_RETURN(map);

   if (cached_keymap == map) return;

   if (cached_keymap)
      _e_input_device_cached_keymap_update(map);

   cached_keymap = map;
}

static void
e_input_device_destroy(E_Input_Device *dev)
{
   E_Input_Backend *input;
   E_Input_Seat *seat;
   E_Input_Evdev *edev;

   EINA_SAFETY_ON_NULL_RETURN(dev);

   EINA_LIST_FREE(dev->seats, seat)
     {
        EINA_LIST_FREE(seat->devices, edev)
          {
             if (edev->fd >= 0)
               close(edev->fd);
             _e_input_evdev_device_destroy(edev);
          }

        if (seat->name)
          eina_stringshare_del(seat->name);
        free(seat);
     }

   EINA_LIST_FREE(dev->inputs, input)
     {
        if (input->hdlr)
          ecore_main_fd_handler_del(input->hdlr);
        if (input->libinput)
          libinput_unref(input->libinput);
        free(input);
     }

   eina_stringshare_del(dev->seat);
   xkb_context_unref(dev->xkb_ctx);
   eina_hash_free(dev->fd_hash);
   dev->fd_hash = NULL;

   if (dev == e_input_device_default)
     e_input_device_default = NULL;

   free(dev);
}

static void
_e_input_device_add_list(E_Input_Device *dev)
{
   Eina_List *l;
   E_Input_Device *dev_data;

   EINA_LIST_FOREACH(einput_devices, l, dev_data)
     {
        if (dev_data == dev) return;
     }

   einput_devices = eina_list_append(einput_devices, dev);
}

static void
_e_input_device_remove_list(E_Input_Device *dev)
{
   Eina_List *l, *l_next;
   E_Input_Device *dev_data;

   EINA_LIST_FOREACH_SAFE(einput_devices, l, l_next, dev_data)
     {
        if (dev == dev_data)
          einput_devices = eina_list_remove_list(einput_devices, l);
     }
}

EINTERN E_Input_Device *
e_input_device_open(void)
{
   E_Input_Device *dev = NULL;

   dev = (E_Input_Device *)calloc(1, sizeof(E_Input_Device));

   if (!dev)
     {
        EINA_LOG_ERR("Failed to alloc memory for E_Input_Device\n");
        return NULL;
     }

   dev->seat = eina_stringshare_add("seat0");
   dev->fd_hash = eina_hash_string_superfast_new(NULL);

   /* try to create xkb context */
   if (!(dev->xkb_ctx = _e_input_device_cached_context_get(0)))
     {
        ERR("Failed to create xkb context: %m");
        goto err;
     }

   if (!e_input_device_default)
     e_input_device_default = dev;

   _e_input_device_add_list(dev);

   return dev;

err:
   if (dev)
     {
        eina_stringshare_del(dev->seat);
        xkb_context_unref(dev->xkb_ctx);
        free(dev);
     }

   return NULL;
}

EINTERN Eina_Bool
e_input_device_close(E_Input_Device *dev)
{
   /* check for valid device */
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);

   _e_input_device_remove_list(dev);
   e_input_device_destroy(dev);

   return EINA_TRUE;
}

EINTERN void
e_input_device_window_set(E_Input_Device *dev, unsigned int window)
{
   /* check for valid device */
   EINA_SAFETY_ON_TRUE_RETURN(!dev);

   /* TODO : Must update window of ecore/evas device when the given window */
   /*        is not equal to the existing window. */

   dev->window = window;
}

EINTERN void
e_input_device_pointer_xy_get(E_Input_Device *dev, int *x, int *y)
{
   E_Input_Seat *seat;
   E_Input_Evdev *edev;
   Eina_List *l, *ll;

   if (x) *x = 0;
   if (y) *y = 0;

   if (!dev)
     dev = _e_input_device_default_get();

   /* check for valid device */
   EINA_SAFETY_ON_TRUE_RETURN(!dev);
   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(seat->devices, ll, edev)
          {
             if (!libinput_device_has_capability(edev->device,
                                                 LIBINPUT_DEVICE_CAP_POINTER))
               continue;

             if (x) *x = seat->ptr.dx;
             if (y) *y = seat->ptr.dy;

             return;
          }
     }
}

E_API void
e_input_device_pointer_warp(E_Input_Device *dev, int x, int y)
{
   E_Input_Seat *seat;
   E_Input_Evdev *edev;
   Eina_List *l, *ll;

   if (!dev)
     dev = _e_input_device_default_get();

   /* check for valid device */
   EINA_SAFETY_ON_TRUE_RETURN(!dev);
   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(seat->devices, ll, edev)
          {
             if (!libinput_device_has_capability(edev->device,
                                                 LIBINPUT_DEVICE_CAP_POINTER))
               continue;

             seat->ptr.dx = seat->ptr.ix = x;
             seat->ptr.dy = seat->ptr.iy = y;
             _e_input_pointer_motion_post(edev);
          }
     }
}

EINTERN Eina_Bool
e_input_device_pointer_left_handed_set(E_Input_Device *dev, Eina_Bool left_handed)
{
   E_Input_Seat *seat = NULL;
   E_Input_Evdev *edev = NULL;
   Eina_List *l = NULL, *l2 = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev->seats, EINA_FALSE);

   if (dev->left_handed == left_handed)
     return EINA_TRUE;
   dev->left_handed = left_handed;

   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), l2, edev)
          {
             if (libinput_device_has_capability(edev->device,
                                                LIBINPUT_DEVICE_CAP_POINTER))
               {
                  if (libinput_device_config_left_handed_set(edev->device, (int)left_handed) !=
                      LIBINPUT_CONFIG_STATUS_SUCCESS)
                    {
                       WRN("Failed to set left hand mode about device: %s\n",
                           libinput_device_get_name(edev->device));
                       continue;
                    }
               }
          }
     }
   return EINA_TRUE;
}


EINTERN Eina_Bool
e_input_device_pointer_rotation_set(E_Input_Device *dev, int rotation)
{
   E_Input_Seat *seat = NULL;
   Eina_List *l = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev->seats, EINA_FALSE);

   if ((rotation % 90 != 0) || (rotation / 90 > 3) || (rotation < 0)) return EINA_FALSE;

   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        switch (rotation)
          {
           case 90:
              seat->ptr.swap = EINA_TRUE;
              seat->ptr.invert_x = EINA_FALSE;
              seat->ptr.invert_y = EINA_TRUE;
              break;
           case 180:
              seat->ptr.swap = EINA_FALSE;
              seat->ptr.invert_x = EINA_TRUE;
              seat->ptr.invert_y = EINA_TRUE;
              break;
           case 270:
              seat->ptr.swap = EINA_TRUE;
              seat->ptr.invert_x = EINA_TRUE;
              seat->ptr.invert_y = EINA_FALSE;
		break;
           case 0:
              seat->ptr.swap = EINA_FALSE;
              seat->ptr.invert_x = EINA_FALSE;
              seat->ptr.invert_y = EINA_FALSE;
              break;
           default:
              break;
          }
     }
   return EINA_TRUE;
}

EINTERN void
e_input_device_rotation_set(E_Input_Device *dev, unsigned int rotation)
{
   E_Input_Seat *seat = NULL;
   E_Input_Evdev *edev = NULL;
   Eina_List *l = NULL, *l2 = NULL;
   int temp;

   EINA_SAFETY_ON_NULL_RETURN(dev);
   EINA_SAFETY_ON_NULL_RETURN(dev->seats);

   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), l2, edev)
          {
             if (libinput_device_has_capability(edev->device,
                                      LIBINPUT_DEVICE_CAP_POINTER))
               {
                  edev->mouse.minx = edev->mouse.miny = 0;
                  e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen),
                                       &edev->mouse.maxw, &edev->mouse.maxh);

                  if (rotation == 90 || rotation == 270)
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
     }
}

static void
_e_input_device_touch_matrix_identify(float result[6])
{
   result[0] = 1.0;
   result[1] = 0.0;
   result[2] = 0.0;
   result[3] = 0.0;
   result[4] = 1.0;
   result[5] = 0.0;
}

static void
_e_input_device_touch_matrix_mulifly(float result[6], float m1[6], float m2[6])
{
   result[0] = m1[0] * m2 [0] + m1[1] * m2[3];
   result[1] = m1[0] * m2 [1] + m1[1] * m2[4];
   result[2] = m1[0] * m2 [2] + m1[1] * m2[5] + m1[2];
   result[3] = m1[3] * m2 [0] + m1[4] * m2[3];
   result[4] = m1[3] * m2 [1] + m1[4] * m2[4];
   result[5] = m1[3] * m2 [2] + m1[4] * m2[5] + m1[5];
}

static void
_e_input_device_touch_matrix_rotation_get(float result[6], int degree, float w, float h)
{
   if (w == 0.0) w = 1.0;
   if (h == 0.0) h = 1.0;

   switch (degree)
     {
        case 90:
          result[0] = 0.0;
          result[1] = -h/w;
          result[2] = h/w;
          result[3] = w/h;
          result[4] = 0.0;
          result[5] = 0.0;
          break;
        case 180:
          result[0] = -1.0;
          result[1] = 0.0;
          result[2] = 1.0;
          result[3] = 0.0;
          result[4] = -1.0;
          result[5] = 1.0;
          break;
        case 270:
          result[0] = 0.0;
          result[1] = h/w;
          result[2] = 0.0;
          result[3] = -w/h;
          result[4] = 0.0;
          result[5] = w/h;
          break;
        case 0:
          _e_input_device_touch_matrix_identify(result);
          break;
        default:
          WRN("Please input valid angle(%d)\n", degree);
     }
}

static void
_e_input_device_touch_matrix_translate_get(float result[6], float x, float y, float w, float h, float default_w, float default_h)
{
   if (default_w == 0.0) default_w = 1.0;
   if (default_h == 0.0) default_h = 1.0;

   result[0] = w / default_w;
   result[4] = h / default_h;
   result[2] = x / default_w;
   result[5] = y / default_h;
}

EINTERN Eina_Bool
e_input_device_touch_rotation_set(E_Input_Device *dev, unsigned int rotation)
{
   E_Input_Seat *seat = NULL;
   E_Input_Evdev *edev = NULL;
   Eina_List *l = NULL, *l2 = NULL;
   float mat_translate[6] = {0.0, }, mat_rotation[6] = {0.0, }, result[6] = {0.0, };
   float default_w = 0.0, default_h = 0.0;
   Eina_Bool res = EINA_TRUE;
   int output_w = 0, output_h = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev->seats, EINA_FALSE);

   e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen), &output_w, &output_h);
   default_w = (float)output_w;
   default_h = (float)output_h;

   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), l2, edev)
          {
             if (edev->caps & E_INPUT_SEAT_TOUCH)
               {
                  _e_input_device_touch_matrix_identify(mat_translate);
                  _e_input_device_touch_matrix_identify(mat_rotation);
                  _e_input_device_touch_matrix_identify(result);

                  if (edev->touch.transform.x || edev->touch.transform.y ||
                      edev->touch.transform.w || edev->touch.transform.h)
                    {
                       _e_input_device_touch_matrix_translate_get(mat_translate,
                                                                    (float)edev->touch.transform.x,
                                                                    (float)edev->touch.transform.y,
                                                                    (float)edev->touch.transform.w,
                                                                    (float)edev->touch.transform.h,
                                                                    default_w, default_h);

                    }

                  _e_input_device_touch_matrix_rotation_get(mat_rotation, rotation, default_w, default_h);

                  _e_input_device_touch_matrix_mulifly(result, mat_translate, mat_rotation);

                  if (!e_input_evdev_touch_calibration_set(edev, result))
                    {
                       res = EINA_FALSE;
                       continue;
                    }
                  else
                    {
                       edev->touch.transform.rotation = rotation;
                    }
               }
          }
     }

   return res;
}

EINTERN Eina_Bool
e_input_device_touch_transformation_set(E_Input_Device *dev, int offset_x, int offset_y, int w, int h)
{
   E_Input_Seat *seat = NULL;
   E_Input_Evdev *edev = NULL;
   Eina_List *l = NULL, *l2 = NULL;
   float mat_translate[6] = {0.0, }, mat_rotation[6] = {0.0 }, result[6] = {0.0, };
   float default_w = 0.0, default_h = 0.0;
   Eina_Bool res = EINA_TRUE;
   int output_w = 0, output_h = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev->seats, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL((w == 0) || (h == 0), EINA_FALSE);

   e_output_size_get(e_comp_screen_primary_output_get(e_comp->e_comp_screen), &output_w, &output_h);
   default_w = (float)output_w;
   default_h = (float)output_h;

   EINA_LIST_FOREACH(dev->seats, l, seat)
     {
        EINA_LIST_FOREACH(e_input_seat_evdev_list_get(seat), l2, edev)
          {
             if (edev->caps & E_INPUT_SEAT_TOUCH)
               {
                  _e_input_device_touch_matrix_identify(mat_translate);
                  _e_input_device_touch_matrix_identify(mat_rotation);
                  _e_input_device_touch_matrix_identify(result);

                  _e_input_device_touch_matrix_translate_get(mat_translate,
                                                               (float)offset_x, (float)offset_y,
                                                               (float)w, (float)h, default_w, default_h);

                  if (edev->touch.transform.rotation)
                    {
                       _e_input_device_touch_matrix_rotation_get(mat_rotation,
                                                                   edev->touch.transform.rotation,
                                                                   default_w, default_h);
                    }

                  _e_input_device_touch_matrix_mulifly(result, mat_translate, mat_rotation);

                  if (!e_input_evdev_touch_calibration_set(edev, result))
                    {
                       res = EINA_FALSE;
                       continue;
                    }
                  else
                    {
                       edev->touch.transform.x = offset_x;
                       edev->touch.transform.y = offset_y;
                       edev->touch.transform.w = w;
                       edev->touch.transform.h = h;
                    }
               }
          }
     }
   return res;
}

static void
e_input_device_libinput_log_handler(struct libinput *libinput EINA_UNUSED,
                               enum libinput_log_priority priority,
                               const char *format, va_list args)
{
   char buf[1024] = {0,};

   vsnprintf(buf, 1024, format, args);
   switch (priority)
     {
        case LIBINPUT_LOG_PRIORITY_DEBUG:
           DBG("%s", buf);
           break;
        case LIBINPUT_LOG_PRIORITY_INFO:
           INF("%s", buf);
           break;
        case LIBINPUT_LOG_PRIORITY_ERROR:
           ERR("%s", buf);
           break;
        default:
           break;
     }
}

EINTERN Eina_Bool
e_input_device_input_backend_create(E_Input_Device *dev, const char *backend)
{
   Eina_Bool res = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(backend, EINA_FALSE);

   if (!strncmp(backend, "libinput_udev", strlen("libinput_udev")))
     res = e_input_device_input_create_libinput_udev(dev);
   else if (!strncmp(backend, "libinput_path", strlen("libinput_path")))
     res = e_input_device_input_create_libinput_path(dev);

   return res;
}

/* public functions */
EINTERN Eina_Bool
e_input_device_input_create_libinput_udev(E_Input_Device *dev)
{
   E_Input_Backend *input;
   char *env;

   /* check for valid device */
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);

   /* try to allocate space for new input structure */
   if (!(input = calloc(1, sizeof(E_Input_Backend))))
     {
        return EINA_FALSE;
     }

   /* set reference for parent device */
   input->dev = dev;

   /* try to create libinput context */
   input->libinput =
     libinput_udev_create_context(&_input_interface, input, eeze_udev_get());
   if (!input->libinput)
     {
        ERR("Could not create libinput context: %m");
        goto err;
     }

   /* set libinput log priority */
   if ((env = getenv(E_INPUT_ENV_LIBINPUT_LOG_DISABLE)) && (atoi(env) == 1))
     libinput_log_set_handler(input->libinput, NULL);
   else if ((env = getenv(E_INPUT_ENV_LIBINPUT_LOG_EINA_LOG)) && (atoi(env) == 1))
     libinput_log_set_handler(input->libinput, e_input_device_libinput_log_handler);

   libinput_log_set_priority(input->libinput, LIBINPUT_LOG_PRIORITY_INFO);

   /* assign udev seat */
   if (libinput_udev_assign_seat(input->libinput, dev->seat) != 0)
     {
        ERR("Failed to assign seat: %m");
        goto err;
     }

   /* process pending events */
   _input_events_process(input);

   /* enable this input */
   if (!e_input_enable_input(input))
     {
        ERR("Failed to enable input");
        goto err;
     }

   /* append this input */
   dev->inputs = eina_list_append(dev->inputs, input);

   return EINA_TRUE;

err:
   if (input->libinput) libinput_unref(input->libinput);
   free(input);

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_input_device_input_create_libinput_path(E_Input_Device *dev)
{
   E_Input_Backend *input;
   struct libinput_device *device;
   int devices_num = 0;
   char *env;
   Eina_Stringshare *path;

   /* check for valid device */
   EINA_SAFETY_ON_NULL_RETURN_VAL(dev, EINA_FALSE);

   if ((env = getenv("PATH_DEVICES_NUM")))
     devices_num = atoi(env);
   if (devices_num <= 0 || devices_num >= INT_MAX)
     {
        return EINA_TRUE;
     }

   INF("PATH_DEVICES_NUM : %d", devices_num);

   /* try to allocate space for new input structure */
   if (!(input = calloc(1, sizeof(E_Input_Backend))))
     {
        return EINA_FALSE;
     }

   /* set reference for parent device */
   input->dev = dev;

   /* try to create libinput context */
   input->libinput =
     libinput_path_create_context(&_input_interface, input);
   if (!input->libinput)
     {
        ERR("Could not create libinput path context: %m");
        goto err;
     }

   /* set libinput log priority */
   if ((env = getenv(E_INPUT_ENV_LIBINPUT_LOG_DISABLE)) && (atoi(env) == 1))
     libinput_log_set_handler(input->libinput, NULL);
   else if ((env = getenv(E_INPUT_ENV_LIBINPUT_LOG_EINA_LOG)) && (atoi(env) == 1))
     libinput_log_set_handler(input->libinput, e_input_device_libinput_log_handler);

   libinput_log_set_priority(input->libinput, LIBINPUT_LOG_PRIORITY_INFO);

   for (int i = 0; i < devices_num; i++)
     {
        char buf[1024] = "PATH_DEVICE_";
        eina_convert_itoa(i + 1, buf + 12);
        env = getenv(buf);
        if (env)
          {
             path = eina_stringshare_add(env);
             device = libinput_path_add_device(input->libinput, path);
             if (!device)
               ERR("Failed to initialized device %s", path);
             else
               INF("libinput_path created input device %s", path);
          }
     }

   /* process pending events */
   _input_events_process(input);

   /* enable this input */
   if (!e_input_enable_input(input))
     {
        ERR("Failed to enable input");
        goto err;
     }

   /* append this input */
   dev->inputs = eina_list_append(dev->inputs, input);

   return EINA_TRUE;

err:
   if (input->libinput) libinput_unref(input->libinput);
   free(input);

   return EINA_FALSE;
}


E_API const Eina_List *
e_input_devices_get(void)
{
   return einput_devices;
}

