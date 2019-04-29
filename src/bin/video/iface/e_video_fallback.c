#include "e_video_internal.h"

typedef struct _E_Video_Fallback E_Video_Fallback;

struct _E_Video_Fallback
{
   E_Video_Comp_Iface base;
   E_Client_Video *ecv;
};

static void
_e_video_fallback_iface_destroy(E_Video_Comp_Iface *iface)
{
   E_Video_Fallback *evs;
   E_Client *ec;

   evs = container_of(iface, E_Video_Fallback, base);

   /* Unset animatable lock */
   ec = e_client_video_ec_get(evs->ecv);
   e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, 0);

   free(evs);
}

static void
_e_video_fallback_init(E_Client_Video *ecv)
{
   E_Client *ec;

   /* software compositing */
   e_client_video_comp_redirect_unset(ecv);

   /* Set animatable lock */
   ec = e_client_video_ec_get(ecv);
   e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, 1);
}

EINTERN E_Video_Comp_Iface *
e_video_fallback_iface_create(E_Client_Video *ecv)
{
   E_Video_Fallback *evs;

   INF("Intializing SW Compositing mode");

   evs = E_NEW(E_Video_Fallback, 1);
   if (!evs)
     {
        ERR("Failed to create E_Video_Fallback");
        return NULL;
     }

   _e_video_fallback_init(ecv);

   evs->ecv = ecv;
   evs->base.destroy = _e_video_fallback_iface_destroy;

   return &evs->base;
}
