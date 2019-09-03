#include "e.h"
#include "services/e_service_lockscreen.h"

EINTERN Eina_Bool
e_service_lockscreen_client_set(E_Client *ec)
{
   if (!ec) return EINA_TRUE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ELOGF("LOCKSCREEN","Set Client", ec->pixmap, ec);

   e_client_window_role_set(ec, "lockscreen");

   // set lockscreen layer
   if (E_LAYER_CLIENT_NOTIFICATION_LOW > ec->layer)
     {
        e_client_layer_set(ec, E_LAYER_CLIENT_NOTIFICATION_LOW);
     }

   return EINA_TRUE;
}

