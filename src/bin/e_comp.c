#include "e.h"
#include <sys/xattr.h>
#include "services/e_service_quickpanel.h"

#define OVER_FLOW 1
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

#define DBG(...)            EINA_LOG_DOM_DBG(_e_comp_log_dom, __VA_ARGS__)
#define INF(...)            EINA_LOG_DOM_INFO(_e_comp_log_dom, __VA_ARGS__)
#define WRN(...)            EINA_LOG_DOM_WARN(_e_comp_log_dom, __VA_ARGS__)
#define ERR(...)            EINA_LOG_DOM_ERR(_e_comp_log_dom, __VA_ARGS__)
#define CRI(...)            EINA_LOG_DOM_CRIT(_e_comp_log_dom, __VA_ARGS__)

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

EINTERN void
e_comp_hook_call(E_Comp_Hook_Point hookpoint, void *data EINA_UNUSED)
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
                  /* if client pixmap is not valid while checking updates list in job cb handler
                     than let evas object as it is and make no updats on. */
               }
          }

        if (e_comp->saver) continue;

        res = e_pixmap_size_get(ec->pixmap, &pw, &ph);
        if (!res) continue;

        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_dirty(ec->frame);
     }

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

   E_EVENT_COMP_OBJECT_IMG_RENDER = ecore_event_type_new();
   E_EVENT_COMP_OBJECT_EFFECT_START = ecore_event_type_new();
   E_EVENT_COMP_OBJECT_EFFECT_END = ecore_event_type_new();

   ignores = eina_hash_pointer_new(NULL);

   e_main_ts_begin("\tE_Comp_Data Init");
   e_comp_cfdata_edd_init(&conf_edd, &conf_match_edd);
   e_main_ts_end("\tE_Comp_Data Init Done");

   e_main_ts_begin("\tE_Comp_Data Load");
   conf = e_config_domain_load("e_comp", conf_edd);
   e_main_ts_end("\tE_Comp_Data Load Done");

   if (!conf)
     {
        e_main_ts_begin("\tE_Comp_Data New");
        conf = e_comp_cfdata_config_new();
        e_main_ts_end("\tE_Comp_Data New Done");
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

   /* conf->hwc configuration has to be check before e_comp_screen_init() */
   if (conf->hwc)
     e_comp->hwc = EINA_TRUE; // activate hwc policy on the primary output

   if (conf->avoid_afill) e_comp->avoid_afill = EINA_TRUE;

   e_main_ts_begin("\tE_Comp_Screen Init");
   if (!e_comp_screen_init())
     {
        e_main_ts_end("\tE_Comp_Screen Init Failed");
        ERR("Fail to init e_comp_screen");
        e_object_del(E_OBJECT(e_comp));
        E_FREE_FUNC(ignores, eina_hash_free);
        return EINA_FALSE;
     }
   e_main_ts_end("\tE_Comp_Screen Init Done");

   if (e_comp->hwc)
     {
        if (conf->hwc_deactive) e_comp_hwc_deactive_set(EINA_TRUE);
        if (conf->hwc_reuse_cursor_buffer) e_comp->hwc_reuse_cursor_buffer = EINA_TRUE;

        if (conf->hwc_use_multi_plane) e_comp_hwc_multi_plane_set(EINA_TRUE);
        if (conf->hwc_sync_mode_change) e_comp->hwc_sync_mode_change = EINA_TRUE;
        if (conf->hwc_use_detach) e_comp->hwc_use_detach = EINA_TRUE;
        if (conf->hwc_ignore_primary) e_comp->hwc_ignore_primary = EINA_TRUE;
     }

   // use wl_surface instead of tbm_surface for the e_comp_wl_buffer
   if (conf->use_native_type_buffer) e_comp->use_native_type_buffer = EINA_TRUE;

   if (conf->canvas_render_delay_after_boot) e_comp->canvas_render_delayed = EINA_TRUE;

   e_comp->comp_type = E_PIXMAP_TYPE_WL;

   e_comp_canvas_fake_layers_init();

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_ON,  _e_comp_screensaver_on,  NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREENSAVER_OFF, _e_comp_screensaver_off, NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_KEY_DOWN,    _e_comp_key_down,        NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_SIGNAL_USER, _e_comp_signal_user,     NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_COMP_OBJECT_ADD, _e_comp_object_add,      NULL);

   return EINA_TRUE;
}

EINTERN E_Comp *
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

EINTERN int
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

EINTERN void
e_comp_deferred_job(void)
{
   /* Bg update */
   e_main_ts_begin("\tE_BG_Zone Update");
   if (e_zone_current_get()->bg_object)
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_DESK);
   else
     e_bg_zone_update(e_zone_current_get(), E_BG_TRANSITION_START);
   e_main_ts_end("\tE_BG_Zone Update Done");

   e_main_ts_begin("\tE_Comp_Wl_Deferred");
   e_comp_wl_deferred_job();
   e_main_ts_end("\tE_Comp_Wl_Deferred Done");
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

EINTERN void
e_comp_client_post_update_add(E_Client *ec)
{
   if (ec->on_post_updates) return;
   ec->on_post_updates = EINA_TRUE;
   e_comp->post_updates = eina_list_append(e_comp->post_updates, ec);
   REFD(ec, 111);
   e_object_ref(E_OBJECT(ec));
}

EINTERN void
e_comp_client_render_list_add(E_Client *ec)
{
   if (ec->on_render_list) return;
   ec->on_render_list = EINA_TRUE;
   e_comp->render_list = eina_list_append(e_comp->render_list, ec);
   REFD(ec, 111);
   e_object_ref(E_OBJECT(ec));
}

E_API E_Comp_Config *
e_comp_config_get(void)
{
   return conf;
}

EINTERN void
e_comp_shadows_reset(void)
{
   E_Client *ec;

   _e_comp_fps_update();
   E_LIST_FOREACH(e_comp->zones, e_comp_canvas_zone_update);
   E_CLIENT_FOREACH(ec)
     e_comp_object_frame_theme_set(ec->frame, E_COMP_OBJECT_FRAME_RESHADOW);
}

EINTERN Ecore_Window
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

EINTERN void
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

EINTERN void
e_comp_ignore_win_add(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_add(ignores, &win, (void*)1);
   ec = e_pixmap_find_client(type, win);
   if (!ec) return;
   ec->ignored = 1;
   if (ec->visible) evas_object_hide(ec->frame);
}

EINTERN void
e_comp_ignore_win_del(E_Pixmap_Type type, Ecore_Window win)
{
   E_Client *ec;

   eina_hash_del_by_key(ignores, &win);
   ec = e_pixmap_find_client(type, win);
   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return;
   ec->ignored = 0;
   if (ec->visible) evas_object_show(ec->frame);
}

EINTERN Eina_Bool
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
   if (e_comp->nocomp_override > 0)
     {
        // go full compositing
        e_comp_hwc_end(__FUNCTION__);
     }
}

E_API void
e_comp_client_override_del(E_Client *ec)
{
   if (!ec) return;

   ec->comp_override--;
   if (ec->comp_override <= 0)
     {
        ec->comp_override = 0;
        e_comp_render_queue();
     }
}

E_API void
e_comp_client_override_add(E_Client *ec)
{
   if (!ec) return;

   ec->comp_override++;
   if (ec->comp_override > 0)
     e_comp_hwc_client_end(ec, __FUNCTION__);
}

EINTERN E_Comp *
e_comp_find_by_window(Ecore_Window win)
{
   if ((e_comp->win == win) || (e_comp->ee_win == win) || (e_comp->root == win)) return e_comp;
   return NULL;
}

EINTERN void
e_comp_override_timed_pop(void)
{
   if (e_comp->nocomp_override <= 0) return;
   if (e_comp->nocomp_override_timer)
     e_comp->nocomp_override--;
   else
     e_comp->nocomp_override_timer = ecore_timer_add(1.0, _e_comp_override_expire, NULL);
}

EINTERN unsigned int
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
      case E_LAYER_DESK_OBJECT_BELOW: strncpy(buff, "E_LAYER_DESK_OBJECT_BELOW", buff_size); break;
      case E_LAYER_DESK_OBJECT: strncpy(buff, "E_LAYER_DESK_OBJECT", buff_size); break;
      case E_LAYER_DESK_OBJECT_ABOVE: strncpy(buff, "E_LAYER_DESK_OBJECT_ABOVE", buff_size); break;
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

EINTERN Eina_Bool
e_comp_util_kbd_grabbed(void)
{
   return e_client_action_get() || e_grabinput_key_win_get();
}

EINTERN Eina_Bool
e_comp_util_mouse_grabbed(void)
{
   return e_client_action_get() || e_grabinput_mouse_win_get();
}

EINTERN void
e_comp_gl_set(Eina_Bool set)
{
   gl_avail = !!set;
}

EINTERN Eina_Bool
e_comp_gl_get(void)
{
   return gl_avail;
}

EINTERN void
e_comp_button_bindings_ungrab_all(void)
{
   if (e_comp->bindings_ungrab_cb)
     e_comp->bindings_ungrab_cb();
}

EINTERN void
e_comp_button_bindings_grab_all(void)
{
   if (e_comp->bindings_grab_cb)
     e_comp->bindings_grab_cb();
}

EINTERN void
e_comp_client_redirect_toggle(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   if (!conf->enable_advanced_features) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_X) return;
   ec->unredirected_single = !ec->unredirected_single;
   e_client_redirected_set(ec, !ec->redirected);
   ec->no_shape_cut = !ec->redirected;
}

EINTERN Eina_Bool
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
   Eina_List *l, *ll;
   E_Output *eout;
   E_Plane *ep;
   E_Hwc_Window *hwc_window;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(!ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(!ec->zone, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(!ec->zone->output_id, EINA_FALSE);

   eout = e_output_find(ec->zone->output_id);
   if (!eout) return EINA_FALSE;

   if (e_hwc_policy_get(eout->hwc) == E_HWC_POLICY_PLANES)
     {
        if (!e_hwc_mode_get(eout->hwc)) return EINA_FALSE;

        EINA_LIST_FOREACH_SAFE(eout->planes, l, ll, ep)
          {
             E_Client *overlay_ec = ep->ec;
             if (overlay_ec == ec) return EINA_TRUE;
          }
     }
   else
     {
        hwc_window = ec->hwc_window;
        EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

        if (e_hwc_window_is_on_hw_overlay(hwc_window)) return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN E_Zone *
e_comp_zone_find(const char *output_id)
{
   Eina_List *l = NULL;
   E_Zone *zone = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_id, NULL);

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        if (!zone) continue;
        if (!strcmp(zone->output_id, output_id)) return zone;
     }

   return NULL;
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
        int scr_w, scr_h;

        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

        if (ec->zone != zone) continue;

        // check clients to skip composite
        if (e_client_util_ignored_get(ec) || (!evas_object_visible_get(ec->frame)))
          continue;

        // check geometry if located out of screen such as quick panel
        ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &scr_w, &scr_h);
        if (!E_INTERSECTS(0, 0, scr_w, scr_h,
                          ec->client.x, ec->client.y, ec->client.w, ec->client.h))
          continue;

        if (evas_object_data_get(ec->frame, "comp_skip"))
          continue;

        ec_list = eina_list_append(ec_list, ec);

        // find full opaque win and excludes below wins from the visible list.
        e_client_geometry_get(ec, &x, &y, &w, &h);
        if (!E_CONTAINS(x, y, w, h,
                        0, 0, scr_w, scr_h))
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

EINTERN Eina_Bool
e_comp_socket_init(const char *name)
{
   char *dir = NULL;
   char socket_path[108];
   uid_t uid;
   gid_t gid;
   int res;
   E_Config_Socket_Access *sa = NULL;
   Eina_List *l = NULL;
   int l_dir, l_name;
#undef STRERR_BUFSIZE
#define STRERR_BUFSIZE 1024
   char buf[STRERR_BUFSIZE];

   if (!name) return EINA_FALSE;

   dir = e_util_env_get("XDG_RUNTIME_DIR");
   if (!dir) return EINA_FALSE;

   /* check whether buffer size is less than concatenated string which
    * is made of XDG_RUNTIME_DIR, '/', socket name and NULL.
    */
   l_dir = strlen(dir);
   l_name = strlen(name);
   if ((l_dir + l_name + 2) > STRERR_BUFSIZE)
     {
        ERR("Size of buffer is not enough. dir:%s name:%s",
            dir, name);
		free(dir);
        return EINA_FALSE;
     }

   snprintf(socket_path, sizeof(socket_path), "%s/%s", dir, name);
   free(dir);

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

/* set the deactive value to the only primary output */
EINTERN void
e_comp_hwc_deactive_set(Eina_Bool set)
{
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(output);

   e_hwc_deactive_set(output->hwc, set);
}

/* get the deactive value to the only primary output */
EINTERN Eina_Bool
e_comp_hwc_deactive_get(void)
{
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, EINA_FALSE);

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return e_hwc_deactive_get(output->hwc);
}

/* set the multi_plane value to the only primary output */
EINTERN void
e_comp_hwc_multi_plane_set(Eina_Bool set)
{
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(output);

   e_hwc_planes_multi_plane_set(output->hwc, EINA_TRUE);
}

/* get the multi_plane value to the only primary output */
EINTERN Eina_Bool
e_comp_hwc_multi_plane_get(void)
{
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, EINA_FALSE);

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return e_hwc_planes_multi_plane_get(output->hwc);
}

/* end the hwc policy at the primary output */
EINTERN void
e_comp_hwc_end(const char *location)
{
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(output);

   e_hwc_planes_end(output->hwc, location);
}

EINTERN void
e_comp_hwc_client_end(E_Client *ec, const char *location)
{
   E_Zone *zone = NULL;
   E_Output *output = NULL;
   E_Hwc *hwc = NULL;
   E_Hwc_Window *hwc_window = NULL;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN(zone);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   hwc = output->hwc;
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   if (hwc->hwc_policy == E_HWC_POLICY_PLANES)
     {
        e_hwc_planes_client_end(output->hwc, ec, location);
     }
   else if (hwc->hwc_policy == E_HWC_POLICY_WINDOWS)
     {
        hwc_window = ec->hwc_window;
        EINA_SAFETY_ON_NULL_RETURN(hwc_window);

        e_hwc_window_client_type_override(hwc_window);
     }
}

EINTERN Eina_Bool
e_comp_util_client_is_fullscreen(const E_Client *ec)
{
   if ((!ec->visible) || (ec->input_only))
     return EINA_FALSE;
   return ((ec->client.x == 0) && (ec->client.y == 0) &&
       ((ec->client.w) >= e_comp->w) &&
       ((ec->client.h) >= e_comp->h) &&
       (!ec->argb) && (!ec->shaped)
       );
}
