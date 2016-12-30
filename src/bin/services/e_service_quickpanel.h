#ifndef E_SERVICE_QUICKPANEL_H
#define E_SERVICE_QUICKPANEL_H

#include "e_policy_private_data.h"
#include <tzsh_server.h>

typedef struct _E_QP_Mgr_Funcs       E_QP_Mgr_Funcs;

typedef enum
{
   E_SERVICE_QUICKPANEL_EFFECT_TYPE_SWIPE = TZSH_QUICKPANEL_SERVICE_EFFECT_TYPE_SWIPE,
   E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE = TZSH_QUICKPANEL_SERVICE_EFFECT_TYPE_MOVE,
   E_SERVICE_QUICKPANEL_EFFECT_TYPE_APP_CUSTOM = TZSH_QUICKPANEL_SERVICE_EFFECT_TYPE_APP_CUSTOM,
} E_Service_Quickpanel_Effect_Type;

struct _E_QP_Mgr_Funcs
{
   void          (*quickpanel_client_set)(E_Client *ec);
   E_Client*     (*quickpanel_client_get)(void);
   void          (*quickpanel_show)(void);
   void          (*quickpanel_hide)(void);
   Eina_Bool     (*quickpanel_region_set)(int type, int angle, Eina_Tiler *tiler);
   Evas_Object*  (*quickpanel_handler_object_add)(E_Client *ec, int x, int y, int w, int h);
   void          (*quickpanel_handler_object_del)(Evas_Object *handler);
   void          (*quickpanel_effect_type_set)(E_Client *ec, E_Service_Quickpanel_Effect_Type type);

   Eina_Bool    (*qp_visible_get)(void);
   int          (*qp_orientation_get)(void);

   void         (*qp_client_add)(E_Client *ec);
   void         (*qp_client_del)(E_Client *ec);
   void         (*qp_client_show)(E_Client *ec);
   void         (*qp_client_hide)(E_Client *ec);
   Eina_Bool    (*qp_client_scrollable_set)(E_Client *ec, Eina_Bool set);
   Eina_Bool    (*qp_client_scrollable_get)(E_Client *ec);
};

E_API Eina_Bool       e_service_quickpanel_module_func_set(E_QP_Mgr_Funcs *fp);
E_API Eina_Bool       e_service_quickpanel_module_func_unset(void);

EINTERN void          e_service_quickpanel_client_set(E_Client *ec);
E_API E_Client       *e_service_quickpanel_client_get(void);
EINTERN void          e_service_quickpanel_show(void);
EINTERN void          e_service_quickpanel_hide(void);
EINTERN Eina_Bool     e_service_quickpanel_region_set(int type, int angle, Eina_Tiler *tiler);
EINTERN Evas_Object  *e_service_quickpanel_handler_object_add(E_Client *ec, int x, int y, int w, int h);
EINTERN void          e_service_quickpanel_handler_object_del(Evas_Object *handler);
EINTERN void          e_service_quickpanel_effect_type_set(E_Client *ec, E_Service_Quickpanel_Effect_Type type);

EINTERN Eina_Bool    e_qp_visible_get(void);
EINTERN int          e_qp_orientation_get(void);

EINTERN void         e_qp_client_add(E_Client *ec);
EINTERN void         e_qp_client_del(E_Client *ec);
EINTERN void         e_qp_client_show(E_Client *ec);
EINTERN void         e_qp_client_hide(E_Client *ec);
EINTERN Eina_Bool    e_qp_client_scrollable_set(E_Client *ec, Eina_Bool set);
EINTERN Eina_Bool    e_qp_client_scrollable_get(E_Client *ec);

#endif
