#ifdef E_TYPEDEFS
#else
#ifndef E_SCREENSAVER_H
#define E_SCREENSAVER_H

EINTERN void      e_screensaver_preinit(void);
EINTERN int       e_screensaver_init(void);
EINTERN int       e_screensaver_shutdown(void);

E_API   void      e_screensaver_timeout_set(double time);
E_API   double    e_screensaver_timeout_get(void);
E_API   void      e_screensaver_notidle(void);
E_API   void      e_screensaver_enable(void);
E_API   void      e_screensaver_disable(void);
E_API   Eina_Bool e_screensaver_enabled_get(void);

E_API extern int E_EVENT_SCREENSAVER_ON;
E_API extern int E_EVENT_SCREENSAVER_OFF_PRE;
E_API extern int E_EVENT_SCREENSAVER_OFF;

#endif
#endif
