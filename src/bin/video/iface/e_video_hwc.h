#ifndef _E_VIDEO_HWC_H_
#define _E_VIDEO_HWC_H_

#include <string.h>
#include <Eina.h>
#include "e.h"
#include "e_video_internal.h"

#define BUFFER_MAX_COUNT   5

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#endif

typedef struct _E_Video_Hwc_Geometry E_Video_Hwc_Geometry;

struct _E_Video_Hwc_Geometry
{
   int input_w, input_h;      /* input buffer's size */
   Eina_Rectangle input_r;    /* input buffer's content rect */
   Eina_Rectangle output_r;   /* video plane rect */
   uint transform;            /* rotate, flip */

   struct {
        Eina_Rectangle output_r; /* video plane rect in physical output coordinates */
        uint transform;          /* rotate, flip in physical output coordinates */
   } tdm;
};

EINTERN E_Video_Comp_Iface  *e_video_hwc_planes_iface_create(E_Client *ec);
EINTERN E_Video_Comp_Iface  *e_video_hwc_windows_iface_create(E_Client *ec);

EINTERN E_Client    *e_video_hwc_child_client_get(E_Client *ec);
EINTERN E_Client    *e_video_hwc_client_offscreen_parent_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_client_visible_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_client_parent_viewable_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_geometry_get(E_Client *ec, E_Video_Hwc_Geometry *out);
EINTERN Eina_Bool    e_video_hwc_geometry_map_apply(E_Client *ec, E_Video_Hwc_Geometry *out);

EINTERN E_Comp_Wl_Video_Buf *e_video_hwc_vbuf_find(Eina_List *list, tbm_surface_h buffer);
EINTERN E_Comp_Wl_Video_Buf *e_video_hwc_vbuf_find_with_comp_buffer(Eina_List *list, E_Comp_Wl_Buffer *comp_buffer);
#endif
