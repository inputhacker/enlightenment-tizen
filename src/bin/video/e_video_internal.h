#ifndef _E_VIDEO_INTERNAL_H_
#define _E_VIDEO_INTERNAL_H_

#include "e.h"

#include <tdm.h>
#include <values.h>
#include <tdm_helper.h>
#include <tbm_surface.h>
#include <wayland-tbm-server.h>

#define GEO_FMT   "%dx%d(%dx%d+%d+%d) -> (%dx%d+%d+%d) transform(%d)"
#define GEO_ARG(g) \
   (g)->input_w, (g)->input_h, \
   (g)->input_r.w, (g)->input_r.h, (g)->input_r.x, (g)->input_r.y, \
   (g)->output_r.w, (g)->output_r.h, (g)->output_r.x, (g)->output_r.y, \
   (g)->transform

typedef struct _E_Video_Comp_Iface E_Video_Comp_Iface;

struct _E_Video_Comp_Iface
{
   void            (*destroy)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*follow_topmost_visibility)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*unfollow_topmost_visibility)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*allowed_property)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*disallowed_property)(E_Video_Comp_Iface *iface);
   Eina_Bool       (*property_get)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value);
   Eina_Bool       (*property_set)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value);
   Eina_Bool       (*property_delay_set)(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value);
   Eina_Bool       (*available_properties_get)(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count);
   /* FIXME for hwc windows mode */
   Eina_Bool       (*info_get)(E_Video_Comp_Iface *iface, E_Client_Video_Info *info);
   Eina_Bool       (*commit_data_release)(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec);
   tbm_surface_h   (*tbm_surface_get)(E_Video_Comp_Iface *iface);
};

EINTERN E_Video_Comp_Iface  *e_video_hwc_planes_iface_create(E_Client *ec);
EINTERN E_Video_Comp_Iface  *e_video_hwc_windows_iface_create(E_Client *ec);
EINTERN E_Video_Comp_Iface  *e_video_fallback_iface_create(E_Client *ec);

#endif
