#include "e.h"

#define E_MAGNIFIER_SMART_DATA_GET(obj, ptr)                        \
   E_Magnifier_Smart_Data *ptr = evas_object_smart_data_get(obj);

#define E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(obj, ptr)              \
   E_MAGNIFIER_SMART_DATA_GET(obj, ptr);                            \
   if (!ptr) return

typedef struct _E_Magnifier_Smart_Data E_Magnifier_Smart_Data;

struct _E_Magnifier_Smart_Data
{
   Evas_Object_Smart_Clipped_Data base;
   Eina_List      *handlers;
   E_Desk         *desk;

   int stand_alone_mode;
   E_Magnifier_Zoom_Ratio ratio;
   struct 
   {
      struct
      {
         int x, y, w, h;
      } user;
      struct
      {
         int x, y, w, h;
      } system;
   } geom;
   Eina_Bool    enabled;
};

EVAS_SMART_SUBCLASS_NEW(E_MAGNIFIER_SMART_OBJ_TYPE, _e_magnifier,
                    Evas_Smart_Class, Evas_Smart_Class,
                    evas_object_smart_clipped_class_get, NULL);


static void      _e_magnifier_smart_init(void);
static void      _e_magnifier_smart_add(Evas_Object *obj);
static void      _e_magnifier_smart_del(Evas_Object *obj);
static Eina_Bool _e_magnifier_proxy_ec_new(E_Client *ec);
static void      _e_magnifier_proxy_ec_del(E_Client *ec);
static Eina_Bool _e_magnifier_proxy_ec_all_add(E_Desk *desk);
static Eina_Bool _e_magnifier_proxy_ec_all_remove(void);


static Evas_Object *_e_magnifier_mgr = NULL;

static void
_e_magnifier_smart_init(void)
{
   E_Zone *zone;

   _e_magnifier_mgr = evas_object_smart_add(e_comp->evas, _e_magnifier_smart_class_new());
   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   ELOGF("MAGNIFIER", "INIT Magnifier (%p)", NULL, _e_magnifier_mgr);

   zone = e_zone_current_get();

   sd->ratio = E_MAGNIFIER_ZOOM_RATIO_150;

   sd->geom.system.x = 0;
   sd->geom.system.y = 0;
   sd->geom.system.w = 360;
   sd->geom.system.h = 360;

   evas_object_move(_e_magnifier_mgr, zone->x, zone->y);
   evas_object_resize(_e_magnifier_mgr, zone->w, zone->h);
}

static Eina_Bool
_e_magnifier_proxy_ec_new(E_Client *ec)
{
   Eina_Bool ret;

   if (!ec) return EINA_FALSE;
   if (!ec->frame) return EINA_FALSE;
   if (ec->is_magnifier) return EINA_FALSE;
   if (ec->magnifier_proxy)
     {
        ELOGF("MAGNIFIER", "Aready magnifier proxy exist... proxy:%p", ec, ec->magnifier_proxy);
        return EINA_FALSE;
     }

   ec->magnifier_proxy = evas_object_image_filled_add(e_comp->evas);
   if (!ec->magnifier_proxy)
     {
        ELOGF("MAGNIFIER", "CAN NOT make PROXY object..", ec);
        return EINA_FALSE;
     }

   ELOGF("MAGNIFIER", "New PROXY object.. proxy:%p", ec, ec->magnifier_proxy);

   ret = evas_object_image_source_set(ec->magnifier_proxy, ec->frame);
   if (!ret)
     {
        ELOGF("MAGNIFIER", "Fail to set image source to PROXY object..", ec);
        return EINA_FALSE;
     }

   evas_object_image_source_events_set(ec->magnifier_proxy, EINA_TRUE);
   evas_object_image_source_clip_set(ec->magnifier_proxy, EINA_FALSE);

   evas_object_move(ec->magnifier_proxy, ec->x, ec->y);
   evas_object_resize(ec->magnifier_proxy, ec->w, ec->h);

   evas_object_show(ec->magnifier_proxy);

   return EINA_TRUE;   
}

static void
_e_magnifier_proxy_ec_del(E_Client *ec)
{
   if (!ec) return;
   if (!ec->magnifier_proxy) return;

   ELOGF("MAGNIFIER", "Delete PROXY object.. proxy:%p", ec, ec->magnifier_proxy);

   evas_object_del(ec->magnifier_proxy);
   ec->magnifier_proxy = NULL;
}

static Eina_Bool
_e_magnifier_proxy_ec_all_add(E_Desk *desk)
{
   E_Client *ec = NULL;
   Eina_Bool ret;

   if (!desk) return EINA_FALSE;

   E_CLIENT_FOREACH(ec)
     {
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->desk != desk) continue;
        if (!ec->frame) continue;
        if (ec->is_magnifier) continue;

        ret = _e_magnifier_proxy_ec_new(ec);
        if (!ret) continue;

        e_magnifier_smart_member_add(desk, ec->magnifier_proxy);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_magnifier_proxy_ec_all_remove(void)
{
   E_Client *ec = NULL;

   E_CLIENT_FOREACH(ec)
     {
        e_magnifier_smart_member_del(ec->magnifier_proxy);
        _e_magnifier_proxy_ec_del(ec);
     }

   return EINA_TRUE;
}

static void
_e_magnifier_smart_member_reorder(E_Desk *desk)
{
   E_Client *ec;
   Evas_Object *proxy;
   Evas_Object *smart_parent;

   E_CLIENT_FOREACH(ec)
     {
        proxy = ec->magnifier_proxy;
        if (!proxy) continue;

        smart_parent = evas_object_smart_parent_get(proxy);
        if (smart_parent == _e_magnifier_mgr)
          {
             evas_object_raise(proxy);
          }
     }
}

static void
_e_magnifier_calculate_zoom_geometry(E_Magnifier_Zoom_Ratio ratio, int x, int y, int w, int h, int *nx, int *ny, int *nw, int *nh)
{
   if (!nx || !ny || !nw || !nh)
     return;

   switch (ratio)
     {
      case E_MAGNIFIER_ZOOM_RATIO_100:
         // zoom 1.0
         *nx = x;
         *ny = y;
         *nw = w;
         *nh = h;
         break;
      case E_MAGNIFIER_ZOOM_RATIO_110:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_120:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_130:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_140:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_150:
         // zoom 1.5
         *nx = x + (w/8);
         *ny = y + (h/8);
         *nw = w * 0.67;
         *nh = h * 0.67;
         break;
      case E_MAGNIFIER_ZOOM_RATIO_160:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_170:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_180:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_190:
         // TODO: need to implement
         break;
      case E_MAGNIFIER_ZOOM_RATIO_200:
         *nx = x + (w/4);
         *ny = y + (h/4);
         *nw = w/2;
         *nh = h/2;
         break;
      default:
         break;
     }
}

static void
_e_magnifier_apply_zoom(Evas_Object *zoom_obj)
{
   if (!zoom_obj) return;

   E_Magnifier_Zoom_Ratio zoom_ratio;
   int x, y, w, h;
   int mx, my, mw, mh;
   Evas_Map *map;

   //Evas Map
   map = evas_map_new(4);

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   x = sd->geom.system.x;
   y = sd->geom.system.y;
   w = sd->geom.system.w;
   h = sd->geom.system.h;

   mx = sd->geom.system.x;
   my = sd->geom.system.y;
   mw = sd->geom.system.w;
   mh = sd->geom.system.h;

   evas_map_point_coord_set(map, 0, x, y, 0);
   evas_map_point_coord_set(map, 1, x + w, y, 0);
   evas_map_point_coord_set(map, 2, x + w, y + h, 0);
   evas_map_point_coord_set(map, 3, x, y + h, 0);

   //ELOGF("MAGNIFIER", "zoom_obj(%p) sd->geom.system(%d,%d,%d,%d)", NULL, zoom_obj, x, y, w, h);

   zoom_ratio = sd->ratio;
   _e_magnifier_calculate_zoom_geometry(zoom_ratio, x, y, w, h, &mx, &my, &mw, &mh);

   //ELOGF("MAGNIFIER", "zoom_obj(%p) uv set(%d,%d,%d,%d)", NULL, zoom_obj, mx, my, mw, mh);

   evas_map_point_image_uv_set(map, 0, mx, my);
   evas_map_point_image_uv_set(map, 1, mx+mw, my);
   evas_map_point_image_uv_set(map, 2, mx+mw, my+mh);
   evas_map_point_image_uv_set(map, 3, mx, my+mh);

   // apply Evas Map to btn
   evas_object_map_set(zoom_obj, map);
   evas_object_map_enable_set(zoom_obj, EINA_TRUE);

   // Remove Map
   evas_map_free(map);
}

static void
_e_magnifier_smart_set_user(Evas_Smart_Class *sc)
{
   sc->add = _e_magnifier_smart_add;
   sc->del = _e_magnifier_smart_del;
}

static Eina_Bool
_e_magnifier_smart_client_cb_add(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   ec = ev->ec;
   if (!ec) goto end;

   _e_magnifier_proxy_ec_new(ec);
   e_magnifier_smart_member_add(ec->desk, ec->magnifier_proxy);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_magnifier_smart_client_cb_remove(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   ec = ev->ec;
   if (!ec) goto end;

   if (ec->magnifier_proxy)
     {
        e_magnifier_smart_member_del(ec->magnifier_proxy);
        _e_magnifier_proxy_ec_del(ec);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_magnifier_smart_client_cb_stack(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   ec = ev->ec;
   if (!ec) goto end;

   _e_magnifier_smart_member_reorder(ec->desk);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_magnifier_smart_client_cb_show(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   ec = ev->ec;
   if (!ec) goto end;

   if (ec->magnifier_proxy)
     {
        evas_object_show(ec->magnifier_proxy);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_magnifier_smart_client_cb_hide(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec = NULL;

   if (!data) goto end;
   if (!event) goto end;

   ev = event;
   ec = ev->ec;
   if (!ec) goto end;

   if (ec->magnifier_proxy)
     {
        evas_object_hide(ec->magnifier_proxy);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_magnifier_smart_add(Evas_Object *obj)
{
   EVAS_SMART_DATA_ALLOC(obj, E_Magnifier_Smart_Data);

   /* to apply zoom transformation whenever the client's size is changed. */
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_ADD, _e_magnifier_smart_client_cb_add, priv);
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_REMOVE, _e_magnifier_smart_client_cb_remove, priv);
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_STACK, _e_magnifier_smart_client_cb_stack, priv);
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_SHOW, _e_magnifier_smart_client_cb_show, priv);
   E_LIST_HANDLER_APPEND(priv->handlers, E_EVENT_CLIENT_HIDE, _e_magnifier_smart_client_cb_hide, priv);

   /* FIXME hard coded, it will be laid upper than unpacked clients */
   evas_object_layer_set(obj, E_LAYER_DESK_OBJECT_BELOW);

   _e_magnifier_parent_sc->add(obj);
}

static void
_e_magnifier_smart_del(Evas_Object *obj)
{
   _e_magnifier_parent_sc->del(obj);

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(obj, sd);

   E_FREE_LIST(sd->handlers, ecore_event_handler_del);
   free(sd);

   evas_object_smart_data_set(obj, NULL);
}

static void
_e_magnifier_cb_mouse_move_proxy(void *data,
                                 Evas *e EINA_UNUSED,
                                 Evas_Object *obj,
                                 void *event_info)
{
   Evas_Event_Mouse_Move *ev = event_info;
   Evas_Object *target_obj;
   int w, h;
   int nx, ny;

   target_obj = _e_magnifier_mgr;
   if (!target_obj) return;

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   w = sd->geom.system.w;
   h = sd->geom.system.h;

   nx = ev->cur.output.x - (w/2);
   ny = ev->cur.output.y - (h/2);

   sd->geom.system.x = nx;
   sd->geom.system.y = ny;

   _e_magnifier_apply_zoom(_e_magnifier_mgr);
}

static void
_e_magnifier_cb_mouse_down_proxy(void *data,
                                 Evas *e EINA_UNUSED,
                                 Evas_Object *obj,
                                 void *event_info)
{
   Evas_Object *target_obj;

   target_obj = _e_magnifier_mgr;
   if (!target_obj) return;

   evas_object_event_callback_add(target_obj, EVAS_CALLBACK_MOUSE_MOVE, _e_magnifier_cb_mouse_move_proxy, NULL);
}

static void
_e_magnifier_cb_mouse_up_proxy(void *data,
                               Evas *e EINA_UNUSED,
                               Evas_Object *obj,
                               void *event_info)
{
   Evas_Object *target_obj;

   target_obj = _e_magnifier_mgr;
   if (!target_obj) return;

   evas_object_event_callback_del(target_obj, EVAS_CALLBACK_MOUSE_MOVE, _e_magnifier_cb_mouse_move_proxy);
}

static void
_e_magnifier_zoom_obj_geometry_convert_set(int angle, int x, int y, int w, int h, int tx, int ty, int tw, int th)
{
   int nx, ny, nw, nh;

   switch (angle)
     {
      case 90:
         // TODO: need to implement

      case 180:
         // TODO: need to implement

      case 270:
         // TODO: need to implement

      case 0:
      default:
         nx = x + tx;
         ny = y + ty;
         nw = w;
         nh = h;
         break;
     }

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   sd->geom.user.x = nx;
   sd->geom.user.y = ny;
   sd->geom.user.w = nw;
   sd->geom.user.h = nh;

   //   ELOGF("MAGNIFIER", "New position.. (%d,%d,%dx%d)", NULL, nx, ny, nw, nh);
   _e_magnifier_apply_zoom(_e_magnifier_mgr);
}



static void
_e_magnifier_cb_owner_move_resize(void *data EINA_UNUSED,
                                  Evas *e EINA_UNUSED,
                                  Evas_Object *obj,
                                  void *event_info EINA_UNUSED)
{
   int x, y, w, h;
   int nx, ny, nw, nh;

   evas_object_geometry_get(obj, &x, &y, &w, &h);

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   // TODO: Need to check rotation...
   nx = x + sd->geom.user.x;
   ny = y + sd->geom.user.y;
   nw = sd->geom.user.w;
   nh = sd->geom.user.h;

   sd->geom.system.x = nx;
   sd->geom.system.y = ny;
   sd->geom.system.w = nw;
   sd->geom.system.h = nh;

   //ELOGF("MAGNIFIER", "Magnifier Owner MoveResize (%,%d,%dx%d). Apply Geometry (%d,%d,%dx%d)", NULL, x, y, w, h, nx, ny, nw, nh);
   _e_magnifier_apply_zoom(_e_magnifier_mgr);
}


EINTERN int
e_magnifier_init(void)
{
   return 1;
}

EINTERN int
e_magnifier_shutdown(void)
{
   return 1;
}

E_API Eina_Bool
e_magnifier_new(void)
{
   ELOGF("MAGNIFIER", "NEW Magnifier", NULL);

   _e_magnifier_smart_init();

   E_Zone *zone;
   E_Desk *desk;

   zone = e_zone_current_get();
   desk = e_desk_current_get(zone);

   _e_magnifier_proxy_ec_all_add(desk);

   return EINA_TRUE;
}

E_API void
e_magnifier_del(void)
{
   ELOGF("MAGNIFIER", "DELETE Magnifier", NULL);

   _e_magnifier_proxy_ec_all_remove();

   if (_e_magnifier_mgr)
     {
        evas_object_del(_e_magnifier_mgr);
        _e_magnifier_mgr = NULL;
     }
}

E_API void
e_magnifier_show(void)
{
   Evas_Object *target_obj;

   ELOGF("MAGNIFIER", "SHOW Magnifier", NULL);
   evas_object_show(_e_magnifier_mgr);

   _e_magnifier_apply_zoom(_e_magnifier_mgr);

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);

   if (sd->stand_alone_mode)
     {
        target_obj = _e_magnifier_mgr;

        evas_object_event_callback_add(target_obj, EVAS_CALLBACK_MOUSE_DOWN, _e_magnifier_cb_mouse_down_proxy, NULL);
        evas_object_event_callback_add(target_obj, EVAS_CALLBACK_MOUSE_UP, _e_magnifier_cb_mouse_up_proxy, NULL);
     }
}

E_API void
e_magnifier_hide(void)
{
   Evas_Object *target_obj;

   ELOGF("MAGNIFIER", "HIDE Magnifier", NULL);
   evas_object_hide(_e_magnifier_mgr);

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd);
   if (sd->stand_alone_mode)
     {
        target_obj = _e_magnifier_mgr;

        evas_object_event_callback_del(target_obj, EVAS_CALLBACK_MOUSE_DOWN, _e_magnifier_cb_mouse_down_proxy);
        evas_object_event_callback_del(target_obj, EVAS_CALLBACK_MOUSE_UP, _e_magnifier_cb_mouse_up_proxy);
     }
}

E_API Eina_Bool
e_magnifier_zoom_obj_ratio_set(E_Client* ec, E_Magnifier_Zoom_Ratio ratio)
{
   if ((ratio < E_MAGNIFIER_ZOOM_RATIO_100) ||
       (ratio > E_MAGNIFIER_ZOOM_RATIO_200))
     return EINA_FALSE;

   E_MAGNIFIER_SMART_DATA_GET_OR_RETURN(_e_magnifier_mgr, sd) EINA_FALSE;
   sd->ratio = ratio;

   _e_magnifier_apply_zoom(_e_magnifier_mgr);

   return EINA_TRUE;
}

E_API Eina_Bool
e_magnifier_zoom_obj_geometry_set(E_Client *ec, int angle, int x, int y, int w, int h)
{
   if (!ec) return EINA_FALSE;

   ELOGF("MAGNIFIER", "Zoom obj geometry set (%d,%d,%dx%d)", ec, x, y, w, h);

   E_Desk *desk = NULL;
   int tx, ty, tw, th;

   tx = ec->zone->x;
   ty = ec->zone->y;
   tw = ec->zone->w;
   th = ec->zone->h;

   if (e_config->use_desk_smart_obj)
     {
        desk = e_desk_current_get(ec->zone);
        if (desk)
          {
             tx = desk->geom.x;
             ty = desk->geom.y;
             tw = desk->geom.w;
             th = desk->geom.h;
          }
     }

   _e_magnifier_zoom_obj_geometry_convert_set(angle, x, y, w, h, tx, ty, tw, th);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_magnifier_smart_member_add(E_Desk *desk, Evas_Object *obj)
{
   E_OBJECT_CHECK_RETURN(desk, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(desk, E_DESK_TYPE, EINA_FALSE);

   ELOGF("WAYFORU", "MAGNIFIER.... SMART MEMBER ADD... obj:%p", NULL, obj);
   evas_object_smart_member_add(obj, _e_magnifier_mgr);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_magnifier_smart_member_del(Evas_Object *obj)
{
   Evas_Object *parent = NULL;

   if (!obj) return EINA_FALSE;
   parent = evas_object_smart_parent_get(obj);

   ELOGF("WAYFORU", "MAGNIFIER.... SMART MEMBER DEL... obj:%p", NULL, obj);

   if (parent != _e_magnifier_mgr)
     return EINA_FALSE;

   ELOGF("WAYFORU", "MAGNIFIER.... SMART MEMBER DEL... obj:%p", NULL, obj);

   evas_object_smart_member_del(obj);
   return EINA_TRUE;
}

E_API Eina_Bool
e_magnifier_owner_set(E_Client *ec)
{
   if (!ec) return EINA_FALSE;

   ELOGF("MAGNIFIER", "SET Magnifier Owner", ec);

   ec->is_magnifier = EINA_TRUE;
   ec->exp_iconify.deiconify_update = EINA_FALSE;
   evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ALERT_HIGH);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE, _e_magnifier_cb_owner_move_resize, NULL);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE, _e_magnifier_cb_owner_move_resize, NULL);

   return EINA_TRUE;
}
