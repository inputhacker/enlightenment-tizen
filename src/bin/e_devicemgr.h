#ifdef E_TYPEDEFS

typedef enum _E_Devicemgr_Intercept_Hook_Point
{
   E_DEVICEMGR_INTERCEPT_HOOK_DETENT,
   E_DEVICEMGR_INTERCEPT_HOOK_LAST
} E_Devicemgr_Intercept_Hook_Point;

typedef Eina_Bool (*E_Devicemgr_Intercept_Hook_Cb) (void *data, int point, void *event);
typedef struct _E_Devicemgr_Intercept_Hook E_Devicemgr_Intercept_Hook;

#else
#ifndef E_DEVICEMGR_H
#define E_DEVICEMGR_H

struct _E_Devicemgr_Intercept_Hook
{
   EINA_INLIST;
   E_Devicemgr_Intercept_Hook_Point hookpoint;
   E_Devicemgr_Intercept_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

E_API E_Devicemgr_Intercept_Hook *e_devicemgr_intercept_hook_add(E_Devicemgr_Intercept_Hook_Point hookpoint, E_Devicemgr_Intercept_Hook_Cb func, const void *data);
E_API void e_devicemgr_intercept_hook_del(E_Devicemgr_Intercept_Hook *ch);
E_API Eina_Bool e_devicemgr_intercept_hook_call(E_Devicemgr_Intercept_Hook_Point hookpoint, void *event);

#endif
#endif

