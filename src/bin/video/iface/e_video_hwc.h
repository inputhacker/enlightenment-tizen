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
typedef struct _E_Video_Hwc_Iface E_Video_Hwc_Iface;
typedef struct _E_Video_Hwc_Geometry E_Video_Hwc_Geometry;

struct _E_Video_Hwc_Iface
{
   void           (*destroy)(E_Video_Hwc *evh);
   Eina_Bool      (*property_get)(E_Video_Hwc *evh, unsigned int id, tdm_value *value);
   Eina_Bool      (*property_set)(E_Video_Hwc *evh, unsigned int id, tdm_value value);
   Eina_Bool      (*available_properties_get)(E_Video_Hwc *evh, const tdm_prop **props, int *count);
   Eina_Bool      (*buffer_commit)(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf);
   Eina_Bool      (*check_if_pp_needed)(E_Video_Hwc *evh);
   Eina_Bool      (*commit_available_check)(E_Video_Hwc *evh);
   tbm_surface_h  (*displaying_buffer_get)(E_Video_Hwc *evh);
};

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
   E_Video_Hwc_Iface backend;

   E_Hwc_Policy hwc_policy;

   E_Client *ec;
   Ecore_Window window;
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
   Eina_List    *bqueue;   /* A queue for buffer which will have to be committed next time. */
   Eina_List    *committed_list; /* buffers which are committed, but not shown on screen yet */
   E_Comp_Wl_Video_Buf *current_fb;     /* buffer which is showing on screen currently */

   Eina_Bool  cb_registered;
   Eina_Bool  need_force_render;
   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;
};

/* Functions for HWC */
EINTERN void         e_video_hwc_show(E_Video_Hwc *evh);
EINTERN void         e_video_hwc_wait_buffer_commit(E_Video_Hwc *evh);
EINTERN void         e_video_hwc_client_mask_update(E_Video_Hwc *evh);
EINTERN Eina_Bool    e_video_hwc_current_fb_update(E_Video_Hwc *evh);
EINTERN E_Client    *e_video_hwc_client_offscreen_parent_get(E_Client *ec);

/* Functions for HWC Planes */
EINTERN E_Video_Hwc *e_video_hwc_planes_create(E_Output *output, E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_planes_properties_commit(E_Video_Hwc *evh);
EINTERN Eina_Bool    e_video_hwc_planes_property_delay_set(E_Video_Hwc *evh, unsigned int id, tdm_value value);

/* Functions for HWC Windows */
EINTERN E_Video_Hwc *e_video_hwc_windows_create(E_Output *output, E_Client *ec);
EINTERN Eina_Bool    e_video_hwc_windows_info_get(E_Video_Hwc *evh, E_Client_Video_Info *info);
EINTERN Eina_Bool    e_video_hwc_windows_commit_data_release(E_Video_Hwc *evh, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec);
EINTERN tbm_surface_h   e_video_hwc_windows_tbm_surface_get(E_Video_Hwc *evh);

#endif
