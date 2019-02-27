#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "../e_video_internal.h"

#define VER(fmt, arg...)   ELOGF("VIDEO", "<ERR> window(0x%08"PRIxPTR"): "fmt, \
                                 evhw->ec->pixmap, evhw->ec, evhw->window, ##arg)
#define VWR(fmt, arg...)   ELOGF("VIDEO", "<WRN> window(0x%08"PRIxPTR"): "fmt, \
                                 evhw->ec->pixmap, evhw->ec, evhw->window, ##arg)
#define VIN(fmt, arg...)   ELOGF("VIDEO", "<INF> window(0x%08"PRIxPTR"): "fmt, \
                                 evhw->ec->pixmap, evhw->ec, evhw->window, ##arg)
#define VDB(fmt, arg...)   DBG("window(0x%08"PRIxPTR") ec(%p): "fmt, evhw->window, evhw->ec, ##arg)

//#define DUMP_BUFFER
#define CHECKING_PRIMARY_ZPOS

#define BUFFER_MAX_COUNT   5
#define MIN_WIDTH   32

#undef NEVER_GET_HERE
#define NEVER_GET_HERE()     CRI("** need to improve more **")

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#endif

#define IFACE_ENTRY                                      \
   E_Video_Hwc_Windows *evhw;                              \
   evhw = container_of(iface, E_Video_Hwc_Windows, base)

typedef struct _E_Video_Hwc_Windows E_Video_Hwc_Windows;

struct _E_Video_Hwc_Windows
{
   E_Video_Comp_Iface base;

   E_Client *ec;
   Ecore_Window window;
   tdm_output *output;
   E_Output *e_output;
   E_Hwc_Window *hwc_window;
   E_Hwc *hwc;
   E_Client_Video_Info info;
   tbm_surface_h cur_tsurface; // tsurface to be set this layer.
   E_Client *e_client;
   Eina_List *ec_event_handler;

   /* input info */
   tbm_format tbmfmt;
   Eina_List *input_buffer_list;

   /* in screen coordinates */
   struct
     {
        int input_w, input_h;    /* input buffer's size */
        Eina_Rectangle input_r;  /* input buffer's content rect */
        Eina_Rectangle output_r; /* video plane rect */
        uint transform;          /* rotate, flip */

        Eina_Rectangle tdm_output_r; /* video plane rect in physical output coordinates */
        uint tdm_transform;          /* rotate, flip in physical output coordinates */
     } geo, old_geo;

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

   /* attributes */
   int tdm_mute_id;

   Eina_Bool  cb_registered;
   Eina_Bool  need_force_render;
   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;
};

static Eina_List *video_list = NULL;

static void      _e_video_destroy(E_Video_Hwc_Windows *evhw);
static void      _e_video_render(E_Video_Hwc_Windows *evhw, const char *func);
static Eina_Bool _e_video_frame_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf);
static void      _e_video_vblank_handler(tdm_output *output, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);

static void
buffer_transform(int width, int height, uint32_t transform, int32_t scale,
                 int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         *dx = sx, *dy = sy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         *dx = width - sx, *dy = sy;
         break;
      case WL_OUTPUT_TRANSFORM_90:
         *dx = height - sy, *dy = sx;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         *dx = height - sy, *dy = width - sx;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         *dx = width - sx, *dy = height - sy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         *dx = sx, *dy = height - sy;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         *dx = sy, *dy = width - sx;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         *dx = sy, *dy = sx;
         break;
     }

   *dx *= scale;
   *dy *= scale;
}

static E_Client *
find_video_child_get(E_Client *ec)
{
   E_Client *subc = NULL;
   Eina_List *l;
   if (!ec) return NULL;
   if (e_object_is_del(E_OBJECT(ec))) return NULL;
   if (!ec->comp_data) return NULL;

   if (ec->comp_data->video_client) return ec;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        E_Client *temp= NULL;
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        temp = find_video_child_get(subc);
        if(temp) return temp;
     }

   return NULL;
}

static E_Client *
find_offscreen_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || !ec->comp_data->sub.data)
     return NULL;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        if (!parent->comp_data || !parent->comp_data->sub.data)
          return NULL;

        if (parent->comp_data->sub.data->remote_surface.offscreen_parent)
          return parent->comp_data->sub.data->remote_surface.offscreen_parent;

        parent = parent->comp_data->sub.data->parent;
     }

   return NULL;
}

static E_Comp_Wl_Video_Buf *
_e_video_vbuf_find(Eina_List *list, tbm_surface_h buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(list, l, vbuf)
     {
        if (vbuf->tbm_surface == buffer)
          return vbuf;
     }

   return NULL;
}

static E_Comp_Wl_Video_Buf *
_e_video_vbuf_find_with_comp_buffer(Eina_List *list, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(list, l, vbuf)
     {
        if (vbuf->comp_buffer == comp_buffer)
          return vbuf;
     }

   return NULL;
}

static Eina_Bool
_e_video_is_visible(E_Video_Hwc_Windows *evhw)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(evhw->ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(evhw->ec->pixmap))
     {
        VDB("no comp buffer");
        return EINA_FALSE;
     }

   if (evhw->ec->comp_data->sub.data && evhw->ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = find_offscreen_parent_get(evhw->ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured");
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(evhw->ec->frame))
     {
        VDB("evas obj invisible");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_parent_is_viewable(E_Video_Hwc_Windows *evhw)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(evhw->ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(evhw->ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == evhw->ec)
     {
        VDB("There is no video parent surface");
        return EINA_FALSE;
     }

   if (!topmost_parent->visible)
     {
        VDB("parent(0x%08"PRIxPTR") not viewable", (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   if (!e_pixmap_resource_get(topmost_parent->pixmap))
     {
        VDB("parent(0x%08"PRIxPTR") no comp buffer", (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_video_input_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc_Windows *evhw = data;
   Eina_Bool need_hide = EINA_FALSE;

   DBG("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   evhw->input_buffer_list = eina_list_remove(evhw->input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (evhw->current_fb == vbuf)
     {
        VIN("current fb destroyed");
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);
        evhw->current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhw->committed_list, vbuf))
     {
        VIN("committed fb destroyed");
        evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhw->waiting_list, vbuf))
     {
        VIN("waiting fb destroyed");
        evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);
     }

   if (need_hide)
     _e_video_frame_buffer_show(evhw, NULL);
}

static Eina_Bool
_e_video_input_buffer_scanout_check(E_Comp_Wl_Video_Buf *vbuf)
{
   tbm_surface_h tbm_surface = NULL;
   tbm_bo bo = NULL;
   int flag;

   tbm_surface = vbuf->tbm_surface;
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, EINA_FALSE);

   bo = tbm_surface_internal_get_bo(tbm_surface, 0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(bo, EINA_FALSE);

   flag = tbm_bo_get_flags(bo);
   if (flag == TBM_BO_SCANOUT)
      return EINA_TRUE;

   return EINA_FALSE;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_copy(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buf, E_Comp_Wl_Video_Buf *vbuf, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *temp = NULL;
   int aligned_width = ROUNDUP(vbuf->width_from_pitch, evhw->pp_align);

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = comp_buf;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)",
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);
   e_comp_wl_video_buffer_unref(vbuf);

   evhw->geo.input_w = vbuf->width_from_pitch;
#ifdef DUMP_BUFFER
   char file[256];
   static int i;
   snprintf(file, sizeof file, "/tmp/dump/%s_%d.png", "cpy", i++);
   tdm_helper_dump_buffer(temp->tbm_surface, file);
#endif

   return temp;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_get(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buffer, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_Bool need_pp_scanout = EINA_FALSE;

   vbuf = _e_video_vbuf_find_with_comp_buffer(evhw->input_buffer_list, comp_buffer);
   if (vbuf)
     {
        vbuf->content_r = evhw->geo.input_r;
        return vbuf;
     }

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   if (evhw->pp_scanout)
     {
        Eina_Bool input_buffer_scanout = EINA_FALSE;
        input_buffer_scanout = _e_video_input_buffer_scanout_check(vbuf);
        if (!input_buffer_scanout) need_pp_scanout = EINA_TRUE;
     }

   if (evhw->pp)
     {
        if ((evhw->pp_align != -1 && (vbuf->width_from_pitch % evhw->pp_align)) ||
            need_pp_scanout)
          {
             E_Comp_Wl_Video_Buf *temp;

             if (need_pp_scanout)
               temp = _e_video_input_buffer_copy(evhw, comp_buffer, vbuf, EINA_TRUE);
             else
               temp = _e_video_input_buffer_copy(evhw, comp_buffer, vbuf, scanout);
             if (!temp)
               {
                  e_comp_wl_video_buffer_unref(vbuf);
                  return NULL;
               }
             vbuf = temp;
          }
     }

   vbuf->content_r = evhw->geo.input_r;

   evhw->input_buffer_list = eina_list_append(evhw->input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_input_buffer_cb_free, evhw);

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p) created, refcnt:%d"
       " scanout=%d", e_client_util_name_get(evhw->ec) ?: "No Name" ,
       evhw->ec->netwm.pid, wl_resource_get_id(evhw->ec->comp_data->surface), vbuf,
       vbuf->ref_cnt, scanout);

   return vbuf;
}

static void
_e_video_input_buffer_valid(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(evhw->input_buffer_list, l, vbuf)
     {
        tbm_surface_h tbm_surf;
        tbm_bo bo;
        uint32_t size = 0, offset = 0, pitch = 0;

        if (!vbuf->comp_buffer) continue;
        if (vbuf->resource == comp_buffer->resource)
          {
             WRN("got wl_buffer@%d twice", wl_resource_get_id(comp_buffer->resource));
             return;
          }

        tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
        bo = tbm_surface_internal_get_bo(tbm_surf, 0);
        tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

        if (vbuf->names[0] == tbm_bo_export(bo) && vbuf->offsets[0] == offset)
          {
             WRN("can tearing: wl_buffer@%d, wl_buffer@%d are same. gem_name(%d)",
                 wl_resource_get_id(vbuf->resource),
                 wl_resource_get_id(comp_buffer->resource), vbuf->names[0]);
             return;
          }
     }
}

static void
_e_video_pp_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc_Windows *evhw = data;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   if (evhw->current_fb == vbuf)
     evhw->current_fb = NULL;

   evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);

   evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);

   evhw->pp_buffer_list = eina_list_remove(evhw->pp_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf *
_e_video_pp_buffer_get(E_Video_Hwc_Windows *evhw, int width, int height)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   int i = 0;
   int aligned_width;

   if (evhw->video_align != -1)
     aligned_width = ROUNDUP(width, evhw->video_align);
   else
     aligned_width = width;

   if (evhw->pp_buffer_list)
     {
        vbuf = eina_list_data_get(evhw->pp_buffer_list);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

        /* if we need bigger pp_buffers, destroy all pp_buffers and create */
        if (aligned_width > vbuf->width_from_pitch || height != vbuf->height)
          {
             Eina_List *ll;

             VIN("pp buffer changed: %dx%d => %dx%d",
                 vbuf->width_from_pitch, vbuf->height,
                 aligned_width, height);

             EINA_LIST_FOREACH_SAFE(evhw->pp_buffer_list, l, ll, vbuf)
               {
                  /* free forcely */
                  e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
                  e_comp_wl_video_buffer_unref(vbuf);
               }
             if (evhw->pp_buffer_list)
               NEVER_GET_HERE();

             if (evhw->waiting_list)
               NEVER_GET_HERE();
          }
     }

   if (!evhw->pp_buffer_list)
     {
        for (i = 0; i < BUFFER_MAX_COUNT; i++)
          {
             vbuf = e_comp_wl_video_buffer_alloc(aligned_width, height, evhw->pp_tbmfmt, EINA_TRUE);
             EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

             e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_pp_buffer_cb_free, evhw);
             evhw->pp_buffer_list = eina_list_append(evhw->pp_buffer_list, vbuf);

          }

        VIN("pp buffer created: %dx%d, %c%c%c%c",
            vbuf->width_from_pitch, height, FOURCC_STR(evhw->pp_tbmfmt));

        evhw->next_buffer = evhw->pp_buffer_list;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw->pp_buffer_list, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw->next_buffer, NULL);

   l = evhw->next_buffer;
   while ((vbuf = evhw->next_buffer->data))
     {
        evhw->next_buffer = (evhw->next_buffer->next) ? evhw->next_buffer->next : evhw->pp_buffer_list;

        if (!vbuf->in_use)
          return vbuf;

        if (l == evhw->next_buffer)
          {
             VWR("all video framebuffers in use (max:%d)", BUFFER_MAX_COUNT);
             return NULL;
          }
     }

   return NULL;
}

/* convert from logical screen to physical output */
static void
_e_video_geometry_cal_physical(E_Video_Hwc_Windows *evhw)
{
   E_Zone *zone;
   E_Comp_Wl_Output *output;
   E_Client *topmost;
   int tran, flip;
   int transform;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   EINA_SAFETY_ON_NULL_GOTO(topmost, normal);

   output = e_comp_wl_output_find(topmost);
   EINA_SAFETY_ON_NULL_GOTO(output, normal);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_GOTO(zone, normal);

   tran = evhw->geo.transform & 0x3;
   flip = evhw->geo.transform & 0x4;
   transform = flip + (tran + output->transform) % 4;
   switch(transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
         evhw->geo.tdm_transform = TDM_TRANSFORM_270;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         evhw->geo.tdm_transform = TDM_TRANSFORM_180;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         evhw->geo.tdm_transform = TDM_TRANSFORM_90;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
         break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         evhw->geo.tdm_transform = TDM_TRANSFORM_NORMAL;
         break;
     }

   if (output->transform % 2)
     {
        if (evhw->geo.tdm_transform == TDM_TRANSFORM_FLIPPED)
          evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
        else if (evhw->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_90)
          evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
        else if (evhw->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_180)
          evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
        else if (evhw->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_270)
          evhw->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
     }

   if (output->transform == 0)
     evhw->geo.tdm_output_r = evhw->geo.output_r;
   else
     e_comp_wl_rect_convert(zone->w, zone->h, output->transform, 1,
                            evhw->geo.output_r.x, evhw->geo.output_r.y,
                            evhw->geo.output_r.w, evhw->geo.output_r.h,
                            &evhw->geo.tdm_output_r.x, &evhw->geo.tdm_output_r.y,
                            &evhw->geo.tdm_output_r.w, &evhw->geo.tdm_output_r.h);

   VDB("geomtry: screen(%d,%d %dx%d | %d) => %d => physical(%d,%d %dx%d | %d)",
       EINA_RECTANGLE_ARGS(&evhw->geo.output_r), evhw->geo.transform, transform,
       EINA_RECTANGLE_ARGS(&evhw->geo.tdm_output_r), evhw->geo.tdm_transform);

   return;
normal:
   evhw->geo.tdm_output_r = evhw->geo.output_r;
   evhw->geo.tdm_transform = evhw->geo.transform;
}

static Eina_Bool
_e_video_geometry_cal_viewport(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec = evhw->ec;
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   E_Comp_Wl_Subsurf_Data *sdata;
   int x1, y1, x2, y2;
   int tx1, ty1, tx2, ty2;
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbm_surf;
   uint32_t size = 0, offset = 0, pitch = 0;
   int bw, bh;
   int width_from_buffer, height_from_buffer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   comp_buffer = e_pixmap_resource_get(evhw->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_buffer, EINA_FALSE);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surf, EINA_FALSE);

   tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

   /* input geometry */
   if (IS_RGB(evhw->tbmfmt))
     evhw->geo.input_w = pitch / 4;
   else
     evhw->geo.input_w = pitch;

   evhw->geo.input_h = tbm_surface_get_height(tbm_surf);

   bw = tbm_surface_get_width(tbm_surf);
   bh = tbm_surface_get_height(tbm_surf);
   VDB("TBM buffer size %d %d", bw, bh);

   switch (vp->buffer.transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
      case WL_OUTPUT_TRANSFORM_270:
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         width_from_buffer = bh / vp->buffer.scale;
         height_from_buffer = bw / vp->buffer.scale;
         break;
      default:
         width_from_buffer = bw / vp->buffer.scale;
         height_from_buffer = bh / vp->buffer.scale;
         break;
     }


   if (vp->buffer.src_width == wl_fixed_from_int(-1))
     {
        x1 = 0.0;
        y1 = 0.0;
        x2 = width_from_buffer;
        y2 = height_from_buffer;
     }
   else
     {
        x1 = wl_fixed_to_int(vp->buffer.src_x);
        y1 = wl_fixed_to_int(vp->buffer.src_y);
        x2 = wl_fixed_to_int(vp->buffer.src_x + vp->buffer.src_width);
        y2 = wl_fixed_to_int(vp->buffer.src_y + vp->buffer.src_height);
     }

   VDB("transform(%d) scale(%d) buffer(%dx%d) src(%d,%d %d,%d) viewport(%dx%d)",
       vp->buffer.transform, vp->buffer.scale,
       width_from_buffer, height_from_buffer,
       x1, y1, x2 - x1, y2 - y1,
       ec->comp_data->width_from_viewport, ec->comp_data->height_from_viewport);

   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x1, y1, &tx1, &ty1);
   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x2, y2, &tx2, &ty2);

   evhw->geo.input_r.x = (tx1 <= tx2) ? tx1 : tx2;
   evhw->geo.input_r.y = (ty1 <= ty2) ? ty1 : ty2;
   evhw->geo.input_r.w = (tx1 <= tx2) ? tx2 - tx1 : tx1 - tx2;
   evhw->geo.input_r.h = (ty1 <= ty2) ? ty2 - ty1 : ty1 - ty2;

   /* output geometry */
   if ((sdata = ec->comp_data->sub.data))
     {
        if (sdata->parent)
          {
             evhw->geo.output_r.x = sdata->parent->x + sdata->position.x;
             evhw->geo.output_r.y = sdata->parent->y + sdata->position.y;
          }
        else
          {
             evhw->geo.output_r.x = sdata->position.x;
             evhw->geo.output_r.y = sdata->position.y;
          }
     }
   else
     {
        evhw->geo.output_r.x = ec->x;
        evhw->geo.output_r.y = ec->y;
     }

   evhw->geo.output_r.w = ec->comp_data->width_from_viewport;
   evhw->geo.output_r.w = (evhw->geo.output_r.w + 1) & ~1;
   evhw->geo.output_r.h = ec->comp_data->height_from_viewport;

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   evhw->geo.output_r.x, evhw->geo.output_r.y,
                                   &evhw->geo.output_r.x, &evhw->geo.output_r.y);
   e_comp_object_frame_wh_unadjust(ec->frame,
                                   evhw->geo.output_r.w, evhw->geo.output_r.h,
                                   &evhw->geo.output_r.w, &evhw->geo.output_r.h);

   evhw->geo.transform = vp->buffer.transform;

   _e_video_geometry_cal_physical(evhw);

   VDB("geometry(%dx%d  %d,%d %dx%d  %d,%d %dx%d  %d)",
       evhw->geo.input_w, evhw->geo.input_h,
       EINA_RECTANGLE_ARGS(&evhw->geo.input_r),
       EINA_RECTANGLE_ARGS(&evhw->geo.output_r),
       evhw->geo.transform);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_geometry_cal_map(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec;
   const Evas_Map *m;
   Evas_Coord x1, x2, y1, y2;
   Eina_Rectangle output_r;

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, EINA_FALSE);

   ec = evhw->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->frame, EINA_FALSE);

   m = evas_object_map_get(ec->frame);
   if (!m) return EINA_TRUE;

   /* If frame has map, it means that ec's geometry is decided by map's geometry.
    * ec->x,y,w,h and ec->client.x,y,w,h is not useful.
    */

   evas_map_point_coord_get(m, 0, &x1, &y1, NULL);
   evas_map_point_coord_get(m, 2, &x2, &y2, NULL);

   output_r.x = x1;
   output_r.y = y1;
   output_r.w = x2 - x1;
   output_r.w = (output_r.w + 1) & ~1;
   output_r.h = y2 - y1;
   output_r.h = (output_r.h + 1) & ~1;

   if (!memcmp(&evhw->geo.output_r, &output_r, sizeof(Eina_Rectangle)))
     return EINA_FALSE;

   VDB("frame(%p) m(%p) output(%d,%d %dx%d) => (%d,%d %dx%d)", ec->frame, m,
       EINA_RECTANGLE_ARGS(&evhw->geo.output_r), EINA_RECTANGLE_ARGS(&output_r));

   evhw->geo.output_r = output_r;

   _e_video_geometry_cal_physical(evhw);

   return EINA_TRUE;
}

static void
_e_video_geometry_cal_to_input(int output_w, int output_h, int input_w, int input_h,
                               uint32_t trasnform, int ox, int oy, int *ix, int *iy)
{
   float ratio_w, ratio_h;

   switch(trasnform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         *ix = ox, *iy = oy;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         *ix = oy, *iy = output_w - ox;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         *ix = output_w - ox, *iy = output_h - oy;
         break;
      case WL_OUTPUT_TRANSFORM_90:
         *ix = output_h - oy, *iy = ox;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         *ix = output_w - ox, *iy = oy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         *ix = oy, *iy = ox;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         *ix = ox, *iy = output_h - oy;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         *ix = output_h - oy, *iy = output_w - ox;
         break;
     }
   if (trasnform & 0x1)
     {
        ratio_w = (float)input_w / output_h;
        ratio_h = (float)input_h / output_w;
     }
   else
     {
        ratio_w = (float)input_w / output_w;
        ratio_h = (float)input_h / output_h;
     }
   *ix *= ratio_w;
   *iy *= ratio_h;
}

static void
_e_video_geometry_cal_to_input_rect(E_Video_Hwc_Windows * evhw, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int xf1, yf1, xf2, yf2;

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_video_geometry_cal_to_input(evhw->geo.output_r.w, evhw->geo.output_r.h,
                                  evhw->geo.input_r.w, evhw->geo.input_r.h,
                                  evhw->geo.transform, xf1, yf1, &xf1, &yf1);
   _e_video_geometry_cal_to_input(evhw->geo.output_r.w, evhw->geo.output_r.h,
                                  evhw->geo.input_r.w, evhw->geo.input_r.h,
                                  evhw->geo.transform, xf2, yf2, &xf2, &yf2);

   drect->x = MIN(xf1, xf2);
   drect->y = MIN(yf1, yf2);
   drect->w = MAX(xf1, xf2) - drect->x;
   drect->h = MAX(yf1, yf2) - drect->y;
}

static Eina_Bool
_e_video_geometry_cal(E_Video_Hwc_Windows *evhw)
{
   Eina_Rectangle screen = {0,};
   Eina_Rectangle output_r = {0,}, input_r = {0,};
   E_Zone *zone;
   E_Client *topmost;

   /* get geometry information with buffer scale, transform and viewport. */
   if (!_e_video_geometry_cal_viewport(evhw))
     return EINA_FALSE;

   _e_video_geometry_cal_map(evhw);

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(topmost, EINA_FALSE);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   screen.w = zone->w;
   screen.h = zone->h;

   e_comp_wl_video_buffer_size_get(evhw->ec, &input_r.w, &input_r.h);
   // when topmost is not mapped, input size can be abnormal.
   // in this case, it will be render by topmost showing.
   if (!eina_rectangle_intersection(&evhw->geo.input_r, &input_r) || (evhw->geo.input_r.w <= 10 || evhw->geo.input_r.h <= 10))
     {
        VER("input area is empty");
        return EINA_FALSE;
     }

   if (evhw->geo.output_r.x >= 0 && evhw->geo.output_r.y >= 0 &&
       (evhw->geo.output_r.x + evhw->geo.output_r.w) <= screen.w &&
       (evhw->geo.output_r.y + evhw->geo.output_r.h) <= screen.h)
     return EINA_TRUE;

   /* TODO: need to improve */

   output_r = evhw->geo.output_r;
   if (!eina_rectangle_intersection(&output_r, &screen))
     {
        VER("output_r(%d,%d %dx%d) screen(%d,%d %dx%d) => intersect(%d,%d %dx%d)",
            EINA_RECTANGLE_ARGS(&evhw->geo.output_r),
            EINA_RECTANGLE_ARGS(&screen), EINA_RECTANGLE_ARGS(&output_r));
        return EINA_TRUE;
     }

   output_r.x -= evhw->geo.output_r.x;
   output_r.y -= evhw->geo.output_r.y;

   if (output_r.w <= 0 || output_r.h <= 0)
     {
        VER("output area is empty");
        return EINA_FALSE;
     }

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   _e_video_geometry_cal_to_input_rect(evhw, &output_r, &input_r);

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   output_r.x += evhw->geo.output_r.x;
   output_r.y += evhw->geo.output_r.y;

   input_r.x += evhw->geo.input_r.x;
   input_r.y += evhw->geo.input_r.y;

   output_r.x = output_r.x & ~1;
   output_r.w = (output_r.w + 1) & ~1;

   input_r.x = input_r.x & ~1;
   input_r.w = (input_r.w + 1) & ~1;

   evhw->geo.output_r = output_r;
   evhw->geo.input_r = input_r;

   _e_video_geometry_cal_physical(evhw);

   return EINA_TRUE;
}

static void
_e_video_format_info_get(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbm_surf;

   comp_buffer = e_pixmap_resource_get(evhw->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(comp_buffer);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN(tbm_surf);

   evhw->tbmfmt = tbm_surface_get_format(tbm_surf);
}

static Eina_Bool
_e_video_can_commit(E_Video_Hwc_Windows *evhw)
{
   if (e_output_dpms_get(evhw->e_output))
     return EINA_FALSE;

   if (!_e_video_is_visible(evhw))
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_video_commit_handler(tdm_layer *layer, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Windows *evhw;
   Eina_List *l;
   E_Comp_Wl_Video_Buf *vbuf;

   EINA_LIST_FOREACH(video_list, l, evhw)
     {
        if (evhw == user_data) break;
     }

   if (!evhw) return;
   if (!evhw->committed_list) return;

   if (_e_video_can_commit(evhw))
     {
        tbm_surface_h displaying_buffer = evhw->cur_tsurface;

        EINA_LIST_FOREACH(evhw->committed_list, l, vbuf)
          {
             if (vbuf->tbm_surface == displaying_buffer) break;
          }
        if (!vbuf) return;
     }
   else
     vbuf = eina_list_nth(evhw->committed_list, 0);

   evhw->committed_list = eina_list_remove(evhw->committed_list, vbuf);

   /* client can attachs the same wl_buffer twice. */
   if (evhw->current_fb && VBUF_IS_VALID(evhw->current_fb) && vbuf != evhw->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);

        if (evhw->current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&evhw->current_fb->buffer_ref, NULL);
     }

   evhw->current_fb = vbuf;

   VDB("current_fb(%d)", MSTAMP(evhw->current_fb));

   _e_video_vblank_handler(NULL, sequence, tv_sec, tv_usec, evhw);
}

static void
_e_video_commit_buffer(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   evhw->committed_list = eina_list_append(evhw->committed_list, vbuf);

   if (!_e_video_can_commit(evhw))
     goto no_commit;

   if (!_e_video_frame_buffer_show(evhw, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_commit_handler(NULL, 0, 0, 0, evhw);
   _e_video_vblank_handler(NULL, 0, 0, 0, evhw);
}

static void
_e_video_commit_from_waiting_list(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = eina_list_nth(evhw->waiting_list, 0);
   evhw->waiting_list = eina_list_remove(evhw->waiting_list, vbuf);

   _e_video_commit_buffer(evhw, vbuf);
}

static void
_e_video_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Windows *evhw;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, evhw)
     {
        if (evhw == user_data) break;
     }

   if (!evhw) return;

   evhw->waiting_vblank = EINA_FALSE;

   if (evhw->waiting_list)
     _e_video_commit_from_waiting_list(evhw);
}

static Eina_Bool
_e_video_frame_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf)
{
   /* show means that set the information of the buffer and the info of the hwc window */

   if (!vbuf) return EINA_TRUE;

   CLEAR(evhw->info);
   evhw->info.src_config.size.h = vbuf->width_from_pitch;
   evhw->info.src_config.size.v = vbuf->height_from_size;
   evhw->info.src_config.pos.x = vbuf->content_r.x;
   evhw->info.src_config.pos.y = vbuf->content_r.y;
   evhw->info.src_config.pos.w = vbuf->content_r.w;
   evhw->info.src_config.pos.h = vbuf->content_r.h;
   evhw->info.src_config.format = vbuf->tbmfmt;
   evhw->info.dst_pos.x = evhw->geo.tdm_output_r.x;
   evhw->info.dst_pos.y = evhw->geo.tdm_output_r.y;
   evhw->info.dst_pos.w = evhw->geo.tdm_output_r.w;
   evhw->info.dst_pos.h = evhw->geo.tdm_output_r.h;
   evhw->info.transform = vbuf->content_t;

   evhw->cur_tsurface = vbuf->tbm_surface;

   evhw->waiting_vblank = EINA_TRUE;

   // TODO:: this logic move to the hwc windows after hwc commit
#if 1
   E_Client *topmost;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(evhw->ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (evhw->geo.output_r.x == 0 || evhw->geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 evhw->geo.output_r.w < 100 && evhw->geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)",
                      bw, bh, evhw->geo.output_r.w, evhw->geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(evhw->ec->frame, EINA_TRUE);
             VIN("punched");
          }
     }

   if (e_video_debug_punch_value_get())
     {
        e_comp_object_mask_set(evhw->ec->frame, EINA_TRUE);
        VIN("punched");
     }
#endif

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhw->ec) ?: "No Name" , evhw->ec->netwm.pid,
       wl_resource_get_id(evhw->ec->comp_data->surface), vbuf, vbuf->ref_cnt,
       evhw->info.src_config.size.h, evhw->info.src_config.size.v, evhw->info.src_config.pos.x,
       evhw->info.src_config.pos.y, evhw->info.src_config.pos.w, evhw->info.src_config.pos.h,
       evhw->info.dst_pos.x, evhw->info.dst_pos.y, evhw->info.dst_pos.w, evhw->info.dst_pos.h, evhw->info.transform);


   return EINA_TRUE;
}

static void
_e_video_buffer_show(E_Video_Hwc_Windows *evhw, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (evhw->waiting_vblank)
     {
        evhw->waiting_list = eina_list_append(evhw->waiting_list, vbuf);
        VDB("There are waiting fbs more than 1");
        return;
     }

   _e_video_commit_buffer(evhw, vbuf);
}

static void
_e_video_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (_e_video_geometry_cal_map(evhw))
     _e_video_render(evhw, __FUNCTION__);
}

static void
_e_video_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (_e_video_geometry_cal_map(evhw))
     _e_video_render(evhw, __FUNCTION__);
}

static void
_e_video_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Windows *evhw = data;

   if (e_object_is_del(E_OBJECT(evhw->ec))) return;

   if (!evhw->ec->comp_data->video_client)
     return;

   if (evhw->need_force_render)
     {
        VIN("video forcely rendering..");
        _e_video_render(evhw, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if ((evhw->ec->comp_data->sub.data && evhw->ec->comp_data->sub.data->stand_alone) ||
       (evhw->ec->comp_data->sub.data && evhw->follow_topmost_visibility))
     {
        return;
     }

   VIN("evas show (ec:%p)", evhw->ec);
   if (evhw->current_fb)
     _e_video_buffer_show(evhw, evhw->current_fb, evhw->current_fb->content_t);
}

static E_Video_Hwc_Windows *
_e_video_create(E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;
   E_Output *e_output;
   E_Hwc *hwc;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->zone, EINA_FALSE);

   e_output = e_output_find(ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_output, EINA_FALSE);
   hwc = e_output->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   hwc_window = e_hwc_window_new(hwc, ec, E_HWC_WINDOW_STATE_VIDEO);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   evhw = calloc(1, sizeof *evhw);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhw, NULL);

   evhw->ec = ec;
   evhw->pp_align = -1;
   evhw->video_align = -1;
   evhw->e_output = e_output;
   evhw->tdm_mute_id = -1;
   evhw->window = e_client_util_win_get(ec);
   evhw->hwc_window = hwc_window;
   evhw->hwc = hwc;

   /* This ec is a video client now. */
   VIN("video client");
   ec->comp_data->video_client = 1;

   //TODO: shoud this function be called here?
   e_zone_video_available_size_get(ec->zone, NULL, NULL, NULL, NULL, &evhw->video_align);

   VIN("create. ec(%p) wl_surface@%d", ec, wl_resource_get_id(evhw->ec->comp_data->surface));

   video_list = eina_list_append(video_list, evhw);

   return evhw;
}

static void
_e_video_hide(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (evhw->current_fb || evhw->committed_list)
     _e_video_frame_buffer_show(evhw, NULL);

   if (evhw->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhw->current_fb, EINA_FALSE);
        evhw->current_fb = NULL;
     }

   if (evhw->old_comp_buffer)
     evhw->old_comp_buffer = NULL;

   EINA_LIST_FREE(evhw->committed_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   EINA_LIST_FREE(evhw->waiting_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_destroy(E_Video_Hwc_Windows *evhw)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   if (!evhw)
     return;

   VIN("destroy");

   if (evhw->cb_registered)
     {
        evas_object_event_callback_del_full(evhw->ec->frame, EVAS_CALLBACK_RESIZE,
                                            _e_video_cb_evas_resize, evhw);
        evas_object_event_callback_del_full(evhw->ec->frame, EVAS_CALLBACK_MOVE,
                                            _e_video_cb_evas_move, evhw);
     }

   _e_video_hide(evhw);

   /* others */
   EINA_LIST_FOREACH_SAFE(evhw->input_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   EINA_LIST_FOREACH_SAFE(evhw->pp_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   if (evhw->input_buffer_list)
     NEVER_GET_HERE();
   if (evhw->pp_buffer_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   if (evhw->pp)
     tdm_pp_destroy(evhw->pp);

   video_list = eina_list_remove(video_list, evhw);

   e_hwc_window_free(evhw->hwc_window);

   free(evhw);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static Eina_Bool
_e_video_check_if_pp_needed(E_Video_Hwc_Windows *evhw)
{
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   E_Hwc *hwc = evhw->hwc;

   if (hwc->tdm_hwc_video_stream)
     return EINA_FALSE;

   if (!e_comp_screen_available_video_formats_get(&formats, &count))
     return EINA_FALSE;

   for (i = 0; i < count; i++)
     if (formats[i] == evhw->tbmfmt)
       {
          found = EINA_TRUE;
          break;
       }

   if (!found)
     {
        if (formats && count > 0)
          evhw->pp_tbmfmt = formats[0];
        else
          {
             WRN("No layer format information!!!");
             evhw->pp_tbmfmt = TBM_FORMAT_ARGB8888;
          }
        return EINA_TRUE;
     }

   if (hwc->tdm_hwc_video_scanout)
     goto need_pp;

   /* check size */
   if (evhw->geo.input_r.w != evhw->geo.output_r.w || evhw->geo.input_r.h != evhw->geo.output_r.h)
     if (!hwc->tdm_hwc_video_scale)
       goto need_pp;

   /* check rotate */
   if (evhw->geo.transform || e_comp->e_comp_screen->rotation > 0)
     if (!hwc->tdm_hwc_video_transform)
       goto need_pp;

   return EINA_FALSE;

need_pp:
   evhw->pp_tbmfmt = evhw->tbmfmt;

   return EINA_TRUE;
}

static void
_e_video_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video_Hwc_Windows *evhw = (E_Video_Hwc_Windows*)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = _e_video_vbuf_find(evhw->input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = _e_video_vbuf_find(evhw->pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!_e_video_is_visible(evhw)) return;

        _e_video_buffer_show(evhw, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer");
        // there is no way to set in_use flag.
        // This will cause issue when server get available pp_buffer.
     }
}

static void
_e_video_render(E_Video_Hwc_Windows *evhw, const char *func)
{
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Video_Buf *pp_buffer = NULL;
   E_Comp_Wl_Video_Buf *input_buffer = NULL;
   E_Client *topmost;

   EINA_SAFETY_ON_NULL_RETURN(evhw->ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!evhw->ec->pixmap)
     return;

   if (!_e_video_is_visible(evhw))
     {
        _e_video_hide(evhw);
        return;
     }

   comp_buffer = e_pixmap_resource_get(evhw->ec->pixmap);
   if (!comp_buffer) return;

   _e_video_format_info_get(evhw);

   /* not interested with other buffer type */
   if (!wayland_tbm_server_get_surface(NULL, comp_buffer->resource))
     return;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost");
        e_comp_wl_viewport_apply(topmost);
     }

   if (!_e_video_geometry_cal(evhw))
     {
        if(!evhw->need_force_render && !_e_video_parent_is_viewable(evhw))
          {
             VIN("need force render");
             evhw->need_force_render = EINA_TRUE;
          }
        return;
     }

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)", GEO_ARG(&evhw->old_geo), evhw->old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c", GEO_ARG(&evhw->geo), comp_buffer, FOURCC_STR(evhw->tbmfmt));

   if (!memcmp(&evhw->old_geo, &evhw->geo, sizeof evhw->geo) &&
       evhw->old_comp_buffer == comp_buffer)
     return;

   evhw->need_force_render = EINA_FALSE;

   _e_video_input_buffer_valid(evhw, comp_buffer);

   if (!_e_video_check_if_pp_needed(evhw))
     {
        /* 1. non converting case */
        input_buffer = _e_video_input_buffer_get(evhw, comp_buffer, EINA_TRUE);
        EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

        _e_video_buffer_show(evhw, input_buffer, evhw->geo.tdm_transform);

        evhw->old_geo = evhw->geo;
        evhw->old_comp_buffer = comp_buffer;

        goto done;
     }

   /* 2. converting case */
   if (!evhw->pp)
     {
        tdm_pp_capability pp_cap;
        tdm_error error = TDM_ERROR_NONE;

        evhw->pp = tdm_display_create_pp(e_comp->e_comp_screen->tdisplay, NULL);
        EINA_SAFETY_ON_NULL_GOTO(evhw->pp, render_fail);

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &evhw->pp_minw, &evhw->pp_minh,
                                          &evhw->pp_maxw, &evhw->pp_maxh, &evhw->pp_align);

        error = tdm_display_get_pp_capabilities(e_comp->e_comp_screen->tdisplay, &pp_cap);
        if (error == TDM_ERROR_NONE)
          {
             if (pp_cap & TDM_PP_CAPABILITY_SCANOUT)
               evhw->pp_scanout = EINA_TRUE;
          }
     }

   if ((evhw->pp_minw > 0 && (evhw->geo.input_r.w < evhw->pp_minw || evhw->geo.tdm_output_r.w < evhw->pp_minw)) ||
       (evhw->pp_minh > 0 && (evhw->geo.input_r.h < evhw->pp_minh || evhw->geo.tdm_output_r.h < evhw->pp_minh)) ||
       (evhw->pp_maxw > 0 && (evhw->geo.input_r.w > evhw->pp_maxw || evhw->geo.tdm_output_r.w > evhw->pp_maxw)) ||
       (evhw->pp_maxh > 0 && (evhw->geo.input_r.h > evhw->pp_maxh || evhw->geo.tdm_output_r.h > evhw->pp_maxh)))
     {
        INF("size(%dx%d, %dx%d) is out of PP range",
            evhw->geo.input_r.w, evhw->geo.input_r.h, evhw->geo.tdm_output_r.w, evhw->geo.tdm_output_r.h);
        goto done;
     }

   input_buffer = _e_video_input_buffer_get(evhw, comp_buffer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

   pp_buffer = _e_video_pp_buffer_get(evhw, evhw->geo.tdm_output_r.w, evhw->geo.tdm_output_r.h);
   EINA_SAFETY_ON_NULL_GOTO(pp_buffer, render_fail);

   if (memcmp(&evhw->old_geo, &evhw->geo, sizeof evhw->geo))
     {
        tdm_info_pp info;

        CLEAR(info);
        info.src_config.size.h = input_buffer->width_from_pitch;
        info.src_config.size.v = input_buffer->height_from_size;
        info.src_config.pos.x = evhw->geo.input_r.x;
        info.src_config.pos.y = evhw->geo.input_r.y;
        info.src_config.pos.w = evhw->geo.input_r.w;
        info.src_config.pos.h = evhw->geo.input_r.h;
        info.src_config.format = evhw->tbmfmt;
        info.dst_config.size.h = pp_buffer->width_from_pitch;
        info.dst_config.size.v = pp_buffer->height_from_size;
        info.dst_config.pos.w = evhw->geo.tdm_output_r.w;
        info.dst_config.pos.h = evhw->geo.tdm_output_r.h;
        info.dst_config.format = evhw->pp_tbmfmt;
        info.transform = evhw->geo.tdm_transform;

        if (tdm_pp_set_info(evhw->pp, &info))
          {
             VER("tdm_pp_set_info() failed");
             goto render_fail;
          }

        if (tdm_pp_set_done_handler(evhw->pp, _e_video_pp_cb_done, evhw))
          {
             VER("tdm_pp_set_done_handler() failed");
             goto render_fail;
          }

        CLEAR(evhw->pp_r);
        evhw->pp_r.w = info.dst_config.pos.w;
        evhw->pp_r.h = info.dst_config.pos.h;
     }

   pp_buffer->content_r = evhw->pp_r;

   if (tdm_pp_attach(evhw->pp, input_buffer->tbm_surface, pp_buffer->tbm_surface))
     {
        VER("tdm_pp_attach() failed");
        goto render_fail;
     }

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);

   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, comp_buffer);

   if (tdm_pp_commit(evhw->pp))
     {
        VER("tdm_pp_commit() failed");
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        goto render_fail;
     }

   evhw->old_geo = evhw->geo;
   evhw->old_comp_buffer = comp_buffer;

   goto done;

render_fail:
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

done:
   if (!evhw->cb_registered)
     {
        evas_object_event_callback_add(evhw->ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_cb_evas_resize, evhw);
        evas_object_event_callback_add(evhw->ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_cb_evas_move, evhw);
        evhw->cb_registered = EINA_TRUE;
     }
   DBG("======================================.");
}

static Eina_Bool
_e_video_cb_ec_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   E_Video_Hwc_Windows *evhw;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   evhw = data;
   ec = ev->ec;

   if (evhw->ec != ec)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   /* not interested with non video_surface */
   if (!evhw->ec->comp_data->video_client)
     return ECORE_CALLBACK_PASS_ON;

   _e_video_render(evhw, __FUNCTION__);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_client_show(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Client *video_ec = NULL;
   E_Video_Hwc_Windows *evhw = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video_ec = find_video_child_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   evhw = data;
   if (!evhw) return ECORE_CALLBACK_PASS_ON;

   VIN("client(0x%08"PRIxPTR") show: find video child(0x%08"PRIxPTR")", (Ecore_Window)e_client_util_win_get(ec), (Ecore_Window)e_client_util_win_get(video_ec));
   if(evhw->old_comp_buffer)
     {
        VIN("video already rendering..");
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(evhw->ec))
     {
        VIN("video need rendering..");
        e_comp_wl_viewport_apply(ec);
        _e_video_render(evhw, __FUNCTION__);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev = event;
   E_Client *ec = ev->ec;
   E_Video_Hwc_Windows *evhw;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, evhw)
     {
        E_Client *offscreen_parent = find_offscreen_parent_get(evhw->ec);
        if (!offscreen_parent) continue;
        if (offscreen_parent != ec) continue;
        switch (ec->visibility.obscured)
          {
           case E_VISIBILITY_FULLY_OBSCURED:
              evas_object_hide(evhw->ec->frame);
              break;
           case E_VISIBILITY_UNOBSCURED:
              evas_object_show(evhw->ec->frame);
              break;
           default:
              VER("Not implemented");
              return ECORE_CALLBACK_PASS_ON;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_ec_visibility_event_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

static void
_e_video_ec_visibility_event_send(E_Client *ec)
{
   E_Event_Client *ev;
   int obscured;

   obscured = ec->visibility.obscured;
   ELOGF("VIDEO <INF>", "Signal visibility change event of video, type %d",
         ec, obscured);

   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;
   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_VISIBILITY_CHANGE, ev,
                   (Ecore_End_Cb)_e_video_ec_visibility_event_free, NULL);
}

static Eina_Bool
_e_video_cb_topmost_ec_visibility_change(void *data, int type, void *event)
{
   E_Video_Hwc_Windows *evhw;
   E_Event_Client *ev;
   E_Client *topmost;

   ev = event;
   evhw = data;
   if (!evhw->follow_topmost_visibility)
       goto end;

   topmost = e_comp_wl_topmost_parent_get(evhw->ec);
   if (!topmost) goto end;
   if (topmost != ev->ec) goto end;
   if (topmost == evhw->ec) goto end;
   if (evhw->ec->visibility.obscured == topmost->visibility.obscured) goto end;

   /* Update visibility of video client by changing visibility of topmost client */
   evhw->ec->visibility.obscured = topmost->visibility.obscured;
   _e_video_ec_visibility_event_send(evhw->ec);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_video_hwc_windows_ec_event_deinit(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec;

   ec = evhw->ec;

   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_cb_evas_show, evhw);

   E_FREE_LIST(evhw->ec_event_handler, ecore_event_handler_del);
}

const char *
_e_video_hwc_windows_prop_name_get_by_id(E_Video_Hwc_Windows *evhw, unsigned int id)
{
   const tdm_prop *props;
   int i, count = 0;

   e_hwc_windows_get_video_available_properties(evhw->hwc, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          {
             VDB("check property(%s)", props[i].name);
             return props[i].name;
          }
     }

   VER("No available property: id %d", id);

   return NULL;
}

static void
_e_video_hwc_windows_ec_event_init(E_Video_Hwc_Windows *evhw)
{
   E_Client *ec;

   ec = evhw->ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_cb_evas_show, evhw);

   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_cb_ec_buffer_change, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_SHOW,
                         _e_video_cb_ec_client_show, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, evhw);
   E_LIST_HANDLER_APPEND(evhw->ec_event_handler, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_video_cb_topmost_ec_visibility_change, evhw);
}

static void
_e_video_hwc_windows_iface_destroy(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   _e_video_hwc_windows_ec_event_deinit(evhw);
   _e_video_destroy(evhw);
}

static Eina_Bool
_e_video_hwc_windows_iface_follow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->follow_topmost_visibility = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_unfollow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->follow_topmost_visibility = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_allowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->allowed_attribute = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_disallowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhw->allowed_attribute = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = tdm_hwc_window_get_property(evhw->hwc_window->thwc_window, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   const char *name;

   IFACE_ENTRY;

   VIN("set_attribute");

   name = _e_video_hwc_windows_prop_name_get_by_id(evhw, id);
   if (!name)
   {
      VER("_e_video_hwc_windows_prop_name_get_by_id failed");
      return EINA_FALSE;
   }

   if (evhw->allowed_attribute)
     {
        VIN("set_attribute now : property(%s), value(%d)", name, value.u32);

        /* set the property on the fly */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_TRUE))
          {
             VER("set property failed");
             return EINA_FALSE;
          }
     }
   else
     {
        VIN("set_attribute at commit : property(%s), value(%d)", name, value.u32);

        /* set the property before hwc commit */
        if (!e_hwc_window_set_property(evhw->hwc_window, id, name, value, EINA_FALSE))
          {
             VER("set property failed");
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   IFACE_ENTRY;

   if (!e_hwc_windows_get_video_available_properties(evhw->hwc, props, count))
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   IFACE_ENTRY;

   memcpy(&info->src_config, &evhw->info.src_config, sizeof(tdm_info_config));
   memcpy(&info->dst_pos, &evhw->info.dst_pos, sizeof(tdm_pos));
   info->transform = evhw->info.transform;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_windows_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   _e_video_commit_handler(NULL, sequence, tv_sec, tv_usec, evhw);

   return EINA_TRUE;
}

static tbm_surface_h
_e_video_hwc_windows_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   return evhw->cur_tsurface;
}

EINTERN E_Video_Comp_Iface *
e_video_hwc_windows_iface_create(E_Client *ec)
{
   E_Video_Hwc_Windows *evhw;

   INF("Initializing HWC Windows mode");

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   evhw = _e_video_create(ec);
   if (!evhw)
     {
        ERR("Failed to create 'E_Video_Hwc_Windows'");
        return NULL;
     }

   _e_video_hwc_windows_ec_event_init(evhw);

   evhw->base.destroy = _e_video_hwc_windows_iface_destroy;
   evhw->base.follow_topmost_visibility = _e_video_hwc_windows_iface_follow_topmost_visibility;
   evhw->base.unfollow_topmost_visibility = _e_video_hwc_windows_iface_unfollow_topmost_visibility;
   evhw->base.allowed_property = _e_video_hwc_windows_iface_allowed_property;
   evhw->base.disallowed_property = _e_video_hwc_windows_iface_disallowed_property;
   evhw->base.property_get = _e_video_hwc_windows_iface_property_get;
   evhw->base.property_set = _e_video_hwc_windows_iface_property_set;
   evhw->base.property_delay_set = NULL;
   evhw->base.available_properties_get = _e_video_hwc_windows_iface_available_properties_get;
   evhw->base.info_get = _e_video_hwc_windows_iface_info_get;
   evhw->base.commit_data_release = _e_video_hwc_windows_iface_commit_data_release;
   evhw->base.tbm_surface_get = _e_video_hwc_windows_iface_tbm_surface_get;

   return &evhw->base;
}
