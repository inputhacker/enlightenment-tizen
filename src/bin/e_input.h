#ifdef E_TYPEDEFS

typedef struct _E_Input E_Input;
//typedef struct _E_Event_Input_Generic        E_Event_Input_XXX;

#else

#ifndef E_INPUT_H
#define E_INPUT_H

#define E_INPUT_TYPE (int)0xE0b0beaf

#include <xkbcommon/xkbcommon.h>

typedef enum _E_Input_Evdev_Capabilities
{
   EVDEV_KEYBOARD = (1 << 0),
   EVDEV_BUTTON = (1 << 1),
   EVDEV_MOTION_ABS = (1 << 2),
   EVDEV_MOTION_REL = (1 << 3),
   EVDEV_TOUCH = (1 << 4),
} E_Input_Evdev_Capabilities;

typedef enum _E_Input_Evdev_Event_Type
{
   EVDEV_NONE,
   EVDEV_ABSOLUTE_TOUCH_DOWN,
   EVDEV_ABSOLUTE_MOTION,
   EVDEV_ABSOLUTE_TOUCH_UP,
   EVDEV_ABSOLUTE_MT_DOWN,
   EVDEV_ABSOLUTE_MT_MOTION,
   EVDEV_ABSOLUTE_MT_UP,
   EVDEV_RELATIVE_MOTION,
} E_Input_Evdev_Event_Type;

typedef enum _E_Input_Seat_Capabilities
{
   EVDEV_SEAT_POINTER = (1 << 0),
   EVDEV_SEAT_KEYBOARD = (1 << 1),
   EVDEV_SEAT_TOUCH = (1 << 2),
} E_Input_Seat_Capabilities;

struct _E_Input_Event_Input_Device_Add
{
   const char *name; /* descriptive device name */
   const char *sysname; /* system name of the input device */
   const char *seatname; /* logical name of the seat */
   E_Input_Evdev_Capabilities caps; /* capabilities on a device */
};

struct _E_Input_Event_Input_Device_Del
{
   const char *name; /* descriptive device name */
   const char *sysname; /* system name of the input device */
   const char *seatname; /* logical name of the seat */
   E_Input_Evdev_Capabilities caps; /* capabilities on a device */
};

/* opaque structure to represent a drm device */
typedef struct _E_Input_Device E_Input_Device;

/* opaque structure to represent a drm input */
typedef struct _E_Input_Backend E_Input_Backend;

/* opaque structure to represent a drm evdev input */
typedef struct _E_Input_Evdev E_Input_Evdev;

/* opaque structure to represent a drm seat */
typedef struct _E_Input_Seat E_Input_Seat;

/* structure to inform new input device added */
typedef struct _E_Input_Event_Input_Device_Add E_Input_Event_Input_Device_Add;

/* structure to inform old input device deleted */
typedef struct _E_Input_Event_Input_Device_Del E_Input_Event_Input_Device_Del;

E_API extern int E_INPUT_EVENT_INPUT_DEVICE_ADD;
E_API extern int E_INPUT_EVENT_INPUT_DEVICE_DEL;
E_API extern int E_INPUT_EVENT_SEAT_ADD;
E_API extern int E_EVENT_INPUT_ENABLED;
E_API extern int E_EVENT_INPUT_DISABLED;


struct _E_Input
{
   E_Object e_obj_inherit;

   uint32_t window;
};

struct _E_Event_Input_Generic
{
   E_Input *input;
};

/**
 * Sets the window of E_Input_Devices.
 * This function will set the window for given E_Input_Device devices.
 *
 * @param dev The E_Input_Device for which window is set
 * @param window The window to set
 */
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

#endif
#endif
