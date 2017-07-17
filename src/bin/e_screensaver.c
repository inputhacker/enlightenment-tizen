#include "e.h"

typedef struct _E_Screensaver
{
   Eina_List          *handlers;
   Ecore_Idle_Enterer *idle_before;

   double              idletime;
   Ecore_Timer        *idletimer;

   Eina_Bool           ev_update;
   Eina_Bool           active; /* indicates that screensaver window is showing */
   Eina_Bool           enable; /* indicates that screensaver feature is enabled */
} E_Screensaver;

static E_Screensaver *_saver = NULL;

E_API int E_EVENT_SCREENSAVER_ON = -1;
E_API int E_EVENT_SCREENSAVER_OFF = -1;
E_API int E_EVENT_SCREENSAVER_OFF_PRE = -1;

static Eina_Bool _e_scrsaver_cb_idletimeout(void *data EINA_UNUSED);
static void      _e_scrsaver_idletimeout_reset(void);
static Eina_Bool _e_scrsaver_cb_idle_before(void *data EINA_UNUSED);
static Eina_Bool _e_scrsaver_cb_input(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED);
static void      _e_scrsaver_active_set(Eina_Bool set);

static Eina_Bool
_e_scrsaver_cb_idletimeout(void *data EINA_UNUSED)
{
   _e_scrsaver_active_set(EINA_TRUE);
   _saver->idletimer = NULL;
   return EINA_FALSE;
}

static void
_e_scrsaver_idletimeout_reset(void)
{
   if (_saver->idletimer)
     {
        ecore_timer_del(_saver->idletimer);
        _saver->idletimer = NULL;
     }

   if (!_saver->enable) return;

   if (_saver->idletime != 0.0)
     {
        _saver->idletimer =
           ecore_timer_add(_saver->idletime,
                           _e_scrsaver_cb_idletimeout,
                           NULL);
     }
}

static Eina_Bool
_e_scrsaver_cb_idle_before(void *data EINA_UNUSED)
{
   if (!_saver) return ECORE_CALLBACK_RENEW;
   if (!_saver->ev_update) return ECORE_CALLBACK_RENEW;

   _e_scrsaver_idletimeout_reset();
   _e_scrsaver_active_set(EINA_FALSE);
   _saver->ev_update = EINA_FALSE;

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_scrsaver_cb_input(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   if (!_saver) return ECORE_CALLBACK_PASS_ON;
   _saver->ev_update = EINA_TRUE;
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_scrsaver_active_set(Eina_Bool set)
{
   int ev = -1;
   if (set == _saver->active) return;

   if (set)
     ev = E_EVENT_SCREENSAVER_ON;
   else
     ev = E_EVENT_SCREENSAVER_OFF;

   ecore_event_add(ev, NULL, NULL, NULL);

   _saver->active = set;
}

EINTERN void
e_screensaver_preinit(void)
{
   E_EVENT_SCREENSAVER_ON = ecore_event_type_new();
   E_EVENT_SCREENSAVER_OFF = ecore_event_type_new();
   E_EVENT_SCREENSAVER_OFF_PRE = ecore_event_type_new();
}

EINTERN int
e_screensaver_init(void)
{
   if (!_saver)
     {
        _saver = E_NEW(E_Screensaver, 1);
        if (!_saver) return 0;

        _saver->idletime = 0.0;
     }
   return 1;
}

EINTERN int
e_screensaver_shutdown(void)
{
   e_screensaver_disable();
   E_FREE(_saver);
   return 1;
}

E_API void
e_screensaver_timeout_set(double time)
{
   if (!_saver) return;

   _saver->idletime = time;
   _e_scrsaver_idletimeout_reset();
}

E_API double
e_screensaver_timeout_get(void)
{
   if (!_saver) return 0.0;
   return _saver->idletime;
}

E_API void
e_screensaver_notidle(void)
{
   _e_scrsaver_cb_input(NULL, 0, NULL);
}

E_API void
e_screensaver_enable(void)
{
   if (!_saver) return;
   if (_saver->enable) return;

   _saver->enable = EINA_TRUE;
   _saver->idle_before = ecore_idle_enterer_before_add(_e_scrsaver_cb_idle_before, NULL);

   E_LIST_HANDLER_APPEND(_saver->handlers, ECORE_EVENT_MOUSE_MOVE,        _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(_saver->handlers, ECORE_EVENT_MOUSE_BUTTON_DOWN, _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(_saver->handlers, ECORE_EVENT_MOUSE_BUTTON_UP,   _e_scrsaver_cb_input, NULL);
   E_LIST_HANDLER_APPEND(_saver->handlers, ECORE_EVENT_MOUSE_WHEEL,       _e_scrsaver_cb_input, NULL);

   _e_scrsaver_idletimeout_reset();
}

E_API void
e_screensaver_disable(void)
{
   if (!_saver) return;
   if (!_saver->enable) return;

   _saver->enable = EINA_FALSE;
   _e_scrsaver_active_set(EINA_FALSE);

   E_FREE_LIST(_saver->handlers, ecore_event_handler_del);
   _saver->handlers = NULL;

   if (_saver->idletimer)
     {
        ecore_timer_del(_saver->idletimer);
        _saver->idletimer = NULL;
     }

   if (_saver->idle_before)
     {
        ecore_idle_enterer_del(_saver->idle_before);
        _saver->idle_before = NULL;
     }
}

E_API Eina_Bool
e_screensaver_enabled_get(void)
{
   if (!_saver) return EINA_FALSE;

   return _saver->enable;
}
