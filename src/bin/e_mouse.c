#include "e.h"

E_API int
e_mouse_update(void)
{
   const Eina_List *list, *l;
   E_Input_Device *dev;

   list = ecore_drm_devices_get();
   EINA_LIST_FOREACH(list, l, dev)
     {
        e_input_device_pointer_left_handed_set(dev, (Eina_Bool)!e_config->mouse_hand);
     }

   return 1;
}
