#ifdef E_TYPEDEFS
#else
#ifndef E_HWC_PLANES_H
#define E_HWC_PLANES_H

/* used by e_hwc */
EINTERN Eina_Bool            e_hwc_planes_init(void);
EINTERN void                 e_hwc_planes_deinit(void);

EINTERN Eina_Bool            e_hwc_planes_usable(E_Hwc *hwc);
EINTERN void                 e_hwc_planes_begin(E_Hwc *hwc);
EINTERN void                 e_hwc_planes_end(E_Hwc *hwc, const char *location);
EINTERN void                 e_hwc_planes_changed(E_Hwc *hwc);

EINTERN void                 e_hwc_planes_multi_plane_set(E_Hwc *hwc, Eina_Bool set);
EINTERN Eina_Bool            e_hwc_planes_multi_plane_get(E_Hwc *hwc);

#endif
#endif
