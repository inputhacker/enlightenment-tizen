#ifndef _E_VIDEO_INTERNAL_H_
#define _E_VIDEO_INTERNAL_H_

#include "e.h"

#include <tdm.h>
#include <tbm_surface.h>

#ifdef VER
#undef VER
#endif

#ifdef VWR
#undef VWR
#endif

#ifdef VIN
#undef VIN
#endif

#ifdef VDB
#undef VDB
#endif

#define VER(fmt, ec, arg...)   ELOGF("VIDEO <ERR>", fmt, ec, ##arg)
#define VWR(fmt, ec, arg...)   ELOGF("VIDEO <WRN>", fmt, ec, ##arg)
#define VIN(fmt, ec, arg...)   ELOGF("VIDEO <INF>", fmt, ec, ##arg)
#define VDB(fmt, ec, arg...)   DBG("window(0x%08"PRIxPTR") ec(%p): "fmt, \
                                 e_client_util_win_get(ec), ec, ##arg)

#undef NEVER_GET_HERE
#define NEVER_GET_HERE()     CRI("** need to improve more **")

#define GEO_FMT   "%dx%d(%dx%d+%d+%d) -> (%dx%d+%d+%d) transform(%d)"
#define GEO_ARG(g) \
   (g)->input_w, (g)->input_h, \
   (g)->input_r.w, (g)->input_r.h, (g)->input_r.x, (g)->input_r.y, \
   (g)->output_r.w, (g)->output_r.h, (g)->output_r.x, (g)->output_r.y, \
   (g)->transform

typedef struct _E_Client_Video E_Client_Video;
typedef struct _E_Video_Comp_Iface E_Video_Comp_Iface;

struct _E_Video_Comp_Iface
{
   void            (*destroy)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*property_get)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value);
   Eina_Bool       (*property_set)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value);
   Eina_Bool       (*property_delay_set)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value);
   Eina_Bool       (*available_properties_get)(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count);
   /* FIXME for hwc windows mode */
   Eina_Bool       (*info_get)(E_Video_Comp_Iface *iface, E_Client_Video_Info *info);
   Eina_Bool       (*commit_data_release)(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec);
   tbm_surface_h   (*tbm_surface_get)(E_Video_Comp_Iface *iface);
};

EINTERN E_Hwc_Policy         e_zone_video_hwc_policy_get(E_Zone *zone);
EINTERN E_Video_Comp_Iface  *e_video_hwc_iface_create(E_Client_Video *ecv);
EINTERN E_Video_Comp_Iface  *e_video_fallback_iface_create(E_Client_Video *ecv);
EINTERN E_Video_Comp_Iface  *e_video_external_iface_create(E_Client_Video *ecv);
EINTERN E_Client            *e_client_video_ec_get(E_Client_Video *ecv);
EINTERN void                 e_client_video_comp_redirect_set(E_Client_Video *ecv);
EINTERN void                 e_client_video_comp_redirect_unset(E_Client_Video *ecv);
EINTERN Eina_Bool            e_client_video_property_allow_get(E_Client_Video *ecv);

#endif
