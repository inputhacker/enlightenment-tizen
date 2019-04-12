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

typedef struct _E_Video_Hwc E_Video_Hwc;
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

struct _E_Video_Hwc
{
   E_Video_Comp_Iface iface;

   E_Hwc_Policy hwc_policy;
   /* FIXME: Workaround */
   E_Video_Comp_Iface backend;

   E_Client *ec;
   Ecore_Window window;
   tdm_output *output;
   E_Output *e_output;

   Eina_List *ec_event_handler;

   /* input info */
   tbm_format tbmfmt;
   Eina_List *input_buffer_list;

   /* in screen coordinates */
   E_Video_Hwc_Geometry geo, old_geo;

   E_Comp_Wl_Buffer *old_comp_buffer;

   /* converter info */
   tbm_format pp_tbmfmt;
   tdm_pp *pp;
   Eina_Rectangle pp_r;    /* converter dst content rect */
   Eina_List *pp_buffer_list;
   Eina_List *next_buffer;
   Eina_Bool pp_scanout;

   int pp_align;
   int pp_minw, pp_minh, pp_maxw, pp_maxh;
   int video_align;

   /* When a video buffer be attached, it will be appended to the end of waiting_list .
    * And when it's committed, it will be moved to committed_list.
    * Finally when the commit handler is called, it will become current_fb.
    */
   Eina_List    *waiting_list;   /* buffers which are not committed yet */
   Eina_List    *committed_list; /* buffers which are committed, but not shown on screen yet */
   E_Comp_Wl_Video_Buf *current_fb;     /* buffer which is showing on screen currently */
   Eina_Bool     waiting_vblank;

   Eina_Bool  cb_registered;
   Eina_Bool  need_force_render;
   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;
};

EINTERN E_Video_Hwc *e_video_hwc_planes_create(void);
EINTERN E_Video_Hwc *e_video_hwc_windows_create(void);
EINTERN Eina_Bool    e_video_hwc_planes_init(E_Video_Hwc *evh);
EINTERN Eina_Bool    e_video_hwc_windows_init(E_Video_Hwc *evh);

EINTERN E_Client    *e_video_hwc_child_client_get(E_Client *ec);
EINTERN E_Client    *e_video_hwc_client_offscreen_parent_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_client_visible_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_client_parent_viewable_get(E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_geometry_get(E_Client *ec, E_Video_Hwc_Geometry *out);
EINTERN Eina_Bool    e_video_hwc_geometry_map_apply(E_Client *ec, E_Video_Hwc_Geometry *out);

EINTERN E_Comp_Wl_Video_Buf *e_video_hwc_vbuf_find(Eina_List *list, tbm_surface_h buffer);
EINTERN E_Comp_Wl_Video_Buf *e_video_hwc_vbuf_find_with_comp_buffer(Eina_List *list, E_Comp_Wl_Buffer *comp_buffer);

EINTERN tbm_format  e_video_hwc_comp_buffer_tbm_format_get(E_Comp_Wl_Buffer *comp_buffer);
#endif
