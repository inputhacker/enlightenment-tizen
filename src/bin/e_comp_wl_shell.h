#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_WL_SHELL_H
#  define E_COMP_WL_SHELL_H

typedef struct
{
   void (*configure_send)(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height);
   void (*configure)(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h);
   void (*ping)(struct wl_resource *resource);
   void (*map)(struct wl_resource *resource);
   void (*unmap)(struct wl_resource *resource);
} E_Shell_Surface_Api;

EINTERN void e_comp_wl_shell_init(void);
EINTERN void e_comp_wl_shell_shutdown(void);

EINTERN Eina_Bool e_shell_e_client_shell_assignable_check(E_Client *ec);
EINTERN void      e_shell_e_client_shsurface_assign(E_Client *ec, struct wl_resource *shsurface, E_Shell_Surface_Api *api);
EINTERN void      e_shell_e_client_shsurface_api_set(E_Client *ec, E_Shell_Surface_Api *api);
EINTERN void      e_shell_e_client_toplevel_set(E_Client *ec);
EINTERN void      e_shell_e_client_popup_set(E_Client *ec);
EINTERN Eina_Bool e_shell_e_client_name_title_set(E_Client *ec, const char *name, const char *title);
EINTERN Eina_Bool e_shell_e_client_app_id_set(E_Client *ec, const char *app_id);
EINTERN void      e_shell_e_client_parent_set(E_Client *ec, struct wl_resource *parent_resource);
EINTERN void      e_shell_e_client_map(E_Client *ec);
EINTERN void      e_shell_e_client_unmap(E_Client *ec);
EINTERN Eina_Bool e_shell_e_client_interactive_move(E_Client *ec, struct wl_resource *seat);
EINTERN Eina_Bool e_shell_e_client_interactive_resize(E_Client *ec, struct wl_resource *resource, struct wl_resource *seat, uint32_t edges);
EINTERN void      e_shell_e_client_pong(E_Client *ec);
EINTERN void      e_shell_e_client_destroy(E_Client *ec);



# endif
#endif
