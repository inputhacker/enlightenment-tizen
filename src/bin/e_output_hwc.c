#include "e.h"
#include "services/e_service_quickpanel.h"

EINTERN E_Output_Hwc *
e_output_hwc_new(E_Output *output)
{
   E_Output_Hwc *output_hwc = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   output_hwc = E_NEW(E_Output_Hwc, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_hwc, NULL);

   output_hwc->output = output;

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
   if (output_hwc) E_FREE(output_hwc);

   return NULL;
}

EINTERN void
e_output_hwc_del(E_Output_Hwc *output_hwc)
{
   if (!output_hwc) return;

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
