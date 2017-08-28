#include "e.h"
#include <sys/xattr.h>

# include <tdm.h> /* temporary */

#define OVER_FLOW 1
//#define SHAPE_DEBUG
//#define BORDER_ZOOMAPS
//////////////////////////////////////////////////////////////////////////
//
// TODO (no specific order):
//   1. abstract evas object and compwin so we can duplicate the object N times
//      in N canvases - for winlist, everything, pager etc. too
//   2. implement "unmapped composite cache" -> N pixels worth of unmapped
//      windows to be fully composited. only the most active/recent.
//   3. for unmapped windows - when window goes out of unmapped comp cache
//      make a miniature copy (1/4 width+height?) and set property on window
//      with pixmap id
//
//////////////////////////////////////////////////////////////////////////

static Eina_List *handlers = NULL;
static Eina_List *hooks = NULL;
E_API E_Comp *e_comp = NULL;
E_API E_Comp_Wl_Data *e_comp_wl = NULL;
static Eina_Hash *ignores = NULL;
static Eina_List *actions = NULL;

static E_Comp_Config *conf = NULL;
static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_match_edd = NULL;

static Ecore_Timer *action_timeout = NULL;
static Eina_Bool gl_avail = EINA_FALSE;

static double ecore_frametime = 0;

static int _e_comp_log_dom = -1;

static int _e_comp_hooks_delete = 0;
static int _e_comp_hooks_walking = 0;

static Eina_Inlist *_e_comp_hooks[] =
{
   [E_COMP_HOOK_PREPARE_PLANE] = NULL,
};

typedef enum _E_Comp_HWC_Mode
{
   E_HWC_MODE_NO = 0,
   E_HWC_MODE_HYBRID,
   E_HWC_MODE_FULL
} E_Comp_HWC_Mode;

E_API int E_EVENT_COMPOSITOR_RESIZE = -1;
E_API int E_EVENT_COMPOSITOR_DISABLE = -1;
E_API int E_EVENT_COMPOSITOR_ENABLE = -1;
E_API int E_EVENT_COMPOSITOR_FPS_UPDATE = -1;

//////////////////////////////////////////////////////////////////////////
#undef DBG
#undef INF
#undef WRN
#undef ERR
#undef CRI

#if 1
# ifdef SHAPE_DEBUG
#  define SHAPE_DBG(...)            EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_INF(...)            EINA_LOG_DOM_INFO(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_WRN(...)            EINA_LOG_DOM_WARN(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)
#  define SHAPE_CRI(...)            EINA_LOG_DOM_CRIT(_e_comp_log_dom, __VA_ARGS__)
# else
#  define SHAPE_DBG(f, x ...)
#  define SHAPE_INF(f, x ...)
#  define SHAPE_WRN(f, x ...)
#  define SHAPE_ERR(f, x ...)
#  define SHAPE_CRI(f, x ...)
# endif

#define DBG(...)            EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__)
#define INF(...)            EINA_LOG_DOM_INFO(_e_comp_log_dom, __VA_ARGS__)
#define WRN(...)            EINA_LOG_DOM_WARN(_e_comp_log_dom, __VA_ARGS__)
#define ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)
#define CRI(...)            EINA_LOG_DOM_CRIT(_e_comp_log_dom, __VA_ARGS__)
#else
#define DBG(f, x ...)
#define INF(f, x ...)
#define WRN(f, x ...)
#define ERR(f, x ...)
#define CRI(f, x ...)
#endif

static void
_e_comp_fps_update(void)
{
   static double rtime = 0.0;
   static double rlapse = 0.0;
   static int frames = 0;
   static int flapse = 0;
   double dt;
   double tim = ecore_time_get();

   /* calculate fps */
   dt = tim - e_comp->frametimes[0];
   e_comp->frametimes[0] = tim;

   rtime += dt;
   frames++;

   if (rlapse == 0.0)
     {
        rlapse = tim;
        flapse = frames;
     }
   else if ((tim - rlapse) >= 0.5)
     {
        e_comp->fps = (frames - flapse) / (tim - rlapse);
        rlapse = tim;
        flapse = frames;
        rtime = 0.0;
     }

   if (conf->fps_show)
     {
        if (e_comp->fps_bg && e_comp->fps_fg)
          {
             char buf[128];
             Evas_Coord x = 0, y = 0, w = 0, h = 0;
             E_Zone *z;

             if (e_comp->fps > 0.0) snprintf(buf, sizeof(buf), "FPS: %1.1f", e_comp->fps);
             else snprintf(buf, sizeof(buf), "N/A");
             evas_object_text_text_set(e_comp->fps_fg, buf);

             evas_object_geometry_get(e_comp->fps_fg, NULL, NULL, &w, &h);
             w += 8;
             h += 8;
             z = e_zone_current_get();
             if (z)
               {
                  switch (conf->fps_corner)
                    {
                     case 3: // bottom-right
                        x = z->x + z->w - w;
                        y = z->y + z->h - h;
                        break;

                     case 2: // bottom-left
                        x = z->x;
                        y = z->y + z->h - h;
                        break;

                     case 1: // top-right
                        x = z->x + z->w - w;
                        y = z->y;
                        break;
                     default: // 0 // top-left
                        x = z->x;
                        y = z->y;
                        break;
                    }
               }
             evas_object_move(e_comp->fps_bg, x, y);
             evas_object_resize(e_comp->fps_bg, w, h);
             evas_object_move(e_comp->fps_fg, x + 4, y + 4);
          }
        else
          {
             e_comp->fps_bg = evas_object_rectangle_add(e_comp->evas);
             evas_object_color_set(e_comp->fps_bg, 0, 0, 0, 128);
             evas_object_layer_set(e_comp->fps_bg, E_LAYER_MAX);
             evas_object_name_set(e_comp->fps_bg, "e_comp->fps_bg");
             evas_object_lower(e_comp->fps_bg);
             evas_object_show(e_comp->fps_bg);

             e_comp->fps_fg = evas_object_text_add(e_comp->evas);
             evas_object_text_font_set(e_comp->fps_fg, "Sans", 10);
             evas_object_text_text_set(e_comp->fps_fg, "???");
             evas_object_color_set(e_comp->fps_fg, 255, 255, 255, 255);
             evas_object_layer_set(e_comp->fps_fg, E_LAYER_MAX);
             evas_object_name_set(e_comp->fps_bg, "e_comp->fps_fg");
             evas_object_stack_above(e_comp->fps_fg, e_comp->fps_bg);
             evas_object_show(e_comp->fps_fg);
          }
     }
   else
     {
        E_FREE_FUNC(e_comp->fps_fg, evas_object_del);
        E_FREE_FUNC(e_comp->fps_bg, evas_object_del);
     }
}

#ifdef ENABLE_HWC_MULTI
static void
_e_comp_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Comp_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_COMP_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_comp_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_comp_hooks[x] = eina_inlist_remove(_e_comp_hooks[x],
                                                EINA_INLIST_GET(ch));
          free(ch);
       }
}

static void
_e_comp_hook_call(E_Comp_Hook_Point hookpoint, void *data EINA_UNUSED)
{
   E_Comp_Hook *ch;

   _e_comp_hooks_walking++;
   EINA_INLIST_FOREACH(_e_comp_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, NULL);
     }
   _e_comp_hooks_walking--;
   if ((_e_comp_hooks_walking == 0) && (_e_comp_hooks_delete > 0))
     _e_comp_hooks_clean();
}

static int
_hwc_set(E_Output *eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL;
   E_Comp_HWC_Mode mode = E_HWC_MODE_NO;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   ep_l = e_output_planes_get(eout);
   EINA_LIST_REVERSE_FOREACH(ep_l, l , ep)
     {
        Eina_Bool set = EINA_FALSE;

        if (e_plane_is_fb_target(ep))
          {
             if (ep->prepare_ec)
               {
                  set = e_plane_ec_set(ep, ep->prepare_ec);
                  if (set)
                    {
                       ELOGF("HWC", "is set on fb_target( %d)", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                       mode = E_HWC_MODE_FULL;

                       // fb target is occupied by a client surface, means compositor disabled
                       ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
                    }
                  break; /* skip video layer */
               }
          }
        else if (ep->prepare_ec)
          {
             set = e_plane_ec_set(ep, ep->prepare_ec);
             if (set)
               {
                  ELOGF("HWC", "is set on %d", ep->prepare_ec->pixmap, ep->prepare_ec, ep->zpos);
                  mode |= E_HWC_MODE_HYBRID;
               }
             else
               break;
          }
     }

   return mode;
}

static Eina_Bool
_hwc_available_get(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   E_Output *eout;
   int transform = 0, minw = 0, minh = 0;

   if ((!cdata) ||
       (!cdata->buffer_ref.buffer) ||
       (cdata->width_from_buffer != cdata->width_from_viewport) ||
       (cdata->height_from_buffer != cdata->height_from_viewport) ||
       cdata->never_hwc)
     {
        return EINA_FALSE;
     }

   if (e_client_transform_core_enable_get(ec)) return EINA_FALSE;

   switch (cdata->buffer_ref.buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
         break;
      case E_COMP_WL_BUFFER_TYPE_TBM:
         if (cdata->buffer_ref.buffer->resource)
           break;
      case E_COMP_WL_BUFFER_TYPE_SHM:
         if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
           break;

      default:
         return EINA_FALSE;
     }

   if (e_comp_wl_tbm_buffer_sync_timeline_used(cdata->buffer_ref.buffer))
     return EINA_FALSE;

   /* if the buffer transform of surface is not same with output's transform, we
    * can't show it to HW overlay directly.
    */
   eout = e_output_find(ec->zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);

   tdm_output_get_available_size(eout->toutput, &minw, &minh, NULL, NULL, NULL);

   if ((minw > 0) && (minw > cdata->buffer_ref.buffer->w))
     return EINA_FALSE;
   if ((minh > 0) && (minh > cdata->buffer_ref.buffer->h))
     return EINA_FALSE;

   transform = e_comp_wl_output_buffer_transform_get(ec);
   if ((eout->config.rotation / 90) != transform)
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_hwc_prepare_init(E_Output *eout)
{
   const Eina_List *ep_l = NULL, *l ;
   E_Plane *ep = NULL;

   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!e_comp->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          continue;

        e_plane_ec_prepare_set(ep, NULL);
     }
}

static int
_hwc_prepare_cursor(E_Output *eout, int n_cur, Eina_List *hwc_clist)
{
   // policy for cursor layer
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *cur_ly = NULL;
   E_Plane *ep = NULL;
   int n_skip = 0;
   int n_curly = 0;
   int nouse = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_clist, EINA_FALSE);

   // list up cursor only layers
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (e_plane_is_cursor(ep))
          {
             cur_ly = eina_list_append(cur_ly, ep);
             continue;
          }
     }

   if (!cur_ly) return 0;
   n_curly = eina_list_count(cur_ly);

   if (n_cur > 0 && n_curly > 0)
     {
        if (n_cur >= n_curly) nouse = 0;
        else nouse = n_curly - n_cur;

        //assign cursor on cursor only layers
        EINA_LIST_REVERSE_FOREACH(cur_ly, l, ep)
          {
             E_Client *ec = NULL;
             if (nouse > 0)
               {
                  nouse--;
                  continue;
               }
             if (hwc_clist) ec = eina_list_data_get(hwc_clist);
             if (ec && e_plane_ec_prepare_set(ep, ec))
               {
                  n_skip += 1;
                  hwc_clist = eina_list_next(hwc_clist);
               }
          }
     }

   eina_list_free(cur_ly);

   return n_skip;
}

static Eina_Bool
_hwc_prepare(E_Output *eout, int n_vis, int n_skip, Eina_List *hwc_clist)
{
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *hwc_ly = NULL;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int n_ly = 0, n_ec = 0;
   E_Client *ec = NULL;
   Eina_Bool ret = EINA_FALSE;
   int nouse = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_clist, EINA_FALSE);

   n_ec = eina_list_count(hwc_clist);
   if (n_skip > 0)
     {
        int i;
        for (i = 0; i < n_skip; i++)
          hwc_clist = eina_list_next(hwc_clist);

        n_ec -= n_skip;
        n_vis -= n_skip;
     }

   if (n_ec <= 0) return EINA_FALSE;

   // list up available_hw layers E_Client can be set
   // if e_comp->hwc_use_multi_plane FALSE, than use only fb target plane
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!ep_fb)
          {
             if (e_plane_is_fb_target(ep))
               {
                  ep_fb = ep;
                  hwc_ly = eina_list_append(hwc_ly, ep);
               }
             continue;
          }
        if (!e_comp->hwc_use_multi_plane) continue;
        if (e_plane_is_cursor(ep)) continue;
        if (ep->zpos > ep_fb->zpos)
          hwc_ly = eina_list_append(hwc_ly, ep);
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_ly, EINA_FALSE);

   // finally, assign client on available_hw layers
   n_ly = eina_list_count(hwc_ly);
   if ((n_ec == n_vis) &&
       (n_ec <= n_ly)) // fully hwc
     {
        nouse = n_ly - n_ec;
     }
   else if ((n_ly < n_vis) || // e_comp->evas on fb target plane
            (n_ec < n_vis))
     {
        if (n_ec <= n_ly) nouse = n_ly - n_ec - 1;
        else nouse = 0;
     }

   EINA_LIST_REVERSE_FOREACH(hwc_ly, l, ep)
     {
        ec = NULL;
        if (nouse > 0)
          {
             nouse--;
             continue;
          }
        if (hwc_clist) ec = eina_list_data_get(hwc_clist);
        if (ec && e_plane_ec_prepare_set(ep, ec))
          {
             ret = EINA_TRUE;

             hwc_clist = eina_list_next(hwc_clist);
             n_ec--; n_vis--;
          }
        if (e_plane_is_fb_target(ep))
          {
             if (n_ec > 0 || n_vis > 0) e_plane_ec_prepare_set(ep, NULL);
             break;
          }
     }

   eina_list_free(hwc_ly);

   return ret;
}

/* get current tbm_surface (surface which has been committed last) for the e_client */
static tbm_surface_h
_e_comp_get_current_surface_for_cl(E_Client *ec)
{
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   if (!wl_comp_data) return NULL;

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if (!cdata) return NULL;

   E_Comp_Wl_Buffer_Ref *buffer_ref = &cdata->buffer_ref;

   E_Comp_Wl_Buffer *e_wl_buff = buffer_ref->buffer;
   if (!e_wl_buff) return NULL;

   return wayland_tbm_server_get_surface(wl_comp_data->tbm.server, e_wl_buff->resource);
}

/* this object represents 'hwc-able' e_client, that is e_client which will
 * be mapped to e_plane (set to e_plane) */
typedef struct
{
   E_Client *ec;
   tdm_hwc_window *hwc_wnd;
} e_client_hwc_wnd_pair_t;

/* remove no-valid (from hwc extension point of view) pair(s);
 * returned list may be empty */
static Eina_List*
_filter_pairs_by_hw(E_Output *eo, Eina_List *cl_hwc_wnd_pair_ls, int num_changes)
{
   e_client_hwc_wnd_pair_t *pair;
   Eina_List *l, *l_next;

   tdm_hwc_window_composition_t *composition_types;
   tdm_hwc_window **hwc_wnds;
   int i;

   INF("hwc-opt: filter list of 'e_client and hwc_widnow' pairs by hw.");

   hwc_wnds = calloc(num_changes, sizeof(tdm_hwc_window *));
   composition_types = calloc(num_changes, sizeof(tdm_hwc_window_composition_t));

   tdm_output_get_changed_composition_types(eo->toutput, &num_changes, hwc_wnds,
           composition_types);

   for (i = 0; i < num_changes; i++)
     {
        INF("hwc-opt: type's been changed to %d.", composition_types[i]);

        tdm_output_destroy_hwc_window(eo->toutput, hwc_wnds[i]);

        EINA_LIST_FOREACH_SAFE(cl_hwc_wnd_pair_ls, l, l_next, pair)
          {
             if (pair->hwc_wnd == hwc_wnds[i])
               {
                  free(pair);
                  cl_hwc_wnd_pair_ls = eina_list_remove_list(cl_hwc_wnd_pair_ls, l);
               }
          }
     }

   free(composition_types);
   free(hwc_wnds);

   return cl_hwc_wnd_pair_ls;
}

/* remove no-valid (from wm point of view) pair(s);
 * returned list may be empty */
static Eina_List*
_filter_pairs_by_wm(E_Output *eo, Eina_List *cl_hwc_wnd_pair_ls)
{
   e_client_hwc_wnd_pair_t *pair;
   Eina_List *l, *l_next;

   INF("hwc-opt: filter list of 'e_client and hwc_widnow' pairs by wm.");

   EINA_LIST_FOREACH_SAFE(cl_hwc_wnd_pair_ls, l, l_next, pair)
     {
        if (!pair->ec->hwc_acceptable)
          {
             tdm_output_destroy_hwc_window(eo->toutput, pair->hwc_wnd);

             free(pair);
             cl_hwc_wnd_pair_ls = eina_list_remove_list(cl_hwc_wnd_pair_ls, l);
          }
     }

   return cl_hwc_wnd_pair_ls;
}

/* create an 'e_client and hwc_widnow' pair for EACH clients in the 'cl_list' list;
 * create a hwc_window for EACH clients in the 'cl_list' list;
 * 'is_wm_prevented' to inform the caller side if any e_clients prevented
 * (to be set on hw overlay) by wm are */
static Eina_List*
_create_pairs_list(E_Output *eo, const Eina_List *cl_list, Eina_Bool *is_wm_prevented)
{
   Eina_List *cl_hwc_wnd_pair_ls = NULL;
   e_client_hwc_wnd_pair_t *pair;
   const Eina_List *l;
   E_Client *ec;
   int i;

   INF("hwc-opt: create list of 'e_client and hwc_widnow' pairs.");

   /* z-pos '0' is the lowest z-pos */
   i = 0;
   EINA_LIST_REVERSE_FOREACH(cl_list, l, ec)
     {
        tdm_hwc_window_info info;
        tdm_hwc_window *hwc_wnd;
        tbm_surface_h surface;

        hwc_wnd = tdm_output_create_hwc_window(eo->toutput, NULL);

        /* window manager could ask to prevent some e_clients being shown by hw directly */
        if (ec->hwc_acceptable)
          tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_DEVICE);
        else
          tdm_hwc_window_set_composition_type(hwc_wnd, TDM_COMPOSITION_CLIENT);

        tdm_hwc_window_set_zpos(hwc_wnd, i);

        memset(&info, 0, sizeof(info));

        info.src_config.pos.x = 0;
        info.src_config.pos.y = 0;
        info.src_config.pos.w = ec->w;
        info.src_config.pos.h = ec->h;

        /* do we have to fill out these? */
        info.src_config.size.h = ec->w;
        info.src_config.size.v = ec->h;

        /* do we have to fill out these? */
        info.src_config.format = TBM_FORMAT_ARGB8888;

        info.dst_pos.x = ec->x;
        info.dst_pos.y = ec->y;
        info.dst_pos.w = ec->w;
        info.dst_pos.h = ec->h;

        info.transform = TDM_TRANSFORM_NORMAL;

        tdm_hwc_window_set_info(hwc_wnd, &info);

        /* if e_client is in cl_list it means it has attached/committed
         * tbm_surface anyway
         *
         * NB: only an applicability of the e_client to own the hw overlay
         *     is checked here, no buffer fetching happens here however */
        surface = _e_comp_get_current_surface_for_cl(ec);
        tdm_hwc_window_set_buffer(hwc_wnd, surface);

        pair = calloc(1, sizeof(e_client_hwc_wnd_pair_t));
        pair->ec = ec;
        pair->hwc_wnd = hwc_wnd;

        cl_hwc_wnd_pair_ls = eina_list_append(cl_hwc_wnd_pair_ls, pair);
        i++;

        if (!ec->hwc_acceptable)
          is_wm_prevented = EINA_TRUE;

        INF("hwc-opt: hwc_wnd:%p, ec:%p, hwc_acceptable:%d, zpos:%d, surface:%p.",
                hwc_wnd, ec, ec->hwc_acceptable, i - 1, surface);
        INF("hwc-opt: pair(%p): {cl: %p, hwc_wnd:%p}.", pair, pair->ec, pair->hwc_wnd);
     }

   return cl_hwc_wnd_pair_ls;
}

/* reset e_plane to default state;
 * reset only hwc related things, things related to e_client are kept */
static void
_e_plane_reset(E_Plane *ep)
{
   if (ep && ep->hwc_wnd)
     {
        tdm_output_destroy_hwc_window(ep->output->toutput, ep->hwc_wnd);
        ep->hwc_wnd = NULL;
     }
}

/* cl_list - list of e_clients that we have to try to associate with e_planes,
 *           in case of the success e_client(s) will own hw overlay(s)
 *           this list contains ALL visible e_clients for this output ('eo'),
 *           at least one e_client
 */
static Eina_Bool
_hwc_optimized_prepare(E_Output *eo, Eina_List *cl_list)
{
   Eina_List *ep_list, *ep_list_dup;
   const Eina_List *l;
   E_Client *ec;
   E_Plane *ep;

   Eina_List *cl_hwc_wnd_pair_ls = NULL;
   e_client_hwc_wnd_pair_t *pair;
   int hwc_mode, num_changes;
   Eina_Bool is_wm_prevented; /* if any prevented (to be set on hw overlays) by wm e_clients are */

   ep_list = e_output_planes_get(eo);
   hwc_mode = E_HWC_MODE_FULL;
   is_wm_prevented = EINA_FALSE;
   num_changes = 0;


   /* new scheduling is the new scheduling... */
   EINA_LIST_FOREACH(ep_list, l, ep)
     _e_plane_reset(ep);

   /* create at least one 'e_client and hwc_window' pair;
    * !!! request a hw overlay for ALL clients, except prevented by wm !!! */
   cl_hwc_wnd_pair_ls = _create_pairs_list(eo, cl_list, &is_wm_prevented);

   INF("hwc-opt: number of pairs:%d.", eina_list_count(cl_hwc_wnd_pair_ls));

   /* make hwc extension choose which clients will own hw overlays,
    * by another point of view - make hwc extension choose which pair(s) are no-valid */
   tdm_output_validate(eo->toutput, &num_changes);

   /* thin out the 'e_client and hwc_window' pair list to leave only DEVICE-able pairs,
    * if hwc changed our requested types */
   if (num_changes)
     {
        INF("hwc-opt: hwc extension refused our request for full hw composition.");

        /* if hwc extension requested changes it's mean no way to make full hwc composition */
        hwc_mode = E_HWC_MODE_HYBRID;

        /* as a pair is a candidate to own a hw overlay it's useless to have pairs
         * which are forced by hw to be no-hwc-able */
        cl_hwc_wnd_pair_ls = _filter_pairs_by_hw(eo, cl_hwc_wnd_pair_ls, num_changes);

        /* at least now, it useless to call this function as we removed
         * hwc_windows hwc extension requested changes for */
        tdm_output_accept_changes(eo->toutput);
     }

   /* as a pair is a candidate to own a hw overlay it's useless to have pairs
    * which are forced by window manager to be no-hwc-able */
   cl_hwc_wnd_pair_ls = _filter_pairs_by_wm(eo, cl_hwc_wnd_pair_ls);

   /* if we got no valid pairs we have to turn hwc off at all */
   if (!eina_list_count(cl_hwc_wnd_pair_ls))
     return EINA_FALSE;

   if (is_wm_prevented)
     hwc_mode = E_HWC_MODE_HYBRID;

   /* here we have only valid (hwc-able) pairs OR have no pair at all */

   INF("hwc-opt: number of pairs (after thinning):%d.", eina_list_count(cl_hwc_wnd_pair_ls));

   ep_list_dup = ep_list;

   /* if hw couldn't satisfy our demand (to provide a hw overlay for EACH e_clients we asked) */
   if (hwc_mode != E_HWC_MODE_FULL)
     {
        E_Plane *fb_target_plane;

        INF("hwc-opt: hybrid composition.");

        /* TODO: is it correct to use always the lowest e_plane for 'fb_target'?
         *
         * as we've faced with no full hw composition we have to prepare 'fb_target' e_plane
         * to be used as a sink for the gles composition */
        fb_target_plane = e_output_fb_target_get(eo);

        /* it was done within _hwc_prepare_init() but to improve
         * readability this line was left here intentionally */
        e_plane_ec_prepare_set(fb_target_plane, NULL);

        /* skip an e_plane used as the 'fb_target' */
        ep_list_dup = eina_list_next(ep_list_dup);
     }
   else
     INF("hwc-opt: full hw composition.");

    /* create "e_clients to e_planes" association
     * amount of pairs is always less or equal then amount of e_planes,
     * e_planes and pairs are sorted in the same order (by z-pos) */
    EINA_LIST_FOREACH(cl_hwc_wnd_pair_ls, l, pair)
      {
         ep = eina_list_data_get(ep_list_dup);

         e_plane_ec_prepare_set(ep, pair->ec);
         ep->hwc_wnd = pair->hwc_wnd; /* pass ownership of hwc_window to e_plane */

         ep_list_dup = eina_list_next(ep_list_dup);

         INF("hwc-opt: create association -- pair(%p): {cl: %p, hwc_wnd:%p}, ep:%p", pair,
                 pair->ec, pair->hwc_wnd, ep);
      }

    INF("hwc-opt: clean up...");

    /* just clean up... (hwc_windows got owned by e_planes) */
    EINA_LIST_FREE(cl_hwc_wnd_pair_ls, pair)
      free(pair);

    return EINA_TRUE;
}

static Eina_Bool
_hwc_cancel(E_Output *eout)
{
   Eina_List *l ;
   E_Plane *ep;
   Eina_Bool ret = EINA_TRUE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!e_comp->hwc_use_multi_plane &&
            !e_plane_is_cursor(ep) &&
            !e_plane_is_fb_target(ep))
          {
             if (ep->ec) ret = EINA_FALSE; // core cannot end HWC
             continue;
          }

        e_plane_ec_prepare_set(ep, NULL);
        e_plane_ec_set(ep, NULL);
     }

   return ret;
}

static Eina_Bool
_hwc_reserved_clean()
{
   Eina_List *l, *ll;
   E_Zone *zone;
   E_Plane *ep;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output * eout;
        if (!zone->output_id) continue;
        eout = e_output_find(zone->output_id);
        EINA_LIST_FOREACH(eout->planes, ll, ep)
          {
             if (!e_comp->hwc_use_multi_plane &&
                 !e_plane_is_cursor(ep) &&
                 !e_plane_is_fb_target(ep))
               continue;

             if (e_plane_is_reserved(ep))
                e_plane_reserved_set(ep, 0);
          }
     }

   return EINA_TRUE;
}

static void
_hwc_plane_unset(E_Plane *ep)
{
   if (e_plane_is_reserved(ep))
     e_plane_reserved_set(ep, 0);

   e_plane_ec_prepare_set(ep, NULL);
   e_plane_ec_set(ep, NULL);

   ELOGF("HWC", "unset plane %d to NULL", NULL, NULL, ep->zpos);
}

static Eina_Bool
_hwc_plane_change_ec(E_Plane *ep, E_Client *old_ec, E_Client *new_ec)
{
   Eina_Bool ret = EINA_FALSE;

   if (!new_ec)
     {
        if (e_plane_is_reserved(ep))
          e_plane_reserved_set(ep, 0);
     }

   e_plane_ec_prepare_set(ep, NULL);

   if (e_plane_ec_set(ep, new_ec))
     {
        if (new_ec)
          {
             ELOGF("HWC", "new_ec(%s) is set on %d",
                   new_ec->pixmap, new_ec,
                   e_client_util_name_get(new_ec) ? new_ec->icccm.name : "no name", ep->zpos);
          }
        else
          {
             ELOGF("HWC", "NULL is set on %d", NULL, NULL, ep->zpos);
          }
        ret = EINA_TRUE;
     }
   else
     {
        ELOGF("HWC", "failed to set new_ec(%s) on %d",
              NULL, new_ec,
              new_ec ? (new_ec->icccm.name ? new_ec->icccm.name : "no name") : "NULL",
              ep->zpos);
     }

   return ret;
}

static Eina_Bool
_e_comp_hwc_apply(E_Output * eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int mode = 0;

   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!ep_fb)
          {
             if (e_plane_is_fb_target(ep))
               {
                  ep_fb = ep;
                  if (ep->prepare_ec != NULL) goto hwcompose;
               }
             continue;
          }
        if (ep->zpos > ep_fb->zpos)
          if (ep->prepare_ec != NULL) goto hwcompose;
     }

   goto compose;

hwcompose:
   mode = _hwc_set(eout);
   if (mode == E_HWC_MODE_NO) ELOGF("HWC", "it is failed to assign surface on plane", NULL, NULL);

compose:
   if (mode != E_HWC_MODE_NO) e_comp->hwc_mode = mode;

   return !!mode;
}

static Eina_Bool
_e_comp_hwc_changed(void)
{
   Eina_List *l;
   E_Zone *zone;
   Eina_Bool ret = EINA_FALSE;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *eout = NULL;
        E_Plane *ep = NULL;
        const Eina_List *ep_l = NULL, *p_l;
        Eina_Bool assign_success = EINA_TRUE;
        int mode = E_HWC_MODE_NO;

        if (!zone || !zone->output_id) continue;

        eout = e_output_find(zone->output_id);
        ep_l = e_output_planes_get(eout);
        EINA_LIST_REVERSE_FOREACH(ep_l, p_l, ep)
          {
             if (!assign_success)
               {
                  //unset planes from 'assign_success' became EINA_FALSE to the fb target
                  _hwc_plane_unset(ep);
                  continue;
               }

             if (ep->ec != ep->prepare_ec)
               {
                  assign_success = _hwc_plane_change_ec(ep, ep->ec, ep->prepare_ec);
                  ret = EINA_TRUE;
               }
             else if (!ep->prepare_ec)
               {
                  if (e_plane_is_reserved(ep))
                    {
                       e_plane_reserved_set(ep, 0);
                       ELOGF("HWC", "unset reserved mem on %d", NULL, NULL, ep->zpos);
                    }
               }

             if (ep->ec) mode = E_HWC_MODE_HYBRID;

             if (e_plane_is_fb_target(ep))
               {
                  if (ep->ec) mode = E_HWC_MODE_FULL;
                  break;
               }
          }

        if (e_comp->hwc_mode != mode)
          {
             ELOGF("HWC", "mode changed (from %d to %d) due to surface changes",
                   NULL, NULL,
                   e_comp->hwc_mode, mode);

             if (mode == E_HWC_MODE_FULL)
               {
                  // fb target is occupied by a client surface, means compositor disabled
                  ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
               }
             else if (e_comp->hwc_mode == E_HWC_MODE_FULL)
               {
                  // fb target is occupied by a client surface, means compositor disabled
                  ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
               }

             e_comp->hwc_mode = mode;
          }
     }

   return ret;
}

/* filter visible clients by the window manager
 *
 * returns list of clients which are acceptable to be composited by hw,
 * it's a caller responsibility to free it
 *
 * for optimized hwc the returned list contains ALL clients
 */
static Eina_List *
_e_comp_filter_cl_by_wm(Eina_List *vis_cl_list, int *n_cur)
{
   Eina_List *hwc_acceptable_cl_list = NULL;
   Eina_List *l;
   E_Client *ec;
   int n_ec = 0;

   if (!n_cur) return NULL;

   *n_cur = 0;

   if (e_comp->hwc_optimized)
     {
        /* let's hope for the best... */
        EINA_LIST_FOREACH(vis_cl_list, l, ec)
        {
          ec->hwc_acceptable = EINA_TRUE;
          INF("hwc-opt: ec:%p (name:%s, title:%s) is gonna be hwc_acceptable.",
                  ec, ec->icccm.name, ec->icccm.title);
        }
     }

   EINA_LIST_FOREACH(vis_cl_list, l, ec)
     {
        // check clients not able to use hwc

        /* window manager required full GLES composition */
        if (e_comp->hwc_optimized && e_comp->nocomp_override > 0)
          {
             ec->hwc_acceptable = EINA_FALSE;
             INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable(nocomp_override > 0).",
                                  ec, ec->icccm.name, ec->icccm.title);
          }

        // if ec->frame is not for client buffer (e.g. launchscreen)
        if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE ||

            // if there is UI subfrace, it means need to composite
            e_client_normal_client_has(ec))
          {
             if (!e_comp->hwc_optimized) goto no_hwc;

             /* we have to let hwc know about ALL clients(buffers) in case we're using
              * optimized hwc, that's why it can be called optimized :), but also we have to provide
              * the ability for wm to prevent some clients to be shown by hw directly */
             ec->hwc_acceptable = EINA_FALSE;
             INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable (UI subsurface).",
                     ec, ec->icccm.name, ec->icccm.title);
          }

        // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
        if (!_hwc_available_get(ec))
          {
             if (!e_comp->hwc_optimized)
               {
                  if (!n_ec) goto no_hwc;
                  break;
               }

             ec->hwc_acceptable = EINA_FALSE;
             INF("hwc-opt: prevent ec:%p (name:%s, title:%s) to be hwc_acceptable.",
                     ec, ec->icccm.name, ec->icccm.title);
          }

        // listup as many as possible from the top most visible order
        n_ec++;
        if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role)) (*n_cur)++;
        hwc_acceptable_cl_list = eina_list_append(hwc_acceptable_cl_list, ec);
     }

   return hwc_acceptable_cl_list;

no_hwc:

   eina_list_free(hwc_acceptable_cl_list);

   return NULL;
}

static Eina_Bool
_e_comp_hwc_prepare(void)
{
   Eina_List *l;
   E_Zone *zone;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_comp->hwc, EINA_FALSE);

   INF("hwc-opt: we have something which causes to reschedule 'e_cliens to e_planes' mapping.");

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *output;
        int n_vis = 0, n_cur = 0, n_skip = 0;
        Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;

        if (!zone || !zone->output_id) continue;  // no hw layer

        output = e_output_find(zone->output_id);
        if (!output) continue;

        vis_clist = e_comp_vis_ec_list_get(zone);
        if (!vis_clist)
        {
            INF("hwc-opt: no visible clients, why we're here?");
            continue;
        }

        INF("hwc-opt: number of visible clients:%d.", eina_list_count(vis_clist));

        /* by demand of window manager to prevent some e_clients to be shown by hw directly */
        hwc_ok_clist = _e_comp_filter_cl_by_wm(vis_clist, &n_cur);
        if (!hwc_ok_clist) goto nextzone;

        n_vis = eina_list_count(vis_clist);
        if ((n_vis < 1) || (eina_list_count(hwc_ok_clist) < 1))
          goto nextzone;

        INF("hwc-opt: number of clients which are gonna own hw overlays:%d.", eina_list_count(hwc_ok_clist));

        _hwc_prepare_init(output);

        if (n_cur >= 1)
          n_skip = _hwc_prepare_cursor(output, n_cur, hwc_ok_clist);

        if (n_skip > 0) ret = EINA_TRUE;

        /* we don't worry about cursors now... */
        if (e_comp->hwc_optimized)
          ret |= _hwc_optimized_prepare(output, hwc_ok_clist);
        else
          ret |= _hwc_prepare(output, n_vis, n_skip, hwc_ok_clist);

        nextzone:
        eina_list_free(hwc_ok_clist);
        eina_list_free(vis_clist);
     }

   return ret;
}

static Eina_Bool
_e_comp_hwc_usable(void)
{
   Eina_List *l;
   E_Zone *zone;
   Eina_Bool ret = EINA_FALSE;

   if (!e_comp->hwc) return EINA_FALSE;

   // check whether to use hwc
   // core assignment policy
   ret = _e_comp_hwc_prepare();
   if (!ret) return EINA_FALSE;

   // extra policy can replace core policy
   _e_comp_hook_call(E_COMP_HOOK_PREPARE_PLANE, NULL);

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *eout = NULL;
        E_Plane *ep = NULL, *ep_fb = NULL;
        const Eina_List *ep_l = NULL, *p_l;

        if (!zone || !zone->output_id) continue;

        eout = e_output_find(zone->output_id);
        ep_l = e_output_planes_get(eout);

        if ((eout->cursor_available.max_w == -1) ||
            (eout->cursor_available.max_h == -1))
          {
             // hw cursor is not supported by libtdm, than let's composite
             if (!e_pointer_is_hidden(e_comp->pointer)) return EINA_FALSE;
          }

        EINA_LIST_FOREACH(ep_l, p_l, ep)
          {
             E_Comp_Wl_Buffer *buffer = NULL;

             if (ep->prepare_ec)
               {
                  buffer = e_pixmap_resource_get(ep->prepare_ec->pixmap);

                  if (!buffer)
                    {
                       // if attached buffer is not valid than hwc is not usable
                       DBG("Cannot use HWC due to invalid pixmap");
                       return EINA_FALSE;
                    }
               }

             if (!ep_fb)
               {
                  if (e_plane_is_fb_target(ep))
                    {
                       int bw = 0, bh = 0;

                       ep_fb = ep;

                       if (ep_fb->prepare_ec)
                         e_pixmap_size_get(ep->prepare_ec->pixmap, &bw, &bh);

                       if (ep_fb->prepare_ec &&
                           ep_fb->reserved_memory &&
                           ((bw != zone->w) || (bh != zone->h) ||
                            (ep_fb->prepare_ec->x != zone->x) || (ep_fb->prepare_ec->y != zone->y) ||
                            (ep_fb->prepare_ec->w != zone->w) || (ep_fb->prepare_ec->h != zone->h)))
                         {
                            // if client and zone's geometry is not match with, or
                            // if plane with reserved_memory(esp. fb target) has assigned smaller buffer,
                            // won't support hwc properly, than let's composite
                            DBG("Cannot use HWC if geometry is not 1 on 1 match with reserved_memory");
                            return EINA_FALSE;
                         }
                       else if (ep_fb->prepare_ec != NULL)
                         {
                            return EINA_TRUE;
                         }
                    }
                  continue;
               }
             if (ep->zpos > ep_fb->zpos)
               if (ep->prepare_ec != NULL) return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

static void
_e_comp_hwc_begin(void)
{
   Eina_List *l;
   E_Zone *zone;
   Eina_Bool mode_set = EINA_FALSE;

   E_FREE_FUNC(e_comp->nocomp_delay_timer, ecore_timer_del);

   if (!e_comp->hwc) return;
   if (e_comp->nocomp_override > 0) return;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output * eout;
        if (!zone->output_id) continue;
        eout = e_output_find(zone->output_id);
        if(eout) mode_set |= _e_comp_hwc_apply(eout);
     }

   if (!mode_set) return;
   if (!e_comp->hwc_mode) return;

   if (e_comp->calc_fps) e_comp->frametimes[0] = 0;

   ELOGF("HWC", " Begin ...", NULL, NULL);
}

static Eina_Bool
_e_comp_hwc_cb_begin_timeout(void *data EINA_UNUSED)
{
   e_comp->nocomp_delay_timer = NULL;

   if (e_comp->nocomp_override == 0)
     {
        e_comp_render_queue();
     }
   return EINA_FALSE;
}

E_API void
e_comp_hwc_end(const char *location)
{
   Eina_Bool mode_set = EINA_FALSE;
   E_Zone *zone;
   Eina_List *l;
   Eina_Bool fully_hwc = (e_comp->hwc_mode == E_HWC_MODE_FULL) ? EINA_TRUE : EINA_FALSE;

   E_FREE_FUNC(e_comp->nocomp_delay_timer, ecore_timer_del);
   _hwc_reserved_clean();

   if (!e_comp->hwc) return;
   if (!e_comp->hwc_mode) return;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output * eout;
        if (!zone->output_id) continue;
        eout = e_output_find(zone->output_id);
        if (eout) mode_set |= _hwc_cancel(eout);
     }

   if (!mode_set) return;

   e_comp->hwc_mode = E_HWC_MODE_NO;

   if (fully_hwc)
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

   ELOGF("HWC", " End...  at %s.", NULL, NULL, location);
}

EINTERN void
e_comp_hwc_multi_plane_set(Eina_Bool set)
{
   if (e_comp->hwc_use_multi_plane == set) return;

   e_comp_hwc_end(__FUNCTION__);
   e_comp->hwc_use_multi_plane = set;

   ELOGF("HWC", "e_comp_hwc_multi_plane_set : %d", NULL, NULL, set);
}
#endif  // end of ENABLE_HWC_MULTI

static Eina_Bool
_e_comp_cb_update(void)
{
   E_Client *ec;
   Eina_List *l;
   int pw, ph, w, h;
   Eina_Bool res;

   if (!e_comp) return EINA_FALSE;

   TRACE_DS_BEGIN(COMP:UPDATE CB);

   if (e_comp->update_job)
     e_comp->update_job = NULL;
   else
     ecore_animator_freeze(e_comp->render_animator);

   DBG("UPDATE ALL");

   if (conf->grab && (!e_comp->grabbed))
     {
        if (e_comp->grab_cb) e_comp->grab_cb();
        e_comp->grabbed = 1;
     }
   l = e_comp->updates;
   e_comp->updates = NULL;
   EINA_LIST_FREE(l, ec)
     {
        /* clear update flag */
        e_comp_object_render_update_del(ec->frame);

        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_comp->hwc && e_comp_is_on_overlay(ec)) continue;

        /* update client */
        e_pixmap_size_get(ec->pixmap, &pw, &ph);

        if (e_pixmap_dirty_get(ec->pixmap))
          {
             if (e_pixmap_refresh(ec->pixmap) &&
                 e_pixmap_size_get(ec->pixmap, &w, &h) &&
                 e_pixmap_size_changed(ec->pixmap, pw, ph))
               {
                  e_pixmap_image_clear(ec->pixmap, 0);
               }
             else if (!e_pixmap_size_get(ec->pixmap, NULL, NULL))
               {
                  WRN("FAIL %p", ec);
                  e_comp_object_redirected_set(ec->frame, 0);
                  if (e_pixmap_failures_get(ec->pixmap) < 3)
                    e_comp_object_render_update_add(ec->frame);
               }
          }

        if (e_comp->saver) continue;

        res = e_pixmap_size_get(ec->pixmap, &pw, &ph);
        if (!res) continue;

        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_dirty(ec->frame);
     }

   if (e_comp->hwc_mode == E_HWC_MODE_FULL) goto setup_hwcompose;

#ifndef ENABLE_HWC_MULTI
   if (conf->fps_show || e_comp->calc_fps)
     {
        _e_comp_fps_update();
     }
#endif // end of ENABLE_HWC_MULTI

   if (conf->lock_fps)
     {
        DBG("MANUAL RENDER...");
     }

   if (conf->grab && e_comp->grabbed)
     {
        if (e_comp->grab_cb) e_comp->grab_cb();
        e_comp->grabbed = 0;
     }
   if (e_comp->updates && (!e_comp->update_job))
     ecore_animator_thaw(e_comp->render_animator);

setup_hwcompose:
#ifdef ENABLE_HWC_MULTI
   // query if HWC can be used
   if (!e_comp->hwc ||
       e_comp->hwc_deactive)
     {
        goto end;
     }

   if(_e_comp_hwc_usable())
     {
        if (e_comp->hwc_mode)
          {
             if (_e_comp_hwc_changed())
               {
                  if (e_comp->hwc_mode == E_HWC_MODE_NO)
                    ELOGF("HWC", " End...  due to surface changes", NULL, NULL);
                  else
                    ELOGF("HWC", " hwc surface changed", NULL, NULL);
               }
          }
        else
          {
             // switch mode
             if (conf->nocomp_use_timer)
               {
                  if (!e_comp->nocomp_delay_timer)
                    {
                       e_comp->nocomp_delay_timer = ecore_timer_add(conf->nocomp_begin_timeout,
                                                                     _e_comp_hwc_cb_begin_timeout,
                                                                     NULL);
                    }
               }
             else
               {
                  _e_comp_hwc_begin();
               }
          }
     }
   else
     {
        e_comp_hwc_end(__FUNCTION__);
     }

end:
#endif  // end of ENABLE_HWC_MULTI
   TRACE_DS_END();

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_cb_job(void *data EINA_UNUSED)
{
   DBG("UPDATE ALL JOB...");
   _e_comp_cb_update();
}

static Eina_Bool
_e_comp_cb_animator(void *data EINA_UNUSED)
{
   return _e_comp_cb_update();
}

//////////////////////////////////////////////////////////////////////////


#ifdef SHAPE_DEBUG
static void
_e_comp_shape_debug_rect(Eina_Rectangle *rect, E_Color *color)
{
   Evas_Object *o;

#define COLOR_INCREMENT 30
   o = evas_object_rectangle_add(e_comp->evas);
   if (color->r < 256 - COLOR_INCREMENT)
     evas_object_color_set(o, (color->r += COLOR_INCREMENT), 0, 0, 255);
   else if (color->g < 256 - COLOR_INCREMENT)
     evas_object_color_set(o, 0, (color->g += COLOR_INCREMENT), 0, 255);
   else
     evas_object_color_set(o, 0, 0, (color->b += COLOR_INCREMENT), 255);
   evas_object_repeat_events_set(o, 1);
   evas_object_layer_set(o, E_LAYER_EFFECT - 1);
   evas_object_move(o, rect->x, rect->y);
   evas_object_resize(o, rect->w, rect->h);
   e_comp->debug_rects = eina_list_append(e_comp->debug_rects, o);
   evas_object_show(o);
}
#endif

static Eina_Bool
_e_comp_shapes_update_object_checker_function_thingy(Evas_Object *o)
{
   Eina_List *l;
   E_Zone *zone;

   if (o == e_comp->bg_blank_object) return EINA_TRUE;
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        if ((o == zone->over) || (o == zone->base)) return EINA_TRUE;
        if ((o == zone->bg_object) || (o == zone->bg_event_object) ||
            (o == zone->bg_clip_object) || (o == zone->prev_bg_object) ||
            (o == zone->transition_object))
          return EINA_TRUE;
     }
   return EINA_FALSE;
}

static void
#ifdef SHAPE_DEBUG
_e_comp_shapes_update_comp_client_shape_comp_helper(E_Client *ec, Eina_Tiler *tb, Eina_List **rl)
#else
_e_comp_shapes_update_comp_client_shape_comp_helper(E_Client *ec, Eina_Tiler *tb)
#endif
{
   int x, y, w, h;

   /* ignore deleted shapes */
   if (e_object_is_del(E_OBJECT(ec)))
     {
        SHAPE_INF("IGNORING DELETED: %p", ec);
        return;
     }
   if ((!ec->visible) || (ec->hidden) || (!evas_object_visible_get(ec->frame)) || evas_object_pass_events_get(ec->frame))
     {
        SHAPE_DBG("SKIPPING SHAPE FOR %p", ec);
        return;
     }
#ifdef SHAPE_DEBUG
   INF("COMP EC: %p", ec);
#endif

   if (ec->shaped || ec->shaped_input)
     {
        int num, tot;
        int l, r, t, b;
        Eina_Rectangle *rect, *rects;

        /* add the frame */
        e_comp_object_frame_geometry_get(ec->frame, &l, &r, &t, &b);
        e_comp_object_frame_extends_get(ec->frame, &x, &y, &w, &h);
        if ((l + x) || (r + (w - ec->w + x)) || (t - y) || (b + (h - ec->h + y)))
          {
             if (t - y)
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, w, t - y});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, w, t - y);
               }
             if (l - x)
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, l - x, h});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, l - x, h);
               }
             if (r + (w - ec->w + x))
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + l + ec->client.w + x, ec->y + y, r + (w - ec->w + x), h});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + l + ec->client.w + x, ec->y + y, r + (w - ec->w + x), h);
               }
             if (b + (h - ec->h + y))
               {
                  eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + t + ec->client.h + y, w, b + (h - ec->h + y)});
                  SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + t + ec->client.h + y, w, b + (h - ec->h + y));
               }
          }
        rects = ec->shape_rects ?: ec->shape_input_rects;
        tot = ec->shape_rects_num ?: ec->shape_input_rects_num;
        for (num = 0, rect = rects; num < tot; num++, rect++)
          {
             x = rect->x, y = rect->y, w = rect->w, h = rect->h;
             x += ec->client.x, y += ec->client.y;
             E_RECTS_CLIP_TO_RECT(x, y, w, h, 0, 0, e_comp->w, e_comp->h);
             if ((w < 1) || (h < 1)) continue;
   //#ifdef SHAPE_DEBUG not sure we can shape check these?
             //r = E_NEW(Eina_Rectangle, 1);
             //EINA_RECTANGLE_SET(r, x, y, w, h);
             //rl = eina_list_append(rl, r);
   //#endif
             eina_tiler_rect_del(tb, &(Eina_Rectangle){x, y, w, h});
             SHAPE_INF("DEL: %d,%d@%dx%d", x, y, w, h);
          }
        return;
     }

#ifdef SHAPE_DEBUG
     {
        Eina_Rectangle *r;

        r = E_NEW(Eina_Rectangle, 1);
        EINA_RECTANGLE_SET(r, ec->client.x, ec->client.y, ec->client.w, ec->client.h);
        *rl = eina_list_append(*rl, r);
     }
#endif

   if (!e_client_util_borderless(ec))
     {
        e_comp_object_frame_extends_get(ec->frame, &x, &y, &w, &h);
        /* add the frame */
        eina_tiler_rect_add(tb, &(Eina_Rectangle){ec->x + x, ec->y + y, w, h});
        SHAPE_INF("ADD: %d,%d@%dx%d", ec->x + x, ec->y + y, w, h);
     }

   if ((!ec->shaded) && (!ec->shading))
     {
        /* delete the client if not shaded */
        eina_tiler_rect_del(tb, &(Eina_Rectangle){ec->client.x, ec->client.y, ec->client.w, ec->client.h});
        SHAPE_INF("DEL: %d,%d@%dx%d", ec->client.x, ec->client.y, ec->client.w, ec->client.h);
     }
}

static void
_e_comp_shapes_update_object_shape_comp_helper(Evas_Object *o, Eina_Tiler *tb)
{
   int x, y, w, h;

   /* ignore hidden and pass-event objects */
   if ((!evas_object_visible_get(o)) || evas_object_pass_events_get(o) || evas_object_repeat_events_get(o)) return;
   /* ignore canvas objects */
   if (_e_comp_shapes_update_object_checker_function_thingy(o)) return;
   SHAPE_INF("OBJ: %p:%s", o, evas_object_name_get(o));
   evas_object_geometry_get(o, &x, &y, &w, &h);
   eina_tiler_rect_add(tb, &(Eina_Rectangle){x, y, w, h});
   SHAPE_INF("ADD: %d,%d@%dx%d", x, y, w, h);
}

static void
_e_comp_shapes_update_job(void *d EINA_UNUSED)
{
   Eina_Tiler *tb;
   E_Client *ec;
   Evas_Object *o = NULL;
   Eina_Rectangle *tr;
   Eina_Iterator *ti;
   Eina_Rectangle *exr;
   unsigned int i, tile_count;
#ifdef SHAPE_DEBUG
   Eina_Rectangle *r;
   Eina_List *rl = NULL;
   E_Color color = {0};

   INF("---------------------");
#endif

   E_FREE_LIST(e_comp->debug_rects, evas_object_del);
   tb = eina_tiler_new(e_comp->w, e_comp->h);
   EINA_SAFETY_ON_NULL_GOTO(tb, tb_fail);

   eina_tiler_tile_size_set(tb, 1, 1);
   /* background */
   eina_tiler_rect_add(tb, &(Eina_Rectangle){0, 0, e_comp->w, e_comp->h});

   ec = e_client_bottom_get();
   if (ec) o = ec->frame;
   for (; o; o = evas_object_above_get(o))
     {
        int layer;

        layer = evas_object_layer_get(o);
        if (e_comp_canvas_client_layer_map(layer) == 9999) //not a client layer
          {
             _e_comp_shapes_update_object_shape_comp_helper(o, tb);
             continue;
          }
        ec = e_comp_object_client_get(o);
        if (ec && (!ec->no_shape_cut))
          _e_comp_shapes_update_comp_client_shape_comp_helper(ec, tb
#ifdef SHAPE_DEBUG
                                                           ,&rl
#endif
                                                          );

        else
          _e_comp_shapes_update_object_shape_comp_helper(o, tb);
     }

   ti = eina_tiler_iterator_new(tb);
   EINA_SAFETY_ON_NULL_GOTO(ti, ti_fail);
   tile_count = 128;

   exr = malloc(sizeof(Eina_Rectangle) * tile_count);
   EINA_SAFETY_ON_NULL_GOTO(exr, exr_fail);

   i = 0;
   EINA_ITERATOR_FOREACH(ti, tr)
     {
        exr[i++] = *(Eina_Rectangle*)((char*)tr);
        if (i == tile_count - 1)
          {
             exr = realloc(exr, sizeof(Eina_Rectangle) * (tile_count *= 2));
             EINA_SAFETY_ON_NULL_GOTO(exr, exr_fail);
          }
#ifdef SHAPE_DEBUG
        Eina_List *l;

        _e_comp_shape_debug_rect(&exr[i - 1], &color);
        INF("%d,%d @ %dx%d", exr[i - 1].x, exr[i - 1].y, exr[i - 1].w, exr[i - 1].h);
        EINA_LIST_FOREACH(rl, l, r)
          {
             if (E_INTERSECTS(r->x, r->y, r->w, r->h, tr->x, tr->y, tr->w, tr->h))
               ERR("POSSIBLE RECT FAIL!!!!");
          }
#endif
     }

exr_fail:
   free(exr);
ti_fail:
   eina_iterator_free(ti);
#ifdef SHAPE_DEBUG
   E_FREE_LIST(rl, free);
   printf("\n");
#endif
tb_fail:
   eina_tiler_free(tb);
   e_comp->shape_job = NULL;
}

//////////////////////////////////////////////////////////////////////////


static Eina_Bool
_e_comp_key_down(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Event_Key *ev)
{
   if ((!strcasecmp(ev->key, "f")) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_SHIFT) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_CTRL) &&
       (ev->modifiers & ECORE_EVENT_MODIFIER_ALT))
     {
        e_comp_canvas_fps_toggle();
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_signal_user(void *data EINA_UNUSED, int type EINA_UNUSED, Ecore_Event_Signal_User *ev)
{
   if (ev->number == 1)
     {
        // e uses this to pop up config panel
     }
   else if (ev->number == 2)
     {
        e_comp_canvas_fps_toggle();
     }
   return ECORE_CALLBACK_PASS_ON;
}

//////////////////////////////////////////////////////////////////////////

static void
_e_comp_free(E_Comp *c)
{
   Eina_List *l, *ll;
   E_Zone *zone;

   EINA_LIST_FOREACH_SAFE(c->zones, l, ll, zone)
     {
        e_object_del(E_OBJECT(zone));
     }

   e_comp_canvas_clear();

   ecore_evas_free(c->ee);
   eina_stringshare_del(c->name);

   if (c->render_animator) ecore_animator_del(c->render_animator);
   if (c->update_job) ecore_job_del(c->update_job);
   if (c->screen_job) ecore_job_del(c->screen_job);
   if (c->nocomp_delay_timer) ecore_timer_del(c->nocomp_delay_timer);
   if (c->nocomp_override_timer) ecore_timer_del(c->nocomp_override_timer);
   ecore_job_del(c->shape_job);

   free(c);
}

//////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_comp_object_add(void *d EINA_UNUSED, int t EINA_UNUSED, E_Event_Comp_Object *ev)
{
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_comp_override_expire(void *data EINA_UNUSED)
{
   e_comp->nocomp_override_timer = NULL;
   e_comp->nocomp_override--;

   if (e_comp->nocomp_override <= 0)
     {
        e_comp->nocomp_override = 0;
        e_comp_render_queue();
     }
   return EINA_FALSE;
}

//////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_comp_screensaver_on(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screensaver_off(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   return ECORE_CALLBACK_PASS_ON;
}

//////////////////////////////////////////////////////////////////////////

EINTERN Eina_Bool
e_comp_init(void)
{
   _e_comp_log_dom = eina_log_domain_register("e_comp", EINA_COLOR_YELLOW);
   eina_log_domain_level_set("e_comp", EINA_LOG_LEVEL_INFO);

   ecore_frametime = ecore_animator_frametime_get();

   E_EVENT_COMPOSITOR_RESIZE = ecore_event_type_new();
   E_EVENT_COMP_OBJECT_ADD = ecore_event_type_new();
   E_EVENT_COMPOSITOR_DISABLE = ecore_event_type_new();
   E_EVENT_COMPOSITOR_ENABLE = ecore_event_type_new();
   E_EVENT_COMPOSITOR_FPS_UPDATE = ecore_event_type_new();

   ignores = eina_hash_pointer_new(NULL);

   e_main_ts("\tE_Comp_Data Init");
   e_comp_cfdata_edd_init(&conf_edd, &conf_match_edd);
   e_main_ts("\tE_Comp_Data Init Done");

   e_main_ts("\tE_Comp_Data Load");
   conf = e_config_domain_load("e_comp", conf_edd);
   e_main_ts("\tE_Comp_Data Load Done");

   if (!conf)
     {
        e_main_ts("\tE_Comp_Data New");
        conf = e_comp_cfdata_config_new();
        e_main_ts("\tE_Comp_Data New Done");
     }

   /* HWC, in terms of E20's architecture, is a part of E20 responsible for hardware compositing
    *
    * - no-optimized HWC takes away, from the evas engine compositor, a part of the composition
    * work without an assumption was that part worthy(optimally) to be delegated to hardware;
    * - optimized HWC makes this assumption (delegate it to tdm-backend, to be exact);
    *
    * of course if the tdm-backend makes no optimization these HWCs behave equally...
    *
    * when we're talking about 'optimized' we mean optimized by power consumption criteria.
    */
   if (conf->hwc_optimized)
     INF("hwc-opt: E20's gonna use optimized hwc.");
   else
     INF("hwc-opt: E20's gonna use no-optimized hwc.");

   // comp config versioning - add this in. over time add epochs etc. if
   // necessary, but for now a simple version number will do
   if (conf->version < E_COMP_VERSION)
     {
        switch (conf->version)
          {
           case 0:
             // going from version 0 we should disable grab for smoothness
             conf->grab = 0;
             /* fallthrough */
           default:
             break;
          }
        e_config_save_queue();
        conf->version = E_COMP_VERSION;
     }

   e_comp_new();

   if (conf->hwc_ignore_primary) e_comp->hwc_ignore_primary = EINA_TRUE;
   if (conf->hwc_optimized) e_comp->hwc_optimized = EINA_TRUE;

   e_main_ts("\tE_Comp_Screen Init");
   if (!e_comp_screen_init())
     {
        ERR("Fail to init e_comp_screen");
        e_object_del(E_OBJECT(e_comp));
        E_FREE_FUNC(ignores, eina_hash_free);
        return EINA_FALSE;
     }
   e_main_ts("\tE_Comp_Screen Init Done");

   e_comp->comp_type = E_PIXMAP_TYPE_WL;

   e_comp_canvas_fake_layers_init();

   if (conf->hwc) e_comp->hwc = EINA_TRUE; // activate hwc policy
   if (conf->hwc_deactive) e_comp->hwc_deactive = EINA_TRUE; // deactive hwc policy
   if (conf->hwc_reuse_cursor_buffer) e_comp->hwc_reuse_cursor_buffer = EINA_TRUE;
   if (conf->hwc_sync_mode_change) e_comp->hwc_sync_mode_change = EINA_TRUE;
#ifdef ENABLE_HWC_MULTI
   if (conf->hwc_use_multi_plane) e_comp->hwc_use_multi_plane = EINA_TRUE;
#endif

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_ON,  _e_comp_screensaver_on,  NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_OFF, _e_comp_screensaver_off, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_DOWN,    _e_comp_key_down,        NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_SIGNAL_USER, _e_comp_signal_user,     NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_COMP_OBJECT_ADD, _e_comp_object_add,      NULL);

   return EINA_TRUE;
}

E_API E_Comp *
e_comp_new(void)
{
   if (e_comp)
     CRI("CANNOT REPLACE EXISTING COMPOSITOR");
   e_comp = E_OBJECT_ALLOC(E_Comp, E_COMP_TYPE, _e_comp_free);
   if (!e_comp) return NULL;

   e_comp->name = eina_stringshare_add(_("Compositor"));
   e_comp->render_animator = ecore_animator_add(_e_comp_cb_animator, NULL);
   ecore_animator_freeze(e_comp->render_animator);
   return e_comp;
}

E_API int
e_comp_internal_save(void)
{
   return e_config_domain_save("e_comp", conf_edd, conf);
}

EINTERN int
e_comp_shutdown(void)
{
   Eina_List *l, *ll;
   E_Client *ec;

   E_FREE_FUNC(action_timeout, ecore_timer_del);
   EINA_LIST_FOREACH_SAFE(e_comp->clients, l, ll, ec)
     {
        DELD(ec, 99999);
        e_object_del(E_OBJECT(ec));
     }

   e_comp_wl_shutdown();
   e_comp_screen_shutdown();

   e_object_del(E_OBJECT(e_comp));
   E_FREE_LIST(handlers, ecore_event_handler_del);
   E_FREE_LIST(actions, e_object_del);
   E_FREE_LIST(hooks, e_client_hook_del);

   gl_avail = EINA_FALSE;
   e_comp_cfdata_config_free(conf);
   E_CONFIG_DD_FREE(conf_match_edd);
   E_CONFIG_DD_FREE(conf_edd);
   conf = NULL;
   conf_match_edd = NULL;
   conf_edd = NULL;

   E_FREE_FUNC(ignores, eina_hash_free);

   return 1;
}

E_API void
e_comp_deferred_job(void)
{
   /* Bg update */
   e_main_ts("\tE_BG_Zone Update");
   if (e_zone_current_get()->bg_object)
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_DESK);
   else
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_START);
   e_main_ts("\tE_BG_Zone Update Done");

   e_main_ts("\tE_Comp_Wl_Deferred");
   e_comp_wl_deferred_job();
   e_main_ts("\tE_Comp_Wl_Deferred Done");
}

E_API void
e_comp_render_queue(void)
{
   if (conf->lock_fps)
     {
        ecore_animator_thaw(e_comp->render_animator);
     }
   else
     {
        if (e_comp->update_job)
          {
             DBG("UPDATE JOB DEL...");
             E_FREE_FUNC(e_comp->update_job, ecore_job_del);
          }
        DBG("UPDATE JOB ADD...");
        e_comp->update_job = ecore_job_add(_e_comp_cb_job, e_comp);
     }
}

E_API void
e_comp_client_post_update_add(E_Client *ec)
{
   if (ec->on_post_updates) return;
   ec->on_post_updates = EINA_TRUE;
   e_comp->post_updates = eina_list_append(e_comp->post_updates, ec);
   REFD(ec, 111);
   e_object_ref(E_OBJECT(ec));
}

// TODO: shoulde be removed - yigl
E_API void
e_comp_shape_queue(void)
{
   if (e_comp->comp_type != E_PIXMAP_TYPE_X) return;
   if (!e_comp->shape_job)
     e_comp->shape_job = ecore_job_add(_e_comp_shapes_update_job, NULL);
}

E_API void
e_comp_shape_queue_block(Eina_Bool block)
{
   e_comp->shape_queue_blocked = !!block;
   if (block)
     E_FREE_FUNC(e_comp->shape_job, ecore_job_del);
   else
     e_comp_shape_queue();
}

E_API E_Comp_Config *
e_comp_config_get(void)
{
   return conf;
}

E_API void
e_comp_shadows_reset(void)
{
   E_Client *ec;

   _e_comp_fps_update();
   E_LIST_FOREACH(e_comp->zones, e_comp_canvas_zone_update);
   E_CLIENT_FOREACH(ec)
     e_comp_object_frame_theme_set(ec->frame, E_COMP_OBJECT_FRAME_RESHADOW);
}

E_API Ecore_Window
e_comp_top_window_at_xy_get(Evas_Coord x, Evas_Coord y)
{
   E_Client *ec;
   Evas_Object *o;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, 0);
   o = evas_object_top_at_xy_get(e_comp->evas, x, y, 0, 0);
   if (!o) return e_comp->ee_win;
   ec = evas_object_data_get(o, "E_Client");
   if (ec) return e_client_util_pwin_get(ec);
   return e_comp->ee_win;
}

E_API void
e_comp_util_wins_print(void)
{
   Evas_Object *o;

   o = evas_object_top_get(e_comp->evas);
   while (o)
     {
        E_Client *ec;
        int x, y, w, h;

        ec = evas_object_data_get(o, "E_Client");
        evas_object_geometry_get(o, &x, &y, &w, &h);
        fprintf(stderr, "LAYER %d  ", evas_object_layer_get(o));
        if (ec)
          fprintf(stderr, "EC%s%s:  %p - '%s:%s' || %d,%d @ %dx%d\n",
                  ec->override ? "O" : "", ec->focused ? "*" : "", ec,
                  e_client_util_name_get(ec) ?: ec->icccm.name, ec->icccm.class, x, y, w, h);
        else
          fprintf(stderr, "OBJ: %p - %s || %d,%d @ %dx%d\n", o, evas_object_name_get(o), x, y, w, h);
        o = evas_object_below_get(o);
     }
   fputc('\n', stderr);
}

E_API void
e_comp_ignore_win_add(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_add(ignores, &win, (void*)1);
   ec = e_pixmap_find_client(type, win);
   if (!ec) return;
   ec->ignored = 1;
   if (ec->visible) evas_object_hide(ec->frame);
}

E_API void
e_comp_ignore_win_del(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_del_by_key(ignores, &win);
   ec = e_pixmap_find_client(type, win);
   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   ec->ignored = 0;
   if (ec->visible) evas_object_show(ec->frame);
}

E_API Eina_Bool
e_comp_ignore_win_find(Ecore_Window win)
{
   return !!eina_hash_find(ignores, &win);
}

E_API void
e_comp_override_del()
{
   e_comp->nocomp_override--;
   if (e_comp->nocomp_override <= 0)
     {
        e_comp->nocomp_override = 0;
        e_comp_render_queue();
     }
}

E_API void
e_comp_override_add()
{
   e_comp->nocomp_override++;
#ifdef ENABLE_HWC_MULTI
   if (e_comp->nocomp_override > 0)
     {
        // go full GLES compositing
        if (!e_comp->hwc_optimized)
          e_comp_hwc_end(__FUNCTION__);
        else
          /* We must notify the hwc extension about the full GLES composition. */
          e_comp_render_queue();
     }
#endif
}

E_API E_Comp *
e_comp_find_by_window(Ecore_Window win)
{
   if ((e_comp->win == win) || (e_comp->ee_win == win) || (e_comp->root == win)) return e_comp;
   return NULL;
}

E_API void
e_comp_override_timed_pop(void)
{
   if (e_comp->nocomp_override <= 0) return;
   if (e_comp->nocomp_override_timer)
     e_comp->nocomp_override--;
   else
     e_comp->nocomp_override_timer = ecore_timer_add(1.0, _e_comp_override_expire, NULL);
}

E_API unsigned int
e_comp_e_object_layer_get(const E_Object *obj)
{
   E_Client *ec = NULL;

   if (!obj) return 0;

   switch (obj->type)
     {
      case E_ZONE_TYPE:
        return E_LAYER_DESKTOP;

      case E_CLIENT_TYPE:
        return ((E_Client *)(obj))->layer;

      /* FIXME: add more types as needed */
      default:
        break;
     }
   if (e_obj_is_win(obj))
     {
        ec = e_win_client_get((void*)obj);
        if (ec)
          return ec->layer;
     }
   return 0;
}

E_API void
e_comp_layer_name_get(unsigned int layer, char *buff, int buff_size)
{
   if (!buff) return;

   switch(layer)
     {
      case E_LAYER_BOTTOM: strncpy(buff, "E_LAYER_BOTTOM", buff_size); break;
      case E_LAYER_BG: strncpy(buff, "E_LAYER_BG", buff_size); break;
      case E_LAYER_DESKTOP: strncpy(buff, "E_LAYER_DESKTOP", buff_size); break;
      case E_LAYER_DESKTOP_TOP: strncpy(buff, "E_LAYER_DESKTOP_TOP", buff_size); break;
      case E_LAYER_CLIENT_DESKTOP: strncpy(buff, "E_LAYER_CLIENT_DESKTOP", buff_size); break;
      case E_LAYER_CLIENT_BELOW: strncpy(buff, "E_LAYER_CLIENT_BELOW", buff_size); break;
      case E_LAYER_CLIENT_NORMAL: strncpy(buff, "E_LAYER_CLIENT_NORMAL", buff_size); break;
      case E_LAYER_CLIENT_ABOVE: strncpy(buff, "E_LAYER_CLIENT_ABOVE", buff_size); break;
      case E_LAYER_CLIENT_EDGE: strncpy(buff, "E_LAYER_CLIENT_EDGE", buff_size); break;
      case E_LAYER_CLIENT_FULLSCREEN: strncpy(buff, "E_LAYER_CLIENT_FULLSCREEN", buff_size); break;
      case E_LAYER_CLIENT_EDGE_FULLSCREEN: strncpy(buff, "E_LAYER_CLIENT_EDGE_FULLSCREEN", buff_size); break;
      case E_LAYER_CLIENT_POPUP: strncpy(buff, "E_LAYER_CLIENT_POPUP", buff_size); break;
      case E_LAYER_CLIENT_TOP: strncpy(buff, "E_LAYER_CLIENT_TOP", buff_size); break;
      case E_LAYER_CLIENT_DRAG: strncpy(buff, "E_LAYER_CLIENT_DRAG", buff_size); break;
      case E_LAYER_CLIENT_PRIO: strncpy(buff, "E_LAYER_CLIENT_PRIO", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_LOW: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_LOW", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_NORMAL: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_NORMAL", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_HIGH: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_HIGH", buff_size); break;
      case E_LAYER_CLIENT_NOTIFICATION_TOP: strncpy(buff, "E_LAYER_CLIENT_NOTIFICATION_TOP", buff_size); break;
      case E_LAYER_CLIENT_ALERT_LOW: strncpy(buff, "E_LAYER_CLIENT_ALERT_LOW", buff_size); break;
      case E_LAYER_CLIENT_ALERT: strncpy(buff, "E_LAYER_CLIENT_ALERT", buff_size); break;
      case E_LAYER_CLIENT_ALERT_HIGH: strncpy(buff, "E_LAYER_CLIENT_ALERT_HIGH", buff_size); break;
      case E_LAYER_CLIENT_CURSOR: strncpy(buff, "E_LAYER_CLIENT_CURSOR", buff_size); break;
      case E_LAYER_POPUP: strncpy(buff, "E_LAYER_POPUP", buff_size); break;
      case E_LAYER_EFFECT: strncpy(buff, "E_LAYER_EFFECT", buff_size); break;
      case E_LAYER_MENU: strncpy(buff, "E_LAYER_MENU", buff_size); break;
      case E_LAYER_DESKLOCK: strncpy(buff, "E_LAYER_DESKLOCK", buff_size); break;
      case E_LAYER_MAX: strncpy(buff, "E_LAYER_MAX", buff_size); break;
      default:strncpy(buff, "E_LAYER_NONE", buff_size); break;
     }
}

E_API Eina_Bool
e_comp_grab_input(Eina_Bool mouse, Eina_Bool kbd)
{
   Eina_Bool ret = EINA_FALSE;
   Ecore_Window mwin = 0, kwin = 0;

   mouse = !!mouse;
   kbd = !!kbd;
   if (mouse || e_comp->input_mouse_grabs)
     mwin = e_comp->ee_win;
   if (kbd || e_comp->input_mouse_grabs)
     kwin = e_comp->ee_win;
   //e_comp_override_add(); //nocomp condition
   if ((e_comp->input_mouse_grabs && e_comp->input_key_grabs) ||
       e_grabinput_get(mwin, 0, kwin))
     {
        ret = EINA_TRUE;
        e_comp->input_mouse_grabs += mouse;
        e_comp->input_key_grabs += kbd;
     }
   return ret;
}

E_API void
e_comp_ungrab_input(Eina_Bool mouse, Eina_Bool kbd)
{
   Ecore_Window mwin = 0, kwin = 0;

   mouse = !!mouse;
   kbd = !!kbd;
   if (e_comp->input_mouse_grabs)
     e_comp->input_mouse_grabs -= mouse;
   if (e_comp->input_key_grabs)
     e_comp->input_key_grabs -= kbd;
   if (mouse && (!e_comp->input_mouse_grabs))
     mwin = e_comp->ee_win;
   if (kbd && (!e_comp->input_key_grabs))
     kwin = e_comp->ee_win;
   //e_comp_override_timed_pop(); //nocomp condition
   if ((!mwin) && (!kwin)) return;
   e_grabinput_release(mwin, kwin);
   evas_event_feed_mouse_out(e_comp->evas, 0, NULL);
   evas_event_feed_mouse_in(e_comp->evas, 0, NULL);
   if (e_client_focused_get()) return;
   if (e_config->focus_policy != E_FOCUS_MOUSE)
     e_client_refocus();
}

E_API Eina_Bool
e_comp_util_kbd_grabbed(void)
{
   return e_client_action_get() || e_grabinput_key_win_get();
}

E_API Eina_Bool
e_comp_util_mouse_grabbed(void)
{
   return e_client_action_get() || e_grabinput_mouse_win_get();
}

E_API void
e_comp_gl_set(Eina_Bool set)
{
   gl_avail = !!set;
}

E_API Eina_Bool
e_comp_gl_get(void)
{
   return gl_avail;
}

E_API void
e_comp_button_bindings_ungrab_all(void)
{
   if (e_comp->bindings_ungrab_cb)
     e_comp->bindings_ungrab_cb();
}

E_API void
e_comp_button_bindings_grab_all(void)
{
   if (e_comp->bindings_grab_cb)
     e_comp->bindings_grab_cb();
}

E_API void
e_comp_client_redirect_toggle(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!conf->enable_advanced_features) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_X) return;
   ec->unredirected_single = !ec->unredirected_single;
   e_client_redirected_set(ec, !ec->redirected);
   ec->no_shape_cut = !ec->redirected;
   e_comp_shape_queue();
}

E_API Eina_Bool
e_comp_util_object_is_above_nocomp(Evas_Object *obj)
{
   Evas_Object *o;
   int cl, ol;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, EINA_FALSE);
   if (!evas_object_visible_get(obj)) return EINA_FALSE;
   if (!e_comp->nocomp_ec) return EINA_FALSE;
   cl = evas_object_layer_get(e_comp->nocomp_ec->frame);
   ol = evas_object_layer_get(obj);
   if (cl > ol) return EINA_FALSE;
   o = evas_object_above_get(e_comp->nocomp_ec->frame);
   if ((cl == ol) && (evas_object_layer_get(o) == cl))
     {
        do {
           if (o == obj)
             return EINA_TRUE;
           o = evas_object_above_get(o);
        } while (o && (evas_object_layer_get(o) == cl));
     }
   else
     return EINA_TRUE;
   return EINA_FALSE;
}

E_API E_Comp_Hook *
e_comp_hook_add(E_Comp_Hook_Point hookpoint, E_Comp_Hook_Cb func, const void *data)
{
   E_Comp_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_COMP_HOOK_LAST, NULL);
   ch = E_NEW(E_Comp_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ch, NULL);
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_comp_hooks[hookpoint] = eina_inlist_append(_e_comp_hooks[hookpoint],
                                                 EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_comp_hook_del(E_Comp_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_comp_hooks_walking == 0)
     {
        _e_comp_hooks[ch->hookpoint] = eina_inlist_remove(_e_comp_hooks[ch->hookpoint],
                                                          EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_comp_hooks_delete++;
}

EINTERN Eina_Bool
e_comp_is_on_overlay(E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   if (e_comp->hwc_mode)
     {
        Eina_List *l, *ll;
        E_Output * eout;
        E_Plane *ep;

        if (!ec->zone || !ec->zone->output_id) return EINA_FALSE;
        eout = e_output_find(ec->zone->output_id);
        EINA_LIST_FOREACH_SAFE(eout->planes, l, ll, ep)
          {
             E_Client *overlay_ec = ep->ec;
             if (overlay_ec == ec) return EINA_TRUE;
          }
     }
   return EINA_FALSE;
}

E_API Eina_List *
e_comp_vis_ec_list_get(E_Zone *zone)
{
   Eina_List *ec_list = NULL;
   E_Client  *ec;
   Evas_Object *o;

   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   // TODO: check if eout is available to use hwc policy
   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        int x, y, w, h;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

        if (ec->zone != zone) continue;

        // check clients to skip composite
        if (e_client_util_ignored_get(ec) || (!evas_object_visible_get(ec->frame)))
          continue;

        // check geometry if located out of screen such as quick panel
        if (!E_INTERSECTS(0, 0, e_comp->w, e_comp->h,
                          ec->client.x, ec->client.y, ec->client.w, ec->client.h))
          continue;

        if (evas_object_data_get(ec->frame, "comp_skip"))
          continue;

        ec_list = eina_list_append(ec_list, ec);

        // find full opaque win and excludes below wins from the visible list.
        e_client_geometry_get(ec, &x, &y, &w, &h);
        if (!E_CONTAINS(x, y, w, h,
                        0, 0, e_comp->w, e_comp->h))
           continue;

        if (!ec->argb)
          break;
     }

   return ec_list;
}

static int
e_getpwnam_r(const char *name)
{
   struct passwd *u;
   struct passwd *u_res;
   char* buf;
   size_t buflen;
   int ret;
#undef BUFLEN
#define BUFLEN 2048

   buflen = sysconf(_SC_GETPW_R_SIZE_MAX);
   if (buflen == -1)          /* Value was indeterminate */
     buflen = BUFLEN;        /* Should be more than enough */
#undef BUFLEN

   buf = malloc(buflen);
   if (buf == NULL)
     {
        ERR("failed to create buffer");
        return 0;
     }

   u = malloc(sizeof(struct passwd));
   if (!u)
     {
        ERR("failed to create password struct");
        free(buf);
        return 0;
     }

   ret = getpwnam_r(name, u, buf, buflen, &u_res);
   if (u_res == NULL)
     {
        if (ret == 0)
          ERR("password not found");
        else
          ERR("errno returned by getpwnam_r is %d", ret);
        free(buf);
        free(u);
        return 0;
     }
   ret = u->pw_uid;
   free(buf);
   free(u);
   return ret;
}

static int
e_getgrnam_r(const char *name)
{
   struct group *g;
   struct group *grp_res;
   char* buf;
   size_t buflen;
   int ret;
#undef BUFLEN
#define BUFLEN 2048
   buflen = sysconf(_SC_GETGR_R_SIZE_MAX);
   if (buflen == -1)          /* Value was indeterminate */
     buflen = BUFLEN;        /* Should be more than enough */
#undef BUFLEN

   buf = malloc(buflen);
   if (buf == NULL)
     {
        ERR("failed to create buffer");
        return 0;
     }

   g = malloc(sizeof(struct group));
   if (!g)
     {
        ERR("failed to create group struct");
        free(buf);
        return 0;
     }

   ret = getgrnam_r(name, g, buf, buflen, &grp_res);
   if (grp_res == NULL)
     {
        if (ret == 0)
          ERR("Group not found");
        else
          ERR("errno returned by getpwnam_r is %d", ret);

        free(buf);
        free(g);
        return 0;
     }

   ret = g->gr_gid;
   free(buf);
   free(g);
   return ret;
}

E_API Eina_Bool
e_comp_socket_init(const char *name)
{
   const char *dir = NULL;
   char socket_path[108];
   uid_t uid;
   gid_t gid;
   int res;
   E_Config_Socket_Access *sa = NULL;
   Eina_List *l = NULL;
#undef STRERR_BUFSIZE
#define STRERR_BUFSIZE 128
   char buf[STRERR_BUFSIZE];

   dir = getenv("XDG_RUNTIME_DIR");
   if (!dir) return EINA_FALSE;
   if (!name) return EINA_FALSE;

   snprintf(socket_path, sizeof(socket_path), "%s/%s", dir, name);

   EINA_LIST_FOREACH(e_config->sock_accesses, l, sa)
     {
        if (strcmp(sa->sock_access.name, name)) continue;
        if (!sa->sock_access.use) break;

        if ((sa->sock_access.owner) &&
            (sa->sock_access.group))
          {
             uid = e_getpwnam_r(sa->sock_access.owner);

             gid = e_getgrnam_r(sa->sock_access.group);

             DBG("socket path: %s owner: %s (%d) group: %s (%d) permissions: %o",
                 socket_path,
                 sa->sock_access.owner, uid,
                 sa->sock_access.group, gid,
                 sa->sock_access.permissions);

             res = chmod(socket_path, sa->sock_access.permissions);
             if (res < 0)
               {
                  ERR("Could not change modes of socket file:%s (%s)",
                      socket_path,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] Could not chane modes of socket file: %s", socket_path);
                  return EINA_FALSE;
               }

             res = chown(socket_path, uid, gid);
             if (res < 0)
               {
                  ERR("Could not change owner of socket file:%s (%s)",
                      socket_path,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] Could not change owner of socket file: %s", socket_path);
                  return EINA_FALSE;
               }
          }

        if (sa->sock_access.smack.use)
          {
             res = setxattr(socket_path,
                            sa->sock_access.smack.name,
                            sa->sock_access.smack.value,
                            strlen(sa->sock_access.smack.value),
                            sa->sock_access.smack.flags);
             if (res < 0)
               {
                  ERR("Could not change smack variable for socket file: %s (%s)",
                      socket_path,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] Could not change smack variable for socket file: %s", socket_path);
                  return EINA_FALSE;
               }
          }

        if (sa->sock_symlink_access.use)
          {
             res = symlink(socket_path,
                           sa->sock_symlink_access.link_name);
             if (res < 0)
               {
                  ERR("Could not make symbolic link: %s (%s)",
                      sa->sock_symlink_access.link_name,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] Could not make symbolic link: %s", sa->sock_symlink_access.link_name);
                  if (errno != EEXIST)
                    return EINA_FALSE;
               }

             uid = e_getpwnam_r(sa->sock_symlink_access.owner);

             gid = e_getgrnam_r(sa->sock_symlink_access.group);

             res = lchown(sa->sock_symlink_access.link_name, uid, gid);
             if (res < 0)
               {
                  ERR("chown -h owner:users %s failed! (%s)", sa->sock_symlink_access.link_name,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] chown -h owner:users %s failed!", sa->sock_symlink_access.link_name);
                  return EINA_FALSE;
               }

             res = setxattr(sa->sock_symlink_access.link_name,
                            sa->sock_symlink_access.smack.name,
                            sa->sock_symlink_access.smack.value,
                            strlen(sa->sock_symlink_access.smack.value),
                            sa->sock_symlink_access.smack.flags);
             if (res < 0)
               {
                  ERR("Chould not change smack variable for symbolic link: %s (%s)", sa->sock_symlink_access.link_name,
                      strerror_r(errno, buf, STRERR_BUFSIZE));

                  PRCTL("[Winsys] Chould not change smack variable for symbolic link: %s", sa->sock_symlink_access.link_name);
                  return EINA_FALSE;
               }
          }
        break;
     }

   return EINA_TRUE;
}
