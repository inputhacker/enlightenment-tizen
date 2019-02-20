#include "e.h"
#include "services/e_service_gesture.h"

#define MAX_FINGERS 10
#define E_SERVICE_GESTURE_KEY "e_service_gesture_enabled"

typedef enum
{
   POL_GESTURE_STATUS_NONE,
   POL_GESTURE_STATUS_READY,
   POL_GESTURE_STATUS_ACTIVE,
   POL_GESTURE_STATUS_CANCEL
} E_Policy_Gesture_Status;

struct _E_Policy_Gesture
{
   Evas_Object *obj;
   E_Policy_Gesture_Type type;

   E_Policy_Gesture_Status status;

   unsigned int set_fingers;
   int pressed_fingers;
   int gesture_fingers;

   int angle;
   Ecore_Timer *waiting_timer;

   double wait_time;
   int wait_dist;

   unsigned int timestamp;
   Evas_Coord_Point c_start;

   struct
     {
        Eina_Bool pressed; /* to avoid processing that happened mouse move right after mouse up */

        Evas_Coord_Point start;
        Evas_Coord_Point cur;
     } touch_info[MAX_FINGERS];

   struct
     {
        E_Policy_Gesture_Start_Cb start;
        E_Policy_Gesture_Move_Cb move;
        E_Policy_Gesture_End_Cb end;
        void *data;
     } cb;
};

static void
_gesture_util_center_start_point_get(E_Policy_Gesture *gesture, int *cx, int *cy)
{
   int i;
   Evas_Coord_Point total_point = {0, };

   if (!gesture->pressed_fingers) return;

   for (i = 0; i < gesture->pressed_fingers; i++)
     {
        total_point.x += gesture->touch_info[i].start.x;
        total_point.y += gesture->touch_info[i].start.y;
     }

   total_point.x = (int)(total_point.x / gesture->pressed_fingers);
   total_point.y = (int)(total_point.y / gesture->pressed_fingers);

   *cx = total_point.x;
   *cy = total_point.y;
}

static void
_gesture_util_center_cur_point_get(E_Policy_Gesture *gesture, int *cx, int *cy)
{
   int i, touch_num;
   Evas_Coord_Point total_point = {0, };

   if (!gesture->pressed_fingers) return;

   touch_num = gesture->pressed_fingers;

   for (i = 0; i < touch_num; i++)
     {
        if (!gesture->touch_info[i].pressed)
          {
             touch_num++;
             continue;
          }
        total_point.x += gesture->touch_info[i].cur.x;
        total_point.y += gesture->touch_info[i].cur.y;
     }

   total_point.x = (int)(total_point.x / gesture->pressed_fingers);
   total_point.y = (int)(total_point.y / gesture->pressed_fingers);

   *cx = total_point.x;
   *cy = total_point.y;
}

static E_Policy_Gesture_Status
_gesture_line_check(E_Policy_Gesture *gesture, int x, int y, int sensitivity)
{
   int dx, dy;

   dx = x - gesture->c_start.x;
   dy = y - gesture->c_start.y;

   if (gesture->angle == 0 || gesture->angle == 180)
     {
        if (abs(dy) < sensitivity)
          return POL_GESTURE_STATUS_READY;
     }
   else if (gesture->angle == 90 || gesture->angle == 270)
     {
        if (abs(dx) < sensitivity)
          return POL_GESTURE_STATUS_READY;
     }
   else
     {
        if ((abs(dy) < sensitivity) &&
            (abs(dx) < sensitivity))
          return POL_GESTURE_STATUS_READY;
     }

   return POL_GESTURE_STATUS_ACTIVE;
}

static E_Policy_Gesture_Status
_gesture_flick_check(E_Policy_Gesture *gesture, Evas_Object *obj, int x, int y, unsigned int timestamp)
{
   int dy;
   int ox, oy, ow, oh;
   unsigned int dt;
   float vel = 0.0;
   const float sensitivity = 0.25; /* FIXME: hard coded, it sould be configurable. */

   evas_object_geometry_get(obj, &ox, &oy, &ow, &oh);
   if (!E_INSIDE(x, y, ox, oy, ow, oh))
     return POL_GESTURE_STATUS_READY;

   dy = y - gesture->c_start.y;
   dt = timestamp - gesture->timestamp;
   if (dt == 0)
     return POL_GESTURE_STATUS_READY;

   vel = (float)dy / (float)dt;
   if (fabs(vel) < sensitivity)
     return POL_GESTURE_STATUS_READY;

   return POL_GESTURE_STATUS_ACTIVE;
}

static E_Policy_Gesture_Status
_gesture_check(E_Policy_Gesture *gesture, Evas_Object *obj, int x, int y, unsigned int timestamp)
{
   E_Policy_Gesture_Status ret = POL_GESTURE_STATUS_READY;

   if (gesture->waiting_timer)
     {
        // All waiting fingers are not pressed. So waiting other fingers
        return ret;
     }

   switch (gesture->type)
     {
      case POL_GESTURE_TYPE_NONE:
         ret = POL_GESTURE_STATUS_ACTIVE;
         break;
      case POL_GESTURE_TYPE_LINE:
         /* FIXME: sensitivity is hard coded, it sould be configurable. */
         ret = _gesture_line_check(gesture, x, y, 50);
         break;
      case POL_GESTURE_TYPE_FLICK:
         ret = _gesture_flick_check(gesture, obj, x, y, timestamp);
         break;
      default:
         WRN("Unknown gesture type %d", gesture->type);
         break;
     }

   if ((ret == POL_GESTURE_STATUS_ACTIVE) &&
       !gesture->gesture_fingers)
     {
        gesture->gesture_fingers = gesture->pressed_fingers;
     }

   return ret;
}

static void
_gesture_cleanup(E_Policy_Gesture *gesture)
{
   gesture->pressed_fingers = 0;
   gesture->gesture_fingers = 0;
   gesture->status = POL_GESTURE_STATUS_READY;
   if (gesture->waiting_timer)
     {
        ecore_timer_del(gesture->waiting_timer);
        gesture->waiting_timer = NULL;
     }
}

static void
_gesture_cancel(E_Policy_Gesture *gesture)
{
   gesture->status = POL_GESTURE_STATUS_CANCEL;
}

static void
_gesture_start(E_Policy_Gesture *gesture)
{
   if (gesture->set_fingers & (1 << gesture->pressed_fingers))
     {
        if (gesture->set_fingers < (1 << (gesture->pressed_fingers + 1)))
          {
             // All of waiting fingers are come. Stop waiting other fingers
             if (gesture->waiting_timer)
               {
                  ecore_timer_del(gesture->waiting_timer);
                  gesture->waiting_timer = NULL;
               }
          }
     }
}

static Eina_Bool
_gesture_waiting_timer(void *data)
{
   E_Policy_Gesture *gesture = data;
   unsigned int timestamp;

   ecore_timer_del(gesture->waiting_timer);
   gesture->waiting_timer = NULL;

   if (gesture->set_fingers & (1 << gesture->pressed_fingers))
     {
        timestamp = (int)(ecore_time_get() * 1000);
        gesture->status = _gesture_check(gesture, gesture->obj, gesture->c_start.x, gesture->c_start.y, timestamp);
        if (gesture->status == POL_GESTURE_STATUS_ACTIVE)
          {
             if (gesture->cb.start)
               {
                  gesture->cb.start(gesture->cb.data, gesture->obj, gesture->gesture_fingers, gesture->c_start.x, gesture->c_start.y, timestamp);
               }
          }
        gesture->gesture_fingers = gesture->pressed_fingers;
     }
   else
     {
        _gesture_cancel(gesture);
     }

   return ECORE_CALLBACK_CANCEL;
}

static void
_gesture_touch_up(E_Policy_Gesture *gesture, Evas_Object *obj, int idx, int x, int y, int timestamp)
{
   int cx = 0, cy = 0;

   switch (gesture->status)
     {
        case POL_GESTURE_STATUS_READY:
          gesture->touch_info[idx].pressed = EINA_FALSE;
          _gesture_cancel(gesture);
          break;

        case POL_GESTURE_STATUS_ACTIVE:
          gesture->touch_info[idx].pressed = EINA_FALSE;
          gesture->touch_info[idx].cur.x = x;
          gesture->touch_info[idx].cur.y = y;
          _gesture_util_center_cur_point_get(gesture, &cx, &cy);

          if (gesture->pressed_fingers <= 0)
            {
               if (gesture->cb.end)
                 {
                    gesture->cb.end(gesture->cb.data, obj, gesture->gesture_fingers, cx, cy, timestamp);
                 }
            }
          break;

        case POL_GESTURE_STATUS_CANCEL:
          break;

        default:
          break;
     }

   if (gesture->pressed_fingers <= 0)
     {
        _gesture_cleanup(gesture);
     }
}

static void
_gesture_touch_move(E_Policy_Gesture *gesture, Evas_Object *obj, int idx, int x, int y, int timestamp)
{
   int cx = 0, cy = 0;

   switch (gesture->status)
     {
        case POL_GESTURE_STATUS_READY:
          gesture->touch_info[idx].cur.x = x;
          gesture->touch_info[idx].cur.y = y;

          _gesture_util_center_cur_point_get(gesture, &cx, &cy);
          if (gesture->waiting_timer)
            {
               if (_gesture_line_check(gesture, cx, cy, gesture->wait_dist) == POL_GESTURE_STATUS_ACTIVE)
                 {
                    ecore_timer_del(gesture->waiting_timer);
                    gesture->waiting_timer = NULL;
                    if (!(gesture->set_fingers & (1 << gesture->pressed_fingers)))
                      {
                         _gesture_cancel(gesture);
                         return;
                      }
                 }
            }

          gesture->status = _gesture_check(gesture, obj, cx, cy, timestamp);
          if (gesture->status == POL_GESTURE_STATUS_ACTIVE)
            {
               if (gesture->cb.start)
                 {
                    gesture->cb.start(gesture->cb.data, obj, gesture->gesture_fingers, gesture->c_start.x, gesture->c_start.y, timestamp);
                 }
            }
          else
            return;
          break;

        case POL_GESTURE_STATUS_ACTIVE:
          gesture->touch_info[idx].cur.x = x;
          gesture->touch_info[idx].cur.y = y;

          _gesture_util_center_cur_point_get(gesture, &cx, &cy);
          break;

        case POL_GESTURE_STATUS_CANCEL:
          return;

        default:
          return;
     }

   if (gesture->cb.move)
     gesture->cb.move(gesture->cb.data, obj, gesture->gesture_fingers, cx, cy, timestamp);
}

static void
_gesture_touch_down(E_Policy_Gesture *gesture, Evas_Object *obj, int idx, Evas_Coord x, Evas_Coord y, unsigned int timestamp)
{
   int cx = 0, cy = 0;

   switch (gesture->status)
     {
        case POL_GESTURE_STATUS_READY:
          gesture->touch_info[idx].pressed = EINA_TRUE;
          gesture->touch_info[idx].cur.x = gesture->touch_info[idx].start.x = x;
          gesture->touch_info[idx].cur.y = gesture->touch_info[idx].start.y = y;

          if (gesture->set_fingers < (1 << (idx + 1)))
            {
               // too many fingers are touched
               _gesture_cancel(gesture);
               break;
            }

          if (gesture->gesture_fingers)
            {
               // already gesture is begin(or definited), so ignore meaningless touch.
               break;
            }

          gesture->timestamp = timestamp;

          if (gesture->pressed_fingers == 1)
            {
               gesture->c_start.x = x;
               gesture->c_start.y = y;
               if (gesture->set_fingers > 2)
                 gesture->waiting_timer = ecore_timer_add(gesture->wait_time, _gesture_waiting_timer, (void *)gesture);
            }
          else
            {
               _gesture_util_center_start_point_get(gesture, &cx, &cy);
               gesture->c_start.x = cx;
               gesture->c_start.y = cy;
            }

          _gesture_start(gesture);

          gesture->status = _gesture_check(gesture, obj, x, y, timestamp);
          if (gesture->status == POL_GESTURE_STATUS_ACTIVE)
            {
               if (gesture->cb.start)
                 gesture->cb.start(gesture->cb.data, obj, gesture->gesture_fingers, gesture->c_start.x, gesture->c_start.y, timestamp);
            }
          break;

        case POL_GESTURE_STATUS_ACTIVE:
          gesture->touch_info[idx].pressed = EINA_TRUE;
          break;

        case POL_GESTURE_STATUS_CANCEL:
          break;

        default:
          break;
     }
}

static void
_gesture_obj_cb_mouse_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Up *ev = event;

   gesture->pressed_fingers--;
   _gesture_touch_up(gesture, obj, 0, ev->canvas.x, ev->canvas.y, ev->timestamp);
}

static void
_gesture_obj_cb_mouse_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Move *ev = event;

   if (!gesture->touch_info[0].pressed)
     return;

   _gesture_touch_move(gesture, obj, 0, ev->cur.canvas.x, ev->cur.canvas.y, ev->timestamp);
}

static void
_gesture_obj_cb_mouse_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Mouse_Down *ev = event;

   gesture->pressed_fingers++;
   _gesture_touch_down(data, obj, 0, ev->canvas.x, ev->canvas.y, ev->timestamp);
}

static void
_gesture_obj_cb_multi_up(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Multi_Up *ev = event;

   if (gesture->gesture_fingers &&
       (gesture->gesture_fingers < (ev->device + 1)))
     {
        // already gesture is begin(or definited), so ignore meaningless touch
        return;
     }

   if (!gesture->touch_info[ev->device].pressed)
     {
        return;
     }

   gesture->pressed_fingers--;
   _gesture_touch_up(gesture, obj, ev->device, ev->canvas.x, ev->canvas.y, ev->timestamp);
}

static void
_gesture_obj_cb_multi_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Multi_Move *ev = event;

   if (!gesture->touch_info[ev->device].pressed)
     return;

   _gesture_touch_move(gesture, obj, ev->device, ev->cur.canvas.x, ev->cur.canvas.y, ev->timestamp);
}

static void
_gesture_obj_cb_multi_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Gesture *gesture = data;
   Evas_Event_Multi_Down *ev = event;

   if (gesture->gesture_fingers &&
       (gesture->gesture_fingers < (ev->device + 1)))
     {
        // already gesture is begin(or definited), so ignore meaningless touch
        return;
     }

   gesture->pressed_fingers++;

   _gesture_touch_down(data, obj, ev->device, ev->canvas.x, ev->canvas.y, ev->timestamp);
}

E_API E_Policy_Gesture *
e_service_gesture_add(Evas_Object *obj, E_Policy_Gesture_Type type, int nfingers)
{
   E_Policy_Gesture *gesture;

   EINA_SAFETY_ON_NULL_RETURN_VAL(obj, NULL);

   if (evas_object_data_get(obj, E_SERVICE_GESTURE_KEY))
     {
        WRN("obj(%p) is already added gesture.\n", obj);
        return NULL;
     }

   gesture = E_NEW(E_Policy_Gesture, 1);
   if (EINA_UNLIKELY(gesture == NULL))
     return NULL;

   gesture->obj = obj;
   gesture->type = type;
   gesture->set_fingers |= 1 << nfingers;

   // attempt to get wait_time and wait_dist to config file.
   // but if failed, set default VALUE
   if (e_config)
     {
        if (e_config->gesture_service.wait_time)
          gesture->wait_time = e_config->gesture_service.wait_time;
        if (e_config->gesture_service.wait_dist)
          gesture->wait_dist = e_config->gesture_service.wait_dist;
     }
   if (!gesture->wait_time)
     gesture->wait_time = 0.1;
   if (!gesture->wait_dist)
     gesture->wait_dist = 50;
   gesture->status = POL_GESTURE_STATUS_READY;

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
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_DOWN,
                                  _gesture_obj_cb_multi_down, gesture);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_MOVE,
                                  _gesture_obj_cb_multi_move, gesture);
   evas_object_event_callback_add(obj, EVAS_CALLBACK_MULTI_UP,
                                  _gesture_obj_cb_multi_up, gesture);

   evas_object_data_set(obj, E_SERVICE_GESTURE_KEY, (void *)1);

   return gesture;
}

E_API void
e_service_gesture_del(E_Policy_Gesture *gesture)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_DOWN,
                                  _gesture_obj_cb_mouse_down);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_MOVE,
                                  _gesture_obj_cb_mouse_move);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MOUSE_UP,
                                  _gesture_obj_cb_mouse_up);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MULTI_DOWN,
                                  _gesture_obj_cb_multi_down);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MULTI_MOVE,
                                  _gesture_obj_cb_multi_move);
   evas_object_event_callback_del(gesture->obj, EVAS_CALLBACK_MULTI_UP,
                                  _gesture_obj_cb_multi_up);

   evas_object_data_del(gesture->obj, E_SERVICE_GESTURE_KEY);

   free(gesture);
}

E_API void
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

E_API void
e_service_gesture_cb_set(E_Policy_Gesture *gesture, E_Policy_Gesture_Start_Cb cb_start, E_Policy_Gesture_Move_Cb cb_move, E_Policy_Gesture_End_Cb cb_end, void *data)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   gesture->cb.start = cb_start;
   gesture->cb.move = cb_move;
   gesture->cb.end = cb_end;
   gesture->cb.data = data;
}

E_API void
e_service_gesture_angle_set(E_Policy_Gesture *gesture, int angle)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);
   gesture->angle = angle;
}

E_API void
e_service_gesture_fingers_set(E_Policy_Gesture *gesture, int nfingers)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);

   if (gesture->set_fingers & (1 << nfingers))
     return;
   gesture->set_fingers |= 1 << nfingers;
}

E_API void
e_service_gesture_wait_time_set(E_Policy_Gesture *gesture, double wait_time)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);
   gesture->wait_time = wait_time;
}

E_API void
e_service_gesture_wait_dist_set(E_Policy_Gesture *gesture, int wait_dist)
{
   EINA_SAFETY_ON_NULL_RETURN(gesture);
   gesture->wait_dist = wait_dist;
}

