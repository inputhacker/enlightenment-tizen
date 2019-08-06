#ifdef E_TYPEDEFS
#else
#ifndef E_HWC_PLANES_H
#define E_HWC_PLANES_H

/* used by e_hwc */
EINTERN Eina_Bool            e_hwc_planes_init(void);
EINTERN void                 e_hwc_planes_deinit(void);

EINTERN void                 e_hwc_planes_end(E_Hwc *hwc, const char *location);
EINTERN void                 e_hwc_planes_client_end(E_Hwc *hwc, E_Client *ec, const char *location);
EINTERN void                 e_hwc_planes_apply(E_Hwc *hwc);

EINTERN void                 e_hwc_planes_multi_plane_set(E_Hwc *hwc, Eina_Bool set);
EINTERN Eina_Bool            e_hwc_planes_multi_plane_get(E_Hwc *hwc);

EINTERN Eina_Bool            e_hwc_planes_mirror_set(E_Hwc *hwc, E_Hwc *src_hwc, Eina_Rectangle *rect);
EINTERN void                 e_hwc_planes_mirror_unset(E_Hwc *hwc);
EINTERN Eina_Bool            e_hwc_planes_presentation_update(E_Hwc *hwc, E_Client *ec);
EINTERN Eina_Bool            e_hwc_planes_external_commit(E_Hwc *hwc);

#endif
#endif
