#ifdef E_TYPEDEFS
#else
#ifndef E_HWC_WINDOWS_H
#define E_HWC_WINDOWS_H

EINTERN Eina_Bool            e_hwc_windows_init(E_Hwc *hwc);
EINTERN void                 e_hwc_windows_deinit(void);

EINTERN Eina_Bool            e_hwc_windows_render(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_commit(E_Hwc *hwc);

EINTERN Eina_Bool            e_hwc_windows_pp_commit_possible_check(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_windows_zoom_set(E_Hwc *hwc, Eina_Rectangle *rect);
EINTERN void                 e_hwc_windows_zoom_unset(E_Hwc *hwc);

#endif
#endif
