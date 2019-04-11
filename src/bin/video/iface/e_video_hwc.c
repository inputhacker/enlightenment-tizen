#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

#define IFACE_ENTRY                                      \
   E_Video_Hwc *evh;                                    \
   evh = container_of(iface, E_Video_Hwc, base)

typedef struct _E_Video_Hwc E_Video_Hwc;

struct _E_Video_Hwc
{
   E_Video_Comp_Iface base;
   E_Video_Comp_Iface *backend;
};

static void
_e_video_hwc_iface_destroy(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evh->backend->destroy(evh->backend);
}

static Eina_Bool
_e_video_hwc_iface_follow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->backend->follow_topmost_visibility)
     return evh->backend->follow_topmost_visibility(evh->backend);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_unfollow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->backend->unfollow_topmost_visibility)
     return evh->backend->unfollow_topmost_visibility(evh->backend);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_allowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->backend->allowed_property)
     return evh->backend->allowed_property(evh->backend);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_disallowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->backend->disallowed_property)
     return evh->backend->disallowed_property(evh->backend);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   IFACE_ENTRY;

   if (evh->backend->property_get)
     return evh->backend->property_get(evh->backend, id, value);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   IFACE_ENTRY;

   if (evh->backend->property_set)
     return evh->backend->property_set(evh->backend, id, value);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_property_delay_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   IFACE_ENTRY;

   if (evh->backend->property_delay_set)
     return evh->backend->property_delay_set(evh->backend, id, value);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   IFACE_ENTRY;

   if (evh->backend->available_properties_get)
     return evh->backend->available_properties_get(evh->backend, props, count);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   IFACE_ENTRY;

   if (evh->backend->info_get)
     return evh->backend->info_get(evh->backend, info);
   return EINA_FALSE;
}

static Eina_Bool
_e_video_hwc_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   if (evh->backend->commit_data_release)
     return evh->backend->commit_data_release(evh->backend, sequence, tv_sec, tv_usec);
   return EINA_FALSE;
}

static tbm_surface_h
_e_video_hwc_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (evh->backend->tbm_surface_get)
     return evh->backend->tbm_surface_get(evh->backend);
   return NULL;
}

static E_Video_Hwc *
_e_video_hwc_create(E_Client *ec)
{
   E_Video_Hwc *evh;
   E_Hwc_Policy hwc_pol;

   evh = E_NEW(E_Video_Hwc, 1);
   if (!evh)
     {
        VER("Failed to allocate memory for 'E_Video_Hwc'", ec);
        return NULL;
     }

   hwc_pol = e_zone_video_hwc_policy_get(ec->zone);
   if (hwc_pol == E_HWC_POLICY_PLANES)
     evh->backend = e_video_hwc_planes_iface_create(ec);
   else if (hwc_pol == E_HWC_POLICY_WINDOWS)
     evh->backend = e_video_hwc_windows_iface_create(ec);

   if (!evh->backend)
     {
        VER("Failed to create backend interface", ec);
        free(evh);
        return NULL;
     }

   return evh;
}

EINTERN E_Video_Comp_Iface *
e_video_hwc_iface_create(E_Client *ec)
{
   E_Video_Hwc *evh;

   VIN("Create HWC interface", ec);

   evh = _e_video_hwc_create(ec);
   if (!evh)
     {
        VER("Failed to create 'E_Video_Hwc'", ec);
        return NULL;
     }

   evh->base.destroy = _e_video_hwc_iface_destroy;
   evh->base.follow_topmost_visibility = _e_video_hwc_iface_follow_topmost_visibility;
   evh->base.unfollow_topmost_visibility = _e_video_hwc_iface_unfollow_topmost_visibility;
   evh->base.allowed_property = _e_video_hwc_iface_allowed_property;
   evh->base.disallowed_property = _e_video_hwc_iface_disallowed_property;
   evh->base.property_get = _e_video_hwc_iface_property_get;
   evh->base.property_set = _e_video_hwc_iface_property_set;
   evh->base.property_delay_set = _e_video_hwc_iface_property_delay_set;
   evh->base.available_properties_get = _e_video_hwc_iface_available_properties_get;
   evh->base.info_get = _e_video_hwc_iface_info_get;
   evh->base.commit_data_release = _e_video_hwc_iface_commit_data_release;
   evh->base.tbm_surface_get = _e_video_hwc_iface_tbm_surface_get;

   return &evh->base;
}
