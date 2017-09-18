#include "e.h"
#include "e_input_private.h"

void 
_e_input_evdev_device_destroy(E_Input_Evdev *edev)
{
   EINA_SAFETY_ON_NULL_RETURN(edev);

   if (edev->seat_caps & EVDEV_SEAT_KEYBOARD)
     {
        if (edev->xkb.state) xkb_state_unref(edev->xkb.state);
        if (edev->xkb.keymap) xkb_map_unref(edev->xkb.keymap);
     }

   if (edev->path) eina_stringshare_del(edev->path);
   if (edev->device) libinput_device_unref(edev->device);
   if (edev->key_remap_hash) eina_hash_free(edev->key_remap_hash);

   free(edev);
}

