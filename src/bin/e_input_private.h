#ifdef E_TYPEDEFS

#else

#ifndef E_INPUT_PRIVIATES_H
#define E_INPUT_PRIVIATES_H

#include "e.h"
#include "e_input.h"
#include <libinput.h>
#include <Eeze.h>

struct xkb_keymap *cached_keymap;
struct xkb_context *cached_context;

#define E_INPUT_ENV_LIBINPUT_LOG_DISABLE "E_INPUT_LIBINPUT_LOG_DISABLE"
#define E_INPUT_ENV_LIBINPUT_LOG_EINA_LOG "E_INPUT_LIBINPUT_LOG_EINA_LOG"

struct _E_Input_Seat
{
   const char *name;
   E_Input_Backend *input;
   Eina_List *devices;
   struct libinput_seat *seat;

   struct
     {
        int ix, iy;
        double dx, dy;
        Eina_Bool swap;
        Eina_Bool invert_x;
        Eina_Bool invert_y;
     } ptr;
};

struct _E_Input_Backend
{
   int fd;
   E_Input_Device *dev;
   struct libinput *libinput;

   Ecore_Fd_Handler *hdlr;

   Eina_Bool enabled : 1;
   Eina_Bool suspended : 1;
   Eina_Bool left_handed : 1;
};

struct _E_Input_Evdev
{
   E_Input_Seat *seat;
   struct libinput_device *device;

   const char *path;
   int fd;

   int mt_slot;

   struct
     {
        int minx, miny, maxw, maxh;
        double dx, dy;
        unsigned int last, prev;
        uint32_t threshold;
        Eina_Bool did_double : 1;
        Eina_Bool did_triple : 1;
        uint32_t prev_button, last_button;
     } mouse;

   struct
     {
        struct xkb_keymap *keymap;
        struct xkb_state *state;
        xkb_mod_mask_t ctrl_mask;
        xkb_mod_mask_t alt_mask;
        xkb_mod_mask_t shift_mask;
        xkb_mod_mask_t win_mask;
        xkb_mod_mask_t scroll_mask;
        xkb_mod_mask_t num_mask;
        xkb_mod_mask_t caps_mask;
        xkb_mod_mask_t altgr_mask;
        unsigned int modifiers;
        unsigned int depressed, latched, locked, group;
     } xkb;

     Eina_Hash *key_remap_hash;
     Eina_Bool key_remap_enabled;

   E_Input_Seat_Capabilities caps;

   struct
     {
        struct
          {
             int rotation;
             int x, y;
             int w, h;
          } transform;
     } touch;
};

void _e_input_inputs_init(void);
Eina_Bool e_input_inputs_create(E_Input_Device *dev);
void _e_input_inputs_shutdown(void);

void _input_events_process(E_Input_Backend *input);
E_Input_Evdev *_e_input_evdev_device_create(E_Input_Seat *seat, struct libinput_device *device);
Eina_Bool _e_input_evdev_event_process(struct libinput_event *event);

struct xkb_context * _e_input_device_cached_context_get(enum xkb_context_flags flags);
struct xkb_keymap *_e_input_device_cached_keymap_get(struct xkb_context *ctx, const struct xkb_rule_names *names, enum xkb_keymap_compile_flags flags);


void _e_input_evdev_device_destroy(E_Input_Evdev *evdev);
void _e_input_pointer_motion_post(E_Input_Evdev *edev);

#endif
#endif
