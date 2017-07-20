#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_SCREENSHOOTER_H
#define E_COMP_WL_SCREENSHOOTER_H

EINTERN int e_comp_wl_screenshooter_init(void);
EINTERN void e_comp_wl_screenshooter_shutdown(void);
EINTERN Eina_Bool e_comp_wl_screenshooter_dump(tbm_surface_h tbm_surface);

#endif
#endif
