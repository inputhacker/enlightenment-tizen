#include "e.h"
#include "services/e_service_volume.h"

#include <wayland-server.h>
#include <tzsh_server.h>

#define REGION_OBJS_FOREACH(l, o) \
   EINA_LIST_FOREACH(_volume_region_objs[_volume_cur_angle_map], l, o)

#define REGION_OBJS_VISIBLE_CHANGE(V) \
do { \
   Eina_List *l; \
   Evas_Object *o; \
   EINA_LIST_FOREACH(_volume_region_objs[_volume_cur_angle_map], l, o) \
     { \
        if (V) evas_object_show(o); \
        else evas_object_hide(o); \
     } \
} while(0)
#define REGION_OBJS_SHOW() REGION_OBJS_VISIBLE_CHANGE(EINA_TRUE)
#define REGION_OBJS_HIDE() REGION_OBJS_VISIBLE_CHANGE(EINA_FALSE)

/* private data for volume */
static struct wl_resource  *_volume_wl_touch = NULL;
static E_Client            *_volume_ec = NULL;
static Eina_List           *_volume_region_objs[E_POLICY_ANGLE_MAP_NUM];
static E_Policy_Angle_Map              _volume_cur_angle_map = E_POLICY_ANGLE_MAP_0;
static Eina_Bool            _volume_ec_ev_init = EINA_FALSE;

/* event handler */
static Ecore_Event_Handler *_rot_handler = NULL;
static E_Client_Hook       *_volume_del_hook = NULL;

EINTERN E_Client *
e_service_volume_client_get(void)
{
   return _volume_ec;
}

static void
_volume_region_obj_cb_mouse_move(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Mouse_Move *e = event;

   wl_touch_send_motion(_volume_wl_touch, e->timestamp, 0, // id 0 for the 1st figner
                        wl_fixed_from_int(e->cur.canvas.x - _volume_ec->client.x),
                        wl_fixed_from_int(e->cur.canvas.y - _volume_ec->client.y));
}

static void
_volume_region_obj_cb_mouse_down(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Mouse_Down *e = event;
   uint32_t serial;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_touch_send_down(_volume_wl_touch, serial, e->timestamp,
                      _volume_ec->comp_data->surface, 0,
                      wl_fixed_from_int(e->canvas.x - _volume_ec->client.x),
                      wl_fixed_from_int(e->canvas.y - _volume_ec->client.y));
}

static void
_volume_region_obj_cb_mouse_up(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Mouse_Up *e = event;
   uint32_t serial;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_touch_send_up(_volume_wl_touch, serial, e->timestamp, 0);
}

static void
_volume_region_obj_cb_multi_down(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Multi_Down *e = event;
   uint32_t serial;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_touch_send_down(_volume_wl_touch, serial, e->timestamp,
                      _volume_ec->comp_data->surface, e->device,
                      wl_fixed_from_int(e->canvas.x - _volume_ec->client.x),
                      wl_fixed_from_int(e->canvas.y - _volume_ec->client.y));
}

static void
_volume_region_obj_cb_multi_up(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Multi_Up *e = event;
   uint32_t serial;

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wl_touch_send_up(_volume_wl_touch, serial, e->timestamp, e->device);
}

static void
_volume_region_obj_cb_multi_move(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   Evas_Event_Multi_Move *e = event;

   wl_touch_send_motion(_volume_wl_touch, e->timestamp, e->device,
                        wl_fixed_from_int(e->cur.canvas.x - _volume_ec->client.x),
                        wl_fixed_from_int(e->cur.canvas.y - _volume_ec->client.y));
}

static void
_volume_client_evas_cb_show(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   /* show region objects in current rotation */
   REGION_OBJS_SHOW();
}

static void
_volume_client_evas_cb_hide(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   /* hide region objects in current rotation */
   REGION_OBJS_HIDE();
}

static void
_volume_client_evas_cb_move(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *volume_obj, void *event EINA_UNUSED)
{
   Eina_List *l;
   Eina_Rectangle *r;
   Evas_Object *region_obj;
   int x, y;

   REGION_OBJS_FOREACH(l, region_obj)
     {
        r = evas_object_data_get(region_obj, "content_rect");
        if (EINA_UNLIKELY(r == NULL))
          continue;

        evas_object_geometry_get(volume_obj, &x, &y, NULL, NULL);
        evas_object_move(region_obj, x + r->x, y + r->y);
     }
}

static void
_volume_client_evas_cb_restack(void *data EINA_UNUSED, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_List *l;
   Evas_Object *region_obj;

   REGION_OBJS_FOREACH(l, region_obj)
      evas_object_stack_above(region_obj, _volume_ec->frame);
}

static Eina_Bool
_region_objs_is_empty(void)
{
   int i;

   for (i = E_POLICY_ANGLE_MAP_0; i < E_POLICY_ANGLE_MAP_NUM; i++)
     {
        if (_volume_region_objs[i])
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_region_obj_del(Evas_Object *obj)
{
   Eina_Rectangle *r;

   r = evas_object_data_get(obj, "content_rect");
   E_FREE_FUNC(r, eina_rectangle_free);
   evas_object_del(obj);
}

static void
_region_objs_del(E_Policy_Angle_Map angle_map)
{
   Evas_Object *obj;

   EINA_LIST_FREE(_volume_region_objs[angle_map], obj)
      _region_obj_del(obj);

   if ((_volume_ec_ev_init) &&
       (_region_objs_is_empty()))
     {
        _volume_ec_ev_init = EINA_FALSE;

        evas_object_event_callback_del(_volume_ec->frame, EVAS_CALLBACK_SHOW,
                                       _volume_client_evas_cb_show);
        evas_object_event_callback_del(_volume_ec->frame, EVAS_CALLBACK_HIDE,
                                       _volume_client_evas_cb_hide);
        evas_object_event_callback_del(_volume_ec->frame, EVAS_CALLBACK_MOVE,
                                       _volume_client_evas_cb_move);
        evas_object_event_callback_del(_volume_ec->frame, EVAS_CALLBACK_RESTACK,
                                       _volume_client_evas_cb_restack);
     }
}

static void
_volume_client_unset(void)
{
   int i;

   for (i = E_POLICY_ANGLE_MAP_0; i < E_POLICY_ANGLE_MAP_NUM; i++)
     _region_objs_del(i);

   E_FREE_FUNC(_rot_handler, ecore_event_handler_del);
   E_FREE_FUNC(_volume_del_hook, e_client_hook_del);

   _volume_wl_touch = NULL;
   _volume_ec = NULL;
}

static void
_volume_hook_client_del(void *d EINA_UNUSED, E_Client *ec)
{
   if (EINA_UNLIKELY(!ec)) return;
   if (EINA_LIKELY(_volume_ec != ec)) return;

   ELOGF("VOLUME","Del Client", ec->pixmap, ec);

   _volume_client_unset();
}

static Eina_Bool
_volume_client_cb_rot_done(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client_Rotation_Change_End *e = event;
   E_Policy_Angle_Map new_idx;

   if (EINA_UNLIKELY(e == NULL))
     goto end;

   new_idx = e_policy_angle_map(_volume_ec->e.state.rot.ang.curr);
   if (EINA_UNLIKELY(new_idx == -1))
     goto end;

   if (e->ec != _volume_ec)
     goto end;

   /* is new rotation same with previous? */
   if (_volume_cur_angle_map == new_idx)
     goto end;

   /* hide region object in current rotation */
   REGION_OBJS_HIDE();

   /* update current rotation */
   _volume_cur_angle_map = new_idx;

   /* show region object in current rotation */
   REGION_OBJS_SHOW();

end:
   return ECORE_CALLBACK_RENEW;
}

EINTERN Eina_Bool
e_service_volume_client_set(E_Client *ec)
{
   if (!ec)
     {
        if (_volume_ec)
          _volume_client_unset();

        return EINA_TRUE;
     }

   if (_volume_ec)
     {
        ERR("Volume client is already registered."
            "Multi volume service is not supported.");
        return EINA_FALSE;
     }

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ELOGF("VOLUME","Set Client", ec->pixmap, ec);

   _volume_ec = ec;
   _volume_cur_angle_map = e_policy_angle_map(ec->e.state.rot.ang.curr);

   /* repeat events for volume client. */
   evas_object_repeat_events_set(ec->frame, EINA_TRUE);

   _rot_handler =
      ecore_event_handler_add(E_EVENT_CLIENT_ROTATION_CHANGE_END,
                              (Ecore_Event_Handler_Cb)_volume_client_cb_rot_done,
                              NULL);
   _volume_del_hook =
      e_client_hook_add(E_CLIENT_HOOK_DEL, _volume_hook_client_del, NULL);

   // set volume layer
   if (E_POLICY_VOLUME_LAYER != evas_object_layer_get(ec->frame))
     {
        evas_object_layer_set(ec->frame, E_POLICY_VOLUME_LAYER);
     }
   ec->layer = E_POLICY_VOLUME_LAYER;

   // set skip iconify
   ec->exp_iconify.skip_iconify = 1;

   return EINA_TRUE;
}

static Evas_Object *
_volume_content_region_obj_new(void)
{
   Evas_Object *obj;

   obj = evas_object_rectangle_add(evas_object_evas_get(_volume_ec->frame));

   /* make it transparent */
   evas_object_color_set(obj, 0, 0, 0, 0);

   /* set stack of obj object on the volume object. */
   evas_object_layer_set(obj, evas_object_layer_get(_volume_ec->frame));
   evas_object_stack_above(obj, _volume_ec->frame);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_MOVE,
                                  _volume_region_obj_cb_mouse_move, NULL);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_DOWN,
                                  _volume_region_obj_cb_mouse_down, NULL);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_UP,
                                  _volume_region_obj_cb_mouse_up, NULL);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_DOWN,
                                  _volume_region_obj_cb_multi_down, NULL);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_UP,
                                  _volume_region_obj_cb_multi_up, NULL);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_MOVE,
                                  _volume_region_obj_cb_multi_move, NULL);

   return obj;
}

static void
_region_objs_tile_set(E_Policy_Angle_Map angle_map, Eina_Tiler *tiler)
{
   Eina_List *objs_list, *l, *ll;
   Eina_Iterator *it;
   Eina_Rectangle *r, *cr;
   Evas_Object *obj;

   objs_list = _volume_region_objs[angle_map];
   it = eina_tiler_iterator_new(tiler);
   EINA_ITERATOR_FOREACH(it, r)
     {
        /* trying to reuse allocated object */
        obj = eina_list_data_get(objs_list);
        if (obj)
          {
             objs_list = eina_list_next(objs_list);
             cr = evas_object_data_get(obj, "content_rect");
             E_FREE_FUNC(cr, eina_rectangle_free);
          }
        else
          {
             obj = _volume_content_region_obj_new();
             _volume_region_objs[angle_map] = eina_list_append(_volume_region_objs[angle_map], obj);
          }

        INF("\t@@@@@ Region Set: %d %d %d %d", r->x, r->y, r->w, r->h);
        /* set geometry of region object */
        evas_object_move(obj, _volume_ec->client.x + r->x, _volume_ec->client.y + r->y);
        evas_object_resize(obj, r->w, r->h);

        /* store the value of reigon as a region object's data */
        cr = eina_rectangle_new(r->x, r->y, r->w, r->h);
        evas_object_data_set(obj, "content_rect", cr);

        if (angle_map == _volume_cur_angle_map)
          {
             if (evas_object_visible_get(_volume_ec->frame))
               evas_object_show(obj);
          }
     }
   eina_iterator_free(it);

   /* delete rest of objects after reusing */
   EINA_LIST_FOREACH_SAFE(objs_list, l, ll, obj)
     {
        _region_obj_del(obj);
        _volume_region_objs[angle_map] =
           eina_list_remove_list(_volume_region_objs[angle_map], l);
     }
}

static void
_volume_content_region_set(E_Policy_Angle_Map angle_map, Eina_Tiler *tiler)
{
   if (!tiler)
     {
        _region_objs_del(angle_map);
        return;
     }

   _region_objs_tile_set(angle_map, tiler);
}

static struct wl_resource *
_volume_wl_touch_resource_get(void)
{
   Eina_List *l;
   struct wl_client *wc;
   struct wl_resource *res;

   if (_volume_wl_touch) goto end;

   wc = wl_resource_get_client(_volume_ec->comp_data->surface);
   EINA_LIST_FOREACH(e_comp_wl->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;

        _volume_wl_touch = res;
        goto end;
     }

end:
   return _volume_wl_touch;
}

EINTERN Eina_Bool
e_service_volume_region_set(int type, int angle, Eina_Tiler *tiler)
{
   E_Policy_Angle_Map angle_map;

   if (EINA_UNLIKELY(!_volume_ec))
     {
        ERR("No registered volume client");
        return EINA_FALSE;
     }

   angle_map = e_policy_angle_map(angle);
   if (EINA_UNLIKELY(angle_map == -1))
     return EINA_FALSE;

   /* FIXME: use enum instead of constant */
   if (EINA_UNLIKELY(type != 1))
     {
        ERR("Not supported region type %d", type);
        return EINA_FALSE;
     }

   if (EINA_UNLIKELY(_volume_wl_touch_resource_get() == NULL))
     {
        ERR("Could not found wl_touch resource for volume");
        return EINA_FALSE;
     }

   ELOGF("VOLUME","Content Region Set: angle %d, tiler %p",
         NULL, NULL, angle, tiler);

   _volume_content_region_set(angle_map, tiler);

   if (!_volume_ec_ev_init)
     {
        _volume_ec_ev_init = EINA_TRUE;

        evas_object_event_callback_add(_volume_ec->frame, EVAS_CALLBACK_SHOW,
                                       _volume_client_evas_cb_show, NULL);
        evas_object_event_callback_add(_volume_ec->frame, EVAS_CALLBACK_HIDE,
                                       _volume_client_evas_cb_hide, NULL);
        evas_object_event_callback_add(_volume_ec->frame, EVAS_CALLBACK_MOVE,
                                       _volume_client_evas_cb_move, NULL);
        evas_object_event_callback_add(_volume_ec->frame, EVAS_CALLBACK_RESTACK,
                                       _volume_client_evas_cb_restack, NULL);
     }

   return EINA_TRUE;
}
