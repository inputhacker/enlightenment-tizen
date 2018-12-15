#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <tdm.h>
#include <values.h>
#include <tdm_helper.h>
#include <wayland-tbm-server.h>
#include <tizen-extension-server-protocol.h>

//#define DUMP_BUFFER
#define CHECKING_PRIMARY_ZPOS

static int _video_detail_log_dom = -1;
static Eina_Bool video_to_primary;
static Eina_Bool video_punch;

#define BUFFER_MAX_COUNT   5
#define MIN_WIDTH   32

#undef NEVER_GET_HERE
#define NEVER_GET_HERE()     CRI("** need to improve more **")

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof(x))
#endif

#define VER(fmt, arg...) ELOGF("VIDEO", "<ERR> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VWR(fmt, arg...) ELOGF("VIDEO", "<WRN> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VIN(fmt, arg...) ELOGF("VIDEO", "<INF> window(0x%08"PRIxPTR"): "fmt, \
                               video->ec->pixmap, video->ec, video->window, ##arg)
#define VDB(fmt, arg...) DBG("window(0x%08"PRIxPTR") ec(%p): "fmt, video->window, video->ec, ##arg)

#define DET(...)          EINA_LOG_DOM_DBG(_video_detail_log_dom, __VA_ARGS__)
#define VDT(fmt, arg...)   DET("window(0x%08"PRIxPTR"): "fmt, video->window, ##arg)

#define GEO_FMT   "%dx%d(%dx%d+%d+%d) -> (%dx%d+%d+%d) transform(%d)"
#define GEO_ARG(g) \
   (g)->input_w, (g)->input_h, \
   (g)->input_r.w, (g)->input_r.h, (g)->input_r.x, (g)->input_r.y, \
   (g)->output_r.w, (g)->output_r.h, (g)->output_r.x, (g)->output_r.y, \
   (g)->transform

struct _E_Video_Info_Layer
{
   tdm_info_config src_config;
   tdm_pos dst_pos;
   tdm_transform transform;
};

/* the new TDM API doesn't have layers, so we have to invent layer here*/
struct _E_Video_Layer
{
   E_Video *video;

   tdm_layer *tdm_layer;

   /* for hwc_window */
   E_Video_Info_Layer info;
   tbm_surface_h cur_tsurface; // tsurface to be set this layer.
   E_Client *e_client;
};

struct _E_Video
{
   struct wl_resource *video_object;
   struct wl_resource *surface;
   E_Client *ec;
   Ecore_Window window;
   tdm_output *output;
   E_Output *e_output;
   E_Video_Layer *layer;
   E_Plane *e_plane;
   Eina_Bool external_video;

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

   int output_align;
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

static tbm_format sw_formats[] = {
	TBM_FORMAT_ARGB8888,
	TBM_FORMAT_XRGB8888,
	TBM_FORMAT_YUV420,
	TBM_FORMAT_YVU420,
};

#define NUM_SW_FORMAT   (sizeof(sw_formats) / sizeof(sw_formats[0]))

static Eina_List *video_list = NULL;
static Eina_List *video_layers = NULL;

static void _e_video_set(E_Video *video, E_Client *ec);
static void _e_video_destroy(E_Video *video);
static void _e_video_render(E_Video *video, const char *func);
static Eina_Bool _e_video_frame_buffer_show(E_Video *video, E_Comp_Wl_Video_Buf *vbuf);
static void _e_video_video_set_hook(void *data, E_Plane *plane);

static tdm_layer* _e_video_tdm_video_layer_get(tdm_output *output);
static tdm_layer* _e_video_tdm_available_video_layer_get(tdm_output *output);
static void _e_video_tdm_set_layer_usable(tdm_layer *layer, Eina_Bool usable);
static Eina_Bool _e_video_tdm_get_layer_usable(tdm_layer *layer);

static void _e_video_vblank_handler(tdm_output *output, unsigned int sequence, unsigned int tv_sec, unsigned int tv_usec, void *user_data);

static int
gcd(int a, int b)
{
   if (a % b == 0)
     return b;
   return gcd(b, a % b);
}

static int
lcm(int a, int b)
{
   return a * b / gcd(a, b);
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

static E_Video *
find_video_with_surface(struct wl_resource *surface)
{
   E_Video *video;
   Eina_List *l;
   EINA_LIST_FOREACH(video_list, l, video)
     {
        if (video->surface == surface)
          return video;
     }
   return NULL;
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

   if (e_hwc_policy_get(output->hwc) != E_HWC_POLICY_WINDOWS)
     {
        /* get the first suitable layer */
        layer = _e_video_tdm_video_layer_get(toutput);
        if (!layer)
          return EINA_FALSE;

        tdm_layer_get_capabilities(layer, &lyr_capabilities);
        if (lyr_capabilities & TDM_LAYER_CAPABILITY_VIDEO)
          return EINA_TRUE;
     }
   else
     {
        /* TODO: add the hwc_window video support */
        ;;;;
     }

   return EINA_FALSE;
}

E_Video_Layer *
_e_video_available_video_layer_get(E_Video *video)
{
   E_Video_Layer *layer = NULL;
   E_Hwc_Window *hwc_window = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(video, NULL);

   layer = calloc(1, sizeof(E_Video_Layer));
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

   layer->video = video;
   layer->e_client = video->ec;

   if (e_hwc_policy_get(video->e_output->hwc) != E_HWC_POLICY_WINDOWS)
     {
        layer->tdm_layer = _e_video_tdm_available_video_layer_get(video->output);
        if (!layer->tdm_layer)
          {
             free(layer);
             return NULL;
          }
        _e_video_tdm_set_layer_usable(layer->tdm_layer, EINA_FALSE);
     }
   else
     {
       /*
        * Try to create a video hwc window.
        * In this moment the video resource will be held.
        */
        hwc_window = e_hwc_window_new(video->e_output->hwc, video->ec, E_HWC_WINDOW_STATE_VIDEO);
        if (!hwc_window)
          {
             VER("hwc_opt: cannot create new video hwc window for ec(%p)", video->ec);
             free(layer);
             return NULL;
          }

        /* free previous hwc_window */
        if (video->ec->hwc_window)
           e_hwc_window_free(video->ec->hwc_window);

        /* set new hwc_window to the e_client */
        video->ec->hwc_window = hwc_window;
     }

   return layer;
}

/* this function is called on the start work with client while the video interface is bind */
static void
_e_video_get_available_formats(const tbm_format **formats, int *count)
{
   E_Output *output;
   tdm_output *toutput;
   tdm_layer *layer;

   *count = 0;

   /* get the first output */
   toutput = tdm_display_get_output(e_comp->e_comp_screen->tdisplay, 0, NULL);
   EINA_SAFETY_ON_NULL_RETURN(toutput);

   output = _get_e_output(toutput);
   EINA_SAFETY_ON_NULL_RETURN(output);

   if (e_hwc_policy_get(output->hwc) != E_HWC_POLICY_WINDOWS)
     {
        /* get the first suitable layer */
        layer = _e_video_tdm_video_layer_get(toutput);
        if (layer)
          {
             tdm_layer_get_available_formats(layer, formats, count);
          }
        else
          {
             *formats = sw_formats;
             *count = NUM_SW_FORMAT;
          }
     }
   else
     {
        tdm_error error;
        error = tdm_hwc_get_video_supported_formats(output->hwc->thwc, formats, count);
        if (error != TDM_ERROR_NONE)
          {
             *formats = sw_formats;
             *count = NUM_SW_FORMAT;
          }
     }
}

static int
_e_video_get_prop_id(E_Video *video, const char *name)
{
   tdm_layer *layer;
   const tdm_prop *props;
   int i, count = 0;

   if (e_hwc_policy_get(video->e_output->hwc) != E_HWC_POLICY_WINDOWS)
     {
        layer = _e_video_tdm_video_layer_get(video->output);
        tdm_layer_get_available_properties(layer, &props, &count);

        for (i = 0; i < count; i++)
          {
             if (!strncmp(name, props[i].name, TDM_NAME_LEN))
               {
                  VDB("check property(%s)", name);
                  return props[i].id;
               }
          }
     }
   else
     {
       // TODO: hwc windows don't have any properties yet
       //       video hwc_window has to get and set the property id.
       // tdm_error tdm_hwc_window_video_get_property(tdm_hwc_window *hwc_window, uint32_t id, tdm_value *value);
       ;;;;;
     }

   return -1;
}

// TODO: this function has to be removed.....
//       Use. e_hwc_policy_get();
static Eina_Bool
_is_video_hwc_windows(E_Video *video)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(video, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(video->e_output, EINA_FALSE);

   if (e_hwc_policy_get(video->e_output->hwc) == E_HWC_POLICY_WINDOWS)
     return EINA_TRUE;

   return EINA_FALSE;
}

static tdm_error
_e_video_layer_get_info(E_Video_Layer *layer, E_Video_Info_Layer *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer tinfo = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   if (_is_video_hwc_windows(layer->video))
        memcpy(vinfo, &layer->info, sizeof(E_Video_Info_Layer));
   else
     {
        ret = tdm_layer_get_info(layer->tdm_layer, &tinfo);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

        memcpy(&vinfo->src_config, &tinfo.src_config, sizeof(tdm_info_config));
        memcpy(&vinfo->dst_pos, &tinfo.dst_pos, sizeof(tdm_pos));
        vinfo->transform = tinfo.transform;
     }

   return ret;
}

static tdm_error
_e_video_layer_set_info(E_Video_Layer *layer, E_Video_Info_Layer *vinfo)
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_info_layer info_layer = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_INVALID_PARAMETER);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, TDM_ERROR_INVALID_PARAMETER);

   if (_is_video_hwc_windows(layer->video))
     memcpy(&layer->info, vinfo, sizeof(E_Video_Info_Layer));
   else
     {

        memcpy(&info_layer.src_config, &vinfo->src_config, sizeof(tdm_info_config));
        memcpy(&info_layer.dst_pos, &vinfo->dst_pos, sizeof(tdm_pos));
        info_layer.transform = vinfo->transform;

        ret = tdm_layer_set_info(layer->tdm_layer, &info_layer);
     }

   return ret;
}

static tdm_error
_e_video_layer_set_buffer(E_Video_Layer * layer, tbm_surface_h buff)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
   EINA_SAFETY_ON_NULL_RETURN_VAL(buff, TDM_ERROR_BAD_REQUEST);

   if (_is_video_hwc_windows(layer->video))
     layer->cur_tsurface = buff; // set the buffer to the tdm at the e_hwc_window_buffer_update();
   else
     ret = tdm_layer_set_buffer(layer->tdm_layer, buff);

   return ret;
}

static tdm_error
_e_video_layer_unset_buffer(E_Video_Layer *layer)
{
   tdm_error ret;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   if (_is_video_hwc_windows(layer->video))
     {
        hwc_window = layer->e_client->hwc_window;
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, TDM_ERROR_OPERATION_FAILED);

        e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);
        layer->cur_tsurface = NULL; // set the buffer to the tdm at the e_hwc_window_buffer_update();

        ret = TDM_ERROR_NONE;
     }
   else
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

   if (_is_video_hwc_windows(layer->video))
     {
        E_Hwc_Window *hwc_window;
        E_Hwc_Window_State state;

        hwc_window = layer->e_client->hwc_window;
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, TDM_ERROR_OPERATION_FAILED);

        state = e_hwc_window_state_get(hwc_window);
        if (state == E_HWC_WINDOW_STATE_NONE || state == E_HWC_WINDOW_STATE_CLIENT)
          *usable = 1;
        else
          *usable = 0;

        return TDM_ERROR_NONE;
     }

   ret = tdm_layer_is_usable(layer->tdm_layer, usable);
   return ret;
}

static tdm_error
_e_video_layer_commit(E_Video_Layer *layer, tdm_layer_commit_handler func, void *user_data)
{
   tdm_error ret = TDM_ERROR_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   if (_is_video_hwc_windows(layer->video))
     ret = TDM_ERROR_NONE;
   else
     ret = tdm_layer_commit(layer->tdm_layer, func, user_data);

   return ret;
}

static tbm_surface_h
_e_video_layer_get_displaying_buffer(E_Video_Layer *layer, int *tdm_error)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, NULL);

   if (tdm_error)
     *tdm_error = TDM_ERROR_OPERATION_FAILED;

   if (_is_video_hwc_windows(layer->video))
     {
        E_Hwc_Window *hwc_window;

        hwc_window = layer->e_client->hwc_window;
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

        if (tdm_error)
          *tdm_error = TDM_ERROR_NONE;

        return e_hwc_window_displaying_surface_get(hwc_window);
     }

   return tdm_layer_get_displaying_buffer(layer->tdm_layer, tdm_error);
}

static tdm_error
_e_video_layer_get_available_properties(E_Video_Layer * layer, const tdm_prop **props,
    int *count)
{
  tdm_error ret = TDM_ERROR_OPERATION_FAILED;

  EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
  EINA_SAFETY_ON_NULL_RETURN_VAL(props, TDM_ERROR_BAD_REQUEST);
  EINA_SAFETY_ON_NULL_RETURN_VAL(count, TDM_ERROR_BAD_REQUEST);

  if (_is_video_hwc_windows(layer->video))
    {
       *count = 0;
#if 0 //TODO:
       if (layer->e_client->hwc_window->thwc_window)
         ret = tdm_hwc_window_video_get_available_properties(
                         layer->e_client->hwc_window->thwc_window, props, count);
#endif
    }
  else
    {
       tdm_layer *tlayer = layer->tdm_layer;
       /* if layer wasn't set then get an any available tdm_layer */
       if (tlayer == NULL)
         tlayer = _e_video_tdm_available_video_layer_get(layer->video->output);
       ret = tdm_layer_get_available_properties(tlayer, props, count);
    }

  return ret;
}

static tdm_error
_e_video_layer_get_property(E_Video_Layer * layer, unsigned id, tdm_value *value)
{
  tdm_error ret = TDM_ERROR_OPERATION_FAILED;

  EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);
  EINA_SAFETY_ON_NULL_RETURN_VAL(value, TDM_ERROR_BAD_REQUEST);

  if (_is_video_hwc_windows(layer->video))
    {
#if 0 //TODO:
       if (layer->e_client->hwc_window->thwc_window)
         ret = tdm_hwc_window_video_get_property(
                      layer->e_client->hwc_window->thwc_window, id, value);
       else
#endif
         ret = TDM_ERROR_BAD_MODULE;
    }
  else
    ret = tdm_layer_get_property(layer->tdm_layer, id, value);

  return ret;
}

static tdm_error
_e_video_layer_set_property(E_Video_Layer * layer, Tdm_Prop_Value *prop)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(layer, TDM_ERROR_BAD_REQUEST);

   if (_is_video_hwc_windows(layer->video))
     return TDM_ERROR_BAD_MODULE;

   ret = tdm_layer_set_property(layer->tdm_layer, prop->id, prop->value);
   return ret;
}

static void
_e_video_layer_destroy(E_Video_Layer *layer)
{
   EINA_SAFETY_ON_NULL_RETURN(layer);

   if (layer->e_client && layer->e_client->hwc_window)
     {
        E_Hwc_Window *hwc_window;

        hwc_window = layer->e_client->hwc_window;
        EINA_SAFETY_ON_FALSE_RETURN(hwc_window);

        e_hwc_window_free(hwc_window);
        layer->e_client->hwc_window = NULL;

        /* to re-evaluate the window policy */
        e_comp_render_queue();
     }

   if (layer->tdm_layer)
     _e_video_tdm_set_layer_usable(layer->tdm_layer, EINA_TRUE);

   free(layer);
}

static Eina_Bool
_e_video_set_layer(E_Video *video, Eina_Bool set)
{
   Eina_Bool need_wait;

   if (!set)
     {
        unsigned int usable = 1;
        if (!video->layer) return EINA_TRUE;

        _e_video_layer_is_usable(video->layer, &usable);
        if (!usable && !video->video_plane_ready_handler)
          {
             VIN("stop video");
             _e_video_layer_unset_buffer(video->layer);
             _e_video_layer_commit(video->layer, NULL, NULL);
          }

        VIN("release layer: %p", video->layer);
        _e_video_layer_destroy(video->layer);
        video->layer = NULL;
        video->old_comp_buffer = NULL;

        if (e_hwc_policy_get(video->e_output->hwc) != E_HWC_POLICY_WINDOWS)
          {
             e_plane_video_set(video->e_plane, EINA_FALSE, NULL);
             video->e_plane = NULL;
          }

        E_FREE_FUNC(video->video_plane_ready_handler, e_plane_hook_del);
     }
   else
     {
        int zpos;
        tdm_error ret;

        if (video->layer) return EINA_TRUE;

        video->layer = _e_video_available_video_layer_get(video);
        if (!video->layer)
          {
             VWR("no available layer for video");
             return EINA_FALSE;
          }

        if (e_hwc_policy_get(video->e_output->hwc) != E_HWC_POLICY_WINDOWS)
          {
             ret = tdm_layer_get_zpos(video->layer->tdm_layer, &zpos);
             if (ret == TDM_ERROR_NONE)
               video->e_plane = e_output_plane_get_by_zpos(video->e_output, zpos);

             if (!video->e_plane)
               {
                  VWR("fail get e_plane");
                  _e_video_layer_destroy(video->layer);
                  video->layer = NULL;
                  return EINA_FALSE;
               }

             if (!e_plane_video_set(video->e_plane, EINA_TRUE, &need_wait))
               {
                  VWR("fail set video to e_plane");
                  _e_video_layer_destroy(video->layer);
                  video->layer = NULL;
                  video->e_plane = NULL;
                  return EINA_FALSE;
               }
             if (need_wait)
               {
                    video->video_plane_ready_handler =
                       e_plane_hook_add(E_PLANE_HOOK_VIDEO_SET,
                                        _e_video_video_set_hook, video);
               }
          }

        VIN("assign layer: %p", video->layer);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_is_visible(E_Video *video)
{
   E_Client *offscreen_parent;

   if (e_object_is_del(E_OBJECT(video->ec))) return EINA_FALSE;

   if (!e_pixmap_resource_get(video->ec->pixmap))
     {
        VDB("no comp buffer");
        return EINA_FALSE;
     }

   if (video->ec->comp_data->sub.data && video->ec->comp_data->sub.data->stand_alone)
     return EINA_TRUE;

   offscreen_parent = find_offscreen_parent_get(video->ec);
   if (offscreen_parent && offscreen_parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     {
        VDB("video surface invisible: offscreen fully obscured");
        return EINA_FALSE;
     }

   if (!evas_object_visible_get(video->ec->frame))
     {
        VDB("evas obj invisible");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_video_parent_is_viewable(E_Video *video)
{
   E_Client *topmost_parent;

   if (e_object_is_del(E_OBJECT(video->ec))) return EINA_FALSE;

   topmost_parent = e_comp_wl_topmost_parent_get(video->ec);

   if (!topmost_parent)
     return EINA_FALSE;

   if (topmost_parent == video->ec)
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
   E_Video *video = data;
   Eina_Bool need_hide = EINA_FALSE;

   VDT("Buffer(%p) to be free, refcnt(%d)", vbuf, vbuf->ref_cnt);

   video->input_buffer_list = eina_list_remove(video->input_buffer_list, vbuf);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, NULL);

   if (video->current_fb == vbuf)
     {
        VIN("current fb destroyed");
        e_comp_wl_video_buffer_set_use(video->current_fb, EINA_FALSE);
        video->current_fb = NULL;
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(video->committed_list, vbuf))
     {
        VIN("committed fb destroyed");
        video->committed_list = eina_list_remove(video->committed_list, vbuf);
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        need_hide = EINA_TRUE;
     }

   if (eina_list_data_find(video->waiting_list, vbuf))
     {
        VIN("waiting fb destroyed");
        video->waiting_list = eina_list_remove(video->waiting_list, vbuf);
     }

   if (need_hide && video->layer)
     _e_video_frame_buffer_show(video, NULL);
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
_e_video_input_buffer_copy(E_Video *video, E_Comp_Wl_Buffer *comp_buf, E_Comp_Wl_Video_Buf *vbuf, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *temp = NULL;
   int aligned_width = ROUNDUP(vbuf->width_from_pitch, video->pp_align);

   temp = e_comp_wl_video_buffer_alloc(aligned_width, vbuf->height, vbuf->tbmfmt, scanout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(temp, NULL);

   temp->comp_buffer = comp_buf;

   VDB("copy vbuf(%d,%dx%d) => vbuf(%d,%dx%d)",
       MSTAMP(vbuf), vbuf->width_from_pitch, vbuf->height,
       MSTAMP(temp), temp->width_from_pitch, temp->height);

   e_comp_wl_video_buffer_copy(vbuf, temp);
   e_comp_wl_video_buffer_unref(vbuf);

   video->geo.input_w = vbuf->width_from_pitch;
#ifdef DUMP_BUFFER
   char file[256];
   static int i;
   snprintf(file, sizeof file, "/tmp/dump/%s_%d.png", "cpy", i++);
   tdm_helper_dump_buffer(temp->tbm_surface, file);
#endif

   return temp;
}

static E_Comp_Wl_Video_Buf *
_e_video_input_buffer_get(E_Video *video, E_Comp_Wl_Buffer *comp_buffer, Eina_Bool scanout)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_Bool need_pp_scanout = EINA_FALSE;

   vbuf = _e_video_vbuf_find_with_comp_buffer(video->input_buffer_list, comp_buffer);
   if (vbuf)
     {
        vbuf->content_r = video->geo.input_r;
        return vbuf;
     }

   vbuf = e_comp_wl_video_buffer_create_comp(comp_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   if (video->pp_scanout)
     {
        Eina_Bool input_buffer_scanout = EINA_FALSE;
        input_buffer_scanout = _e_video_input_buffer_scanout_check(vbuf);
        if (!input_buffer_scanout) need_pp_scanout = EINA_TRUE;
     }

   if (video->pp)
     {
        if ((video->pp_align != -1 && (vbuf->width_from_pitch % video->pp_align)) ||
            need_pp_scanout)
          {
             E_Comp_Wl_Video_Buf *temp;

             if (need_pp_scanout)
               temp = _e_video_input_buffer_copy(video, comp_buffer, vbuf, EINA_TRUE);
             else
               temp = _e_video_input_buffer_copy(video, comp_buffer, vbuf, scanout);
             if (!temp)
               {
                  e_comp_wl_video_buffer_unref(vbuf);
                  return NULL;
               }
             vbuf = temp;
          }
     }

   vbuf->content_r = video->geo.input_r;

   video->input_buffer_list = eina_list_append(video->input_buffer_list, vbuf);
   e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_input_buffer_cb_free, video);

   VDT("Client(%s):PID(%d) RscID(%d), Buffer(%p) created, refcnt:%d"
       " scanout=%d", e_client_util_name_get(video->ec) ?: "No Name" ,
       video->ec->netwm.pid, wl_resource_get_id(video->surface), vbuf,
       vbuf->ref_cnt, scanout);

   return vbuf;
}

static void
_e_video_input_buffer_valid(E_Video *video, E_Comp_Wl_Buffer *comp_buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(video->input_buffer_list, l, vbuf)
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
   E_Video *video = data;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   if (video->current_fb == vbuf)
     video->current_fb = NULL;

   video->committed_list = eina_list_remove(video->committed_list, vbuf);

   video->waiting_list = eina_list_remove(video->waiting_list, vbuf);

   video->pp_buffer_list = eina_list_remove(video->pp_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf *
_e_video_pp_buffer_get(E_Video *video, int width, int height)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;
   int i = 0;
   int aligned_width;

   if (video->video_align != -1)
     aligned_width = ROUNDUP(width, video->video_align);
   else
     aligned_width = width;

   if (video->pp_buffer_list)
     {
        vbuf = eina_list_data_get(video->pp_buffer_list);
        EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

        /* if we need bigger pp_buffers, destroy all pp_buffers and create */
        if (aligned_width > vbuf->width_from_pitch || height != vbuf->height)
          {
             Eina_List *ll;

             VIN("pp buffer changed: %dx%d => %dx%d",
                 vbuf->width_from_pitch, vbuf->height,
                 aligned_width, height);

             EINA_LIST_FOREACH_SAFE(video->pp_buffer_list, l, ll, vbuf)
               {
                  /* free forcely */
                  e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
                  e_comp_wl_video_buffer_unref(vbuf);
               }
             if (video->pp_buffer_list)
               NEVER_GET_HERE();

             if (video->waiting_list)
               NEVER_GET_HERE();
          }
     }

   if (!video->pp_buffer_list)
     {
        for (i = 0; i < BUFFER_MAX_COUNT; i++)
          {
             vbuf = e_comp_wl_video_buffer_alloc(aligned_width, height, video->pp_tbmfmt, EINA_TRUE);
             EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

             e_comp_wl_video_buffer_free_func_add(vbuf, _e_video_pp_buffer_cb_free, video);
             video->pp_buffer_list = eina_list_append(video->pp_buffer_list, vbuf);

          }

        VIN("pp buffer created: %dx%d, %c%c%c%c",
            vbuf->width_from_pitch, height, FOURCC_STR(video->pp_tbmfmt));

        video->next_buffer = video->pp_buffer_list;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(video->pp_buffer_list, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(video->next_buffer, NULL);

   l = video->next_buffer;
   while ((vbuf = video->next_buffer->data))
     {
        video->next_buffer = (video->next_buffer->next) ? video->next_buffer->next : video->pp_buffer_list;

        if (!vbuf->in_use)
          return vbuf;

        if (l == video->next_buffer)
          {
             VWR("all video framebuffers in use (max:%d)", BUFFER_MAX_COUNT);
             return NULL;
          }
     }

   return NULL;
}

/* convert from logical screen to physical output */
static void
_e_video_geometry_cal_physical(E_Video *video)
{
   E_Zone *zone;
   E_Comp_Wl_Output *output;
   E_Client *topmost;
   int tran, flip;
   int transform;

   topmost = e_comp_wl_topmost_parent_get(video->ec);
   EINA_SAFETY_ON_NULL_GOTO(topmost, normal);

   output = e_comp_wl_output_find(topmost);
   EINA_SAFETY_ON_NULL_GOTO(output, normal);

   zone = e_comp_zone_xy_get(topmost->x, topmost->y);
   EINA_SAFETY_ON_NULL_GOTO(zone, normal);

   tran = video->geo.transform & 0x3;
   flip = video->geo.transform & 0x4;
   transform = flip + (tran + output->transform) % 4;
   switch(transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
         video->geo.tdm_transform = TDM_TRANSFORM_270;
         break;
      case WL_OUTPUT_TRANSFORM_180:
         video->geo.tdm_transform = TDM_TRANSFORM_180;
         break;
      case WL_OUTPUT_TRANSFORM_270:
         video->geo.tdm_transform = TDM_TRANSFORM_90;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:
         video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
         video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180:
         video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
         break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
         video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
         break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
         video->geo.tdm_transform = TDM_TRANSFORM_NORMAL;
         break;
     }

   if (output->transform % 2)
     {
        if (video->geo.tdm_transform == TDM_TRANSFORM_FLIPPED)
          video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_180;
        else if (video->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_90)
          video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_270;
        else if (video->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_180)
          video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED;
        else if (video->geo.tdm_transform == TDM_TRANSFORM_FLIPPED_270)
          video->geo.tdm_transform = TDM_TRANSFORM_FLIPPED_90;
     }

   if (output->transform == 0)
     video->geo.tdm_output_r = video->geo.output_r;
   else
     e_comp_wl_rect_convert(zone->w, zone->h, output->transform, 1,
                            video->geo.output_r.x, video->geo.output_r.y,
                            video->geo.output_r.w, video->geo.output_r.h,
                            &video->geo.tdm_output_r.x, &video->geo.tdm_output_r.y,
                            &video->geo.tdm_output_r.w, &video->geo.tdm_output_r.h);

   VDB("geomtry: screen(%d,%d %dx%d | %d) => %d => physical(%d,%d %dx%d | %d)",
       EINA_RECTANGLE_ARGS(&video->geo.output_r), video->geo.transform, transform,
       EINA_RECTANGLE_ARGS(&video->geo.tdm_output_r), video->geo.tdm_transform);

   return;
normal:
   video->geo.tdm_output_r = video->geo.output_r;
   video->geo.tdm_transform = video->geo.transform;
}

static Eina_Bool
_e_video_geometry_cal_viewport(E_Video *video)
{
   E_Client *ec = video->ec;
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

   comp_buffer = e_pixmap_resource_get(video->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(comp_buffer, EINA_FALSE);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surf, EINA_FALSE);

   tbm_surface_internal_get_plane_data(tbm_surf, 0, &size, &offset, &pitch);

   /* input geometry */
   if (IS_RGB(video->tbmfmt))
     video->geo.input_w = pitch / 4;
   else
     video->geo.input_w = pitch;

   video->geo.input_h = tbm_surface_get_height(tbm_surf);

   bw = tbm_surface_get_width(tbm_surf);
   bh = tbm_surface_get_height(tbm_surf);

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

#if 0
   VDB("transform(%d) scale(%d) buffer(%dx%d) src(%d,%d %d,%d) viewport(%dx%d)",
       vp->buffer.transform, vp->buffer.scale,
       width_from_buffer, height_from_buffer,
       x1, y1, x2 - x1, y2 - y1,
       ec->comp_data->width_from_viewport, ec->comp_data->height_from_viewport);
#endif

   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x1, y1, &tx1, &ty1);
   buffer_transform(width_from_buffer, height_from_buffer,
                    vp->buffer.transform, vp->buffer.scale, x2, y2, &tx2, &ty2);

   video->geo.input_r.x = (tx1 <= tx2) ? tx1 : tx2;
   video->geo.input_r.y = (ty1 <= ty2) ? ty1 : ty2;
   video->geo.input_r.w = (tx1 <= tx2) ? tx2 - tx1 : tx1 - tx2;
   video->geo.input_r.h = (ty1 <= ty2) ? ty2 - ty1 : ty1 - ty2;

   /* output geometry */
   if ((sdata = ec->comp_data->sub.data))
     {
        if (sdata->parent)
          {
             video->geo.output_r.x = sdata->parent->x + sdata->position.x;
             video->geo.output_r.y = sdata->parent->y + sdata->position.y;
          }
        else
          {
             video->geo.output_r.x = sdata->position.x;
             video->geo.output_r.y = sdata->position.y;
          }
     }
   else
     {
        video->geo.output_r.x = ec->x;
        video->geo.output_r.y = ec->y;
     }

   video->geo.output_r.w = ec->comp_data->width_from_viewport;
   video->geo.output_r.w = (video->geo.output_r.w + 1) & ~1;
   video->geo.output_r.h = ec->comp_data->height_from_viewport;

   e_comp_object_frame_xy_unadjust(ec->frame,
                                   video->geo.output_r.x, video->geo.output_r.y,
                                   &video->geo.output_r.x, &video->geo.output_r.y);
   e_comp_object_frame_wh_unadjust(ec->frame,
                                   video->geo.output_r.w, video->geo.output_r.h,
                                   &video->geo.output_r.w, &video->geo.output_r.h);

   video->geo.transform = vp->buffer.transform;

   _e_video_geometry_cal_physical(video);

#if 0
   VDB("geometry(%dx%d  %d,%d %dx%d  %d,%d %dx%d  %d)",
       video->geo.input_w, video->geo.input_h,
       EINA_RECTANGLE_ARGS(&video->geo.input_r),
       EINA_RECTANGLE_ARGS(&video->geo.output_r),
       video->geo.transform);
#endif

   return EINA_TRUE;
}

static Eina_Bool
_e_video_geometry_cal_map(E_Video *video)
{
   E_Client *ec;
   const Evas_Map *m;
   Evas_Coord x1, x2, y1, y2;
   Eina_Rectangle output_r;

   EINA_SAFETY_ON_NULL_RETURN_VAL(video, EINA_FALSE);

   ec = video->ec;
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

   if (!memcmp(&video->geo.output_r, &output_r, sizeof(Eina_Rectangle)))
     return EINA_FALSE;

   VDB("frame(%p) m(%p) output(%d,%d %dx%d) => (%d,%d %dx%d)", ec->frame, m,
       EINA_RECTANGLE_ARGS(&video->geo.output_r), EINA_RECTANGLE_ARGS(&output_r));

   video->geo.output_r = output_r;

   _e_video_geometry_cal_physical(video);

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
_e_video_geometry_cal_to_input_rect(E_Video * video, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int xf1, yf1, xf2, yf2;

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_video_geometry_cal_to_input(video->geo.output_r.w, video->geo.output_r.h,
                                  video->geo.input_r.w, video->geo.input_r.h,
                                  video->geo.transform, xf1, yf1, &xf1, &yf1);
   _e_video_geometry_cal_to_input(video->geo.output_r.w, video->geo.output_r.h,
                                  video->geo.input_r.w, video->geo.input_r.h,
                                  video->geo.transform, xf2, yf2, &xf2, &yf2);

   drect->x = MIN(xf1, xf2);
   drect->y = MIN(yf1, yf2);
   drect->w = MAX(xf1, xf2) - drect->x;
   drect->h = MAX(yf1, yf2) - drect->y;
}

static Eina_Bool
_e_video_geometry_cal(E_Video * video)
{
   Eina_Rectangle screen = {0,};
   Eina_Rectangle output_r = {0,}, input_r = {0,};
   const tdm_output_mode *mode = NULL;
   tdm_error tdm_err = TDM_ERROR_NONE;

   /* get geometry information with buffer scale, transform and viewport. */
   if (!_e_video_geometry_cal_viewport(video))
     return EINA_FALSE;

   _e_video_geometry_cal_map(video);

   if (e_config->eom_enable == EINA_TRUE && video->external_video)
     {
        tdm_err = tdm_output_get_mode(video->output, &mode);
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

        topmost = e_comp_wl_topmost_parent_get(video->ec);
        EINA_SAFETY_ON_NULL_RETURN_VAL(topmost, EINA_FALSE);

        zone = e_comp_zone_xy_get(topmost->x, topmost->y);
        EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);

        screen.w = zone->w;
        screen.h = zone->h;
     }

   e_comp_wl_video_buffer_size_get(video->ec, &input_r.w, &input_r.h);
   // when topmost is not mapped, input size can be abnormal.
   // in this case, it will be render by topmost showing.
   if (!eina_rectangle_intersection(&video->geo.input_r, &input_r) || (video->geo.input_r.w <= 10 || video->geo.input_r.h <= 10))
     {
        VER("input area is empty");
        return EINA_FALSE;
     }

   if (video->geo.output_r.x >= 0 && video->geo.output_r.y >= 0 &&
       (video->geo.output_r.x + video->geo.output_r.w) <= screen.w &&
       (video->geo.output_r.y + video->geo.output_r.h) <= screen.h)
     return EINA_TRUE;

   /* TODO: need to improve */

   output_r = video->geo.output_r;
   if (!eina_rectangle_intersection(&output_r, &screen))
     {
        VER("output_r(%d,%d %dx%d) screen(%d,%d %dx%d) => intersect(%d,%d %dx%d)",
            EINA_RECTANGLE_ARGS(&video->geo.output_r),
            EINA_RECTANGLE_ARGS(&screen), EINA_RECTANGLE_ARGS(&output_r));
        return EINA_TRUE;
     }

   output_r.x -= video->geo.output_r.x;
   output_r.y -= video->geo.output_r.y;

   if (output_r.w <= 0 || output_r.h <= 0)
     {
        VER("output area is empty");
        return EINA_FALSE;
     }

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   _e_video_geometry_cal_to_input_rect(video, &output_r, &input_r);

   VDB("output(%d,%d %dx%d) input(%d,%d %dx%d)",
       EINA_RECTANGLE_ARGS(&output_r), EINA_RECTANGLE_ARGS(&input_r));

   output_r.x += video->geo.output_r.x;
   output_r.y += video->geo.output_r.y;

   input_r.x += video->geo.input_r.x;
   input_r.y += video->geo.input_r.y;

   output_r.x = output_r.x & ~1;
   output_r.w = (output_r.w + 1) & ~1;

   input_r.x = input_r.x & ~1;
   input_r.w = (input_r.w + 1) & ~1;

   video->geo.output_r = output_r;
   video->geo.input_r = input_r;

   _e_video_geometry_cal_physical(video);

   return EINA_TRUE;
}

static void
_e_video_format_info_get(E_Video *video)
{
   E_Comp_Wl_Buffer *comp_buffer;
   tbm_surface_h tbm_surf;

   comp_buffer = e_pixmap_resource_get(video->ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(comp_buffer);

   tbm_surf = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, comp_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN(tbm_surf);

   video->tbmfmt = tbm_surface_get_format(tbm_surf);
}

static Eina_Bool
_e_video_can_commit(E_Video *video)
{
   if (e_config->eom_enable == EINA_TRUE)
     {
        if (!video->external_video && e_output_dpms_get(video->e_output))
          return EINA_FALSE;
     }
   else
     if (e_output_dpms_get(video->e_output))
       return EINA_FALSE;

   if (!_e_video_is_visible(video))
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_video_commit_handler(tdm_layer *layer, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video *video;
   Eina_List *l;
   E_Comp_Wl_Video_Buf *vbuf;

   EINA_LIST_FOREACH(video_list, l, video)
     {
        if (video == user_data) break;
     }

   if (!video) return;
   if (!video->committed_list) return;

   if (_e_video_can_commit(video))
     {
        tbm_surface_h displaying_buffer = _e_video_layer_get_displaying_buffer(video->layer, NULL);

        EINA_LIST_FOREACH(video->committed_list, l, vbuf)
          {
             if (vbuf->tbm_surface == displaying_buffer) break;
          }
        if (!vbuf) return;
     }
   else
     vbuf = eina_list_nth(video->committed_list, 0);

   video->committed_list = eina_list_remove(video->committed_list, vbuf);

   /* client can attachs the same wl_buffer twice. */
   if (video->current_fb && VBUF_IS_VALID(video->current_fb) && vbuf != video->current_fb)
     {
        e_comp_wl_video_buffer_set_use(video->current_fb, EINA_FALSE);

        if (video->current_fb->comp_buffer)
          e_comp_wl_buffer_reference(&video->current_fb->buffer_ref, NULL);
     }

   video->current_fb = vbuf;

   VDB("current_fb(%d)", MSTAMP(video->current_fb));
}

static void
_e_video_commit_buffer(E_Video *video, E_Comp_Wl_Video_Buf *vbuf)
{
   video->committed_list = eina_list_append(video->committed_list, vbuf);

   if (!_e_video_can_commit(video))
     goto no_commit;

   if (!_e_video_frame_buffer_show(video, vbuf))
     goto no_commit;

   return;

no_commit:
   _e_video_commit_handler(NULL, 0, 0, 0, video);
   _e_video_vblank_handler(NULL, 0, 0, 0, video);
}

static void
_e_video_commit_from_waiting_list(E_Video *video)
{
   E_Comp_Wl_Video_Buf *vbuf;

   vbuf = eina_list_nth(video->waiting_list, 0);
   video->waiting_list = eina_list_remove(video->waiting_list, vbuf);

   _e_video_commit_buffer(video, vbuf);
}

EINTERN void
e_comp_wl_video_hwc_window_commit_data_release(E_Hwc_Window *hwc_window, unsigned int sequence,
                            unsigned int tv_sec, unsigned int tv_usec)
{
   E_Client *ec = NULL;
   E_Video *video = NULL;
   Eina_List *l = NULL;
   E_Video_Layer *video_layer;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   EINA_LIST_FOREACH(video_list, l, video)
     if (video->ec == ec) break;
   EINA_SAFETY_ON_NULL_RETURN(video);

   video_layer = video->layer;
   EINA_SAFETY_ON_NULL_RETURN(video_layer);

   _e_video_commit_handler(NULL, sequence, tv_sec, tv_usec, video);
}

EINTERN tbm_surface_h
e_comp_wl_video_hwc_widow_surface_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;
   Eina_List *l = NULL;
   E_Video *video = NULL;
   E_Video_Layer *video_layer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   if (!e_hwc_window_is_video(hwc_window))
     {
       ERR("ehw:%p is NOT Video HWC window.", hwc_window);
       return NULL;
     }

   ec = hwc_window->ec;
   if (!ec) return NULL;

   EINA_LIST_FOREACH(video_list, l, video)
     if (video->ec == ec) break;
   EINA_SAFETY_ON_NULL_RETURN_VAL(video, NULL);

   video_layer = video->layer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(video_layer, NULL);

   return video_layer->cur_tsurface;
}

EINTERN Eina_Bool
e_comp_wl_video_hwc_window_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   E_Client *ec = NULL;
   Eina_List *l = NULL;
   E_Video *video = NULL;
   E_Video_Layer *video_layer;
   E_Video_Info_Layer *vinfo;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (!e_hwc_window_is_video(hwc_window))
     {
       ERR("ehw:%p is NOT Video HWC window.", hwc_window);
       return EINA_FALSE;
     }

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   EINA_LIST_FOREACH(video_list, l, video)
     if (video->ec == ec) break;
   EINA_SAFETY_ON_NULL_RETURN_VAL(video,EINA_FALSE);

   video_layer = video->layer;
   EINA_SAFETY_ON_NULL_RETURN_VAL(video_layer, EINA_FALSE);

   vinfo = &video_layer->info;
   EINA_SAFETY_ON_NULL_RETURN_VAL(vinfo, EINA_FALSE);

   memcpy(&hwc_win_info->src_config, &vinfo->src_config, sizeof(tdm_info_config));
   memcpy(&hwc_win_info->dst_pos, &vinfo->dst_pos, sizeof(tdm_pos));
   hwc_win_info->transform = vinfo->transform;

   return EINA_TRUE;
}

static void
_e_video_vblank_handler(tdm_output *output, unsigned int sequence,
                        unsigned int tv_sec, unsigned int tv_usec,
                        void *user_data)
{
   E_Video *video;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, video)
     {
        if (video == user_data) break;
     }

   if (!video) return;

   video->waiting_vblank = EINA_FALSE;

   if (video->video_plane_ready_handler) return;

   if (video->waiting_list)
     _e_video_commit_from_waiting_list(video);
}

static void
_e_video_video_set_hook(void *data, E_Plane *plane)
{
   E_Video *video = (E_Video *)data;

   if (video->e_plane != plane) return;
   if (video->waiting_vblank) return;

   if (video->waiting_list)
     _e_video_commit_from_waiting_list(video);

   E_FREE_FUNC(video->video_plane_ready_handler, e_plane_hook_del);
}

static Eina_Bool
_e_video_frame_buffer_show(E_Video *video, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Video_Info_Layer info, old_info;
   tdm_error ret;
   E_Client *topmost;
   Tdm_Prop_Value *prop;

   if (!vbuf)
     {
        if (video->layer)
          {
             VIN("unset layer: hide");
             _e_video_set_layer(video, EINA_FALSE);
          }
        return EINA_TRUE;
     }

   if (!video->layer)
     {
        VIN("set layer: show");
        if (!_e_video_set_layer(video, EINA_TRUE))
          {
             VER("set layer failed");
             return EINA_FALSE;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(video->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
             _e_video_layer_set_property(video->layer, prop);
             free(prop);
          }
     }

   CLEAR(old_info);
   ret = _e_video_layer_get_info(video->layer, &old_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   CLEAR(info);
   info.src_config.size.h = vbuf->width_from_pitch;
   info.src_config.size.v = vbuf->height_from_size;
   info.src_config.pos.x = vbuf->content_r.x;
   info.src_config.pos.y = vbuf->content_r.y;
   info.src_config.pos.w = vbuf->content_r.w;
   info.src_config.pos.h = vbuf->content_r.h;
   info.src_config.format = vbuf->tbmfmt;
   info.dst_pos.x = video->geo.tdm_output_r.x;
   info.dst_pos.y = video->geo.tdm_output_r.y;
   info.dst_pos.w = video->geo.tdm_output_r.w;
   info.dst_pos.h = video->geo.tdm_output_r.h;
   info.transform = vbuf->content_t;

   if (memcmp(&old_info, &info, sizeof(tdm_info_layer)))
     {
        ret = _e_video_layer_set_info(video->layer, &info);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);
     }

   ret = _e_video_layer_set_buffer(video->layer, vbuf->tbm_surface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = _e_video_layer_commit(video->layer, _e_video_commit_handler, video);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   ret = tdm_output_wait_vblank(video->output, 1, 0, _e_video_vblank_handler, video);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   video->waiting_vblank = EINA_TRUE;

   EINA_LIST_FREE(video->late_tdm_prop_list, prop)
     {
        VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
        _e_video_layer_set_property(video->layer, prop);
        free(prop);
     }

   topmost = e_comp_wl_topmost_parent_get(video->ec);
   if (topmost && topmost->argb && !e_comp_object_mask_has(video->ec->frame))
     {
        Eina_Bool do_punch = EINA_TRUE;

        /* FIXME: the mask obj can be drawn at the wrong position in the beginnig
         * time. It happens caused by window manager policy.
         */
        if ((topmost->fullscreen || topmost->maximized) &&
            (video->geo.output_r.x == 0 || video->geo.output_r.y == 0))
          {
             int bw, bh;

             e_pixmap_size_get(topmost->pixmap, &bw, &bh);

             if (bw > 100 && bh > 100 &&
                 video->geo.output_r.w < 100 && video->geo.output_r.h < 100)
               {
                  VIN("don't punch. (%dx%d, %dx%d)",
                      bw, bh, video->geo.output_r.w, video->geo.output_r.h);
                  do_punch = EINA_FALSE;
               }
          }

        if (do_punch)
          {
             e_comp_object_mask_set(video->ec->frame, EINA_TRUE);
             VIN("punched");
          }
     }

   if (video_punch)
     {
        e_comp_object_mask_set(video->ec->frame, EINA_TRUE);
        VIN("punched");
     }

   VDT("Client(%s):PID(%d) RscID(%d), Buffer(%p, refcnt:%d) is shown."
       "Geometry details are : buffer size(%dx%d) src(%d,%d, %dx%d)"
       " dst(%d,%d, %dx%d), transform(%d)",
       e_client_util_name_get(video->ec) ?: "No Name" , video->ec->netwm.pid,
       wl_resource_get_id(video->surface), vbuf, vbuf->ref_cnt,
       info.src_config.size.h, info.src_config.size.v, info.src_config.pos.x,
       info.src_config.pos.y, info.src_config.pos.w, info.src_config.pos.h,
       info.dst_pos.x, info.dst_pos.y, info.dst_pos.w, info.dst_pos.h, info.transform);


   return EINA_TRUE;
}

static void
_e_video_buffer_show(E_Video *video, E_Comp_Wl_Video_Buf *vbuf, unsigned int transform)
{
   vbuf->content_t = transform;

   e_comp_wl_video_buffer_set_use(vbuf, EINA_TRUE);

   if (vbuf->comp_buffer)
     e_comp_wl_buffer_reference(&vbuf->buffer_ref, vbuf->comp_buffer);

   if (video->waiting_vblank || video->video_plane_ready_handler)
     {
        video->waiting_list = eina_list_append(video->waiting_list, vbuf);
        VDB("There are waiting fbs more than 1");
        return;
     }

   _e_video_commit_buffer(video, vbuf);
}

static void
_e_video_cb_evas_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Video *video = data;

   if (_e_video_geometry_cal_map(video))
     _e_video_render(video, __FUNCTION__);
}

static void
_e_video_cb_evas_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video *video = data;

   if (_e_video_geometry_cal_map(video))
     _e_video_render(video, __FUNCTION__);
}

static void
_e_video_cb_evas_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video *video = data;

   if (e_object_is_del(E_OBJECT(video->ec))) return;

   if (!video->ec->comp_data->video_client)
     return;

   if (video->need_force_render)
     {
        VIN("video forcely rendering..");
        _e_video_render(video, __FUNCTION__);
     }

   /* if stand_alone is true, not show */
   if ((video->ec->comp_data->sub.data && video->ec->comp_data->sub.data->stand_alone) ||
       (video->ec->comp_data->sub.data && video->follow_topmost_visibility))
     {
#if 0 //mute off is managed by client. mute off in server made many issues.
        if (!video->layer) return;

        if (video->tdm_mute_id != -1)
          {
             Tdm_Prop_Value prop = {.id = video->tdm_mute_id, .value.u32 = 0};
             VIN("video surface show. mute off (ec:%p)", video->ec);
             _e_video_layer_set_property(video->layer, &prop);
          }
#endif
        return;
     }

   if (!video->layer)
     {
        VIN("set layer: show");
        if (!_e_video_set_layer(video, EINA_TRUE))
          {
             VER("set layer failed");
             return;
          }
        // need call tdm property in list
        Tdm_Prop_Value *prop;
        EINA_LIST_FREE(video->tdm_prop_list, prop)
          {
             VIN("call property(%s), value(%d)", prop->name, (unsigned int)prop->value.u32);
             _e_video_layer_set_property(video->layer, prop);
             free(prop);
          }
     }

   VIN("evas show (ec:%p)", video->ec);
   if (video->current_fb)
     _e_video_buffer_show(video, video->current_fb, video->current_fb->content_t);
}

static void
_e_video_cb_evas_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Video *video = data;

   if (e_object_is_del(E_OBJECT(video->ec))) return;

   if (!video->ec->comp_data->video_client)
     return;

   /* if stand_alone is true, not hide */
   if (video->ec->comp_data->sub.data && video->ec->comp_data->sub.data->stand_alone)
     {
        if (!video->layer) return;

        if (video->tdm_mute_id != -1)
          {
             Tdm_Prop_Value prop = {.id = video->tdm_mute_id, .value.u32 = 1};
             VIN("video surface hide. mute on (ec:%p)", video->ec);
             _e_video_layer_set_property(video->layer, &prop);
          }
        return;
     }

   VIN("evas hide (ec:%p)", video->ec);
   _e_video_frame_buffer_show(video, NULL);
}

static E_Video *
_e_video_create(struct wl_resource *video_object, struct wl_resource *surface)
{
   E_Video *video;
   E_Client *ec;

   ec = wl_resource_get_user_data(surface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), NULL);

   video = calloc(1, sizeof *video);
   EINA_SAFETY_ON_NULL_RETURN_VAL(video, NULL);

   video->video_object = video_object;
   video->surface = surface;
   video->output_align = -1;
   video->pp_align = -1;
   video->video_align = -1;
   video->tdm_mute_id = -1;

   VIN("create. ec(%p) wl_surface@%d", ec, wl_resource_get_id(video->surface));

   video_list = eina_list_append(video_list, video);

   _e_video_set(video, ec);

   return video;
}

static void
_e_video_set(E_Video *video, E_Client *ec)
{
   int ominw = -1, ominh = -1, omaxw = -1, omaxh = -1;
   int i, count = 0;
   const tdm_prop *props;

   video->ec = ec;
   video->window = e_client_util_win_get(ec);

   evas_object_event_callback_add(video->ec->frame, EVAS_CALLBACK_HIDE,
                                  _e_video_cb_evas_hide, video);
   evas_object_event_callback_add(video->ec->frame, EVAS_CALLBACK_SHOW,
                                  _e_video_cb_evas_show, video);

   if (e_config->eom_enable == EINA_TRUE)
     {
        video->external_video = e_eom_is_ec_external(ec);
        if (video->external_video)
          {
             tdm_error ret;
             unsigned int index = 0;

             video->output = e_eom_tdm_output_by_ec_get(ec);
             EINA_SAFETY_ON_NULL_RETURN(video->output);

             ret = tdm_output_get_pipe(video->output, &index);
             EINA_SAFETY_ON_FALSE_RETURN(ret == TDM_ERROR_NONE);

             video->e_output = e_output_find_by_index(index);
             EINA_SAFETY_ON_NULL_RETURN(video->e_output);

             ec->comp_data->video_client = 1;

             return;
          }
     }

   EINA_SAFETY_ON_NULL_RETURN(video->ec->zone);

   video->e_output = e_output_find(video->ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(video->e_output);

   video->output = video->e_output->toutput;
   EINA_SAFETY_ON_NULL_RETURN(video->output);

   if (video_to_primary)
     {
        VIN("show video to primary layer");
        ec->comp_data->video_client = 0;
        e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, 1);
     }
   else if (_e_video_tdm_output_has_video_layer(video->output))
     {
        /* If tdm offers video layers, we will assign a tdm layer when showing */
        VIN("video client");
        ec->comp_data->video_client = 1;
     }
   else
     {
        /* If tdm doesn't offer video layers, we assign a tdm layer now. If failed,
         * video will be displayed via the UI rendering path.
         */
        if (_e_video_set_layer(video, EINA_TRUE))
          {
             VIN("video client");
             ec->comp_data->video_client = 1;
          }
        else
          {
             VIN("no video client");
             ec->comp_data->video_client = 0;
             e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, 1);
          }
     }

   tdm_output_get_available_size(video->output, &ominw, &ominh, &omaxw, &omaxh, &video->output_align);

   if (!e_comp_screen_pp_support())
     {
        video->video_align = video->output_align;
        tizen_video_object_send_size(video->video_object,
                                     ominw, ominh, omaxw, omaxh, video->output_align);
     }
   else
     {
        int minw = -1, minh = -1, maxw = -1, maxh = -1;
        int pminw = -1, pminh = -1, pmaxw = -1, pmaxh = -1;

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &pminw, &pminh, &pmaxw, &pmaxh, &video->pp_align);

        minw = MAX(ominw, pminw);
        minh = MAX(ominh, pminh);

        if (omaxw != -1 && pmaxw == -1)
          maxw = omaxw;
        else if (omaxw == -1 && pmaxw != -1)
          maxw = pmaxw;
        else
          maxw = MIN(omaxw, pmaxw);

        if (omaxh != -1 && pmaxh == -1)
          maxh = omaxh;
        else if (omaxh == -1 && pmaxh != -1)
          maxh = pmaxh;
        else
          maxh = MIN(omaxh, pmaxh);

        if (video->output_align != -1 && video->pp_align == -1)
          video->video_align = video->output_align;
        else if (video->output_align == -1 && video->pp_align != -1)
          video->video_align = video->pp_align;
        else if (video->output_align == -1 && video->pp_align == -1)
          video->video_align = video->pp_align;
        else if (video->output_align > 0 && video->pp_align > 0)
          video->video_align = lcm(video->output_align, video->pp_align);
        else
          ERR("invalid align: %d, %d", video->output_align, video->pp_align);

        tizen_video_object_send_size(video->video_object,
                                     minw, minh, maxw, maxh, video->video_align);
        VIN("align width: output(%d) pp(%d) video(%d)",
            video->output_align, video->pp_align, video->video_align);
     }

   _e_video_layer_get_available_properties(video->layer, &props, &count);
   for (i = 0; i < count; i++)
     {
        tdm_value value;
        _e_video_layer_get_property(video->layer, props[i].id, &value);
        tizen_video_object_send_attribute(video->video_object, props[i].name, value.u32);

        if (!strncmp(props[i].name, "mute", TDM_NAME_LEN))
          video->tdm_mute_id = props[i].id;
     }
}

static void
_e_video_hide(E_Video *video)
{
   E_Comp_Wl_Video_Buf *vbuf;

   if (video->current_fb || video->committed_list)
     _e_video_frame_buffer_show(video, NULL);

   if (video->current_fb)
     {
        e_comp_wl_video_buffer_set_use(video->current_fb, EINA_FALSE);
        video->current_fb = NULL;
     }

   if (video->old_comp_buffer)
     video->old_comp_buffer = NULL;

   EINA_LIST_FREE(video->committed_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);

   EINA_LIST_FREE(video->waiting_list, vbuf)
      e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
}

static void
_e_video_destroy(E_Video *video)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l = NULL, *ll = NULL;

   if (!video)
     return;

   VIN("destroy");

   if (video->cb_registered)
     {
        evas_object_event_callback_del_full(video->ec->frame, EVAS_CALLBACK_RESIZE,
                                            _e_video_cb_evas_resize, video);
        evas_object_event_callback_del_full(video->ec->frame, EVAS_CALLBACK_MOVE,
                                            _e_video_cb_evas_move, video);
     }

   evas_object_event_callback_del_full(video->ec->frame, EVAS_CALLBACK_HIDE,
                                       _e_video_cb_evas_hide, video);
   evas_object_event_callback_del_full(video->ec->frame, EVAS_CALLBACK_SHOW,
                                       _e_video_cb_evas_show, video);

   wl_resource_set_destructor(video->video_object, NULL);

   _e_video_hide(video);

   /* others */
   EINA_LIST_FOREACH_SAFE(video->input_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   EINA_LIST_FOREACH_SAFE(video->pp_buffer_list, l, ll, vbuf)
     {
        e_comp_wl_video_buffer_set_use(vbuf, EINA_FALSE);
        e_comp_wl_video_buffer_unref(vbuf);
     }

   if(video->tdm_prop_list)
     {
        Tdm_Prop_Value *tdm_prop;
        EINA_LIST_FREE(video->tdm_prop_list, tdm_prop)
          {
             free(tdm_prop);
          }
     }
   if(video->late_tdm_prop_list)
     {
        Tdm_Prop_Value *tdm_prop;
        EINA_LIST_FREE(video->late_tdm_prop_list, tdm_prop)
          {
             free(tdm_prop);
          }
     }

   if (video->input_buffer_list)
     NEVER_GET_HERE();
   if (video->pp_buffer_list)
     NEVER_GET_HERE();
   if (video->tdm_prop_list)
     NEVER_GET_HERE();
   if (video->late_tdm_prop_list)
     NEVER_GET_HERE();

   /* destroy converter second */
   if (video->pp)
     tdm_pp_destroy(video->pp);

   if (video->layer)
     {
        VIN("unset layer: destroy");
        _e_video_set_layer(video, EINA_FALSE);
     }

   video_list = eina_list_remove(video_list, video);

   free(video);

#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static Eina_Bool
_e_video_check_if_pp_needed(E_Video *video)
{
   int i, count = 0;
   const tbm_format *formats;
   Eina_Bool found = EINA_FALSE;
   tdm_layer_capability capabilities = 0;

   if (e_hwc_policy_get(video->e_output->hwc) != E_HWC_POLICY_WINDOWS)
     {
        tdm_layer *layer = _e_video_tdm_video_layer_get(video->output);

        tdm_layer_get_capabilities(layer, &capabilities);

        /* don't need pp if a layer has TDM_LAYER_CAPABILITY_VIDEO capability*/
        if (capabilities & TDM_LAYER_CAPABILITY_VIDEO)
          return EINA_FALSE;

        /* check formats */
        tdm_layer_get_available_formats(layer, &formats, &count);
        for (i = 0; i < count; i++)
          if (formats[i] == video->tbmfmt)
            {
               found = EINA_TRUE;
               break;
            }

        if (!found)
          {
             if (formats && count > 0)
               video->pp_tbmfmt = formats[0];
             else
               {
                  WRN("No layer format information!!!");
                  video->pp_tbmfmt = TBM_FORMAT_ARGB8888;
               }
             return EINA_TRUE;
          }

        if (capabilities & TDM_LAYER_CAPABILITY_SCANOUT)
          goto need_pp;

        /* check size */
        if (video->geo.input_r.w != video->geo.output_r.w || video->geo.input_r.h != video->geo.output_r.h)
          if (!(capabilities & TDM_LAYER_CAPABILITY_SCALE))
            goto need_pp;

        /* check rotate */
        if (video->geo.transform || e_comp->e_comp_screen->rotation > 0)
          if (!(capabilities & TDM_LAYER_CAPABILITY_TRANSFORM))
            goto need_pp;
     }
   else
     {
        tdm_hwc_capability capabilities = 0;
        tdm_error error;

        error = tdm_hwc_get_video_supported_formats(video->e_output->hwc->thwc, &formats, &count);
        if (error != TDM_ERROR_NONE)
          {
             video->pp_tbmfmt = TBM_FORMAT_ARGB8888;
             return EINA_TRUE;
          }
        for (i = 0; i < count; i++)
          if (formats[i] == video->tbmfmt)
            {
               found = EINA_TRUE;
               break;
            }

        if (!found)
          {
             video->pp_tbmfmt = TBM_FORMAT_ARGB8888;
             return EINA_TRUE;
          }

        error = tdm_hwc_get_capabilities(video->e_output->hwc->thwc, &capabilities);
        if (error != TDM_ERROR_NONE)
          {
             video->pp_tbmfmt = TBM_FORMAT_ARGB8888;
             return EINA_TRUE;
          }

        /* check size */
        if (capabilities & TDM_HWC_CAPABILITY_VIDEO_SCANOUT)
          goto need_pp;

        if (video->geo.input_r.w != video->geo.output_r.w || video->geo.input_r.h != video->geo.output_r.h)
          if (!(capabilities & TDM_HWC_CAPABILITY_VIDEO_SCALE))
            goto need_pp;

        /* check rotate */
        if (video->geo.transform || e_comp->e_comp_screen->rotation > 0)
          if (!(capabilities & TDM_HWC_CAPABILITY_VIDEO_TRANSFORM))
            goto need_pp;
     }

   return EINA_FALSE;

need_pp:
   video->pp_tbmfmt = video->tbmfmt;
   return EINA_TRUE;
}

static void
_e_video_pp_cb_done(tdm_pp *pp, tbm_surface_h sb, tbm_surface_h db, void *user_data)
{
   E_Video *video = (E_Video*)user_data;
   E_Comp_Wl_Video_Buf *input_buffer, *pp_buffer;

   input_buffer = _e_video_vbuf_find(video->input_buffer_list, sb);
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

   pp_buffer = _e_video_vbuf_find(video->pp_buffer_list, db);
   if (pp_buffer)
     {
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        if (!_e_video_is_visible(video)) return;

        _e_video_buffer_show(video, pp_buffer, 0);
     }
   else
     {
        VER("There is no pp_buffer");
        // there is no way to set in_use flag.
        // This will cause issue when server get available pp_buffer.
     }
}

static void
_e_video_render(E_Video *video, const char *func)
{
   E_Comp_Wl_Buffer *comp_buffer;
   E_Comp_Wl_Video_Buf *pp_buffer = NULL;
   E_Comp_Wl_Video_Buf *input_buffer = NULL;
   E_Client *topmost;

   EINA_SAFETY_ON_NULL_RETURN(video->ec);

   /* buffer can be NULL when camera/video's mode changed. Do nothing and
    * keep previous frame in this case.
    */
   if (!video->ec->pixmap)
     return;

   if (!_e_video_is_visible(video))
     {
        _e_video_hide(video);
        return;
     }

   comp_buffer = e_pixmap_resource_get(video->ec->pixmap);
   if (!comp_buffer) return;

   _e_video_format_info_get(video);

   /* not interested with other buffer type */
   if (!wayland_tbm_server_get_surface(NULL, comp_buffer->resource))
     return;

   topmost = e_comp_wl_topmost_parent_get(video->ec);
   EINA_SAFETY_ON_NULL_RETURN(topmost);

   if(e_comp_wl_viewport_is_changed(topmost))
     {
        VIN("need update viewport: apply topmost");
        e_comp_wl_viewport_apply(topmost);
     }

   if (!_e_video_geometry_cal(video))
     {
        if(!video->need_force_render && !_e_video_parent_is_viewable(video))
          {
             VIN("need force render");
             video->need_force_render = EINA_TRUE;
          }
        return;
     }

   if (!memcmp(&video->old_geo, &video->geo, sizeof video->geo) &&
       video->old_comp_buffer == comp_buffer)
     return;

   video->need_force_render = EINA_FALSE;

   DBG("====================================== (%s)", func);
   VDB("old: "GEO_FMT" buf(%p)", GEO_ARG(&video->old_geo), video->old_comp_buffer);
   VDB("new: "GEO_FMT" buf(%p) %c%c%c%c", GEO_ARG(&video->geo), comp_buffer, FOURCC_STR(video->tbmfmt));

   _e_video_input_buffer_valid(video, comp_buffer);

   if (!_e_video_check_if_pp_needed(video))
     {
        /* 1. non converting case */
        input_buffer = _e_video_input_buffer_get(video, comp_buffer, EINA_TRUE);
        EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

        _e_video_buffer_show(video, input_buffer, video->geo.tdm_transform);

        video->old_geo = video->geo;
        video->old_comp_buffer = comp_buffer;

        goto done;
     }

   /* 2. converting case */
   if (!video->pp)
     {
        tdm_pp_capability pp_cap;
        tdm_error error = TDM_ERROR_NONE;

        video->pp = tdm_display_create_pp(e_comp->e_comp_screen->tdisplay, NULL);
        EINA_SAFETY_ON_NULL_GOTO(video->pp, render_fail);

        tdm_display_get_pp_available_size(e_comp->e_comp_screen->tdisplay, &video->pp_minw, &video->pp_minh,
                                          &video->pp_maxw, &video->pp_maxh, &video->pp_align);

        error = tdm_display_get_pp_capabilities(e_comp->e_comp_screen->tdisplay, &pp_cap);
        if (error == TDM_ERROR_NONE)
          {
             if (pp_cap & TDM_PP_CAPABILITY_SCANOUT)
               video->pp_scanout = EINA_TRUE;
          }
     }

   if ((video->pp_minw > 0 && (video->geo.input_r.w < video->pp_minw || video->geo.tdm_output_r.w < video->pp_minw)) ||
       (video->pp_minh > 0 && (video->geo.input_r.h < video->pp_minh || video->geo.tdm_output_r.h < video->pp_minh)) ||
       (video->pp_maxw > 0 && (video->geo.input_r.w > video->pp_maxw || video->geo.tdm_output_r.w > video->pp_maxw)) ||
       (video->pp_maxh > 0 && (video->geo.input_r.h > video->pp_maxh || video->geo.tdm_output_r.h > video->pp_maxh)))
     {
        INF("size(%dx%d, %dx%d) is out of PP range",
            video->geo.input_r.w, video->geo.input_r.h, video->geo.tdm_output_r.w, video->geo.tdm_output_r.h);
        goto done;
     }

   input_buffer = _e_video_input_buffer_get(video, comp_buffer, EINA_FALSE);
   EINA_SAFETY_ON_NULL_GOTO(input_buffer, render_fail);

   pp_buffer = _e_video_pp_buffer_get(video, video->geo.tdm_output_r.w, video->geo.tdm_output_r.h);
   EINA_SAFETY_ON_NULL_GOTO(pp_buffer, render_fail);

   if (memcmp(&video->old_geo, &video->geo, sizeof video->geo))
     {
        tdm_info_pp info;

        CLEAR(info);
        info.src_config.size.h = input_buffer->width_from_pitch;
        info.src_config.size.v = input_buffer->height_from_size;
        info.src_config.pos.x = video->geo.input_r.x;
        info.src_config.pos.y = video->geo.input_r.y;
        info.src_config.pos.w = video->geo.input_r.w;
        info.src_config.pos.h = video->geo.input_r.h;
        info.src_config.format = video->tbmfmt;
        info.dst_config.size.h = pp_buffer->width_from_pitch;
        info.dst_config.size.v = pp_buffer->height_from_size;
        info.dst_config.pos.w = video->geo.tdm_output_r.w;
        info.dst_config.pos.h = video->geo.tdm_output_r.h;
        info.dst_config.format = video->pp_tbmfmt;
        info.transform = video->geo.tdm_transform;

        if (tdm_pp_set_info(video->pp, &info))
          {
             VER("tdm_pp_set_info() failed");
             goto render_fail;
          }

        if (tdm_pp_set_done_handler(video->pp, _e_video_pp_cb_done, video))
          {
             VER("tdm_pp_set_done_handler() failed");
             goto render_fail;
          }

        CLEAR(video->pp_r);
        video->pp_r.w = info.dst_config.pos.w;
        video->pp_r.h = info.dst_config.pos.h;
     }

   pp_buffer->content_r = video->pp_r;

   if (tdm_pp_attach(video->pp, input_buffer->tbm_surface, pp_buffer->tbm_surface))
     {
        VER("tdm_pp_attach() failed");
        goto render_fail;
     }

   e_comp_wl_video_buffer_set_use(pp_buffer, EINA_TRUE);

   e_comp_wl_buffer_reference(&input_buffer->buffer_ref, comp_buffer);

   if (tdm_pp_commit(video->pp))
     {
        VER("tdm_pp_commit() failed");
        e_comp_wl_video_buffer_set_use(pp_buffer, EINA_FALSE);
        goto render_fail;
     }

   video->old_geo = video->geo;
   video->old_comp_buffer = comp_buffer;

   goto done;

render_fail:
   if (input_buffer)
     e_comp_wl_video_buffer_unref(input_buffer);

done:
   if (!video->cb_registered)
     {
        evas_object_event_callback_add(video->ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_video_cb_evas_resize, video);
        evas_object_event_callback_add(video->ec->frame, EVAS_CALLBACK_MOVE,
                                       _e_video_cb_evas_move, video);
        video->cb_registered = EINA_TRUE;
     }
   DBG("======================================.");
}

static Eina_Bool
_e_video_cb_ec_buffer_change(void *data, int type, void *event)
{
   E_Client *ec;
   E_Event_Client *ev = event;
   E_Video *video;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   /* not interested with non video_surface,  */
   video = find_video_with_surface(ec->comp_data->surface);
   if (!video) return ECORE_CALLBACK_PASS_ON;

   if (!video->ec->comp_data->video_client)
     return ECORE_CALLBACK_PASS_ON;

   if (e_config->eom_enable == EINA_TRUE)
     {
        /* skip external client buffer if its top parent is not current for eom anymore */
        if (video->external_video && e_eom_is_ec_external(ec))
          {
             VWR("skip external buffer");
             return ECORE_CALLBACK_PASS_ON;
          }
     }

   _e_video_render(video, __FUNCTION__);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_remove(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Video *video;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video = find_video_with_surface(ec->comp_data->surface);
   if (!video) return ECORE_CALLBACK_PASS_ON;

   _e_video_destroy(video);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_client_show(void *data, int type, void *event)
{
   E_Event_Client *ev = event;
   E_Client *ec;
   E_Client *video_ec = NULL;
   E_Video *video = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (!ec->comp_data) return ECORE_CALLBACK_PASS_ON;

   video_ec = find_video_child_get(ec);
   if (!video_ec) return ECORE_CALLBACK_PASS_ON;

   video = find_video_with_surface(video_ec->comp_data->surface);
   if (!video) return ECORE_CALLBACK_PASS_ON;

   VIN("client(0x%08"PRIxPTR") show: find video child(0x%08"PRIxPTR")", (Ecore_Window)e_client_util_win_get(ec), (Ecore_Window)e_client_util_win_get(video_ec));
   if(video->old_comp_buffer)
     {
        VIN("video already rendering..");
        return ECORE_CALLBACK_PASS_ON;
     }

   if (ec == e_comp_wl_topmost_parent_get(video->ec))
     {
        if (e_config->eom_enable == EINA_TRUE)
          {
             /* skip external client buffer if its top parent is not current for eom anymore */
             if (video->external_video && e_eom_is_ec_external(ec))
               {
                  VWR("skip external buffer");
                  return ECORE_CALLBACK_PASS_ON;
               }
          }

        VIN("video need rendering..");
        e_comp_wl_viewport_apply(ec);
        _e_video_render(video, __FUNCTION__);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_video_cb_ec_visibility_change(void *data, int type, void *event)
{
   E_Event_Remote_Surface_Provider *ev = event;
   E_Client *ec = ev->ec;
   E_Video *video;
   Eina_List *l;

   EINA_LIST_FOREACH(video_list, l, video)
     {
        E_Client *offscreen_parent = find_offscreen_parent_get(video->ec);
        if (!offscreen_parent) continue;
        if (offscreen_parent != ec) continue;
        switch (ec->visibility.obscured)
          {
           case E_VISIBILITY_FULLY_OBSCURED:
              _e_video_cb_evas_hide(video, NULL, NULL, NULL);
              break;
           case E_VISIBILITY_UNOBSCURED:
              _e_video_cb_evas_show(video, NULL, NULL, NULL);
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
   E_Video *video;
   Eina_List *l = NULL;

   EINA_LIST_FOREACH(video_list, l, video)
     {
        E_Client *topmost = e_comp_wl_topmost_parent_get(video->ec);
        if (!topmost) continue;
        if (topmost == video->ec) continue;
        if (topmost != ec) continue;
        if (video->follow_topmost_visibility)
          {
             switch (ec->visibility.obscured)
               {
                case E_VISIBILITY_FULLY_OBSCURED:
                   VIN("follow_topmost_visibility: fully_obscured");
                   _e_video_cb_evas_hide(video, NULL, NULL, NULL);
                   break;
                case E_VISIBILITY_UNOBSCURED:
                   VIN("follow_topmost_visibility: UNOBSCURED");
                   _e_video_cb_evas_show(video, NULL, NULL, NULL);
                   break;
                default:
                   return ECORE_CALLBACK_PASS_ON;
               }
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_wl_video_object_destroy(struct wl_resource *resource)
{
   E_Video *video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   VDT("Video from Client(%s):PID(%d) is being destroyed, details are: "
       "Buffer(%p), Video_Format(%c%c%c%c), "
       "Buffer_Size(%dx%d), Src Rect(%d,%d, %dx%d), Dest Rect(%d,%d, %dx%d),"
       " Transformed(%d)",
       e_client_util_name_get(video->ec) ?: "No Name" , video->ec->netwm.pid,
       video->current_fb, FOURCC_STR(video->tbmfmt),
       video->geo.input_w, video->geo.input_h, video->geo.input_r.x ,
       video->geo.input_r.y, video->geo.input_r.w, video->geo.input_r.h,
       video->geo.output_r.x ,video->geo.output_r.y, video->geo.output_r.w,
       video->geo.output_r.h, video->geo.transform);

   _e_video_destroy(video);
}

static void
_e_comp_wl_video_object_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_video_object_cb_set_attribute(struct wl_client *client,
                                         struct wl_resource *resource,
                                         const char *name,
                                         int32_t value)
{
   E_Video *video;
   int id;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(video->ec)
     VDT("Client(%s):PID(%d) RscID(%d) Attribute:%s, Value:%d",
         e_client_util_name_get(video->ec) ?: "No Name",
         video->ec->netwm.pid, wl_resource_get_id(video->surface),
         name, value);

   // check available property & count
   id = _e_video_get_prop_id(video, name);
   if(id < 0)
     {
        VIN("no available property");
        return;
     }

   if (!video->layer && video->allowed_attribute)
     {
        VIN("set layer: set_attribute");
        if (!_e_video_set_layer(video, EINA_TRUE))
          {
             VER("set layer failed");
             return;
          }
     }

   if (!_e_video_is_visible(video) || !video->layer)
     {
        /* if mute off, need to do it after buffer commit */
        if (!strncmp(name, "mute", TDM_NAME_LEN) && value == 0)
          {
             Tdm_Prop_Value *prop = NULL;
             const Eina_List *l = NULL;

             EINA_LIST_FOREACH(video->late_tdm_prop_list, l, prop)
               {
                  if (!strncmp(name, prop->name, TDM_NAME_LEN))
                    {
                       prop->value.u32 = value;
                       VDB("update property(%s) value(%d)", prop->name, value);
                       return;
                    }
               }

             prop = calloc(1, sizeof(Tdm_Prop_Value));
             if(!prop) return;

             prop->value.u32 = value;
             prop->id = id;
             memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
             VIN("Add property(%s) value(%d)", prop->name, value);
             video->late_tdm_prop_list = eina_list_append(video->late_tdm_prop_list, prop);
             return;
          }
     }

   // check set video layer
   if(!video->layer)
     {
        VIN("no layer: save property value");

        Tdm_Prop_Value *prop = NULL;
        const Eina_List *l = NULL;

        EINA_LIST_FOREACH(video->tdm_prop_list, l, prop)
          {
             if (!strncmp(name, prop->name, TDM_NAME_LEN))
               {
                  VDB("find prop data(%s) update value(%d -> %d)", prop->name, (unsigned int)prop->value.u32, (unsigned int)value);
                  prop->value.u32 = value;
                  return;
               }
          }
        EINA_LIST_FOREACH(video->late_tdm_prop_list, l, prop)
          {
             if (!strncmp(name, prop->name, TDM_NAME_LEN))
               {
                  VDB("find prop data(%s) update value(%d -> %d)", prop->name, (unsigned int)prop->value.u32, (unsigned int)value);
                  prop->value.u32 = value;
                  return;
               }
          }

        prop = calloc(1, sizeof(Tdm_Prop_Value));
        if(!prop) return;
        prop->value.u32 = value;
        prop->id = id;
        memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
        VIN("Add property(%s) value(%d)", prop->name, value);
        video->tdm_prop_list = eina_list_append(video->tdm_prop_list, prop);
     }
   // if set layer call property
   else
     {
        Tdm_Prop_Value prop = {.id = id, .value.u32 = value};
        VIN("set layer: call property(%s), value(%d)", name, value);
        _e_video_layer_set_property(video->layer, &prop);
     }
}

static void
_e_comp_wl_video_object_cb_follow_topmost_visibility(struct wl_client *client,
                                                     struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || video->follow_topmost_visibility)
     return;

   VIN("set follow_topmost_visibility");

   video->follow_topmost_visibility= EINA_TRUE;

}

static void
_e_comp_wl_video_object_cb_unfollow_topmost_visibility(struct wl_client *client,
                                                       struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || !video->follow_topmost_visibility)
     return;

   VIN("set unfollow_topmost_visibility");

   video->follow_topmost_visibility= EINA_FALSE;

}

static void
_e_comp_wl_video_object_cb_allowed_attribute(struct wl_client *client,
                                             struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || video->allowed_attribute)
     return;

   VIN("set allowed_attribute");

   video->allowed_attribute = EINA_TRUE;

}

static void
_e_comp_wl_video_object_cb_disallowed_attribute(struct wl_client *client,
                                                struct wl_resource *resource)
{
   E_Video *video;

   video = wl_resource_get_user_data(resource);
   EINA_SAFETY_ON_NULL_RETURN(video);

   if(!video->ec || !video->allowed_attribute)
     return;

   VIN("set disallowed_attribute");

   video->allowed_attribute = EINA_FALSE;

}

static const struct tizen_video_object_interface _e_comp_wl_video_object_interface =
{
   _e_comp_wl_video_object_cb_destroy,
   _e_comp_wl_video_object_cb_set_attribute,
   _e_comp_wl_video_object_cb_follow_topmost_visibility,
   _e_comp_wl_video_object_cb_unfollow_topmost_visibility,
   _e_comp_wl_video_object_cb_allowed_attribute,
   _e_comp_wl_video_object_cb_disallowed_attribute,
};

static void
_e_comp_wl_video_cb_get_object(struct wl_client *client,
                               struct wl_resource *resource,
                               uint32_t id,
                               struct wl_resource *surface)
{
   E_Video *video;
   int version = wl_resource_get_version(resource);
   struct wl_resource *res;

   res = wl_resource_create(client, &tizen_video_object_interface, version, id);
   if (res == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   if (!(video = _e_video_create(res, surface)))
     {
        wl_resource_destroy(res);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_wl_video_object_interface,
                                  video, _e_comp_wl_video_object_destroy);
}

static void
_e_comp_wl_video_cb_get_viewport(struct wl_client *client,
                                 struct wl_resource *resource,
                                 uint32_t id,
                                 struct wl_resource *surface)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(surface))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data && ec->comp_data->scaler.viewport)
     {
        wl_resource_post_error(resource,
                               TIZEN_VIDEO_ERROR_VIEWPORT_EXISTS,
                               "a viewport for that subsurface already exists");
        return;
     }

   if (!e_comp_wl_viewport_create(resource, id, surface))
     {
        ERR("Failed to create viewport for wl_surface@%d",
            wl_resource_get_id(surface));
        wl_client_post_no_memory(client);
        return;
     }
}

static void
_e_comp_wl_video_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_video_interface _e_comp_wl_video_interface =
{
   _e_comp_wl_video_cb_get_object,
   _e_comp_wl_video_cb_get_viewport,
   _e_comp_wl_video_cb_destroy,
};

static void
_e_comp_wl_video_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   const uint32_t *formats = NULL;
   int i, count = 0;
   Eina_List *pp_format_list = NULL;
   Eina_List *l = NULL;
   uint32_t *pp_format;

   if (!(res = wl_resource_create(client, &tizen_video_interface, version, id)))
     {
        ERR("Could not create tizen_video_interface resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_wl_video_interface, NULL, NULL);

   /* 1st, use pp information. */
   if (e_comp_screen_pp_support())
     pp_format_list = e_comp_screen_pp_available_formats_get();

   if (pp_format_list)
     {
        EINA_LIST_FOREACH(pp_format_list, l, pp_format)
          tizen_video_send_format(res, *pp_format);
     }
   else
     {
        _e_video_get_available_formats(&formats, &count);
        for (i = 0; i < count; i++)
          tizen_video_send_format(res, formats[i]);
     }
}

static Eina_List *video_hdlrs;

static void
_e_comp_wl_vbuf_print(void *data, const char *log_path)
{
   e_comp_wl_video_buffer_list_print(log_path);
}

static void
_e_comp_wl_video_to_primary(void *data, const char *log_path)
{
   video_to_primary = !video_to_primary;
}

static void
_e_comp_wl_video_punch(void *data, const char *log_path)
{
   video_punch = !video_punch;
}

EINTERN int
e_comp_wl_video_init(void)
{
   e_comp->wl_comp_data->available_hw_accel.underlay = EINA_TRUE;
   DBG("enable HW underlay");

   e_comp->wl_comp_data->available_hw_accel.scaler = EINA_TRUE;
   DBG("enable HW scaler");

   if (!e_comp_wl) return 0;
   if (!e_comp_wl->wl.disp) return 0;
   if (e_comp->wl_comp_data->video.global) return 1;

   e_info_server_hook_set("vbuf", _e_comp_wl_vbuf_print, NULL);
   e_info_server_hook_set("video-to-primary", _e_comp_wl_video_to_primary, NULL);
   e_info_server_hook_set("video-punch", _e_comp_wl_video_punch, NULL);

   _video_detail_log_dom = eina_log_domain_register("e-comp-wl-video", EINA_COLOR_BLUE);
   if (_video_detail_log_dom < 0)
     {
        ERR("Failed eina_log_domain_register()..!\n");
        return 0;
     }

   /* try to add tizen_video to wayland globals */
   e_comp->wl_comp_data->video.global =
      wl_global_create(e_comp_wl->wl.disp, &tizen_video_interface, 1, NULL, _e_comp_wl_video_cb_bind);

   if (!e_comp->wl_comp_data->video.global)
     {
        ERR("Could not add tizen_video to wayland globals");
        return 0;
     }

   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_CLIENT_BUFFER_CHANGE,
                         _e_video_cb_ec_buffer_change, NULL);
   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_CLIENT_REMOVE,
                         _e_video_cb_ec_remove, NULL);
   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_CLIENT_SHOW,
                         _e_video_cb_ec_client_show, NULL);
   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE,
                         _e_video_cb_ec_visibility_change, NULL);
   E_LIST_HANDLER_APPEND(video_hdlrs, E_EVENT_CLIENT_VISIBILITY_CHANGE,
                         _e_video_cb_topmost_ec_visibility_change, NULL);

   return 1;
}

EINTERN void
e_comp_wl_video_shutdown(void)
{
   e_comp->wl_comp_data->available_hw_accel.underlay = EINA_FALSE;
   e_comp->wl_comp_data->available_hw_accel.scaler = EINA_FALSE;

   E_FREE_FUNC(e_comp->wl_comp_data->video.global, wl_global_destroy);
   E_FREE_LIST(video_hdlrs, ecore_event_handler_del);

   e_info_server_hook_set("vbuf", NULL, NULL);
   e_info_server_hook_set("video-dst-change", NULL, NULL);
   e_info_server_hook_set("video-to-primary", NULL, NULL);
   e_info_server_hook_set("video-punch", NULL, NULL);

   eina_log_domain_unregister(_video_detail_log_dom);
   _video_detail_log_dom = -1;
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
