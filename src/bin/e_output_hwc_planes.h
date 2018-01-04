#ifdef E_TYPEDEFS
#else
#ifndef E_OUTPUT_HWC_PLANES_H
#define E_OUTPUT_HWC_PLANES_H

/* used by e_output_hwc */
EINTERN Eina_Bool            e_output_hwc_planes_init(void);
EINTERN void                 e_output_hwc_planes_deinit(void);

EINTERN Eina_Bool            e_output_hwc_planes_usable(E_Output_Hwc *output_hwc);
EINTERN void                 e_output_hwc_planes_begin(E_Output_Hwc *output_hwc);
EINTERN void                 e_output_hwc_planes_end(E_Output_Hwc *output_hwc, const char *location);
EINTERN void                 e_output_hwc_planes_changed(E_Output_Hwc *output_hwc);

EINTERN void                 e_output_hwc_planes_multi_plane_set(E_Output_Hwc *output_hwc, Eina_Bool set);
EINTERN Eina_Bool            e_output_hwc_planes_multi_plane_get(E_Output_Hwc *output_hwc);

#endif
#endif
