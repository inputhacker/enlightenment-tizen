#include "e.h"
#include "e_input_private.h"

/* e_input_device private variable */
static Eina_List *einput_devices;

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
       EINA_LIST_FOREACH(ecore_drm_seat_evdev_list_get(seat), l3, edev)
         {
            xkb_keymap_unref(edev->xkb.keymap);
            edev->xkb.keymap = xkb_keymap_ref(map);
            xkb_state_unref(edev->xkb.state);
            edev->xkb.state = xkb_state_new(map);
         }
}

E_API void
e_input_device_keyboard_cached_context_set(struct xkb_context *ctx)
{
   EINA_SAFETY_ON_NULL_RETURN(ctx);

   if (cached_context == ctx) return;

   if (cached_context)
     _e_input_device_cached_context_update(ctx);

   cached_context = ctx;
}

E_API void
e_input_device_keyboard_cached_keymap_set(struct xkb_keymap *map)
{
   EINA_SAFETY_ON_NULL_RETURN(map);

   if (cached_keymap == map) return;

   if (cached_keymap)
      _e_input_device_cached_keymap_update(map);

   cached_keymap = map;
}

E_API const Eina_List *
e_input_devices_get(void)
{
   return einput_devices;
}

