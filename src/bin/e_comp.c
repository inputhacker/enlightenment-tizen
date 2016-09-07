#include "e.h"


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
   else if ((tim - rlapse) >= 1.0)
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

static Eina_Bool
_hwc_set(E_Output *eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (ep->ec) e_client_redirected_set(ep->ec, 1);
     }

   ep_l = e_output_planes_get(eout);
   EINA_LIST_REVERSE_FOREACH(ep_l, l , ep)
     {
        Eina_Bool set = EINA_FALSE;

        //if (e_plane_type_get(ep) != E_PLANE_TYPE_OVERLAY) continue;

        if (e_plane_is_fb_target(ep))
          {
             if (ep->prepare_ec)
               {
                  set = e_plane_ec_set(ep, ep->prepare_ec);
                  if (set)
                    {
                       INF("HWC : ec(0x%08x, %s) is set on fb_target( %d)\n",(unsigned int)ep->prepare_ec, ep->prepare_ec->icccm.title, ep->zpos);
                       e_client_redirected_set(ep->prepare_ec, 0);
                       ret |= EINA_TRUE;

                       // fb target is occupied by a client surface, means compositor disabled
                       ecore_event_add(E_EVENT_COMPOSITOR_DISABLE, NULL, NULL, NULL);
                    }
                  break;
               }
          }
        else if (ep->prepare_ec)
          {
             set = e_plane_ec_set(ep, ep->prepare_ec);
             if (set)
               {
                  INF("HWC : ec(0x%08x, %s) is set on %d\n", (unsigned int)ep->prepare_ec, ep->prepare_ec->icccm.title, ep->zpos);
                  e_client_redirected_set(ep->prepare_ec, 0);
                  ret |= EINA_TRUE;
               }
             else
               break;
          }
     }

   return ret;
}

static Eina_Bool
_hwc_plane_prepare(E_Output *eout, int n_vis, Eina_List *clist)
{
   const Eina_List *ep_l = NULL, *l ;
   Eina_List *hwc_l = NULL, *clist2;
   E_Plane *ep = NULL, *ep_fb = NULL;
   int n_ly = 0, n_ec = 0;
   E_Client *ec = NULL;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(clist, EINA_FALSE);

   // list up available_hw layers E_Client can be set
   // if conf->hwc_use_multi_plane FALSE, than use only fb target plane
   ep_l = e_output_planes_get(eout);
   EINA_LIST_FOREACH(ep_l, l, ep)
     {
        if (!ep_fb)
          {
             if (e_plane_is_fb_target(ep))
               {
                  ep_fb = ep;
                  hwc_l = eina_list_append(hwc_l, ep);
               }
             continue;
          }
        if (!conf->hwc_use_multi_plane) continue;
        if (e_plane_is_cursor(ep)) continue;
        if (ep->zpos > ep_fb->zpos)
          hwc_l = eina_list_append(hwc_l, ep);
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_l, EINA_FALSE);

   // finally, assign client on available_hw layers
   n_ly = eina_list_count(hwc_l);
   n_ec = eina_list_count(clist);
   clist2 = eina_list_clone(clist);
   if ((n_ec == n_vis) &&
       (n_ec <= n_ly)) // fully hwc
     {
        int del = n_ly - n_ec;
        EINA_LIST_REVERSE_FOREACH(hwc_l, l, ep)
          {
             ec = NULL;
             if (del > 0)
               {
                  e_plane_ec_prepare_set(ep, NULL);
                  del--;
                  continue;
               }
             if (clist) ec = eina_list_data_get(clist);
             if (ec) e_plane_ec_prepare_set(ep, ec);
             clist = eina_list_next(clist);
          }
        ret = EINA_TRUE;
     }
   else if ((n_ly < n_vis) || // e_comp->evas on fb target plane
            (n_ec < n_vis))
     {
        int del = n_ly - n_ec + 1;
        EINA_LIST_REVERSE_FOREACH(hwc_l, l, ep)
          {
             ec = NULL;
             if (del > 0)
               {
                  e_plane_ec_prepare_set(ep, NULL);
                  del--;
                  continue;
               }
             if (clist2) ec = eina_list_data_get(clist2);
             if (ec) e_plane_ec_prepare_set(ep, ec);
             if (e_plane_is_fb_target(ep)) e_plane_ec_prepare_set(ep, NULL);
             clist2 = eina_list_next(clist2);
          }
        ret = EINA_TRUE;
     }

   eina_list_free(hwc_l);
   return ret;
}

static Eina_Bool
_hwc_cancel(E_Output *eout)
{
   Eina_List *l ;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout->planes, EINA_FALSE);

   e_comp->hwc_mode = 0;

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (ep->ec) e_client_redirected_set(ep->ec, 1);
        e_plane_ec_prepare_set(ep, NULL);
        e_plane_ec_set(ep, NULL);
     }

   return EINA_TRUE;
}

static Eina_Bool
_hwc_plane_reserved_clean()
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
             if (e_plane_is_reserved(ep))
                e_plane_reserved_set(ep, 0);
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_hwc_apply(E_Output * eout)
{
   const Eina_List *ep_l = NULL, *l;
   E_Plane *ep = NULL, *ep_fb = NULL;
   Eina_Bool ret = EINA_FALSE;

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
        if (e_plane_is_cursor(ep)) continue;
        if (ep->zpos > ep_fb->zpos)
          if (ep->prepare_ec != NULL) goto hwcompose;
     }

   goto compose;

hwcompose:
   ret = _hwc_set(eout);
   if (!ret) INF("HWC : it is failed to assign surface on plane\n");

compose:
   if (ret) e_comp->hwc_mode = 1;

   return ret;
}

static Eina_Bool
_e_comp_hwc_changed(void)
{
   Eina_List *l;
   E_Zone *zone;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Output *eout = NULL;
        E_Plane *ep = NULL, *ep_fb = NULL;
        const Eina_List *ep_l = NULL, *p_l ;

        if (!zone || !zone->output_id) continue;

        eout = e_output_find(zone->output_id);
        ep_l = e_output_planes_get(eout);
        EINA_LIST_FOREACH(ep_l, p_l, ep)
          {
             if (!ep_fb)
               {
                  if (e_plane_is_fb_target(ep))
                    {
                       ep_fb = ep;
                       if (ep->ec != ep->prepare_ec) return EINA_TRUE;
                    }
                  continue;
               }
             if (e_plane_is_cursor(ep)) continue;
             if ((ep->zpos > ep_fb->zpos) &&
                 (ep->ec != ep->prepare_ec))
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_comp_hwc_prepare(void)
{
   Eina_List *l, *vl;
   E_Zone *zone;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_comp->hwc, EINA_FALSE);

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        E_Client *ec;
        E_Output *output;
        int n_vis = 0, n_ec = 0;
        Eina_List *hwc_ok_clist = NULL, *vis_clist = NULL;

        if (!zone || !zone->output_id) continue;  // no hw layer

        output = e_output_find(zone->output_id);
        if (!output) continue;

        vis_clist = e_comp_vis_ec_list_get(zone);
        if (!vis_clist) continue;

        EINA_LIST_FOREACH(vis_clist, vl, ec)
          {
             E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;

             // check clients not able to use hwc
             // if ec->frame is not for client buffer (e.g. launchscreen)
             if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
                goto nextzone;

             // if there is UI or video stream on subfrace, it means need to composite
             if (cdata && (cdata->sub.below_list || cdata->sub.below_list_pending))
               {
                  if (!e_comp_wl_video_client_has(ec))
                     goto nextzone;
               }

             // if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
             if ((!cdata) ||
                 (!cdata->buffer_ref.buffer) ||
                 (cdata->buffer_ref.buffer->type != E_COMP_WL_BUFFER_TYPE_NATIVE) ||
                 (cdata->width_from_buffer != cdata->width_from_viewport) ||
                 (cdata->height_from_buffer != cdata->height_from_viewport) ||
                 e_client_transform_core_enable_get(ec) ||
                 cdata->never_hwc ||
                 e_comp_wl_tbm_buffer_sync_timeline_used(cdata->buffer_ref.buffer))
               {
                  if (!n_ec) goto nextzone;
                  break;
               }

             // listup as many as possible from the top most visible order
             n_ec++;
             hwc_ok_clist = eina_list_append(hwc_ok_clist, ec);
          }

        n_vis = eina_list_count(vis_clist);
        if ((n_vis < 1) || (n_ec < 1))
          goto nextzone;

        ret |= _hwc_plane_prepare(output, n_vis, hwc_ok_clist);

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

   // will be removed once hwc cursor is supported
   if (!e_pointer_is_hidden(e_comp->pointer)) return EINA_FALSE;

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
        EINA_LIST_FOREACH(ep_l, p_l, ep)
          {
             if (!ep_fb)
               {
                  if (e_plane_is_fb_target(ep))
                    {
                       ep_fb = ep;
                       if (ep->prepare_ec != NULL) return EINA_TRUE;
                    }
                  continue;
               }
             if (e_plane_is_cursor(ep)) continue;
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

   INF("HWC : Begin ...");
}

static Eina_Bool
_e_comp_hwc_cb_begin_timeout(void *data EINA_UNUSED)
{
   e_comp->nocomp_delay_timer = NULL;

   if (e_comp->nocomp_override == 0 && _e_comp_hwc_usable())
     {
        e_comp->nocomp_want = 1;
        _e_comp_hwc_begin();
     }
   return EINA_FALSE;
}

E_API void
e_comp_hwc_end(const char *location)
{
   Eina_Bool mode_set = EINA_FALSE;
   E_Zone *zone;
   Eina_List *l;

   e_comp->nocomp_want = 0;
   E_FREE_FUNC(e_comp->nocomp_delay_timer, ecore_timer_del);
   _hwc_plane_reserved_clean();

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

   ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
   INF("HWC : End...  at %s", location);
}
#endif  // end of ENABLE_HWC_MULTI

static void
_e_comp_client_update(E_Client *ec)
{
   int pw, ph;

   DBG("UPDATE [%p] pm = %p", ec, ec->pixmap);
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_pixmap_size_get(ec->pixmap, &pw, &ph);

   if (e_pixmap_dirty_get(ec->pixmap) && (!e_comp->nocomp))
     {
        int w, h;

        if (e_pixmap_refresh(ec->pixmap) &&
            e_pixmap_size_get(ec->pixmap, &w, &h) &&
            e_pixmap_size_changed(ec->pixmap, pw, ph))
          {
             e_pixmap_image_clear(ec->pixmap, 0);
             e_comp_object_render_update_del(ec->frame); //clear update
          }
        else if (!e_pixmap_size_get(ec->pixmap, NULL, NULL))
          {
             WRN("FAIL %p", ec);
             e_comp_object_redirected_set(ec->frame, 0);
             if (e_pixmap_failures_get(ec->pixmap) < 3)
               e_comp_object_render_update_add(ec->frame);
          }
     }
   if ((!e_comp->saver) && e_pixmap_size_get(ec->pixmap, &pw, &ph))
     {
        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_dirty(ec->frame);
        if (e_pixmap_is_x(ec->pixmap) && (!ec->override))
          evas_object_resize(ec->frame, ec->w, ec->h);
     }
}

static Eina_Bool
_e_comp_cb_update(void)
{
   E_Client *ec;
   Eina_List *l;

   if (!e_comp) return EINA_FALSE;

   TRACE_DS_BEGIN(COMP:UPDATE CB);

   if (e_comp->update_job)
     e_comp->update_job = NULL;
   else
     ecore_animator_freeze(e_comp->render_animator);

   DBG("UPDATE ALL");
   if (e_comp->hwc_mode) goto setup_hwcompose;

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
        _e_comp_client_update(ec);
     }

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
   if (!e_comp_gl_get() || // TODO: e_comp_gl_get can be removed out if evas sw backend is TBM
       !e_comp->hwc ||
       !e_comp->hwc_fs)
     {
        goto end;
     }

   if(_e_comp_hwc_usable())
     {
        if (e_comp->hwc_mode)
          {
             // FIXME : will remove out this condition
             // new(ec at prepared list) and current(ec on e_plane)
             if (_e_comp_hwc_changed())
                e_comp_hwc_end("overlay surface changed");
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
                  e_comp->nocomp_want = 1;
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
   E_FREE_LIST(c->zones, e_object_del);

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
   //if ((!e_comp->nocomp) || (!e_comp->nocomp_ec)) return ECORE_CALLBACK_RENEW;
   //if (evas_object_layer_get(ev->comp_object) > MAX(e_comp->nocomp_ec->saved.layer, E_LAYER_CLIENT_NORMAL))
   //  e_comp_nocomp_end();
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
#ifdef ENABLE_HWC_MULTI
        if (e_comp->nocomp_want) _e_comp_hwc_begin();
#endif
     }
   return EINA_FALSE;
}

//////////////////////////////////////////////////////////////////////////

static Eina_Bool
_e_comp_screensaver_on(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
#if 0
   Eina_List *l;
   E_Zone *zone;

   ecore_frametime = ecore_animator_frametime_get();
   if (e_comp->saver) return ECORE_CALLBACK_RENEW;
   e_comp_override_add();
   e_comp->saver = EINA_TRUE;
   if (e_comp->render_animator)
     ecore_animator_freeze(e_comp->render_animator);
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        e_zone_fade_handle(zone, 1, 3.0);
        edje_object_signal_emit(zone->base, "e,state,screensaver,on", "e");
        edje_object_signal_emit(zone->over, "e,state,screensaver,on", "e");
     }
#endif

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screensaver_off(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
#if 0
   Eina_List *l;
   E_Zone *zone;
   E_Client *ec;

   ecore_animator_frametime_set(ecore_frametime);
   if (!e_comp->saver) return ECORE_CALLBACK_RENEW;
   e_comp_override_del();
   e_comp->saver = EINA_FALSE;
   if (!e_comp->nocomp)
     ecore_evas_manual_render_set(e_comp->ee, EINA_FALSE);
   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        edje_object_signal_emit(zone->base, "e,state,screensaver,off", "e");
        edje_object_signal_emit(zone->over, "e,state,screensaver,off", "e");
        e_zone_fade_handle(zone, 0, 0.5);
     }
   E_CLIENT_FOREACH(ec)
     if (e_comp_object_damage_exists(ec->frame))
       e_comp_object_render_update_add(ec->frame);
#endif

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_hwc_init(void)
{
   if (!e_comp || !e_comp->e_comp_screen)
     {
        e_error_message_show(_("Enlightenment cannot has no e_comp at HWC(HardWare Composite)!\n"));
        return EINA_FALSE;
     }

   if (!e_comp_screen_hwc_setup(e_comp->e_comp_screen))
     {
        ERR("fail to e_comp_screen_hwc_setup");
        return EINA_FALSE;
     }

   return EINA_TRUE;
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

#ifdef HAVE_HWC
   if (conf->hwc &&
       e_comp_gl_get()) // TODO: it can be removed out if evas sw backend is TBM
     {
        e_comp->hwc = _e_comp_hwc_init();
        if (!e_comp->hwc)
          WRN("fail to init hwc.");
        else
          e_comp->hwc_fs = EINA_TRUE; // 1: active hwc policy

        conf->nocomp_use_timer = 0; // will remove out once e-tizen-data is applied on all profiles
     }
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
   /* Add elm fake win */
   //e_comp->elm = elm_win_fake_add(e_comp->ee);
   //evas_object_show(e_comp->elm);

   /* Bg update */
   e_main_ts("\tE_BG_Zone Update");
   if (e_zone_current_get()->bg_object)
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_DESK);
   else
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_START);
   e_main_ts("\tE_BG_Zone Update Done");

   /* Pointer setting */
   e_main_ts("\tE_Pointer New");
   if (!e_comp->pointer)
     {
        e_comp->pointer = e_pointer_canvas_new(e_comp->ee, EINA_TRUE);
        e_pointer_hide(e_comp->pointer);
     }
   e_main_ts("\tE_Pointer New Done");

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
#ifdef ENABLE_HWC_MULTI
        if (e_comp->nocomp_want)
          _e_comp_hwc_begin();
#endif
     }
}

E_API void
e_comp_override_add()
{
   e_comp->nocomp_override++;
   if (e_comp->nocomp_override > 0)
     {
        // go full compositing
#ifdef ENABLE_HWC_MULTI
        e_comp_hwc_end(__FUNCTION__);
#endif
     }
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
      case E_LAYER_CLIENT_ALERT: strncpy(buff, "E_LAYER_CLIENT_ALERT", buff_size); break;
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

   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   // TODO: check if eout is available to use hwc policy
   E_CLIENT_REVERSE_FOREACH(ec)
     {
        int x, y, w, h;

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
