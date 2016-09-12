#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_WL_RSM_H
#  define E_COMP_WL_RSM_H

EINTERN void      e_comp_wl_remote_surface_init(void);
EINTERN void      e_comp_wl_remote_surface_shutdown(void);
EINTERN Eina_Bool e_comp_wl_remote_surface_commit(E_Client *ec);

# endif
#endif
