#ifdef E_TYPEDEFS

#else
# ifndef E_COMP_WL_TBM_H
#  define E_COMP_WL_TBM_H

#include <tbm_surface.h>

EINTERN Eina_Bool e_comp_wl_tbm_init(void);
EINTERN void e_comp_wl_tbm_shutdown(void);

E_API E_Comp_Wl_Buffer *e_comp_wl_tbm_buffer_get(tbm_surface_h tsurface);
E_API void e_comp_wl_tbm_buffer_destroy(E_Comp_Wl_Buffer *buffer);
E_API Eina_Bool e_comp_wl_tbm_buffer_sync_timeline_used(E_Comp_Wl_Buffer *buffer);

EINTERN struct wl_resource *e_comp_wl_tbm_remote_buffer_get(struct wl_resource *wl_tbm, struct wl_resource *wl_buffer);
EINTERN struct wl_resource *e_comp_wl_tbm_remote_buffer_get_with_tbm(struct wl_resource *wl_tbm, tbm_surface_h tbm_surface);
# endif
#endif
