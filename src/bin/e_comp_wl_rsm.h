#ifdef E_TYPEDEFS
typedef struct _E_Event_Remote_Surface_Provider E_Event_Remote_Surface_Provider;
#else
# ifndef E_COMP_WL_RSM_H
#  define E_COMP_WL_RSM_H

EINTERN void      e_comp_wl_remote_surface_init(void);
EINTERN void      e_comp_wl_remote_surface_shutdown(void);
EINTERN Eina_Bool e_comp_wl_remote_surface_commit(E_Client *ec);

E_API extern int E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE;

struct _E_Event_Remote_Surface_Provider
{
   E_Client *ec;
};

# endif
#endif
