#include "e_video_internal.h"

typedef struct _E_Video_External E_Video_External;

struct _E_Video_External
{
   E_Video_Comp_Iface base;
   E_Client *ec;
};

static void
_e_video_external_iface_destroy(E_Video_Comp_Iface *iface)
{
   E_Video_External *evs;

   evs = container_of(iface, E_Video_External, base);

   if (!e_object_is_del(E_OBJECT(evs->ec)))
     {
        /* 'ec->comp_data' is supposed to be freed when ec is deleted. */
        evs->ec->comp_data->video_client = 0;
     }

   free(evs);
}

static void
_e_video_external_init(E_Client *ec)
{
   /* Set video_client flag so that 'e_comp_wl' can ignore it. */
   ec->comp_data->video_client = 1;
}

EINTERN E_Video_Comp_Iface *
e_video_external_iface_create(E_Client *ec)
{
   E_Video_External *evs;

   INF("Intializing External Compositing mode");

   evs = E_NEW(E_Video_External, 1);
   if (!evs)
     {
        ERR("Failed to create E_Video_External");
        return NULL;
     }

   _e_video_external_init(ec);

   evs->ec = ec;
   evs->base.destroy = _e_video_external_iface_destroy;

   return &evs->base;
}
