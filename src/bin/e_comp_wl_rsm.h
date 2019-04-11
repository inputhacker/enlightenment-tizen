#ifdef E_TYPEDEFS
typedef struct _E_Event_Remote_Surface_Provider E_Event_Remote_Surface_Provider;
#else
# ifndef E_COMP_WL_RSM_H
#  define E_COMP_WL_RSM_H

EINTERN void       e_comp_wl_remote_surface_init(void);
EINTERN void       e_comp_wl_remote_surface_shutdown(void);
EINTERN Eina_Bool  e_comp_wl_remote_surface_commit(E_Client *ec);
E_API   void       e_comp_wl_remote_surface_image_save(E_Client *ec);
E_API   void       e_comp_wl_remote_surface_image_save_skip_set(E_Client *ec, Eina_Bool set);
E_API   Eina_Bool  e_comp_wl_remote_surface_image_save_skip_get(E_Client *ec);
EINTERN void       e_comp_wl_remote_surface_debug_info_get(Eldbus_Message_Iter *iter);
E_API   E_Client  *e_comp_wl_remote_surface_bound_provider_ec_get(E_Client *ec);

/**
 * Get a list of e_clients of tizen remote surface providers which is used in given ec
 * NB: caller must free returned Eina_List object after using it.
 */
E_API   Eina_List *e_comp_wl_remote_surface_providers_get(E_Client *ec);

E_API extern int E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE;

struct _E_Event_Remote_Surface_Provider
{
   E_Client *ec;
};

# endif
#endif
