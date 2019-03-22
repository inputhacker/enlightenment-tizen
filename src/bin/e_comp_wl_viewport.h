#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_VIEWPORT_H
#define E_COMP_WL_VIEWPORT_H

EINTERN int e_comp_wl_viewport_init(void);
EINTERN void e_comp_wl_viewport_shutdown(void);

E_API Eina_Bool e_comp_wl_viewport_create(struct wl_resource *resource,
                                          uint32_t id,
                                          struct wl_resource *surface);
EINTERN Eina_Bool e_comp_wl_viewport_apply(E_Client *ec);
EINTERN Eina_Bool e_comp_wl_viewport_is_changed(E_Client *ec);

#endif
#endif
