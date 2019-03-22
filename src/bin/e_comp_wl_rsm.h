#ifdef E_TYPEDEFS
typedef struct _E_Event_Remote_Surface_Provider E_Event_Remote_Surface_Provider;
#else
# ifndef E_COMP_WL_RSM_H
#  define E_COMP_WL_RSM_H

typedef enum _E_Image_Save_State
{
   E_IMAGE_SAVE_STATE_START = 0,
   E_IMAGE_SAVE_STATE_DONE,
   E_IMAGE_SAVE_STATE_CANCEL,
   E_IMAGE_SAVE_STATE_INVALID,
   E_IMAGE_SAVE_STATE_BUSY,
} E_Image_Save_State;

typedef void (*E_Image_Save_End_Cb)(void *data, E_Client *ec, const Eina_Stringshare *dest, E_Image_Save_State state);

EINTERN void      e_comp_wl_remote_surface_init(void);
EINTERN void      e_comp_wl_remote_surface_shutdown(void);
EINTERN Eina_Bool e_comp_wl_remote_surface_commit(E_Client *ec);
EAPI    void      e_comp_wl_remote_surface_image_save(E_Client *ec);
EAPI    void      e_comp_wl_remote_surface_image_save_skip_set(E_Client *ec, Eina_Bool set);
EAPI    Eina_Bool e_comp_wl_remote_surface_image_save_skip_get(E_Client *ec);
EINTERN void      e_comp_wl_remote_surface_debug_info_get(Eldbus_Message_Iter *iter);
EAPI E_Client*    e_comp_wl_remote_surface_bound_provider_ec_get(E_Client *ec);

E_API   E_Image_Save_State   e_client_image_save(E_Client *ec, const char* path, const char* name, E_Image_Save_End_Cb func_end, void *data, Eina_Bool skip_child);

E_API extern int E_EVENT_REMOTE_SURFACE_PROVIDER_VISIBILITY_CHANGE;

struct _E_Event_Remote_Surface_Provider
{
   E_Client *ec;
};

# endif
#endif
