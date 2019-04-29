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
   E_Video_External *evs;

   evs = container_of(iface, E_Video_External, base);
   e_client_video_comp_redirect_unset(evs->ecv);

   free(evs);
}

static void
_e_video_external_init(E_Client_Video *ecv)
{
   e_client_video_comp_redirect_set(ecv);
}

EINTERN E_Video_Comp_Iface *
e_video_external_iface_create(E_Client_Video *ecv)
{
   E_Video_External *evs;

   INF("Intializing External Compositing mode");

   evs = E_NEW(E_Video_External, 1);
   if (!evs)
     {
        ERR("Failed to create E_Video_External");
        return NULL;
     }

   _e_video_external_init(ecv);

   evs->ecv = ecv;
   evs->base.destroy = _e_video_external_iface_destroy;

   return &evs->base;
}
