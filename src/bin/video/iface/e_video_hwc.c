#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

EINTERN E_Video_Comp_Iface *
e_video_hwc_iface_create(E_Client *ec)
{
   E_Video_Comp_Iface *iface = NULL;
   E_Hwc_Policy hwc_pol;

   VIN("Create HWC interface", ec);

   hwc_pol = e_zone_video_hwc_policy_get(ec->zone);
   if (hwc_pol == E_HWC_POLICY_PLANES)
     iface = e_video_hwc_planes_iface_create(ec);
   else if (hwc_pol == E_HWC_POLICY_WINDOWS)
     iface = e_video_hwc_windows_iface_create(ec);

   return iface;
}
