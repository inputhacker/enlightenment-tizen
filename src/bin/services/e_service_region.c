#include "e.h"
#include "services/e_service_region.h"

/* FIXME: temporary use quickpanel to find out ui orientation */
#include "services/e_service_quickpanel.h"

#define ENTRY(...)                                 \
   E_Policy_Region *region;                             \
   EINA_SAFETY_ON_NULL_RETURN_VAL(ro, EINA_FALSE); \
   region = evas_object_data_get(ro, EO_DATA_KEY); \
   if (EINA_UNLIKELY(!region))                     \
     return __VA_ARGS__

/* FIXME: Implementation for log that can access commonly */
#ifdef INF
#undef INF
#endif

#define INF(f, x...) NULL

#define EO_DATA_KEY  "pol-region"

struct _E_Policy_Region
{
   Evas_Object        *obj;
   E_Client           *ec;
   E_Policy_Gesture   *gesture;
   Eina_List          *event_list;
   Eina_Rectangle      geom[E_POLICY_ANGLE_MAP_NUM];
   E_Policy_Angle_Map  rotation;
};

static void
_region_rotation_set(E_Policy_Region *region, int angle)
{
   if (!e_policy_angle_valid_check(angle))
     return;

   region->rotation = e_policy_angle_map(angle);
   e_service_gesture_angle_set(region->gesture, angle);
}

static void
_region_obj_geometry_update(E_Policy_Region *region)
{
   E_Policy_Angle_Map r;

   r = region->rotation;
   if ((r < E_POLICY_ANGLE_MAP_0 ) || (r >= E_POLICY_ANGLE_MAP_NUM))
     return;

   INF("Update Geometry: rotation %d x %d y %d w %d h %d",
       e_policy_angle_get(r), region->geom[r].x, region->geom[r].y, region->geom[r].w, region->geom[r].h);

   evas_object_geometry_set(region->obj,
                            region->geom[r].x, region->geom[r].y,
                            region->geom[r].w, region->geom[r].h);
}

static Eina_Bool
_region_rotation_cb_change_end(void *data, int type, void *event)
{
   E_Policy_Region *region;
   E_Event_Client *ev;
   E_Client *ec;

   region = data;
   if (EINA_UNLIKELY(!region))
     goto end;

   ev = event;
   if (EINA_UNLIKELY(!ev))
     goto end;

   ec = ev->ec;
   if (EINA_UNLIKELY(!ec))
     goto end;

   if (ec != region->ec)
     goto end;

   _region_rotation_set(region, ec->e.state.rot.ang.curr);
   _region_obj_geometry_update(region);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_region_rotation_init(E_Policy_Region *region)
{
   _region_rotation_set(region, region->ec->e.state.rot.ang.curr);

   E_LIST_HANDLER_APPEND(region->event_list, E_EVENT_CLIENT_ROTATION_CHANGE_END, _region_rotation_cb_change_end, region);

   return EINA_TRUE;
}

static void
_region_free(E_Policy_Region *region)
{
   INF("Free Instant");
   E_FREE_LIST(region->event_list, ecore_event_handler_del);
   E_FREE_FUNC(region->gesture, e_service_gesture_del);
   E_FREE_FUNC(region->obj, evas_object_del);
   E_FREE(region);
}

static void
_region_object_cb_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   E_Policy_Region *region;

   region = data;
   if (EINA_UNLIKELY(!region))
     return;

   _region_free(region);
}

EINTERN Evas_Object *
e_service_region_object_new(E_Client *ec)
{
   E_Policy_Region *region;
   Evas_Object *o;

   INF("New Instant");

   region = calloc(1, sizeof(*region));
   if (!region)
     return NULL;

   o = evas_object_rectangle_add(e_comp->evas);
   evas_object_color_set(o, 0, 0, 0, 0);
   evas_object_repeat_events_set(o, EINA_TRUE);
   region->obj = o;
   region->ec = ec;

   if (!_region_rotation_init(region))
     goto err_event;

   evas_object_data_set(o, EO_DATA_KEY, region);
   evas_object_event_callback_add(o, EVAS_CALLBACK_DEL, _region_object_cb_del, region);

   return o;
err_event:
   evas_object_del(o);
   free(region);

   return NULL;
}

EINTERN Eina_Bool
e_service_region_gesture_set(Evas_Object *ro, E_Policy_Gesture_Type type, int nfingers, E_Policy_Gesture_Start_Cb cb_start, E_Policy_Gesture_Move_Cb cb_move, E_Policy_Gesture_End_Cb cb_end, void *data)
{
   E_Policy_Gesture *gesture;

   ENTRY(EINA_FALSE);

   INF("Set Callback function");
   if (!region->gesture)
     {
        gesture = e_service_gesture_add(ro, type, nfingers);
        if (!gesture)
          return EINA_FALSE;

        region->gesture = gesture;
     }
   else
     {
        e_service_gesture_type_set(region->gesture, type);
        e_service_gesture_fingers_set(region->gesture, nfingers);
     }

   e_service_gesture_cb_set(region->gesture, cb_start, cb_move, cb_end, data);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_service_region_rectangle_set(Evas_Object *ro, E_Policy_Angle_Map ridx, int x, int y, int w, int h)
{
   ENTRY(EINA_FALSE);

   INF("Add Rectangle: a %d x %d y %d w %d h %d",
       e_policy_angle_get(ridx), x, y, w, h);

   EINA_RECTANGLE_SET(&region->geom[ridx], x, y, w, h);

   if (ridx == region->rotation)
     _region_obj_geometry_update(region);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_service_region_rectangle_get(Evas_Object *ro, E_Policy_Angle_Map ridx, int *x, int *y, int *w, int *h)
{
   ENTRY(EINA_FALSE);

   if (x) *x = region->geom[ridx].x;
   if (y) *y = region->geom[ridx].y;
   if (w) *w = region->geom[ridx].w;
   if (h) *h = region->geom[ridx].h;

   return EINA_TRUE;
}
