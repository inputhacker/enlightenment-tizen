#ifndef E_SERVICE_GESTURE
#define E_SERVICE_GESTURE

#include "e_policy_private_data.h"

typedef struct _E_Policy_Gesture E_Policy_Gesture;

typedef enum
{
   POL_GESTURE_TYPE_NONE,
   POL_GESTURE_TYPE_LINE,
   POL_GESTURE_TYPE_FLICK,
} E_Policy_Gesture_Type;

typedef void (*E_Policy_Gesture_Start_Cb)(void *data, Evas_Object *obj, int nfingers, int x, int y, unsigned int timestamp);
typedef void (*E_Policy_Gesture_Move_Cb)(void *data, Evas_Object *obj, int nfingers, int x, int y, unsigned int timestamp);
typedef void (*E_Policy_Gesture_End_Cb)(void *data, Evas_Object *obj, int nfingers, int x, int y, unsigned int timestamp);

E_API E_Policy_Gesture  *e_service_gesture_add(Evas_Object *obj, E_Policy_Gesture_Type type, int nfingers);
E_API void          e_service_gesture_del(E_Policy_Gesture *gesture);
E_API void          e_service_gesture_type_set(E_Policy_Gesture *gesture, E_Policy_Gesture_Type type);
E_API void          e_service_gesture_cb_set(E_Policy_Gesture *gesture, E_Policy_Gesture_Start_Cb cb_start, E_Policy_Gesture_Move_Cb cb_move, E_Policy_Gesture_End_Cb cb_end, void *data);
E_API void          e_service_gesture_angle_set(E_Policy_Gesture *gesture, int angle);
E_API void          e_service_gesture_fingers_set(E_Policy_Gesture *gesture, int nfingers);
E_API void          e_service_gesture_wait_time_set(E_Policy_Gesture *gesture, double wait_time);
E_API void          e_service_gesture_wait_dist_set(E_Policy_Gesture *gesture, int wait_dist);

#endif /* E_SERVICE_GESTURE */
