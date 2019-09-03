#include "e.h"
#include "services/e_service_scrsaver.h"

EINTERN Eina_Bool
e_service_scrsaver_client_set(E_Client *ec)
{
   if (!ec) return EINA_TRUE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ELOGF("SCRSAVER","Set Client", ec->pixmap, ec);

   // set screensaver layer
   if (E_POLICY_SCRSAVER_LAYER != ec->layer)
     {
        e_client_layer_set(ec, E_POLICY_SCRSAVER_LAYER);
     }

   return EINA_TRUE;
}

