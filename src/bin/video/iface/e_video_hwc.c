#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

#define IFACE_ENTRY                                      \
   E_Video_Hwc *evh;                                    \
   evh = container_of(iface, E_Video_Hwc, base)

#define IS_RGB(f) ((f) == TBM_FORMAT_XRGB8888 || (f) == TBM_FORMAT_ARGB8888)

typedef struct _E_Video_Hwc E_Video_Hwc;

struct _E_Video_Hwc
{
   E_Video_Comp_Iface base;
   E_Video_Comp_Iface *backend;
};

EINTERN tbm_format
e_video_hwc_comp_buffer_tbm_format_get(E_Comp_Wl_Buffer *comp_buffer)
{
   tbm_surface_h tbm_surf;

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surf, 0);

   return tbm_surface_get_format(tbm_surf);
}

EINTERN Eina_Bool
e_video_hwc_client_parent_viewable_get(E_Client *ec)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == ec)
     {
        VDB("There is no video parent surface", ec);
        return EINA_FALSE;
     }

   if (!topmost_parent->visible)
     {
        VDB("parent(0x%08"PRIxPTR") not viewable", ec,
            (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   if (!e_pixmap_resource_get(topmost_parent->pixmap))
     {
        VDB("parent(0x%08"PRIxPTR") no comp buffer", ec,
            (Ecore_Window)e_client_util_win_get(topmost_parent));
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN E_Comp_Wl_Video_Buf *
e_video_hwc_vbuf_find(Eina_List *list, tbm_surface_h buffer)
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

EINTERN E_Comp_Wl_Video_Buf *
e_video_hwc_vbuf_find_with_comp_buffer(Eina_List *list, E_Comp_Wl_Buffer *comp_buffer)
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

EINTERN E_Client *
e_video_hwc_client_offscreen_parent_get(E_Client *ec)
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

EINTERN Eina_Bool
e_video_hwc_client_visible_get(E_Client *ec)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(ec->pixmap))
     {
        VDB("no comp buffer", ec);
        return EINA_FALSE;
     }

   if (ec->comp_data->sub.data && ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = e_video_hwc_client_offscreen_parent_get(ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured", ec);
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(ec->frame))
     {
        VDB("evas obj invisible", ec);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

EINTERN E_Client *
e_video_hwc_child_client_get(E_Client *ec)
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
        temp = e_video_hwc_child_client_get(subc);
        if(temp) return temp;
     }

   return NULL;
}

static tbm_surface_h
_e_video_hwc_client_tbm_surface_get(E_Client *ec)
{
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbmsurf;

   comp_buffer = e_pixmap_resource_get(ec->pixmap);
   if (!comp_buffer)
     {
        /* No comp buffer */
        return NULL;
     }

   tbmsurf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server,
                                            comp_buffer->resource);

   return tbmsurf;
}

static E_Comp_Wl_Subsurf_Data *
_e_video_hwc_client_subsurface_data_get(E_Client *ec)
{
   if (ec->comp_data && ec->comp_data->sub.data)
     return ec->comp_data->sub.data;

   return NULL;
}

static void
_e_video_hwc_buffer_size_get(tbm_surface_h tbm_surf, int *w, int *h)
{
   tbm_format tbmfmt;
   uint32_t size = 0, offset = 0, pitch = 0;

   tbmfmt = tbm_surface_get_format(tbm_surf);
   tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

   if (IS_RGB(tbmfmt))
     *w = pitch / 4;
   else
     *w = pitch;

   *h = tbm_surface_get_height(tbm_surf);
}

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

static void
_e_video_hwc_geometry_input_rect_get_with_viewport(tbm_surface_h tbm_surf, E_Comp_Wl_Buffer_Viewport *vp, Eina_Rectangle *out)
{
   int bw, bh;
   int x1, y1, x2, y2;
   int tx1, ty1, tx2, ty2;
   int width_from_buffer, height_from_buffer;

   bw = tbm_surface_get_width(tbm_surf);
   bh = tbm_surface_get_height(tbm_surf);
   VDB("TBM buffer size %d %d", NULL, bw, bh);

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

   VDB("transform(%d) scale(%d) buffer(%dx%d) src(%d,%d %d,%d)",
       NULL, vp->buffer.transform, vp->buffer.scale,
       width_from_buffer, height_from_buffer,
       x1, y1, x2 - x1, y2 - y1);

   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x1, y1, &tx1, &ty1);
   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x2, y2, &tx2, &ty2);

   out->x = (tx1 <= tx2) ? tx1 : tx2;
   out->y = (ty1 <= ty2) ? ty1 : ty2;
   out->w = (tx1 <= tx2) ? tx2 - tx1 : tx1 - tx2;
   out->h = (ty1 <= ty2) ? ty2 - ty1 : ty1 - ty2;
}

static void
_e_video_hwc_geometry_output_rect_get(E_Client *ec, Eina_Rectangle *out)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   sdata = _e_video_hwc_client_subsurface_data_get(ec);
   if (sdata)
     {
        if (sdata->parent)
          {
             out->x = sdata->parent->x + sdata->position.x;
             out->y = sdata->parent->y + sdata->position.y;
          }
        else
          {
             out->x = sdata->position.x;
             out->y = sdata->position.y;
          }
     }
   else
     {
        out->x = ec->x;
        out->y = ec->y;
     }

   out->w = ec->comp_data->width_from_viewport;
   out->w = (out->w + 1) & ~1;
   out->h = ec->comp_data->height_from_viewport;

   e_comp_object_frame_xy_unadjust(ec->frame, out->x, out->y, &out->x, &out->y);
   e_comp_object_frame_wh_unadjust(ec->frame, out->w, out->h, &out->w, &out->h);
}

/* convert from logical screen to physical output */
static void
_e_video_hwc_geometry_tdm_config_update(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Zone *zone;
   E_Comp_Wl_Output *output;
   E_Client *topmost;
   int tran, flip;
   int transform;

   topmost = e_comp_wl_topmost_parent_get(ec);
   EINA_SAFETY_ON_NULL_GOTO(topmost, normal);

   output = e_comp_wl_output_find(topmost);
   EINA_SAFETY_ON_NULL_GOTO(output, normal);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_GOTO(zone, normal);

   tran = out->transform & 0x3;
   flip = out->transform & 0x4;
   transform = flip + (tran + output->transform) % 4;
   switch(transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
         out->tdm.transform = TDM_TRANSFORM_270;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         out->tdm.transform = TDM_TRANSFORM_180;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         out->tdm.transform = TDM_TRANSFORM_90;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_270;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_180;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         out->tdm.transform = TDM_TRANSFORM_FLIPPED_90;
         break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         out->tdm.transform = TDM_TRANSFORM_NORMAL;
         break;
     }

   if (output->transform % 2)
     {
        if (out->tdm.transform == TDM_TRANSFORM_FLIPPED)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_180;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_90)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_270;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_180)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED;
        else if (out->tdm.transform == TDM_TRANSFORM_FLIPPED_270)
          out->tdm.transform = TDM_TRANSFORM_FLIPPED_90;
     }

   if (output->transform == 0)
     out->tdm.output_r = out->output_r;
   else
     e_comp_wl_rect_convert(zone->w, zone->h, output->transform, 1,
                            out->output_r.x, out->output_r.y,
                            out->output_r.w, out->output_r.h,
                            &out->tdm.output_r.x, &out->tdm.output_r.y,
                            &out->tdm.output_r.w, &out->tdm.output_r.h);

   VDB("geomtry: screen(%d,%d %dx%d | %d) => %d => physical(%d,%d %dx%d | %d)",
       ec, EINA_RECTANGLE_ARGS(&out->output_r), out->transform, transform,
       EINA_RECTANGLE_ARGS(&out->tdm.output_r), out->tdm.transform);

   return;
normal:
   out->tdm.output_r = out->output_r;
   out->tdm.transform = out->transform;
}

static Eina_Bool
_e_video_hwc_geometry_viewport_apply(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Comp_Wl_Buffer_Viewport *vp;
   tbm_surface_h tbm_surf;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(out, EINA_FALSE);

   tbm_surf = _e_video_hwc_client_tbm_surface_get(ec);
   if (!tbm_surf)
     {
        /* No tbm_surface */
        return EINA_FALSE;
     }

   _e_video_hwc_buffer_size_get(tbm_surf, &out->input_w, &out->input_h);

   vp = &ec->comp_data->scaler.buffer_viewport;
   _e_video_hwc_geometry_input_rect_get_with_viewport(tbm_surf, vp, &out->input_r);

   _e_video_hwc_geometry_output_rect_get(ec, &out->output_r);
   out->transform = vp->buffer.transform;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   VDB("geometry(%dx%d  %d,%d %dx%d  %d,%d %dx%d  %d)",
       ec, out->input_w, out->input_h,
       EINA_RECTANGLE_ARGS(&out->input_r),
       EINA_RECTANGLE_ARGS(&out->output_r),
       out->transform);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_video_hwc_geometry_map_apply(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   const Evas_Map *m;
   Evas_Coord x1, x2, y1, y2;
   Eina_Rectangle output_r;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->frame, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(out, EINA_FALSE);

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

   if (!memcmp(&out->output_r, &output_r, sizeof(Eina_Rectangle)))
     return EINA_FALSE;

   VDB("frame(%p) m(%p) output(%d,%d %dx%d) => (%d,%d %dx%d)", ec, ec->frame, m,
       EINA_RECTANGLE_ARGS(&out->output_r), EINA_RECTANGLE_ARGS(&output_r));

   out->output_r = output_r;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   return EINA_TRUE;
}

static void
_e_video_hwc_geometry_cal_to_input(int output_w, int output_h, int input_w, int input_h,
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
_e_video_hwc_geometry_cal_to_input_rect(E_Video_Hwc_Geometry *geo, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int xf1, yf1, xf2, yf2;

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_video_hwc_geometry_cal_to_input(geo->output_r.w, geo->output_r.h,
                                      geo->input_r.w, geo->input_r.h,
                                      geo->transform, xf1, yf1, &xf1, &yf1);
   _e_video_hwc_geometry_cal_to_input(geo->output_r.w, geo->output_r.h,
                                      geo->input_r.w, geo->input_r.h,
                                      geo->transform, xf2, yf2, &xf2, &yf2);

   drect->x = MIN(xf1, xf2);
   drect->y = MIN(yf1, yf2);
   drect->w = MAX(xf1, xf2) - drect->x;
   drect->h = MAX(yf1, yf2) - drect->y;
}

EINTERN Eina_Bool
e_video_hwc_geometry_get(E_Client *ec, E_Video_Hwc_Geometry *out)
{
   E_Zone *zone;
   E_Client *topmost;
   Eina_Rectangle screen = {0,};
   Eina_Rectangle output_r = {0,}, input_r = {0,};

   /* get geometry information with buffer scale, transform and viewport. */
   if (!_e_video_hwc_geometry_viewport_apply(ec, out))
     return EINA_FALSE;

   e_video_hwc_geometry_map_apply(ec, out);

   topmost = e_comp_wl_topmost_parent_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(topmost, EINA_FALSE);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

   screen.w = zone->w;
   screen.h = zone->h;

   e_comp_wl_video_buffer_size_get(ec, &input_r.w, &input_r.h);
   // when topmost is not mapped, input size can be abnormal.
   // in this case, it will be render by topmost showing.
   if (!eina_rectangle_intersection(&out->input_r, &input_r) || (out->input_r.w <= 10 || out->input_r.h <= 10))
     {
        VER("input area is empty", ec);
        return EINA_FALSE;
     }

   if (out->output_r.x >= 0 && out->output_r.y >= 0 &&
       (out->output_r.x + out->output_r.w) <= screen.w &&
       (out->output_r.y + out->output_r.h) <= screen.h)
     return EINA_TRUE;

   /* TODO: need to improve */

   output_r = out->output_r;
   if (!eina_rectangle_intersection(&output_r, &screen))
     {
        VER("output_r(%d,%d %dx%d) screen(%d,%d %dx%d) => intersect(%d,%d %dx%d)",
            ec, EINA_RECTANGLE_ARGS(&out->output_r),
            EINA_RECTANGLE_ARGS(&screen), EINA_RECTANGLE_ARGS(&output_r));
        return EINA_TRUE;
     }

   output_r.x -= out->output_r.x;
   output_r.y -= out->output_r.y;

   if (output_r.w <= 0 || output_r.h <= 0)
     {
        VER("output area is empty", ec);
        return EINA_FALSE;
     }

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       ec, EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   _e_video_hwc_geometry_cal_to_input_rect(out, &output_r, &input_r);

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       ec, EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   output_r.x += out->output_r.x;
   output_r.y += out->output_r.y;

   input_r.x += out->input_r.x;
   input_r.y += out->input_r.y;

   output_r.x = output_r.x & ~1;
   output_r.w = (output_r.w + 1) & ~1;

   input_r.x = input_r.x & ~1;
   input_r.w = (input_r.w + 1) & ~1;

   out->output_r = output_r;
   out->input_r = input_r;

   _e_video_hwc_geometry_tdm_config_update(ec, out);

   return EINA_TRUE;
}

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
