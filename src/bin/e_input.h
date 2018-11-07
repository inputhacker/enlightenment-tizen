#ifdef E_TYPEDEFS

typedef struct _E_Input E_Input;

E_API extern int E_INPUT_EVENT_SEAT_ADD;
E_API extern int E_EVENT_INPUT_ENABLED;
E_API extern int E_EVENT_INPUT_DISABLED;

#else

#ifndef E_INPUT_H
#define E_INPUT_H

#define E_INPUT_TYPE (int)0xE0b0beaf

#include <xkbcommon/xkbcommon.h>

typedef enum _E_Input_Seat_Capabilities
{
   E_INPUT_SEAT_POINTER = (1 << 0),
   E_INPUT_SEAT_KEYBOARD = (1 << 1),
   E_INPUT_SEAT_TOUCH = (1 << 2),
} E_Input_Seat_Capabilities;

typedef enum _E_Input_Libinput_Backend
{
   E_INPUT_LIBINPUT_BACKEND_UDEV = 1,
   E_INPUT_LIBINPUT_BACKEND_PATH
} E_Input_Libinput_Backend;

typedef struct _E_Input_Device E_Input_Device;
typedef struct _E_Input_Backend E_Input_Backend;
typedef struct _E_Input_Evdev E_Input_Evdev;
typedef struct _E_Input_Seat E_Input_Seat;

struct _E_Input
{
   Ecore_Window window;
   Ecore_Evas *ee;
   E_Input_Device *dev;
   const char *input_base_dir;
   Eina_List *handlers;

   Eina_Bool use_thread : 1;
};

struct _E_Input_Device
{
   const char *seat;

   Eina_List *seats;
   Eina_List *inputs;
   Eina_Hash *fd_hash;

   struct xkb_context *xkb_ctx;
   Ecore_Window window;
   Eina_Bool left_handed : 1;
};

EINTERN int e_input_init(Ecore_Evas *ee);
EINTERN int e_input_shutdown(void);
EINTERN const char *e_input_base_dir_get(void);
EINTERN Eina_Bool e_input_thread_enabled_get(void);
EINTERN E_Input *e_input_get(void);
EINTERN Ecore_Evas *e_input_ecore_evas_get(E_Input *ei);

EINTERN E_Input_Device *e_input_device_open(void);
EINTERN Eina_Bool e_input_device_close(E_Input_Device *dev);
EINTERN void e_input_device_keyboard_cached_context_set(struct xkb_context *ctx);
EINTERN void e_input_device_keyboard_cached_keymap_set(struct xkb_keymap *map);
EINTERN Eina_Bool e_input_device_input_backend_create(E_Input_Device *dev, E_Input_Libinput_Backend backend);
EINTERN Eina_Bool e_input_device_input_create_libinput_udev(E_Input_Device *dev);
EINTERN Eina_Bool e_input_device_input_create_libinput_path(E_Input_Device *dev);
EINTERN void e_input_device_window_set(E_Input_Device *dev, Ecore_Window window);
EINTERN void e_input_device_pointer_xy_get(E_Input_Device *dev, int *x, int *y);
EINTERN Eina_Bool e_input_device_pointer_left_handed_set(E_Input_Device *dev, Eina_Bool left_handed);
EINTERN Eina_Bool e_input_device_pointer_rotation_set(E_Input_Device *dev, int rotation);
EINTERN Eina_Bool e_input_device_touch_rotation_set(E_Input_Device *dev, unsigned int rotation);
EINTERN void e_input_device_rotation_set(E_Input_Device *dev, unsigned int rotation);
EINTERN Eina_Bool e_input_device_touch_transformation_set(E_Input_Device *dev, int offset_x, int offset_y, int w, int h);

EINTERN Eina_Bool e_input_enable_input(E_Input_Backend *input);
EINTERN void e_input_disable_input(E_Input_Backend *input);

EINTERN void e_input_evdev_axis_size_set(E_Input_Evdev *edev, int w, int h);
EINTERN const char *e_input_evdev_sysname_get(E_Input_Evdev *evdev);
EINTERN Eina_Bool e_input_evdev_key_remap_enable(E_Input_Evdev *edev, Eina_Bool enable);
EINTERN Eina_Bool e_input_evdev_key_remap_set(E_Input_Evdev *edev, int *from_keys, int *to_keys, int num);
EINTERN Eina_Bool e_input_evdev_touch_calibration_set(E_Input_Evdev *edev, float matrix[6]);
EINTERN Eina_Bool e_input_evdev_mouse_accel_speed_set(E_Input_Evdev *edev, double speed);
EINTERN unsigned int e_input_evdev_touch_pressed_get(E_Input_Evdev *edev);

E_API const Eina_List *e_input_devices_get(void);
E_API Eina_Bool e_input_device_pointer_warp(E_Input_Device *dev, int x, int y);
E_API Eina_Bool e_input_device_mouse_accel_speed_set(E_Input_Device *dev, double speed);

E_API const char *e_input_evdev_name_get(E_Input_Evdev *evdev);
E_API Eina_List *e_input_seat_evdev_list_get(E_Input_Seat *seat);
E_API int e_input_evdev_wheel_click_angle_get(E_Input_Evdev *dev);
E_API Ecore_Device *e_input_evdev_get_ecore_device(const char *path, Ecore_Device_Class clas);

E_API unsigned int e_input_device_touch_pressed_get(E_Input_Device *dev);

#endif
#endif
