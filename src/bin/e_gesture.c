#include "e.h"
#include "e_gesture.h"

E_API E_Gesture_Info *e_gesture = NULL;

E_API int E_EVENT_GESTURE_EDGE_SWIPE;
E_API int E_EVENT_GESTURE_EDGE_DRAG;
E_API int E_EVENT_GESTURE_TAP;
E_API int E_EVENT_GESTURE_PALM_COVER;
E_API int E_EVENT_GESTURE_PAN;
E_API int E_EVENT_GESTURE_PINCH;

EINTERN void
e_gesture_init(void)
{
   e_gesture = E_NEW(E_Gesture_Info, 1);
   if (!e_gesture) return;

   E_EVENT_GESTURE_EDGE_SWIPE = ecore_event_type_new();
   E_EVENT_GESTURE_EDGE_DRAG = ecore_event_type_new();
   E_EVENT_GESTURE_TAP = ecore_event_type_new();
   E_EVENT_GESTURE_PALM_COVER = ecore_event_type_new();
   E_EVENT_GESTURE_PAN = ecore_event_type_new();
   E_EVENT_GESTURE_PINCH = ecore_event_type_new();
}

EINTERN int
e_gesture_shutdown(void)
{
   if (e_gesture)
     {
        E_FREE(e_gesture);
        E_EVENT_GESTURE_EDGE_SWIPE = 0;
        E_EVENT_GESTURE_EDGE_DRAG = 0;
        E_EVENT_GESTURE_TAP = 0;
        E_EVENT_GESTURE_PALM_COVER = 0;
        E_EVENT_GESTURE_PAN = 0;
        E_EVENT_GESTURE_PINCH = 0;
     }

   return 1;
}

E_API int
e_gesture_edge_swipe_grab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point)
{
   int ret;

   if (!e_gesture || !e_gesture->edge_swipe.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->edge_swipe.grab(fingers, edge, edge_size, start_point, end_point);
   return ret;
}

E_API int
e_gesture_edge_swipe_ungrab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point)
{
   int ret;

   if (!e_gesture || !e_gesture->edge_swipe.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->edge_swipe.ungrab(fingers, edge, edge_size, start_point, end_point);
   return ret;
}

E_API int
e_gesture_edge_drag_grab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point)
{
   int ret;

   if (!e_gesture || !e_gesture->edge_drag.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->edge_drag.grab(fingers, edge, edge_size, start_point, end_point);
   return ret;
}

E_API int
e_gesture_edge_drag_ungrab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point)
{
   int ret;

   if (!e_gesture || !e_gesture->edge_drag.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->edge_drag.ungrab(fingers, edge, edge_size, start_point, end_point);
   return ret;
}

E_API int
e_gesture_tap_grab(unsigned int fingers, unsigned int repeats)
{
   int ret;

   if (!e_gesture || !e_gesture->tap.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->tap.grab(fingers, repeats);
   return ret;
}

E_API int
e_gesture_tap_ungrab(unsigned int fingers, unsigned int repeats)
{
   int ret;

   if (!e_gesture || !e_gesture->tap.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->tap.ungrab(fingers, repeats);
   return ret;
}

E_API int
e_gesture_palm_cover_grab(void)
{
   int ret;

   if (!e_gesture || !e_gesture->palm_cover.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->palm_cover.grab();
   return ret;
}

E_API int
e_gesture_palm_cover_ungrab(void)
{
   int ret;

   if (!e_gesture || !e_gesture->palm_cover.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->palm_cover.ungrab();
   return ret;
}

E_API int
e_gesture_pan_grab(unsigned int fingers)
{
   int ret;

   if (!e_gesture || !e_gesture->pan.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->pan.grab(fingers);
   return ret;
}

E_API int
e_gesture_pan_ungrab(unsigned int fingers)
{
   int ret;

   if (!e_gesture || !e_gesture->pan.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->pan.ungrab(fingers);
   return ret;
}

E_API int
e_gesture_pinch_grab(unsigned int fingers)
{
   int ret;

   if (!e_gesture || !e_gesture->pinch.grab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->pinch.grab(fingers);
   return ret;
}

E_API int
e_gesture_pinch_ungrab(unsigned int fingers)
{
   int ret;

   if (!e_gesture || !e_gesture->pinch.ungrab)
     return E_GESTURE_ERROR_NOT_SUPPORTED;

   ret = e_gesture->pinch.ungrab(fingers);
   return ret;
}