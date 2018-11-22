#include "../e_video_internal.h"

typedef struct _E_Video_Fallback E_Video_Fallback;

struct _E_Video_Fallback
{
   E_Video_Comp_Iface base;
   E_Client *ec;
};

static void
_e_video_fallback_iface_destroy(E_Video_Comp_Iface *iface)
{
   E_Video_Fallback *evs;

   evs = container_of(iface, E_Video_Fallback, base);

   /* Unset animatable lock */
   e_policy_animatable_lock(evs->ec, E_POLICY_ANIMATABLE_NEVER, 0);

   free(evs);
}

static void
_e_video_fallback_init(E_Client *ec)
{
   /* software compositing */
   ec->comp_data->video_client = 0;
   /* Set animatable lock */
   e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, 1);
}

EINTERN E_Video_Comp_Iface *
e_video_fallback_iface_create(E_Client *ec)
{
   E_Video_Fallback *evs;

   INF("Intializing SW Compositing mode");

   evs = E_NEW(E_Video_Fallback, 1);
   if (!evs)
     {
        ERR("Failed to create E_Video_Fallback");
        return NULL;
     }

   _e_video_fallback_init(ec);

   evs->ec = ec;
   evs->base.destroy = _e_video_fallback_iface_destroy;

   return &evs->base;
}
