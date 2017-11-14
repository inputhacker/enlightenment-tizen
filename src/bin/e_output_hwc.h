#ifdef E_TYPEDEFS

typedef struct _E_Output_Hwc     E_Output_Hwc;

typedef enum _E_Output_Hwc_Mode
{
   E_OUTPUT_HWC_MODE_NO = 0,
   E_OUTPUT_HWC_MODE_HYBRID,
   E_OUTPUT_HWC_MODE_FULL
} E_Output_Hwc_Mode;

#else
#ifndef E_OUTPUT_HWC_H
#define E_OUTPUT_HWC_H

struct _E_Output_Hwc
{
   E_Output          *output;
   E_Output_Hwc_Mode  hwc_mode;

   /* non opt_hwc */
   Eina_Bool          hwc_deactive : 1; // deactive hwc policy
   Eina_Bool          hwc_use_multi_plane;

   /* opt_hwc */
   /* that is whether the layer policy for this output
      is controlled by tdm-backend */
   Eina_Bool          opt_hwc;
   Eina_List         *windows;
};

/* This module is responsible for evaluate which an ec will be composite by a hwc
extension and commit the changes to hwc extension. */

EINTERN Eina_Bool            e_output_hwc_opt_hwc_enabled(E_Output_Hwc *output_hwc);
EINTERN const Eina_List     *e_output_hwc_windows_get(E_Output_Hwc *output_hwc);
EINTERN Eina_Bool            e_output_hwc_render(E_Output_Hwc *output_hwc);
EINTERN E_Output_Hwc_Window *e_output_hwc_find_window_by_ec(E_Output_Hwc *output_hwc, E_Client *ec);

EINTERN E_Output_Hwc  *e_output_hwc_new(E_Output *output);
EINTERN void           e_output_hwc_del(E_Output_Hwc *output_hwc);
EINTERN void           e_output_hwc_apply(E_Output_Hwc *output_hwc);

EINTERN E_Output_Hwc_Mode e_output_hwc_mode_get(E_Output_Hwc *output_hwc);

EINTERN void e_output_hwc_deactive_set(E_Output_Hwc *output_hwc, Eina_Bool set);
EINTERN Eina_Bool e_output_hwc_deactive_get(E_Output_Hwc *output_hwc);
EINTERN void e_output_hwc_multi_plane_set(E_Output_Hwc *output_hwc, Eina_Bool set);
EINTERN Eina_Bool e_output_hwc_multi_plane_get(E_Output_Hwc *output_hwc);

EINTERN void e_output_hwc_end(E_Output_Hwc *output_hwc, const char *location);

#endif // E_OUTPUT_HWC_H
#endif
