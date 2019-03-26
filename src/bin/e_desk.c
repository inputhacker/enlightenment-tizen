#include "e.h"

/* E_Desk is a child object of E_Zone. A desk is essentially a background
 * and an associated set of client windows. Each zone can have an arbitrary
 * number of desktops.
 */

#define E_DESK_SMART_DATA_GET(obj, ptr)                        \
   E_Desk_Smart_Data *ptr = evas_object_smart_data_get(obj);

#define E_DESK_SMART_DATA_GET_OR_RETURN(obj, ptr)              \
   E_DESK_SMART_DATA_GET(obj, ptr);                            \
   if (!ptr) return

typedef struct _E_Desk_Smart_Data E_Desk_Smart_Data;

struct _E_Desk_Smart_Data
{
   Evas_Object_Smart_Clipped_Data base;
   Eina_List      *clients;
   Eina_List      *handlers;

   struct
   {
      double       ratio_x, ratio_y;
      int          cord_x, cord_y;
      Eina_Bool    enabled;
   } zoom;
};

static void      _e_desk_free(E_Desk *desk);
static void      _e_desk_event_desk_show_free(void *data, void *ev);
static void      _e_desk_event_desk_before_show_free(void *data, void *ev);
static void      _e_desk_event_desk_after_show_free(void *data, void *ev);
static void      _e_desk_event_desk_deskshow_free(void *data, void *ev);
static void      _e_desk_event_desk_name_change_free(void *data, void *ev);
static void      _e_desk_show_begin(E_Desk *desk, int dx, int dy);
static void      _e_desk_hide_begin(E_Desk *desk, int dx, int dy);
static void      _e_desk_event_desk_window_profile_change_free(void *data, void *ev);
static void      _e_desk_event_desk_geometry_change_free(void *data, void *ev);
static Eina_Bool _e_desk_cb_zone_move_resize(void *data, int type EINA_UNUSED, void *event);

static void      _e_desk_smart_init(E_Desk *desk);
static void      _e_desk_smart_add(Evas_Object *obj);
static void      _e_desk_smart_del(Evas_Object *obj);
static void      _e_desk_smart_client_add(Evas_Object *obj, E_Client *ec);
static void      _e_desk_smart_client_del(Evas_Object *obj, E_Client *ec);
static void      _e_desk_object_zoom(Evas_Object *obj, double zoomx, double zoomy, Evas_Coord cx, Evas_Coord cy);
static void      _e_desk_client_zoom(E_Client *ec, double zoomx, double zoomy, Evas_Coord cx, Evas_Coord cy);
static void      _e_desk_util_comp_hwc_disable_set(Eina_Bool enable);

EVAS_SMART_SUBCLASS_NEW(E_DESK_SMART_OBJ_TYPE, _e_desk,
                        Evas_Smart_Class, Evas_Smart_Class,
                        evas_object_smart_clipped_class_get, NULL)

static E_Desk_Flip_Cb _e_desk_flip_cb = NULL;
static void *_e_desk_flip_data = NULL;

E_API int E_EVENT_DESK_SHOW = 0;
E_API int E_EVENT_DESK_BEFORE_SHOW = 0;
E_API int E_EVENT_DESK_AFTER_SHOW = 0;
E_API int E_EVENT_DESK_DESKSHOW = 0;
E_API int E_EVENT_DESK_NAME_CHANGE = 0;
E_API int E_EVENT_DESK_WINDOW_PROFILE_CHANGE = 0;
E_API int E_EVENT_DESK_GEOMETRY_CHANGE = 0;

EINTERN int
e_desk_init(void)
{
   E_EVENT_DESK_SHOW = ecore_event_type_new();
   E_EVENT_DESK_BEFORE_SHOW = ecore_event_type_new();
   E_EVENT_DESK_AFTER_SHOW = ecore_event_type_new();
   E_EVENT_DESK_DESKSHOW = ecore_event_type_new();
   E_EVENT_DESK_NAME_CHANGE = ecore_event_type_new();
   E_EVENT_DESK_WINDOW_PROFILE_CHANGE = ecore_event_type_new();
   E_EVENT_DESK_GEOMETRY_CHANGE = ecore_event_type_new();
   return 1;
}

EINTERN int
e_desk_shutdown(void)
{
   return 1;
}

E_API E_Desk *
e_desk_new(E_Zone *zone, int x, int y)
{
   E_Desk *desk;
   Eina_List *l;
   E_Config_Desktop_Name *cfname;
   E_Config_Desktop_Window_Profile *cfprof;
   char name[40];
   int ok = 0;

   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   desk = E_OBJECT_ALLOC(E_Desk, E_DESK_TYPE, _e_desk_free);
   if (!desk) return NULL;

   desk->zone = zone;
   desk->x = x;
   desk->y = y;

   if (!e_config->use_desk_smart_obj)
     {
        /* need to set geometry of desk even if disable the smart object,
         * because 'E_Client' can be reconfigured base on desk.geom as a member
         * of desk. the reason why this is necessary is all of 'E_Client' is not
         * members of the smart object so far.
         */
        EINA_RECTANGLE_SET(&desk->geom, zone->x, zone->y, zone->w, zone->h);
        E_LIST_HANDLER_APPEND(desk->handlers, E_EVENT_ZONE_MOVE_RESIZE, _e_desk_cb_zone_move_resize, desk);
     }
   else
     {
        /* init smart object */
        _e_desk_smart_init(desk);
     }

   /* Get current desktop's name */
   EINA_LIST_FOREACH(e_config->desktop_names, l, cfname)
     {
        if ((cfname->zone >= 0) &&
            ((int)zone->num != cfname->zone)) continue;
        if ((cfname->desk_x != desk->x) || (cfname->desk_y != desk->y))
          continue;
        desk->name = eina_stringshare_ref(cfname->name);
        ok = 1;
        break;
     }

   if (!ok)
     {
        snprintf(name, sizeof(name), _(e_config->desktop_default_name), x, y);
        desk->name = eina_stringshare_add(name);
     }
   /* Get window profile name for current desktop */
   ok = 0;
   EINA_LIST_FOREACH(e_config->desktop_window_profiles, l, cfprof)
     {
        if ((cfprof->zone >= 0) &&
            ((int)zone->num != cfprof->zone)) continue;
        if ((cfprof->desk_x != desk->x) || (cfprof->desk_y != desk->y))
          continue;
        desk->window_profile = eina_stringshare_ref(cfprof->profile);
        ok = 1;
        break;
     }

   if (!ok)
     desk->window_profile = eina_stringshare_ref(e_config->desktop_default_window_profile);
   return desk;
}

E_API E_Client *
e_desk_client_top_visible_get(const E_Desk *desk)
{
   E_Client *ec;

   E_OBJECT_CHECK_RETURN(desk, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(desk, E_DESK_TYPE, NULL);

   E_CLIENT_REVERSE_FOREACH(ec)
     if (e_client_util_desk_visible(ec, desk) && evas_object_visible_get(ec->frame)) return ec;
   return NULL;
}

E_API void
e_desk_name_set(E_Desk *desk, const char *name)
{
   E_Event_Desk_Name_Change *ev;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   eina_stringshare_replace(&desk->name, name);

   ev = E_NEW(E_Event_Desk_Name_Change, 1);
   if (!ev) return;
   ev->desk = desk;
   e_object_ref(E_OBJECT(desk));
   ecore_event_add(E_EVENT_DESK_NAME_CHANGE, ev,
                   _e_desk_event_desk_name_change_free, NULL);
}

E_API void
e_desk_name_add(int zone, int desk_x, int desk_y, const char *name)
{
   E_Config_Desktop_Name *cfname;

   e_desk_name_del(zone, desk_x, desk_y);

   cfname = E_NEW(E_Config_Desktop_Name, 1);
   if (!cfname) return;
   cfname->zone = zone;
   cfname->desk_x = desk_x;
   cfname->desk_y = desk_y;
   if (name) cfname->name = eina_stringshare_add(name);
   else cfname->name = NULL;
   e_config->desktop_names = eina_list_append(e_config->desktop_names, cfname);
}

E_API void
e_desk_name_del(int zone, int desk_x, int desk_y)
{
   Eina_List *l = NULL;
   E_Config_Desktop_Name *cfname = NULL;

   EINA_LIST_FOREACH(e_config->desktop_names, l, cfname)
     {
        if ((cfname->zone == zone) &&
            (cfname->desk_x == desk_x) && (cfname->desk_y == desk_y))
          {
             e_config->desktop_names =
               eina_list_remove_list(e_config->desktop_names, l);
             if (cfname->name) eina_stringshare_del(cfname->name);
             E_FREE(cfname);
             break;
          }
     }
}

E_API void
e_desk_name_update(void)
{
   const Eina_List *z, *l;
   E_Zone *zone;
   E_Desk *desk;
   E_Config_Desktop_Name *cfname;
   int d_x, d_y, ok;
   char name[40];

   EINA_LIST_FOREACH(e_comp->zones, z, zone)
     {
        for (d_x = 0; d_x < zone->desk_x_count; d_x++)
          {
             for (d_y = 0; d_y < zone->desk_y_count; d_y++)
               {
                  desk = zone->desks[d_x + zone->desk_x_count * d_y];
                  ok = 0;

                  EINA_LIST_FOREACH(e_config->desktop_names, l, cfname)
                    {
                       if ((cfname->zone >= 0) &&
                           ((int)zone->num != cfname->zone)) continue;
                       if ((cfname->desk_x != d_x) ||
                           (cfname->desk_y != d_y)) continue;
                       e_desk_name_set(desk, cfname->name);
                       ok = 1;
                       break;
                    }

                  if (!ok)
                    {
                       snprintf(name, sizeof(name),
                                _(e_config->desktop_default_name),
                                d_x, d_y);
                       e_desk_name_set(desk, name);
                    }
               }
          }
     }
}

E_API void
e_desk_show(E_Desk *desk)
{
   E_Event_Desk_Show *ev = NULL;
   E_Event_Desk_Before_Show *eev = NULL;
   E_Event_Desk_After_Show *eeev = NULL;
   Edje_Message_Float_Set *msg = NULL;
   E_Desk *desk2 = NULL;
   int dx = 0, dy = 0;
   Ecore_Event *eev_ecore_event = NULL;
   Ecore_Event *ev_ecore_event = NULL;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);
   if (desk->visible) return;

   desk2 = e_desk_at_xy_get(desk->zone, desk->zone->desk_x_current, desk->zone->desk_y_current);
   if ((!starting) && (!desk2->visible)) return;
   eev = E_NEW(E_Event_Desk_Before_Show, 1);
   if (!eev) return;
   eev->desk = e_desk_current_get(desk->zone);
   e_object_ref(E_OBJECT(eev->desk));
   eev_ecore_event = ecore_event_add(E_EVENT_DESK_BEFORE_SHOW, eev,
                                     _e_desk_event_desk_before_show_free, NULL);

   if (desk2->visible)
     {
        desk2->visible = 0;
        if ((!dx) && (!dy))
          {
             dx = desk->x - desk2->x;
             dy = desk->y - desk2->y;
          }
        _e_desk_hide_begin(desk2, dx, dy);
        if (desk2->smart_obj)
          evas_object_hide(desk2->smart_obj);
     }

   desk->zone->desk_x_prev = desk->zone->desk_x_current;
   desk->zone->desk_y_prev = desk->zone->desk_y_current;
   desk->zone->desk_x_current = desk->x;
   desk->zone->desk_y_current = desk->y;
   desk->visible = 1;

   msg = alloca(sizeof(Edje_Message_Float_Set) + (4 * sizeof(double)));
   msg->count = 5;
   msg->val[0] = 0.0;
   msg->val[1] = desk->x;
   msg->val[2] = desk->zone->desk_x_count;
   msg->val[3] = desk->y;
   msg->val[4] = desk->zone->desk_y_count;
   if (desk->zone->bg_object)
     edje_object_message_send(desk->zone->bg_object, EDJE_MESSAGE_FLOAT_SET, 0, msg);

#ifndef ENABLE_QUICK_INIT
   int was_zone = 0;
   if (desk->zone->bg_object) was_zone = 1;
#endif
   _e_desk_show_begin(desk, dx, dy);
   if (desk->smart_obj)
     evas_object_show(desk->smart_obj);

#ifndef ENABLE_QUICK_INIT
   if (was_zone)
     e_bg_zone_update(desk->zone, E_BG_TRANSITION_DESK);
   else
     e_bg_zone_update(desk->zone, E_BG_TRANSITION_START);
#endif

   ev = E_NEW(E_Event_Desk_Show, 1);
   if (!ev) goto error;
   ev->desk = desk;
   e_object_ref(E_OBJECT(desk));
   ev_ecore_event = ecore_event_add(E_EVENT_DESK_SHOW, ev, _e_desk_event_desk_show_free, NULL);

   eeev = E_NEW(E_Event_Desk_After_Show, 1);
   if (!eeev) goto error;
   eeev->desk = e_desk_current_get(desk->zone);
   e_object_ref(E_OBJECT(eeev->desk));
   ecore_event_add(E_EVENT_DESK_AFTER_SHOW, eeev,
                   _e_desk_event_desk_after_show_free, NULL);
   e_zone_edge_flip_eval(desk->zone);

   return;

error:
   if (ev)
     {
        if (ev_ecore_event)
          ecore_event_del(ev_ecore_event);
        e_object_unref(E_OBJECT(ev->desk));
        free(ev);
     }
   if (eev)
     {
        if (eev_ecore_event)
          ecore_event_del(eev_ecore_event);
        e_object_unref(E_OBJECT(eev->desk));
        free(eev);
     }
}

E_API void
e_desk_deskshow(E_Zone *zone)
{
   E_Client *ec;
   E_Desk *desk;
   E_Event_Desk_Show *ev;

   E_OBJECT_CHECK(zone);
   E_OBJECT_TYPE_CHECK(zone, E_ZONE_TYPE);

   desk = e_desk_current_get(zone);
   EINA_SAFETY_ON_NULL_RETURN(desk);

   if (desk->deskshow_toggle)
     {
        /* uniconify raises windows and changes stacking order
         * go top-down to avoid skipping windows
         */
        E_CLIENT_REVERSE_FOREACH(ec)
          {
             if (e_client_util_ignored_get(ec)) continue;
             if (ec->desk != desk) continue;
             if (ec->deskshow)
               {
                  ec->deskshow = 0;
                  e_client_uniconify(ec);
               }
          }
     }
   else
     {
        /*
         * iconify raises, so we have to start from the bottom so we are going forward
         */
        E_CLIENT_FOREACH(ec)
          {
             if (e_client_util_ignored_get(ec)) continue;
             if (ec->desk != desk) continue;
             if (ec->iconic) continue;
             if (ec->netwm.state.skip_taskbar) continue;
             if (ec->user_skip_winlist) continue;
             ec->deskshow = 1;
             e_client_iconify(ec);
          }
     }
   desk->deskshow_toggle = !desk->deskshow_toggle;
   ev = E_NEW(E_Event_Desk_Show, 1);
   if (!ev) return;
   ev->desk = desk;
   e_object_ref(E_OBJECT(desk));
   ecore_event_add(E_EVENT_DESK_DESKSHOW, ev,
                   _e_desk_event_desk_deskshow_free, NULL);
}

E_API E_Client *
e_desk_last_focused_focus(E_Desk *desk)
{
   Eina_List *l = NULL;
   E_Client *ec, *ecs = NULL, *focus_ec = NULL;

   EINA_LIST_FOREACH(e_client_focus_stack_get(), l, ec)
     {
        if ((!ec->iconic) && (evas_object_visible_get(ec->frame) || ec->changes.visible) &&
            ((ec->desk == desk) || ((ec->zone == desk->zone) && ec->sticky)) &&
            (ec->icccm.accepts_focus || ec->icccm.take_focus) &&
            (ec->netwm.type != E_WINDOW_TYPE_DOCK) &&
            (ec->netwm.type != E_WINDOW_TYPE_TOOLBAR) &&
            (ec->netwm.type != E_WINDOW_TYPE_MENU) &&
            (ec->netwm.type != E_WINDOW_TYPE_SPLASH) &&
            (ec->netwm.type != E_WINDOW_TYPE_DESKTOP))
          {
             /* this was the window last focused in this desktop */
             if (!ec->lock_focus_out)
               {
                  if (ec->sticky)
                    {
                       ecs = ec;
                       continue;
                    }
                  if (ec->changes.visible && (!evas_object_visible_get(ec->frame)))
                    ec->want_focus = ec->take_focus = 1;
                  else
                    e_client_focus_set_with_pointer(ec);
                  if (e_config->raise_on_revert_focus)
                    evas_object_raise(ec->frame);
                  return ec;
               }
          }
     }
   if (ecs)
     {
        if (ecs->changes.visible && (!evas_object_visible_get(ecs->frame)))
          ecs->want_focus = ecs->take_focus = 1;
        else
          e_client_focus_set_with_pointer(ecs);
        if (e_config->raise_on_revert_focus)
          evas_object_raise(ecs->frame);
        return ecs;
     }

   focus_ec = e_client_focused_get();
   if (focus_ec)
     {
        ELOGF("FOCUS", "focus unset | last_focused_focus", focus_ec);
        evas_object_focus_set(focus_ec->frame, 0);
     }

   return NULL;
}

E_API void
e_desk_row_add(E_Zone *zone)
{
   e_zone_desk_count_set(zone, zone->desk_x_count, zone->desk_y_count + 1);
}

E_API void
e_desk_row_remove(E_Zone *zone)
{
   e_zone_desk_count_set(zone, zone->desk_x_count, zone->desk_y_count - 1);
}

E_API void
e_desk_col_add(E_Zone *zone)
{
   e_zone_desk_count_set(zone, zone->desk_x_count + 1, zone->desk_y_count);
}

E_API void
e_desk_col_remove(E_Zone *zone)
{
   e_zone_desk_count_set(zone, zone->desk_x_count - 1, zone->desk_y_count);
}

E_API E_Desk *
e_desk_current_get(E_Zone *zone)
{
   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   return e_desk_at_xy_get(zone, zone->desk_x_current, zone->desk_y_current);
}

E_API E_Desk *
e_desk_at_xy_get(E_Zone *zone, int x, int y)
{
   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   if ((x >= zone->desk_x_count) || (y >= zone->desk_y_count))
     return NULL;
   else if ((x < 0) || (y < 0))
     return NULL;

   if (!zone->desks) return NULL;
   return zone->desks[x + (y * zone->desk_x_count)];
}

E_API E_Desk *
e_desk_at_pos_get(E_Zone *zone, int pos)
{
   int x, y;

   E_OBJECT_CHECK_RETURN(zone, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(zone, E_ZONE_TYPE, NULL);

   y = pos / zone->desk_x_count;
   x = pos - (y * zone->desk_x_count);

   if ((x >= zone->desk_x_count) || (y >= zone->desk_y_count))
     return NULL;

   return zone->desks[x + (y * zone->desk_x_count)];
}

E_API void
e_desk_xy_get(E_Desk *desk, int *x, int *y)
{
   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   if (x) *x = desk->x;
   if (y) *y = desk->y;
}

E_API void
e_desk_next(E_Zone *zone)
{
   int x, y;

   E_OBJECT_CHECK(zone);
   E_OBJECT_TYPE_CHECK(zone, E_ZONE_TYPE);

   if ((zone->desk_x_count < 2) && (zone->desk_y_count < 2))
     return;

   x = zone->desk_x_current;
   y = zone->desk_y_current;

   x++;
   if (x >= zone->desk_x_count)
     {
        x = 0;
        y++;
        if (y >= zone->desk_y_count) y = 0;
     }

   e_desk_show(e_desk_at_xy_get(zone, x, y));
}

E_API void
e_desk_prev(E_Zone *zone)
{
   int x, y;

   E_OBJECT_CHECK(zone);
   E_OBJECT_TYPE_CHECK(zone, E_ZONE_TYPE);

   if ((zone->desk_x_count < 2) && (zone->desk_y_count < 2))
     return;

   x = zone->desk_x_current;
   y = zone->desk_y_current;

   x--;
   if (x < 0)
     {
        x = zone->desk_x_count - 1;
        y--;
        if (y < 0) y = zone->desk_y_count - 1;
     }
   e_desk_show(e_desk_at_xy_get(zone, x, y));
}

E_API void
e_desk_window_profile_set(E_Desk *desk,
                          const char *profile)
{
   E_Event_Desk_Window_Profile_Change *ev;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   eina_stringshare_replace(&desk->window_profile, profile);

   ev = E_NEW(E_Event_Desk_Window_Profile_Change, 1);
   if (!ev) return;
   ev->desk = desk;
   e_object_ref(E_OBJECT(desk));
   ecore_event_add(E_EVENT_DESK_WINDOW_PROFILE_CHANGE, ev,
                   _e_desk_event_desk_window_profile_change_free, NULL);
}

E_API void
e_desk_window_profile_add(int zone,
                          int desk_x,
                          int desk_y,
                          const char *profile)
{
   E_Config_Desktop_Window_Profile *cfprof;

   e_desk_window_profile_del(zone, desk_x, desk_y);

   cfprof = E_NEW(E_Config_Desktop_Window_Profile, 1);
   if (!cfprof) return;
   cfprof->zone = zone;
   cfprof->desk_x = desk_x;
   cfprof->desk_y = desk_y;
   cfprof->profile = eina_stringshare_add(profile);
   e_config->desktop_window_profiles = eina_list_append(e_config->desktop_window_profiles, cfprof);
}

E_API void
e_desk_window_profile_del(int zone,
                          int desk_x,
                          int desk_y)
{
   Eina_List *l = NULL;
   E_Config_Desktop_Window_Profile *cfprof = NULL;

   EINA_LIST_FOREACH(e_config->desktop_window_profiles, l, cfprof)
     {
        if (!((cfprof->zone == zone) &&
              (cfprof->desk_x == desk_x) &&
              (cfprof->desk_y == desk_y)))
          continue;

        e_config->desktop_window_profiles =
          eina_list_remove_list(e_config->desktop_window_profiles, l);
        eina_stringshare_del(cfprof->profile);
        free(cfprof);
        break;
     }
}

E_API void
e_desk_window_profile_update(void)
{
   const Eina_List *z, *l;
   E_Zone *zone;
   E_Desk *desk;
   E_Config_Desktop_Window_Profile *cfprof;
   int d_x, d_y, ok;

   if (!(e_config->use_desktop_window_profile))
     return;

   EINA_LIST_FOREACH(e_comp->zones, z, zone)
     {
        for (d_x = 0; d_x < zone->desk_x_count; d_x++)
          {
             for (d_y = 0; d_y < zone->desk_y_count; d_y++)
               {
                  desk = zone->desks[d_x + zone->desk_x_count * d_y];
                  ok = 0;

                  EINA_LIST_FOREACH(e_config->desktop_window_profiles, l, cfprof)
                    {
                       if ((cfprof->zone >= 0) &&
                           ((int)zone->num != cfprof->zone)) continue;
                       if ((cfprof->desk_x != d_x) ||
                           (cfprof->desk_y != d_y)) continue;
                       e_desk_window_profile_set(desk, cfprof->profile);
                       ok = 1;
                       break;
                    }

                  if (!ok)
                    {
                       e_desk_window_profile_set
                         (desk, e_config->desktop_default_window_profile);
                    }
               }
          }
     }
}

E_API void
e_desk_flip_cb_set(E_Desk_Flip_Cb cb, const void *data)
{
   _e_desk_flip_cb = cb;
   _e_desk_flip_data = (void*)data;
}

E_API void
e_desk_flip_end(E_Desk *desk)
{
   E_Event_Desk_After_Show *ev;
   E_Client *ec;

   ev = E_NEW(E_Event_Desk_After_Show, 1);
   if (!ev) return;
   ev->desk = desk;
   e_object_ref(E_OBJECT(ev->desk));
   ecore_event_add(E_EVENT_DESK_AFTER_SHOW, ev,
                   _e_desk_event_desk_after_show_free, NULL);

   if ((e_config->focus_policy == E_FOCUS_MOUSE) ||
       (e_config->focus_policy == E_FOCUS_SLOPPY))
     {
        ec = e_client_focused_get();
        /* only set focus/warp pointer if currently focused window
         * is on same screen (user hasn't switched screens during transition)
         */
        if (ec && ec->desk && (ec->desk->zone != desk->zone)) return;
     }
   if (starting) return;
   ec = e_desk_last_focused_focus(desk);
   if ((e_config->focus_policy != E_FOCUS_MOUSE) && (!ec))
     {
        /* we didn't previously have a focused window on this desk
         * but we should, so this is probably the first time the
         * user has flipped to this desk. let's be helpful and
         * focus a random window!
         */
         E_CLIENT_REVERSE_FOREACH(ec)
           {
              /* start with top and go down... */
              if (e_client_util_ignored_get(ec)) continue;
              if (!e_client_util_desk_visible(ec, desk)) continue;
              if (ec->iconic) continue;
              ELOGF("FOCUS", "focus set | desk flip end", ec);
              evas_object_focus_set(ec->frame, 1);
              if (e_config->raise_on_revert_focus)
                evas_object_raise(ec->frame);
              return;
           }
     }
}

E_API unsigned int
e_desks_count(void)
{
   Eina_List *l;
   E_Zone *zone;
   unsigned int count = 0;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        int cx = 0, cy = 0;

        e_zone_desk_count_get(zone, &cx, &cy);
        count += cx * cy;
     }
   return count;
}

E_API void
e_desk_client_add(E_Desk *desk, E_Client *ec)
{
   if (!e_config->use_desk_smart_obj)
     return;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   _e_desk_smart_client_add(desk->smart_obj, ec);
}

E_API void
e_desk_client_del(E_Desk *desk, E_Client *ec)
{
   if (!e_config->use_desk_smart_obj)
     return;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   _e_desk_smart_client_del(desk->smart_obj, ec);
}

E_API void
e_desk_geometry_set(E_Desk *desk, int x, int y, int w, int h)
{
   E_Client *ec;
   E_Maximize max;
   Eina_List *l = NULL, *ll = NULL;
   Evas_Object *m;
   E_Event_Desk_Geometry_Change *ev = NULL;

   int cx, cy, dx, dy;

   if (!e_config->use_desk_smart_obj)
     {
        DBG("Do nothing, Desk Smart Object is disabled");
        return;
     }

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   E_DESK_SMART_DATA_GET_OR_RETURN(desk->smart_obj, sd);

   if ((desk->geom.x == x) && (desk->geom.y == y) &&
       (desk->geom.w == w) && (desk->geom.h == h))
     return;

   dx = x - desk->geom.x;
   dy = y - desk->geom.y;
   EINA_RECTANGLE_SET(&desk->geom, x, y, w, h);

   EINA_LIST_FOREACH(sd->clients, l, ec)
     {
        /* even if the desktop geometry is chagned, the system partial windows such as virtual
         * keyboard and clipboard should be placed at the bottom of the desktop. */
        /* QUICK FIX */
        if (e_policy_client_is_keyboard(ec))
          {
             continue;
          }
        else if ((ec->comp_data) && (ec->comp_data->sub.data))
           {
              continue;
           }
        else if (ec->maximized)
          {
             max = ec->maximized;
             ec->maximized = E_MAXIMIZE_NONE;
             e_client_maximize(ec, max);
          }
        else
          {
             e_client_geometry_get(ec, &cx, &cy, NULL, NULL);
             e_client_util_move_without_frame(ec, cx + dx, cy + dy);
          }
     }

   // E Client as an member of smart object is not changed even though parent obj is changed
   // Workaround : update max geometry if ec is a member of desk->smart_obj
   EINA_LIST_FOREACH(evas_object_smart_members_get(desk->smart_obj), ll, m)
     {
        ec = evas_object_data_get(m, "E_Client");
        if (ec && ec->maximized)
          {
             max = ec->maximized;
             ec->maximized = E_MAXIMIZE_NONE;
             e_client_maximize(ec, max);
          }
     }

   // apply geometry on smart obj
   evas_object_geometry_set(desk->smart_obj, x, y, w, h);

   ev = E_NEW(E_Event_Desk_Geometry_Change, 1);
   if (ev)
     {
        ev->desk = desk;
        ev->x = x;
        ev->y = y;
        ev->w = w;
        ev->h = h;
        e_object_ref(E_OBJECT(desk));
        ecore_event_add(E_EVENT_DESK_GEOMETRY_CHANGE, ev,
                        _e_desk_event_desk_geometry_change_free, NULL);
     }

   e_comp_render_queue();
}

E_API void
e_desk_zoom_set(E_Desk *desk, double zoomx, double zoomy, int cx, int cy)
{
   E_Client *ec;
   Eina_List *l;
   E_Zone *zone = NULL;
   E_Output *eout = NULL;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   if (e_config->use_pp_zoom)
     {
        if (e_comp_screen_pp_support())
          {
             zone = desk->zone;
             eout = e_output_find(zone->output_id);
             if (!eout)
               {
                  ERR("e_desk_zoom_set: fail get eout");
                  return;
               }
             if (!e_output_zoom_set(eout, zoomx, zoomy, cx, cy))
               ERR("e_desk_zoom_set: fail zoom set");
             else
               DBG("e_desk_zoom_set: zoomx:%f, zoomy:%f, x:%d, y:%d", zoomx, zoomy, cx, cy);

             return;
          }
     }

   if (e_config->use_desk_smart_obj)
     {
        E_DESK_SMART_DATA_GET_OR_RETURN(desk->smart_obj, sd);

        if ((sd->zoom.ratio_x != zoomx) || (sd->zoom.ratio_y != zoomy) ||
            (sd->zoom.cord_x != cx) || (sd->zoom.cord_y != cy))
          {
             sd->zoom.ratio_x = zoomx;
             sd->zoom.ratio_y = zoomy;
             sd->zoom.cord_x = cx;
             sd->zoom.cord_y = cy;

             _e_desk_object_zoom(desk->smart_obj, zoomx, zoomy, cx, cy);
             EINA_LIST_FOREACH(sd->clients, l, ec)
               _e_desk_client_zoom(ec, zoomx, zoomy, cx, cy);
          }

        if (!sd->zoom.enabled)
          {
             sd->zoom.enabled = EINA_TRUE;

             evas_object_map_enable_set(desk->smart_obj, EINA_TRUE);
             EINA_LIST_FOREACH(sd->clients, l, ec)
               evas_object_map_enable_set(ec->frame, EINA_TRUE);

             /* FIXME TEMP disable hwc */
             _e_desk_util_comp_hwc_disable_set(EINA_TRUE);
          }
     }
}

E_API Eina_Bool
e_desk_zoom_get(E_Desk *desk, double *zoomx, double *zoomy, int *cx, int *cy)
{
   E_Zone *zone = NULL;
   E_Output *eout = NULL;
   Eina_Bool res = EINA_FALSE;

   E_OBJECT_CHECK_RETURN(desk, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(desk, E_DESK_TYPE, EINA_FALSE);

   if (e_config->use_pp_zoom)
     {
        if (e_comp_screen_pp_support())
          {
             zone = desk->zone;
             eout = e_output_find(zone->output_id);
             if (!eout)
               {
                  ERR("e_desk_zoom_set: fail get eout");
                  return res;
               }

             res = e_output_zoom_get(eout, zoomx, zoomy, cx, cy);
             return res;
          }
     }

   if (e_config->use_desk_smart_obj)
     {
        E_DESK_SMART_DATA_GET(desk->smart_obj, sd);
        EINA_SAFETY_ON_NULL_RETURN_VAL(sd, EINA_FALSE);

        if (zoomx) *zoomx = sd->zoom.ratio_x;
        if (zoomy) *zoomy = sd->zoom.ratio_y;
        if (cx) *cx = sd->zoom.cord_x;
        if (cy) *cy = sd->zoom.cord_y;

        res = EINA_TRUE;
     }

   return res;
}

E_API void
e_desk_zoom_unset(E_Desk *desk)
{
   E_Client *ec;
   Eina_List *l;
   E_Zone *zone = NULL;
   E_Output *eout = NULL;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   if (e_config->use_pp_zoom)
     {
        if (e_comp_screen_pp_support())
          {
             zone = desk->zone;
             eout = e_output_find(zone->output_id);
             if (!eout)
               {
                  ERR("e_desk_zoom_unset: fail get eout");
                  return;
               }

             e_output_zoom_unset(eout);
             DBG("e_desk_zoom_unset");

             return;
          }
     }

   if (e_config->use_desk_smart_obj)
     {
        E_DESK_SMART_DATA_GET_OR_RETURN(desk->smart_obj, sd);

        if (!sd->zoom.enabled)
          return;

        sd->zoom.ratio_x = 1.0;
        sd->zoom.ratio_y = 1.0;
        sd->zoom.cord_x = 0;
        sd->zoom.cord_y = 0;
        sd->zoom.enabled = EINA_FALSE;

        _e_desk_object_zoom(desk->smart_obj, sd->zoom.ratio_x, sd->zoom.ratio_y,
                            sd->zoom.cord_x, sd->zoom.cord_y);
        evas_object_map_enable_set(desk->smart_obj, EINA_FALSE);
        EINA_LIST_FOREACH(sd->clients, l, ec)
          {
             /* NOTE Is it really necessary?
              * Why isn't it enough to just call evas_object_map_enable_set(false)? */
             _e_desk_client_zoom(ec, sd->zoom.ratio_x, sd->zoom.ratio_y,
                                 sd->zoom.cord_x, sd->zoom.cord_y);
             evas_object_map_enable_set(ec->frame, EINA_FALSE);
          }

        /* FIXME TEMP enable hwc */
        _e_desk_util_comp_hwc_disable_set(EINA_FALSE);
     }
}

E_API void
e_desk_smart_member_add(E_Desk *desk, Evas_Object *obj)
{
   if (!e_config->use_desk_smart_obj)
     return;

   E_OBJECT_CHECK(desk);
   E_OBJECT_TYPE_CHECK(desk, E_DESK_TYPE);

   evas_object_smart_member_add(obj, desk->smart_obj);
}

E_API void
e_desk_smart_member_del(Evas_Object *obj)
{
   if (!e_config->use_desk_smart_obj)
     return;

   evas_object_smart_member_del(obj);
}

static void
_e_desk_free(E_Desk *desk)
{
   E_FREE_FUNC(desk->smart_obj, evas_object_del);
   eina_stringshare_del(desk->name);
   desk->name = NULL;
   E_FREE_LIST(desk->handlers, ecore_event_handler_del);
   free(desk);
}

static void
_e_desk_event_desk_show_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Show *ev;

   ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_desk_event_desk_before_show_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Before_Show *ev;

   ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_desk_event_desk_after_show_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_After_Show *ev;

   ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_desk_event_desk_deskshow_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Show *ev;

   ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_desk_event_desk_name_change_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Name_Change *ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   free(ev);
}

static void
_e_desk_event_desk_window_profile_change_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Window_Profile_Change *ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   E_FREE(ev);
}

static void
_e_desk_event_desk_geometry_change_free(void *data EINA_UNUSED, void *event)
{
   E_Event_Desk_Geometry_Change *ev = event;
   e_object_unref(E_OBJECT(ev->desk));
   E_FREE(ev);
}

static Eina_Bool
_e_desk_cb_zone_move_resize(void *data, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Move_Resize *ev;
   E_Desk *desk;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   desk = data;
   if (!desk) return ECORE_CALLBACK_PASS_ON;

   if (ev->zone != desk->zone)
     return ECORE_CALLBACK_PASS_ON;

   EINA_RECTANGLE_SET(&desk->geom, ev->zone->x, ev->zone->y, ev->zone->w, ev->zone->h);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_desk_transition_setup(E_Client *ec, int dx, int dy, int state)
{
   e_comp_object_effect_set(ec->frame, "none");

   return EINA_FALSE;
}

static void
_e_desk_show_end(void *data, Evas_Object *obj EINA_UNUSED, const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
   E_Client *ec = data;

   ec->desk->animate_count--;
   e_client_comp_hidden_set(ec, ec->shaded);
   e_comp_object_effect_unclip(ec->frame);
   ec->hidden = 0;
   if (!ec->visible) evas_object_show(ec->frame);
   if (ec->desk != e_desk_current_get(ec->zone)) return;
   if (!ec->desk->animate_count) e_desk_flip_end(ec->desk);
}

static void
_e_desk_hide_end(void *data, Evas_Object *obj EINA_UNUSED, const char *emission EINA_UNUSED, const char *source EINA_UNUSED)
{
   E_Client *ec = data;

   ec->desk->animate_count--;
   ec->hidden = 1;
   evas_object_hide(ec->frame);
}

static void
_e_desk_show_begin(E_Desk *desk, int dx, int dy)
{
   E_Client *ec;

   if (dx < 0) dx = -1;
   if (dx > 0) dx = 1;
   if (dy < 0) dy = -1;
   if (dy > 0) dy = 1;

   desk->animate_count = 0;
   E_CLIENT_FOREACH(ec)
     {
        if (e_client_util_ignored_get(ec) || (ec->desk->zone != desk->zone) || (ec->iconic)) continue;
        if (ec->moving)
          {
             e_client_desk_set(ec, desk);
             evas_object_show(ec->frame);
             continue;
          }
        if ((ec->desk != desk) || (ec->sticky)) continue;
        if ((!starting) && (!ec->new_client) && _e_desk_transition_setup(ec, dx, dy, 1))
          {
             e_comp_object_effect_stop(ec->frame, _e_desk_hide_end);
             e_comp_object_effect_start(ec->frame, _e_desk_show_end, ec);
             desk->animate_count++;
          }
        else
          ec->hidden = 0;

        e_client_comp_hidden_set(ec, ec->hidden || ec->shaded);
        evas_object_show(ec->frame);
     }
   e_desk_flip_end(desk);
}

static void
_e_desk_hide_begin(E_Desk *desk, int dx, int dy)
{
   E_Client *ec;

   if (dx < 0) dx = -1;
   if (dx > 0) dx = 1;
   if (dy < 0) dy = -1;
   if (dy > 0) dy = 1;

   desk->animate_count = 0;
   E_CLIENT_FOREACH(ec)
     {
        if (e_client_util_ignored_get(ec) || (ec->desk->zone != desk->zone) || (ec->iconic)) continue;
        if (ec->moving) continue;
        if ((ec->desk != desk) || (ec->sticky)) continue;
        if ((!starting) && (!ec->new_client) && _e_desk_transition_setup(ec, -dx, -dy, 0))
          {
             e_comp_object_effect_stop(ec->frame, _e_desk_show_end);
             e_comp_object_effect_start(ec->frame, _e_desk_hide_end, ec);
             desk->animate_count++;
          }
        else
          {
             ec->hidden = 1;
             evas_object_show(ec->frame);
             ec->changes.visible = 0;
             evas_object_hide(ec->frame);
          }
        e_client_comp_hidden_set(ec, EINA_TRUE);
     }
}

static void
_e_desk_smart_init(E_Desk *desk)
{
   E_Zone *zone;

   zone = desk->zone;

   if (!e_config->use_desk_smart_obj)
     return;

   desk->smart_obj = evas_object_smart_add(e_comp->evas, _e_desk_smart_class_new());
   e_desk_geometry_set(desk, zone->x, zone->y, zone->w, zone->h);

   E_DESK_SMART_DATA_GET_OR_RETURN(desk->smart_obj, sd);

   sd->zoom.ratio_x = 1.0;
   sd->zoom.ratio_y = 1.0;
   sd->zoom.cord_x = 0;
   sd->zoom.cord_y = 0;
}

static Eina_Bool
_e_desk_smart_client_cb_resize(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Desk_Smart_Data *sd;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   sd = data;
   ec = ev->ec;
   if (!ec) goto end;

   if (!eina_list_data_find(sd->clients, ec))
     goto end;

   if (sd->zoom.enabled)
     _e_desk_client_zoom(ec,
                         sd->zoom.ratio_x, sd->zoom.ratio_y,
                         sd->zoom.cord_x, sd->zoom.cord_y);
end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_desk_smart_add(Evas_Object *obj)
{
   EVAS_SMART_DATA_ALLOC(obj, E_Desk_Smart_Data);

   /* to apply zoom transformation whenever the client's size is changed. */
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_RESIZE, _e_desk_smart_client_cb_resize, priv);

   /* FIXME hard coded, it will be laid upper than unpacked clients */
   evas_object_layer_set(obj, E_LAYER_DESK_OBJECT);

   _e_desk_parent_sc->add(obj);
}

static void
_e_desk_smart_del(Evas_Object *obj)
{
   _e_desk_parent_sc->del(obj);

   E_DESK_SMART_DATA_GET_OR_RETURN(obj, sd);

   E_FREE_LIST(sd->handlers, ecore_event_handler_del);
   eina_list_free(sd->clients);
   free(sd);

   evas_object_smart_data_set(obj, NULL);
}

static void
_e_desk_smart_member_add(Evas_Object *obj, Evas_Object *child)
{
   E_Client *ec;

   _e_desk_parent_sc->member_add(obj, child);

   ec = evas_object_data_get(child, "E_Client");
   if (ec)
     e_desk_client_del(ec->desk, ec);
}

static void
_e_desk_smart_member_del(Evas_Object *obj, Evas_Object *child)
{
   E_Client *ec = NULL;
   Evas_Object *parent = NULL;

   _e_desk_parent_sc->member_del(obj, child);

   // if quickpanel packed into mover smart obj, _e_desk_smart_member_del is called
   // but parent is still e_desk, because mover's parent is the same e_desk
   // than don't add ec on the sd->clists
   parent = evas_object_smart_parent_get(child);
   if (parent && (parent == obj)) return;

   ec = evas_object_data_get(child, "E_Client");
   if (ec)
     e_desk_client_add(ec->desk, ec);
}

static void
_e_desk_smart_set_user(Evas_Smart_Class *sc)
{
   sc->add = _e_desk_smart_add;
   sc->del = _e_desk_smart_del;
   sc->member_add = _e_desk_smart_member_add;
   sc->member_del = _e_desk_smart_member_del;
}

static void
_e_desk_smart_client_add(Evas_Object *obj, E_Client *ec)
{
   Evas_Object *parent = NULL;

   E_DESK_SMART_DATA_GET_OR_RETURN(obj, sd);

   // if ec is a member of e_desk, don't add it in data.
   parent = evas_object_smart_parent_get(ec->frame);
   if (parent && (parent == ec->desk->smart_obj)) return;

   if (eina_list_data_find(sd->clients, ec))
     return;

   sd->clients = eina_list_append(sd->clients, ec);
   evas_object_smart_changed(obj);
}

static void
_e_desk_smart_client_del(Evas_Object *obj, E_Client *ec)
{
   E_DESK_SMART_DATA_GET_OR_RETURN(obj, sd);

   if (!eina_list_data_find(sd->clients, ec))
     return;

   if (sd->zoom.enabled)
     _e_desk_client_zoom(ec, 1.0, 1.0, 0, 0);

   sd->clients = eina_list_remove(sd->clients, ec);
   evas_object_smart_changed(obj);
}

static void
_e_desk_util_comp_hwc_disable_set(Eina_Bool disable)
{
   if (disable)
     e_comp_hwc_end("in runtime by e_desk");

   e_comp_hwc_deactive_set(disable);
}

static void
_e_desk_object_zoom(Evas_Object *obj, double zoomx, double zoomy, Evas_Coord cx, Evas_Coord cy)
{
   Evas_Map *map;
   Eina_Bool enabled;

   map = evas_map_new(4);
   evas_map_util_object_move_sync_set(map, EINA_TRUE);
   evas_map_util_points_populate_from_object(map, obj);
   evas_map_util_zoom(map, zoomx, zoomy, cx, cy);
   evas_object_map_set(obj, map);
   enabled = ((zoomx != 1.0) || (zoomy != 1.0));
   evas_object_map_enable_set(obj, enabled);
   evas_map_free(map);
}

static void
_e_desk_client_zoom(E_Client *ec, double zoomx, double zoomy, Evas_Coord cx, Evas_Coord cy)
{
   _e_desk_object_zoom(ec->frame, zoomx, zoomy, cx, cy);
}
