#include "e.h"

E_API double e_scale = 1.0;
static Eina_Bool _initted = EINA_FALSE;
static int _dpi = -1;

#define BASE_DPI 75

EINTERN int
e_scale_init(void)
{
   _initted = EINA_TRUE;

   if (_dpi == -1) e_scale_update();
   else e_scale_manual_update(_dpi);

   return 1;
}

EINTERN int
e_scale_shutdown(void)
{
   _initted = EINA_FALSE;
   _dpi = -1;
   return 1;
}

E_API void
e_scale_update(void)
{
   char buf[128];

   if (e_config->scale.use_dpi)
     {
        double profile_factor = 1.0;
        double inch = 0.0;

        if (e_config->scale.profile_factor > 0.0)
          {
             if (e_config->scale.inch_correction > 0.0)
               {
                  inch = floor(sqrt((e_comp->w * e_comp->w) + (e_comp->h * e_comp->h)) / BASE_DPI * 10 + 0.5) / 10;

                  if (inch <= e_config->scale.inch_correction_bound)
                    profile_factor = e_config->scale.profile_factor;
                  else
                    profile_factor = e_config->scale.profile_factor + e_config->scale.inch_correction;
               }
             else
               profile_factor = e_config->scale.profile_factor;
          }

        e_scale = floor((double)BASE_DPI * profile_factor / (double)e_config->scale.base_dpi * 10 + 0.5) / 10;

        if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
        else if (e_scale < e_config->scale.min)
          e_scale = e_config->scale.min;
     }
   else if (e_config->scale.use_custom)
     {
        e_scale = e_config->scale.factor;
        if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
        else if (e_scale < e_config->scale.min)
          e_scale = e_config->scale.min;
     }
   //elm_config_scale_set(e_scale);
   //elm_config_all_flush();
   edje_scale_set(e_scale);
   snprintf(buf, sizeof(buf), "%1.3f", e_scale);
   e_util_env_set("E_SCALE", buf);
   e_hints_scale_update();
}

E_API void
e_scale_manual_update(int dpi)
{
   char buf[128];
   double profile_factor = 1.0;
   double inch = 0.0;

   if (!_initted)
     {
        _dpi = dpi;
        return;
     }

   if (e_config->scale.profile_factor > 0.0)
     {
        if (e_config->scale.inch_correction > 0.0)
          {
             inch = floor(sqrt((e_comp->w * e_comp->w) + (e_comp->h * e_comp->h)) / dpi * 10 + 0.5) / 10;

             if (inch <= e_config->scale.inch_correction_bound)
               profile_factor = e_config->scale.profile_factor;
             else
               profile_factor = e_config->scale.profile_factor + e_config->scale.inch_correction;
          }
        else
          profile_factor = e_config->scale.profile_factor;
     }

   e_scale = floor((double)dpi * profile_factor / (double)e_config->scale.base_dpi * 10 + 0.5) / 10;

   if (e_scale > e_config->scale.max) e_scale = e_config->scale.max;
   else if (e_scale < e_config->scale.min)
     e_scale = e_config->scale.min;

   //elm_config_scale_set(e_scale);
   //elm_config_all_flush();
   edje_scale_set(e_scale);
   snprintf(buf, sizeof(buf), "%1.3f", e_scale);
   e_util_env_set("E_SCALE", buf);
   e_hints_scale_update();
}
