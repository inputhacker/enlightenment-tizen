#ifdef E_TYPEDEFS
#else
# ifndef E_XDG_SHELL_V6_H
#  define E_XDG_SHELL_V6_H

EINTERN Eina_Bool e_xdg_shell_v6_init(void);
EINTERN E_Client *e_xdg_shell_v6_xdg_surface_ec_get(struct wl_resource *resource);

# endif
#endif
