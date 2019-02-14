#ifndef E_SERVICE_REGION
#define E_SERVICE_REGION

#include "services/e_service_gesture.h"
#include "e_policy_private_data.h"

typedef struct _E_Policy_Region E_Policy_Region;

EINTERN Evas_Object        *e_service_region_object_new(E_Client *ec);
EINTERN Eina_Bool           e_service_region_rectangle_set(Evas_Object *ro, E_Policy_Angle_Map ridx, int x, int y, int w, int h);
EINTERN Eina_Bool           e_service_region_rectangle_get(Evas_Object *ro, E_Policy_Angle_Map ridx, int *x, int *y, int *w, int *h);
EINTERN Eina_Bool           e_service_region_gesture_set(Evas_Object *ro, E_Policy_Gesture_Type type, int nfingers, E_Policy_Gesture_Start_Cb cb_start, E_Policy_Gesture_Move_Cb cb_move, E_Policy_Gesture_End_Cb cb_end, void *data);

#endif
