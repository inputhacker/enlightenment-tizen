#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

//#define DUMP_BUFFER
#define CHECKING_PRIMARY_ZPOS

#define IFACE_ENTRY                                      \
   E_Video_Hwc_Planes *evhp;                              \
   evhp = container_of(iface, E_Video_Hwc_Planes, base.backend)

typedef struct _E_Video_Hwc_Planes E_Video_Hwc_Planes;
typedef struct _E_Video_Layer E_Video_Layer;
typedef struct _E_Video_Info_Layer E_Video_Info_Layer;

/* the new TDM API doesn't have layers, so we have to invent layer here*/
struct _E_Video_Layer
{
   E_Video_Hwc_Planes *evhp;

   tdm_layer *tdm_layer;
};

struct _E_Video_Hwc_Planes
{
   E_Video_Hwc base;

   E_Video_Layer *layer;
   E_Plane *e_plane;

   /* attributes */
   Eina_List *tdm_prop_list;
   Eina_List *late_tdm_prop_list;
   int tdm_mute_id;

   E_Plane_Hook *video_plane_ready_handler;
};

typedef struct _Tdm_Prop_Value
{
   unsigned int id;
   char name[TDM_NAME_LEN];
   tdm_value value;
} Tdm_Prop_Value;

static Eina_List *video_layers = NULL;

static Eina_Bool _e_video_set(E_Video_Hwc_Planes *evhp);
static void _e_video_destroy(E_Video_Hwc_Planes *evhp);
static void _e_video_render(E_Video_Hwc_Planes *evhp, const char *func);
static Eina_Bool _e_video_frame_buffer_show(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf);
static void _e_video_video_set_hook(void *data, E_Plane *plane);

static tdm_layer* _e_video_tdm_video_layer_get(tdm_output *output);
static tdm_layer* _e_video_tdm_available_video_layer_get(tdm_output *output);
static void _e_video_tdm_set_layer_usable(tdm_layer *layer, Eina_Bool usable);
static Eina_Bool _e_video_tdm_get_layer_usable(tdm_layer *layer);

static void _e_video_vblank_handler(tdm_output *output, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);


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


   /* layer->tdm_layer = e_output_video_available_tdm_layer_get(evhp->e_output); */
   layer->tdm_layer = _e_video_tdm_available_video_layer_get(evhp->base.output);
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
             VIN("stop video", evhp->base.ec);
             _e_video_layer_unset_buffer(evhp->layer);
             _e_video_layer_commit(evhp->layer, NULL, NULL);
          }

        VIN("release layer: %p", evhp->base.ec, evhp->layer);
        _e_video_layer_destroy(evhp->layer);
        evhp->layer = NULL;
        evhp->base.old_comp_buffer = NULL;

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
             VWR("no available layer for evhp", evhp->base.ec);
             return EINA_FALSE;
          }


        ret = tdm_layer_get_zpos(evhp->layer->tdm_layer, &zpos);
        if (ret == TDM_ERROR_NONE)
          evhp->e_plane = e_output_plane_get_by_zpos(evhp->base.e_output, zpos);

        if (!evhp->e_plane)
          {
             VWR("fail get e_plane", evhp->base.ec);
             _e_video_layer_destroy(evhp->layer);
             evhp->layer = NULL;
             return EINA_FALSE;
          }

        if (!e_plane_video_set(evhp->e_plane, EINA_TRUE, &need_wait))
          {
             VWR("fail set video to e_plane", evhp->base.ec);
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

        VIN("assign layer: %p", evhp->base.ec, evhp->layer);
     }

   return EINA_TRUE;
}

static void
_e_video_input_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Video_Hwc_Planes *evhp = data;
   Eina_Bool need_hide = EINA_FALSE;

   DBG("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   evhp->base.input_buffer_list = eina_list_remove(evhp->base.input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (evhp->base.current_fb == vbuf)
     {
        VIN("current fb destroyed", evhp->base.ec);
        e_comp_wl_video_buffer_set_use(evhp->base.current_fb, EINA_FALSE);
        evhp->base.current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhp->base.committed_list, vbuf))
     {
        VIN("committed fb destroyed", evhp->base.ec);
        evhp->base.committed_list = eina_list_remove(evhp->base.committed_list, vbuf);
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(evhp->base.waiting_list, vbuf))
     {
        VIN("waiting fb destroyed", evhp->base.ec);
        evhp->base.waiting_list = eina_list_remove(evhp->base.waiting_list, vbuf);
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
   int aligned_width = ROUNDUP(vbuf->width_from_pitch, evhp->base.pp_align);

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = comp_buf;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)", evhp->base.ec,
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);
   e_comp_wl_video_buffer_unref(vbuf);

   evhp->base.geo.input_w = vbuf->width_from_pitch;
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

   vbuf = e_video_hwc_vbuf_find_with_comp_buffer(evhp->base.input_buffer_list, comp_buffer);
   if (vbuf)
     {
        vbuf->content_r = evhp->base.geo.input_r;
        return vbuf;
     }

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   if (evhp->base.pp_scanout)
     {
        Eina_Bool input_buffer_scanout = EINA_FALSE;
        input_buffer_scanout = _e_video_input_buffer_scanout_check(vbuf);
        if (!input_buffer_scanout) need_pp_scanout = EINA_TRUE;
     }

   if (evhp->base.pp)
     {
        if ((evhp->base.pp_align != -1 && (vbuf->width_from_pitch % evhp->base.pp_align)) ||
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

   vbuf->content_r = evhp->base.geo.input_r;

   evhp->base.input_buffer_list = eina_list_append(evhp->base.input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_input_buffer_cb_free, evhp);

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p) created, refcnt:%d"
       " scanout=%d", e_client_util_name_get(evhp->base.ec) ?: "No Name" ,
       evhp->base.ec->netwm.pid, wl_resource_get_id(evhp->base.ec->comp_data->surface), vbuf,
       vbuf->ref_cnt, scanout);

   return vbuf;
}

static void
_e_video_input_buffer_valid(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(evhp->base.input_buffer_list, l, vbuf)
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

static Eina_Bool
_e_video_can_commit(E_Video_Hwc_Planes *evhp)
{
   if (e_output_dpms_get(evhp->base.e_output))
     return EINA_FALSE;

   return e_video_hwc_client_visible_get(evhp->base.ec);
}

static void
_e_video_commit_handler(tdm_layer *layer, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Planes *evhp;
   Eina_List *l;
   E_Comp_Wl_Video_Buf *vbuf;

   evhp = user_data;
   if (!evhp) return;
   if (!evhp->base.committed_list) return;

   if (_e_video_can_commit(evhp))
     {
        tbm_surface_h displaying_buffer = _e_video_layer_get_displaying_buffer(evhp->layer, NULL);

        EINA_LIST_FOREACH(evhp->base.committed_list, l, vbuf)
          {
             if (vbuf->tbm_surface == displaying_buffer) break;
          }
        if (!vbuf) return;
     }
   else
     vbuf = eina_list_nth(evhp->base.committed_list, 0);

   evhp->base.committed_list = eina_list_remove(evhp->base.committed_list, vbuf);

   /* client can attachs the same wl_buffer twice. */
   if (evhp->base.current_fb && VBUF_IS_VALID(evhp->base.current_fb) && vbuf != evhp->base.current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhp->base.current_fb, EINA_FALSE);

        if (evhp->base.current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&evhp->base.current_fb->buffer_ref, NULL);
     }

   evhp->base.current_fb = vbuf;

   VDB("current_fb(%d)", evhp->base.ec, MSTAMP(evhp->base.current_fb));
}

static void
_e_video_commit_buffer(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf)
{
   evhp->base.committed_list = eina_list_append(evhp->base.committed_list, vbuf);

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

   vbuf = eina_list_nth(evhp->base.waiting_list, 0);
   evhp->base.waiting_list = eina_list_remove(evhp->base.waiting_list, vbuf);

   _e_video_commit_buffer(evhp, vbuf);
}

static void
_e_video_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video_Hwc_Planes *evhp;

   evhp = user_data;
   if (!evhp) return;

   evhp->base.waiting_vblank = EINA_FALSE;

   if (evhp->video_plane_ready_handler) return;

   if (evhp->base.waiting_list)
     _e_video_commit_from_waiting_list(evhp);
}

static void
_e_video_video_set_hook(void *data, E_Plane *plane)
{
   E_Video_Hwc_Planes *evhp = (E_Video_Hwc_Planes *)data;

   if (evhp->e_plane != plane) return;

   E_FREE_FUNC(evhp->video_plane_ready_handler, e_plane_hook_del);

   if (evhp->base.waiting_vblank) return;

   if (evhp->base.waiting_list)
     _e_video_commit_from_waiting_list(evhp);
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
             VIN("unset layer: hide", evhp->base.ec);
             _e_video_set_layer(evhp, EINA_FALSE);
          }
        return EINA_TRUE;
     }

   if (!evhp->layer)
     {
        VIN("set layer: show", evhp->base.ec);
        if (!_e_video_set_layer(evhp, EINA_TRUE))
          {
             VER("set layer failed", evhp->base.ec);
             return EINA_FALSE;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(evhp->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", evhp->base.ec, prop->name,
                 (unsigned int)prop->value.u32);
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
   info.dst_pos.x = evhp->base.geo.tdm.output_r.x;
   info.dst_pos.y = evhp->base.geo.tdm.output_r.y;
   info.dst_pos.w = evhp->base.geo.tdm.output_r.w;
   info.dst_pos.h = evhp->base.geo.tdm.output_r.h;
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

   ret = tdm_output_wait_vblank(evhp->base.output, 1, 0, _e_video_vblank_handler, evhp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   evhp->base.waiting_vblank = EINA_TRUE;

   EINA_LIST_FREE(evhp->late_tdm_prop_list, prop)
     {
        VIN("call property(%s), value(%d)", evhp->base.ec,
            prop->name, (unsigned int)prop->value.u32);
        _e_video_layer_set_property(evhp->layer, prop);
        free(prop);
     }

   topmost = e_comp_wl_topmost_parent_get(evhp->base.ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(evhp->base.ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (evhp->base.geo.output_r.x == 0 || evhp->base.geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 evhp->base.geo.output_r.w < 100 && evhp->base.geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)", evhp->base.ec,
                      bw, bh, evhp->base.geo.output_r.w, evhp->base.geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(evhp->base.ec->frame, EINA_TRUE);
             VIN("punched", evhp->base.ec);
          }
     }

   if (e_video_debug_punch_value_get())
     {
        e_comp_object_mask_set(evhp->base.ec->frame, EINA_TRUE);
        VIN("punched", evhp->base.ec);
     }

   DBG("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(evhp->base.ec) ?: "No Name" , evhp->base.ec->netwm.pid,
       wl_resource_get_id(evhp->base.ec->comp_data->surface), vbuf, vbuf->ref_cnt,
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

   if (evhp->base.waiting_vblank || evhp->video_plane_ready_handler)
     {
        evhp->base.waiting_list = eina_list_append(evhp->base.waiting_list, vbuf);
        VDB("There are waiting fbs more than 1", evhp->base.ec);
        return;
     }

   _e_video_commit_buffer(evhp, vbuf);
}

static void
_e_video_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_video_hwc_geometry_map_apply(evhp->base.ec, &evhp->base.geo))
     _e_video_render(evhp, __FUNCTION__);
}

static void
_e_video_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_video_hwc_geometry_map_apply(evhp->base.ec, &evhp->base.geo))
     _e_video_render(evhp, __FUNCTION__);
}

static void
_e_video_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_object_is_del(E_OBJECT(evhp->base.ec))) return;

   if (evhp->base.need_force_render)
     {
        VIN("video forcely rendering..", evhp->base.ec);
        _e_video_render(evhp, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if ((evhp->base.ec->comp_data->sub.data && evhp->base.ec->comp_data->sub.data->stand_alone) ||
       (evhp->base.ec->comp_data->sub.data && evhp->base.follow_topmost_visibility))
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
        VIN("set layer: show", evhp->base.ec);
        if (!_e_video_set_layer(evhp, EINA_TRUE))
          {
             VER("set layer failed", evhp->base.ec);
             return;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(evhp->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", evhp->base.ec,
                 prop->name, (unsigned int)prop->value.u32);
             _e_video_layer_set_property(evhp->layer, prop);
             free(prop);
          }
     }

   VIN("evas show", evhp->base.ec);
   if (evhp->base.current_fb)
     _e_video_buffer_show(evhp, evhp->base.current_fb, evhp->base.current_fb->content_t);
}

static void
_e_video_cb_evas_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_object_is_del(E_OBJECT(evhp->base.ec))) return;

   /* if stand_alone is true, not hide */
   if (evhp->base.ec->comp_data->sub.data && evhp->base.ec->comp_data->sub.data->stand_alone)
     {
        if (!evhp->layer) return;

        if (evhp->tdm_mute_id != -1)
          {
             Tdm_Prop_Value prop = {.id = evhp->tdm_mute_id, .value.u32 = 1};
             VIN("video surface hide. mute on", evhp->base.ec);
             _e_video_layer_set_property(evhp->layer, &prop);
          }
        return;
     }

   VIN("evas hide", evhp->base.ec);
   _e_video_frame_buffer_show(evhp, NULL);
}

static Eina_Bool
_e_video_hwc_planes_init(E_Video_Hwc_Planes *evhp)
{
   evhp->tdm_mute_id = -1;

   if (!_e_video_set(evhp))
     {
        VER("Failed to init hwc_planes", evhp->base.ec);
        return EINA_FALSE;
     }

   return EINA_TRUE;
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
        tlayer = _e_video_tdm_available_video_layer_get(layer->evhp->base.output);
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
_e_video_set(E_Video_Hwc_Planes *evhp)
{
   const tdm_prop *props;
   int i, count = 0;

   /* if (e_output_video_capability_get(evhp->e_output)) */
   if (_e_video_tdm_output_has_video_layer(evhp->base.output))
     {
        /* If tdm offers video layers, we will assign a tdm layer when showing */
        ;;;
     }
   else if (_e_video_set_layer(evhp, EINA_TRUE))
     {
        /* If tdm doesn't offer video layers, we assign a tdm layer now. If failed,
         * video will be displayed via the UI rendering path.
         */
        ;;;
     }
   else
     return EINA_FALSE;

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

   if (evhp->base.current_fb || evhp->base.committed_list)
     _e_video_frame_buffer_show(evhp, NULL);

   if (evhp->base.current_fb)
     {
        e_comp_wl_video_buffer_set_use(evhp->base.current_fb, EINA_FALSE);
        evhp->base.current_fb = NULL;
     }

   if (evhp->base.old_comp_buffer)
     evhp->base.old_comp_buffer = NULL;

   EINA_LIST_FREE(evhp->base.committed_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   EINA_LIST_FREE(evhp->base.waiting_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_destroy(E_Video_Hwc_Planes *evhp)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   if (!evhp)
     return;

   VIN("destroy", evhp->base.ec);

   if (evhp->base.cb_registered)
     {
        evas_object_event_callback_del_full(evhp->base.ec->frame, EVAS_CALLBACK_RESIZE,
                                            _e_video_cb_evas_resize, evhp);
        evas_object_event_callback_del_full(evhp->base.ec->frame, EVAS_CALLBACK_MOVE,
                                            _e_video_cb_evas_move, evhp);
     }

   _e_video_hide(evhp);

   /* others */
   EINA_LIST_FOREACH_SAFE(evhp->base.input_buffer_list, l, ll, vbuf)
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

   if (evhp->base.input_buffer_list)
     NEVER_GET_HERE();
   if (evhp->tdm_prop_list)
     NEVER_GET_HERE();
   if (evhp->late_tdm_prop_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   if (evhp->base.pp)
     tdm_pp_destroy(evhp->base.pp);

   if (evhp->layer)
     {
        VIN("unset layer: destroy", evhp->base.ec);
        _e_video_set_layer(evhp, EINA_FALSE);
     }

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


   tdm_layer *layer = _e_video_tdm_video_layer_get(evhp->base.output);

   tdm_layer_get_capabilities(layer, &capabilities);

   /* don't need pp if a layer has TDM_LAYER_CAPABILITY_VIDEO capability*/
   if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
     return EINA_FALSE;

   /* check formats */
   tdm_layer_get_available_formats(layer, &formats, &count);
   for (i = 0; i < count; i++)
     if (formats[i] == evhp->base.tbmfmt)
       {
          found = EINA_TRUE;
          break;
       }

   if (!found)
     {
        if (formats && count > 0)
          evhp->base.pp_tbmfmt = formats[0];
        else
          {
             WRN("No layer format information!!!");
             evhp->base.pp_tbmfmt = TBM_FORMAT_ARGB8888;
          }
        return EINA_TRUE;
     }

   if (capabilities & TDM_LAYER_CAPABILITY_SCANOUT)
     goto need_pp;

   /* check size */
   if (evhp->base.geo.input_r.w != evhp->base.geo.output_r.w || evhp->base.geo.input_r.h != evhp->base.geo.output_r.h)
     if (!(capabilities & TDM_LAYER_CAPABILITY_SCALE))
       goto need_pp;

   /* check rotate */
   if (evhp->base.geo.transform || e_comp->e_comp_screen->rotation > 0)
     if (!(capabilities & TDM_LAYER_CAPABILITY_TRANSFORM))
       goto need_pp;

   return EINA_FALSE;

need_pp:
   evhp->base.pp_tbmfmt = evhp->base.tbmfmt;
   return EINA_TRUE;
}

static void
_e_video_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video_Hwc_Planes *evhp = (E_Video_Hwc_Planes*)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = e_video_hwc_vbuf_find(evhp->base.input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = e_video_hwc_vbuf_find(evhp->base.pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!e_video_hwc_client_visible_get(evhp->base.ec)) return;

        _e_video_buffer_show(evhp, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer", evhp->base.ec);
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

   EINA_SAFETY_ON_NULL_RETURN(evhp->base.ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!evhp->base.ec->pixmap)
     return;

   if (!e_video_hwc_client_visible_get(evhp->base.ec))
     {
        _e_video_hide(evhp);
        return;
     }

   comp_buffer = e_pixmap_resource_get(evhp->base.ec->pixmap);
   if (!comp_buffer) return;

   evhp->base.tbmfmt = e_video_hwc_comp_buffer_tbm_format_get(comp_buffer);

   topmost = e_comp_wl_topmost_parent_get(evhp->base.ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost", evhp->base.ec);
        e_comp_wl_viewport_apply(topmost);
     }

   if (!e_video_hwc_geometry_get(evhp->base.ec, &evhp->base.geo))
     {
        if(!evhp->base.need_force_render && !e_video_hwc_client_parent_viewable_get(evhp->base.ec))
          {
             VIN("need force render", evhp->base.ec);
             evhp->base.need_force_render = EINA_TRUE;
          }
        return;
     }

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)", evhp->base.ec, GEO_ARG(&evhp->base.old_geo), evhp->base.old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c", evhp->base.ec, GEO_ARG(&evhp->base.geo), comp_buffer, FOURCC_STR(evhp->base.tbmfmt));

   if (!memcmp(&evhp->base.old_geo, &evhp->base.geo, sizeof evhp->base.geo) &&
       evhp->base.old_comp_buffer == comp_buffer)
     return;

   evhp->base.need_force_render = EINA_FALSE;

   _e_video_input_buffer_valid(evhp, comp_buffer);

   if (!_e_video_check_if_pp_needed(evhp))
     {
        /* 1. non converting case */
        input_buffer = _e_video_input_buffer_get(evhp, comp_buffer, EINA_TRUE);
        EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

        _e_video_buffer_show(evhp, input_buffer, evhp->base.geo.tdm.transform);

        evhp->base.old_geo = evhp->base.geo;
        evhp->base.old_comp_buffer = comp_buffer;

        goto done;
     }

   /* 2. converting case */
   if (!evhp->base.pp)
     {
        tdm_pp_capability pp_cap;
        tdm_error error = TDM_ERROR_NONE;

        evhp->base.pp = tdm_display_create_pp(e_comp->e_comp_screen->tdisplay, NULL);
        EINA_SAFETY_ON_NULL_GOTO(evhp->base.pp, render_fail);

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &evhp->base.pp_minw, &evhp->base.pp_minh,
                                          &evhp->base.pp_maxw, &evhp->base.pp_maxh, &evhp->base.pp_align);

        error = tdm_display_get_pp_capabilities(e_comp->e_comp_screen->tdisplay, &pp_cap);
        if (error == TDM_ERROR_NONE)
          {
             if (pp_cap & TDM_PP_CAPABILITY_SCANOUT)
               evhp->base.pp_scanout = EINA_TRUE;
          }
     }

   if ((evhp->base.pp_minw > 0 && (evhp->base.geo.input_r.w < evhp->base.pp_minw || evhp->base.geo.tdm.output_r.w < evhp->base.pp_minw)) ||
       (evhp->base.pp_minh > 0 && (evhp->base.geo.input_r.h < evhp->base.pp_minh || evhp->base.geo.tdm.output_r.h < evhp->base.pp_minh)) ||
       (evhp->base.pp_maxw > 0 && (evhp->base.geo.input_r.w > evhp->base.pp_maxw || evhp->base.geo.tdm.output_r.w > evhp->base.pp_maxw)) ||
       (evhp->base.pp_maxh > 0 && (evhp->base.geo.input_r.h > evhp->base.pp_maxh || evhp->base.geo.tdm.output_r.h > evhp->base.pp_maxh)))
     {
        INF("size(%dx%d, %dx%d) is out of PP range",
            evhp->base.geo.input_r.w, evhp->base.geo.input_r.h, evhp->base.geo.tdm.output_r.w, evhp->base.geo.tdm.output_r.h);
        goto done;
     }

   input_buffer = _e_video_input_buffer_get(evhp, comp_buffer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

   pp_buffer = e_video_hwc_pp_buffer_get((E_Video_Hwc *)evhp, evhp->base.geo.tdm.output_r.w, evhp->base.geo.tdm.output_r.h);
   EINA_SAFETY_ON_NULL_GOTO(pp_buffer, render_fail);

   if (memcmp(&evhp->base.old_geo, &evhp->base.geo, sizeof evhp->base.geo))
     {
        tdm_info_pp info;

        CLEAR(info);
        info.src_config.size.h = input_buffer->width_from_pitch;
        info.src_config.size.v = input_buffer->height_from_size;
        info.src_config.pos.x = evhp->base.geo.input_r.x;
        info.src_config.pos.y = evhp->base.geo.input_r.y;
        info.src_config.pos.w = evhp->base.geo.input_r.w;
        info.src_config.pos.h = evhp->base.geo.input_r.h;
        info.src_config.format = evhp->base.tbmfmt;
        info.dst_config.size.h = pp_buffer->width_from_pitch;
        info.dst_config.size.v = pp_buffer->height_from_size;
        info.dst_config.pos.w = evhp->base.geo.tdm.output_r.w;
        info.dst_config.pos.h = evhp->base.geo.tdm.output_r.h;
        info.dst_config.format = evhp->base.pp_tbmfmt;
        info.transform = evhp->base.geo.tdm.transform;

        if (tdm_pp_set_info(evhp->base.pp, &info))
          {
             VER("tdm_pp_set_info() failed", evhp->base.ec);
             goto render_fail;
          }

        if (tdm_pp_set_done_handler(evhp->base.pp, _e_video_pp_cb_done, evhp))
          {
             VER("tdm_pp_set_done_handler() failed", evhp->base.ec);
             goto render_fail;
          }

        CLEAR(evhp->base.pp_r);
        evhp->base.pp_r.w = info.dst_config.pos.w;
        evhp->base.pp_r.h = info.dst_config.pos.h;
     }

   pp_buffer->content_r = evhp->base.pp_r;

   if (tdm_pp_attach(evhp->base.pp, input_buffer->tbm_surface, pp_buffer->tbm_surface))
     {
        VER("tdm_pp_attach() failed", evhp->base.ec);
        goto render_fail;
     }

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);

   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, comp_buffer);

   if (tdm_pp_commit(evhp->base.pp))
     {
        VER("tdm_pp_commit() failed", evhp->base.ec);
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        goto render_fail;
     }

   evhp->base.old_geo = evhp->base.geo;
   evhp->base.old_comp_buffer = comp_buffer;

   goto done;

render_fail:
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

done:
   if (!evhp->base.cb_registered)
     {
        evas_object_event_callback_add(evhp->base.ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_cb_evas_resize, evhp);
        evas_object_event_callback_add(evhp->base.ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_cb_evas_move, evhp);
        evhp->base.cb_registered = EINA_TRUE;
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

   if (evhp->base.ec != ec)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

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

   video_ec = e_video_hwc_child_client_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   evhp = data;
   if (!evhp) return ECORE_CALLBACK_PASS_ON;

   VIN("show: find video child(0x%08"PRIxPTR")", evhp->base.ec,
       (Ecore_Window)e_client_util_win_get(video_ec));
   if (evhp->base.old_comp_buffer)
     {
        VIN("video already rendering..", evhp->base.ec);
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(evhp->base.ec))
     {
        VIN("video need rendering..", evhp->base.ec);
        e_comp_wl_viewport_apply(ec);
        _e_video_render(evhp, __FUNCTION__);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev;
   E_Client *ec, *offscreen_parent;
   E_Video_Hwc_Planes *evhp;

   evhp = data;
   offscreen_parent = e_video_hwc_client_offscreen_parent_get(evhp->base.ec);
   if (!offscreen_parent)
     goto end;

   ev = event;
   ec = ev->ec;
   if (offscreen_parent != ec)
     goto end;

   switch (ec->visibility.obscured)
     {
      case E_VISIBILITY_FULLY_OBSCURED:
         _e_video_cb_evas_hide(evhp, NULL, NULL, NULL);
         break;
      case E_VISIBILITY_UNOBSCURED:
         _e_video_cb_evas_show(evhp, NULL, NULL, NULL);
         break;
      default:
         VER("Not implemented", evhp->base.ec);
         return ECORE_CALLBACK_PASS_ON;
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_topmost_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec, *topmost;
   E_Video_Hwc_Planes *evhp;

   evhp = data;
   topmost = e_comp_wl_topmost_parent_get(evhp->base.ec);
   if (!topmost)
     goto end;

   if (topmost == evhp->base.ec)
     goto end;

   ev = event;
   ec = ev->ec;
   if (topmost != ec)
     goto end;

   if (evhp->base.follow_topmost_visibility)
     {
        switch (ec->visibility.obscured)
          {
           case E_VISIBILITY_FULLY_OBSCURED:
              VIN("follow_topmost_visibility: fully_obscured", ec);
              _e_video_cb_evas_hide(evhp, NULL, NULL, NULL);
              break;
           case E_VISIBILITY_UNOBSCURED:
              VIN("follow_topmost_visibility: UNOBSCURED", ec);
              _e_video_cb_evas_show(evhp, NULL, NULL, NULL);
              break;
           default:
              return ECORE_CALLBACK_PASS_ON;
          }
     }

end:
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

   ec = evhp->base.ec;

   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_cb_evas_show, evhp);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_HIDE,
                                       _e_video_cb_evas_hide, evhp);

   E_FREE_LIST(evhp->base.ec_event_handler, ecore_event_handler_del);
}

const char *
_e_video_hwc_planes_prop_name_get_by_id(E_Video_Hwc_Planes *evhp, unsigned int id)
{
   tdm_layer *layer;
   const tdm_prop *props;
   int i, count = 0;

   layer = _e_video_tdm_video_layer_get(evhp->base.output);
   tdm_layer_get_available_properties(layer, &props, &count);
   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          {
             VDB("check property(%s)", evhp->base.ec, props[i].name);
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
             VDB("update property(%s) value(%d)", evhp->base.ec, prop->name, value.u32);
             return EINA_FALSE;
          }
     }

   prop = calloc(1, sizeof(Tdm_Prop_Value));
   if(!prop) return EINA_FALSE;

   prop->value.u32 = value.u32;
   prop->id = id;
   memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
   VIN("Add property(%s) value(%d)", evhp->base.ec, prop->name, value.u32);
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
             VDB("find prop data(%s) update value(%d -> %d)", evhp->base.ec,
                 prop->name, (unsigned int)prop->value.u32, (unsigned int)value.u32);
             prop->value.u32 = value.u32;
             return EINA_TRUE;
          }
     }
   EINA_LIST_FOREACH(evhp->late_tdm_prop_list, l, prop)
     {
        if (!strncmp(name, prop->name, TDM_NAME_LEN))
          {
             VDB("find prop data(%s) update value(%d -> %d)", evhp->base.ec,
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
   VIN("Add property(%s) value(%d)", evhp->base.ec, prop->name, value.u32);
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

   ec = evhp->base.ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_cb_evas_show, evhp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,
                                  _e_video_cb_evas_hide, evhp);

   E_LIST_HANDLER_APPEND(evhp->base.ec_event_handler, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_cb_ec_buffer_change, evhp);
   E_LIST_HANDLER_APPEND(evhp->base.ec_event_handler, E_EVENT_CLIENT_SHOW,
                         _e_video_cb_ec_client_show, evhp);
   E_LIST_HANDLER_APPEND(evhp->base.ec_event_handler, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, evhp);
   E_LIST_HANDLER_APPEND(evhp->base.ec_event_handler, E_EVENT_CLIENT_VISIBILITY_CHANGE,
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

   evhp->base.follow_topmost_visibility = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_unfollow_topmost_visibility(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->base.follow_topmost_visibility = EINA_FALSE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_allowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->base.allowed_attribute = EINA_TRUE;
   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_disallowed_property(E_Video_Comp_Iface *iface)
{
   IFACE_ENTRY;

   evhp->base.allowed_attribute = EINA_FALSE;
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

   VIN("set layer: set_attribute", evhp->base.ec);

   name = _e_video_hwc_planes_prop_name_get_by_id(evhp, id);

   if (!evhp->layer)
     {
        /* FIXME
         * Set property with assigning layer right away if allowed_attribute
         * flag is set. The reason why we have to do like this isn't figured
         * yet. It's for backward compatibility. */
        if (evhp->base.allowed_attribute)
          {
             if (!_e_video_set_layer(evhp, EINA_TRUE))
               {
                  VER("set layer failed", evhp->base.ec);
                  return EINA_FALSE;
               }
          }
        else
          {
             VIN("no layer: save property value", evhp->base.ec);
             if (!_e_video_hwc_planes_property_save(evhp, id, name, value))
               {
                  VER("save property failed", evhp->base.ec);
                  return EINA_FALSE;
               }

             return EINA_TRUE;
          }
     }

   VIN("set layer: call property(%s), value(%d)", evhp->base.ec, name, value.u32);

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

EINTERN E_Video_Hwc *
e_video_hwc_planes_create(void)
{
   E_Video_Hwc_Planes *evhp;

   evhp = calloc(1, sizeof *evhp);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, NULL);

   return (E_Video_Hwc *)evhp;
}

EINTERN Eina_Bool
e_video_hwc_planes_init(E_Video_Hwc *evh)
{
   E_Video_Hwc_Planes *evhp;

   INF("Initializing HWC Planes mode");

   evhp = (E_Video_Hwc_Planes *)evh;
   if (!_e_video_hwc_planes_init(evhp))
     {
        ERR("Failed to init 'E_Video_Hwc_Planes'");
        return EINA_FALSE;
     }

   _e_video_hwc_planes_ec_event_init(evhp);

   evhp->base.backend.destroy = _e_video_hwc_planes_iface_destroy;
   evhp->base.backend.follow_topmost_visibility = _e_video_hwc_planes_iface_follow_topmost_visibility;
   evhp->base.backend.unfollow_topmost_visibility = _e_video_hwc_planes_iface_unfollow_topmost_visibility;
   evhp->base.backend.allowed_property = _e_video_hwc_planes_iface_allowed_property;
   evhp->base.backend.disallowed_property = _e_video_hwc_planes_iface_disallowed_property;
   evhp->base.backend.property_get = _e_video_hwc_planes_iface_property_get;
   evhp->base.backend.property_set = _e_video_hwc_planes_iface_property_set;
   evhp->base.backend.property_delay_set = _e_video_hwc_planes_iface_property_delay_set;
   evhp->base.backend.available_properties_get = _e_video_hwc_planes_iface_available_properties_get;
   evhp->base.backend.info_get = NULL;
   evhp->base.backend.commit_data_release = NULL;
   evhp->base.backend.tbm_surface_get = NULL;

   return EINA_TRUE;
}
