#include "e.h"
#include "e_keyrouter.h"

E_API int E_KEYROUTER_EVENT_KEY;

static int _e_keyrouter_intercept_hooks_delete = 0;
static int _e_keyrouter_intercept_hooks_walking = 0;

static Eina_Inlist *_e_keyrouter_intercept_hooks[] =
{
   [E_KEYROUTER_INTERCEPT_HOOK_BEFORE_KEYROUTING] = NULL,
   [E_KEYROUTER_INTERCEPT_HOOK_DELIVER_FOCUS] = NULL,
};

E_API E_Keyrouter_Info e_keyrouter;

E_API E_Keyrouter_Intercept_Hook *
e_keyrouter_intercept_hook_add(E_Keyrouter_Intercept_Hook_Point hookpoint, E_Keyrouter_Intercept_Hook_Cb func, const void *data)
{
   E_Keyrouter_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_KEYROUTER_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Keyrouter_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_keyrouter_intercept_hooks[hookpoint] = eina_inlist_append(_e_keyrouter_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_keyrouter_intercept_hook_del(E_Keyrouter_Intercept_Hook *ch)
{
   EINA_SAFETY_ON_NULL_RETURN(ch);

   ch->delete_me = 1;
   if (_e_keyrouter_intercept_hooks_walking == 0)
     {
        _e_keyrouter_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_keyrouter_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_keyrouter_intercept_hooks_delete++;
}

static void
_e_keyrouter_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Keyrouter_Intercept_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_KEYROUTER_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_keyrouter_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_keyrouter_intercept_hooks[x] = eina_inlist_remove(_e_keyrouter_intercept_hooks[x], EINA_INLIST_GET(ch));
         free(ch);
       }
}

E_API Eina_Bool
e_keyrouter_intercept_hook_call(E_Keyrouter_Intercept_Hook_Point hookpoint, int type, Ecore_Event_Key *event)
{
   E_Keyrouter_Intercept_Hook *ch;
   Eina_Bool res = EINA_TRUE;

   _e_keyrouter_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_keyrouter_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        res = ch->func(ch->data, type, event);
     }
   _e_keyrouter_intercept_hooks_walking--;
   if ((_e_keyrouter_intercept_hooks_walking == 0) && (_e_keyrouter_intercept_hooks_delete > 0))
     _e_keyrouter_intercept_hooks_clean();

   return res;
}

E_API void
e_keyrouter_send_event_surface(struct wl_resource *surface, int key, int mode)
{
   EINA_SAFETY_ON_NULL_RETURN(e_keyrouter.event_surface_send);
   EINA_SAFETY_ON_NULL_RETURN(surface);

   e_keyrouter.event_surface_send(surface, key, mode);
}

EINTERN void
e_keyrouter_init(void)
{
   E_KEYROUTER_EVENT_KEY = ecore_event_type_new();
}

EINTERN int
e_keyrouter_shutdown(void)
{
   E_KEYROUTER_EVENT_KEY = 0;

   return 1;
}
