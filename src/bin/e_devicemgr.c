//#include "e.h"
#include "e_devicemgr_private.h"

E_API E_Devicemgr *e_devicemgr;

static int _e_devicemgr_intercept_hooks_delete = 0;
static int _e_devicemgr_intercept_hooks_walking = 0;

static Eina_Inlist *_e_devicemgr_intercept_hooks[] =
{
   [E_DEVICEMGR_INTERCEPT_HOOK_DETENT] = NULL,
};

int _devicemgr_log_dom = -1;

E_API E_Devicemgr_Intercept_Hook *
e_devicemgr_intercept_hook_add(E_Devicemgr_Intercept_Hook_Point hookpoint, E_Devicemgr_Intercept_Hook_Cb func, const void *data)
{
   E_Devicemgr_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint < 0 || hookpoint >= E_DEVICEMGR_INTERCEPT_HOOK_LAST,
                                  EINA_FALSE);

   ch = E_NEW(E_Devicemgr_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_devicemgr_intercept_hooks[hookpoint] = eina_inlist_append(_e_devicemgr_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_devicemgr_intercept_hook_del(E_Devicemgr_Intercept_Hook *ch)
{
   EINA_SAFETY_ON_NULL_RETURN(ch);

   ch->delete_me = 1;
   if (_e_devicemgr_intercept_hooks_walking == 0)
     {
        _e_devicemgr_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_devicemgr_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_devicemgr_intercept_hooks_delete++;
}

static void
_e_devicemgr_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Devicemgr_Intercept_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_DEVICEMGR_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_devicemgr_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_devicemgr_intercept_hooks[x] = eina_inlist_remove(_e_devicemgr_intercept_hooks[x], EINA_INLIST_GET(ch));
         free(ch);
       }
}

E_API Eina_Bool
e_devicemgr_intercept_hook_call(E_Devicemgr_Intercept_Hook_Point hookpoint, void *event)
{
   E_Devicemgr_Intercept_Hook *ch;
   Eina_Bool res = EINA_TRUE, ret = EINA_TRUE;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint < 0 || hookpoint >= E_DEVICEMGR_INTERCEPT_HOOK_LAST,
                                  EINA_FALSE);

   _e_devicemgr_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_devicemgr_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        res = ch->func(ch->data, hookpoint, event);
        if (!res) ret = EINA_FALSE;
     }
   _e_devicemgr_intercept_hooks_walking--;
   if ((_e_devicemgr_intercept_hooks_walking == 0) && (_e_devicemgr_intercept_hooks_delete > 0))
     _e_devicemgr_intercept_hooks_clean();

   return ret;
}

E_API Eina_Bool
e_devicemgr_is_blocking_event(Ecore_Device_Class clas)
{
   unsigned int dev_clas = 0x0;

   switch (clas)
     {
        case ECORE_DEVICE_CLASS_KEYBOARD:
          dev_clas = TIZEN_INPUT_DEVICE_MANAGER_CLAS_KEYBOARD;
          break;
        case ECORE_DEVICE_CLASS_MOUSE:
          dev_clas = TIZEN_INPUT_DEVICE_MANAGER_CLAS_MOUSE;
          break;
        case ECORE_DEVICE_CLASS_TOUCH:
          dev_clas = TIZEN_INPUT_DEVICE_MANAGER_CLAS_TOUCHSCREEN;
          break;
        default:
          return EINA_FALSE;
     }

   if (e_devicemgr->block.devtype & dev_clas)
     return EINA_TRUE;

   return EINA_FALSE;
}

E_API int
e_devicemgr_init(void)
{
   E_Devicemgr_Config_Data *dconfig = NULL;
   Eina_Bool res = EINA_FALSE;

   EINA_SAFETY_ON_NULL_GOTO(e_comp, failed);

   _devicemgr_log_dom = eina_log_domain_register("e-devicemgr", EINA_COLOR_BLUE);
   EINA_SAFETY_ON_FALSE_GOTO(_devicemgr_log_dom >= 0, failed);
   eina_log_domain_level_set("e-devicemgr", EINA_LOG_LEVEL_INFO);

   e_devicemgr = E_NEW(E_Devicemgr, 1);
   EINA_SAFETY_ON_NULL_GOTO(e_devicemgr, failed);

   dconfig = E_NEW(E_Devicemgr_Config_Data, 1);
   EINA_SAFETY_ON_NULL_GOTO(dconfig, failed);

   e_devicemgr_conf_init(dconfig);
   e_devicemgr->dconfig = dconfig;

   res = e_devicemgr_wl_init();
   EINA_SAFETY_ON_FALSE_GOTO(res, wl_failed);

   res = e_devicemgr_input_init();
   EINA_SAFETY_ON_FALSE_GOTO(res, input_failed);

   return EINA_TRUE;

input_failed:
   e_devicemgr_input_shutdown();

wl_failed:
   e_devicemgr_wl_shutdown();

failed:
   if (e_devicemgr)
     {

        if (e_devicemgr->dconfig)
          {
             e_devicemgr_conf_fini(e_devicemgr->dconfig);
             E_FREE(e_devicemgr->dconfig);
             e_devicemgr->dconfig = NULL;
          }

        E_FREE(e_devicemgr);
        e_devicemgr = NULL;
     }
   eina_log_domain_unregister(_devicemgr_log_dom);
   _devicemgr_log_dom = -1;
   return EINA_FALSE;
}

E_API int
e_devicemgr_shutdown(void)
{
   eina_log_domain_unregister(_devicemgr_log_dom);
   _devicemgr_log_dom = -1;
   if (e_devicemgr->dconfig)
     {
        e_devicemgr_conf_fini(e_devicemgr->dconfig);
        E_FREE(e_devicemgr->dconfig);
        e_devicemgr->dconfig = NULL;
     }
   E_FREE(e_devicemgr);
   return EINA_TRUE;
}

