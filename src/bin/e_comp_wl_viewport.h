#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_VIEWPORT_H
#define E_COMP_WL_VIEWPORT_H

int e_comp_wl_viewport_init(void);
void e_comp_wl_viewport_shutdown(void);

Eina_Bool e_comp_wl_viewport_create(struct wl_resource *resource,
                                      uint32_t id,
                                      struct wl_resource *surface);
Eina_Bool e_comp_wl_viewport_apply(E_Client *ec);
Eina_Bool e_comp_wl_viewport_is_changed(E_Client *ec);

#endif
#endif
