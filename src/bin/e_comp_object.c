#include "e.h"

/* data keys:

   = keys that return objects =
   - E_Client: the client associated with the object (E_Client*)
   - comp_smart_obj: cw->smart_obj (Evas_Object*)
   - comp_obj: cw (E_Comp_Object*)

   = keys that are bool flags =
   - client_restack: client needs a protocol-level restack
   - comp_override: object is triggering a nocomp override to force compositing
   - comp_ref: object has a ref from visibility animations
   - comp_showing: object is currently running its show animation
   - comp_hiding: object is currently running its hiding animation
   - comp_object: object is a compositor-created object
   - comp_object_skip: object has a name which prohibits theme shadows
   - comp_object-to_del: list of objects which will be deleted when this object is deleted
   - comp_mirror: object is the result of e_comp_object_util_mirror_add()
   - effect_running: object is animating by external module
*/

#define UPDATE_MAX 512 // same as evas
#define FAILURE_MAX 2 // seems reasonable
#define SMART_NAME     "e_comp_object"
#define INPUT_OBJ_SMART_NAME "input_object"

/* for non-util functions */
#define API_ENTRY      E_Comp_Object *cw; \
                       cw = evas_object_smart_data_get(obj); \
                       if ((!obj) || (!cw) || (e_util_strcmp(evas_object_type_get(obj), SMART_NAME))) return

/* for util functions (obj may or may not be E_Comp_Object */
#define SOFT_ENTRY(...)      E_Comp_Object *cw; \
                       if (!obj) \
                         { \
                            CRI("YOU PASSED NULL! ARGH!"); \
                            return __VA_ARGS__; \
                         } \
                       cw = evas_object_smart_data_get(obj); \
                       if ((!cw) || (e_util_strcmp(evas_object_type_get(obj), SMART_NAME))) \
                         cw = NULL
#define INTERNAL_ENTRY E_Comp_Object *cw; cw = evas_object_smart_data_get(obj); if (!cw) return;

/* enable for lots of client size info in console output */
#if 1
# define e_util_size_debug_set(x, y)
#endif

/* enable along with display-specific damage INF calls to enable render tracing
 * SLOW!
 */
#if 0
#define RENDER_DEBUG(...) INF(__VA_ARGS__)
#else
#define RENDER_DEBUG(...)
#endif

typedef struct _E_Comp_Object
{
   EINA_INLIST;

   int                  x, y, w, h;  // geometry

   E_Client *ec;

   E_Comp_Object_Frame client_inset;
   struct
   {
      double          start;
      double          val;
      int             x, y;
      E_Direction     dir;
      Ecore_Animator *anim;
   } shade;

   Eina_Stringshare   *frame_theme;
   Eina_Stringshare   *frame_name;
   Eina_Stringshare   *visibility_effect; //effect when toggling visibility

   E_Comp_Object_Content_Type content_type; // type of e.swallow.content

   Evas_Object         *smart_obj;  // smart object
   Evas_Object         *clip; // clipper over effect object
   Evas_Object         *input_obj; // input smart object
   Evas_Object         *obj;  // composite object
   Evas_Object         *frame_object; // for client frames
   Evas_Object         *zoomobj; // zoomap
   Evas_Object         *shobj;  // shadow object
   Evas_Object         *effect_obj; // effects object
   Evas_Object         *transform_bg_obj;   // transform backgroung with keep_ratio option
   Evas_Object         *transform_tranp_obj;// transform transp rect obj
   Evas_Object         *map_input_obj; // input object to avoid the event transform by evas map
   unsigned int         layer; //e_comp_canvas_layer_map(cw->ec->layer)
   Eina_List           *obj_mirror;  // extra mirror objects
   Eina_Tiler          *updates; //render update regions
   Eina_Tiler          *pending_updates; //render update regions which are about to render

   Evas_Native_Surface *ns; //for custom gl rendering

   struct wl_listener   buffer_destroy_listener;

   unsigned int         update_count;  // how many updates have happened to this obj

   unsigned int         opacity;  // opacity set with _NET_WM_WINDOW_OPACITY

   unsigned int         animating;  // it's busy animating
   unsigned int         failures; //number of consecutive e_pixmap_image_draw() failures
   unsigned int         force_visible; //number of visible obj_mirror objects
   Eina_Bool            delete_pending : 1;  // delete pending
   Eina_Bool            defer_hide : 1;  // flag to get hide to work on deferred hide
   Eina_Bool            showing : 1;  // object is currently in "show" animation
   Eina_Bool            visible : 1;  // is visible

   Eina_Bool            shaped : 1;  // is shaped
   Eina_Bool            update : 1;  // has updates to fetch
   Eina_Bool            redirected : 1;  // has updates to fetch
   Eina_Bool            native : 1;  // native

   Eina_Bool            nocomp : 1;  // nocomp applied
   Eina_Bool            hwc_need_update : 1;  // this window updated while on e_plane to do hw composite
   Eina_Bool            real_hid : 1;  // last hide was a real window unmap

   Eina_Bool            effect_set : 1; //effect_obj has a valid group
   Eina_Bool            effect_running : 1; //effect_obj is playing an animation
   Eina_Bool            effect_clip : 1; //effect_obj is clipped
   Eina_Bool            effect_clip_able : 1; //effect_obj will be clipped for effects

   Eina_Bool            zoomap_disabled : 1; //whether zoomap is usable
   Eina_Bool            updates_exist : 1;
   Eina_Bool            updates_full : 1; // entire object will be updated

   Eina_Bool            force_move : 1;
   Eina_Bool            frame_extends : 1; //frame may extend beyond object size
   Eina_Bool            blanked : 1; //window is rendering blank content (externally composited)
   Eina_Bool            external_content : 1; // e.swallow.content(obj) is set by external evas object
   Eina_Bool            user_alpha_set : 1;
   Eina_Bool            user_alpha : 1;

   struct
     {
        Evas_Object         *obj; // mask object: transparent parts of this comp object allow to copy the alpha to current H/W plane.
        Evas_Render_Op       saved_render_op; // saved render operation value to restore when clear a mask.
     } mask;

   struct
     {
        Evas_Object         *obj;
        int                  w, h;
     } indicator; //indicator object for internal client

   struct
     {
        Evas_Object         *mask_obj;
        Eina_Bool           enable;
        Eina_Bool           mask_set;
        int                 mask_x, mask_y, mask_w, mask_h;
     } dim;

   Eina_Bool            render_trace : 1; // trace co->obj rendering on canvas

   tbm_surface_h        tbm_surface;
} E_Comp_Object;

typedef struct _E_Input_Rect_Data
{
   Eina_Rectangle rect;
   Evas_Object *obj;
} E_Input_Rect_Data;

typedef struct _E_Input_Rect_Smart_Data
{
   Eina_List *input_rect_data_list;
   E_Comp_Object *cw;
} E_Input_Rect_Smart_Data;

struct E_Comp_Object_Mover
{
   EINA_INLIST;
   E_Comp_Object_Mover_Cb func;
   const char *sig;
   void *data;
   int pri;
};

static Eina_Inlist *_e_comp_object_movers = NULL;
static Evas_Smart *_e_comp_smart = NULL;
static Evas_Smart *_e_comp_input_obj_smart = NULL;

static int _e_comp_object_hooks_delete = 0;
static int _e_comp_object_hooks_walking = 0;

static Eina_Inlist *_e_comp_object_hooks[] =
{
   [E_COMP_OBJECT_HOOK_EFFECT_START] = NULL,
   [E_COMP_OBJECT_HOOK_EFFECT_END] = NULL,
   [E_COMP_OBJECT_HOOK_RESTACK] = NULL,
};

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
static int _e_comp_object_intercept_hooks_delete = 0;
static int _e_comp_object_intercept_hooks_walking = 0;

static Eina_Inlist *_e_comp_object_intercept_hooks[] =
{
   [E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER] = NULL,
   [E_COMP_OBJECT_INTERCEPT_HOOK_HIDE] = NULL,
};
#endif

/* sekrit functionzzz */
EINTERN void e_client_focused_set(E_Client *ec);

/* emitted every time a new noteworthy comp object is added */
E_API int E_EVENT_COMP_OBJECT_ADD = -1;

/* ecore event define */
E_API int E_EVENT_COMP_OBJECT_IMG_RENDER = -1;
E_API int E_EVENT_COMP_OBJECT_EFFECT_START = -1;
E_API int E_EVENT_COMP_OBJECT_EFFECT_END = -1;

static void           _e_comp_object_dim_enable_set(E_Client *ec, Evas_Object *obj, Eina_Bool enable, Eina_Bool noeffect);
static Eina_Bool      _e_comp_object_dim_enable_get(E_Client *ec, Evas_Object *obj);
static void           _e_comp_object_dim_update(E_Comp_Object *cw);
static void           _e_comp_object_native_surface_set(E_Comp_Object *cw, Evas_Native_Surface *ns, Eina_Bool with_mirror);

static E_Client       *dim_client = NULL;

static void
_e_comp_object_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Comp_Object_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_COMP_OBJECT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_comp_object_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_comp_object_hooks[x] = eina_inlist_remove(_e_comp_object_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static Eina_Bool
_e_comp_object_hook_call(E_Comp_Object_Hook_Point hookpoint, E_Client *ec)
{
   E_Comp_Object_Hook *ch;
   Eina_Bool ret = EINA_TRUE;

   if (e_object_is_del(E_OBJECT(ec)))
     {
        if ((hookpoint != E_COMP_OBJECT_HOOK_EFFECT_START) &&
            (hookpoint != E_COMP_OBJECT_HOOK_EFFECT_END) &&
            (hookpoint != E_COMP_OBJECT_HOOK_RESTACK))
          {
             return ret;
          }
     }
   e_object_ref(E_OBJECT(ec));
   _e_comp_object_hooks_walking++;
   EINA_INLIST_FOREACH(_e_comp_object_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        if (!(ch->func(ch->data, ec)))
          {
             ret = EINA_FALSE;
             break;
          }
     }
   _e_comp_object_hooks_walking--;
   if ((_e_comp_object_hooks_walking == 0) && (_e_comp_object_hooks_delete > 0))
     _e_comp_object_hooks_clean();

   e_object_unref(E_OBJECT(ec));

   return ret;
}

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
static void
_e_comp_object_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Comp_Object_Intercept_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_COMP_OBJECT_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_comp_object_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_comp_object_intercept_hooks[x] = eina_inlist_remove(_e_comp_object_intercept_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static Eina_Bool
_e_comp_object_intercept_hook_call(E_Comp_Object_Intercept_Hook_Point hookpoint, E_Client *ec)
{
   E_Comp_Object_Intercept_Hook *ch;
   Eina_Bool ret = EINA_TRUE;

   if (e_object_is_del(E_OBJECT(ec))) return ret;
   e_object_ref(E_OBJECT(ec));
   _e_comp_object_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_comp_object_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        if (!(ch->func(ch->data, ec)))
          {
             ret = EINA_FALSE;
             break;
          }
     }
   _e_comp_object_intercept_hooks_walking--;
   if ((_e_comp_object_intercept_hooks_walking == 0) && (_e_comp_object_intercept_hooks_delete > 0))
     _e_comp_object_intercept_hooks_clean();

   e_object_unref(E_OBJECT(ec));

   return ret;
}
#endif

static void
_e_comp_object_event_free(void *d EINA_UNUSED, void *event)
{
   E_Event_Comp_Object *ev = event;
   E_Client *ec;

   ec = evas_object_data_get(ev->comp_object, "E_Client");
   if (ec)
     {
        UNREFD(ec, 1);
        e_object_unref(E_OBJECT(ec));
     }
   evas_object_unref(ev->comp_object);
   free(ev);
}

static void
_e_comp_object_event_add(Evas_Object *obj)
{
   E_Event_Comp_Object *ev;
   E_Client *ec;

   if (stopping) return;
   ev = E_NEW(E_Event_Comp_Object, 1);
   EINA_SAFETY_ON_NULL_RETURN(ev);

   evas_object_ref(obj);
   ev->comp_object = obj;
   ec = evas_object_data_get(ev->comp_object, "E_Client");
   if (ec)
     {
        REFD(ec, 1);
        e_object_ref(E_OBJECT(ec));
     }
   ecore_event_add(E_EVENT_COMP_OBJECT_ADD, ev, _e_comp_object_event_free, NULL);
}

static void
_e_comp_object_simple_free(void *d EINA_UNUSED, void *event)
{
   E_Event_Comp_Object *ev = event;
   E_Client *ec;

   ec = evas_object_data_get(ev->comp_object, "E_Client");
   if (ec)
     {
        UNREFD(ec, 1);
        e_object_unref(E_OBJECT(ec));
     }
   evas_object_unref(ev->comp_object);
   free(ev);
}

static void
_e_comp_object_event_simple(Evas_Object *obj, int type)
{
   E_Event_Comp_Object *ev;
   E_Client *ec;

   ev = E_NEW(E_Event_Comp_Object, 1);
   if (!ev) return;

   evas_object_ref(obj);
   ev->comp_object = obj;
   ec = evas_object_data_get(ev->comp_object, "E_Client");
   if (ec)
     {
        REFD(ec, 1);
        e_object_ref(E_OBJECT(ec));
     }
   ecore_event_add(type, ev, (Ecore_End_Cb)_e_comp_object_simple_free, NULL);
}
/////////////////////////////////////

static void
_e_comp_object_cb_mirror_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   cw->obj_mirror = eina_list_remove(cw->obj_mirror, obj);
}

static void
_e_comp_object_cb_mirror_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if ((!cw->force_visible) && (!e_object_is_del(E_OBJECT(cw->ec))))
     evas_object_smart_callback_call(cw->smart_obj, "visibility_force", cw->ec);
   cw->force_visible++;

   if (e_comp->hwc && !e_comp_is_on_overlay(cw->ec))
     e_comp_hwc_client_end(cw->ec, __FUNCTION__);
}

static void
_e_comp_object_cb_mirror_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   cw->force_visible--;
   if ((!cw->force_visible) && (!e_object_is_del(E_OBJECT(cw->ec))))
     evas_object_smart_callback_call(cw->smart_obj, "visibility_normal", cw->ec);
}

/////////////////////////////////////

static void
_e_comp_object_transform_obj_stack_update(Evas_Object *obj)
{
   int layer = 0;
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;

   layer = evas_object_layer_get(obj);

   if (cw->transform_bg_obj)
     {
        if (layer != evas_object_layer_get(cw->transform_bg_obj))
          {
             evas_object_layer_set(cw->transform_bg_obj, layer);
          }

        evas_object_stack_below(cw->transform_bg_obj, obj);
     }

   if (cw->transform_tranp_obj)
     {
        if (layer != evas_object_layer_get(cw->transform_tranp_obj))
          {
             evas_object_layer_set(cw->transform_tranp_obj, layer);
          }

        evas_object_stack_below(cw->transform_tranp_obj, obj);
     }
}

static void
_e_comp_object_transform_obj_map_set(Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices)
{
   if (!obj) return;

   if (vertices)
     {
        Evas_Map *map = evas_map_new(4);

        if (map)
          {
             int i;
             evas_map_util_points_populate_from_object_full(map, obj, 0);
             evas_map_util_points_color_set(map, 255, 255, 255, 255);

             for (i = 0 ; i < 4 ; ++i)
               {
                  int x = 0;
                  int y = 0;

                  e_util_transform_vertices_pos_round_get(vertices, i, &x, &y, 0, 0);
                  evas_map_point_coord_set(map, i, x, y, 1.0);
               }

             evas_object_map_set(obj, map);
             evas_object_map_enable_set(obj, EINA_TRUE);

             evas_map_free(map);
          }
     }
   else
     {
        evas_object_map_enable_set(obj, EINA_FALSE);
     }
}

/////////////////////////////////////

static inline Eina_Bool
_e_comp_shaped_check(int w, int h, const Eina_Rectangle *rects, int num)
{
   if (num > 1) return EINA_TRUE;
   if ((rects[0].x == 0) && (rects[0].y == 0) &&
       ((int)rects[0].w == w) && ((int)rects[0].h == h))
     return EINA_FALSE;
   return EINA_TRUE;
}

/////////////////////////////////////

/* add a client to the layer-client list */
static void
_e_comp_object_layers_add(E_Comp_Object *cw, E_Comp_Object *above, E_Comp_Object *below, Eina_Bool prepend)
{
   E_Comp_Object *layer_cw = NULL;

   /* try to get the internal data for the layer;
    * will return NULL for fake layers (eg. wayland)
    */
   if (e_comp->layers[cw->layer].obj)
     layer_cw = evas_object_smart_data_get(e_comp->layers[cw->layer].obj);
   if (layer_cw == cw) layer_cw = NULL;
   if (above)
    e_comp->layers[above->layer].clients = eina_inlist_append_relative(e_comp->layers[above->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(above->ec));
   else if (below)
     e_comp->layers[below->layer].clients = eina_inlist_prepend_relative(e_comp->layers[below->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(below->ec));
   if ((!above) && (!below))
     {
        if (prepend)
          e_comp->layers[cw->layer].clients = eina_inlist_prepend(e_comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
        else if (layer_cw)
          e_comp->layers[cw->layer].clients = eina_inlist_prepend_relative(e_comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec), EINA_INLIST_GET(layer_cw->ec));
        else //this is either the layer object or a tough actin tinactin^W^W^Wfast stacking client
          e_comp->layers[cw->layer].clients = eina_inlist_append(e_comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
     }
   e_comp->layers[cw->layer].clients_count++;
#ifndef E_RELEASE_BUILD
   if (layer_cw)
     {
        E_Client *below_ec = e_client_below_get(cw->ec);
        if (below_ec)
          {
             if (e_comp->layers[cw->layer].obj == below_ec->frame)
               CRI("ACK!");
          }
     }
#endif
}

static void
_e_comp_object_layers_remove(E_Comp_Object *cw)
{
   if (cw->ec && e_comp->layers[cw->layer].clients)
     {
        e_comp->layers[cw->layer].clients = eina_inlist_remove(e_comp->layers[cw->layer].clients, EINA_INLIST_GET(cw->ec));
        e_comp->layers[cw->layer].clients_count--;
     }
/*
   e_comp->layers[cw->layer].objs = eina_inlist_remove(e_comp->layers[cw->layer].objs, EINA_INLIST_GET(cw));
   e_comp->layers[cw->layer].objs_count--;
*/
}

/////////////////////////////////////
static void
_e_comp_object_alpha_set(E_Comp_Object *cw)
{
   Eina_Bool alpha = cw->ec->argb;

   if ((cw->external_content) &&
       (cw->content_type != E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE))
     {
        WRN("Can set up alpha value to ONLY evas \"image\" object. "
            "But current external content is %d object for %p.",
            cw->content_type, cw->ec);
        return;
     }

   if (cw->blanked || cw->ns || cw->ec->shaped) alpha = EINA_TRUE;
   if (cw->user_alpha_set) alpha = cw->user_alpha;

   evas_object_image_alpha_set(cw->obj, alpha);
}

static void
_e_comp_object_shadow(E_Comp_Object *cw)
{
   if (e_client_util_shadow_state_get(cw->ec))
     edje_object_signal_emit(cw->frame_object ?: cw->shobj, "e,state,shadow,on", "e");
   else
     edje_object_signal_emit(cw->frame_object ?: cw->shobj, "e,state,shadow,off", "e");
   if (cw->frame_object)
     edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
   evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
}

/* convert from the surface coordinates to the buffer coordinates */
static void
_e_comp_object_map_transform_pos(E_Client *ec, int sx, int sy, int *dx, int *dy)
{
   E_Comp_Wl_Buffer_Viewport *vp;
   int transform;
   int bw, bh, tx, ty;

   if (!ec || !ec->comp_data || e_object_is_del(E_OBJECT(ec)))
     {
        *dx = sx;
        *dy = sy;
        return;
     }

   vp = &ec->comp_data->scaler.buffer_viewport;
   transform = e_comp_wl_output_buffer_transform_get(ec);

   e_pixmap_size_get(ec->pixmap, &bw, &bh);

   /* for subsurface, it should be swap 90 and 270 */
   if (ec->comp_data->sub.data)
     switch (transform)
       {
        case WL_OUTPUT_TRANSFORM_90:          transform = WL_OUTPUT_TRANSFORM_270;         break;
        case WL_OUTPUT_TRANSFORM_270:         transform = WL_OUTPUT_TRANSFORM_90;          break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_90:  transform = WL_OUTPUT_TRANSFORM_FLIPPED_270; break;
        case WL_OUTPUT_TRANSFORM_FLIPPED_270: transform = WL_OUTPUT_TRANSFORM_FLIPPED_90;  break;
        default: break;
       }

   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:                              tx = sx,      ty = sy;      break;
      case WL_OUTPUT_TRANSFORM_90:          tx = sy,      ty = bw - sx; break;
      case WL_OUTPUT_TRANSFORM_180:         tx = bw - sx, ty = bh - sy; break;
      case WL_OUTPUT_TRANSFORM_270:         tx = bh - sy, ty = sx;      break;
      case WL_OUTPUT_TRANSFORM_FLIPPED:     tx = bw - sx, ty = sy;      break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:  tx = sy,      ty = sx;      break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180: tx = sx,      ty = bh - sy; break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270: tx = bh - sy, ty = bw - sx; break;
     }

   tx *= vp->buffer.scale;
   ty *= vp->buffer.scale;

   *dx = tx;
   *dy = ty;
}

static void
_e_comp_object_map_transform_rect(E_Client *ec, int sx, int sy, int sw, int sh, int *dx, int *dy, int *dw, int *dh)
{
   int x1 = sx;
   int y1 = sy;
   int x2 = sx + sw;
   int y2 = sy + sh;
   int mx, my;

   _e_comp_object_map_transform_pos(ec, x1, y1, &x1, &y1);
   _e_comp_object_map_transform_pos(ec, x2, y2, &x2, &y2);

   mx = MIN(x1, x2);
   my = MIN(y1, y2);

   if (dx) *dx = mx;
   if (dy) *dy = my;
   if (dw) *dw = MAX(x1, x2) - mx;
   if (dh) *dh = MAX(y1, y2) - my;
}

/////////////////////////////////////

/* handle evas mouse-in events on client object */
static void
_e_comp_object_cb_mouse_in(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_In *ev = event_info;
   E_Comp_Object *cw = data;

   e_client_mouse_in(cw->ec, ev->output.x, ev->output.y);
}

/* handle evas mouse-out events on client object */
static void
_e_comp_object_cb_mouse_out(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Out *ev = event_info;
   E_Comp_Object *cw = data;

   e_client_mouse_out(cw->ec, ev->output.x, ev->output.y);
}

/* handle evas mouse wheel events on client object */
static void
_e_comp_object_cb_mouse_wheel(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Wheel *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Wheel ev2;

   if (!cw->ec) return;
   if (e_client_action_get()) return;
   e_bindings_evas_event_mouse_wheel_convert(ev, &ev2);
   e_client_mouse_wheel(cw->ec, &ev->output, &ev2);
}

/* handle evas mouse down events on client object */
static void
_e_comp_object_cb_mouse_down(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Down *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Mouse_Button ev2;

   if (!cw->ec) return;
   if (e_client_action_get()) return;
   e_bindings_evas_event_mouse_down_button_convert(ev, &ev2);
   e_client_mouse_down(cw->ec, ev->button, &ev->output, &ev2);
}

/* handle evas mouse up events on client object */
static void
_e_comp_object_cb_mouse_up(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Up *ev = event_info;
   E_Comp_Object *cw = data;
   E_Binding_Event_Mouse_Button ev2;

   if (!cw->ec) return;
   if (e_client_action_get() && (e_client_action_get() != cw->ec)) return;
   e_bindings_evas_event_mouse_up_button_convert(ev, &ev2);
   e_client_mouse_up(cw->ec, ev->button, &ev->output, &ev2);
}

/* handle evas mouse movement events on client object */
static void
_e_comp_object_cb_mouse_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Evas_Event_Mouse_Move *ev = event_info;
   E_Comp_Object *cw = data;

   if (!cw->ec) return;
   if (e_client_action_get() && (e_client_action_get() != cw->ec)) return;
   e_client_mouse_move(cw->ec, &ev->cur.output);
}
/////////////////////////////////////

/* helper function for checking compositor themes based on user-defined matches */
static Eina_Bool
_e_comp_object_shadow_client_match(const E_Client *ec, E_Comp_Match *m)
{
   if (((m->title) && (!ec->netwm.name)) ||
       ((ec->netwm.name) && (m->title) && (!e_util_glob_match(ec->netwm.name, m->title))))
     return EINA_FALSE;
   if (((m->clas) && (!ec->icccm.class)) ||
       ((ec->icccm.class) && (m->clas) && (!e_util_glob_match(ec->icccm.class, m->clas))))
     return EINA_FALSE;
   if (((m->role) && (!ec->icccm.window_role)) ||
       ((ec->icccm.window_role) && (m->role) && (!e_util_glob_match(ec->icccm.window_role, m->role))))
     return EINA_FALSE;
   if (m->primary_type)
     {
        if (ec->netwm.type)
          {
             if ((int)ec->netwm.type != m->primary_type)
               return EINA_FALSE;
          }
        else if (m->primary_type != E_WINDOW_TYPE_REAL_UNKNOWN)
          return EINA_FALSE;
     }
  if (m->borderless != 0)
    {
       int borderless = 0;

       if (e_client_util_borderless(ec))
         borderless = 1;
       if (!(((m->borderless == -1) && (!borderless)) ||
             ((m->borderless == 1) && (borderless))))
         return EINA_FALSE;
    }
  if (m->dialog != 0)
    {
       int dialog = 0;

       if (((ec->icccm.transient_for != 0) ||
            (ec->dialog)))
         dialog = 1;
       if (!(((m->dialog == -1) && (!dialog)) ||
             ((m->dialog == 1) && (dialog))))
         return EINA_FALSE;
    }
  if (m->accepts_focus != 0)
    {
       int accepts_focus = 0;

       if (ec->icccm.accepts_focus)
         accepts_focus = 1;
       if (!(((m->accepts_focus == -1) && (!accepts_focus)) ||
             ((m->accepts_focus == 1) && (accepts_focus))))
         return EINA_FALSE;
    }
  if (m->vkbd != 0)
    {
       int vkbd = 0;

       if (ec->vkbd.vkbd)
         vkbd = 1;
       if (!(((m->vkbd == -1) && (!vkbd)) ||
             ((m->vkbd == 1) && (vkbd))))
         return EINA_FALSE;
    }
  if (m->argb != 0)
    {
       if (!(((m->argb == -1) && (!ec->argb)) ||
             ((m->argb == 1) && (ec->argb))))
         return EINA_FALSE;
    }
  if (m->fullscreen != 0)
    {
       int fullscreen = ec->fullscreen;

       if (!(((m->fullscreen == -1) && (!fullscreen)) ||
             ((m->fullscreen == 1) && (fullscreen))))
         return EINA_FALSE;
    }
  if (m->modal != 0)
    {
       int modal = 0;

       if (ec->netwm.state.modal)
         modal = 1;
       if (!(((m->modal == -1) && (!modal)) ||
             ((m->modal == 1) && (modal))))
         return EINA_FALSE;
    }
  return EINA_TRUE;
}

/* function for setting up a client's compositor frame theme (cw->shobj) */
static Eina_Bool
_e_comp_object_shadow_setup(E_Comp_Object *cw)
{
   int ok = 0;
   char buf[4096];
   Eina_List *list = NULL, *l;
   E_Input_Rect_Data *input_rect_data;
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Comp_Match *m;
   Eina_Stringshare *reshadow_group = NULL;
   Eina_Bool focus = EINA_FALSE, urgent = EINA_FALSE, skip = EINA_FALSE, fast = EINA_FALSE, reshadow = EINA_FALSE, no_shadow = EINA_FALSE, pass_event_flag = EINA_FALSE;
   Eina_Stringshare *name, *title;
   E_Comp_Config *conf = e_comp_config_get();

   edje_object_file_get(cw->shobj, NULL, &reshadow_group);
   /* match correct client type */
   list = cw->ec->override ? conf->match.overrides : conf->match.borders;
   name = cw->ec->icccm.name;
   title = cw->ec->icccm.title;
   skip = (cw->ec->override ? conf->match.disable_overrides : conf->match.disable_borders) || (title && (!strncmp(title, "noshadow", 8)));
   fast = cw->ec->override ? conf->fast_overrides : conf->fast_borders;

   /* skipping here is mostly a hack for systray because I hate it */
   if (!skip)
     {
        EINA_LIST_FOREACH(list, l, m)
          {
             if (((m->name) && (!name)) ||
                 ((name) && (m->name) && (!e_util_glob_match(name, m->name))))
               continue;
             if (!_e_comp_object_shadow_client_match(cw->ec, m)) continue;
             
             focus = m->focus;
             urgent = m->urgent;
             no_shadow = m->no_shadow;
             if (m->shadow_style)
               {
                  /* fast effects are just themes with "/fast" appended and shorter effect times */
                  if (fast)
                    {
                       snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", m->shadow_style);
                       reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                       if (!ok)
                         ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
                    }
                  /* default to non-fast style if fast not available */
                  if (!ok)
                    {
                       snprintf(buf, sizeof(buf), "e/comp/frame/%s", m->shadow_style);
                       reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                       if (!ok)
                         ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
                    }
                  if (ok && m->visibility_effect)
                    eina_stringshare_refplace(&cw->visibility_effect, m->visibility_effect);
                  if (ok) break;
               }
          }
     }
   while (!ok)
     {
        if (skip || (cw->ec->e.state.video))
          {
             reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/none");
             if (!ok)
               ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/none");
          }
        if (ok) break;
        if (conf->shadow_style)
          {
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", conf->shadow_style);
                  reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", conf->shadow_style);
                  reshadow = ok = !e_util_strcmp(reshadow_group, buf);
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", buf);
               }
          }
        if (!ok)
          {
             if (fast)
               {
                  reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/default/fast");
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/default/fast");
               }
             if (!ok)
               {
                  reshadow = ok = !e_util_strcmp(reshadow_group, "e/comp/frame/default");
                  if (!ok)
                    ok = e_theme_edje_object_set(cw->shobj, "base/theme/comp", "e/comp/frame/default");
               }
          }
        break;
     }
   /* reshadow means this entire function call has been a no-op since we're re-setting the current style */
   if (reshadow)
     {
        if (cw->zoomap_disabled)
          {
             if (cw->frame_object && (e_zoomap_child_get(cw->zoomobj) == cw->frame_object)) return EINA_FALSE;
          }
        else
          {
             if (cw->frame_object && (edje_object_part_swallow_get(cw->shobj, "e.swallow.content") == cw->frame_object)) return EINA_FALSE;
          }
     }
   if (cw->ec->override)
     {
        if ((!cw->ec->shaped) && (!no_shadow) && (!cw->ec->argb))
          edje_object_signal_emit(cw->shobj, "e,state,shadow,on", "e");
        else
          edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
        evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
     }
   else
     {
        if (no_shadow)
          {
             edje_object_signal_emit(cw->shobj, "e,state,shadow,off", "e");
             evas_object_smart_callback_call(cw->smart_obj, "shadow_change", cw->ec);
          }
        else
          _e_comp_object_shadow(cw);
     }

   if (focus || cw->ec->focused || cw->ec->override)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,focused", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,unfocused", "e");
   if (urgent || cw->ec->urgent)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,urgent", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,not_urgent", "e");
   if (cw->ec->shaded)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,shaded", "e");
   if (cw->ec->sticky)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,sticky", "e");
   if (cw->ec->hung)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,hung", "e");
   /* visibility must always be enabled for re_manage clients to prevent
    * pop-in animations every time the user sees a persistent client again;
    * applying visibility for iconic clients prevents the client from getting
    * stuck as hidden
    */
   if (cw->visible || cw->ec->re_manage)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,visible", "e");
   else
     e_comp_object_signal_emit(cw->smart_obj, "e,state,hidden", "e");

   /* breaks animation counter */
   if (!cw->zoomap_disabled)
     e_zoomap_child_set(cw->zoomobj, NULL);
   if (cw->frame_object)
     {
        edje_object_part_swallow(cw->frame_object, "e.swallow.client", cw->obj);
        if (cw->zoomap_disabled)
          edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->frame_object);
        else
          {
             e_zoomap_child_set(cw->zoomobj, cw->frame_object);
             edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
          }
        no_shadow = 1;
     }
   else
     {
        no_shadow = 1;
        if (cw->zoomobj)
          {
             e_zoomap_child_set(cw->zoomobj, cw->obj);
             edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
          }
        else
          edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->obj);
     }
   if (cw->input_obj)
     {
        input_rect_sd = evas_object_smart_data_get(cw->input_obj);
        if (input_rect_sd)
          {
             EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
               {
                  if (input_rect_data->obj)
                    {
                       pass_event_flag = EINA_TRUE;
                       break;
                    }
               }
          }
     }
   if (cw->indicator.obj)
     {
        Evas_Object *indicator;
        indicator = edje_object_part_swallow_get(cw->shobj, "e.swallow.indicator");
        if (indicator != cw->indicator.obj)
          {
             edje_object_part_unswallow(cw->shobj, indicator);
             edje_object_part_swallow(cw->shobj, "e.swallow.indicator", cw->indicator.obj);
             e_comp_object_indicator_size_set(cw->smart_obj, cw->indicator.w, cw->indicator.h);
          }
     }

   evas_object_pass_events_set(cw->map_input_obj, pass_event_flag);
   evas_object_pass_events_set(cw->obj, pass_event_flag);
#ifdef BORDER_ZOOMAPS
   e_zoomap_child_edje_solid_setup(cw->zoomobj);
#endif
   return EINA_TRUE;
}

/////////////////////////////////////////////

static void
_e_comp_object_animating_begin(E_Comp_Object *cw)
{
   cw->animating++;
   if (cw->animating == 1)
     {
        e_comp->animating++;
        REFD(cw->ec, 2);
        e_object_ref(E_OBJECT(cw->ec));
     }
}

static Eina_Bool
_e_comp_object_animating_end(E_Comp_Object *cw)
{
   if (cw->animating)
     {
        cw->animating--;
        if (!cw->animating)
          {
             if (cw->ec)
               {
                  if (cw->ec->launching)
                    {
                       if (!cw->ec->extra_animating)
                         {
                            ELOGF("COMP", "Un-Set launching flag..", cw->ec);
                            cw->ec->launching = EINA_FALSE;
                            if (cw->ec->first_mapped)
                              {
                                 ELOGF("LAUNCH", "SHOW real win", cw->ec);
                                 e_comp_object_signal_emit(cw->ec->frame, "e,action,launch_real,done", "e");
                              }

                            e_comp_object_signal_emit(cw->ec->frame, "e,action,launch,done", "e");
                         }
                    }
               }
             e_comp->animating--;
             cw->showing = 0;
             UNREFD(cw->ec, 2);
             if (e_comp->animating == 0)
               e_client_visibility_calculate();
             /* remove ref from animation start, account for possibility of deletion from unref */
             return !!e_object_unref(E_OBJECT(cw->ec));
          }
     }
   return EINA_TRUE;
}

/* handle the end of a compositor animation */
static void
_e_comp_object_done_defer(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   /* visible clients which have never been sized are a bug */
   if ((!cw->ec->new_client) && (!cw->ec->changes.size) && ((cw->w < 0) || (cw->h < 0)) && (!strcmp(emission, "e,action,show,done")))
     CRI("ACK!");
   if (!_e_comp_object_animating_end(cw)) return;
   if (cw->animating) return;
   /* hide only after animation finishes to guarantee a full run of the animation */
   if (cw->defer_hide && ((!strcmp(emission, "e,action,hide,done")) || (!strcmp(emission, "e,action,done"))))
     evas_object_hide(cw->smart_obj);
}

/* run a visibility compositor effect if available, return false if object is dead */
static Eina_Bool
_e_comp_object_effect_visibility_start(E_Comp_Object *cw, Eina_Bool state)
{
   int x, y, zw, zh;

   if ((!cw->visibility_effect) || (!e_comp_object_effect_allowed_get(cw->smart_obj))) return EINA_TRUE;;
   if (!cw->effect_running)
     _e_comp_object_animating_begin(cw);
   if (!e_comp_object_effect_stop(cw->smart_obj, _e_comp_object_done_defer))
     return _e_comp_object_animating_end(cw);
   if (!e_comp_object_effect_set(cw->smart_obj, cw->visibility_effect))
     return _e_comp_object_animating_end(cw);

   evas_pointer_canvas_xy_get(e_comp->evas, &x, &y);
   x -= cw->x;
   y -= cw->y;
   if (cw->ec->zone)
     zw = cw->ec->zone->w, zh = cw->ec->zone->h;
   else
     {
        E_Zone *zone;

        zone = e_comp_object_util_zone_get(cw->smart_obj);
        if (!zone) zone = e_zone_current_get();
        zw = zone->w, zh = zone->h;
     }
   e_comp_object_effect_params_set(cw->smart_obj, 1, (int[]){cw->x, cw->y,
      cw->w, cw->h, zw, zh, x, y}, 8);
   e_comp_object_effect_params_set(cw->smart_obj, 0, (int[]){state}, 1);
   e_comp_object_effect_start(cw->smart_obj, _e_comp_object_done_defer, cw);
   return EINA_TRUE;
}
/////////////////////////////////////////////

/* create necessary objects for clients that e manages */
static void
_e_comp_object_setup(E_Comp_Object *cw)
{
   cw->clip = evas_object_rectangle_add(e_comp->evas);
   evas_object_move(cw->clip, -9999, -9999);
   evas_object_resize(cw->clip, 999999, 999999);
   evas_object_smart_member_add(cw->clip, cw->smart_obj);
   cw->effect_obj = edje_object_add(e_comp->evas);
   evas_object_move(cw->effect_obj, cw->x, cw->y);
   evas_object_clip_set(cw->effect_obj, cw->clip);
   evas_object_smart_member_add(cw->effect_obj, cw->smart_obj);
   e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", "e/comp/effects/none");
   cw->shobj = edje_object_add(e_comp->evas);
   evas_object_data_set(cw->shobj, "comp_smart_obj", cw->smart_obj);
   edje_object_part_swallow(cw->effect_obj, "e.swallow.content", cw->shobj);
   edje_object_signal_callback_add(cw->shobj, "e,action,*,done", "e", _e_comp_object_done_defer, cw);

   /* name objects appropriately for nicer printing when using e_comp_util_wins_print() */
   if (cw->ec->override)
     {
        evas_object_name_set(cw->shobj, "cw->shobj::WINDOW");
        evas_object_name_set(cw->effect_obj, "cw->effect_obj::WINDOW");
        evas_object_name_set(cw->clip, "cw->clip::WINDOW");
     }
   else if (!cw->ec->input_only)
     {
        evas_object_name_set(cw->shobj, "cw->shobj::CLIENT");
        evas_object_name_set(cw->effect_obj, "cw->effect_obj::CLIENT");
        evas_object_name_set(cw->clip, "cw->clip::CLIENT");
     }
   cw->real_hid = !cw->ec->input_only;
   if (!cw->ec->input_only)
     {
        e_util_size_debug_set(cw->effect_obj, 1);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_IN, _e_comp_object_cb_mouse_in, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_OUT, _e_comp_object_cb_mouse_out, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_DOWN, _e_comp_object_cb_mouse_down, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_UP, _e_comp_object_cb_mouse_up, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_MOVE, _e_comp_object_cb_mouse_move, cw);
        evas_object_event_callback_add(cw->smart_obj, EVAS_CALLBACK_MOUSE_WHEEL, _e_comp_object_cb_mouse_wheel, cw);
     }

   cw->map_input_obj = evas_object_rectangle_add(e_comp->evas);
   evas_object_name_set(cw->map_input_obj, "cw->map_input_obj");
   evas_object_move(cw->map_input_obj, cw->x, cw->y);
   evas_object_resize(cw->map_input_obj, cw->w, cw->h);
   evas_object_color_set(cw->map_input_obj, 0, 0, 0, 0);
   evas_object_smart_member_add(cw->map_input_obj, cw->smart_obj);
}

/////////////////////////////////////////////

/* for fast path evas rendering; only called during render */
static void
_e_comp_object_pixels_get(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   E_Client *ec = cw->ec;
   int pw, ph;
   int bx, by, bxx, byy;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (cw->external_content) return;
   if (!e_pixmap_size_get(ec->pixmap, &pw, &ph)) return;
   e_pixmap_image_opaque_get(cw->ec->pixmap, &bx, &by, &bxx, &byy);
   if (bxx && byy)
     {
        bxx = pw - (bx + bxx), byy = ph - (by + byy);
        evas_object_image_border_set(cw->obj, bx, bxx, by, byy);
     }
   else if (cw->client_inset.calc && (!cw->frame_object)) //CSD
     {
        bx = -cw->client_inset.l + 4, by = -cw->client_inset.t + 4;
        bxx = -cw->client_inset.r, byy = -cw->client_inset.b;
     }
   else
     {
        bx = by = bxx = byy = 0;
        evas_object_image_border_set(cw->obj, bx, bxx, by, byy);
     }
   {
      Edje_Message_Int_Set *msg;
      Edje_Message_Int msg2;
      Eina_Bool id = (bx || by || bxx || byy);

      msg = alloca(sizeof(Edje_Message_Int_Set) + (sizeof(int) * 3));
      msg->count = 4;
      msg->val[0] = bx;
      msg->val[1] = by;
      msg->val[2] = bxx;
      msg->val[3] = byy;
      edje_object_message_send(cw->shobj, EDJE_MESSAGE_INT_SET, 1, msg);
      msg2.val = id;
      edje_object_message_send(cw->shobj, EDJE_MESSAGE_INT, 0, &msg2);
   }
   if (cw->native)
     {
        E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
        e_comp_client_post_update_add(cw->ec);
     }
   else if (e_comp_object_render(ec->frame))
     {
        /* apply shape mask if necessary */
        if ((!cw->native) && (ec->shaped || ec->shape_changed))
          e_comp_object_shape_apply(ec->frame);
        ec->shape_changed = 0;
     }
   /* shaped clients get precise mouse events to handle transparent pixels */
   evas_object_precise_is_inside_set(cw->obj, ec->shaped || ec->shaped_input);

   /* queue another render if client is still dirty; cannot refresh here. */
   if (e_pixmap_dirty_get(ec->pixmap) && e_pixmap_size_get(ec->pixmap, &pw, &ph))
     e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);

   if (cw->render_trace)
     {
        _e_comp_object_event_simple(ec->frame, E_EVENT_COMP_OBJECT_IMG_RENDER);
     }
}


static void
_e_comp_object_pixels_noti(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   E_Client *ec = cw->ec;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (cw->external_content) return;
   if (!e_comp->hwc) return;

   e_comp_client_render_list_add(cw->ec);

   if (!ec->hwc_window) return;

   e_hwc_windows_rendered_window_add(ec->hwc_window);
}

/////////////////////////////////////////////

static void
_e_comp_object_client_pending_resize_add(E_Client *ec,
                                         int w,
                                         int h,
                                         unsigned int serial)
{
   E_Client_Pending_Resize *pnd;

   pnd = E_NEW(E_Client_Pending_Resize, 1);
   if (!pnd) return;
   pnd->w = w;
   pnd->h = h;
   pnd->serial = serial;
   ec->pending_resize = eina_list_append(ec->pending_resize, pnd);
}

static void
_e_comp_intercept_move(void *data, Evas_Object *obj, int x, int y)
{
   E_Comp_Object *cw = data;
   int ix, iy, fx, fy;

   if ((e_pixmap_type_get(cw->ec->pixmap) != E_PIXMAP_TYPE_EXT_OBJECT) &&
       (e_pixmap_usable_get(cw->ec->pixmap)) &&
       (cw->external_content))
     {
        /* delay to move until the external content is unset */
        cw->ec->changes.pos = 1;
        EC_CHANGED(cw->ec);
        return;
     }

   /* if frame_object does not exist, client_inset indicates CSD.
    * this means that ec->client matches cw->x/y, the opposite
    * of SSD.
    */
   fx = (!cw->frame_object) * cw->client_inset.l;
   fy = (!cw->frame_object) * cw->client_inset.t;
   if ((cw->x == x + fx) && (cw->y == y + fy))
     {
        if ((cw->ec->x != x) || (cw->ec->y != y))
          {
             /* handle case where client tries to move to position and back very quickly */
             cw->ec->x = x, cw->ec->y = y;
             cw->ec->client.x = x + cw->client_inset.l;
             cw->ec->client.y = y + cw->client_inset.t;
          }
        return;
     }
   if (!cw->ec->maximize_override)
     {
        /* prevent moving in some directions while directionally maximized */
        if ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_VERTICAL)
          y = cw->y;
        if ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_HORIZONTAL)
          x = cw->x;
     }
   ix = x + cw->client_inset.l;
   iy = y + cw->client_inset.t;
   if (cw->ec->maximized && (!cw->ec->maximize_override) && ((cw->ec->x != x) || (cw->ec->y != y)) &&
       ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) != E_MAXIMIZE_VERTICAL) &&
       ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) != E_MAXIMIZE_HORIZONTAL))
     {
        /* prevent moving at all if move isn't allowed in current maximize state */
        if ((!e_config->allow_manip) && ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_BOTH)) return;
        /* queue unmaximize if we are allowing move and update unmaximize geometry */
        if ((!cw->ec->shading) && (!cw->ec->shaded))
          {
             cw->ec->changes.need_unmaximize = 1;
             cw->ec->saved.x = ix - cw->ec->zone->x;
             cw->ec->saved.y = iy - cw->ec->zone->y;
             cw->ec->saved.w = cw->ec->client.w;
             cw->ec->saved.h = cw->ec->client.h;
             EC_CHANGED(cw->ec);
             return;
          }
        return;
     }
   /* only update during resize if triggered by resize */
   if (e_client_util_resizing_get(cw->ec) && (!cw->force_move)) return;
   /* delay to move while surface waits paired commit serial*/
   if (e_client_pending_geometry_has(cw->ec))
     {
        cw->ec->changes.pos = 1;
        EC_CHANGED(cw->ec);
        return;
     }

   cw->ec->x = x, cw->ec->y = y;
   if (cw->ec->new_client)
     {
        /* don't actually do anything until first client idler loop */
        cw->ec->placed = ((!cw->ec->dialog) && (!cw->ec->parent));
        cw->ec->changes.pos = 1;
        EC_CHANGED(cw->ec);
     }
   else
     {
        /* only update xy position of client to avoid invalid
         * first damage region if it is not a new_client. */
        if (!cw->ec->shading)
          {
             cw->ec->client.x = ix;
             cw->ec->client.y = iy;
          }
        /* flip SSD->CSD */
        if (!cw->frame_object)
          x = ix, y = iy;
        evas_object_move(obj, x, y);
     }
}

static void
_e_comp_intercept_resize(void *data, Evas_Object *obj, int w, int h)
{
   E_Comp_Object *cw = data;
   int pw = 0, ph = 0, fw, fh, iw, ih, prev_w, prev_h, x, y;

   if (!e_util_strcmp("input_panel_surface", cw->ec->icccm.window_role))
     {
        cw->ec->w = w, cw->ec->h = h;
        evas_object_resize(obj, w, h);
        return;
     }

   /* if frame_object does not exist, client_inset indicates CSD.
    * this means that ec->client matches cw->w/h, the opposite
    * of SSD.
    */
   fw = (!cw->frame_object) * (-cw->client_inset.l - cw->client_inset.r);
   fh = (!cw->frame_object) * (-cw->client_inset.t - cw->client_inset.b);
   if ((cw->w == w + fw) && (cw->h == h + fh))
     {
        if (cw->ec->shading || cw->ec->shaded) return;
        if (((cw->ec->w != w) || (cw->ec->h != h)) ||
            (cw->ec->client.w != w - cw->client_inset.l - cw->client_inset.r) ||
            (cw->ec->client.h != h - cw->client_inset.t - cw->client_inset.b))
          {
             /* handle case where client tries to resize itself and back very quickly */
             cw->ec->w = w, cw->ec->h = h;
             cw->ec->client.w = w - cw->client_inset.l - cw->client_inset.r;
             cw->ec->client.h = h - cw->client_inset.t - cw->client_inset.b;
             evas_object_smart_callback_call(obj, "client_resize", NULL);
          }
        return;
     }
   /* guarantee that fullscreen is fullscreen */
   if (cw->ec->fullscreen && ((w != cw->ec->zone->w) || (h != cw->ec->zone->h)))
     return;
   /* calculate client size */
   iw = w - cw->client_inset.l - cw->client_inset.r;
   ih = h - cw->client_inset.t - cw->client_inset.b;
   if (cw->ec->maximized && (!cw->ec->maximize_override) && ((cw->ec->w != w) || (cw->ec->h != h)))
     {
        /* prevent resizing while maximized depending on direction and config */
        if ((!e_config->allow_manip) && ((cw->ec->maximized & E_MAXIMIZE_DIRECTION) == E_MAXIMIZE_BOTH)) return;
        if ((!cw->ec->shading) && (!cw->ec->shaded))
          {
             Eina_Bool reject = EINA_FALSE;
             if (cw->ec->maximized & E_MAXIMIZE_VERTICAL)
               {
                  if (cw->ec->client.h != ih)
                    {
                       cw->ec->saved.h = ih;
                       cw->ec->saved.y = cw->ec->client.y - cw->ec->zone->y;
                       reject = cw->ec->changes.need_unmaximize = 1;
                    }
               }
             if (cw->ec->maximized & E_MAXIMIZE_HORIZONTAL)
               {
                  if (cw->ec->client.w != iw)
                    {
                       cw->ec->saved.w = iw;
                       cw->ec->saved.x = cw->ec->client.x - cw->ec->zone->x;
                       reject = cw->ec->changes.need_unmaximize = 1;
                    }
               }
             if (reject)
               {
                  EC_CHANGED(cw->ec);
                  return;
               }
          }
     }
   if (cw->ec->new_client || (!cw->ec->visible) || (!cw->effect_obj))
     {
        /* do nothing until client idler loops */
        if (!cw->ec->maximized && (cw->ec->w != w || cw->ec->h != h))
          {
             cw->ec->w = w, cw->ec->h = h;
             cw->ec->changes.size = 1;
             EC_CHANGED(cw->ec);
          }
        return;
     }
   if ((!cw->ec->internal) && e_client_util_resizing_get(cw->ec) && cw->ec->netwm.sync.request &&
       ((cw->ec->w != w) || (cw->ec->h != h)))
     {
        /* this is ugly. */
        /* netwm sync resizes queue themselves and then trigger later on */
        _e_comp_object_client_pending_resize_add(cw->ec, iw, ih, cw->ec->netwm.sync.serial);
     }
   if (e_client_pending_geometry_has(cw->ec))
     {
        /* do nothing while waiting paired commit serial*/
        return;
     }

   cw->ec->w = w, cw->ec->h = h;
   if ((!cw->ec->shading) && (!cw->ec->shaded))
     {
        /* client geom never changes when shading since the client is never altered */
        cw->ec->client.w = iw;
        cw->ec->client.h = ih;
        if ((cw->ec->client.w < 0) || (cw->ec->client.h < 0)) CRI("WTF");
     }

   /* The size of non-compositing window can be changed, so there is a
    * need to check that cw is H/W composited if cw is not redirected.
    * And of course we have to change size of evas object of H/W composited cw,
    * otherwise cw can't receive input events even if it is shown on the screen.
    */
   Eina_Bool redirected = cw->redirected;
   if (!redirected)
     redirected = e_comp_is_on_overlay(cw->ec);

   if ((!cw->ec->input_only) && (redirected) &&
       (cw->content_type != E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE) &&
       (cw->content_type != E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE) &&
       (e_pixmap_dirty_get(cw->ec->pixmap) ||
       (!e_pixmap_size_get(cw->ec->pixmap, &pw, &ph))))
     {
        if (e_comp->comp_type != E_PIXMAP_TYPE_X) return;
        /* client can't be resized if its pixmap isn't usable, try again */
        e_pixmap_dirty(cw->ec->pixmap);
        e_comp_object_render_update_add(obj);
        e_comp_render_queue();
        cw->ec->changes.size = 1;
        EC_CHANGED(cw->ec);
        return;
     }
   if (cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE ||
       cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE)
     pw = w, ph = h;
   prev_w = cw->w, prev_h = cw->h;
   e_comp_object_frame_wh_adjust(obj, 0, 0, &fw, &fh);
   /* check shading and clamp to pixmap size for regular clients */
   if ((!cw->ec->shading) && (!cw->ec->shaded) && (!cw->ec->input_only) && (!cw->ec->override) &&
       (((w - fw != pw) || (h - fh != ph))))
     {
        //INF("CALLBACK: REQ(%dx%d) != CUR(%dx%d)", w - fw, h - fh, pw, ph);
        evas_object_smart_callback_call(obj, "client_resize", NULL);
        /* flip for CSD */
        if (cw->frame_object || cw->ec->input_only)
          e_comp_object_frame_wh_adjust(obj, pw, ph, &w, &h);
        else
          w = pw, h = ph;
        if ((cw->w == w) && (cw->h == h))
          {
             /* going to be a noop resize which won't trigger smart resize */
             RENDER_DEBUG("DAMAGE RESIZE(%p): %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
             if (cw->updates) eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
          }
        evas_object_resize(obj, w, h);
     }
   else
     {
        /* flip for CSD */
        if ((!cw->frame_object) && (!cw->ec->input_only))
          w = pw, h = ph;
        /* "just do it" for overrides */
        evas_object_resize(obj, w, h);
     }
   if (!cw->ec->override)
     {
        /* shape probably changed for non-overrides */
        cw->ec->need_shape_merge |= cw->ec->shaped || cw->ec->shaped_input;
        cw->ec->need_shape_export |= cw->ec->shaped;
        if (cw->ec->shaped || cw->ec->shaped_input)
          EC_CHANGED(cw->ec);
     }

   /* this fixes positioning jiggles when using a resize mode
    * which also changes the client's position
    */
   cw->force_move = 1;
   if (cw->frame_object)
     x = cw->x, y = cw->y;
   else
     x = cw->ec->x, y = cw->ec->y;
   switch (cw->ec->resize_mode)
     {
      case E_POINTER_RESIZE_BL:
      case E_POINTER_RESIZE_L:
        evas_object_move(obj, x + prev_w - cw->w, y);
        break;
      case E_POINTER_RESIZE_TL:
        evas_object_move(obj, x + prev_w - cw->w, y + prev_h - cw->h);
        break;
      case E_POINTER_RESIZE_T:
      case E_POINTER_RESIZE_TR:
        evas_object_move(obj, x, y + prev_h - cw->h);
        break;
      default:
        break;
     }
   cw->force_move = 0;
}

static void
_e_comp_intercept_layer_set(void *data, Evas_Object *obj, int layer)
{
   E_Comp_Object *cw = data;
   unsigned int l = e_comp_canvas_layer_map(layer);
   int oldraise;

   if ((cw->ec->layer_block) || (cw->ec->layer_pending))
     {
        /* doing a compositor effect, follow directions */
        evas_object_layer_set(obj, layer);
        if (layer == cw->ec->layer) //trying to put layer back
          {
             E_Client *ec;

             if (cw->visible)
               {
                  e_comp_render_queue();
               }
             ec = e_client_above_get(cw->ec);
             /* skip subsurface: stacking subsurface is handled by e_comp_wl */
             while ((ec) && (ec->comp_data) && (ec->comp_data->sub.data))
               ec = e_client_above_get(ec);
             if (ec && (evas_object_layer_get(ec->frame) != evas_object_layer_get(obj)))
               {
                  ec = e_client_below_get(cw->ec);
                  /* skip subsurface: stacking subsurface is handled by e_comp_wl */
                  while ((ec) && (ec->comp_data) && (ec->comp_data->sub.data))
                    ec = e_client_below_get(ec);
                  if (ec && (evas_object_layer_get(ec->frame) == evas_object_layer_get(cw->smart_obj)))
                    {
                       evas_object_stack_above(obj, ec->frame);
                       return;
                    }
                  ec = NULL;
               }
             if (ec && (cw->ec->parent == ec))
               {
                  if (cw->ec->transient_policy == E_TRANSIENT_ABOVE)
                    evas_object_stack_above(obj, ec->frame);
                  else
                    evas_object_stack_below(obj, ec->frame);
               }
             else
               evas_object_stack_below(obj, ec ? ec->frame : e_comp->layers[cw->layer].obj);
          }
        return;
     }
   if (cw->layer == l) return;
   if (e_comp_canvas_client_layer_map(layer) == 9999)
     return; //invalid layer for clients not doing comp effects
   if (cw->ec->fullscreen)
     {
        cw->ec->saved.layer = layer;
        return;
     }
   oldraise = e_config->transient.raise;
   _e_comp_object_layers_remove(cw);
   /* clamp to valid client layer */
   layer = e_comp_canvas_client_layer_map_nearest(layer);
   cw->ec->layer = layer;
   if (e_config->transient.layer)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(cw->ec->transients);

        /* We need to set raise to one, else the child wont
         * follow to the new layer. It should be like this,
         * even if the user usually doesn't want to raise
         * the transients.
         */
        e_config->transient.raise = 1;
        EINA_LIST_FREE(list, child)
          evas_object_layer_set(child->frame, layer);
     }
   if (!cw->ec->override)
     {
        /* set client stacking hints based on layer */
        if (layer == E_LAYER_CLIENT_BELOW)
          e_hints_window_stacking_set(cw->ec, E_STACKING_BELOW);
        else if (layer == E_LAYER_CLIENT_ABOVE)
          e_hints_window_stacking_set(cw->ec, E_STACKING_ABOVE);
        else
          e_hints_window_stacking_set(cw->ec, E_STACKING_NONE);
     }

   e_config->transient.raise = oldraise;
   cw->layer = e_comp_canvas_layer_map(layer);
   _e_comp_object_layers_add(cw, NULL, NULL, 0);
   //if (cw->ec->new_client)
     //INF("CLIENT STACKED %p: %u", cw->ec, layer);
   evas_object_layer_set(obj, layer);
   if (!e_comp->layers[cw->layer].obj) return; //this is a layer marker
   evas_object_stack_below(obj, e_comp->layers[cw->layer].obj);
   if (evas_object_below_get(obj) == e_comp->layers[cw->layer].obj)
     {
        /* can't stack a client above its own layer marker */
        CRI("STACKING ERROR!!!");
     }
   if (!cw->visible) return;
   e_comp_render_queue();
   _e_comp_object_transform_obj_stack_update(obj);
}

typedef void (*E_Comp_Object_Stack_Func)(Evas_Object *obj, Evas_Object *stack);

static void
_e_comp_object_raise(Evas_Object *obj)
{
   evas_object_raise(obj);

   E_Client *ec = e_comp_object_client_get(obj);
   if (ec)
     _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_RESTACK, ec);
}

static void
_e_comp_object_lower(Evas_Object *obj)
{
   evas_object_lower(obj);

   E_Client *ec = e_comp_object_client_get(obj);
   if (ec)
     _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_RESTACK, ec);
}

static void
_e_comp_object_stack_above(Evas_Object *obj, Evas_Object *target)
{
   evas_object_stack_above(obj, target);

   E_Client *ec = e_comp_object_client_get(obj);
   if (ec)
     _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_RESTACK, ec);
}

static void
_e_comp_object_stack_below(Evas_Object *obj, Evas_Object *target)
{
   evas_object_stack_below(obj, target);

   E_Client *ec = e_comp_object_client_get(obj);
   if (ec)
     _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_RESTACK, ec);
}

static Eina_Bool
_e_comp_object_is_pending(E_Client *ec)
{
   E_Client *topmost;

   if (!ec) return EINA_FALSE;

   topmost = e_comp_wl_topmost_parent_get(ec);

   return (topmost) ? topmost->layer_pending : EINA_FALSE;
}

static void
_e_comp_intercept_stack_helper(E_Comp_Object *cw, Evas_Object *stack, E_Comp_Object_Stack_Func stack_cb)
{
   E_Comp_Object *cw2 = NULL;
   E_Client *ecstack;
   short layer;
   Evas_Object *o = stack;
   Eina_Bool raising = stack_cb == _e_comp_object_stack_above;

   /* We should consider topmost's layer_pending for subsurface */
   if ((cw->ec->layer_block) || _e_comp_object_is_pending(cw->ec))
     {
        if (_e_comp_object_is_pending(cw->ec))
          e_comp_object_layer_update(cw->smart_obj,
                                     raising? stack : NULL,
                                     raising? NULL : stack);

        /* obey compositor effects! */
        if (cw->ec->layer == evas_object_layer_get(cw->smart_obj))
          evas_object_data_set(cw->smart_obj, "client_restack", (void*)1);
        stack_cb(cw->smart_obj, stack);
        if (cw->ec->layer == evas_object_layer_get(cw->smart_obj))
          evas_object_data_del(cw->smart_obj, "client_restack");
        return;
     }

   cw2 = evas_object_data_get(o, "comp_obj");

   /* assume someone knew what they were doing during client init */
   if (cw->ec->new_client)
     layer = cw->ec->layer;
   else if ((cw2) && _e_comp_object_is_pending(cw2->ec))
     layer = cw2->ec->layer;
   else
     layer = evas_object_layer_get(stack);
   ecstack = e_client_below_get(cw->ec);
   if (layer != e_comp_canvas_layer_map_to(cw->layer))
     {
        /* some FOOL is trying to restack a layer marker */
        if (cw->smart_obj == e_comp->layers[cw->layer].obj) return;
        evas_object_layer_set(cw->smart_obj, layer);
        /* we got our layer wrangled, return now! */
        if (layer != e_comp_canvas_layer_map_to(cw->layer)) return;
     }

   /* check if we're stacking below another client */
   while (!cw2)
     {
        /* check for non-client layer object */
        if (!e_util_strcmp(evas_object_name_get(o), "layer_obj"))
          break;
        /* find an existing client to use for layering
         * by walking up the object stack
         *
         * this is guaranteed to be pretty quick since we'll either:
         * - run out of client layers
         * - find a stacking client
         */
        o = evas_object_above_get(o);
        if ((!o) || (o == cw->smart_obj)) break;
        if (evas_object_layer_get(o) != layer)
          {
             /* reached the top client layer somehow
              * use top client object
              */
             o = e_comp->layers[e_comp_canvas_layer_map(E_LAYER_CLIENT_CURSOR)].obj;
          }
        if (!o)
          /* top client layer window hasn't been stacked yet. this probably shouldn't happen?
           * return here since the top client layer window 
           */
          {
             E_Client *ec;

             ec = e_client_top_get();
             if (ec)
               o = ec->frame;
             //else //wat
          }
        if (o) cw2 = evas_object_data_get(o, "comp_obj");
     }

   if (cw2 && cw->layer != cw2->layer)
     return;

   /* remove existing layers */
   _e_comp_object_layers_remove(cw);
   if (cw2)
     {
        if (o == stack) //if stacking above, cw2 is above; else cw2 is below
          _e_comp_object_layers_add(cw, raising ? cw2 : NULL, raising ? NULL : cw2, 0);
        else if (o == cw->smart_obj) //prepend (lower) if not stacking above
          _e_comp_object_layers_add(cw, NULL, NULL, !raising);
        else //if no stacking objects found, either raise or lower
          _e_comp_object_layers_add(cw, raising ? NULL : cw2, raising ? cw2 : NULL, 0);
     }
   else
     _e_comp_object_layers_add(cw, NULL, NULL, 0);

   /* find new object for stacking if cw2 is on state of layer_pending */
   if ((cw2) && _e_comp_object_is_pending(cw2->ec))
     {
        E_Client *new_stack = NULL, *current_ec = NULL;
        current_ec = cw2->ec;
        if (raising)
          {
             while ((new_stack = e_client_below_get(current_ec)))
               {
                  current_ec = new_stack;
                  if (new_stack == cw->ec) continue;
                  if (new_stack->layer != cw2->ec->layer) break;
                  if (!_e_comp_object_is_pending(new_stack)) break;
               }
             if ((new_stack) && (new_stack->layer == cw2->ec->layer))
               stack = new_stack->frame;
             else
               {
                  /* stack it above layer object */
                  int below_layer;
                  below_layer = (cw2->layer <= 0)? 0 : cw2->layer - 1 ;
                  stack = e_comp->layers[below_layer].obj;
               }
          }
        else
          {
             while ((new_stack = e_client_above_get(current_ec)))
               {
                  current_ec = new_stack;
                  if (new_stack == cw->ec) continue;
                  if (new_stack->layer != cw2->ec->layer) break;
                  if (!_e_comp_object_is_pending(new_stack)) break;
               }
             if ((new_stack) && (new_stack->layer == cw2->ec->layer))
               stack = new_stack->frame;
             else
               stack = e_comp->layers[cw2->layer].obj;
          }
     }

   /* set restack if stacking has changed */
   if (cw->ec->new_client || (!ecstack) || (ecstack->frame != o))
     evas_object_data_set(cw->smart_obj, "client_restack", (void*)1);
   stack_cb(cw->smart_obj, stack);
   if (e_comp->layers[cw->layer].obj)
     if (evas_object_below_get(cw->smart_obj) == e_comp->layers[cw->layer].obj)
       {
          CRI("STACKING ERROR!!!");
       }
   if (cw->ec->new_client || (!ecstack) || (ecstack->frame != o))
     evas_object_data_del(cw->smart_obj, "client_restack");
   if (!cw->visible) return;
   e_comp_render_queue();
}

static void
_e_comp_intercept_stack_above(void *data, Evas_Object *obj, Evas_Object *above)
{
   EINA_SAFETY_ON_TRUE_RETURN(obj == above);

   if (evas_object_below_get(obj) == above)
     {
        e_comp_object_layer_update(obj, above, NULL);
        return;
     }

   TRACE_DS_BEGIN(COMP:INTERCEPT STACK ABOVE);
   _e_comp_intercept_stack_helper(data, above, _e_comp_object_stack_above);
   _e_comp_object_transform_obj_stack_update(obj);
   _e_comp_object_transform_obj_stack_update(above);
   TRACE_DS_END();
}

static void
_e_comp_intercept_stack_below(void *data, Evas_Object *obj, Evas_Object *below)
{
   EINA_SAFETY_ON_TRUE_RETURN(obj == below);
   if (evas_object_above_get(obj) == below)
     {
        e_comp_object_layer_update(obj, NULL, below);
        return;
     }

   TRACE_DS_BEGIN(COMP:INTERCEPT STACK BELOW);
   _e_comp_intercept_stack_helper(data, below, _e_comp_object_stack_below);
   _e_comp_object_transform_obj_stack_update(obj);
   _e_comp_object_transform_obj_stack_update(below);
   TRACE_DS_END();
}

static void
_e_comp_intercept_lower(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;
   Evas_Object *o;

   TRACE_DS_BEGIN(COMP:INTERCEPT LOWER);

   if ((cw->ec->layer_block) || (cw->ec->layer_pending))
     {
        if (cw->ec->layer_pending)
          e_comp_object_layer_update(obj, NULL, obj);

        _e_comp_object_lower(obj);
        goto end;
     }
   if (!EINA_INLIST_GET(cw->ec)->prev) goto end; //already lowest on layer
   o = evas_object_below_get(obj);
   _e_comp_object_layers_remove(cw);
   /* prepend to client list since this client should be the first item now */
   _e_comp_object_layers_add(cw, NULL, NULL, 1);
   if (evas_object_layer_get(o) != evas_object_layer_get(obj)) goto end; //already at bottom!
   if (obj == e_comp->layers[cw->layer].obj) goto end; //never lower a layer marker!
   evas_object_data_set(obj, "client_restack", (void*)1);
   _e_comp_object_lower(obj);
   evas_object_data_del(obj, "client_restack");
   if (!cw->visible) goto end;
   e_comp_render_queue();
   _e_comp_object_transform_obj_stack_update(obj);

end:
   e_client_focus_stack_lower(cw->ec);
   TRACE_DS_END();
}

static void
_e_comp_intercept_raise(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;
   Evas_Object *o;

   TRACE_DS_BEGIN(COMP:INTERCEPT RAISE);

   if ((cw->ec->layer_block) || (cw->ec->layer_pending))
     {
        if (cw->ec->layer_pending)
          {
             int obj_layer = evas_object_layer_get(obj);
             if (cw->ec->layer != obj_layer)
               e_comp_object_layer_update(obj, NULL, NULL);
          }

        _e_comp_object_raise(obj);
        goto end;
     }
   if (!EINA_INLIST_GET(cw->ec)->next) goto end;//already highest on layer
   o = evas_object_above_get(obj);
   {
      E_Client *ecabove = e_client_above_get(cw->ec);
      if (ecabove && (ecabove->frame == e_comp->layers[cw->layer].obj) &&
          (ecabove->frame == o)) goto end; //highest below marker
   }
   if (evas_object_layer_get(o) != evas_object_layer_get(obj)) goto end; //already at top!
   if (obj == e_comp->layers[cw->layer].obj) //never raise a non-layer marker!
     _e_comp_object_raise(obj);
   else
     {
        Evas_Object *op;

        /* still stack below override below the layer marker */
        for (op = o = e_comp->layers[cw->layer].obj;
             o && o != e_comp->layers[cw->layer - 1].obj;
             op = o, o = evas_object_below_get(o))
          {
             E_Client *ec;

             ec = e_comp_object_client_get(o);
             if (ec && (!ec->override)) break;
          }
        _e_comp_object_stack_below(obj, op);
        e_client_focus_defer_set(cw->ec);
     }
   if (!cw->visible) goto end;
   e_comp_render_queue();
   _e_comp_object_transform_obj_stack_update(obj);

end:
   TRACE_DS_END();
}

static void
_e_comp_intercept_hide(void *data, Evas_Object *obj)
{
   E_Comp_Object *cw = data;

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
   if( !_e_comp_object_intercept_hook_call(E_COMP_OBJECT_INTERCEPT_HOOK_HIDE, cw->ec)) return;
#endif

   if (cw->ec->launching == EINA_TRUE)
     {
        ELOGF("COMP", "Hide. Cancel launching flag", cw->ec);
        cw->ec->launching = EINA_FALSE;
     }

   if (cw->ec->hidden)
     {
        /* hidden flag = just do it */
        evas_object_hide(obj);
        return;
     }

   if (cw->ec->input_only)
     {
        /* input_only = who cares */
        evas_object_hide(obj);
        return;
     }
   /* already hidden or currently animating */
   if ((!cw->visible) || (cw->animating && (!cw->showing) && (!cw->ec->iconic))) return;

   /* don't try hiding during shutdown */
   cw->defer_hide |= stopping;
   if (!cw->defer_hide)
     {
        if ((!cw->ec->iconic) && (!cw->ec->override))
          /* unset delete requested so the client doesn't break */
          cw->ec->delete_requested = 0;
        if ((!cw->animating) || cw->showing || cw->ec->iconic)
          {
             if (cw->ec->iconic)
               e_comp_object_signal_emit(obj, "e,action,iconify", "e");
             else
               {
                  e_comp_object_signal_emit(obj, "e,state,hidden", "e");
                  if (!cw->showing)
                    _e_comp_object_animating_begin(cw);
                  if (!_e_comp_object_effect_visibility_start(cw, 0)) return;
               }
             evas_object_smart_callback_call(obj, "hiding", cw->ec);
             cw->defer_hide = !!cw->animating;
             if (!cw->animating)
               e_comp_object_effect_set(obj, NULL);
          }
     }
   if (cw->animating) return;
   /* if we have no animations running, go ahead and hide */
   cw->defer_hide = 0;
   evas_object_hide(obj);
}

static void
_e_comp_intercept_show_helper(E_Comp_Object *cw)
{
   E_Client *ec = cw->ec;
   int w = 0, h = 0;

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
   if (!_e_comp_object_intercept_hook_call(E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, ec)) return;
#endif

   if (ec->sticky)
     e_comp_object_signal_emit(cw->smart_obj, "e,state,sticky", "e");
   if (cw->visible)
     {
        if (ec->iconic && cw->animating)
          {
             /* triggered during iconify animation */
             e_comp_object_signal_emit(cw->smart_obj, "e,action,uniconify", "e");
             cw->defer_hide = 0;
          }
        return;
     }
   if (cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE ||
       cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE)
     {
        evas_object_move(cw->smart_obj, ec->x, ec->y);
        evas_object_resize(cw->smart_obj, ec->w, ec->h);
        e_comp_object_frame_theme_set(cw->smart_obj, E_COMP_OBJECT_FRAME_RESHADOW);
        cw->real_hid = 0;
        evas_object_show(cw->smart_obj);

        if (!ec->iconic)
          e_client_focus_defer_set(ec);

        return;
     }
   if ((!cw->updates) && (!ec->input_only) && (!ec->ignored))
     {
        int pw, ph;

        pw = ec->client.w, ph = ec->client.h;
        if ((!pw) || (!ph))
          if (!e_pixmap_size_get(ec->pixmap, &pw, &ph))
            {
               ec->changes.visible = !ec->hidden;
               ec->visible = 1;
               EC_CHANGED(ec);
               return;
            }

        cw->updates = eina_tiler_new(pw, ph);
        if (!cw->updates)
          {
             ec->changes.visible = !ec->hidden;
             ec->visible = 1;
             EC_CHANGED(ec);
             return;
          }
     }
   if (cw->updates)
     eina_tiler_tile_size_set(cw->updates, 1, 1);
   if (ec->new_client)
     {
        /* ignore until client idler first run */
        ec->changes.visible = !ec->hidden;
        ec->visible = 1;
        EC_CHANGED(ec);

        return;
     }
   if (ec->input_only)
     {
        /* who cares */
        cw->real_hid = 0;
        evas_object_move(cw->smart_obj, ec->x, ec->y);
        evas_object_resize(cw->smart_obj, ec->w, ec->h);
        evas_object_show(cw->smart_obj);
        return;
     }
   /* re-set geometry */
   evas_object_move(cw->smart_obj, ec->x, ec->y);
   /* ensure that some kind of frame calc has occurred if there's a frame */
   if (e_pixmap_is_x(ec->pixmap) && cw->frame_object &&
       (ec->h == ec->client.h) && (ec->w == ec->client.w))
     CRI("ACK!");
   /* force resize in case it hasn't happened yet, or just to update size */
   evas_object_resize(cw->smart_obj, ec->w, ec->h);
   if ((cw->w < 1) || (cw->h < 1))
     {
        /* if resize didn't go through, try again */
        ec->visible = ec->changes.visible = 1;
        EC_CHANGED(ec);
        return;
     }
   /* if pixmap not available, clear pixmap since we're going to fetch it again */
   if (!e_pixmap_size_get(ec->pixmap, &w, &h))
     e_pixmap_clear(ec->pixmap);

   if (cw->real_hid && w && h)
     {
        cw->real_hid = 0;
        /* force comp theming in case it didn't happen already */
        e_comp_object_frame_theme_set(cw->smart_obj, E_COMP_OBJECT_FRAME_RESHADOW);
     }

   /* only do the show if show is allowed */
   if (!cw->real_hid)
     {
        if (ec->internal) //internal clients render when they feel like it
          e_comp_object_damage(cw->smart_obj, 0, 0, cw->w, cw->h);

        if (!ec->exp_iconify.by_client ||
            e_policy_visibility_client_is_uniconic(ec))
          {
             evas_object_show(cw->smart_obj);
             if (!ec->iconic ||
                 e_policy_visibility_client_is_uniconic(ec))
               {
                  if (ec->exp_iconify.not_raise &&
                      e_client_check_above_focused(ec))
                    e_client_focus_latest_set(ec);
                  else
                    e_client_focus_defer_set(ec);
               }
          }
     }
}

static void
_e_comp_intercept_show(void *data, Evas_Object *obj EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   E_Client *ec = cw->ec;
   Eina_List *l;
   E_Input_Rect_Data *input_rect_data;
   E_Input_Rect_Smart_Data *input_rect_sd;
   int tw, th;

   if (ec->ignored) return;

   if (cw->effect_obj)
     {
        //INF("SHOW2 %p", ec);
        _e_comp_intercept_show_helper(cw);
        return;
     }
   //INF("SHOW %p", ec);
   if (ec->input_only)
     {
        cw->effect_obj = evas_object_rectangle_add(e_comp->evas);
        evas_object_color_set(cw->effect_obj, 0, 0, 0, 0);
        evas_object_smart_member_add(cw->effect_obj, cw->smart_obj);
     }
   else
     {
        if ((!cw->obj) && (cw->external_content))
          {
             ERR("cw->obj for external content is not created! ec:%p", cw->ec);
             return;
          }

        _e_comp_object_setup(cw);
        if (!cw->obj)
          {
             cw->content_type = E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE;
             cw->obj = evas_object_image_filled_add(e_comp->evas);
             evas_object_image_border_center_fill_set(cw->obj, EVAS_BORDER_FILL_SOLID);
             e_util_size_debug_set(cw->obj, 1);
             evas_object_image_pixels_get_callback_set(cw->obj, _e_comp_object_pixels_get, cw);
             evas_object_image_pixels_noti_callback_set(cw->obj, _e_comp_object_pixels_noti, cw);
             evas_object_image_smooth_scale_set(cw->obj, e_comp_config_get()->smooth_windows);
             evas_object_name_set(cw->obj, "cw->obj");
             evas_object_image_colorspace_set(cw->obj, EVAS_COLORSPACE_ARGB8888);
          }
        _e_comp_object_alpha_set(cw);
#ifdef BORDER_ZOOMAPS
        e_comp_object_zoomap_set(o, 1);
#else
        cw->zoomap_disabled = 1;
#endif
        cw->redirected = 1;
        evas_object_color_set(cw->clip, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity);
     }

   _e_comp_object_map_transform_rect(cw->ec, 0, 0, cw->w, cw->h, NULL, NULL, &tw, &th);
   evas_object_geometry_set(cw->effect_obj, cw->x, cw->y, tw, th);
   if (cw->input_obj)
     {
        input_rect_sd = evas_object_smart_data_get(cw->input_obj);
        if (input_rect_sd)
          {
             EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
               {
                  if (input_rect_data->obj)
                    {
                       evas_object_geometry_set(input_rect_data->obj,
                         cw->x + input_rect_data->rect.x + (!!cw->frame_object * cw->client_inset.l),
                         cw->y + input_rect_data->rect.y + (!!cw->frame_object * cw->client_inset.t),
                         input_rect_data->rect.w, input_rect_data->rect.h);
                    }
               }
          }
     }

   if (cw->mask.obj)
     evas_object_resize(cw->mask.obj, cw->w, cw->h);

   _e_comp_intercept_show_helper(cw);
}

static void
_e_comp_intercept_focus(void *data, Evas_Object *obj, Eina_Bool focus)
{
   E_Comp_Object *cw = data;
   E_Client *ec;

   ec = cw->ec;
   /* note: this is here as it seems there are enough apps that do not even
    * expect us to emulate a look of focus but not actually set x input
    * focus as we do - so simply abort any focus set on such windows */
   /* be strict about accepting focus hint */
   /* be strict about accepting focus hint */
   if ((!ec->icccm.accepts_focus) &&
       (!ec->icccm.take_focus))
     {
        if (!focus)
          {
             if (e_client_focused_get() == ec)
                e_client_focused_set(NULL);
             evas_object_focus_set(obj, focus);
          }
        return;
     }
   if (focus && ec->lock_focus_out) return;
   if (e_object_is_del(E_OBJECT(ec)) && focus)
     CRI("CAN'T FOCUS DELETED CLIENT!");

   /* filter focus setting based on current state */
   if (focus)
     {
        if (ec->focused)
          {
             evas_object_focus_set(obj, focus);
             return;
          }
        if ((ec->iconic) && (!ec->deskshow))
          {
             if (!e_policy_visibility_client_is_uniconify_render_running(ec))
               {
                  /* don't focus an iconified window. that's silly! */
                  e_client_uniconify(ec);
                  if (e_client_focus_track_enabled())
                    e_client_focus_latest_set(ec);
                  return;
               }
          }
        if (!ec->visible)
          {
             return;
          }
        if ((!ec->sticky) && (ec->desk) && (!ec->desk->visible))
          {
             if (ec->desk->animate_count) return;
             e_desk_show(ec->desk);
             if (!ec->desk->visible) return;
          }
     }

   if (focus)
     {
        /* check for dialog children that steal focus */
        if ((ec->modal) && (ec->modal != ec) &&
            (ec->modal->visible) && (!e_object_is_del(E_OBJECT(ec->modal))))
          {
             evas_object_focus_set(ec->modal->frame, focus);
             return;
          }
        else if ((ec->leader) && (ec->leader->modal) &&
                 (ec->leader->modal != ec) && ec->leader->modal->visible &&
                 (!e_object_is_del(E_OBJECT(ec->leader->modal))))
          {
             evas_object_focus_set(ec->leader->modal->frame, focus);
             return;
          }
        if (!cw->visible)
          {
             /* not yet visible, wait till the next time... */
             ec->want_focus = !ec->hidden;
             if (ec->want_focus)
               EC_CHANGED(ec);
             return;
          }
        e_client_focused_set(ec);
     }
   else
     {
        if (e_client_focused_get() == ec)
          e_client_focused_set(NULL);
     }
   evas_object_focus_set(obj, focus);
}

////////////////////////////////////////////////////

static void
_e_comp_object_frame_recalc(E_Comp_Object *cw)
{
   int w, h, ox, oy, ow, oh;
   Eina_List *l;
   Eina_Bool pass_event_flag = EINA_FALSE;
   E_Input_Rect_Data *input_rect_data;
   E_Input_Rect_Smart_Data *input_rect_sd;

   if (cw->frame_object)
     {
        if (cw->obj) edje_object_part_unswallow(cw->frame_object, cw->obj);
        evas_object_geometry_get(cw->frame_object, NULL, NULL, &w, &h);
        /* set a fixed size, force edje calc, check size difference */
        evas_object_resize(cw->frame_object, MAX(w, 50), MAX(h, 50));
        edje_object_message_signal_process(cw->frame_object);
        edje_object_calc_force(cw->frame_object);
        edje_object_part_geometry_get(cw->frame_object, "e.swallow.client", &ox, &oy, &ow, &oh);
        cw->client_inset.l = ox;
        cw->client_inset.r = MAX(w, 50) - (ox + ow);
        cw->client_inset.t = oy;
        cw->client_inset.b = MAX(h, 50) - (oy + oh);
        if (cw->obj) edje_object_part_swallow(cw->frame_object, "e.swallow.client", cw->obj);
        evas_object_resize(cw->frame_object, w, h);

        if (cw->input_obj)
          {
             input_rect_sd = evas_object_smart_data_get(cw->input_obj);
             if (input_rect_sd)
               {
                  EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
                    {
                       if (input_rect_data->obj)
                         {
                            pass_event_flag = EINA_TRUE;
                            break;
                         }
                    }
               }
          }
        evas_object_pass_events_set(cw->map_input_obj, pass_event_flag);
        evas_object_pass_events_set(cw->obj, pass_event_flag);
     }
   else
     {
        cw->client_inset.l = 0;
        cw->client_inset.r = 0;
        cw->client_inset.t = 0;
        cw->client_inset.b = 0;
     }
   cw->client_inset.calc = !!cw->frame_object;
}

static void
_e_comp_smart_cb_frame_recalc(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;
   int w, h, pw, ph;

   /* - get current size
    * - calc new size
    * - readjust for new frame size
    */

   w = cw->ec->w, h = cw->ec->h;
   e_comp_object_frame_wh_unadjust(obj, w, h, &pw, &ph);

   _e_comp_object_frame_recalc(cw);

   if (!cw->ec->fullscreen)
     e_comp_object_frame_wh_adjust(obj, cw->ec->client.w, cw->ec->client.h, &w, &h);

   evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
   if (cw->ec->shading || cw->ec->shaded) return;
   if (cw->ec->fullscreen)
     evas_object_resize(cw->ec->frame, cw->ec->zone->w, cw->ec->zone->h);
   else if (cw->ec->new_client)
     {
        if ((cw->ec->w < 1) || (cw->ec->h < 1)) return;
        e_comp_object_frame_wh_adjust(obj, pw, ph, &w, &h);
        evas_object_resize(cw->ec->frame, w, h);
     }
   else if ((w != cw->ec->w) || (h != cw->ec->h))
     evas_object_resize(cw->ec->frame, w, h);
}

static void
_e_comp_smart_cb_shading(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   cw->shade.x = cw->x;
   cw->shade.y = cw->y;
   e_comp_object_signal_emit(cw->smart_obj, "e,state,shading", "e");
}

static void
_e_comp_smart_cb_shaded(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   e_comp_object_signal_emit(cw->smart_obj, "e,state,shaded", "e");
}

static void
_e_comp_smart_cb_unshading(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   e_comp_object_signal_emit(cw->smart_obj, "e,state,unshading", "e");
}

static void
_e_comp_smart_cb_unshaded(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   E_FREE_FUNC(cw->shade.anim, ecore_timer_del);

   e_comp_object_signal_emit(cw->smart_obj, "e,state,unshaded", "e");
}

static void
_e_comp_smart_cb_maximize(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   _e_comp_object_shadow_setup(cw);
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(cw->smart_obj, "e,action,maximize", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_fullscreen(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (_e_comp_object_shadow_setup(cw))
     e_comp_object_damage(cw->smart_obj, 0, 0, cw->ec->w, cw->ec->h);
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(cw->smart_obj, "e,action,maximize,fullscreen", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_unmaximize(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(obj, "e,action,unmaximize", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_unfullscreen(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (_e_comp_object_shadow_setup(cw))
     {
        EC_CHANGED(cw->ec);
        cw->ec->changes.size = 1;
     }
   if (cw->frame_object)
     {
        _e_comp_object_shadow(cw);
        e_comp_object_signal_emit(obj, "e,action,unmaximize,unfullscreen", "e");
        _e_comp_object_frame_recalc(cw);
        evas_object_smart_callback_call(cw->smart_obj, "frame_recalc_done", &cw->client_inset);
     }
}

static void
_e_comp_smart_cb_sticky(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,sticky", "e");
}

static void
_e_comp_smart_cb_unsticky(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,unsticky", "e");
}

static void
_e_comp_smart_cb_unhung(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   e_comp_object_signal_emit(cw->smart_obj, "e,state,unhung", "e");
}

static void
_e_comp_smart_cb_hung(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!cw->ec) return; //NYI
   e_comp_object_signal_emit(cw->smart_obj, "e,state,hung", "e");
}

static void
_e_comp_smart_focus_in(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   e_comp_object_signal_emit(obj, "e,state,focused", "e");
}

static void
_e_comp_smart_focus_out(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Comp_Object *cw = data;

   if (!e_object_is_del(E_OBJECT(cw->ec)))
     e_comp_object_signal_emit(obj, "e,state,unfocused", "e");
}

static void
_e_comp_input_obj_smart_add(Evas_Object *obj)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   input_rect_sd = E_NEW(E_Input_Rect_Smart_Data, 1);

   if (!input_rect_sd) return;
   evas_object_smart_data_set(obj, input_rect_sd);
}

static void
_e_comp_input_obj_smart_del(Evas_Object *obj)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;

   input_rect_sd = evas_object_smart_data_get(obj);
   if (!input_rect_sd) return;

   EINA_LIST_FREE(input_rect_sd->input_rect_data_list, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_smart_member_del(input_rect_data->obj);
             E_FREE_FUNC(input_rect_data->obj, evas_object_del);
          }
        E_FREE(input_rect_data);
     }
   E_FREE(input_rect_sd);
}

static void
_e_comp_input_obj_smart_move(Evas_Object *obj, int x, int y)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;
   Eina_List *l;
   E_Comp_Object *cw;

   input_rect_sd = evas_object_smart_data_get(obj);
   if (!input_rect_sd) return;

   cw = input_rect_sd->cw;
   EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_geometry_set(input_rect_data->obj,
               x + input_rect_data->rect.x + (!!cw->frame_object * cw->client_inset.l),
               y + input_rect_data->rect.y + (!!cw->frame_object * cw->client_inset.t),
               input_rect_data->rect.w, input_rect_data->rect.h);
          }
     }
}

static void
_e_comp_input_obj_smart_resize(Evas_Object *obj, int w, int h)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;
   Eina_List *l;
   E_Comp_Object *cw;

   input_rect_sd = evas_object_smart_data_get(obj);
   if (!input_rect_sd) return;

   cw = input_rect_sd->cw;
   EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_geometry_set(input_rect_data->obj,
               cw->x + input_rect_data->rect.x + (!!cw->frame_object * cw->client_inset.l),
               cw->y + input_rect_data->rect.y + (!!cw->frame_object * cw->client_inset.t),
               input_rect_data->rect.w, input_rect_data->rect.h);
          }
     }
}

static void
_e_comp_input_obj_smart_show(Evas_Object *obj)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;
   Eina_List *l;

   input_rect_sd = evas_object_smart_data_get(obj);
   if (!input_rect_sd) return;

   EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_show(input_rect_data->obj);
          }
     }
}

static void
_e_comp_input_obj_smart_hide(Evas_Object *obj)
{
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;
   Eina_List *l;

   input_rect_sd = evas_object_smart_data_get(obj);
   if (!input_rect_sd) return;

   EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_hide(input_rect_data->obj);
          }
     }
}

static void
_e_comp_input_obj_smart_init(void)
{
   if (_e_comp_input_obj_smart) return;
   {
      static const Evas_Smart_Class sc =
      {
         INPUT_OBJ_SMART_NAME,
         EVAS_SMART_CLASS_VERSION,
         _e_comp_input_obj_smart_add,
         _e_comp_input_obj_smart_del,
         _e_comp_input_obj_smart_move,
         _e_comp_input_obj_smart_resize,
         _e_comp_input_obj_smart_show,
         _e_comp_input_obj_smart_hide,
         NULL,
         NULL,
         NULL,
         NULL,
         NULL,
         NULL,

         NULL,
         NULL,
         NULL,
         NULL
      };
      _e_comp_input_obj_smart = evas_smart_class_new(&sc);
   }
}


static void
_e_comp_smart_add(Evas_Object *obj)
{
   E_Comp_Object *cw;

   cw = E_NEW(E_Comp_Object, 1);
   EINA_SAFETY_ON_NULL_RETURN(cw);

   cw->smart_obj = obj;
   cw->x = cw->y = cw->w = cw->h = -1;
   evas_object_smart_data_set(obj, cw);
   cw->opacity = 255.0;
   cw->external_content = 0;
   cw->content_type = E_COMP_OBJECT_CONTENT_TYPE_NONE;
   evas_object_data_set(obj, "comp_obj", cw);
   evas_object_move(obj, -1, -1);
   /* intercept ALL the callbacks! */
   evas_object_intercept_stack_above_callback_add(obj, _e_comp_intercept_stack_above, cw);
   evas_object_intercept_stack_below_callback_add(obj, _e_comp_intercept_stack_below, cw);
   evas_object_intercept_raise_callback_add(obj, _e_comp_intercept_raise, cw);
   evas_object_intercept_lower_callback_add(obj, _e_comp_intercept_lower, cw);
   evas_object_intercept_layer_set_callback_add(obj, _e_comp_intercept_layer_set, cw);
   evas_object_intercept_move_callback_add(obj, _e_comp_intercept_move, cw);
   evas_object_intercept_resize_callback_add(obj, _e_comp_intercept_resize, cw);
   evas_object_intercept_show_callback_add(obj, _e_comp_intercept_show, cw);
   evas_object_intercept_hide_callback_add(obj, _e_comp_intercept_hide, cw);
   evas_object_intercept_focus_set_callback_add(obj, _e_comp_intercept_focus, cw);

   evas_object_smart_callback_add(obj, "shading", _e_comp_smart_cb_shading, cw);
   evas_object_smart_callback_add(obj, "shaded", _e_comp_smart_cb_shaded, cw);
   evas_object_smart_callback_add(obj, "unshading", _e_comp_smart_cb_unshading, cw);
   evas_object_smart_callback_add(obj, "unshaded", _e_comp_smart_cb_unshaded, cw);

   evas_object_smart_callback_add(obj, "maximize", _e_comp_smart_cb_maximize, cw);
   evas_object_smart_callback_add(obj, "fullscreen", _e_comp_smart_cb_fullscreen, cw);
   evas_object_smart_callback_add(obj, "unmaximize", _e_comp_smart_cb_unmaximize, cw);
   evas_object_smart_callback_add(obj, "unfullscreen", _e_comp_smart_cb_unfullscreen, cw);

   evas_object_smart_callback_add(obj, "stick", _e_comp_smart_cb_sticky, cw);
   evas_object_smart_callback_add(obj, "unstick", _e_comp_smart_cb_unsticky, cw);

   evas_object_smart_callback_add(obj, "hung", _e_comp_smart_cb_hung, cw);
   evas_object_smart_callback_add(obj, "unhung", _e_comp_smart_cb_unhung, cw);

   evas_object_smart_callback_add(obj, "frame_recalc", _e_comp_smart_cb_frame_recalc, cw);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_FOCUS_IN, _e_comp_smart_focus_in, cw);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_FOCUS_OUT, _e_comp_smart_focus_out, cw);
}

static void
_e_comp_smart_color_set(Evas_Object *obj, int r, int g, int b, int a)
{
   INTERNAL_ENTRY;
   evas_object_color_set(cw->clip, r, g, b, a);
   evas_object_smart_callback_call(obj, "color_set", NULL);
}


static void
_e_comp_smart_clip_set(Evas_Object *obj, Evas_Object *clip)
{
   INTERNAL_ENTRY;
   evas_object_clip_set(cw->clip, clip);
}

static void
_e_comp_smart_clip_unset(Evas_Object *obj)
{
   INTERNAL_ENTRY;
   evas_object_clip_unset(cw->clip);
}

static void
_e_comp_smart_hide(Evas_Object *obj)
{
   TRACE_DS_BEGIN(COMP:SMART HIDE);

   INTERNAL_ENTRY;
   cw->visible = 0;
   evas_object_hide(cw->clip);
   if (cw->input_obj) evas_object_hide(cw->input_obj);
   evas_object_hide(cw->effect_obj);
   evas_object_hide(cw->map_input_obj);
   if (cw->transform_bg_obj) evas_object_hide(cw->transform_bg_obj);
   if (cw->transform_tranp_obj) evas_object_hide(cw->transform_tranp_obj);
   if (stopping)
     {
        TRACE_DS_END();
        return;
     }

   /* unset native surface if current displaying buffer was destroied */
   if (!cw->buffer_destroy_listener.notify)
     {
        Evas_Native_Surface *ns;
        ns = evas_object_image_native_surface_get(cw->obj);
        if ((ns) && (ns->type == EVAS_NATIVE_SURFACE_WL) && (ns->data.wl.legacy_buffer))
          _e_comp_object_native_surface_set(cw, NULL, EINA_TRUE);
     }

   if (!cw->ec->input_only)
     {
        edje_object_freeze(cw->effect_obj);
        edje_object_freeze(cw->shobj);
        edje_object_play_set(cw->shobj, 0);
        if (cw->frame_object)
          edje_object_play_set(cw->frame_object, 0);
     }
   /* ensure focus-out */
   if (cw->ec->focused)
     {
        ELOGF("FOCUS", "focus unset | smart_hide", cw->ec);
        evas_object_focus_set(cw->ec->frame, 0);
        e_client_focus_defer_unset(cw->ec);
     }
   e_comp_render_queue(); //force nocomp recheck

   TRACE_DS_END();
}

static void
_e_comp_smart_show(Evas_Object *obj)
{
   E_Client *tmp;
   Eina_List *l;

   INTERNAL_ENTRY;
   cw->defer_hide = 0;
   cw->visible = 1;
   if ((cw->w < 0) || (cw->h < 0))
     CRI("ACK!");

   TRACE_DS_BEGIN(COMP:SMART SHOW);

   e_comp_object_map_update(obj);

   EINA_LIST_FOREACH(cw->ec->e.state.video_child, l, tmp)
     evas_object_show(tmp->frame);

   evas_object_show(cw->clip);
   if (cw->input_obj) evas_object_show(cw->input_obj);
   if (!cw->ec->input_only)
     {
        edje_object_thaw(cw->effect_obj);
        edje_object_thaw(cw->shobj);
        edje_object_play_set(cw->shobj, 1);
        if (cw->frame_object)
          edje_object_play_set(cw->frame_object, 1);
     }
   evas_object_show(cw->effect_obj);
   if (cw->ec->internal_elm_win && (!evas_object_visible_get(cw->ec->internal_elm_win)))
     evas_object_show(cw->ec->internal_elm_win);
   if (cw->mask.obj) evas_object_show(cw->mask.obj);
   if (cw->transform_bg_obj) evas_object_show(cw->transform_bg_obj);
   if (cw->transform_tranp_obj) evas_object_show(cw->transform_tranp_obj);
   if (evas_object_map_enable_get(cw->effect_obj)) evas_object_show(cw->map_input_obj);
   e_comp_render_queue();
   if (cw->ec->input_only)
     {
        TRACE_DS_END();
        return;
     }
   if (cw->ec->iconic && (!cw->ec->new_client))
     {
        if (cw->ec->exp_iconify.by_client)
          {
             ELOGF("COMP", "Set launching flag..", cw->ec);
             cw->ec->launching = EINA_TRUE;
          }

        e_comp_object_signal_emit(cw->smart_obj, "e,action,uniconify", "e");
     }
   else if (!cw->showing) /* if set, client was ec->hidden during show animation */
     {
        cw->showing = 1;
        ELOGF("COMP", "Set launching flag..", cw->ec);
        cw->ec->launching = EINA_TRUE;

        e_comp_object_signal_emit(cw->smart_obj, "e,state,visible", "e");
        _e_comp_object_animating_begin(cw);
        if (!_e_comp_object_effect_visibility_start(cw, 1))
          {
             TRACE_DS_END();
             return;
          }
     }
   /* ensure some random effect doesn't lock the client offscreen */
   if (!cw->animating)
     {
        cw->showing = 0;
        e_comp_object_effect_set(obj, NULL);
     }

   _e_comp_object_dim_update(cw);

   TRACE_DS_END();
}

static void
_e_comp_smart_del(Evas_Object *obj)
{
   Eina_List *l;

   INTERNAL_ENTRY;

   if (cw->buffer_destroy_listener.notify)
     {
        wl_list_remove(&cw->buffer_destroy_listener.link);
        cw->buffer_destroy_listener.notify = NULL;
     }

   if (cw->tbm_surface)
     {
        tbm_surface_internal_unref(cw->tbm_surface);
        cw->tbm_surface = NULL;
     }

   e_comp_object_render_update_del(cw->smart_obj);
   E_FREE_FUNC(cw->updates, eina_tiler_free);
   E_FREE_FUNC(cw->pending_updates, eina_tiler_free);
   free(cw->ns);

   if (cw->obj_mirror)
     {
        Evas_Object *o;

        EINA_LIST_FREE(cw->obj_mirror, o)
          {
             evas_object_image_data_set(o, NULL);
             evas_object_freeze_events_set(o, 1);
             evas_object_event_callback_del_full(o, EVAS_CALLBACK_DEL, _e_comp_object_cb_mirror_del, cw);
             evas_object_del(o);
          }
     }
   _e_comp_object_layers_remove(cw);
   l = evas_object_data_get(obj, "comp_object-to_del");
   E_FREE_LIST(l, evas_object_del);
   evas_object_del(cw->clip);
   evas_object_del(cw->effect_obj);
   evas_object_del(cw->shobj);
   evas_object_del(cw->frame_object);
   evas_object_del(cw->zoomobj);
   evas_object_del(cw->input_obj);
   evas_object_del(cw->obj);
   evas_object_del(cw->mask.obj);
   if (cw->dim.mask_obj) evas_object_del(cw->dim.mask_obj);
   evas_object_del(cw->transform_bg_obj);
   evas_object_del(cw->transform_tranp_obj);
   evas_object_del(cw->map_input_obj);
   eina_stringshare_del(cw->frame_theme);
   eina_stringshare_del(cw->frame_name);
   if (cw->animating)
     {
        cw->animating = 0;
        e_comp->animating--;
        UNREFD(cw->ec, 2);
        e_object_unref(E_OBJECT(cw->ec));
     }
   cw->ec->frame = NULL;
   free(cw);
}

static void
_e_comp_smart_move(Evas_Object *obj, int x, int y)
{
   INTERNAL_ENTRY;

   cw->x = x, cw->y = y;
   evas_object_move(cw->effect_obj, x, y);
   evas_object_move(cw->map_input_obj, x, y);
   if (cw->input_obj) evas_object_move(cw->input_obj, x, y);

   e_comp_object_map_update(obj);
}

static void
_e_comp_smart_resize(Evas_Object *obj, int w, int h)
{
   Eina_Bool first = EINA_FALSE;
   int tw, th;

   INTERNAL_ENTRY;

   if (!cw->effect_obj) CRI("ACK!");

   TRACE_DS_BEGIN(COMP:SMART RESIZE);

   _e_comp_object_map_transform_rect(cw->ec, 0, 0, w, h, NULL, NULL, &tw, &th);

   if (cw->w != w || cw->h != h)
     e_comp_object_map_update(obj);

   first = ((cw->w < 1) || (cw->h < 1));
   cw->w = w, cw->h = h;
   if ((!cw->ec->shading) && (!cw->ec->shaded))
     {
        int ww, hh, pw, ph;

        if (cw->frame_object)
          e_comp_object_frame_wh_unadjust(obj, w, h, &ww, &hh);
        else
          ww = w, hh = h;
        /* verify pixmap:object size */
        if (e_pixmap_size_get(cw->ec->pixmap, &pw, &ph) && (!cw->ec->override))
          {
             if ((ww != pw) || (hh != ph))
               CRI("CW RSZ: %dx%d || PX: %dx%d", ww, hh, pw, ph);
          }
        evas_object_resize(cw->effect_obj, tw, th);
        evas_object_resize(cw->map_input_obj, w, h);
        if (cw->zoomobj) e_zoomap_child_resize(cw->zoomobj, pw, ph);
        if (cw->input_obj)
          evas_object_resize(cw->input_obj, w, h);
        if (cw->mask.obj)
          evas_object_resize(cw->mask.obj, w, h);
        /* resize render update tiler */
        if (!first)
          {
             RENDER_DEBUG("DAMAGE UNFULL: %p", cw->ec);
             cw->updates_full = 0;
             if (cw->updates) eina_tiler_clear(cw->updates);
          }
        else
          {
             RENDER_DEBUG("DAMAGE RESIZE(%p): %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
             if (cw->updates) eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
          }
     }
   else
     {
        evas_object_resize(cw->effect_obj, tw, th);
        evas_object_resize(cw->map_input_obj, w, h);
     }
   if (!cw->visible)
     {
        TRACE_DS_END();
        return;
     }
   e_comp_render_queue();

   TRACE_DS_END();
}

static void
_e_comp_smart_init(void)
{
   if (_e_comp_smart) return;
   {
      static const Evas_Smart_Class sc =
      {
         SMART_NAME,
         EVAS_SMART_CLASS_VERSION,
         _e_comp_smart_add,
         _e_comp_smart_del,
         _e_comp_smart_move,
         _e_comp_smart_resize,
         _e_comp_smart_show,
         _e_comp_smart_hide,
         _e_comp_smart_color_set,
         _e_comp_smart_clip_set,
         _e_comp_smart_clip_unset,
         NULL,
         NULL,
         NULL,

         NULL,
         NULL,
         NULL,
         NULL
      };
      _e_comp_smart = evas_smart_class_new(&sc);
   }
}

E_API void
e_comp_object_zoomap_set(Evas_Object *obj, Eina_Bool enabled)
{
   API_ENTRY;

   EINA_SAFETY_ON_NULL_RETURN(cw->ec);

   enabled = !enabled;
   if (cw->zoomap_disabled == enabled) return;
   if (enabled)
     {
        cw->zoomobj = e_zoomap_add(e_comp->evas);
        e_zoomap_smooth_set(cw->zoomobj, e_comp_config_get()->smooth_windows);
        e_zoomap_child_set(cw->zoomobj, cw->frame_object);
        edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->zoomobj);
        e_zoomap_child_edje_solid_setup(cw->zoomobj);
        if (cw->ec->override)
          evas_object_name_set(cw->zoomobj, "cw->zoomobj::WINDOW");
        else if (!cw->ec->input_only)
          evas_object_name_set(cw->zoomobj, "cw->zoomobj::CLIENT");
     }
   else
     {
        edje_object_part_unswallow(cw->shobj, cw->zoomobj);
        E_FREE_FUNC(cw->zoomobj, evas_object_del);
        edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->frame_object);
     }
   cw->zoomap_disabled = enabled;
}

E_API Eina_Bool
e_comp_object_mirror_visibility_check(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return !!cw->force_visible;
}
/////////////////////////////////////////////////////////

static void
_e_comp_object_util_del(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Eina_List *l;
   Eina_Bool comp_object;

   comp_object = !!evas_object_data_get(obj, "comp_object");
   if (comp_object)
     {
        Evas_Object *o;

        o = edje_object_part_swallow_get(obj, "e.swallow.content");
        evas_object_del(o);
        e_comp_render_queue();
     }
   l = evas_object_data_get(obj, "comp_object-to_del");
   E_FREE_LIST(l, evas_object_del);
}

static void
_e_comp_object_util_restack(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (e_comp_util_object_is_above_nocomp(obj) &&
       (!evas_object_data_get(obj, "comp_override")))
     {
        evas_object_data_set(obj, "comp_override", (void*)1);
        e_comp_override_add();
     }
}

static void
_e_comp_object_util_show(void *data EINA_UNUSED, Evas_Object *obj)
{
   Eina_Bool ref = EINA_TRUE;
   if (evas_object_visible_get(obj))
     {
        void *d;

        d = evas_object_data_del(obj, "comp_hiding");
        if (d)
          /* currently trying to hide */
          ref = EINA_FALSE;
        else
          /* already visible */
          return;
     }
   
   evas_object_show(obj);
   if (ref)
     {
        evas_object_ref(obj);
        evas_object_data_set(obj, "comp_ref", (void*)1);
     }
   edje_object_signal_emit(obj, "e,state,visible", "e");
   evas_object_data_set(obj, "comp_showing", (void*)1);
   if (e_comp_util_object_is_above_nocomp(obj))
     {
        evas_object_data_set(obj, "comp_override", (void*)1);
        e_comp_override_add();
     }
}

static void
_e_comp_object_util_hide(void *data EINA_UNUSED, Evas_Object *obj)
{
   if (!evas_object_visible_get(obj)) return;
   /* already hiding */
   if (evas_object_data_get(obj, "comp_hiding")) return;
   if (!evas_object_data_del(obj, "comp_showing"))
     {
        evas_object_ref(obj);
        evas_object_data_set(obj, "comp_ref", (void*)1);
     }
   edje_object_signal_emit(obj, "e,state,hidden", "e");
   evas_object_data_set(obj, "comp_hiding", (void*)1);

   if (evas_object_data_del(obj, "comp_override"))
     e_comp_override_timed_pop();
}

static void
_e_comp_object_util_done_defer(void *data, Evas_Object *obj, const char *emission, const char *source EINA_UNUSED)
{
   if (!e_util_strcmp(emission, "e,action,hide,done"))
     {
        if (!evas_object_data_del(obj, "comp_hiding")) return;
        evas_object_intercept_hide_callback_del(obj, _e_comp_object_util_hide);
        evas_object_hide(obj);
        evas_object_intercept_hide_callback_add(obj, _e_comp_object_util_hide, data);
     }
   else
     evas_object_data_del(obj, "comp_showing");
   if (evas_object_data_del(obj, "comp_ref"))
     evas_object_unref(obj);
}

static void
_e_comp_object_util_moveresize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   if (data)
     {
        int w, h;

        evas_object_geometry_get(obj, NULL, NULL, &w, &h);
        e_zoomap_child_resize(data, w, h);
     }
}

E_API E_Comp_Object_Hook *
e_comp_object_hook_add(E_Comp_Object_Hook_Point hookpoint, E_Comp_Object_Hook_Cb func, const void *data)
{
   E_Comp_Object_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_COMP_OBJECT_HOOK_LAST, NULL);
   ch = E_NEW(E_Comp_Object_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_comp_object_hooks[hookpoint] = eina_inlist_append(_e_comp_object_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_comp_object_hook_del(E_Comp_Object_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_comp_object_hooks_walking == 0)
     {
        _e_comp_object_hooks[ch->hookpoint] = eina_inlist_remove(_e_comp_object_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_comp_object_hooks_delete++;
}

#ifdef _F_E_COMP_OBJECT_INTERCEPT_HOOK_
E_API E_Comp_Object_Intercept_Hook *
e_comp_object_intercept_hook_add(E_Comp_Object_Intercept_Hook_Point hookpoint, E_Comp_Object_Intercept_Hook_Cb func, const void *data)
{
   E_Comp_Object_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_COMP_OBJECT_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Comp_Object_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_comp_object_intercept_hooks[hookpoint] = eina_inlist_append(_e_comp_object_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_comp_object_intercept_hook_del(E_Comp_Object_Intercept_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_comp_object_intercept_hooks_walking == 0)
     {
        _e_comp_object_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_comp_object_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_comp_object_intercept_hooks_delete++;
}
#endif

E_API Evas_Object *
e_comp_object_util_add(Evas_Object *obj, E_Comp_Object_Type type)
{
   Evas_Object *o, *z = NULL;
   const char *name;
   Eina_List *l, *list = NULL;
   E_Comp_Config *conf = e_comp_config_get();
   Eina_Bool skip = EINA_FALSE, fast = EINA_FALSE, shadow = EINA_FALSE;
   E_Comp_Match *m;
   char buf[1024];
   int ok = 0;
   int x, y, w, h;
   Eina_Bool vis;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

   switch (type)
     {
      case E_COMP_OBJECT_TYPE_MENU:
        list = conf->match.menus;
        skip = conf->match.disable_menus;
        fast = conf->fast_menus;
        shadow = EINA_TRUE;
        break;
      case E_COMP_OBJECT_TYPE_POPUP:
        list = conf->match.popups;
        skip = conf->match.disable_popups;
        fast = conf->fast_popups;
        break;
      default:
        list = conf->match.objects;
        skip = conf->match.disable_objects;
        fast = conf->fast_objects;
     }
   name = evas_object_name_get(obj);
   vis = evas_object_visible_get(obj);
   o = edje_object_add(e_comp->evas);
   evas_object_data_set(o, "comp_object", (void*)1);
   if (name && (!skip))
     skip = (!strncmp(name, "noshadow", 8));
   if (skip)
     evas_object_data_set(o, "comp_object_skip", (void*)1);
   else if (list)
     {
        EINA_LIST_FOREACH(list, l, m)
          {
             if (((m->name) && (!name)) ||
                 ((name) && (m->name) && (!e_util_glob_match(name, m->name))))
               continue;
             if (!m->shadow_style) continue;
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", m->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", m->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (ok)
               {
                  shadow = !m->no_shadow;
                  break;
               }
          }
     }
   else
     skip = EINA_TRUE;
   while (!ok)
     {
        if (skip)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/none");
        if (ok) break;
        if (conf->shadow_style)
          {
             if (fast)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s/fast", conf->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (!ok)
               {
                  snprintf(buf, sizeof(buf), "e/comp/frame/%s", conf->shadow_style);
                  ok = e_theme_edje_object_set(o, "base/theme/comp", buf);
               }
             if (ok) break;
          }
        if (fast)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/default/fast");
        if (!ok)
          ok = e_theme_edje_object_set(o, "base/theme/comp", "e/comp/frame/default");
        break;
     }
   if (shadow && (e_util_strcmp(evas_object_type_get(obj), "edje") || (!edje_object_data_get(obj, "noshadow"))))
     edje_object_signal_emit(o, "e,state,shadow,on", "e");
   else
     edje_object_signal_emit(o, "e,state,shadow,off", "e");

   evas_object_geometry_get(obj, &x, &y, &w, &h);
   evas_object_geometry_set(o, x, y, w, h);
   evas_object_pass_events_set(o, evas_object_pass_events_get(obj));

   if (list)
     {
        z = e_zoomap_add(e_comp->evas);
        e_zoomap_child_edje_solid_setup(z);
        e_zoomap_smooth_set(z, conf->smooth_windows);
        e_zoomap_child_set(z, obj);
        e_zoomap_child_resize(z, w, h);
        evas_object_show(z);
     }
   edje_object_signal_callback_add(o, "e,action,*,done", "e", _e_comp_object_util_done_defer, z);

   evas_object_intercept_show_callback_add(o, _e_comp_object_util_show, z);
   evas_object_intercept_hide_callback_add(o, _e_comp_object_util_hide, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_MOVE, _e_comp_object_util_moveresize, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_DEL, _e_comp_object_util_del, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_RESIZE, _e_comp_object_util_moveresize, z);
   evas_object_event_callback_add(o, EVAS_CALLBACK_RESTACK, _e_comp_object_util_restack, z);

   e_comp_object_signal_emit(o, "e,state,hidden", "e");

   edje_object_part_swallow(o, "e.swallow.content", z ?: obj);

   _e_comp_object_event_add(o);

   if (vis)
     evas_object_show(o);

   return o;
}

/* utility functions for deleting objects when their "owner" is deleted */
E_API void
e_comp_object_util_del_list_append(Evas_Object *obj, Evas_Object *to_del)
{
   Eina_List *l;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(to_del);
   l = evas_object_data_get(obj, "comp_object-to_del");
   evas_object_data_set(obj, "comp_object-to_del", eina_list_append(l, to_del));
   evas_object_event_callback_del(obj, EVAS_CALLBACK_DEL, _e_comp_object_util_del);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_util_del, NULL);
}

E_API void
e_comp_object_util_del_list_remove(Evas_Object *obj, Evas_Object *to_del)
{
   Eina_List *l;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(to_del);
   l = evas_object_data_get(obj, "comp_object-to_del");
   if (l)
     evas_object_data_set(obj, "comp_object-to_del", eina_list_remove(l, to_del));
}

/////////////////////////////////////////////////////////

E_API Evas_Object *
e_comp_object_client_add(E_Client *ec)
{
   Evas_Object *o;
   E_Comp_Object *cw;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (ec->frame) return NULL;
   _e_comp_smart_init();
   o = evas_object_smart_add(e_comp->evas, _e_comp_smart);
   cw = evas_object_smart_data_get(o);
   if (!cw) return NULL;
   evas_object_data_set(o, "E_Client", ec);
   cw->ec = ec;
   ec->frame = o;
   evas_object_data_set(o, "comp_object", (void*)1);

   _e_comp_object_event_add(o);

   return o;
}

/* utility functions for getting client inset */
E_API void
e_comp_object_frame_xy_adjust(Evas_Object *obj, int x, int y, int *ax, int *ay)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (ax) *ax = x;
        if (ay) *ay = y;
        return;
     }
   if (ax) *ax = x - cw->client_inset.l;
   if (ay) *ay = y - cw->client_inset.t;
}

E_API void
e_comp_object_frame_xy_unadjust(Evas_Object *obj, int x, int y, int *ax, int *ay)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (ax) *ax = x;
        if (ay) *ay = y;
        return;
     }
   if (ax) *ax = x + cw->client_inset.l;
   if (ay) *ay = y + cw->client_inset.t;
}

E_API void
e_comp_object_frame_wh_adjust(Evas_Object *obj, int w, int h, int *aw, int *ah)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (aw) *aw = w;
        if (ah) *ah = h;
        return;
     }
   if (aw) *aw = w + cw->client_inset.l + cw->client_inset.r;
   if (ah) *ah = h + cw->client_inset.t + cw->client_inset.b;
}

E_API void
e_comp_object_frame_wh_unadjust(Evas_Object *obj, int w, int h, int *aw, int *ah)
{
   API_ENTRY;
   if (!cw->client_inset.calc)
     {
        if (aw) *aw = w;
        if (ah) *ah = h;
        return;
     }
   if (aw) *aw = w - cw->client_inset.l - cw->client_inset.r;
   if (ah) *ah = h - cw->client_inset.t - cw->client_inset.b;
}

E_API E_Client *
e_comp_object_client_get(Evas_Object *obj)
{
   Evas_Object *o;

   SOFT_ENTRY(NULL);
   /* FIXME: remove this when eo is used */
   o = evas_object_data_get(obj, "comp_smart_obj");
   if (o)
     return e_comp_object_client_get(o);
   return cw ? cw->ec : NULL;
}

E_API void
e_comp_object_frame_extends_get(Evas_Object *obj, int *x, int *y, int *w, int *h)
{
   API_ENTRY;
   if (cw->frame_extends)
     edje_object_parts_extends_calc(cw->frame_object, x, y, w, h);
   else
     {
        if (x) *x = 0;
        if (y) *y = 0;
        if (w) *w  = cw->ec->w;
        if (h) *h  = cw->ec->h;
     }
}

E_API E_Zone *
e_comp_object_util_zone_get(Evas_Object *obj)
{
   E_Zone *zone = NULL;

   SOFT_ENTRY(NULL);
   if (cw)
     zone = cw->ec->zone;
   if (!zone)
     {
        int x, y;

        if (e_win_client_get(obj))
          return e_win_client_get(obj)->zone;
        evas_object_geometry_get(obj, &x, &y, NULL, NULL);
        zone = e_comp_zone_xy_get(x, y);
     }
   return zone;
}

E_API void
e_comp_object_util_center(Evas_Object *obj)
{
   int x, y, w, h, ow, oh;
   E_Zone *zone;

   SOFT_ENTRY();

   zone = e_comp_object_util_zone_get(obj);
   EINA_SAFETY_ON_NULL_RETURN(zone);
   e_zone_useful_geometry_get(zone, &x, &y, &w, &h);
   if (cw && (cw->ec->changes.size || cw->ec->new_client))
     ow = cw->ec->w, oh = cw->ec->h;
   else
     evas_object_geometry_get(obj, NULL, NULL, &ow, &oh);
   x = x + (w - ow) / 2;
   y = y + (h - oh) / 2;
   evas_object_move(obj, x, y);
}

E_API void
e_comp_object_util_center_on(Evas_Object *obj, Evas_Object *on)
{
   int x, y, w, h, ow, oh;

   SOFT_ENTRY();
   EINA_SAFETY_ON_NULL_RETURN(on);
   evas_object_geometry_get(on, &x, &y, &w, &h);
   if (cw && (cw->ec->changes.size || cw->ec->new_client))
     ow = cw->ec->w, oh = cw->ec->h;
   else
     evas_object_geometry_get(obj, NULL, NULL, &ow, &oh);
   evas_object_move(obj, x + (w / 2) - (ow / 2), y + (h / 2) - (oh / 2));
}

E_API void
e_comp_object_util_fullscreen(Evas_Object *obj)
{
   SOFT_ENTRY();

   if (cw)
     e_client_fullscreen(cw->ec, E_FULLSCREEN_RESIZE);
   else
     {
        evas_object_move(obj, 0, 0);
        evas_object_resize(obj, e_comp->w, e_comp->h);
     }
}

E_API void
e_comp_object_util_center_pos_get(Evas_Object *obj, int *x, int *y)
{
   E_Zone *zone;
   int zx, zy, zw, zh;
   int ow, oh;
   SOFT_ENTRY();

   if (cw)
     ow = cw->w, oh = cw->h;
   else
     evas_object_geometry_get(obj, NULL, NULL, &ow, &oh);
   zone = e_comp_object_util_zone_get(obj);
   e_zone_useful_geometry_get(zone, &zx, &zy, &zw, &zh);
   if (x) *x = zx + (zw - ow) / 2;
   if (y) *y = zy + (zh - oh) / 2;
}

E_API void
e_comp_object_input_objs_del(Evas_Object *obj)
{
   API_ENTRY;
   E_Input_Rect_Data *input_rect_data;
   E_Input_Rect_Smart_Data *input_rect_sd;

   input_rect_sd = evas_object_smart_data_get(cw->input_obj);
   if (!input_rect_sd) return;

   EINA_LIST_FREE(input_rect_sd->input_rect_data_list, input_rect_data)
     {
        if (input_rect_data->obj)
          {
             evas_object_smart_member_del(input_rect_data->obj);
             E_FREE_FUNC(input_rect_data->obj, evas_object_del);
          }
        E_FREE(input_rect_data);
     }
}

E_API void
e_comp_object_input_area_set(Evas_Object *obj, int x, int y, int w, int h)
{
   API_ENTRY;
   E_Input_Rect_Data *input_rect_data;
   E_Input_Rect_Smart_Data *input_rect_sd;
   int client_w, client_h;

   if (cw->ec->client.w)
     client_w = cw->ec->client.w;
   else
     client_w = cw->ec->w;

   if (cw->ec->client.h)
     client_h = cw->ec->client.h;
   else
     client_h = cw->ec->h;

   E_RECTS_CLIP_TO_RECT(x, y, w, h, 0, 0, client_w, client_h);

   input_rect_data = E_NEW(E_Input_Rect_Data, 1);
   EINA_RECTANGLE_SET(&input_rect_data->rect, x, y, w, h);

   if (!cw->input_obj)
     {
        _e_comp_input_obj_smart_init();
        cw->input_obj = evas_object_smart_add(e_comp->evas, _e_comp_input_obj_smart);
        evas_object_smart_member_add(cw->input_obj, cw->smart_obj);
        input_rect_sd = evas_object_smart_data_get(cw->input_obj);

        if (input_rect_sd)
          input_rect_sd->cw = cw;
     }

   input_rect_sd = evas_object_smart_data_get(cw->input_obj);
   if (input_rect_sd)
     input_rect_sd->input_rect_data_list = eina_list_append(input_rect_sd->input_rect_data_list, input_rect_data);
   else
     E_FREE(input_rect_data);

   if ((input_rect_data) &&
       (x || y || (w != cw->ec->client.w) || (h != cw->ec->client.h)))
     {
        input_rect_data->obj = evas_object_rectangle_add(e_comp->evas);
        evas_object_name_set(input_rect_data->obj, "cw->input_obj");
        evas_object_color_set(input_rect_data->obj, 0, 0, 0, 0);
        evas_object_clip_set(input_rect_data->obj, cw->clip);
        evas_object_smart_member_add(input_rect_data->obj, cw->input_obj);
        evas_object_geometry_set(input_rect_data->obj,
          MAX(cw->ec->client.x + (!!cw->frame_object * cw->client_inset.l), 0) + x,
          MAX(cw->ec->client.y + (!!cw->frame_object * cw->client_inset.t), 0) + y, w, h);
        evas_object_pass_events_set(cw->map_input_obj, 1);
        evas_object_pass_events_set(cw->obj, 1);
        if (cw->visible)
          {
             evas_object_show(input_rect_data->obj);
             evas_object_show(cw->input_obj);
          }
     }
   else
     {
        evas_object_smart_member_del(cw->input_obj);
        E_FREE_FUNC(cw->input_obj, evas_object_del);
        evas_object_pass_events_set(cw->map_input_obj, 0);
        evas_object_pass_events_set(cw->obj, 0);
     }
}

E_API void
e_comp_object_input_rect_get(Evas_Object *obj, Eina_List **list)
{
   API_ENTRY;
   E_Input_Rect_Smart_Data *input_rect_sd;
   E_Input_Rect_Data *input_rect_data;
   Eina_List *l;

   if (!cw->input_obj) return;

   input_rect_sd = evas_object_smart_data_get(cw->input_obj);
   if (input_rect_sd)
     {
        EINA_LIST_FOREACH(input_rect_sd->input_rect_data_list, l, input_rect_data)
          {
             *list = eina_list_append(*list, &input_rect_data->rect);
          }
     }
}

E_API void
e_comp_object_frame_geometry_get(Evas_Object *obj, int *l, int *r, int *t, int *b)
{
   API_ENTRY;
   if (l) *l = cw->client_inset.l;
   if (r) *r = cw->client_inset.r;
   if (t) *t = cw->client_inset.t;
   if (b) *b = cw->client_inset.b;
}

/* set geometry for CSD */
E_API void
e_comp_object_frame_geometry_set(Evas_Object *obj, int l, int r, int t, int b)
{
   Eina_Bool calc;

   API_ENTRY;
   if (cw->frame_object)
     CRI("ACK!");
   if ((cw->client_inset.l == l) && (cw->client_inset.r == r) &&
       (cw->client_inset.t == t) && (cw->client_inset.b == b)) return;
   calc = cw->client_inset.calc;
   cw->client_inset.calc = l || r || t || b;
   eina_stringshare_replace(&cw->frame_theme, "borderless");
   if (cw->client_inset.calc)
     {
        cw->ec->w += (l + r) - (cw->client_inset.l + cw->client_inset.r);
        cw->ec->h += (t + b) - (cw->client_inset.t + cw->client_inset.b);
     }
   else if (cw->ec->maximized || cw->ec->fullscreen)
     {
        if (e_client_has_xwindow(cw->ec))
          {
             cw->ec->saved.x += l - cw->client_inset.l;
             cw->ec->saved.y += t - cw->client_inset.t;
          }
        else
          {
             cw->ec->saved.w -= ((l + r) - (cw->client_inset.l + cw->client_inset.r));
             cw->ec->saved.h -= ((t + b) - (cw->client_inset.t + cw->client_inset.b));
          }
     }
   if (!cw->ec->new_client)
     {
        if ((calc || (!e_client_has_xwindow(cw->ec))) && cw->client_inset.calc)
          {
             cw->ec->x -= l - cw->client_inset.l;
             cw->ec->y -= t - cw->client_inset.t;
          }
        cw->ec->changes.pos = cw->ec->changes.size = 1;
        EC_CHANGED(cw->ec);
     }
   cw->client_inset.l = l;
   cw->client_inset.r = r;
   cw->client_inset.t = t;
   cw->client_inset.b = b;
}

E_API Eina_Bool
e_comp_object_frame_allowed(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return (!cw->ec->mwm.borderless) && (cw->frame_object || (!cw->client_inset.calc));
}

E_API Eina_Bool
e_comp_object_frame_title_set(Evas_Object *obj, const char *name)
{
   API_ENTRY EINA_FALSE;
   if (!e_util_strcmp(cw->frame_name, name)) return EINA_FALSE;
   eina_stringshare_replace(&cw->frame_name, name);
   if (cw->frame_object)
     edje_object_part_text_set(cw->frame_object, "e.text.title", cw->frame_name);
   return EINA_TRUE;
}

E_API Eina_Bool
e_comp_object_frame_exists(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return !!cw->frame_object;
}

E_API Eina_Bool
e_comp_object_frame_theme_set(Evas_Object *obj, const char *name)
{
   Evas_Object *o, *pbg;
   char buf[4096];
   int ok;
   Eina_Stringshare *theme;

   API_ENTRY EINA_FALSE;

   if (!e_util_strcmp(cw->frame_theme, name))
    return edje_object_part_swallow(cw->shobj, "e.swallow.content", cw->frame_object ?: cw->obj);
   if (!e_util_strcmp(name, "COMP_RESHADOW"))
     return _e_comp_object_shadow_setup(cw);
   pbg = cw->frame_object;
   theme = eina_stringshare_add(name);

   if (cw->frame_object)
     {
        int w, h;

        w = cw->ec->w, h = cw->ec->h;
        e_comp_object_frame_wh_unadjust(obj, w, h, &cw->ec->w, &cw->ec->h);
        if ((cw->ec->w != w) || (cw->ec->h != h))
          {
             cw->ec->changes.size = 1;
             EC_CHANGED(cw->ec);
          }
        E_FREE_FUNC(cw->frame_object, evas_object_del);
        if (!name) goto reshadow;
     }
   o = edje_object_add(e_comp->evas);
   snprintf(buf, sizeof(buf), "e/widgets/border/%s/border", name);
   ok = e_theme_edje_object_set(o, "base/theme/border", buf);
   if ((!ok) && (!e_util_strcmp(name, "borderless")))
     {
        cw->frame_object = NULL;
        evas_object_del(o);
        eina_stringshare_del(cw->frame_theme);
        cw->frame_theme = theme;
        goto reshadow;
     }
   if (!ok)
     {
        if (theme != e_config->theme_default_border_style)
          {
             snprintf(buf, sizeof(buf), "e/widgets/border/%s/border", e_config->theme_default_border_style);
             ok = e_theme_edje_object_set(o, "base/theme/border", buf);
          }
        if (!ok)
          {
             ok = e_theme_edje_object_set(o, "base/theme/border",
                                          "e/widgets/border/default/border");
             if (ok && (theme == e_config->theme_default_border_style))
               {
                  /* Reset default border style to default */
                  eina_stringshare_replace(&e_config->theme_default_border_style, "default");
                  e_config_save_queue();
               }
          }
     }

   if (ok)
     {
        cw->frame_object = o;
        eina_stringshare_del(cw->frame_theme);
        cw->frame_theme = theme;
        evas_object_name_set(o, "cw->frame_object");

        if (cw->frame_name)
          edje_object_part_text_set(o, "e.text.title", cw->frame_name);

        if (!pbg)
          {
             cw->ec->changes.icon = 1;
             EC_CHANGED(cw->ec);
          }
     }
   else
     {
        CRI("USER IS USING A SHITTY THEME! ABORT!!!!");
        evas_object_del(o);
     }
reshadow:
   if (cw->shobj)
     _e_comp_object_shadow_setup(cw);
   do
     {
        int old_x, old_y, new_x = 0, new_y = 0;

        old_x = cw->x, old_y = cw->y;

        _e_comp_smart_cb_frame_recalc(cw, cw->smart_obj, NULL);
        if (pbg)
          new_x = cw->ec->x, new_y = cw->ec->y;
        else if (cw->ec->placed || (!cw->ec->new_client))
          {
             /* if no previous frame:
              * - reapply client_inset
              * - clamp to zone
              */
             int x, y;

             if (cw->ec->changes.size)
               {
                  x = cw->ec->x;
                  y = cw->ec->y;
               }
             else
               {
                  x = cw->ec->client.x, y = cw->ec->client.y;
                  x = MAX(cw->ec->zone->x, cw->ec->client.x - cw->client_inset.l);
                  y = MAX(cw->ec->zone->y, cw->ec->client.y - cw->client_inset.t);
               }
             new_x = x, new_y = y;
          }

        if (old_x != new_x || old_y != new_y)
          {
             /* this guarantees that we won't get blocked by the NOP check in the interceptor */
             cw->y = cw->x = -99999;
             evas_object_move(obj, new_x, new_y);
          }
     } while (0);

   if (cw->ec->maximized)
     {
        cw->ec->changes.need_maximize = 1;
        EC_CHANGED(cw->ec);
     }
   evas_object_smart_callback_call(cw->smart_obj, "frame_changed", NULL);
   if (cw->frame_object)
     {
        cw->frame_extends = !!edje_object_data_get(cw->frame_object, "frame_extends");
     }
   else
     cw->frame_extends = 0;
   evas_object_del(pbg);
   return EINA_TRUE;
}

E_API void
e_comp_object_signal_emit(Evas_Object *obj, const char *sig, const char *src)
{
   E_Comp_Object_Mover *prov;

   API_ENTRY;
   //INF("EMIT %p: %s %s", cw->ec, sig, src);
   edje_object_signal_emit(cw->shobj, sig, src);
   if (cw->frame_object) edje_object_signal_emit(cw->frame_object, sig, src);
   if ((cw->ec->override && e_comp_config_get()->match.disable_overrides) ||
       ((!cw->ec->override) && e_comp_config_get()->match.disable_borders))
     return;
   /* start with highest priority callback first */
   EINA_INLIST_REVERSE_FOREACH(_e_comp_object_movers, prov)
     {
        if (!e_util_glob_match(sig, prov->sig)) continue;
        if (prov->func(prov->data, obj, sig)) break;
     }
}

E_API void
e_comp_object_signal_callback_add(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data)
{
   /* FIXME: at some point I guess this should use eo to inherit
    * -EDJE_OBJ_SUB_ID_SIGNAL_CALLBACK_ADD
    * -EDJE_OBJ_SUB_ID_SIGNAL_CALLBACK_DEL
    */
   API_ENTRY;
   edje_object_signal_callback_add(cw->shobj, sig, src, cb, (void*)data);
}

E_API void
e_comp_object_signal_callback_del(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb)
{
   API_ENTRY;
   edje_object_signal_callback_del(cw->shobj, sig, src, cb);
}

E_API void
e_comp_object_signal_callback_del_full(Evas_Object *obj, const char *sig, const char *src, Edje_Signal_Cb cb, const void *data)
{
   API_ENTRY;
   edje_object_signal_callback_del_full(cw->shobj, sig, src, cb, (void*)data);
}

E_API void
e_comp_object_damage(Evas_Object *obj, int x, int y, int w, int h)
{
   int tw, th;
   Eina_Rectangle rect;
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates)) return;
   if (cw->nocomp) return;
   rect.x = x, rect.y = y;
   rect.w = w, rect.h = h;
   evas_object_smart_callback_call(obj, "damage", &rect);

   if (e_comp_is_on_overlay(cw->ec))
     {
        /* It will not set hwc_need_update value if E modules already cleanup pixmap
        * resource on the E_COMP_WL_HOOK_BUFFER_CHANGE hook function. It means that
        * E module attempts to block screen update due to the particular policy.
        */
        if (e_pixmap_resource_get(cw->ec->pixmap))
          cw->hwc_need_update = EINA_TRUE;
     }

   /* ignore overdraw */
   if (cw->updates_full)
     {
        RENDER_DEBUG("IGNORED %p: %d,%d %dx%d", cw->ec, x, y, w, h);
        e_comp_object_render_update_add(obj);

        if ((cw->ec->visible) && (!evas_object_visible_get(cw->smart_obj)) && (!cw->ec->iconic))
          evas_object_show(cw->smart_obj);

        return;
     }
   /* clip rect to client surface */
   RENDER_DEBUG("DAMAGE(%d,%d %dx%d) CLIP(%dx%d)", x, y, w, h, cw->ec->client.w, cw->ec->client.h);
   E_RECTS_CLIP_TO_RECT(x, y, w, h, 0, 0, cw->ec->client.w, cw->ec->client.h);
   /* if rect is the total size of the client after clip, clear the updates
    * since this is guaranteed to be the whole region anyway
    */
   eina_tiler_area_size_get(cw->updates, &tw, &th);
   if ((w > tw) || (h > th))
     {
        RENDER_DEBUG("DAMAGE RESIZE %p: %dx%d", cw->ec, cw->ec->client.w, cw->ec->client.h);
        eina_tiler_clear(cw->updates);
        eina_tiler_area_size_set(cw->updates, cw->ec->client.w, cw->ec->client.h);
        x = 0, y = 0;
        tw = cw->ec->client.w, th = cw->ec->client.h;
     }
   if ((!x) && (!y) && (w == tw) && (h == th))
     {
        eina_tiler_clear(cw->updates);
        RENDER_DEBUG("DAMAGE FULL: %p", cw->ec);
        cw->updates_full = 1;
        cw->update_count = 0;
     }
   cw->update_count++;
   if (cw->update_count > UPDATE_MAX)
     {
        /* this is going to get really dumb, so just update the whole thing */
        eina_tiler_clear(cw->updates);
        cw->update_count = cw->updates_full = 1;
        eina_tiler_rect_add(cw->updates, &(Eina_Rectangle){0, 0, tw, th});
        RENDER_DEBUG("DAMAGE MAX: %dx%d", tw, th);
     }
   else
     {
        eina_tiler_rect_add(cw->updates, &(Eina_Rectangle){x, y, w, h});
        RENDER_DEBUG("DAMAGE: %d,%d %dx%d", x, y, w, h);
     }
   cw->updates_exist = 1;
   e_comp_object_render_update_add(obj);

   if ((cw->ec->visible) && (!evas_object_visible_get(cw->smart_obj)) && (!cw->ec->iconic))
     evas_object_show(cw->smart_obj);
}

E_API Eina_Bool
e_comp_object_damage_exists(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return cw->updates_exist;
}

E_API void
e_comp_object_render_update_add(Evas_Object *obj)
{
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates) || (!cw->redirected)) return;
   if (e_object_is_del(E_OBJECT(cw->ec)))
     CRI("CAN'T RENDER A DELETED CLIENT!");
   if (!e_pixmap_usable_get(cw->ec->pixmap)) return;
   if (!cw->update)
     {
        cw->update = 1;
        e_comp->updates = eina_list_append(e_comp->updates, cw->ec);
     }
   e_comp_render_queue();
}

E_API void
e_comp_object_render_update_del(Evas_Object *obj)
{
   API_ENTRY;

   if (cw->ec->input_only || (!cw->updates)) return;
   if (!cw->update) return;
   cw->update = 0;
   /* this gets called during comp animating to clear the update flag */
   if (e_comp->grabbed) return;
   e_comp->updates = eina_list_remove(e_comp->updates, cw->ec);
   if (!e_comp->updates)
     {
        E_FREE_FUNC(e_comp->update_job, ecore_job_del);
        if (e_comp->render_animator)
          ecore_animator_freeze(e_comp->render_animator);
     }
}

E_API void
e_comp_object_shape_apply(Evas_Object *obj)
{
   Eina_List *l;
   Evas_Object *o;
   unsigned int i, *pix, *p;
   int w, h, px, py;

   API_ENTRY;
   if (!cw->ec) return; //NYI
   if (cw->external_content) return;
   if (cw->ec->shaped)
     {
        if ((cw->ec->shape_rects_num >= 1) &&
            (!_e_comp_shaped_check(cw->ec->client.w, cw->ec->client.h, cw->ec->shape_rects, cw->ec->shape_rects_num)))
           return;
     }
   if (cw->native)
     {
        ERR("BUGGER: shape with native surface? cw=%p", cw);
        return;
     }
   evas_object_image_size_get(cw->obj, &w, &h);
   if ((w < 1) || (h < 1)) return;

   if (cw->ec->shaped)
     _e_comp_object_native_surface_set(cw, NULL, EINA_TRUE);
   _e_comp_object_alpha_set(cw);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
      evas_object_image_alpha_set(o, 1);

   p = pix = evas_object_image_data_get(cw->obj, 1);
   if (!pix)
     {
        evas_object_image_data_set(cw->obj, pix);
        return;
     }
   if (cw->ec->shaped)
     {
        unsigned char *spix, *sp;

        spix = calloc(w * h, sizeof(unsigned char));
        if (!spix) return;
        for (i = 0; i < cw->ec->shape_rects_num; i++)
          {
             int rx, ry, rw, rh;

             rx = cw->ec->shape_rects[i].x; ry = cw->ec->shape_rects[i].y;
             rw = cw->ec->shape_rects[i].w; rh = cw->ec->shape_rects[i].h;
             E_RECTS_CLIP_TO_RECT(rx, ry, rw, rh, 0, 0, w, h);
             sp = spix + (w * ry) + rx;
             for (py = 0; py < rh; py++)
               {
                  for (px = 0; px < rw; px++)
                    {
                       *sp = 0xff; sp++;
                    }
                  sp += w - rw;
               }
          }
        sp = spix;
        for (py = 0; py < h; py++)
          {
             for (px = 0; px < w; px++)
               {
                  unsigned int mask, imask;

                  mask = ((unsigned int)(*sp)) << 24;
                  imask = mask >> 8;
                  imask |= imask >> 8;
                  imask |= imask >> 8;
                  *p = mask | (*p & imask);
                  //if (*sp) *p = 0xff000000 | *p;
                  //else *p = 0x00000000;
                  sp++;
                  p++;
               }
          }
        free(spix);
     }
   else
      {
         for (py = 0; py < h; py++)
           {
              for (px = 0; px < w; px++)
                *p |= 0xff000000;
           }
      }
   evas_object_image_data_set(cw->obj, pix);
   evas_object_image_data_update_add(cw->obj, 0, 0, w, h);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_data_set(o, pix);
        evas_object_image_data_update_add(o, 0, 0, w, h);
     }
// don't need to fix alpha chanel as blending
// should be totally off here regardless of
// alpha channel content
}

static void
_e_comp_object_clear(E_Comp_Object *cw)
{
   Eina_List *l;
   Evas_Object *o;

   EINA_SAFETY_ON_NULL_RETURN(cw->ec);

   if (cw->ec->pixmap)
     e_pixmap_clear(cw->ec->pixmap);
   if (cw->native)
     _e_comp_object_native_surface_set(cw, NULL, EINA_TRUE);
   evas_object_image_size_set(cw->obj, 1, 1);
   evas_object_image_data_set(cw->obj, NULL);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_size_set(o, 1, 1);
        evas_object_image_data_set(o, NULL);
     }
   cw->native = 0;
   e_comp_object_render_update_del(cw->smart_obj);
}

/* helper function to simplify toggling of redirection for display servers which support it */
E_API void
e_comp_object_redirected_set(Evas_Object *obj, Eina_Bool set)
{
   API_ENTRY;

   set = !!set;
   if (cw->redirected == set) return;
   cw->redirected = set;
   if (cw->external_content) return;
   if (set)
     {
        if (cw->updates_exist)
          e_comp_object_render_update_add(obj);
        else
          e_comp_object_damage(obj, 0, 0, cw->w, cw->h);
        evas_object_smart_callback_call(obj, "redirected", NULL);
     }
   else
     {
        _e_comp_object_clear(cw);
        evas_object_smart_callback_call(obj, "unredirected", NULL);
     }
}

static void
_e_comp_object_cb_buffer_destroy(struct wl_listener *listener, void *data EINA_UNUSED)
{
   E_Comp_Object *cw;
   cw = container_of(listener, E_Comp_Object, buffer_destroy_listener);

   if (cw->buffer_destroy_listener.notify)
     {
        cw->buffer_destroy_listener.notify = NULL;
        wl_list_remove(&cw->buffer_destroy_listener.link);
     }

   if (e_object_is_del(E_OBJECT(cw->ec)))
     {
        if (!e_object_delay_del_ref_get(E_OBJECT(cw->ec)))
          return;
     }
   else
     {
        /* if it's current displaying buffer, do not remove its content */
        if (!evas_object_visible_get(cw->ec->frame))
          _e_comp_object_native_surface_set(cw, NULL, EINA_TRUE);
     }
}

static void
_e_comp_object_native_surface_set(E_Comp_Object *cw, Evas_Native_Surface *ns, Eina_Bool with_mirror)
{
   Eina_List *l;
   Evas_Object *o;

   if (cw->buffer_destroy_listener.notify)
     {
        wl_list_remove(&cw->buffer_destroy_listener.link);
        cw->buffer_destroy_listener.notify = NULL;
     }

   if (cw->tbm_surface)
     {
        tbm_surface_internal_unref(cw->tbm_surface);
        cw->tbm_surface = NULL;
     }

   if (ns)
     {
        if ((ns->type == EVAS_NATIVE_SURFACE_WL) && (ns->data.wl.legacy_buffer))
          {
             cw->buffer_destroy_listener.notify = _e_comp_object_cb_buffer_destroy;
             wl_resource_add_destroy_listener((struct wl_resource *)ns->data.wl.legacy_buffer, &cw->buffer_destroy_listener);
          }
        else if ((ns->type == EVAS_NATIVE_SURFACE_TBM) && (ns->data.tbm.buffer))
          {
             tbm_surface_internal_ref(ns->data.tbm.buffer);
             cw->tbm_surface = ns->data.tbm.buffer;
          }
     }

   evas_object_image_native_surface_set(cw->obj, ns);
   if (with_mirror)
     {
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          {
             evas_object_image_alpha_set(o, !!cw->ns ? 1 : cw->ec->argb);
             evas_object_image_native_surface_set(o, ns);
          }
     }
}

E_API void
e_comp_object_native_surface_set(Evas_Object *obj, Eina_Bool set)
{
   Evas_Native_Surface ns;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;
   if (cw->external_content) return;
   set = !!set;

   memset(&ns, 0, sizeof(Evas_Native_Surface));

   if (set)
     {
        /* native requires gl enabled, texture from pixmap enabled, and a non-shaped client */
        set = (((e_comp->comp_type != E_PIXMAP_TYPE_X) || e_comp_config_get()->texture_from_pixmap) &&
          (!cw->ec->shaped));
        if (set)
          set = (!!cw->ns) || e_pixmap_native_surface_init(cw->ec->pixmap, &ns);
     }
   cw->native = set;

   _e_comp_object_native_surface_set(cw, set && (!cw->blanked) ? (cw->ns ? cw->ns : &ns) : NULL, EINA_TRUE);
}

E_API void
e_comp_object_native_surface_override(Evas_Object *obj, Evas_Native_Surface *ns)
{
   API_ENTRY;
   if (cw->ec->input_only) return;
   E_FREE(cw->ns);
   if (ns)
     cw->ns = (Evas_Native_Surface*)eina_memdup((unsigned char*)ns, sizeof(Evas_Native_Surface), 0);
   _e_comp_object_alpha_set(cw);
   if (cw->native)
     e_comp_object_native_surface_set(obj, cw->native);
   e_comp_object_damage(obj, 0, 0, cw->w, cw->h);
}

E_API void
e_comp_object_blank(Evas_Object *obj, Eina_Bool set)
{
   API_ENTRY;

   set = !!set;

   if (cw->blanked == set) return;
   cw->blanked = set;
   _e_comp_object_alpha_set(cw);
   if (set)
     {
        _e_comp_object_native_surface_set(cw, NULL, EINA_FALSE);
        evas_object_image_data_set(cw->obj, NULL);
        return;
     }
   if (cw->native)
     e_comp_object_native_surface_set(obj, 1);
   e_comp_object_damage(obj, 0, 0, cw->w, cw->h);
}

/* mark an object as dirty and setup damages */
E_API void
e_comp_object_dirty(Evas_Object *obj)
{
   Eina_Iterator *it;
   Eina_Rectangle *rect;
   Eina_List *ll;
   Evas_Object *o;
   int w, h, tw, th;
   Eina_Bool dirty, visible;

   API_ENTRY;
   if (cw->external_content) return;
   /* only actually dirty if pixmap is available */
   if (!e_pixmap_resource_get(cw->ec->pixmap))
     {
        // e_pixmap_size_get returns last attached buffer size
        // eventhough it is destroyed
        ERR("ERROR NO PIXMAP FOR ec:%p", cw->ec);
        return;
     }
   dirty = e_pixmap_size_get(cw->ec->pixmap, &w, &h);
   visible = cw->visible;
   if (!dirty) w = h = 1;
   evas_object_image_pixels_dirty_set(cw->obj, cw->blanked ? 0 : dirty);
   if (!dirty)
     evas_object_image_data_set(cw->obj, NULL);
   _e_comp_object_map_transform_rect(cw->ec, 0, 0, w, h, NULL, NULL, &tw, &th);
   evas_object_image_size_set(cw->obj, tw, th);
   if (cw->mask.obj) evas_object_resize(cw->mask.obj, w, h);
   if (cw->pending_updates)
     eina_tiler_area_size_set(cw->pending_updates, w, h);
   EINA_LIST_FOREACH(cw->obj_mirror, ll, o)
     {
        evas_object_image_pixels_dirty_set(o, dirty);
        if (!dirty)
          evas_object_image_data_set(o, NULL);
        evas_object_image_size_set(o, tw, th);
        visible |= evas_object_visible_get(o);
     }
   if (!dirty)
     {
        ERR("ERROR FETCHING PIXMAP FOR %p", cw->ec);
        return;
     }

   e_comp_object_native_surface_set(obj, 1);

   it = eina_tiler_iterator_new(cw->updates);
   EINA_ITERATOR_FOREACH(it, rect)
     {
        _e_comp_object_map_transform_rect(cw->ec, rect->x, rect->y, rect->w, rect->h, &rect->x, &rect->y, &rect->w, &rect->h);
        evas_object_image_data_update_add(cw->obj, rect->x, rect->y, rect->w, rect->h);
        EINA_LIST_FOREACH(cw->obj_mirror, ll, o)
          evas_object_image_data_update_add(o, rect->x, rect->y, rect->w, rect->h);
        if (cw->pending_updates)
          eina_tiler_rect_add(cw->pending_updates, rect);
     }
   eina_iterator_free(it);
   if (cw->pending_updates)
     eina_tiler_clear(cw->updates);
   else
     {
        cw->pending_updates = cw->updates;
        cw->updates = eina_tiler_new(w, h);
        eina_tiler_tile_size_set(cw->updates, 1, 1);
     }
   cw->update_count = cw->updates_full = cw->updates_exist = 0;
   evas_object_smart_callback_call(obj, "dirty", NULL);
   if (cw->visible || (!visible) || (!cw->pending_updates) || cw->native) return;
   /* force render if main object is hidden but mirrors are visible */
   RENDER_DEBUG("FORCING RENDER %p", cw->ec);
   e_comp_object_render(obj);
}

E_API Eina_Bool
e_comp_object_render(Evas_Object *obj)
{
   Eina_List *l;
   Evas_Object *o;
   int pw, ph;
   unsigned int *pix;

   API_ENTRY EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE);
   if (cw->ec->input_only) return EINA_TRUE;
   if (cw->external_content) return EINA_TRUE;
   if (cw->native) return EINA_FALSE;
   /* if comp object is not redirected state, comp object should not be set by newly committed data
      because image size of comp object is 1x1 and it should not be shown on canvas */
   if (!cw->redirected) return EINA_TRUE;
   e_comp_object_render_update_del(obj);
   if (!e_pixmap_size_get(cw->ec->pixmap, &pw, &ph)) return EINA_FALSE;

   if (!cw->pending_updates)
     {
        WRN("RENDER [%p]: NO RECTS!", cw->ec);
        evas_object_image_data_set(cw->obj, NULL);
        EINA_LIST_FOREACH(cw->obj_mirror, l, o)
          evas_object_image_data_set(o, NULL);
        return EINA_FALSE;
     }

   evas_object_image_pixels_dirty_set(cw->obj, EINA_FALSE);

   RENDER_DEBUG("RENDER SIZE: %dx%d", pw, ph);

   pix = e_pixmap_image_data_get(cw->ec->pixmap);
   if (!pix)
     {
        e_pixmap_image_refresh(cw->ec->pixmap);
        pix = e_pixmap_image_data_get(cw->ec->pixmap);
     }

   if ((pix) && ((!cw->blanked) || (cw->obj_mirror)))
     e_pixmap_image_data_ref(cw->ec->pixmap);

   /* set pixel data */
   evas_object_image_data_set(cw->obj, cw->blanked ? NULL : pix);
   _e_comp_object_alpha_set(cw);
   EINA_LIST_FOREACH(cw->obj_mirror, l, o)
     {
        evas_object_image_data_set(o, pix);
        evas_object_image_alpha_set(o, evas_object_image_alpha_get(cw->obj));
        evas_object_image_pixels_dirty_set(o, EINA_FALSE);
     }

   E_FREE_FUNC(cw->pending_updates, eina_tiler_free);

   if (e_comp->comp_type == E_PIXMAP_TYPE_WL)
     e_comp_client_post_update_add(cw->ec);

   return EINA_TRUE;
}

/* create a duplicate of an evas object */
E_API Evas_Object *
e_comp_object_util_mirror_add(Evas_Object *obj)
{
   Evas_Object *o;
   int w, h, tw, th;
   unsigned int *pix = NULL;
   Eina_Bool argb = EINA_FALSE;

   SOFT_ENTRY(NULL);

   if (!cw)
     cw = evas_object_data_get(obj, "comp_mirror");
   if (!cw)
     {
        o = evas_object_image_filled_add(evas_object_evas_get(obj));
        evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
        evas_object_image_smooth_scale_set(o, e_comp_config_get()->smooth_windows);
        evas_object_image_alpha_set(o, 1);
        evas_object_image_source_set(o, obj);
        return o;
     }
   if ((!cw->ec) || (!e_pixmap_size_get(cw->ec->pixmap, &w, &h))) return NULL;
   if (cw->external_content)
     {
        ERR("%p of client %p is external content.", obj, cw->ec);
        return NULL;
     }
   o = evas_object_image_filled_add(evas_object_evas_get(obj));
   evas_object_image_colorspace_set(o, EVAS_COLORSPACE_ARGB8888);
   evas_object_image_smooth_scale_set(o, e_comp_config_get()->smooth_windows);
   cw->obj_mirror = eina_list_append(cw->obj_mirror, o);
   evas_object_event_callback_add(o, EVAS_CALLBACK_DEL, _e_comp_object_cb_mirror_del, cw);
   evas_object_event_callback_add(o, EVAS_CALLBACK_SHOW, _e_comp_object_cb_mirror_show, cw);
   evas_object_event_callback_add(o, EVAS_CALLBACK_HIDE, _e_comp_object_cb_mirror_hide, cw);
   evas_object_data_set(o, "E_Client_Mirror", cw->ec);
   evas_object_data_set(o, "comp_mirror", cw);

   evas_object_image_alpha_set(o, evas_object_image_alpha_get(cw->obj));
   _e_comp_object_map_transform_rect(cw->ec, 0, 0, w, h, NULL, NULL, &tw, &th);

   evas_object_image_size_set(o, tw, th);

   if (cw->ec->shaped)
     pix = evas_object_image_data_get(cw->obj, 0);
   else
     {
        if (cw->native)
          {
             if (cw->ns)
               evas_object_image_native_surface_set(o, cw->ns);
             else
               {
                  Evas_Native_Surface ns;
                  memset(&ns, 0, sizeof(Evas_Native_Surface));
                  if (e_pixmap_native_surface_init(cw->ec->pixmap, &ns))
                    evas_object_image_native_surface_set(o, &ns);
               }
          }
        else
          {
             /* FIXME: legacy code, please refer to commit 5e6831187a1 */
             argb = e_pixmap_image_is_argb(cw->ec->pixmap);
             if ((argb) &&
                 (e_pixmap_image_exists(cw->ec->pixmap)))
               pix = e_pixmap_image_data_get(cw->ec->pixmap);
             else
               pix = evas_object_image_data_get(cw->obj, EINA_FALSE);
          }
     }
   if (pix)
     {
      Eina_Bool dirty;

      dirty = evas_object_image_pixels_dirty_get(cw->obj);
      evas_object_image_pixels_dirty_set(o, dirty);
      evas_object_image_data_set(o, pix);
      evas_object_image_data_set(cw->obj, pix);
      if (dirty)
        evas_object_image_data_update_add(o, 0, 0, tw, th);
   }
   return o;
}

//////////////////////////////////////////////////////

E_API Eina_Bool
e_comp_object_effect_allowed_get(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;

   if (!cw->shobj) return EINA_FALSE;
   if (cw->ec->override) return !e_comp_config_get()->match.disable_overrides;
   return !e_comp_config_get()->match.disable_borders;
}

/* setup an api effect for a client */
E_API Eina_Bool
e_comp_object_effect_set(Evas_Object *obj, const char *effect)
{
   char buf[4096];
   Eina_Stringshare *grp;
   E_Comp_Config *config;
   Eina_Bool loaded = EINA_FALSE;

   API_ENTRY EINA_FALSE;
   if (!cw->shobj) return EINA_FALSE; //input window

   if (!effect) effect = "none";
   snprintf(buf, sizeof(buf), "e/comp/effects/%s", effect);

   config = e_comp_config_get();
   if ((config) && (config->effect_file))
     {
        if (edje_object_file_set(cw->effect_obj, config->effect_file, buf))
          {
             cw->effect_set = EINA_TRUE;
             loaded = EINA_TRUE;
          }
     }

   if (!loaded)
     {
        edje_object_file_get(cw->effect_obj, NULL, &grp);
        cw->effect_set = !eina_streq(effect, "none");
        if (!e_util_strcmp(buf, grp)) return cw->effect_set;
        if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", buf))
          {
             snprintf(buf, sizeof(buf), "e/comp/effects/auto/%s", effect);
             if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", buf))
               if (!e_theme_edje_object_set(cw->effect_obj, "base/theme/comp", "e/comp/effects/none"))
                 {
                    if (cw->effect_running)
                      {
                         if (!e_comp_object_effect_stop(obj, evas_object_data_get(cw->effect_obj, "_e_comp.end_cb")))
                           return EINA_FALSE;
                      }
                    cw->effect_set = EINA_FALSE;
                    return cw->effect_set;
                 }
          }
     }
   if (cw->effect_running)
     {
        if (!e_comp_object_effect_stop(obj, evas_object_data_get(cw->effect_obj, "_e_comp.end_cb")))
          return EINA_FALSE;
     }
   INF("[EFFECT Set] ec[%p] GROUP[%s]\n",cw->ec, buf);
   edje_object_part_swallow(cw->effect_obj, "e.swallow.content", cw->shobj);
   if (cw->effect_clip)
     {
        evas_object_clip_unset(cw->clip);
        cw->effect_clip = 0;
     }
   cw->effect_clip_able = !edje_object_data_get(cw->effect_obj, "noclip");

   _e_comp_object_dim_update(cw);

   return cw->effect_set;
}

/* set params for embryo scripts in effect */
E_API void
e_comp_object_effect_params_set(Evas_Object *obj, int id, int *params, unsigned int count)
{
   Edje_Message_Int_Set *msg;
   unsigned int x;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(params);
   EINA_SAFETY_ON_FALSE_RETURN(count);
   if (!cw->effect_set) return;

   msg = alloca(sizeof(Edje_Message_Int_Set) + ((count - 1) * sizeof(int)));
   msg->count = (int)count;
   for (x = 0; x < count; x++)
      msg->val[x] = params[x];
   edje_object_message_send(cw->effect_obj, EDJE_MESSAGE_INT_SET, id, msg);
   edje_object_message_signal_process(cw->effect_obj);
}

static void
_e_comp_object_effect_end_cb(void *data, Evas_Object *obj, const char *emission, const char *source)
{
   Edje_Signal_Cb end_cb;
   void *end_data;
   E_Comp_Object *cw = data;

   edje_object_signal_callback_del_full(obj, "e,action,done", "e", _e_comp_object_effect_end_cb, NULL);
   cw->effect_running = 0;
   if (!_e_comp_object_animating_end(cw)) return;

   if (evas_object_data_get(cw->smart_obj, "effect_running"))
     {
        evas_object_data_del(cw->smart_obj, "effect_running");
        _e_comp_object_event_simple(obj, E_EVENT_COMP_OBJECT_EFFECT_END);
        e_client_visibility_calculate();
     }

   end_cb = evas_object_data_get(obj, "_e_comp.end_cb");
   if (!end_cb) return;
   end_data = evas_object_data_get(obj, "_e_comp.end_data");
   _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_EFFECT_END, cw->ec);
   end_cb(end_data, cw->smart_obj, emission, source);
}

/* clip effect to client's zone */
E_API void
e_comp_object_effect_clip(Evas_Object *obj)
{
   API_ENTRY;
   if (!cw->ec->zone) return;
   if (cw->effect_clip) e_comp_object_effect_unclip(cw->smart_obj);
   if (!cw->effect_clip_able) return;
   evas_object_clip_set(cw->smart_obj, cw->ec->zone->bg_clip_object);
   cw->effect_clip = 1;
}

/* unclip effect from client's zone */
E_API void
e_comp_object_effect_unclip(Evas_Object *obj)
{
   API_ENTRY;
   if (!cw->effect_clip) return;
   evas_object_clip_unset(cw->smart_obj);
   cw->effect_clip = 0;
}

/* start effect, running end_cb after */
E_API Eina_Bool
e_comp_object_effect_start(Evas_Object *obj, Edje_Signal_Cb end_cb, const void *end_data)
{
   API_ENTRY EINA_FALSE;
   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE); //NYI
   if (!cw->effect_set) return EINA_FALSE;

   if (cw->effect_running)
     {
        e_comp_object_effect_stop(obj, evas_object_data_get(cw->effect_obj, "_e_comp.end_cb"));
     }

   e_comp_object_effect_clip(obj);
   edje_object_signal_callback_del(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb);

   edje_object_signal_callback_add(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb, cw);
   evas_object_data_set(cw->effect_obj, "_e_comp.end_cb", end_cb);
   evas_object_data_set(cw->effect_obj, "_e_comp.end_data", end_data);
   evas_object_data_set(cw->smart_obj, "effect_running", (void*)1);

   _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_EFFECT_START, cw->ec);
   _e_comp_object_event_simple(obj, E_EVENT_COMP_OBJECT_EFFECT_START);

   edje_object_signal_emit(cw->effect_obj, "e,action,go", "e");
   _e_comp_object_animating_begin(cw);
   cw->effect_running = 1;
   return EINA_TRUE;
}

/* stop a currently-running effect immediately */
E_API Eina_Bool
e_comp_object_effect_stop(Evas_Object *obj, Edje_Signal_Cb end_cb)
{
   int ret = 0;
   Edje_Signal_Cb end_cb_before = NULL;
   void *end_data_before = NULL;
   API_ENTRY EINA_FALSE;

   end_cb_before   = evas_object_data_get(cw->effect_obj, "_e_comp.end_cb");
   end_data_before = evas_object_data_get(cw->effect_obj, "_e_comp.end_data");

   if (end_cb_before != end_cb) return EINA_TRUE;
   e_comp_object_effect_unclip(obj);
   if (cw->effect_clip)
     {
        evas_object_clip_unset(cw->effect_obj);
        cw->effect_clip = 0;
     }
   edje_object_signal_emit(cw->effect_obj, "e,action,stop", "e");
   edje_object_signal_callback_del_full(cw->effect_obj, "e,action,done", "e", _e_comp_object_effect_end_cb, cw);

   if (evas_object_data_get(cw->smart_obj, "effect_running"))
     {
        evas_object_data_del(cw->smart_obj, "effect_running");
        e_client_visibility_calculate();
     }

   cw->effect_running = 0;
   ret = _e_comp_object_animating_end(cw);

   if ((ret) && (end_cb_before))
     {
        _e_comp_object_hook_call(E_COMP_OBJECT_HOOK_EFFECT_END, cw->ec);
        end_cb_before(end_data_before, cw->smart_obj, "e,action,done", "e");
     }

   return ret;
}

static int
_e_comp_object_effect_mover_sort_cb(E_Comp_Object_Mover *a, E_Comp_Object_Mover *b)
{
   return a->pri - b->pri;
}

/* add a function to trigger based on signal emissions for the purpose of modifying effects */
E_API E_Comp_Object_Mover *
e_comp_object_effect_mover_add(int pri, const char *sig, E_Comp_Object_Mover_Cb provider, const void *data)
{
   E_Comp_Object_Mover *prov;

   prov = E_NEW(E_Comp_Object_Mover, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(prov, NULL);
   prov->func = provider;
   prov->data = (void*)data;
   prov->pri = pri;
   prov->sig = sig;
   _e_comp_object_movers = eina_inlist_sorted_insert(_e_comp_object_movers, EINA_INLIST_GET(prov),
     (Eina_Compare_Cb)_e_comp_object_effect_mover_sort_cb);
   return prov;
}

E_API void
e_comp_object_effect_mover_del(E_Comp_Object_Mover *prov)
{
   EINA_SAFETY_ON_NULL_RETURN(prov);
   _e_comp_object_movers = eina_inlist_remove(_e_comp_object_movers, EINA_INLIST_GET(prov));
   free(prov);
}

E_API Evas_Object *
e_comp_object_effect_object_get(Evas_Object *obj)
{
   API_ENTRY NULL;

   return cw->effect_obj;
}
////////////////////////////////////

static void
_e_comp_object_autoclose_cleanup(Eina_Bool already_del)
{
   if (e_comp->autoclose.obj)
     {
        e_comp_ungrab_input(0, 1);
        if (e_comp->autoclose.del_cb)
          e_comp->autoclose.del_cb(e_comp->autoclose.data, e_comp->autoclose.obj);
        else if (!already_del)
          {
             evas_object_hide(e_comp->autoclose.obj);
             E_FREE_FUNC(e_comp->autoclose.obj, evas_object_del);
          }
        E_FREE_FUNC(e_comp->autoclose.rect, evas_object_del);
     }
   e_comp->autoclose.obj = NULL;
   e_comp->autoclose.data = NULL;
   e_comp->autoclose.del_cb = NULL;
   e_comp->autoclose.key_cb = NULL;
   E_FREE_FUNC(e_comp->autoclose.key_handler, ecore_event_handler_del);
}

static Eina_Bool
_e_comp_object_autoclose_key_down_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;
   Eina_Bool del = EINA_TRUE;

   /* returning false in key_cb means delete the object */
   if (e_comp->autoclose.key_cb)
     del = !e_comp->autoclose.key_cb(e_comp->autoclose.data, ev);
   if (del) _e_comp_object_autoclose_cleanup(0);
   return ECORE_CALLBACK_DONE;
}

static void
_e_comp_object_autoclose_mouse_up_cb(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _e_comp_object_autoclose_cleanup(0);
}

static void
_e_comp_object_autoclose_setup(Evas_Object *obj)
{
   if (!e_comp->autoclose.rect)
     {
        /* create rect just below autoclose object to catch mouse events */
        e_comp->autoclose.rect = evas_object_rectangle_add(e_comp->evas);
        evas_object_move(e_comp->autoclose.rect, 0, 0);
        evas_object_resize(e_comp->autoclose.rect, e_comp->w, e_comp->h);
        evas_object_show(e_comp->autoclose.rect);
        evas_object_name_set(e_comp->autoclose.rect, "e_comp->autoclose.rect");
        evas_object_color_set(e_comp->autoclose.rect, 0, 0, 0, 0);
        evas_object_event_callback_add(e_comp->autoclose.rect, EVAS_CALLBACK_MOUSE_UP, _e_comp_object_autoclose_mouse_up_cb, e_comp);
        e_comp_grab_input(0, 1);
     }
   evas_object_layer_set(e_comp->autoclose.rect, evas_object_layer_get(obj) - 1);
   evas_object_focus_set(obj, 1);
   if (!e_comp->autoclose.key_handler)
     e_comp->autoclose.key_handler = ecore_event_handler_add(ECORE_EVENT_KEY_DOWN, _e_comp_object_autoclose_key_down_cb, e_comp);
}

static void
_e_comp_object_autoclose_show(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   _e_comp_object_autoclose_setup(obj);
   evas_object_event_callback_del(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show);
}

static void
_e_comp_object_autoclose_del(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   evas_object_event_callback_del(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show);
   _e_comp_object_autoclose_cleanup(1);
   if (e_client_focused_get()) return;
   if (e_config->focus_policy != E_FOCUS_MOUSE)
     e_client_refocus();
}

E_API void
e_comp_object_util_autoclose(Evas_Object *obj, E_Comp_Object_Autoclose_Cb del_cb, E_Comp_Object_Key_Cb cb, const void *data)
{
   SOFT_ENTRY();

   if (e_comp->autoclose.obj)
     {
        if (e_comp->autoclose.obj == obj) return;
        evas_object_event_callback_del_full(e_comp->autoclose.obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, e_comp);
        e_comp->autoclose.obj = obj;
        e_comp->autoclose.del_cb = del_cb;
        e_comp->autoclose.key_cb = cb;
        e_comp->autoclose.data = (void*)data;
        if (evas_object_visible_get(obj))
          _e_comp_object_autoclose_setup(obj);
        else
          evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show, e_comp);
        evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, e_comp);
        return;
     }
   e_comp->autoclose.obj = obj;
   e_comp->autoclose.del_cb = del_cb;
   e_comp->autoclose.key_cb = cb;
   e_comp->autoclose.data = (void*)data;
   if (evas_object_visible_get(obj))
     _e_comp_object_autoclose_setup(obj);
   else
     evas_object_event_callback_add(obj, EVAS_CALLBACK_SHOW, _e_comp_object_autoclose_show, e_comp);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_DEL, _e_comp_object_autoclose_del, e_comp);
}

E_API unsigned int
e_comp_object_is_animating(Evas_Object *obj)
{
   API_ENTRY 0;

   return cw->animating;
}

E_API void
e_comp_object_alpha_set(Evas_Object *obj, Eina_Bool alpha)
{
   API_ENTRY;

   if ((cw->external_content) &&
       (cw->content_type != E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE))
     {
        WRN("Can set up alpha value to ONLY evas \"image\" object. "
            "But current external content is %d object for %p.",
            cw->content_type, cw->ec);
        return;
     }

   cw->user_alpha_set = EINA_TRUE;
   cw->user_alpha = alpha;

   if (alpha == evas_object_image_alpha_get(cw->obj)) return;

   evas_object_image_alpha_set(cw->obj, alpha);

   if ((!cw->native) && (!cw->external_content))
     evas_object_image_data_set(cw->obj, NULL);
}

E_API Eina_Bool
e_comp_object_alpha_get(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;

   return evas_object_image_alpha_get(cw->obj);
}

E_API void
e_comp_object_mask_set(Evas_Object *obj, Eina_Bool set)
{
   Eina_Bool mask_set = EINA_FALSE;
   Evas_Object *o;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;
   mask_set = !!set;

   if (mask_set)
     {
        if (!cw->mask.obj)
          {
             o = evas_object_rectangle_add(e_comp->evas);
             evas_object_color_set(o, 0, 0, 0, 0);
             evas_object_clip_set(o, cw->clip);
             evas_object_smart_member_add(o, obj);
             evas_object_move(o, 0, 0);
             /* save render op value to restore when clear a mask.
              *
              * NOTE: DO NOT change the render op on ec->frame while mask object
              * is set. it will overwrite the changed op value. */
             cw->mask.saved_render_op = evas_object_render_op_get(obj);
             evas_object_render_op_set(obj, EVAS_RENDER_COPY);
             evas_object_render_op_set(o, EVAS_RENDER_COPY);
             if (cw->visible) evas_object_show(o);

             cw->mask.obj = o;
             ELOGF("COMP", "         |mask_obj", cw->ec);
          }
     }
   else
     {
        if (cw->mask.obj)
          {
             evas_object_smart_member_del(cw->mask.obj);
             E_FREE_FUNC(cw->mask.obj, evas_object_del);

             evas_object_render_op_set(obj, cw->mask.saved_render_op);
          }
     }
}

E_API Eina_Bool
e_comp_object_mask_has(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;

   return (cw->mask.obj) ? EINA_TRUE : EINA_FALSE;
}

E_API void
e_comp_object_size_update(Evas_Object *obj, int w, int h)
{
   int tw, th;
   API_ENTRY;

   if ((cw->external_content) &&
       (cw->content_type != E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE))
     {
        WRN("Can set up size to ONLY evas \"image\" object. "
            "But current external content is %d object for %p.",
            cw->content_type, cw->ec);
        return;
     }

   _e_comp_object_map_transform_rect(cw->ec, 0, 0, w, h, NULL, NULL, &tw, &th);

   evas_object_image_size_set(cw->obj, tw, th);
}

E_API void
e_comp_object_transform_bg_set(Evas_Object *obj, Eina_Bool set)
{
   Eina_Bool transform_set = EINA_FALSE;
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;

   transform_set = !!set;

   if (transform_set)
     {
        if (!cw->transform_bg_obj)
          {
             Evas_Object *o = evas_object_rectangle_add(e_comp->evas);
             evas_object_move(o, 0, 0);
             evas_object_resize(o, 1, 1);
             evas_object_render_op_set(o, EVAS_RENDER_COPY);
             evas_object_color_set(o, 0, 0, 0, 255);
             if (cw->visible) evas_object_show(o);

             cw->transform_bg_obj = o;
          }
        _e_comp_object_transform_obj_stack_update(obj);
     }
   else
     {
        if (cw->transform_bg_obj)
          {
             evas_object_smart_member_del(cw->transform_bg_obj);
             E_FREE_FUNC(cw->transform_bg_obj, evas_object_del);
          }
     }
}

E_API void
e_comp_object_transform_bg_vertices_set(Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices)
{
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;
   if (!cw->transform_bg_obj) return;

   _e_comp_object_transform_obj_map_set(cw->transform_bg_obj, vertices);
}

E_API void
e_comp_object_transform_transp_set(Evas_Object *obj, Eina_Bool set)
{
   Eina_Bool transform_set = EINA_FALSE;
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;

   transform_set = !!set;

   if (transform_set)
     {
        if (!cw->transform_tranp_obj)
          {
             Evas_Object *o = evas_object_rectangle_add(e_comp->evas);
             evas_object_move(o, 0, 0);
             evas_object_resize(o, 1, 1);
             evas_object_render_op_set(o, EVAS_RENDER_COPY);
             evas_object_color_set(o, 0, 0, 0, 0);
             if (cw->visible) evas_object_show(o);

             cw->transform_tranp_obj = o;
          }
        _e_comp_object_transform_obj_stack_update(obj);
     }
   else
     {
        if (cw->transform_tranp_obj)
          {
             evas_object_smart_member_del(cw->transform_tranp_obj);
             E_FREE_FUNC(cw->transform_tranp_obj, evas_object_del);
          }
     }
}

E_API void
e_comp_object_transform_transp_vertices_set(Evas_Object *obj, E_Util_Transform_Rect_Vertex *vertices)
{
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   if (cw->ec->input_only) return;
   if (!cw->transform_tranp_obj) return;

   _e_comp_object_transform_obj_map_set(cw->transform_tranp_obj, vertices);
}

E_API void
e_comp_object_layer_update(Evas_Object *obj,
                           Evas_Object *above, Evas_Object *below)
{
   E_Comp_Object *cw2 = NULL;
   Evas_Object *o = NULL;
   short layer;

   API_ENTRY;

   if (cw->ec->layer_block) return;
   if ((above) && (below))
     {
        ERR("Invalid layer update request! cw=%p", cw);
        return;
     }

   o = above?:below;

   if (o)
     {
        layer = evas_object_layer_get(o);
        cw2 = evas_object_data_get(o, "comp_obj");
        while (!cw2)
          {
             if (!e_util_strcmp(evas_object_name_get(o), "layer_obj")) break;

             o = evas_object_above_get(o);
             if ((!o) || (o == cw->smart_obj)) break;
             if (evas_object_layer_get(o) != layer)
               {
                  o = e_comp->layers[e_comp_canvas_layer_map(E_LAYER_CLIENT_ALERT)].obj;
               }
             if (!o)
               {
                  E_Client *ec;
                  ec = e_client_top_get();
                  if (ec) o = ec->frame;
               }

             if (o) cw2 = evas_object_data_get(o, "comp_obj");
          }
     }

   _e_comp_object_layers_remove(cw);
   if (cw2)
     {
        if (cw2->layer > cw->layer)
          _e_comp_object_layers_add(cw, NULL, NULL, 0);
        else if (cw2->layer == cw->layer)
          {
             if (above)
               _e_comp_object_layers_add(cw, cw2, NULL, 0);
             else if (o == obj)
               _e_comp_object_layers_add(cw, NULL, NULL, above? 0 : 1);
             else if (below)
               _e_comp_object_layers_add(cw, NULL, cw2, 0);
          }
        else
          _e_comp_object_layers_add(cw, NULL, NULL, 1);
     }
   else
     _e_comp_object_layers_add(cw, NULL, NULL, 0);
}

E_API Eina_Bool
e_comp_object_content_set(Evas_Object *obj,
                          Evas_Object *content,
                          E_Comp_Object_Content_Type type)
{
   API_ENTRY EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec->pixmap, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(content, EINA_FALSE);

   if (cw->obj)
     {
        ERR("Can't set e.swallow.content to requested content. "
            "Previous comp object should not be changed at all.");
        return EINA_FALSE;
     }

   if (e_pixmap_usable_get(cw->ec->pixmap)) return EINA_FALSE;

   if ((type != E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE) &&
       (type != E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE))
     {
        ERR("Content type %d for %p is not supported ec:%p pixmap:%p",
            type, content, cw->ec, cw->ec->pixmap);
        return EINA_FALSE;
     }

   cw->external_content = EINA_TRUE;

   cw->obj = content;
   cw->content_type = type;
   e_util_size_debug_set(cw->obj, 1);
   evas_object_name_set(cw->obj, "cw->obj");
   _e_comp_object_alpha_set(cw);

   _e_comp_object_shadow_setup(cw);

   return EINA_TRUE;
}

E_API Eina_Bool
e_comp_object_content_unset(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec->pixmap, EINA_FALSE);

   if (!cw->obj && !cw->ec->visible)
     {
        INF("%p is not visible yet. no need to unset", cw->ec);
        return EINA_TRUE;
     }

   if (cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
     {
        INF("%p has been set to internal image object already.", cw->ec);
        return EINA_TRUE;
     }

   if (cw->obj)
     {
        if (cw->frame_object)
          edje_object_part_unswallow(cw->frame_object, cw->obj);
        else if (cw->shobj)
          edje_object_part_unswallow(cw->shobj, cw->obj);

        evas_object_del(cw->obj);
        evas_object_hide(cw->obj);
        cw->obj = NULL;
     }

   cw->external_content = EINA_FALSE;
   if (cw->ec->is_cursor)
     {
        int pw, ph;
        DBG("%p is cursor surface..", cw->ec);
        if (!e_pixmap_size_get(cw->ec->pixmap, &pw, &ph))
          pw = ph = 1;
        evas_object_resize(cw->ec->frame, pw, ph);
        evas_object_hide(cw->ec->frame);
        return EINA_TRUE;
     }


   cw->content_type = E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE;
   cw->obj = evas_object_image_filled_add(e_comp->evas);
   evas_object_image_border_center_fill_set(cw->obj, EVAS_BORDER_FILL_SOLID);
   e_util_size_debug_set(cw->obj, 1);
   evas_object_image_pixels_get_callback_set(cw->obj, _e_comp_object_pixels_get, cw);
   evas_object_image_smooth_scale_set(cw->obj, e_comp_config_get()->smooth_windows);
   evas_object_name_set(cw->obj, "cw->obj");
   evas_object_image_colorspace_set(cw->obj, EVAS_COLORSPACE_ARGB8888);
   _e_comp_object_alpha_set(cw);

   _e_comp_object_shadow_setup(cw);

   if (cw->visible)
     {
        cw->visible = 0;
        _e_comp_intercept_show_helper(cw);
        cw->visible = 1;
     }

   e_comp_object_damage(cw->smart_obj, 0, 0, cw->w, cw->h);
   e_comp_object_dirty(cw->smart_obj);
   e_comp_object_render(cw->smart_obj);
   e_comp_object_render_update_add(obj);

   return EINA_TRUE;
}

E_API Evas_Object *
e_comp_object_content_get(Evas_Object *obj)
{
   API_ENTRY NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, NULL);

   if (cw->content_type == E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
     {
        INF("%p has been set to internal image object. couldn't return internal image object", cw->ec);
        return NULL;
     }

   return cw->obj;
}

E_API E_Comp_Object_Content_Type
e_comp_object_content_type_get(Evas_Object *obj)
{
   API_ENTRY E_COMP_OBJECT_CONTENT_TYPE_NONE;

   return cw->content_type;
}

E_API void
e_comp_object_dim_mask_update(Evas_Object *obj, Eina_Bool mask_set, int x, int y, int w, int h)
{
   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   E_Comp_Config *conf = e_comp_config_get();
   if (cw->ec->input_only) return;
   if (!conf->dim_rect_enable) return;

   cw->dim.mask_set = mask_set;
   cw->dim.mask_x = x;
   cw->dim.mask_y = y;
   cw->dim.mask_w = w;
   cw->dim.mask_h = h;

   if (!cw->dim.enable) return;
   e_comp_object_dim_mask_set(cw->ec->frame, mask_set);
}

E_API void
e_comp_object_dim_mask_set(Evas_Object *obj, Eina_Bool set)
{
   Eina_Bool mask_set = EINA_FALSE;
   Evas_Object *o;

   API_ENTRY;
   EINA_SAFETY_ON_NULL_RETURN(cw->ec);
   E_Comp_Config *conf = e_comp_config_get();
   if (cw->ec->input_only) return;
   if (!conf->dim_rect_enable) return;

   mask_set = !!set;

   if (mask_set)
     {
        if (cw->dim.mask_obj)
          {
             evas_object_smart_member_del(cw->dim.mask_obj);
             E_FREE_FUNC(cw->dim.mask_obj, evas_object_del);
          }

        INF("[DIM]Mask applied on Dim rect client[%p] mask_rect[%d %d %d %d]", cw->ec, cw->dim.mask_x, cw->dim.mask_y, cw->dim.mask_w, cw->dim.mask_h);
        o = evas_object_rectangle_add(e_comp->evas);
        evas_object_color_set(o, 0, 0, 0, 0);
        evas_object_smart_member_add(o, obj);
        evas_object_resize(o, cw->dim.mask_w, cw->dim.mask_h);
        evas_object_move(o, cw->dim.mask_x, cw->dim.mask_y);

        evas_object_render_op_set(o, EVAS_RENDER_COPY);
        if (cw->visible) evas_object_show(o);

        cw->dim.mask_obj = o;

        evas_object_layer_set(cw->dim.mask_obj, 9998);
     }
   else
     {
        if (cw->dim.mask_obj)
          {
             INF("[DIM] Mask on Dim rect Removed");
             evas_object_smart_member_del(cw->dim.mask_obj);
             E_FREE_FUNC(cw->dim.mask_obj, evas_object_del);
          }
     }
}

E_API void
e_comp_object_dim_client_set(E_Client *ec)
{
   E_Comp_Config *conf = e_comp_config_get();

   if (!conf->dim_rect_enable) return ;
   if (dim_client == ec) return;

   Eina_Bool prev_dim = EINA_FALSE;
   INF("[DIM] Client Set %p -> %p", dim_client, ec);

   if (dim_client && _e_comp_object_dim_enable_get(dim_client, dim_client->frame))
      prev_dim = EINA_TRUE;

   if (prev_dim && dim_client->visible && ec)
     {
        _e_comp_object_dim_enable_set(dim_client, dim_client->frame, EINA_FALSE, EINA_TRUE);
        _e_comp_object_dim_enable_set(ec, ec->frame, EINA_TRUE, EINA_TRUE);
     }
   else
     {
        if (prev_dim) _e_comp_object_dim_enable_set(dim_client, dim_client->frame, EINA_FALSE, EINA_FALSE);
        if (ec)       _e_comp_object_dim_enable_set(ec, ec->frame, EINA_TRUE, EINA_FALSE);
     }
   dim_client = ec;
}

E_API E_Client *
e_comp_object_dim_client_get(void)
{
   E_Comp_Config *conf = e_comp_config_get();

   if (!conf->dim_rect_enable ) return NULL;

   return dim_client;
}

static void
_e_comp_object_dim_enable_set(E_Client *ec, Evas_Object *obj, Eina_Bool enable, Eina_Bool noeffect)
{
   API_ENTRY;
   char emit[32] = "\0";
   E_Comp_Config *conf = e_comp_config_get();

   if (!ec) return;
   if (!conf->dim_rect_enable) return;
   if (!cw->effect_obj) return;
   if (enable == cw->dim.enable) return;

   if (noeffect || !conf->dim_rect_effect)
     {
        INF("[DIM] Applied on Client[%p] ON[%d]\n",ec,enable );
        strncpy(emit, (enable ? "e,state,dim,on,noeffect" : "e,state,dim,off,noeffect"), sizeof(emit) - 1);
     }
   else
     {
        INF("[DIM] Applied on Client[%p] ON[%d]\n",ec,enable );
        strncpy(emit, (enable ? "e,state,dim,on" : "e,state,dim,off"), sizeof(emit) - 1);
     }

   cw->dim.enable = enable;

   if (cw->dim.mask_set && !enable)
     {
        e_comp_object_dim_mask_set(cw->ec->frame, enable);
        edje_object_signal_emit(cw->effect_obj, emit, "e");
     }
   else if (cw->dim.mask_set && enable)
     {
        edje_object_signal_emit(cw->effect_obj, emit, "e");
        e_comp_object_dim_mask_set(cw->ec->frame, enable);
     }
   else
     {
        edje_object_signal_emit(cw->effect_obj, emit, "e");
     }
}

static Eina_Bool
_e_comp_object_dim_enable_get(E_Client *ec, Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   E_Comp_Config *conf = e_comp_config_get();

   if (!ec) return EINA_FALSE;
   if (!conf->dim_rect_enable) return EINA_FALSE;

   if (cw->dim.enable) return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_comp_object_dim_update(E_Comp_Object *cw)
{
   E_Comp_Config *conf = e_comp_config_get();

   if (!cw) return;
   if (!conf->dim_rect_enable) return;
   if (!cw->effect_obj) return;
   if (cw->dim.enable)
     {
        edje_object_signal_emit(cw->effect_obj, (cw->dim.enable ? "e,state,dim,on,noeffect" : "e,state,dim,off,noeffect"), "e");
        INF("[DIM] Applied on Client[%p] enable[%d]\n",cw->ec, cw->dim.enable);

        if (cw->dim.mask_set)
          {
             e_comp_object_dim_mask_set(cw->ec->frame,  cw->dim.mask_set);
          }
     }
}

E_API void
e_comp_object_clear(Evas_Object *obj)
{
   API_ENTRY;

   _e_comp_object_clear(cw);
}

E_API Eina_Bool
e_comp_object_hwc_update_exists(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   return cw->hwc_need_update;
}

// will remove out
E_API void
e_comp_object_hwc_update_set(Evas_Object *obj, Eina_Bool set)
{
   API_ENTRY;
   cw->hwc_need_update = set;
}

E_API Eina_Bool
e_comp_object_effect_object_part_swallow(Evas_Object *obj, const char *part_name, Evas_Object *swallow_obj)
{
   API_ENTRY EINA_FALSE;
   return edje_object_part_swallow(cw->effect_obj, part_name, swallow_obj);
}

E_API void
e_comp_object_indicator_swallow(Evas_Object *obj, Evas_Object *indicator)
{
   API_ENTRY;
   if (cw->indicator.obj != indicator)
     edje_object_part_unswallow(cw->shobj, cw->indicator.obj);
   cw->indicator.obj = indicator;
   edje_object_part_swallow(cw->shobj, "e.swallow.indicator", indicator);
}

E_API void
e_comp_object_indicator_unswallow(Evas_Object *obj, Evas_Object *indicator)
{
   API_ENTRY;
   if (cw->indicator.obj != indicator) return;
   cw->indicator.obj = NULL;
   edje_object_part_unswallow(cw->shobj, indicator);
}

E_API void
e_comp_object_indicator_size_set(Evas_Object *obj, int w, int h)
{
   API_ENTRY;
   Edje_Message_Int_Set *msg;

   if (!cw->indicator.obj) return;

   cw->indicator.w = w;
   cw->indicator.h = h;

   if (!cw->shobj) return;

   msg = alloca(sizeof(Edje_Message_Int_Set) + (sizeof(int)));
   msg->count = 2;
   msg->val[0] = w;
   msg->val[1] = h;
   edje_object_message_send(cw->shobj, EDJE_MESSAGE_INT_SET, 0, msg);
   edje_object_message_signal_process(cw->shobj);
}

/* buffer transform and scale are applied to e_comp_object and e_pixmap internaly */
void
e_comp_object_map_update(Evas_Object *obj)
{
   API_ENTRY;
   E_Client *ec = cw->ec;
   Evas_Map *map;
   int x1, y1, x2, y2, x, y, bw, bh, tw, th;
   char buffer[128];
   char *p = buffer;
   int l, remain = sizeof buffer;

   if (!ec || !ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   /* if buffer had been flushed, buffer could be NULL. Then map will be applied
    * when new buffer is attached.
    */
   if (!ec->comp_data->buffer_ref.buffer) return;

   if (!e_comp_wl_output_buffer_transform_get(ec) &&
       ec->comp_data->scaler.buffer_viewport.buffer.scale == 1)
     {
        if (evas_object_map_enable_get(cw->effect_obj))
          {
             ELOGF("TRANSFORM", "map: disable", cw->ec);
             evas_object_map_enable_set(cw->effect_obj, EINA_FALSE);
             evas_object_hide(cw->map_input_obj);
          }
        e_pixmap_size_get(ec->pixmap, &bw, &bh);
        evas_object_resize(cw->effect_obj, bw, bh);
        return;
     }

   map = evas_map_new(4);
   EINA_SAFETY_ON_NULL_RETURN(map);

   e_pixmap_size_get(ec->pixmap, &bw, &bh);

   x1 = y1 = 0;
   x2 = bw;
   y2 = bh;

   evas_map_util_points_populate_from_geometry(map, ec->x, ec->y, bw, bh, 0);

   _e_comp_object_map_transform_pos(ec, x1, y1, &x, &y);
   evas_map_point_image_uv_set(map, 0, x, y);
   l = snprintf(p, remain, "%d,%d", x, y);
   p += l, remain -= l;

   _e_comp_object_map_transform_pos(ec, x2, y1, &x, &y);
   evas_map_point_image_uv_set(map, 1, x, y);
   l = snprintf(p, remain, " %d,%d", x, y);
   p += l, remain -= l;

   _e_comp_object_map_transform_pos(ec, x2, y2, &x, &y);
   evas_map_point_image_uv_set(map, 2, x, y);
   l = snprintf(p, remain, " %d,%d", x, y);
   p += l, remain -= l;

   _e_comp_object_map_transform_pos(ec, x1, y2, &x, &y);
   evas_map_point_image_uv_set(map, 3, x, y);
   l = snprintf(p, remain, " %d,%d", x, y);
   p += l, remain -= l;

   ELOGF("TRANSFORM", "map: point(%d,%d %dx%d) uv(%d,%d %d,%d %d,%d %d,%d=>%s)",
         cw->ec,
         ec->x, ec->y, bw, bh, x1, y1, x2, y1, x2, y2, x1, y2, buffer);

   evas_object_map_set(cw->effect_obj, map);
   evas_object_map_enable_set(cw->effect_obj, EINA_TRUE);

   evas_map_free(map);

   /* if there's screen rotation with comp mode, then ec->effect_obj and
    * ec->obj should rotate. if not, in evas_map, update region is clipped.
    */
   _e_comp_object_map_transform_rect(cw->ec, 0, 0, bw, bh, NULL, NULL, &tw, &th);
   evas_object_resize(cw->effect_obj, tw, th);

   evas_object_show(cw->map_input_obj);
}

EINTERN Eina_Bool
e_comp_object_render_trace_set(Evas_Object *obj, Eina_Bool set)
{
   API_ENTRY EINA_FALSE;

   cw->render_trace = set;

   return EINA_TRUE;
}

E_API Eina_Bool
e_comp_object_native_usable_get(Evas_Object *obj)
{
   API_ENTRY EINA_FALSE;
   EINA_SAFETY_ON_NULL_RETURN_VAL(cw->ec, EINA_FALSE);

   if (cw->ec->input_only) return EINA_FALSE;
   if (cw->external_content) return EINA_FALSE;
   if (e_comp_object_content_type_get(cw->ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE) return EINA_FALSE;

   /* just return true value, if it is normal case */
   if (e_pixmap_usable_get(cw->ec->pixmap)) return EINA_TRUE;

   /* abnormal case */
   Evas_Native_Surface *ns;
   ns = evas_object_image_native_surface_get(cw->obj);

   /* client pixmap is not usable but cw->obj is drawable due to it holds valid native surface*/
   if (ns)
     {
        ELOGF("COMP", "Client pixmap is Not usable but still holds valid native surface", cw->ec);
        return EINA_TRUE;
     }

   return EINA_FALSE;
}
