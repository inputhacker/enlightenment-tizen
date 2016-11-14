#include "e.h"
#include "services/e_service_gesture.h"

struct _E_Policy_Gesture
{
   Evas_Object *obj;
   E_Policy_Gesture_Type type;

   Eina_Bool active;
   int angle;

   struct
   {
      int x;
      int y;
      int timestamp;
      Eina_Bool pressed; /* to avoid processing that happened mouse move right after mouse up */
   } mouse_info;

   struct
   {
      E_Policy_Gesture_Start_Cb start;
      E_Policy_Gesture_Move_Cb move;
      E_Policy_Gesture_End_Cb end;
      void *data;
   } cb;
};

static void
_gesture_obj_cb_mouse_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Up *ev = event;

   gesture->mouse_info.pressed = EINA_FALSE;

   if (!gesture->active)
     return;

   gesture->active = EINA_FALSE;

   if (gesture->cb.end)
     gesture->cb.end(gesture->cb.data, obj, ev->canvas.x, ev->canvas.y, ev->timestamp);
}

static Eina_Bool
_gesture_line_check(E_Policy_Gesture *gesture, int x, int y)
{
   int dx, dy;
   const int sensitivity = 50; /* FIXME: hard coded, it sould be configurable. */

   dx = x - gesture->mouse_info.x;
   dy = y - gesture->mouse_info.y;

   if (gesture->angle == 0 || gesture->angle == 180)
     {
        if (abs(dy) < sensitivity)
          return EINA_FALSE;
     }
   else if (gesture->angle == 90 || gesture->angle == 270)
     {
        if (abs(dx) < sensitivity)
          return EINA_FALSE;
     }
   else
     {
        if ((abs(dy) < sensitivity) &&
            (abs(dx) < sensitivity))
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_gesture_flick_check(E_Policy_Gesture *gesture, Evas_Object *obj, int x, int y, unsigned int timestamp)
{
   int dy;
   int ox, oy, ow, oh;
   unsigned int dt;
   float vel = 0.0;
   const float sensitivity = 0.25; /* FIXME: hard coded, it sould be configurable. */

   evas_object_geometry_get(obj, &ox, &oy, &ow, &oh);
   if (!E_INSIDE(x, y, ox, oy, ow, oh))
     return EINA_FALSE;

   dy = y - gesture->mouse_info.y;
   dt = timestamp - gesture->mouse_info.timestamp;
   if (dt == 0)
     return EINA_FALSE;

   vel = (float)dy / (float)dt;
   if (fabs(vel) < sensitivity)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_gesture_check(E_Policy_Gesture *gesture, Evas_Object *obj, int x, int y, unsigned int timestamp)
{
   Eina_Bool ret = EINA_FALSE;

   switch (gesture->type)
     {
      case POL_GESTURE_TYPE_NONE:
         ret = EINA_TRUE;
         break;
      case POL_GESTURE_TYPE_LINE:
         ret = _gesture_line_check(gesture, x, y);
         break;
      case POL_GESTURE_TYPE_FLICK:
         ret = _gesture_flick_check(gesture, obj, x, y, timestamp);
         break;
      default:
         ERR("Unknown gesture type %d", gesture->type);
         break;
     }

   return ret;
}

static void
_gesture_obj_cb_mouse_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Move *ev = event;
   int x, y;
   unsigned int timestamp;

   if (!gesture->mouse_info.pressed)
     return;

   x = ev->cur.canvas.x;
   y = ev->cur.canvas.y;
   timestamp = ev->timestamp;

   if (!gesture->active)
     {
        gesture->active = _gesture_check(gesture, obj, x, y, timestamp);
        if (gesture->active)
          {
             if (gesture->cb.start)
               gesture->cb.start(gesture->cb.data, obj, x, y, timestamp);
          }
        return;
     }

   if (gesture->cb.move)
     gesture->cb.move(gesture->cb.data, obj, x, y, timestamp);
}

static void
_gesture_obj_cb_mouse_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Down *ev = event;

   gesture->active = EINA_FALSE;
   gesture->mouse_info.pressed = EINA_TRUE;
   gesture->mouse_info.x = ev->canvas.x;
   gesture->mouse_info.y = ev->canvas.y;
   gesture->mouse_info.timestamp = ev->timestamp;

   gesture->active = _gesture_check(gesture, obj, ev->canvas.x, ev->canvas.y, ev->timestamp);
   if (gesture->active)
     {
        if (gesture->cb.start)
          gesture->cb.start(gesture->cb.data, obj, ev->canvas.x, ev->canvas.y, ev->timestamp);
     }
}

EINTERN E_Policy_Gesture *
e_service_gesture_add(Evas_Object *obj, E_Policy_Gesture_Type type)
{
   E_Policy_Gesture *gesture;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

   gesture = E_NEW(E_Policy_Gesture, 1);
   if (EINA_UNLIKELY(gesture == NULL))
     return NULL;

   gesture->obj = obj;
   gesture->type = type;

   /* we should to repeat mouse event to below object
    * until we can make sure gesture */
   if (type != POL_GESTURE_TYPE_NONE)
     evas_object_repeat_events_set(obj, EINA_TRUE);

   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_DOWN,
                                  _gesture_obj_cb_mouse_down, gesture);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_MOVE,
                                  _gesture_obj_cb_mouse_move, gesture);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MOUSE_UP,
                                  _gesture_obj_cb_mouse_up, gesture);

   return gesture;
}

EINTERN void
e_service_gesture_del(E_Policy_Gesture *gesture)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_DOWN,
                                  _gesture_obj_cb_mouse_down);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_MOVE,
                                  _gesture_obj_cb_mouse_move);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_UP,
                                  _gesture_obj_cb_mouse_up);

   free(gesture);
}

EINTERN void
e_service_gesture_type_set(E_Policy_Gesture *gesture, E_Policy_Gesture_Type type)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   if (gesture->type == type)
     return;

   gesture->type = type;
   if (type == POL_GESTURE_TYPE_NONE)
     evas_object_repeat_events_set(gesture->obj, EINA_FALSE);
   else
     evas_object_repeat_events_set(gesture->obj, EINA_TRUE);
}

EINTERN void
e_service_gesture_cb_set(E_Policy_Gesture *gesture, E_Policy_Gesture_Start_Cb cb_start, E_Policy_Gesture_Move_Cb cb_move, E_Policy_Gesture_End_Cb cb_end, void *data)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   gesture->cb.start = cb_start;
   gesture->cb.move = cb_move;
   gesture->cb.end = cb_end;
   gesture->cb.data = data;
}

EINTERN void
e_service_gesture_angle_set(E_Policy_Gesture *gesture, int angle)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);
   gesture->angle = angle;
}
