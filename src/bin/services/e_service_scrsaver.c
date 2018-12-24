#include "e.h"
#include "services/e_service_scrsaver.h"

EINTERN Eina_Bool
e_service_scrsaver_client_set(E_Client *ec)
{
   if (!ec) return EINA_TRUE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ELOGF("SCRSAVER","Set Client", ec);

   // set screensaver layer
   if (E_POLICY_SCRSAVER_LAYER != evas_object_layer_get(ec->frame))
     {
        evas_object_layer_set(ec->frame, E_POLICY_SCRSAVER_LAYER);
     }
   ec->layer = E_POLICY_SCRSAVER_LAYER;

   return EINA_TRUE;
}

