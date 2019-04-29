#include "e_video_internal.h"

typedef struct _E_Video_External E_Video_External;

struct _E_Video_External
{
   E_Video_Comp_Iface base;
   E_Client_Video *ecv;
};

static void
_e_video_external_iface_destroy(E_Video_Comp_Iface *iface)
{
   E_Video_External *eve;

   eve = container_of(iface, E_Video_External, base);
   e_client_video_comp_redirect_unset(eve->ecv);

   free(eve);
}

static void
_e_video_external_init(E_Client_Video *ecv)
{
   e_client_video_comp_redirect_set(ecv);
}

EINTERN E_Video_Comp_Iface *
e_video_external_iface_create(E_Client_Video *ecv)
{
   E_Video_External *eve;

   INF("Intializing External Compositing mode");

   eve = E_NEW(E_Video_External, 1);
   if (!eve)
     {
        ERR("Failed to create E_Video_External");
        return NULL;
     }

   _e_video_external_init(ecv);

   eve->ecv = ecv;
   eve->base.destroy = _e_video_external_iface_destroy;

   return &eve->base;
}
