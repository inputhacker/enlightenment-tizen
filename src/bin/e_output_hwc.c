#include "e.h"
#include "services/e_service_quickpanel.h"

static void
_e_output_hwc_cb_ee_resize(Ecore_Evas *ee EINA_UNUSED)
{
   e_comp_canvas_update();
}

static void *
_e_output_hwc_tbm_surface_queue_alloc(void *data, int w, int h)
{
   E_Output_Hwc *output_hwc = (E_Output_Hwc *)data;
   E_Output *output = output_hwc->output;
   E_Comp_Screen *e_comp_screen = output->e_comp_screen;
   tdm_output *toutput = output->toutput;
   tbm_surface_queue_h tqueue = NULL;
   tdm_error error;
   int scr_w, scr_h, queue_w, queue_h;

   e_output_size_get(output, &scr_w, &scr_h);

   if (output->tdm_hwc)
     {
        tqueue = tdm_output_hwc_get_target_buffer_queue(toutput, &error);
        if (error != TDM_ERROR_NONE)
         {
            ERR("fail to tdm_output_hwc_get_target_buffer_queue");
            return (void *)NULL;
         }
     }
   else
     {
        tqueue = tbm_surface_queue_create(3, w, h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
        if (!tqueue)
          {
             ERR("fail to tdm_output_hwc_get_target_buffer_queue");
             return (void *)NULL;
          }
     }

   queue_w = tbm_surface_queue_get_width(tqueue);
   if (scr_w != queue_w)
     WRN("!!!!!!WARNING::: the queue width(%d) is diffrent from output width(%d)!!!!!!", queue_w, scr_w);
   queue_h = tbm_surface_queue_get_height(tqueue);
   if (scr_h != queue_h)
     WRN("!!!!!!WARNING::: the queue height(%d) is diffrent from output height(%d)!!!!!!", queue_h, scr_h);

   output_hwc->target_buffer_queue = tqueue;

   // TODO: change the e_comp_screen->tqueue into output_hwc->target_buffer_queue
   e_comp_screen->tqueue = tqueue;

   return (void *)tqueue;
}

static void
_e_output_hwc_tbm_surface_queue_free(void *data, void *tqueue)
{
   E_Output_Hwc *output_hwc = (E_Output_Hwc *)data;

   tbm_surface_queue_destroy(tqueue);
   output_hwc->target_buffer_queue = NULL;
}

static void
_e_output_hwc_ee_deinit(E_Output_Hwc *output_hwc)
{
   // TODO:
   E_Output *output = output_hwc->output;
   E_Output *primary_output = NULL;

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (primary_output != output)
     {
        if (output_hwc->ee)
          ecore_evas_free(output_hwc->ee);
        output_hwc->ee = NULL;
     }
   else
     {
        /* ecore_evas_free execute when e_comp free */
        output_hwc->ee = NULL;
     }
}

// TODO: Currently E20 has only one e_output for the primary output.
//       We need to change the ee and other logic for multiple E_Output.
static Eina_Bool
_e_output_hwc_ee_init(E_Output_Hwc* output_hwc)
{
   E_Output *output = output_hwc->output;
   E_Output *primary_output = NULL;
   Ecore_Evas *ee = NULL;
   int w = 0, h = 0, scr_w = 1, scr_h = 1;
   int screen_rotation;
   char buf[1024];

   INF("E_OUTPUT_HWC: ecore evase engine init.");

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
             _e_output_hwc_ee_deinit(output_hwc);
             return EINA_FALSE;
          }
     }

   INF("GL available:%d config engine:%d screen size:%dx%d",
       e_comp_gl_get(), e_comp_config_get()->engine, scr_w, scr_h);

   if ((e_comp_gl_get()) &&
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        e_main_ts_begin("\tEE_GL_DRM New");
        ee = ecore_evas_tbm_allocfunc_new("gl_tbm", scr_w, scr_h, _e_output_hwc_tbm_surface_queue_alloc, _e_output_hwc_tbm_surface_queue_free, (void *)output_hwc);
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
        ee = ecore_evas_tbm_allocfunc_new("software_tbm", scr_w, scr_h, _e_output_hwc_tbm_surface_queue_alloc, _e_output_hwc_tbm_surface_queue_free, (void *)output_hwc);
        snprintf(buf, sizeof(buf), "\tEE_DRM New Done %p %dx%d", ee, scr_w, scr_h);
        e_main_ts_end(buf);
     }

   if (!ee)
     {
        e_error_message_show(_("Enlightenment cannot initialize outputs!\n"));
       _e_output_hwc_ee_deinit(output_hwc);
        return EINA_FALSE;
     }

   output_hwc->ee = ee;

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (primary_output == output)
     {
        e_comp->ee = ee;
        ecore_evas_data_set(e_comp->ee, "comp", e_comp);

        ecore_evas_callback_resize_set(e_comp->ee, _e_output_hwc_cb_ee_resize);

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

EINTERN E_Output_Hwc *
e_output_hwc_new(E_Output *output)
{
   E_Output_Hwc *output_hwc = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   output_hwc = E_NEW(E_Output_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   output_hwc->output = output;

   if (!_e_output_hwc_ee_init(output_hwc))
     {
        ERR("hwc_opt: _e_output_hwc_ee_init failed");
        goto fail;
     }

   /*
    * E20 has two hwc policy options.
    * 1. One is the E_OUTPUT_HWC_POLICY_PLANES.
    *   - E20 decides the hwc policy with the E_Planes associated with the tdm_layers.
    *   - E20 manages how to set the surface(buffer) of the ec to the E_Plane.
    * 2. Another is the E_OUTPUT_HWC_POLICY_WIDNOWS.
    *   - The tdm-backend decides the hwc policy with the E_Hwc_Windows associated with the tdm_hwc_window.
    *   - E20 asks to verify the compsition types of the E_Hwc_Window of the ec.
    */
   if (!output->tdm_hwc)
     {
        output_hwc->hwc_policy = E_OUTPUT_HWC_POLICY_PLANES;
        if (!e_output_hwc_planes_init())
          {
             ERR("hwc_opt: e_output_hwc_windows_init failed");
             goto fail;
          }

        INF("Output uses the HWC PLANES Policy.");
     }
   else
     {
        output_hwc->hwc_policy = E_OUTPUT_HWC_POLICY_WINDOWS;

        if (!e_output_hwc_windows_init(output_hwc))
          {
             ERR("hwc_opt: e_output_hwc_windows_init failed");
             goto fail;
          }

        if (!e_hwc_window_init(output_hwc))
          {
             ERR("hwc_opt: E_Hwc_Window init failed");
             goto fail;
          }

        /* turn on sw compositor at the start */
        ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);

        INF("Output uses the HWC WINDOWS Policy.");
     }

   return output_hwc;

fail:
   E_FREE(output_hwc);

   return NULL;
}

EINTERN void
e_output_hwc_del(E_Output_Hwc *output_hwc)
{
   if (!output_hwc) return;

   _e_output_hwc_ee_deinit(output_hwc);

   if (output_hwc->hwc_policy == E_OUTPUT_HWC_POLICY_PLANES)
      e_output_hwc_planes_deinit();
   else
     {
        e_hwc_window_deinit(output_hwc);
        e_output_hwc_windows_deinit();
     }

   E_FREE(output_hwc);
}

EINTERN void
e_output_hwc_apply(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);
   EINA_SAFETY_ON_NULL_RETURN(output_hwc->output);
   if (e_output_hwc_policy_get(output_hwc) == E_OUTPUT_HWC_POLICY_NONE ||
       e_output_hwc_policy_get(output_hwc) == E_OUTPUT_HWC_POLICY_WINDOWS) return;

   if (e_output_hwc_deactive_get(output_hwc))
     {
        if (output_hwc->hwc_mode != E_OUTPUT_HWC_MODE_NONE)
          e_output_hwc_planes_end(output_hwc, "deactive set.");
        return;
     }

   if (!e_output_hwc_planes_usable(output_hwc))
     {
        e_output_hwc_planes_end(output_hwc, __FUNCTION__);
        return;
     }

   if (output_hwc->hwc_mode == E_OUTPUT_HWC_MODE_NONE)
     e_output_hwc_planes_begin(output_hwc);
   else
     e_output_hwc_planes_changed(output_hwc);
}

EINTERN E_Output_Hwc_Mode
e_output_hwc_mode_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, E_OUTPUT_HWC_MODE_NONE);

   return output_hwc->hwc_mode;
}

EINTERN E_Output_Hwc_Policy
e_output_hwc_policy_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, E_OUTPUT_HWC_MODE_NONE);

   return output_hwc->hwc_policy;
}

EINTERN void
e_output_hwc_deactive_set(E_Output_Hwc *output_hwc, Eina_Bool set)
{
   EINA_SAFETY_ON_NULL_RETURN(output_hwc);

   e_output_hwc_planes_end(output_hwc, __FUNCTION__);
   output_hwc->hwc_deactive = set;

   ELOGF("HWC", "e_output_hwc_deactive_set : %d", NULL, NULL, set);
}

EINTERN Eina_Bool
e_output_hwc_deactive_get(E_Output_Hwc *output_hwc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, EINA_FALSE);

   return output_hwc->hwc_deactive;
}
