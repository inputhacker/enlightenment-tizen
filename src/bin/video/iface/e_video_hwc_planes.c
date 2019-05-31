#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "e_video_internal.h"
#include "e_video_hwc.h"

#define CHECKING_PRIMARY_ZPOS

typedef struct _E_Video_Hwc_Planes E_Video_Hwc_Planes;
typedef struct _E_Video_Info_Layer E_Video_Info_Layer;

struct _E_Video_Hwc_Planes
{
   E_Video_Hwc base;
   E_Plane *e_plane;
   E_Plane_Hook *video_plane_ready_handler;

   struct
     {
        tdm_output *output;
        tdm_layer *layer;
        /* attributes */
        Eina_List *prop_list;
        Eina_List *late_prop_list;
     } tdm;

   Eina_Bool waiting_vblank;
};

typedef struct _Tdm_Prop_Value
{
   unsigned int id;
   char name[TDM_NAME_LEN];
   tdm_value value;
} Tdm_Prop_Value;

static Eina_List *video_layers = NULL;

static tdm_layer *
_tdm_output_video_layer_get(tdm_output *output)
{
   tdm_layer *layer;
   tdm_layer_capability capabilities = 0;
   int i, count = 0;
#ifdef CHECKING_PRIMARY_ZPOS
   int primary_idx = 0, primary_zpos = 0;
   tdm_layer *primary_layer;
#endif

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   tdm_output_get_layer_count(output, &count);
   for (i = 0; i < count; i++)
     {
        layer = tdm_output_get_layer(output, i, NULL);
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
        layer = tdm_output_get_layer(output, i, NULL);
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

static Eina_Bool
_tdm_output_video_layer_exists(tdm_output *toutput)
{
   tdm_layer *layer;
   tdm_layer_capability lyr_capabilities = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(toutput, EINA_FALSE);

   /* get the first suitable layer */
   layer = _tdm_output_video_layer_get(toutput);
   if (!layer)
     return EINA_FALSE;

   tdm_layer_get_capabilities(layer, &lyr_capabilities);
   if (lyr_capabilities & TDM_LAYER_CAPABILITY_VIDEO)
     return EINA_TRUE;

   return EINA_FALSE;
}

static tdm_error
_tdm_layer_info_get(tdm_layer *layer, E_Client_Video_Info *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer tinfo = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   ret = tdm_layer_get_info(layer, &tinfo);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

   memcpy(&vinfo->src_config, &tinfo.src_config, sizeof(tdm_info_config));
   memcpy(&vinfo->dst_pos, &tinfo.dst_pos, sizeof(tdm_pos));
   vinfo->transform = tinfo.transform;

   return ret;
}

static tdm_error
_tdm_layer_info_set(tdm_layer *layer, E_Client_Video_Info *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer info_layer = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   memcpy(&info_layer.src_config, &vinfo->src_config, sizeof(tdm_info_config));
   memcpy(&info_layer.dst_pos, &vinfo->dst_pos, sizeof(tdm_pos));
   info_layer.transform = vinfo->transform;

   ret = tdm_layer_set_info(layer, &info_layer);

   return ret;
}

static tdm_error
_tdm_layer_buffer_set(tdm_layer *layer, tbm_surface_h buff)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buff, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_set_buffer(layer, buff);

   return ret;
}

static tdm_error
_tdm_layer_buffer_unset(tdm_layer *layer)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_unset_buffer(layer);

   return ret;
}

/*
 * This function checks if this layer was set
 */
static tdm_error
_tdm_layer_usable_get(tdm_layer *layer, unsigned int *usable)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(usable, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_is_usable(layer, usable);
   return ret;
}

static tdm_error
_tdm_layer_commit(tdm_layer *layer, tdm_layer_commit_handler func, void *user_data)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_commit(layer, func, user_data);

   return ret;
}

static tbm_surface_h
_tdm_layer_displaying_buffer_get(tdm_layer *layer, int *tdm_error)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

   return tdm_layer_get_displaying_buffer(layer, tdm_error);
}

static tdm_error
_tdm_layer_property_set(tdm_layer *layer, Tdm_Prop_Value *prop)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   ret = tdm_layer_set_property(layer, prop->id, prop->value);
   return ret;
}

static tdm_error
_tdm_layer_property_get(tdm_layer *layer, unsigned id, tdm_value *value)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(value, TDM_ERROR_BAD_REQUEST);

   return tdm_layer_get_property(layer, id, value);
}

static void
_tdm_layer_property_list_set(tdm_layer *layer, Eina_List *list)
{
   Tdm_Prop_Value *prop;

   EINA_LIST_FREE(list, prop)
     {
        VIN("call property(%s), value(%d)", NULL, prop->name,
            (unsigned int)prop->value.u32);
        _tdm_layer_property_set(layer, prop);
        free(prop);
     }
}

static void
_e_video_hwc_planes_cb_eplane_video_set_hook(void *data, E_Plane *plane)
{
   E_Video_Hwc_Planes *evhp;

   evhp = (E_Video_Hwc_Planes *)data;
   if (evhp->e_plane != plane) return;

   E_FREE_FUNC(evhp->video_plane_ready_handler, e_plane_hook_del);

   if (evhp->waiting_vblank) return;

   e_video_hwc_wait_buffer_commit((E_Video_Hwc *)evhp);
}

static void
_e_video_hwc_planes_tdm_layer_usable_set(tdm_layer *layer, Eina_Bool usable)
{
   tdm_layer *used_layer;
   Eina_List *l = NULL;

   if (usable)
     video_layers = eina_list_remove(video_layers, layer);
   else
     {
        EINA_LIST_FOREACH(video_layers, l, used_layer)
           if (used_layer == layer) return;
        video_layers = eina_list_append(video_layers, layer);
     }
}

static Eina_Bool
_e_video_hwc_planes_tdm_layer_usable_get(tdm_layer *layer)
{
   tdm_layer *used_layer;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(video_layers, l, used_layer)
      if (used_layer == layer)
        return EINA_FALSE;
   return EINA_TRUE;
}

static tdm_layer *
_e_video_hwc_planes_available_video_tdm_layer_get(tdm_output *output)
{
   tdm_layer *layer;
   tdm_layer_capability capabilities = 0;
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
        layer = tdm_output_get_layer(output, i, NULL);
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
          {
             has_video_layer = EINA_TRUE;
             if (!_e_video_hwc_planes_tdm_layer_usable_get(layer)) continue;
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
        layer = tdm_output_get_layer(output, i, NULL);
        EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_OVERLAY)
          {
#ifdef CHECKING_PRIMARY_ZPOS
             int zpos = 0;
             tdm_layer_get_zpos(layer, &zpos);
             if (zpos >= primary_zpos) continue;
#endif
             if (!_e_video_hwc_planes_tdm_layer_usable_get(layer)) continue;
             return layer;
          }
     }

   return NULL;
}

static Eina_Bool
_e_video_hwc_planes_tdm_layer_set(E_Video_Hwc_Planes *evhp)
{
   E_Plane *plane = NULL;
   Eina_Bool need_wait;
   tdm_layer *layer;
   tdm_error ret;
   int zpos;

   if (evhp->tdm.layer)
     return EINA_TRUE;

   layer = _e_video_hwc_planes_available_video_tdm_layer_get(evhp->tdm.output);
   if (!layer)
     {
        VWR("no available layer for evhp", NULL);
        return EINA_FALSE;
     }

   _e_video_hwc_planes_tdm_layer_usable_set(layer, EINA_FALSE);

   ret = tdm_layer_get_zpos(layer, &zpos);
   if (ret == TDM_ERROR_NONE)
     plane = e_output_plane_get_by_zpos(evhp->base.e_output, zpos);

   if (!plane)
     {
        VWR("fail get e_plane", NULL);
        goto err_plane;
     }

   if (!e_plane_video_set(plane, EINA_TRUE, &need_wait))
     {
        VWR("fail set video to e_plane", NULL);
        goto err_plane;
     }

   if (need_wait)
     {
        evhp->video_plane_ready_handler =
           e_plane_hook_add(E_PLANE_HOOK_VIDEO_SET,
                            _e_video_hwc_planes_cb_eplane_video_set_hook, evhp);
     }

   evhp->tdm.layer = layer;
   evhp->e_plane = plane;

   VIN("assign layer: %p", NULL, evhp->tdm.layer);

   return EINA_TRUE;

err_plane:
   _e_video_hwc_planes_tdm_layer_usable_set(evhp->tdm.layer, EINA_TRUE);

   return EINA_FALSE;
}

static void
_e_video_hwc_planes_tdm_layer_unset(E_Video_Hwc_Planes *evhp)
{
   unsigned int usable = 1;

   if (!evhp->tdm.layer) return;

   _tdm_layer_usable_get(evhp->tdm.layer, &usable);
   if (!usable && !evhp->video_plane_ready_handler)
     {
        VIN("stop video", evhp->base.ec);
        _tdm_layer_buffer_unset(evhp->tdm.layer);
        _tdm_layer_commit(evhp->tdm.layer, NULL, NULL);
     }

   VIN("release layer: %p", evhp->base.ec, evhp->tdm.layer);
   _e_video_hwc_planes_tdm_layer_usable_set(evhp->tdm.layer, EINA_TRUE);
   evhp->tdm.layer = NULL;
   evhp->base.old_comp_buffer = NULL;

   e_plane_video_set(evhp->e_plane, EINA_FALSE, NULL);
   evhp->e_plane = NULL;

   E_FREE_FUNC(evhp->video_plane_ready_handler, e_plane_hook_del);
}

static void
_e_video_hwc_planes_cb_commit_handler(tdm_layer *layer, unsigned int sequence,
                                      unsigned int tv_sec, unsigned int tv_usec,
                                      void *user_data)
{
   E_Video_Hwc_Planes *evhp;

   evhp = user_data;
   if (!evhp) return;

   e_video_hwc_current_fb_update((E_Video_Hwc *)evhp);
}

static void
_e_video_hwc_planes_cb_vblank_handler(tdm_output *output, unsigned int sequence,
                                      unsigned int tv_sec, unsigned int tv_usec,
                                      void *user_data)
{
   E_Video_Hwc_Planes *evhp;

   evhp = user_data;
   if (!evhp) return;

   evhp->waiting_vblank = EINA_FALSE;

   if (evhp->video_plane_ready_handler) return;

   e_video_hwc_wait_buffer_commit((E_Video_Hwc *)evhp);
}

static Eina_Bool
_e_video_hwc_planes_buffer_commit(E_Video_Hwc_Planes *evhp, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Client_Video_Info info, old_info;
   tdm_error ret;

   if (!evhp->tdm.layer)
     {
        VIN("set layer: show", evhp->base.ec);
        if (!_e_video_hwc_planes_tdm_layer_set(evhp))
          {
             VER("set layer failed", evhp->base.ec);
             return EINA_FALSE;
          }

        // need call tdm property in list
        _tdm_layer_property_list_set(evhp->tdm.layer, evhp->tdm.prop_list);
     }

   CLEAR(old_info);
   ret = _tdm_layer_info_get(evhp->tdm.layer, &old_info);
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
        ret = _tdm_layer_info_set(evhp->tdm.layer, &info);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);
     }

   ret = _tdm_layer_buffer_set(evhp->tdm.layer, vbuf->tbm_surface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = _tdm_layer_commit(evhp->tdm.layer, _e_video_hwc_planes_cb_commit_handler, evhp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = tdm_output_wait_vblank(evhp->tdm.output, 1, 0, _e_video_hwc_planes_cb_vblank_handler, evhp);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   evhp->waiting_vblank = EINA_TRUE;

   _tdm_layer_property_list_set(evhp->tdm.layer, evhp->tdm.late_prop_list);

   e_video_hwc_client_mask_update((E_Video_Hwc *)evhp);

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
_e_video_hwc_planes_cb_evas_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video_Hwc_Planes *evhp = data;

   if (e_object_is_del(E_OBJECT(evhp->base.ec))) return;

   /* if stand_alone is true, not hide */
   if (evhp->base.ec->comp_data->sub.data && evhp->base.ec->comp_data->sub.data->stand_alone)
     return;

   VIN("evas hide", evhp->base.ec);
   if (evhp->tdm.layer)
     {
        VIN("unset layer: hide", evhp->base.ec);
        _e_video_hwc_planes_tdm_layer_unset(evhp);
     }
}

static tdm_error
_e_video_hwc_planes_available_properties_get(E_Video_Hwc_Planes *evhp,
                                             const tdm_prop **props,
                                             int *count)
{
   tdm_layer *tlayer;
   tdm_error ret = TDM_ERROR_OPERATION_FAILED;

   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(props, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, TDM_ERROR_BAD_REQUEST);

   tlayer = evhp->tdm.layer;
   /* if layer wasn't set then get an any available tdm_layer */
   if (tlayer == NULL)
     tlayer = _e_video_hwc_planes_available_video_tdm_layer_get(evhp->tdm.output);
   ret = tdm_layer_get_available_properties(tlayer, props, count);

   return ret;
}

static Eina_Bool
_e_video_hwc_planes_init(E_Video_Hwc_Planes *evhp, E_Output *output)
{
   /* If tdm offers video layers, we will assign a tdm layer when showing */
   if (!_tdm_output_video_layer_exists(output->toutput))
     {
        /* If tdm doesn't offer video layers, we assign a tdm layer now. If
         * failed, video will be displayed via the UI rendering path. */
        if (!_e_video_hwc_planes_tdm_layer_set(evhp))
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_video_hwc_planes_destroy(E_Video_Hwc_Planes *evhp)
{
   Tdm_Prop_Value *tdm_prop;

   if (!evhp)
     return;

   VIN("destroy", evhp->base.ec);

   if (evhp->tdm.prop_list)
     {
        EINA_LIST_FREE(evhp->tdm.prop_list, tdm_prop)
           free(tdm_prop);
     }
   if (evhp->tdm.late_prop_list)
     {
        EINA_LIST_FREE(evhp->tdm.late_prop_list, tdm_prop)
           free(tdm_prop);
     }

   if (evhp->tdm.prop_list)
     NEVER_GET_HERE();
   if (evhp->tdm.late_prop_list)
     NEVER_GET_HERE();

   if (evhp->tdm.layer)
     {
        VIN("unset layer: destroy", evhp->base.ec);
        _e_video_hwc_planes_tdm_layer_unset(evhp);
     }

   free(evhp);
}

static void
_e_video_hwc_planes_ec_event_deinit(E_Video_Hwc_Planes *evhp)
{
   E_Client *ec;

   ec = evhp->base.ec;

   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_HIDE,
                                       _e_video_hwc_planes_cb_evas_hide, evhp);
}

const char *
_e_video_hwc_planes_prop_name_get_by_id(E_Video_Hwc_Planes *evhp, unsigned int id)
{
   tdm_layer *layer;
   const tdm_prop *props;
   int i, count = 0;

   layer = _tdm_output_video_layer_get(evhp->tdm.output);
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

   EINA_LIST_FOREACH(evhp->tdm.late_prop_list, l, prop)
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
   evhp->tdm.late_prop_list = eina_list_append(evhp->tdm.late_prop_list, prop);

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

   EINA_LIST_FOREACH(evhp->tdm.prop_list, l, prop)
     {
        if (!strncmp(name, prop->name, TDM_NAME_LEN))
          {
             VDB("find prop data(%s) update value(%d -> %d)", evhp->base.ec,
                 prop->name, (unsigned int)prop->value.u32, (unsigned int)value.u32);
             prop->value.u32 = value.u32;
             return EINA_TRUE;
          }
     }
   EINA_LIST_FOREACH(evhp->tdm.late_prop_list, l, prop)
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
   evhp->tdm.prop_list = eina_list_append(evhp->tdm.prop_list, prop);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_property_save(E_Video_Hwc_Planes *evhp, unsigned int id, const char *name, tdm_value value)
{
   return _e_video_hwc_planes_property_pre_set(evhp, id, name, value);
}

static void
_e_video_hwc_planes_ec_event_init(E_Video_Hwc_Planes *evhp, E_Client *ec)
{
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,
                                  _e_video_hwc_planes_cb_evas_hide, evhp);
}

static void
_e_video_hwc_planes_iface_destroy(E_Video_Hwc *evh)
{
   E_Video_Hwc_Planes *evhp;

   evhp = (E_Video_Hwc_Planes *)evh;
   _e_video_hwc_planes_ec_event_deinit(evhp);
   _e_video_hwc_planes_destroy(evhp);
}

static Eina_Bool
_e_video_hwc_planes_iface_property_get(E_Video_Hwc *evh, unsigned int id, tdm_value *value)
{
   E_Video_Hwc_Planes *evhp;
   tdm_error ret;

   evhp = (E_Video_Hwc_Planes *)evh;
   ret = _tdm_layer_property_get(evhp->tdm.layer, id, value);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_property_set(E_Video_Hwc *evh, unsigned int id, tdm_value value)
{
   E_Video_Hwc_Planes *evhp;
   Tdm_Prop_Value prop;
   const char *name;

   evhp = (E_Video_Hwc_Planes *)evh;
   VIN("set layer: set_attribute", evhp->base.ec);

   name = _e_video_hwc_planes_prop_name_get_by_id(evhp, id);

   if (!evhp->tdm.layer)
     {
        /* FIXME
         * Set property with assigning layer right away if allowed_attribute
         * flag is set. The reason why we have to do like this isn't figured
         * yet. It's for backward compatibility. */
        if (e_client_video_property_allow_get(evhp->base.ecv))
          {
             if (!_e_video_hwc_planes_tdm_layer_set(evhp))
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
   _tdm_layer_property_set(evhp->tdm.layer, &prop);

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_available_properties_get(E_Video_Hwc *evh, const tdm_prop **props, int *count)
{
   E_Video_Hwc_Planes *evhp;
   tdm_error ret;

   evhp = (E_Video_Hwc_Planes *)evh;
   ret = _e_video_hwc_planes_available_properties_get(evhp, props, count);
   if (ret != TDM_ERROR_NONE)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_video_hwc_planes_iface_buffer_commit(E_Video_Hwc *evh, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Video_Hwc_Planes *evhp;
   Eina_Bool ret = EINA_TRUE;

   evhp = (E_Video_Hwc_Planes *)evh;
   if (!vbuf)
     {
        if (evhp->tdm.layer)
          {
             VIN("unset layer: hide", evhp->base.ec);
             _e_video_hwc_planes_tdm_layer_unset(evhp);
          }
     }
   else
     ret = _e_video_hwc_planes_buffer_commit(evhp, vbuf);

   return ret;
}

static Eina_Bool
_e_video_hwc_planes_iface_check_if_pp_needed(E_Video_Hwc *evh)
{
   E_Video_Hwc_Planes *evhp;
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   tdm_layer_capability capabilities = 0;

   evhp = (E_Video_Hwc_Planes *)evh;

   tdm_layer *layer = _tdm_output_video_layer_get(evhp->tdm.output);

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

static tbm_surface_h
_e_video_hwc_planes_iface_displaying_buffer_get(E_Video_Hwc *evh)
{
   E_Video_Hwc_Planes *evhp;

   evhp = (E_Video_Hwc_Planes *)evh;
   return _tdm_layer_displaying_buffer_get(evhp->tdm.layer, NULL);
}

static void
_e_video_hwc_planes_iface_set(E_Video_Hwc_Iface *iface)
{
   iface->destroy = _e_video_hwc_planes_iface_destroy;
   iface->property_get = _e_video_hwc_planes_iface_property_get;
   iface->property_set = _e_video_hwc_planes_iface_property_set;
   iface->available_properties_get = _e_video_hwc_planes_iface_available_properties_get;
   iface->buffer_commit = _e_video_hwc_planes_iface_buffer_commit;
   iface->check_if_pp_needed = _e_video_hwc_planes_iface_check_if_pp_needed;
   iface->displaying_buffer_get = _e_video_hwc_planes_iface_displaying_buffer_get;
}

EINTERN E_Video_Hwc *
e_video_hwc_planes_create(E_Output *output, E_Client *ec)
{
   E_Video_Hwc_Planes *evhp;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);

   VIN("Create HWC Planes backend", ec);

   evhp = E_NEW(E_Video_Hwc_Planes, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(evhp, NULL);

   evhp->base.e_output = output;
   evhp->tdm.output = output->toutput;

   if (!_e_video_hwc_planes_init(evhp, output))
     {
        ERR("Failed to init 'E_Video_Hwc_Planes'");
        free(evhp);
        return NULL;
     }

   _e_video_hwc_planes_ec_event_init(evhp, ec);
   _e_video_hwc_planes_iface_set(&evhp->base.backend);

   return (E_Video_Hwc *)evhp;
}

EINTERN Eina_Bool
e_video_hwc_planes_properties_commit(E_Video_Hwc *evh)
{
   E_Video_Hwc_Planes *evhp;

   evhp = (E_Video_Hwc_Planes *)evh;

   /* FIXME: Is it really necessary? */
   if (evhp->tdm.layer)
     return EINA_TRUE;

   VIN("set layer: show", evhp->base.ec);
   if (!_e_video_hwc_planes_tdm_layer_set(evhp))
     {
        VER("set layer failed", evhp->base.ec);
        return EINA_FALSE;
     }

   _tdm_layer_property_list_set(evhp->tdm.layer, evhp->tdm.prop_list);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_video_hwc_planes_property_delay_set(E_Video_Hwc *evh, unsigned int id, tdm_value value)
{
   E_Video_Hwc_Planes *evhp;
   const char *name;

   evhp = (E_Video_Hwc_Planes *)evh;
   name = _e_video_hwc_planes_prop_name_get_by_id(evhp, id);

   _e_video_hwc_planes_property_post_set(evhp, id, name, value);

   return EINA_TRUE;
}
