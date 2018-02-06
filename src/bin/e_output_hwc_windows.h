#ifdef E_TYPEDEFS
#else
#ifndef E_OUTPUT_HWC_WINDOWS_H
#define E_OUTPUT_HWC_WINDOWS_H

EINTERN Eina_Bool            e_output_hwc_windows_init(E_Output_Hwc *output_hwc);
EINTERN void                 e_output_hwc_windows_deinit(void);

EINTERN const Eina_List     *e_output_hwc_windows_get(E_Output_Hwc *output_hwc);
EINTERN Eina_Bool            e_output_hwc_windows_render(E_Output_Hwc *output_hwc);
EINTERN Eina_Bool            e_output_hwc_windows_commit(E_Output_Hwc *output_hwc);

EINTERN Eina_Bool            e_output_hwc_windows_pp_commit_possible_check(E_Output_Hwc *output_hwc);
EINTERN Eina_Bool            e_output_hwc_windows_zoom_set(E_Output_Hwc *output_hwc, Eina_Rectangle *rect);
EINTERN void                 e_output_hwc_windows_zoom_unset(E_Output_Hwc *output_hwc);

#endif
#endif
