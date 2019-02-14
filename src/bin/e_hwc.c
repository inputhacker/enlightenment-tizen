#include "e.h"
#include "services/e_service_quickpanel.h"

static int _e_hwc_intercept_hooks_delete = 0;
static int _e_hwc_intercept_hooks_walking = 0;

static Eina_Inlist *_e_hwc_intercept_hooks[] =
{
   [E_HWC_INTERCEPT_HOOK_PREPARE_PLANE] = NULL,
   [E_HWC_INTERCEPT_HOOK_END_ALL_PLANE] = NULL,
};

static void
_e_hwc_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Hwc_Intercept_Hook *ch;
   unsigned int x;

   for (x = 0; x < E_HWC_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_hwc_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_hwc_intercept_hooks[x] = eina_inlist_remove(_e_hwc_intercept_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

EINTERN Eina_Bool
e_hwc_intercept_hook_call(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc *hwc)
{
   E_Hwc_Intercept_Hook *ch;
   Eina_Bool ret = EINA_TRUE;

   _e_hwc_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_hwc_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        if (!(ch->func(ch->data, hwc)))
          {
             ret = EINA_FALSE;
             break;
          }
     }
   _e_hwc_intercept_hooks_walking--;
   if ((_e_hwc_intercept_hooks_walking == 0) && (_e_hwc_intercept_hooks_delete > 0))
     _e_hwc_intercept_hooks_clean();

   return ret;
}

E_API E_Hwc_Intercept_Hook *
e_hwc_intercept_hook_add(E_Hwc_Intercept_Hook_Point hookpoint, E_Hwc_Intercept_Hook_Cb func, const void *data)
{
   E_Hwc_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_HWC_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Hwc_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_hwc_intercept_hooks[hookpoint] = eina_inlist_append(_e_hwc_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_hwc_intercept_hook_del(E_Hwc_Intercept_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_hwc_intercept_hooks_walking == 0)
    {
        _e_hwc_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_hwc_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_hwc_intercept_hooks_delete++;
}

static void
_e_hwc_cb_ee_resize(Ecore_Evas *ee EINA_UNUSED)
{
   e_comp_canvas_update();
}

static void *
_e_hwc_tbm_surface_queue_alloc(void *data, int w, int h)
{
   E_Hwc *hwc = (E_Hwc *)data;
   E_Output *output = hwc->output;
   E_Comp_Screen *e_comp_screen = output->e_comp_screen;
   tbm_surface_queue_h tqueue = NULL;
   tdm_error error;
   int scr_w, scr_h, queue_w, queue_h;

   e_output_size_get(output, &scr_w, &scr_h);

   if (output->tdm_hwc)
     {
        tqueue = tdm_hwc_get_client_target_buffer_queue(hwc->thwc, &error);
        if (error != TDM_ERROR_NONE)
         {
            ERR("fail to tdm_hwc_get_client_target_buffer_queue");
            return (void *)NULL;
         }
     }
   else
     {
        tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!tqueue)
          {
             ERR("fail to tbm_surface_queue_create");
             return (void *)NULL;
          }
     }

   queue_w = tbm_surface_queue_get_width(tqueue);
   if (scr_w != queue_w)
     WRN("!!!!!!WARNING::: the queue width(%d) is diffrent from output width(%d)!!!!!!", queue_w, scr_w);
   queue_h = tbm_surface_queue_get_height(tqueue);
   if (scr_h != queue_h)
     WRN("!!!!!!WARNING::: the queue height(%d) is diffrent from output height(%d)!!!!!!", queue_h, scr_h);

   hwc->target_buffer_queue = tqueue;

   // TODO: change the e_comp_screen->tqueue into hwc->target_buffer_queue
   e_comp_screen->tqueue = tqueue;

   return (void *)tqueue;
}

static void
_e_hwc_tbm_surface_queue_free(void *data, void *tqueue)
{
   E_Hwc *hwc = (E_Hwc *)data;

   tbm_surface_queue_destroy(tqueue);
   hwc->target_buffer_queue = NULL;
}

static void
_e_hwc_ee_deinit(E_Hwc *hwc)
{
   // TODO:
   E_Output *output = hwc->output;
   E_Output *primary_output = NULL;

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (primary_output != output)
     {
        if (hwc->ee)
          ecore_evas_free(hwc->ee);
        hwc->ee = NULL;
     }
   else
     {
        /* ecore_evas_free execute when e_comp free */
        hwc->ee = NULL;
     }
}

// TODO: Currently E20 has only one e_output for the primary output.
//       We need to change the ee and other logic for multiple E_Output.
static Eina_Bool
_e_hwc_ee_init(E_Hwc* hwc)
{
   E_Output *output = hwc->output;
   E_Output *primary_output = NULL;
   Ecore_Evas *ee = NULL;
   int w = 0, h = 0, scr_w = 1, scr_h = 1;
   int screen_rotation;
   char buf[1024];

   INF("E_HWC: ecore evase engine init.");

   // TODO: fix me. change the screen_rotation into output_rotation.
   screen_rotation = output->e_comp_screen->rotation;

   /* set env for use tbm_surface_queue*/
   setenv("USE_EVAS_SOFTWARE_TBM_ENGINE", "1", 1);
   //setenv("USE_EVAS_GL_TBM_ENGINE", "1", 1);

   /* set gl available if we have ecore_evas support */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_DRM) ||
       ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_TBM))
     e_comp_gl_set(EINA_TRUE);

   /* get the size of the primary output */
   e_output_size_get(output, &scr_w, &scr_h);

   /* if output is disconnected, set the default width, height */
   if (scr_w == 0 || scr_h == 0)
     {
        scr_w = 2;
        scr_h = 1;

        if (!e_output_fake_config_set(output, scr_w, scr_h))
          {
             e_error_message_show(_("Fail to set the fake output config!\n"));
             _e_hwc_ee_deinit(hwc);
             return EINA_FALSE;
          }
     }

   INF("GL available:%d config engine:%d screen size:%dx%d",
       e_comp_gl_get(), e_comp_config_get()->engine, scr_w, scr_h);

   if ((e_comp_gl_get()) &&
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        e_main_ts_begin("\tEE_GL_DRM New");
        if (e_comp->avoid_afill)
          {
             ee = ecore_evas_tbm_allocfunc_new("gl_tbm_ES", scr_w, scr_h, _e_hwc_tbm_surface_queue_alloc, _e_hwc_tbm_surface_queue_free, (void *)hwc);
             INF("ecore_evas engine:gl_tbm_ES avoid_afill:%d", e_comp->avoid_afill);
          }
        else
          {
             ee = ecore_evas_tbm_allocfunc_new("gl_tbm", scr_w, scr_h, _e_hwc_tbm_surface_queue_alloc, _e_hwc_tbm_surface_queue_free, (void *)hwc);
             INF("ecore_evas engine:gl_tbm avoid_afill:%d", e_comp->avoid_afill);
          }
        snprintf(buf, sizeof(buf), "\tEE_GL_DRM New Done %p %dx%d", ee, scr_w, scr_h);
        e_main_ts_end(buf);

        if (!ee)
          e_comp_gl_set(EINA_FALSE);
        else
          {
             Evas_GL *evasgl = NULL;
             Evas_GL_API *glapi = NULL;

             e_main_ts_begin("\tEvas_GL New");
             evasgl = evas_gl_new(ecore_evas_get(ee));
             if (evasgl)
               {
                  glapi = evas_gl_api_get(evasgl);
                  if (!((glapi) && (glapi->evasglBindWaylandDisplay)))
                    {
                       e_comp_gl_set(EINA_FALSE);
                       ecore_evas_free(ee);
                       ee = NULL;
                       e_main_ts_end("\tEvas_GL New Failed 1");
                    }
                  else
                    {
                       e_main_ts_end("\tEvas_GL New Done");
                    }
               }
             else
               {
                  e_comp_gl_set(EINA_FALSE);
                  ecore_evas_free(ee);
                  ee = NULL;
                  e_main_ts_end("\tEvas_GL New Failed 2");
               }
             evas_gl_free(evasgl);
          }
     }

   /* fallback to framebuffer drm (non-accel) */
   if (!ee)
     {
        e_main_ts_begin("\tEE_DRM New");
        ee = ecore_evas_tbm_allocfunc_new("software_tbm", scr_w, scr_h, _e_hwc_tbm_surface_queue_alloc, _e_hwc_tbm_surface_queue_free, (void *)hwc);
        INF("ecore_evas engine:software_tbm fallback to software engine.");
        snprintf(buf, sizeof(buf), "\tEE_DRM New Done %p %dx%d", ee, scr_w, scr_h);
        e_main_ts_end(buf);
     }

   if (!ee)
     {
        e_error_message_show(_("Enlightenment cannot initialize outputs!\n"));
       _e_hwc_ee_deinit(hwc);
        return EINA_FALSE;
     }

   hwc->ee = ee;

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (primary_output == output)
     {
        e_comp->ee = ee;
        ecore_evas_data_set(e_comp->ee, "comp", e_comp);

        ecore_evas_callback_resize_set(e_comp->ee, _e_hwc_cb_ee_resize);

        if (screen_rotation)
          {
             /* SHOULD called with resize option after ecore_evas_resize */
             ecore_evas_rotation_with_resize_set(e_comp->ee, screen_rotation);
             ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

             snprintf(buf, sizeof(buf), "\tEE Rotate and Resize %d, %dx%d", screen_rotation, w, h);
             e_main_ts(buf);
          }
     }

   return EINA_TRUE;
}

EINTERN E_Hwc *
e_hwc_new(E_Output *output)
{
   E_Hwc *hwc = NULL;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   hwc = E_NEW(E_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);

   hwc->output = output;

   if (!output->tdm_hwc)
     {
        hwc->hwc_policy = E_HWC_POLICY_PLANES;
     }
   else
     {
        hwc->hwc_policy = E_HWC_POLICY_WINDOWS;

        hwc->thwc = tdm_output_get_hwc(output->toutput, &error);
        if (!hwc->thwc)
          {
             ERR("hwc_opt: tdm_output_get_hwc failed");
             goto fail;
          }
     }

   if (!_e_hwc_ee_init(hwc))
     {
        ERR("hwc_opt: _e_hwc_ee_init failed");
        goto fail;
     }

   /*
    * E20 has two hwc policy options.
    * 1. One is the E_HWC_POLICY_PLANES.
    *   - E20 decides the hwc policy with the E_Planes associated with the tdm_layers.
    *   - E20 manages how to set the surface(buffer) of the ec to the E_Plane.
    * 2. Another is the E_HWC_POLICY_WIDNOWS.
    *   - The tdm-backend decides the hwc policy with the E_Hwc_Windows associated with the tdm_hwc_window.
    *   - E20 asks to verify the composition types of the E_Hwc_Window of the ec.
    */
   if (hwc->hwc_policy == E_HWC_POLICY_PLANES)
     {
        if (!e_hwc_planes_init())
          {
             ERR("hwc_opt: e_hwc_windows_init failed");
             goto fail;
          }

        INF("Output uses the HWC PLANES Policy.");
     }
   else
     {
        if (!e_hwc_window_queue_init(hwc))
          {
             ERR("hwc_opt: E_Hwc_Window_Queue init failed");
             goto fail;
          }

        if (!e_hwc_window_init(hwc))
          {
             ERR("hwc_opt: E_Hwc_Window init failed");
             goto fail;
          }

        if (!e_hwc_windows_init(hwc))
          {
             ERR("hwc_opt: e_hwc_windows_init failed");
             goto fail;
          }

        /* turn on sw compositor at the start */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        INF("Output uses the HWC WINDOWS Policy.");
     }

   return hwc;

fail:
   E_FREE(hwc);

   return NULL;
}

EINTERN void
e_hwc_del(E_Hwc *hwc)
{
   if (!hwc) return;

   _e_hwc_ee_deinit(hwc);

   if (hwc->hwc_policy == E_HWC_POLICY_PLANES)
      e_hwc_planes_deinit();
   else
     {
        e_hwc_windows_deinit(hwc);
        e_hwc_window_deinit(hwc);
        e_hwc_window_queue_deinit();
     }

   E_FREE(hwc);
}

EINTERN E_Hwc_Mode
e_hwc_mode_get(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, E_HWC_MODE_NONE);

   return hwc->hwc_mode;
}

EINTERN E_Hwc_Policy
e_hwc_policy_get(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, E_HWC_MODE_NONE);

   return hwc->hwc_policy;
}

EINTERN void
e_hwc_deactive_set(E_Hwc *hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc);

   e_hwc_planes_end(hwc, __FUNCTION__);
   hwc->hwc_deactive = set;

   ELOGF("HWC", "e_hwc_deactive_set : %d", NULL, NULL, set);
}

EINTERN Eina_Bool
e_hwc_deactive_get(E_Hwc *hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   return hwc->hwc_deactive;
}

EINTERN Eina_Bool
e_hwc_client_is_above_hwc(E_Client *ec, E_Client *hwc_ec)
{
   Evas_Object *o;
   int layer, hwc_layer;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_ec, EINA_FALSE);

   if (ec == hwc_ec) return EINA_FALSE;
   if (!evas_object_visible_get(ec->frame)) return EINA_FALSE;

   layer = evas_object_layer_get(ec->frame);
   hwc_layer = evas_object_layer_get(hwc_ec->frame);

   /* compare layer */
   if (hwc_layer > layer) return EINA_FALSE;
   if (layer > hwc_layer) return EINA_TRUE;

   o = evas_object_above_get(hwc_ec->frame);
   if (evas_object_layer_get(o) == hwc_layer)
     {
        do {
           if (o == ec->frame)
             return EINA_TRUE;
           o = evas_object_above_get(o);
        } while (o && (evas_object_layer_get(o) == hwc_layer));
     }
   else
     return EINA_TRUE;

   return EINA_FALSE;
}

E_API Eina_Bool
e_client_hwc_available_properties_get(E_Client *ec, const hwc_prop **props, int *count)
{
   E_Hwc *hwc;
   E_Output *output;
   E_Zone *zone;
   E_Hwc_Window *hwc_window;
   E_Hwc_Window_State state;
   const tdm_prop *tprops;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(count, EINA_FALSE);
   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, EINA_FALSE);
   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   hwc = output->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   hwc_window = ec->hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   state = e_hwc_window_state_get(hwc_window);
   if (state == E_HWC_WINDOW_STATE_VIDEO)
     {
        if (!e_hwc_windows_get_video_available_properties(hwc, &tprops, count))
          {
             ERR("e_hwc_windows_get_video_available_properties failed");
             return EINA_FALSE;
          }
     }
   else
     {
        if (!e_hwc_windows_get_available_properties(hwc, &tprops, count))
          {
             ERR("e_hwc_windows_get_available_properties failed");
             return EINA_FALSE;
          }
     }

   *props = (hwc_prop *)tprops;

   if (state == E_HWC_WINDOW_STATE_VIDEO)
     ELOGF("HWC", ">>>>>>>> Available VIDEO props : count = %d", NULL, NULL, *count);
   else
     ELOGF("HWC", ">>>>>>>> Available UI props : count = %d", NULL, NULL, *count);
   for (i = 0; i < *count; i++)
     ELOGF("HWC", "   [%d] %s, %u", NULL, NULL, i, tprops[i].name, tprops[i].id);

   return EINA_TRUE;
}

E_API Eina_Bool
e_client_hwc_property_get(E_Client *ec, unsigned int id, hwc_value *value)
{
   E_Hwc_Window *hwc_window;
   tdm_value tvalue;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(value, EINA_FALSE);
   hwc_window = ec->hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (!e_hwc_window_get_property(hwc_window, id, &tvalue))
     {
        ERR("e_hwc_window_get_property failed");
        return EINA_FALSE;
     }

   memcpy(&value->ptr, &tvalue.ptr, sizeof(tdm_value));

   return EINA_TRUE;
}

const char *
_e_client_hwc_prop_name_get_by_id(E_Client *ec, unsigned int id)
{
   const hwc_prop *props;
   int i, count = 0;

   if (!e_client_hwc_available_properties_get(ec, &props, &count))
     {
        ERR("e_client_hwc_available_properties_get failed.");
        return EINA_FALSE;
     }

   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          return props[i].name;
     }

   ERR("No available property: id %d", id);

   return NULL;
}

E_API Eina_Bool
e_client_hwc_property_set(E_Client *ec, unsigned int id, hwc_value value)
{
   E_Hwc_Window *hwc_window;
   const char *name;
   tdm_value tvalue;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   hwc_window = ec->hwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   name = _e_client_hwc_prop_name_get_by_id(ec, id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

   memcpy(&tvalue.ptr, &value.ptr, sizeof(hwc_value));

   if (!e_hwc_window_set_property(hwc_window, id, name, tvalue, EINA_TRUE))
     {
        ERR("e_hwc_window_set_property failed");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}
