#ifdef E_TYPEDEFS

typedef struct _E_Input E_Input;

E_API extern int E_INPUT_EVENT_INPUT_DEVICE_ADD;
E_API extern int E_INPUT_EVENT_INPUT_DEVICE_DEL;
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

struct _E_Input_Event_Input_Device_Add
{
   const char *name; /* descriptive device name */
   const char *sysname; /* system name of the input device */
   const char *seatname; /* logical name of the seat */
   E_Input_Seat_Capabilities caps; /* capabilities on a device */
};

struct _E_Input_Event_Input_Device_Del
{
   const char *name; /* descriptive device name */
   const char *sysname; /* system name of the input device */
   const char *seatname; /* logical name of the seat */
   E_Input_Seat_Capabilities caps; /* capabilities on a device */
};

typedef struct _E_Input_Device E_Input_Device;
typedef struct _E_Input_Backend E_Input_Backend;
typedef struct _E_Input_Evdev E_Input_Evdev;
typedef struct _E_Input_Seat E_Input_Seat;

typedef struct _E_Input_Event_Input_Device_Add E_Input_Event_Input_Device_Add;
typedef struct _E_Input_Event_Input_Device_Del E_Input_Event_Input_Device_Del;

struct _E_Input
{
   Ecore_Window window;
   E_Input_Device *dev;
};

struct _E_Event_Input_Generic
{
   E_Input *input;
};

struct _E_Input_Device
{
   const char *seat;

   Eina_List *seats;
   Eina_List *inputs;

   struct xkb_context *xkb_ctx;
   int window;
   Eina_Bool left_handed : 1;
};

EINTERN int e_input_init(Ecore_Evas *ee);

EINTERN int e_input_shutdown(void);

E_API void e_input_inputs_disable(E_Input_Backend *input);
E_API Eina_List *e_input_seat_evdev_list_get(E_Input_Seat *seat);
E_API Eina_Bool e_input_inputs_enable(E_Input_Backend *input);
E_API void e_input_inputs_destroy(E_Input_Device *dev);
E_API Eina_Bool e_input_inputs_devices_create(E_Input_Device *dev);

E_API void e_input_device_keyboard_cached_context_set(struct xkb_context *ctx);
E_API void e_input_device_keyboard_cached_keymap_set(struct xkb_keymap *map);
E_API void e_input_device_free(E_Input_Device *dev);
E_API Eina_Bool e_input_device_open(E_Input_Device *dev);
E_API Eina_Bool e_input_device_close(E_Input_Device *dev);
E_API void e_input_device_window_set(E_Input_Device *dev, unsigned int window);
E_API void e_input_device_pointer_xy_get(E_Input_Device *dev, int *x, int *y);
E_API void e_input_device_pointer_warp(E_Input_Device *dev, int x, int y);
E_API Eina_Bool e_input_device_pointer_left_handed_set(E_Input_Device *dev, Eina_Bool left_handed);
E_API Eina_Bool e_input_device_pointer_rotation_set(E_Input_Device *dev, int rotation);
E_API void e_input_device_rotation_set(E_Input_Device *dev, unsigned int rotation);
E_API Eina_Bool e_input_device_touch_rotation_set(E_Input_Device *dev, unsigned int rotation);
E_API Eina_Bool e_input_device_touch_transformation_set(E_Input_Device *dev, int offset_x, int offset_y, int w, int h);
E_API const Eina_List *e_input_devices_get(void);

E_API void e_input_evdev_axis_size_set(E_Input_Evdev *edev, int w, int h);
E_API const char *e_input_evdev_name_get(E_Input_Evdev *evdev);
E_API const char *e_input_evdev_sysname_get(E_Input_Evdev *evdev);
E_API Eina_Bool e_input_evdev_key_remap_enable(E_Input_Evdev *edev, Eina_Bool enable);
E_API Eina_Bool e_input_evdev_key_remap_set(E_Input_Evdev *edev, int *from_keys, int *to_keys, int num);
E_API int e_input_evdev_wheel_click_angle_get(E_Input_Evdev *dev);
E_API Eina_Bool e_input_evdev_touch_calibration_set(E_Input_Evdev *edev, float matrix[6]);
E_API Ecore_Device *e_input_evdev_get_ecore_device(const char *path, Ecore_Device_Class clas);

#endif
#endif
