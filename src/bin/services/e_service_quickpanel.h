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

typedef enum
{
   E_SERVICE_QUICKPANEL_TYPE_UNKNOWN        = 0x0, /* TZSH_QUICKPANEL_SERVICE_TYPE_UNKNOWN        */
   E_SERVICE_QUICKPANEL_TYPE_SYSTEM_DEFAULT = 0x1, /* TZSH_QUICKPANEL_SERVICE_TYPE_SYSTEM_DEFAULT */
   E_SERVICE_QUICKPANEL_TYPE_CONTEXT_MENU   = 0x2  /* TZSH_QUICKPANEL_SERVICE_TYPE_CONTEXT_MENU   */
} E_Service_Quickpanel_Type;

typedef enum
{
   E_QUICKPANEL_TYPE_UNKNOWN = 0,
   E_QUICKPANEL_TYPE_SYSTEM_DEFAULT = 1,
   E_QUICKPANEL_TYPE_CONTEXT_MENU = 2,
} E_Quickpanel_Type;

typedef enum
{
   E_QUICKPANEL_REGION_TYPE_HANDLER = 0,
   E_QUICKPANEL_REGION_TYPE_CONTENTS = 1,
} E_Quickpanel_Region_Type;

struct _E_QP_Mgr_Funcs
{
   void      (*quickpanel_client_add)(E_Client *ec, E_Service_Quickpanel_Type type);
   void      (*quickpanel_client_del)(E_Client *ec);
   void      (*quickpanel_show)(E_Client *ec);
   void      (*quickpanel_hide)(E_Client *ec);
   Eina_Bool (*quickpanel_region_set)(E_Client *ec, int type, int angle, Eina_Tiler *tiler);
   void      (*quickpanel_effect_type_set)(E_Client *ec, E_Service_Quickpanel_Effect_Type type);
   void      (*quickpanel_scroll_lock_set)(E_Client *ec, Eina_Bool lock);

   Eina_Bool (*qps_visible_get)(void);

   Eina_Bool (*qp_visible_get)(E_Client *ec, E_Quickpanel_Type type);
   int       (*qp_orientation_get)(E_Client *ec, E_Quickpanel_Type type);
   void      (*qp_client_add)(E_Client *ec, E_Quickpanel_Type type);
   void      (*qp_client_del)(E_Client *ec, E_Quickpanel_Type type);
   void      (*qp_client_show)(E_Client *ec, E_Quickpanel_Type type);
   void      (*qp_client_hide)(E_Client *ec, E_Quickpanel_Type type);
   Eina_Bool (*qp_client_scrollable_set)(E_Client *ec, E_Quickpanel_Type type, Eina_Bool set);
   Eina_Bool (*qp_client_scrollable_get)(E_Client *ec, E_Quickpanel_Type type);
};

E_API Eina_Bool   e_service_quickpanel_module_func_set(E_QP_Mgr_Funcs *fp);
E_API Eina_Bool   e_service_quickpanel_module_func_unset(void);
E_API Eina_List  *e_service_quickpanels_get(void);

EINTERN void      e_service_quickpanel_client_add(E_Client *ec, E_Service_Quickpanel_Type type);
EINTERN void      e_service_quickpanel_client_del(E_Client *ec);
EINTERN void      e_service_quickpanel_show(E_Client *ec);
EINTERN void      e_service_quickpanel_hide(E_Client *ec);
EINTERN Eina_Bool e_service_quickpanel_region_set(E_Client *ec, int type, int angle, Eina_Tiler *tiler);
EINTERN void      e_service_quickpanel_effect_type_set(E_Client *ec, E_Service_Quickpanel_Effect_Type type);
EINTERN void      e_service_quickpanel_scroll_lock_set(E_Client *ec, Eina_Bool lock);

/* check if at least one quickpanel is visible */
EINTERN Eina_Bool e_qps_visible_get(void);

EINTERN Eina_Bool e_qp_visible_get(E_Client *ec, E_Quickpanel_Type type);
EINTERN int       e_qp_orientation_get(E_Client *ec, E_Quickpanel_Type type);
EINTERN void      e_qp_client_add(E_Client *ec, E_Quickpanel_Type type);
EINTERN void      e_qp_client_del(E_Client *ec, E_Quickpanel_Type type);
EINTERN void      e_qp_client_show(E_Client *ec, E_Quickpanel_Type type);
EINTERN void      e_qp_client_hide(E_Client *ec, E_Quickpanel_Type type);
EINTERN Eina_Bool e_qp_client_scrollable_set(E_Client *ec, E_Quickpanel_Type type, Eina_Bool set);
EINTERN Eina_Bool e_qp_client_scrollable_get(E_Client *ec, E_Quickpanel_Type type);

#endif
