#ifndef E_SERVICE_SOFTKEY_H
#define E_SERVICE_SOFTKEY_H

#include "e_policy_private_data.h"

typedef struct _E_Service_Softkey E_Service_Softkey;
typedef struct _E_Service_Softkey_Funcs E_Service_Softkey_Funcs;

struct _E_Service_Softkey
{
   E_Zone *zone;
   E_Client *ec;
   struct wl_resource *wl_res;
   E_Policy_Softkey_Expand expand;
   E_Policy_Softkey_Opacity opacity;
   Eina_List *intercept_hooks;
   Eina_Bool show_block;
};

struct _E_Service_Softkey_Funcs
{
   E_Service_Softkey*  (*softkey_service_add)(E_Zone *zone, E_Client *ec);
   void                (*softkey_service_del)(E_Service_Softkey *softkey);
   Eina_Bool           (*softkey_service_wl_resource_set)(E_Service_Softkey *softkey, struct wl_resource *wl_res);
   struct wl_resource* (*softkey_service_wl_resource_get)(E_Service_Softkey *softkey);
   void                (*softkey_service_client_set)(E_Client *ec);
   void                (*softkey_service_client_unset)(E_Client *ec);
   void                (*softkey_service_show)(E_Service_Softkey *softkey);
   void                (*softkey_service_hide)(E_Service_Softkey *softkey);
   void                (*softkey_service_visible_set)(E_Service_Softkey *softkey, int visible);
   int                 (*softkey_service_visible_get)(E_Service_Softkey *softkey);
   void                (*softkey_service_expand_set)(E_Service_Softkey *softkey, E_Policy_Softkey_Expand expand);
   Eina_Bool           (*softkey_service_expand_get)(E_Service_Softkey *softkey, E_Policy_Softkey_Expand *expand);
   void                (*softkey_service_opacity_set)(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity opacity);
   Eina_Bool           (*softkey_service_opacity_get)(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity *opacity);
   E_Service_Softkey*  (*softkey_service_get)(E_Zone *zone);
};


E_API Eina_Bool           e_service_softkey_module_func_set(E_Service_Softkey_Funcs *fp);
E_API Eina_Bool           e_service_softkey_module_func_unset(void);

E_API E_Service_Softkey  *e_service_softkey_add(E_Zone *zone, E_Client *ec);
E_API void                e_service_softkey_del(E_Service_Softkey *softkey);

E_API Eina_Bool           e_service_softkey_wl_resource_set(E_Service_Softkey *softkey, struct wl_resource *wl_res);
E_API struct wl_resource *e_service_softkey_wl_resource_get(E_Service_Softkey *softkey);

EINTERN void              e_service_softkey_client_set(E_Client *ec);
EINTERN void              e_service_softkey_client_unset(E_Client *ec);

EINTERN void              e_service_softkey_show(E_Service_Softkey *softkey);
EINTERN void              e_service_softkey_hide(E_Service_Softkey *softkey);

EINTERN void              e_service_softkey_visible_set(E_Service_Softkey *softkey, int visible);
EINTERN int               e_service_softkey_visible_get(E_Service_Softkey *softkey);
EINTERN void              e_service_softkey_expand_set(E_Service_Softkey *softkey, E_Policy_Softkey_Expand expand);
EINTERN Eina_Bool         e_service_softkey_expand_get(E_Service_Softkey *softkey, E_Policy_Softkey_Expand *expand);
EINTERN void              e_service_softkey_opacity_set(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity opacity);
EINTERN Eina_Bool         e_service_softkey_opacity_get(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity *opacity);

E_API E_Service_Softkey  *e_service_softkey_get(E_Zone *zone);

#endif
