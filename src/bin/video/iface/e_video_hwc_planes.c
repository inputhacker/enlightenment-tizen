#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "../e_video_internal.h"

#define VER(fmt, arg...)   ELOGF("VIDEO", "<ERR> window(0x%08"PRIxPTR"): "fmt, \
                                 evhp->ec, evhp->window, ##arg)
#define VWR(fmt, arg...)   ELOGF("VIDEO", "<WRN> window(0x%08"PRIxPTR"): "fmt, \
                                 evhp->ec, evhp->window, ##arg)
#define VIN(fmt, arg...)   ELOGF("VIDEO", "<INF> window(0x%08"PRIxPTR"): "fmt, \
                                 evhp->ec, evhp->window, ##arg)
#define VDB(fmt, arg...)   DBG("window(0x%08"PRIxPTR") ec(%p): "fmt, evhp->window, evhp->ec, ##arg)

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
   E_Video_Hwc_Planes *evhp;                              \
   evhp = container_of(iface, E_Video_Hwc_Planes, base)

typedef struct _E_Video_Hwc_Planes E_Video_Hwc_Planes;
typedef struct _E_Video_Layer E_Video_Layer;
typedef struct _E_Video_Info_Layer E_Video_Info_Layer;

/* the new TDM API doesn't have layers, so we have to invent layer here*/
struct _E_Video_Layer
{
   E_Video_Hwc_Planes *evhp;

   tdm_layer *tdm_layer;

   /* for hwc_window */
   E_Client_Video_Info info;
   tbm_surface_h cur_tsurface; // tsurface to be set this layer.
   E_Client *e_client;
};

struct _E_Video_Hwc_Planes
{
   E_Video_Comp_Iface base;

   E_Client *ec;
   Ecore_Window window;
   tdm_output *output;
   E_Output *e_output;
   E_Video_Layer *layer;
   E_Plane *e_plane;
   Eina_Bool external_video;

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
   Eina_List *tdm_prop_list;
   Eina_List *late_tdm_prop_list;
   int tdm_mute_id;

   Eina_Bool  cb_registered;
   Eina_Bool  need_force_render;
   Eina_Bool  follow_topmost_visibility;
   Eina_Bool  allowed_attribute;

   E_Plane_Hook *video_plane_ready_handler;
};

typedef struct _Tdm_Prop_Value
{
   unsigned int id;
   char name[TDM_NAME_LEN];
   tdm_value value;
} Tdm_Prop_Value;

static Eina_List *video_list = NULL;
static Eina_List *video_layers = NULL;

static Eina_Bool _e_video_set(E_Video_Hwc_Planes *evhp, E_Client *ec);
static void _e_video_destroy(E_Video_Hwc_Planes *evhp);
static void _e_video_render(E_Video_Hwc_Planes *evhp, const char *func);
static Eina_Bool _e_video_frame_buffer_show(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf);
static void _e_video_video_set_hook(void *data, E_Plane *plane);

static tdm_layer* _e_video_tdm_video_layer_get(tdm_output *output);
static tdm_layer* _e_video_tdm_available_video_layer_get(tdm_output *output);
static void _e_video_tdm_set_layer_usable(tdm_layer *layer, Eina_Bool usable);
static Eina_Bool _e_video_tdm_get_layer_usable(tdm_layer *layer);

static void _e_video_vblank_handler(tdm_output *output, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);

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

static E_Output *
_get_e_output(tdm_output *output)
{
   Eina_List *l;
   E_Output *eo;

   EINA_LIST_FOREACH(e_comp->e_comp_screen->outputs, l, eo)
      if (eo->toutput == output)
         return eo;

   return NULL;
}

static Eina_Bool
_e_video_tdm_output_has_video_layer(tdm_output *toutput)
{
   E_Output *output = NULL;
   tdm_layer *layer;
   tdm_layer_capability lyr_capabilities = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, EINA_FALSE);

   output = _get_e_output(toutput);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   /* get the first suitable layer */
   layer = _e_video_tdm_video_layer_get(toutput);
   if (!layer)
     return EINA_FALSE;

   tdm_layer_get_capabilities(layer, &lyr_capabilities);
   if (lyr_capabilities & TDM_LAYER_CAPABILITY_VIDEO)
     return EINA_TRUE;

   return EINA_FALSE;
}

static E_Video_Layer *
_e_video_available_video_layer_get(E_Video_Hwc_Planes *evhp)
{
   E_Video_Layer *layer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, NULL);

   layer = calloc(1, sizeof(E_Video_Layer));
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

   layer->evhp = evhp;
   layer->e_client = evhp->ec;


   /* layer->tdm_layer = e_output_video_available_tdm_layer_get(evhp->e_output); */
   layer->tdm_layer = _e_video_tdm_available_video_layer_get(evhp->output);
   if (!layer->tdm_layer)
     {
        free(layer);
        return NULL;
     }
   _e_video_tdm_set_layer_usable(layer->tdm_layer, EINA_FALSE);

   return layer;
}

static tdm_error
_e_video_layer_get_info(E_Video_Layer *layer, E_Client_Video_Info *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer tinfo = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   ret = tdm_layer_get_info(layer->tdm_layer, &tinfo);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

   memcpy(&vinfo->src_config, &tinfo.src_config, sizeof(tdm_info_config));
   memcpy(&vinfo->dst_pos, &tinfo.dst_pos, sizeof(tdm_pos));
   vinfo->transform = tinfo.transform;

   return ret;
}

static tdm_error
_e_video_layer_set_info(E_Video_Layer *layer, E_Client_Video_Info *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer info_layer = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   memcpy(&info_layer.src_config, &vinfo->src_config, sizeof(tdm_info_config));
   memcpy(&info_layer.dst_pos, &vinfo->dst_pos, sizeof(tdm_pos));
   info_layer.transform = vinfo->transform;

   ret = tdm_layer_set_info(layer->tdm_layer, &info_layer);

   return ret;
}

static tdm_error
_e_video_layer_set_buffer(E_Video_Layer * layer, tbm_surface_h buff)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buff, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_set_buffer(layer->tdm_layer, buff);

   return ret;
}

static tdm_error
_e_video_layer_unset_buffer(E_Video_Layer *layer)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_unset_buffer(layer->tdm_layer);

   return ret;
}

/*
 * This function checks if this layer was set
 */
static tdm_error
_e_video_layer_is_usable(E_Video_Layer * layer, unsigned int *usable)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(usable, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_is_usable(layer->tdm_layer, usable);
   return ret;
}

static tdm_error
_e_video_layer_commit(E_Video_Layer *layer, tdm_layer_commit_handler func, void *user_data)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_commit(layer->tdm_layer, func, user_data);

   return ret;
}

static tbm_surface_h
_e_video_layer_get_displaying_buffer(E_Video_Layer *layer, int *tdm_error)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

   return tdm_layer_get_displaying_buffer(layer->tdm_layer, tdm_error);
}

static tdm_error
_e_video_layer_set_property(E_Video_Layer * layer, Tdm_Prop_Value *prop)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_set_property(layer->tdm_layer, prop->id, prop->value);
   return ret;
}

static void
_e_video_layer_destroy(E_Video_Layer *layer)
{
   EINA_SAFETY_ON_NULL_RETURN(layer);

   if (layer->tdm_layer)
     _e_video_tdm_set_layer_usable(layer->tdm_layer, EINA_TRUE);

   free(layer);
}

static Eina_Bool
_e_video_set_layer(E_Video_Hwc_Planes *evhp, Eina_Bool set)
{
   Eina_Bool need_wait;

   if (!set)
     {
        unsigned int usable = 1;
        if (!evhp->layer) return EINA_TRUE;

        _e_video_layer_is_usable(evhp->layer, &usable);
        if (!usable && !evhp->video_plane_ready_handler)
          {
             VIN("stop video");
             _e_video_layer_unset_buffer(evhp->layer);
             _e_video_layer_commit(evhp->layer, NULL, NULL);
          }

        VIN("release layer: %p", evhp->layer);
        _e_video_layer_destroy(evhp->layer);
        evhp->layer = NULL;
        evhp->old_comp_buffer = NULL;

        e_plane_video_set(evhp->e_plane, EINA_FALSE, NULL);
        evhp->e_plane = NULL;

        E_FREE_FUNC(evhp->video_plane_ready_handler, e_plane_hook_del);
     }
   else
     {
        int zpos;
        tdm_error ret;

        if (evhp->layer) return EINA_TRUE;

        evhp->layer = _e_video_available_video_layer_get(evhp);
        if (!evhp->layer)
          {
             VWR("no available layer for evhp");
             return EINA_FALSE;
          }


        ret = tdm_layer_get_zpos(evhp->layer->tdm_layer, &zpos);
        if (ret == TDM_ERROR_NONE)
          evhp->e_plane = e_output_plane_get_by_zpos(evhp->e_output, zpos);

        if (!evhp->e_plane)
          {
             VWR("fail get e_plane");
             _e_video_layer_destroy(evhp->layer);
             evhp->layer = NULL;
             return EINA_FALSE;
          }

        if (!e_plane_video_set(evhp->e_plane, EINA_TRUE, &need_wait))
          {
             VWR("fail set video to e_plane");
             _e_video_layer_destroy(evhp->layer);
             evhp->layer = NULL;
             evhp->e_plane = NULL;
             return EINA_FALSE;
          }
        if (need_wait)
          {
             evhp->video_plane_ready_handler =
                e_plane_hook_add(E_PLANE_HOOK_VIDEO_SET,
                                 _e_video_video_set_hook, evhp);
          }

        VIN("assign layer: %p", evhp->layer);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_is_visible(E_Video_Hwc_Planes *evhp)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(evhp->ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(evhp->ec->pixmap))
     {
        VDB("no comp buffer");
        return EINA_FALSE;
     }

   if (evhp->ec->comp_data->sub.data && evhp->ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = find_offscreen_parent_get(evhp->ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured");
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(evhp->ec->frame))
     {
        VDB("evas obj invisible");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_parent_is_viewable(E_Video_Hwc_Planes *evhp)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(evhp->ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(evhp->ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == evhp->ec)
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
   E_Video_Hwc_Planes *evhp = data;
   Eina_Bool need_hide = EINA_FALSE;

   DBG("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   evhp->input_buffer_list = eina_list_remove(evhp->input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (evhp->current_fb == vbuf)
     {
        VIN("current fb destroyed");
        e_comp_wl_video_buffer_set_use(evhp->current_fb, EINA_FALSE);
        evhp->current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhp->committed_list, vbuf))
     {
        VIN("committed fb destroyed");
        evhp->committed_list = eina_list_remove(evhp->committed_list, vbuf);
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhp->waiting_list, vbuf))
     {
        VIN("waiting fb destroyed");
        evhp->waiting_list = eina_list_remove(evhp->waiting_list, vbuf);
     }

   if (need_hide && evhp->layer)
     _e_video_frame_buffer_show(evhp, NULL);
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
_e_video_input_buffer_copy(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Buffer *comp_buf, E_Comp_Wl_Video_Buf *vbuf, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *temp = NULL;
   int aligned_width = ROUNDUP(vbuf->width_from_pitch, evhp->pp_align);

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = comp_buf;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)",
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);
   e_comp_wl_video_buffer_unref(vbuf);

   evhp->geo.input_w = vbuf->width_from_pitch;
#ifdef DUMP_BUFFER
   char file[256];
   static int i;
   snprintf(file, sizeof file, "/tmp/dump/%s_%d.png", "cpy", i++);
   tdm_helper_dump_buffer(temp->tbm_surface, file);
#endif

   return temp;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_get(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Buffer *comp_buffer, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_Bool need_pp_scanout = EINA_FALSE;

   vbuf = _e_video_vbuf_find_with_comp_buffer(evhp->input_buffer_list, comp_buffer);
   if (vbuf)
     {
        vbuf->content_r = evhp->geo.input_r;
        return vbuf;
     }

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   if (evhp->pp_scanout)
     {
        Eina_Bool input_buffer_scanout = EINA_FALSE;
        input_buffer_scanout = _e_video_input_buffer_scanout_check(vbuf);
        if (!input_buffer_scanout) need_pp_scanout = EINA_TRUE;
     }

   if (evhp->pp)
     {
        if ((evhp->pp_align != -1 && (vbuf->width_from_pitch % evhp->pp_align)) ||
            need_pp_scanout)
          {
             E_Comp_Wl_Video_Buf *temp;

             if (need_pp_scanout)
               temp = _e_video_input_buffer_copy(evhp, comp_buffer, vbuf, EINA_TRUE);
             else
               temp = _e_video_input_buffer_copy(evhp, comp_buffer, vbuf, scanout);
             if (!temp)
               {
                  e_comp_wl_video_buffer_unref(vbuf);
                  return NULL;
               }
             vbuf = temp;
          }
     }

   vbuf->content_r = evhp->geo.input_r;

   evhp->input_buffer_list = eina_list_append(evhp->input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_input_buffer_cb_free, evhp);

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p) created, refcnt:%d"
       " scanout=%d", e_client_util_name_get(evhp->ec) ?: "No Name" ,
       evhp->ec->netwm.pid, wl_resource_get_id(evhp->ec->comp_data->surface), vbuf,
       vbuf->ref_cnt, scanout);

   return vbuf;
}

static void
_e_video_input_buffer_valid(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(evhp->input_buffer_list, l, vbuf)
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
   E_Video_Hwc_Planes *evhp = data;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   if (evhp->current_fb == vbuf)
     evhp->current_fb = NULL;

   evhp->committed_list = eina_list_remove(evhp->committed_list, vbuf);

   evhp->waiting_list = eina_list_remove(evhp->waiting_list, vbuf);

   evhp->pp_buffer_list = eina_list_remove(evhp->pp_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf *
_e_video_pp_buffer_get(E_Video_Hwc_Planes *evhp, int width, int height)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   int i = 0;
   int aligned_width;

   if (evhp->video_align != -1)
     aligned_width = ROUNDUP(width, evhp->video_align);
   else
     aligned_width = width;

   if (evhp->pp_buffer_list)
     {
        vbuf = eina_list_data_get(evhp->pp_buffer_list);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

        /* if we need bigger pp_buffers, destroy all pp_buffers and create */
        if (aligned_width > vbuf->width_from_pitch || height != vbuf->height)
          {
             Eina_List *ll;

             VIN("pp buffer changed: %dx%d => %dx%d",
                 vbuf->width_from_pitch, vbuf->height,
                 aligned_width, height);

             EINA_LIST_FOREACH_SAFE(evhp->pp_buffer_list, l, ll, vbuf)
               {
                  /* free forcely */
                  e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
                  e_comp_wl_video_buffer_unref(vbuf);
               }
             if (evhp->pp_buffer_list)
               NEVER_GET_HERE();

             if (evhp->waiting_list)
               NEVER_GET_HERE();
          }
     }

   if (!evhp->pp_buffer_list)
     {
        for (i = 0; i < BUFFER_MAX_COUNT; i++)
          {
             vbuf = e_comp_wl_video_buffer_alloc(aligned_width, height, evhp->pp_tbmfmt, EINA_TRUE);
             EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

             e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_pp_buffer_cb_free, evhp);
             evhp->pp_buffer_list = eina_list_append(evhp->pp_buffer_list, vbuf);

          }

        VIN("pp buffer created: %dx%d, %c%c%c%c",
            vbuf->width_from_pitch, height, FOURCC_STR(evhp->pp_tbmfmt));

        evhp->next_buffer = evhp->pp_buffer_list;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->pp_buffer_list, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->next_buffer, NULL);

   l = evhp->next_buffer;
   while ((vbuf = evhp->next_buffer->data))
     {
        evhp->next_buffer = (evhp->next_buffer->next) ? evhp->next_buffer->next : evhp->pp_buffer_list;

        if (!vbuf->in_use)
          return vbuf;

        if (l == evhp->next_buffer)
          {
             VWR("all video framebuffers in use (max:%d)", BUFFER_MAX_COUNT);
             return NULL;
          }
     }

   return NULL;
}

/* convert from logical screen to physical output */
static void
_e_video_geometry_cal_physical(E_Video_Hwc_Planes *evhp)
{
   E_Zone *zone;
   E_Comp_Wl_Output *output;
   E_Client *topmost;
   int tran, flip;
   int transform;

   topmost = e_comp_wl_topmost_parent_get(evhp->ec);
   EINA_SAFETY_ON_NULL_GOTO(topmost, normal);

   output = e_comp_wl_output_find(topmost);
   EINA_SAFETY_ON_NULL_GOTO(output, normal);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_GOTO(zone, normal);

   tran = evhp->geo.transform & 0x3;
   flip = evhp->geo.transform & 0x4;
   transform = flip + (tran + output->transform) % 4;
   switch(transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
         evhp->geo.tdm_transform = TDM_TRANSFORM_270;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         evhp->geo.tdm_transform = TDM_TRANSFORM_180;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         evhp->geo.tdm_transform = TDM_TRANSFORM_90;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
         break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         evhp->geo.tdm_transform = TDM_TRANSFORM_NORMAL;
         break;
     }

   if (output->transform % 2)
     {
        if (evhp->geo.tdm_transform == TDM_TRANSFORM_FLIPPED)
          evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
        else if (evhp->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_90)
          evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
        else if (evhp->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_180)
          evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
        else if (evhp->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_270)
          evhp->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
     }

   if (output->transform == 0)
     evhp->geo.tdm_output_r = evhp->geo.output_r;
   else
     e_comp_wl_rect_convert(zone->w, zone->h, output->transform, 1,
                            evhp->geo.output_r.x, evhp->geo.output_r.y,
                            evhp->geo.output_r.w, evhp->geo.output_r.h,
                            &evhp->geo.tdm_output_r.x, &evhp->geo.tdm_output_r.y,
                            &evhp->geo.tdm_output_r.w, &evhp->geo.tdm_output_r.h);

   VDB("geomtry: screen(%d,%d %dx%d | %d) => %d => physical(%d,%d %dx%d | %d)",
       EINA_RECTANGLE_ARGS(&evhp->geo.output_r), evhp->geo.transform, transform,
       EINA_RECTANGLE_ARGS(&evhp->geo.tdm_output_r), evhp->geo.tdm_transform);

   return;
normal:
   evhp->geo.tdm_output_r = evhp->geo.output_r;
   evhp->geo.tdm_transform = evhp->geo.transform;
}

static Eina_Bool
_e_video_geometry_cal_viewport(E_Video_Hwc_Planes *evhp)
{
   E_Client *ec = evhp->ec;
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

   comp_buffer = e_pixmap_resource_get(evhp->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_buffer, EINA_FALSE);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surf, EINA_FALSE);

   tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

   /* input geometry */
   if (IS_RGB(evhp->tbmfmt))
     evhp->geo.input_w = pitch / 4;
   else
     evhp->geo.input_w = pitch;

   evhp->geo.input_h = tbm_surface_get_height(tbm_surf);

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

   evhp->geo.input_r.x = (tx1 <= tx2) ? tx1 : tx2;
   evhp->geo.input_r.y = (ty1 <= ty2) ? ty1 : ty2;
   evhp->geo.input_r.w = (tx1 <= tx2) ? tx2 - tx1 : tx1 - tx2;
   evhp->geo.input_r.h = (ty1 <= ty2) ? ty2 - ty1 : ty1 - ty2;

   /* output geometry */
   if ((sdata = ec->comp_data->sub.data))
     {
        if (sdata->parent)
          {
             evhp->geo.output_r.x = sdata->parent->x + sdata->position.x;
             evhp->geo.output_r.y = sdata->parent->y + sdata->position.y;
          }
        else
          {
             evhp->geo.output_r.x = sdata->position.x;
             evhp->geo.output_r.y = sdata->position.y;
          }
     }
   else
     {
        evhp->geo.output_r.x = ec->x;
        evhp->geo.output_r.y = ec->y;
     }

   evhp->geo.output_r.w = ec->comp_data->width_from_viewport;
   evhp->geo.output_r.w = (evhp->geo.output_r.w + 1) & ~1;
   evhp->geo.output_r.h = ec->comp_data->height_from_viewport;

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   evhp->geo.output_r.x, evhp->geo.output_r.y,
                                   &evhp->geo.output_r.x, &evhp->geo.output_r.y);
   e_comp_object_frame_wh_unadjust(ec->frame,
                                   evhp->geo.output_r.w, evhp->geo.output_r.h,
                                   &evhp->geo.output_r.w, &evhp->geo.output_r.h);

   evhp->geo.transform = vp->buffer.transform;

   _e_video_geometry_cal_physical(evhp);

   VDB("geometry(%dx%d  %d,%d %dx%d  %d,%d %dx%d  %d)",
       evhp->geo.input_w, evhp->geo.input_h,
       EINA_RECTANGLE_ARGS(&evhp->geo.input_r),
       EINA_RECTANGLE_ARGS(&evhp->geo.output_r),
       evhp->geo.transform);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_geometry_cal_map(E_Video_Hwc_Planes *evhp)
{
   E_Client *ec;
   const Evas_Map *m;
   Evas_Coord x1, x2, y1, y2;
   Eina_Rectangle output_r;

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, EINA_FALSE);

   ec = evhp->ec;
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

   if (!memcmp(&evhp->geo.output_r, &output_r, sizeof(Eina_Rectangle)))
     return EINA_FALSE;

   VDB("frame(%p) m(%p) output(%d,%d %dx%d) => (%d,%d %dx%d)", ec->frame, m,
       EINA_RECTANGLE_ARGS(&evhp->geo.output_r), EINA_RECTANGLE_ARGS(&output_r));

   evhp->geo.output_r = output_r;

   _e_video_geometry_cal_physical(evhp);

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
_e_video_geometry_cal_to_input_rect(E_Video_Hwc_Planes * evhp, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int xf1, yf1, xf2, yf2;

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_video_geometry_cal_to_input(evhp->geo.output_r.w, evhp->geo.output_r.h,
                                  evhp->geo.input_r.w, evhp->geo.input_r.h,
                                  evhp->geo.transform, xf1, yf1, &xf1, &yf1);
   _e_video_geometry_cal_to_input(evhp->geo.output_r.w, evhp->geo.output_r.h,
                                  evhp->geo.input_r.w, evhp->geo.input_r.h,
                                  evhp->geo.transform, xf2, yf2, &xf2, &yf2);

   drect->x = MIN(xf1, xf2);
   drect->y = MIN(yf1, yf2);
   drect->w = MAX(xf1, xf2) - drect->x;
   drect->h = MAX(yf1, yf2) - drect->y;
}

static Eina_Bool
_e_video_geometry_cal(E_Video_Hwc_Planes *evhp)
{
   Eina_Rectangle screen = {0,};
   Eina_Rectangle output_r = {0,}, input_r = {0,};
   const tdm_output_mode *mode = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;

   /* get geometry information with buffer scale, transform and viewport. */
   if (!_e_video_geometry_cal_viewport(evhp))
     return EINA_FALSE;

   _e_video_geometry_cal_map(evhp);

   if (evhp->external_video)
     {
        tdm_err = tdm_output_get_mode(evhp->output, &mode);
        if (tdm_err != TDM_ERROR_NONE)
          return EINA_FALSE;

        if (mode == NULL)
          return EINA_FALSE;

        screen.w = mode->hdisplay;
        screen.h = mode->vdisplay;
     }
   else
     {
        E_Zone *zone;
        E_Client *topmost;

        topmost = e_comp_wl_topmost_parent_get(evhp->ec);
        EINA_SAFETY_ON_NULL_RETURN_VAL(topmost, EINA_FALSE);

        zone = e_comp_zone_xy_get(topmost->x, topmost->y);
        EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

        screen.w = zone->w;
        screen.h = zone->h;
     }

   e_comp_wl_video_buffer_size_get(evhp->ec, &input_r.w, &input_r.h);
   // when topmost is not mapped, input size can be abnormal.
   // in this case, it will be render by topmost showing.
   if (!eina_rectangle_intersection(&evhp->geo.input_r, &input_r) || (evhp->geo.input_r.w <= 10 || evhp->geo.input_r.h <= 10))
     {
        VER("input area is empty");
        return EINA_FALSE;
     }

   if (evhp->geo.output_r.x >= 0 && evhp->geo.output_r.y >= 0 &&
       (evhp->geo.output_r.x + evhp->geo.output_r.w) <= screen.w &&
       (evhp->geo.output_r.y + evhp->geo.output_r.h) <= screen.h)
     return EINA_TRUE;

   /* TODO: need to improve */

   output_r = evhp->geo.output_r;
   if (!eina_rectangle_intersection(&output_r, &screen))
     {
        VER("output_r(%d,%d %dx%d) screen(%d,%d %dx%d) => intersect(%d,%d %dx%d)",
            EINA_RECTANGLE_ARGS(&evhp->geo.output_r),
            EINA_RECTANGLE_ARGS(&screen), EINA_RECTANGLE_ARGS(&output_r));
        return EINA_TRUE;
     }

   output_r.x -= evhp->geo.output_r.x;
   output_r.y -= evhp->geo.output_r.y;

   if (output_r.w <= 0 || output_r.h <= 0)
     {
        VER("output area is empty");
        return EINA_FALSE;
     }

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   _e_video_geometry_cal_to_input_rect(evhp, &output_r, &input_r);

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   output_r.x += evhp->geo.output_r.x;
   output_r.y += evhp->geo.output_r.y;

   input_r.x += evhp->geo.input_r.x;
   input_r.y += evhp->geo.input_r.y;

   output_r.x = output_r.x & ~1;
   output_r.w = (output_r.w + 1) & ~1;

   input_r.x = input_r.x & ~1;
   input_r.w = (input_r.w + 1) & ~1;

   evhp->geo.output_r = output_r;
   evhp->geo.input_r = input_r;

   _e_video_geometry_cal_physical(evhp);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_can_commit(E_Video_Hwc_Planes *evhp)
{
   if (!evhp->external_video)
     {
        if (e_output_dpms_get(evhp->e_output))
          return EINA_FALSE;
     }

   return _e_video_is_visible(evhp);
}

static void
_e_video_commit_handler(tdm_layer *layer, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Planes *evhp;
   Eina_List *l;
   E_Comp_Wl_Video_Buf *vbuf;

   EINA_LIST_FOREACH(video_list, l, evhp)
     {
        if (evhp == user_data) break;
     }

   if (!evhp) return;
   if (!evhp->committed_list) return;

   if (_e_video_can_commit(evhp))
     {
        tbm_surface_h displaying_buffer = _e_video_layer_get_displaying_buffer(evhp->layer, NULL);

        EINA_LIST_FOREACH(evhp->committed_list, l, vbuf)
          {
             if (vbuf->tbm_surface == displaying_buffer) break;
          }
        if (!vbuf) return;
     }
   else
     vbuf = eina_list_nth(evhp->committed_list, 0);

   evhp->committed_list = eina_list_remove(evhp->committed_list, vbuf);

   /* client can attachs the same wl_buffer twice. */
   if (evhp->current_fb && VBUF_IS_VALID(evhp->current_fb) && vbuf != evhp->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhp->current_fb, EINA_FALSE);

        if (evhp->current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&evhp->current_fb->buffer_ref, NULL);
     }

   evhp->current_fb = vbuf;

   VDB("current_fb(%d)", MSTAMP(evhp->current_fb));
}

static void
_e_video_commit_buffer(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf)
{
   evhp->committed_list = eina_list_append(evhp->committed_list, vbuf);

   if (!_e_video_can_commit(evhp))
     goto no_commit;

   if (!_e_video_frame_buffer_show(evhp, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_commit_handler(NULL, 0, 0, 0, evhp);
   _e_video_vblank_handler(NULL, 0, 0, 0, evhp);
}

static void
_e_video_commit_from_waiting_list(E_Video_Hwc_Planes *evhp)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = eina_list_nth(evhp->waiting_list, 0);
   evhp->waiting_list = eina_list_remove(evhp->waiting_list, vbuf);

   _e_video_commit_buffer(evhp, vbuf);
}

static void
_e_video_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Planes *evhp;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, evhp)
     {
        if (evhp == user_data) break;
     }

   if (!evhp) return;

   evhp->waiting_vblank = EINA_FALSE;

   if (evhp->video_plane_ready_handler) return;

   if (evhp->waiting_list)
     _e_video_commit_from_waiting_list(evhp);
}

static void
_e_video_video_set_hook(void *data, E_Plane *plane)
{
   E_Video_Hwc_Planes *evhp = (E_Video_Hwc_Planes *)data;

   if (evhp->e_plane != plane) return;
   if (evhp->waiting_vblank) return;

   if (evhp->waiting_list)
     _e_video_commit_from_waiting_list(evhp);

   E_FREE_FUNC(evhp->video_plane_ready_handler, e_plane_hook_del);
}

static Eina_Bool
_e_video_frame_buffer_show(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Client_Video_Info info, old_info;
   tdm_error ret;
   E_Client *topmost;
   Tdm_Prop_Value *prop;

   if (!vbuf)
     {
        if (evhp->layer)
          {
             VIN("unset layer: hide");
             _e_video_set_layer(evhp, EINA_FALSE);
          }
        return EINA_TRUE;
     }

   if (!evhp->layer)
     {
        VIN("set layer: show");
        if (!_e_video_set_layer(evhp, EINA_TRUE))
          {
             VER("set layer failed");
             return EINA_FALSE;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(evhp->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
             _e_video_layer_set_property(evhp->layer, prop);
             free(prop);
          }
     }

   CLEAR(old_info);
   ret = _e_video_layer_get_info(evhp->layer, &old_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   CLEAR(info);
   info.src_config.size.h = vbuf->width_from_pitch;
   info.src_config.size.v = vbuf->height_from_size;
   info.src_config.pos.x = vbuf->content_r.x;
   info.src_config.pos.y = vbuf->content_r.y;
   info.src_config.pos.w = vbuf->content_r.w;
   info.src_config.pos.h = vbuf->content_r.h;
   info.src_config.format = vbuf->tbmfmt;
   info.dst_pos.x = evhp->geo.tdm_output_r.x;
   info.dst_pos.y = evhp->geo.tdm_output_r.y;
   info.dst_pos.w = evhp->geo.tdm_output_r.w;
   info.dst_pos.h = evhp->geo.tdm_output_r.h;
   info.transform = vbuf->content_t;

   if (memcmp(&old_info, &info, sizeof(tdm_info_layer)))
     {
        ret = _e_video_layer_set_info(evhp->layer, &info);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);
     }

   ret = _e_video_layer_set_buffer(evhp->layer, vbuf->tbm_surface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = _e_video_layer_commit(evhp->layer, _e_video_commit_handler, evhp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = tdm_output_wait_vblank(evhp->output, 1, 0, _e_video_vblank_handler, evhp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   evhp->waiting_vblank = EINA_TRUE;

   EINA_LIST_FREE(evhp->late_tdm_prop_list, prop)
     {
        VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
        _e_video_layer_set_property(evhp->layer, prop);
        free(prop);
     }

   topmost = e_comp_wl_topmost_parent_get(evhp->ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(evhp->ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (evhp->geo.output_r.x == 0 || evhp->geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 evhp->geo.output_r.w < 100 && evhp->geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)",
                      bw, bh, evhp->geo.output_r.w, evhp->geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(evhp->ec->frame, EINA_TRUE);
             VIN("punched");
          }
     }

   if (e_video_debug_punch_value_get())
     {
        e_comp_object_mask_set(evhp->ec->frame, EINA_TRUE);
        VIN("punched");
     }

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhp->ec) ?: "No Name" , evhp->ec->netwm.pid,
       wl_resource_get_id(evhp->ec->comp_data->surface), vbuf, vbuf->ref_cnt,
       info.src_config.size.h, info.src_config.size.v, info.src_config.pos.x,
       info.src_config.pos.y, info.src_config.pos.w, info.src_config.pos.h,
       info.dst_pos.x, info.dst_pos.y, info.dst_pos.w, info.dst_pos.h, info.transform);


   return EINA_TRUE;
}

static void
_e_video_buffer_show(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (evhp->waiting_vblank || evhp->video_plane_ready_handler)
     {
        evhp->waiting_list = eina_list_append(evhp->waiting_list, vbuf);
        VDB("There are waiting fbs more than 1");
        return;
     }

   _e_video_commit_buffer(evhp, vbuf);
}

static void
_e_video_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (_e_video_geometry_cal_map(evhp))
     _e_video_render(evhp, __FUNCTION__);
}

static void
_e_video_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (_e_video_geometry_cal_map(evhp))
     _e_video_render(evhp, __FUNCTION__);
}

static void
_e_video_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_object_is_del(E_OBJECT(evhp->ec))) return;

   if (evhp->need_force_render)
     {
        VIN("video forcely rendering..");
        _e_video_render(evhp, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if ((evhp->ec->comp_data->sub.data && evhp->ec->comp_data->sub.data->stand_alone) ||
       (evhp->ec->comp_data->sub.data && evhp->follow_topmost_visibility))
     {
#if 0 //mute off is managed by client. mute off in server made many issues.
        if (!evhp->layer) return;

        if (evhp->tdm_mute_id != -1)
          {
             Tdm_Prop_Value prop = {.id = evhp->tdm_mute_id, .value.u32 = 0};
             VIN("video surface show. mute off (ec:%p)", evhp->ec);
             _e_video_layer_set_property(evhp->layer, &prop);
          }
#endif
        return;
     }

   if (!evhp->layer)
     {
        VIN("set layer: show");
        if (!_e_video_set_layer(evhp, EINA_TRUE))
          {
             VER("set layer failed");
             return;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(evhp->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
             _e_video_layer_set_property(evhp->layer, prop);
             free(prop);
          }
     }

   VIN("evas show (ec:%p)", evhp->ec);
   if (evhp->current_fb)
     _e_video_buffer_show(evhp, evhp->current_fb, evhp->current_fb->content_t);
}

static void
_e_video_cb_evas_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_object_is_del(E_OBJECT(evhp->ec))) return;

   /* if stand_alone is true, not hide */
   if (evhp->ec->comp_data->sub.data && evhp->ec->comp_data->sub.data->stand_alone)
     {
        if (!evhp->layer) return;

        if (evhp->tdm_mute_id != -1)
          {
             Tdm_Prop_Value prop = {.id = evhp->tdm_mute_id, .value.u32 = 1};
             VIN("video surface hide. mute on (ec:%p)", evhp->ec);
             _e_video_layer_set_property(evhp->layer, &prop);
          }
        return;
     }

   VIN("evas hide (ec:%p)", evhp->ec);
   _e_video_frame_buffer_show(evhp, NULL);
}

static E_Video_Hwc_Planes *
_e_video_create(E_Client *ec)
{
   E_Video_Hwc_Planes *evhp;

   evhp = calloc(1, sizeof *evhp);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, NULL);

   evhp->ec = ec;
   evhp->pp_align = -1;
   evhp->video_align = -1;
   evhp->tdm_mute_id = -1;

   if (!_e_video_set(evhp, ec))
     {
        free(evhp);
        return NULL;
     }

   VIN("create. ec(%p) wl_surface@%d", ec, wl_resource_get_id(evhp->ec->comp_data->surface));

   video_list = eina_list_append(video_list, evhp);

   return evhp;
}

static tdm_error
_e_video_layer_get_available_properties(E_Video_Layer *layer,
                                        const tdm_prop **props,
                                        int *count)
{
   tdm_error ret = TDM_ERROR_OPERATION_FAILED;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(props, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, TDM_ERROR_BAD_REQUEST);

   tdm_layer *tlayer = layer->tdm_layer;
   /* if layer wasn't set then get an any available tdm_layer */
   if (tlayer == NULL)
     {

        /* tlayer = e_output_video_available_tdm_layer_get(evhp->e_output); */
        tlayer = _e_video_tdm_available_video_layer_get(layer->evhp->output);
     }
   ret = tdm_layer_get_available_properties(tlayer, props, count);

   return ret;
}

static tdm_error
_e_video_layer_get_property(E_Video_Layer * layer, unsigned id, tdm_value *value)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(value, TDM_ERROR_BAD_REQUEST);

   return tdm_layer_get_property(layer->tdm_layer, id, value);
}

static Eina_Bool
_e_video_set(E_Video_Hwc_Planes *evhp, E_Client *ec)
{
   const tdm_prop *props;
   int i, count = 0;

   evhp->ec = ec;
   evhp->window = e_client_util_win_get(ec);

   if (e_config->eom_enable == EINA_TRUE)
     {
        evhp->external_video = e_eom_is_ec_external(ec);
        if (evhp->external_video)
          {
             tdm_error ret;
             unsigned int index = 0;

             evhp->output = e_eom_tdm_output_by_ec_get(ec);
             EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->output, EINA_FALSE);

             ret = tdm_output_get_pipe(evhp->output, &index);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

             evhp->e_output = e_output_find_by_index(index);
             EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->e_output, EINA_FALSE);

             ec->comp_data->video_client = 1;

             return EINA_TRUE;
          }
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->ec->zone, EINA_FALSE);

   evhp->e_output = e_output_find(evhp->ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->e_output, EINA_FALSE);

   evhp->output = evhp->e_output->toutput;
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp->output, EINA_FALSE);

   /* if (e_output_video_capability_get(evhp->e_output)) */
   if (_e_video_tdm_output_has_video_layer(evhp->output))
     {
        /* If tdm offers video layers, we will assign a tdm layer when showing */
        VIN("video client");
        ec->comp_data->video_client = 1;
     }
   else if (_e_video_set_layer(evhp, EINA_TRUE))
     {
        /* If tdm doesn't offer video layers, we assign a tdm layer now. If failed,
         * video will be displayed via the UI rendering path.
         */
        VIN("video client");
        ec->comp_data->video_client = 1;
     }
   else
     return EINA_FALSE;

   e_zone_video_available_size_get(ec->zone, NULL, NULL, NULL, NULL, &evhp->video_align);

   _e_video_layer_get_available_properties(evhp->layer, &props, &count);
   for (i = 0; i < count; i++)
     {
        tdm_value value;

        _e_video_layer_get_property(evhp->layer, props[i].id, &value);
        if (!strncmp(props[i].name, "mute", TDM_NAME_LEN))
          evhp->tdm_mute_id = props[i].id;
     }

   return EINA_TRUE;
}

static void
_e_video_hide(E_Video_Hwc_Planes *evhp)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (evhp->current_fb || evhp->committed_list)
     _e_video_frame_buffer_show(evhp, NULL);

   if (evhp->current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhp->current_fb, EINA_FALSE);
        evhp->current_fb = NULL;
     }

   if (evhp->old_comp_buffer)
     evhp->old_comp_buffer = NULL;

   EINA_LIST_FREE(evhp->committed_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   EINA_LIST_FREE(evhp->waiting_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_destroy(E_Video_Hwc_Planes *evhp)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   if (!evhp)
     return;

   VIN("destroy");

   if (evhp->cb_registered)
     {
        evas_object_event_callback_del_full(evhp->ec->frame, EVAS_CALLBACK_RESIZE,
                                            _e_video_cb_evas_resize, evhp);
        evas_object_event_callback_del_full(evhp->ec->frame, EVAS_CALLBACK_MOVE,
                                            _e_video_cb_evas_move, evhp);
     }

   _e_video_hide(evhp);

   /* others */
   EINA_LIST_FOREACH_SAFE(evhp->input_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   EINA_LIST_FOREACH_SAFE(evhp->pp_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   if(evhp->tdm_prop_list)
     {
        Tdm_Prop_Value *tdm_prop;
        EINA_LIST_FREE(evhp->tdm_prop_list, tdm_prop)
          {
             free(tdm_prop);
          }
     }
   if(evhp->late_tdm_prop_list)
     {
        Tdm_Prop_Value *tdm_prop;
        EINA_LIST_FREE(evhp->late_tdm_prop_list, tdm_prop)
          {
             free(tdm_prop);
          }
     }

   if (evhp->input_buffer_list)
     NEVER_GET_HERE();
   if (evhp->pp_buffer_list)
     NEVER_GET_HERE();
   if (evhp->tdm_prop_list)
     NEVER_GET_HERE();
   if (evhp->late_tdm_prop_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   if (evhp->pp)
     tdm_pp_destroy(evhp->pp);

   if (evhp->layer)
     {
        VIN("unset layer: destroy");
        _e_video_set_layer(evhp, EINA_FALSE);
     }

   video_list = eina_list_remove(video_list, evhp);

   free(evhp);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static Eina_Bool
_e_video_check_if_pp_needed(E_Video_Hwc_Planes *evhp)
{
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   tdm_layer_capability capabilities = 0;


   tdm_layer *layer = _e_video_tdm_video_layer_get(evhp->output);

   tdm_layer_get_capabilities(layer, &capabilities);

   /* don't need pp if a layer has TDM_LAYER_CAPABILITY_VIDEO capability*/
   if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
     return EINA_FALSE;

   /* check formats */
   tdm_layer_get_available_formats(layer, &formats, &count);
   for (i = 0; i < count; i++)
     if (formats[i] == evhp->tbmfmt)
       {
          found = EINA_TRUE;
          break;
       }

   if (!found)
     {
        if (formats && count > 0)
          evhp->pp_tbmfmt = formats[0];
        else
          {
             WRN("No layer format information!!!");
             evhp->pp_tbmfmt = TBM_FORMAT_ARGB8888;
          }
        return EINA_TRUE;
     }

   if (capabilities & TDM_LAYER_CAPABILITY_SCANOUT)
     goto need_pp;

   /* check size */
   if (evhp->geo.input_r.w != evhp->geo.output_r.w || evhp->geo.input_r.h != evhp->geo.output_r.h)
     if (!(capabilities & TDM_LAYER_CAPABILITY_SCALE))
       goto need_pp;

   /* check rotate */
   if (evhp->geo.transform || e_comp->e_comp_screen->rotation > 0)
     if (!(capabilities & TDM_LAYER_CAPABILITY_TRANSFORM))
       goto need_pp;

   return EINA_FALSE;

need_pp:
   evhp->pp_tbmfmt = evhp->tbmfmt;
   return EINA_TRUE;
}

static void
_e_video_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video_Hwc_Planes *evhp = (E_Video_Hwc_Planes*)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = _e_video_vbuf_find(evhp->input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = _e_video_vbuf_find(evhp->pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!_e_video_is_visible(evhp)) return;

        _e_video_buffer_show(evhp, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer");
        // there is no way to set in_use flag.
        // This will cause issue when server get available pp_buffer.
     }
}

static void
_e_video_render(E_Video_Hwc_Planes *evhp, const char *func)
{
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Video_Buf *pp_buffer = NULL;
   E_Comp_Wl_Video_Buf *input_buffer = NULL;
   E_Client *topmost;
   tbm_surface_h tbm_surf;

   EINA_SAFETY_ON_NULL_RETURN(evhp->ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!evhp->ec->pixmap)
     return;

   if (!_e_video_is_visible(evhp))
     {
        _e_video_hide(evhp);
        return;
     }

   comp_buffer = e_pixmap_resource_get(evhp->ec->pixmap);
   if (!comp_buffer) return;

   tbm_surf = wayland_tbm_server_get_surface(NULL, comp_buffer->resource);
   /* not interested with other buffer type */
   if (!tbm_surf)
     return;

   evhp->tbmfmt = tbm_surface_get_format(tbm_surf);

   topmost = e_comp_wl_topmost_parent_get(evhp->ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost");
        e_comp_wl_viewport_apply(topmost);
     }

   if (!_e_video_geometry_cal(evhp))
     {
        if(!evhp->need_force_render && !_e_video_parent_is_viewable(evhp))
          {
             VIN("need force render");
             evhp->need_force_render = EINA_TRUE;
          }
        return;
     }

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)", GEO_ARG(&evhp->old_geo), evhp->old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c", GEO_ARG(&evhp->geo), comp_buffer, FOURCC_STR(evhp->tbmfmt));

   if (!memcmp(&evhp->old_geo, &evhp->geo, sizeof evhp->geo) &&
       evhp->old_comp_buffer == comp_buffer)
     return;

   evhp->need_force_render = EINA_FALSE;

   _e_video_input_buffer_valid(evhp, comp_buffer);

   if (!_e_video_check_if_pp_needed(evhp))
     {
        /* 1. non converting case */
        input_buffer = _e_video_input_buffer_get(evhp, comp_buffer, EINA_TRUE);
        EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

        _e_video_buffer_show(evhp, input_buffer, evhp->geo.tdm_transform);

        evhp->old_geo = evhp->geo;
        evhp->old_comp_buffer = comp_buffer;

        goto done;
     }

   /* 2. converting case */
   if (!evhp->pp)
     {
        tdm_pp_capability pp_cap;
        tdm_error error = TDM_ERROR_NONE;

        evhp->pp = tdm_display_create_pp(e_comp->e_comp_screen->tdisplay, NULL);
        EINA_SAFETY_ON_NULL_GOTO(evhp->pp, render_fail);

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &evhp->pp_minw, &evhp->pp_minh,
                                          &evhp->pp_maxw, &evhp->pp_maxh, &evhp->pp_align);

        error = tdm_display_get_pp_capabilities(e_comp->e_comp_screen->tdisplay, &pp_cap);
        if (error == TDM_ERROR_NONE)
          {
             if (pp_cap & TDM_PP_CAPABILITY_SCANOUT)
               evhp->pp_scanout = EINA_TRUE;
          }
     }

   if ((evhp->pp_minw > 0 && (evhp->geo.input_r.w < evhp->pp_minw || evhp->geo.tdm_output_r.w < evhp->pp_minw)) ||
       (evhp->pp_minh > 0 && (evhp->geo.input_r.h < evhp->pp_minh || evhp->geo.tdm_output_r.h < evhp->pp_minh)) ||
       (evhp->pp_maxw > 0 && (evhp->geo.input_r.w > evhp->pp_maxw || evhp->geo.tdm_output_r.w > evhp->pp_maxw)) ||
       (evhp->pp_maxh > 0 && (evhp->geo.input_r.h > evhp->pp_maxh || evhp->geo.tdm_output_r.h > evhp->pp_maxh)))
     {
        INF("size(%dx%d, %dx%d) is out of PP range",
            evhp->geo.input_r.w, evhp->geo.input_r.h, evhp->geo.tdm_output_r.w, evhp->geo.tdm_output_r.h);
        goto done;
     }

   input_buffer = _e_video_input_buffer_get(evhp, comp_buffer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

   pp_buffer = _e_video_pp_buffer_get(evhp, evhp->geo.tdm_output_r.w, evhp->geo.tdm_output_r.h);
   EINA_SAFETY_ON_NULL_GOTO(pp_buffer, render_fail);

   if (memcmp(&evhp->old_geo, &evhp->geo, sizeof evhp->geo))
     {
        tdm_info_pp info;

        CLEAR(info);
        info.src_config.size.h = input_buffer->width_from_pitch;
        info.src_config.size.v = input_buffer->height_from_size;
        info.src_config.pos.x = evhp->geo.input_r.x;
        info.src_config.pos.y = evhp->geo.input_r.y;
        info.src_config.pos.w = evhp->geo.input_r.w;
        info.src_config.pos.h = evhp->geo.input_r.h;
        info.src_config.format = evhp->tbmfmt;
        info.dst_config.size.h = pp_buffer->width_from_pitch;
        info.dst_config.size.v = pp_buffer->height_from_size;
        info.dst_config.pos.w = evhp->geo.tdm_output_r.w;
        info.dst_config.pos.h = evhp->geo.tdm_output_r.h;
        info.dst_config.format = evhp->pp_tbmfmt;
        info.transform = evhp->geo.tdm_transform;

        if (tdm_pp_set_info(evhp->pp, &info))
          {
             VER("tdm_pp_set_info() failed");
             goto render_fail;
          }

        if (tdm_pp_set_done_handler(evhp->pp, _e_video_pp_cb_done, evhp))
          {
             VER("tdm_pp_set_done_handler() failed");
             goto render_fail;
          }

        CLEAR(evhp->pp_r);
        evhp->pp_r.w = info.dst_config.pos.w;
        evhp->pp_r.h = info.dst_config.pos.h;
     }

   pp_buffer->content_r = evhp->pp_r;

   if (tdm_pp_attach(evhp->pp, input_buffer->tbm_surface, pp_buffer->tbm_surface))
     {
        VER("tdm_pp_attach() failed");
        goto render_fail;
     }

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);

   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, comp_buffer);

   if (tdm_pp_commit(evhp->pp))
     {
        VER("tdm_pp_commit() failed");
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        goto render_fail;
     }

   evhp->old_geo = evhp->geo;
   evhp->old_comp_buffer = comp_buffer;

   goto done;

render_fail:
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

done:
   if (!evhp->cb_registered)
     {
        evas_object_event_callback_add(evhp->ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_cb_evas_resize, evhp);
        evas_object_event_callback_add(evhp->ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_cb_evas_move, evhp);
        evhp->cb_registered = EINA_TRUE;
     }
   DBG("======================================.");
}

static Eina_Bool
_e_video_cb_ec_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   E_Video_Hwc_Planes *evhp;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   evhp = data;
   ec = ev->ec;

   if (evhp->ec != ec)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   /* skip external client buffer if its top parent is not current for eom anymore */
   if (evhp->external_video && e_eom_is_ec_external(ec))
     {
        VWR("skip external buffer");
        return ECORE_CALLBACK_PASS_ON;
     }

   _e_video_render(evhp, __FUNCTION__);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_client_show(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Client *video_ec = NULL;
   E_Video_Hwc_Planes *evhp = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video_ec = find_video_child_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   evhp = data;
   if (!evhp) return ECORE_CALLBACK_PASS_ON;

   VIN("client(0x%08"PRIxPTR") show: find video child(0x%08"PRIxPTR")", (Ecore_Window)e_client_util_win_get(ec), (Ecore_Window)e_client_util_win_get(video_ec));
   if (evhp->old_comp_buffer)
     {
        VIN("video already rendering..");
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(evhp->ec))
     {
        /* skip external client buffer if its top parent is not current for eom anymore */
        if (evhp->external_video && e_eom_is_ec_external(ec))
          {
             VWR("skip external buffer");
             return ECORE_CALLBACK_PASS_ON;
          }

        VIN("video need rendering..");
        e_comp_wl_viewport_apply(ec);
        _e_video_render(evhp, __FUNCTION__);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev = event;
   E_Client *ec = ev->ec;
   E_Video_Hwc_Planes *evhp;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, evhp)
     {
        E_Client *offscreen_parent = find_offscreen_parent_get(evhp->ec);
        if (!offscreen_parent) continue;
        if (offscreen_parent != ec) continue;
        switch (ec->visibility.obscured)
          {
           case E_VISIBILITY_FULLY_OBSCURED:
              _e_video_cb_evas_hide(evhp, NULL, NULL, NULL);
              break;
           case E_VISIBILITY_UNOBSCURED:
              _e_video_cb_evas_show(evhp, NULL, NULL, NULL);
              break;
           default:
              VER("Not implemented");
              return ECORE_CALLBACK_PASS_ON;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_topmost_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec = ev->ec;
   E_Video_Hwc_Planes *evhp;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(video_list, l, evhp)
     {
        E_Client *topmost = e_comp_wl_topmost_parent_get(evhp->ec);
        if (!topmost) continue;
        if (topmost == evhp->ec) continue;
        if (topmost != ec) continue;
        if (evhp->follow_topmost_visibility)
          {
             switch (ec->visibility.obscured)
               {
                case E_VISIBILITY_FULLY_OBSCURED:
                   VIN("follow_topmost_visibility: fully_obscured");
                   _e_video_cb_evas_hide(evhp, NULL, NULL, NULL);
                   break;
                case E_VISIBILITY_UNOBSCURED:
                   VIN("follow_topmost_visibility: UNOBSCURED");
                   _e_video_cb_evas_show(evhp, NULL, NULL, NULL);
                   break;
                default:
                   return ECORE_CALLBACK_PASS_ON;
               }
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static tdm_layer *
_e_video_tdm_video_layer_get(tdm_output *output)
{
   int i, count = 0;
#ifdef CHECKING_PRIMARY_ZPOS
   int primary_idx = 0, primary_zpos = 0;
   tdm_layer *primary_layer;
#endif

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   tdm_output_get_layer_count(output, &count);
   for (i = 0; i < count; i++)
     {
        tdm_layer *layer = tdm_output_get_layer(output, i, NULL);
        tdm_layer_capability capabilities = 0;
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
          return layer;
     }

#ifdef CHECKING_PRIMARY_ZPOS
   tdm_output_get_primary_index(output, &primary_idx);
   primary_layer = tdm_output_get_layer(output, primary_idx, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(primary_layer, NULL);
   tdm_layer_get_zpos(primary_layer, &primary_zpos);
#endif

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer = tdm_output_get_layer(output, i, NULL);
        tdm_layer_capability capabilities = 0;
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_OVERLAY)
          {
#ifdef CHECKING_PRIMARY_ZPOS
             int zpos = 0;
             tdm_layer_get_zpos(layer, &zpos);
             if (zpos >= primary_zpos) continue;
#endif
             return layer;
          }
     }

   return NULL;
}

static tdm_layer *
_e_video_tdm_available_video_layer_get(tdm_output *output)
{
   Eina_Bool has_video_layer = EINA_FALSE;
   int i, count = 0;
#ifdef CHECKING_PRIMARY_ZPOS
   int primary_idx = 0, primary_zpos = 0;
   tdm_layer *primary_layer;
#endif

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   /* check video layers first */
   tdm_output_get_layer_count(output, &count);
   for (i = 0; i < count; i++)
     {
        tdm_layer *layer = tdm_output_get_layer(output, i, NULL);
        tdm_layer_capability capabilities = 0;
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
          {
             has_video_layer = EINA_TRUE;
             if (!_e_video_tdm_get_layer_usable(layer)) continue;
             return layer;
          }
     }

   /* if a output has video layers, it means that there is no available video layer for video */
   if (has_video_layer)
     return NULL;

   /* check graphic layers second */
#ifdef CHECKING_PRIMARY_ZPOS
   tdm_output_get_primary_index(output, &primary_idx);
   primary_layer = tdm_output_get_layer(output, primary_idx, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(primary_layer, NULL);
   tdm_layer_get_zpos(primary_layer, &primary_zpos);
#endif

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer = tdm_output_get_layer(output, i, NULL);
        tdm_layer_capability capabilities = 0;
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_OVERLAY)
          {
#ifdef CHECKING_PRIMARY_ZPOS
             int zpos = 0;
             tdm_layer_get_zpos(layer, &zpos);
             if (zpos >= primary_zpos) continue;
#endif
             if (!_e_video_tdm_get_layer_usable(layer)) continue;
             return layer;
          }
     }

   return NULL;
}

static void
_e_video_tdm_set_layer_usable(tdm_layer *layer, Eina_Bool usable)
{
   if (usable)
     video_layers = eina_list_remove(video_layers, layer);
   else
     {
        tdm_layer *used_layer;
        Eina_List *l = NULL;
        EINA_LIST_FOREACH(video_layers, l, used_layer)
           if (used_layer == layer) return;
        video_layers = eina_list_append(video_layers, layer);
     }
}

static Eina_Bool
_e_video_tdm_get_layer_usable(tdm_layer *layer)
{
   tdm_layer *used_layer;
   Eina_List *l = NULL;
   EINA_LIST_FOREACH(video_layers, l, used_layer)
      if (used_layer == layer)
        return EINA_FALSE;
   return EINA_TRUE;
}

static void
_e_video_hwc_planes_ec_event_deinit(E_Video_Hwc_Planes *evhp)
{
   E_Client *ec;

   ec = evhp->ec;

   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_cb_evas_show, evhp);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_HIDE,
                                       _e_video_cb_evas_hide, evhp);

   E_FREE_LIST(evhp->ec_event_handler, ecore_event_handler_del);
}

const char *
_e_video_hwc_planes_prop_name_get_by_id(E_Video_Hwc_Planes *evhp, unsigned int id)
{
   tdm_layer *layer;
   const tdm_prop *props;
   int i, count = 0;

   layer = _e_video_tdm_video_layer_get(evhp->output);
   tdm_layer_get_available_properties(layer, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          {
             VDB("check property(%s)", props[i].name);
             return props[i].name;
          }
     }

   return NULL;
}

static Eina_Bool
_e_video_hwc_planes_property_post_set(E_Video_Hwc_Planes *evhp,
                                      unsigned int id,
                                      const char *name,
                                      tdm_value value)
{
   Tdm_Prop_Value *prop = NULL;
   const Eina_List *l = NULL;

   EINA_LIST_FOREACH(evhp->late_tdm_prop_list, l, prop)
     {
        if (!strncmp(name, prop->name, TDM_NAME_LEN))
          {
             prop->value.u32 = value.u32;
             VDB("update property(%s) value(%d)", prop->name, value.u32);
             return EINA_FALSE;
          }
     }

   prop = calloc(1, sizeof(Tdm_Prop_Value));
   if(!prop) return EINA_FALSE;

   prop->value.u32 = value.u32;
   prop->id = id;
   memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
   VIN("Add property(%s) value(%d)", prop->name, value.u32);
   evhp->late_tdm_prop_list = eina_list_append(evhp->late_tdm_prop_list, prop);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_property_pre_set(E_Video_Hwc_Planes *evhp,
                                     unsigned int id,
                                     const char *name,
                                     tdm_value value)
{
   Tdm_Prop_Value *prop = NULL;
   const Eina_List *l = NULL;

   EINA_LIST_FOREACH(evhp->tdm_prop_list, l, prop)
     {
        if (!strncmp(name, prop->name, TDM_NAME_LEN))
          {
             VDB("find prop data(%s) update value(%d -> %d)",
                 prop->name, (unsigned int)prop->value.u32, (unsigned int)value.u32);
             prop->value.u32 = value.u32;
             return EINA_TRUE;
          }
     }
   EINA_LIST_FOREACH(evhp->late_tdm_prop_list, l, prop)
     {
        if (!strncmp(name, prop->name, TDM_NAME_LEN))
          {
             VDB("find prop data(%s) update value(%d -> %d)",
                 prop->name, (unsigned int)prop->value.u32, (unsigned int)value.u32);
             prop->value.u32 = value.u32;
             return EINA_TRUE;
          }
     }

   prop = calloc(1, sizeof(Tdm_Prop_Value));
   if(!prop) return EINA_FALSE;
   prop->value.u32 = value.u32;
   prop->id = id;
   memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
   VIN("Add property(%s) value(%d)", prop->name, value.u32);
   evhp->tdm_prop_list = eina_list_append(evhp->tdm_prop_list, prop);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_property_save(E_Video_Hwc_Planes *evhp, unsigned int id, const char *name, tdm_value value)
{
   /* FIXME workaround
    * if mute off, need to do it after buffer commit */
   if ((id == evhp->tdm_mute_id) && value.u32 == 0)
     return _e_video_hwc_planes_property_post_set(evhp, id, name, value);
   else
     return _e_video_hwc_planes_property_pre_set(evhp, id, name, value);
}

static void
_e_video_hwc_planes_ec_event_init(E_Video_Hwc_Planes *evhp)
{
   E_Client *ec;

   ec = evhp->ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_cb_evas_show, evhp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,
                                  _e_video_cb_evas_hide, evhp);

   E_LIST_HANDLER_APPEND(evhp->ec_event_handler, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_cb_ec_buffer_change, evhp);
   E_LIST_HANDLER_APPEND(evhp->ec_event_handler, E_EVENT_CLIENT_SHOW,
                         _e_video_cb_ec_client_show, evhp);
   E_LIST_HANDLER_APPEND(evhp->ec_event_handler, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, evhp);
   E_LIST_HANDLER_APPEND(evhp->ec_event_handler, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_video_cb_topmost_ec_visibility_change, evhp);
}

static void
_e_video_hwc_planes_iface_destroy(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   _e_video_hwc_planes_ec_event_deinit(evhp);
   _e_video_destroy(evhp);
}

static Eina_Bool
_e_video_hwc_planes_iface_follow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->follow_topmost_visibility = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_unfollow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->follow_topmost_visibility = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_allowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->allowed_attribute = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_disallowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->allowed_attribute = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_property_get(E_Video_Comp_Iface *iface, unsigned int id, tdm_value *value)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = _e_video_layer_get_property(evhp->layer, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_property_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   Tdm_Prop_Value prop;
   const char *name;

   IFACE_ENTRY;

   VIN("set layer: set_attribute");

   name = _e_video_hwc_planes_prop_name_get_by_id(evhp, id);

   if (!evhp->layer)
     {
        /* FIXME
         * Set property with assigning layer right away if allowed_attribute
         * flag is set. The reason why we have to do like this isn't figured
         * yet. It's for backward compatibility. */
        if (evhp->allowed_attribute)
          {
             if (!_e_video_set_layer(evhp, EINA_TRUE))
               {
                  VER("set layer failed");
                  return EINA_FALSE;
               }
          }
        else
          {
             VIN("no layer: save property value");
             if (!_e_video_hwc_planes_property_save(evhp, id, name, value))
               {
                  VER("save property failed");
                  return EINA_FALSE;
               }

             return EINA_TRUE;
          }
     }

   VIN("set layer: call property(%s), value(%d)", name, value.u32);

   prop.id = id;
   prop.value = value;
   _e_video_layer_set_property(evhp->layer, &prop);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_property_delay_set(E_Video_Comp_Iface *iface, unsigned int id, tdm_value value)
{
   const char *name;

   IFACE_ENTRY;

   name = _e_video_hwc_planes_prop_name_get_by_id(evhp, id);

   _e_video_hwc_planes_property_post_set(evhp, id, name, value);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_available_properties_get(E_Video_Comp_Iface *iface, const tdm_prop **props, int *count)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = _e_video_layer_get_available_properties(evhp->layer, props, count);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_info_get(E_Video_Comp_Iface *iface, E_Client_Video_Info *info)
{
   tdm_error ret;

   IFACE_ENTRY;

   ret = _e_video_layer_get_info(evhp->layer, info);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_commit_data_release(E_Video_Comp_Iface *iface, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec)
{
   IFACE_ENTRY;

   if (!evhp->layer)
     return EINA_FALSE;

   _e_video_commit_handler(NULL, sequence, tv_sec, tv_usec, evhp);

   return EINA_TRUE;
}

static tbm_surface_h
_e_video_hwc_planes_iface_tbm_surface_get(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   if (!evhp->layer)
     return NULL;

   return evhp->layer->cur_tsurface;
}

EINTERN E_Video_Comp_Iface *
e_video_hwc_planes_iface_create(E_Client *ec)
{
   E_Video_Hwc_Planes *evhp;

   INF("Initializing HWC Planes mode");

   evhp = _e_video_create(ec);
   if (!evhp)
     {
        ERR("Failed to create 'E_Video_Hwc_Planes'");
        return NULL;
     }

   _e_video_hwc_planes_ec_event_init(evhp);

   evhp->base.destroy = _e_video_hwc_planes_iface_destroy;
   evhp->base.follow_topmost_visibility = _e_video_hwc_planes_iface_follow_topmost_visibility;
   evhp->base.unfollow_topmost_visibility = _e_video_hwc_planes_iface_unfollow_topmost_visibility;
   evhp->base.allowed_property = _e_video_hwc_planes_iface_allowed_property;
   evhp->base.disallowed_property = _e_video_hwc_planes_iface_disallowed_property;
   evhp->base.property_get = _e_video_hwc_planes_iface_property_get;
   evhp->base.property_set = _e_video_hwc_planes_iface_property_set;
   evhp->base.property_delay_set = _e_video_hwc_planes_iface_property_delay_set;
   evhp->base.available_properties_get = _e_video_hwc_planes_iface_available_properties_get;
   evhp->base.info_get = _e_video_hwc_planes_iface_info_get;
   evhp->base.commit_data_release = _e_video_hwc_planes_iface_commit_data_release;
   evhp->base.tbm_surface_get = _e_video_hwc_planes_iface_tbm_surface_get;

   return &evhp->base;
}
