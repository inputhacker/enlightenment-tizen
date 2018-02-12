#include "e.h"
#include "e_devicemgr.h"

static int _e_devicemgr_intercept_hooks_delete = 0;
static int _e_devicemgr_intercept_hooks_walking = 0;

static Eina_Inlist *_e_devicemgr_intercept_hooks[] =
{
   [E_DEVICEMGR_INTERCEPT_HOOK_DETENT] = NULL,
};

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
