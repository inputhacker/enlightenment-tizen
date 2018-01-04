#ifdef E_TYPEDEFS

typedef struct _E_Output_Hwc     E_Output_Hwc;

typedef enum _E_Output_Hwc_Mode
{
   E_OUTPUT_HWC_MODE_NONE = 0,
   E_OUTPUT_HWC_MODE_HYBRID,
   E_OUTPUT_HWC_MODE_FULL
} E_Output_Hwc_Mode;

typedef enum _E_Output_Hwc_Policy
{
   E_OUTPUT_HWC_POLICY_NONE = 0,
   E_OUTPUT_HWC_POLICY_PLANES,   // hwc_planes policy that controls the hwc policy at e20 with e_planes
   E_OUTPUT_HWC_POLICY_WINDOWS,  // hwc_windows policy that controls the hwc policy at tdm-backend with e_hwc_windows
} E_Output_Hwc_Policy;

#else
#ifndef E_OUTPUT_HWC_H
#define E_OUTPUT_HWC_H

struct _E_Output_Hwc
{
   E_Output          *output;

   E_Output_Hwc_Policy  hwc_policy;
   E_Output_Hwc_Mode  hwc_mode;
   Eina_Bool          hwc_deactive : 1; // deactive hwc policy
   Eina_Bool          hwc_use_multi_plane;
};

EINTERN E_Output_Hwc        *e_output_hwc_new(E_Output *output);
EINTERN void                 e_output_hwc_del(E_Output_Hwc *output_hwc);
EINTERN void                 e_output_hwc_apply(E_Output_Hwc *output_hwc);
EINTERN E_Output_Hwc_Policy  e_output_hwc_policy_get(E_Output_Hwc *output_hwc);
EINTERN E_Output_Hwc_Mode    e_output_hwc_mode_get(E_Output_Hwc *output_hwc);

EINTERN void               e_output_hwc_deactive_set(E_Output_Hwc *output_hwc, Eina_Bool set);
EINTERN Eina_Bool          e_output_hwc_deactive_get(E_Output_Hwc *output_hwc);
EINTERN void               e_output_hwc_planes_multi_plane_set(E_Output_Hwc *output_hwc, Eina_Bool set);
EINTERN Eina_Bool          e_output_hwc_planes_multi_plane_get(E_Output_Hwc *output_hwc);

EINTERN void               e_output_hwc_planes_end(E_Output_Hwc *output_hwc, const char *location);

#endif
#endif
