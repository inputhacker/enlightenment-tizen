#ifndef _E_POLICY_VISIBILITY_H_
#define _E_POLICY_VISIBILITY_H_

typedef struct _E_Vis_Grab E_Vis_Grab;

E_API Eina_Bool                   e_policy_visibility_init(void);
E_API void                        e_policy_visibility_shutdown(void);
E_API E_Client                   *e_policy_visibility_main_activity_get(void);
E_API Eina_List                  *e_policy_visibility_foreground_clients_get(void);
E_API Eina_Bool                   e_policy_visibility_client_is_activity(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_lower(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_raise(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_uniconify(E_Client *ec, Eina_Bool raise);
E_API Eina_Bool                   e_policy_visibility_client_activate(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_layer_lower(E_Client *ec, E_Layer layer);
E_API E_Vis_Grab                 *e_policy_visibility_client_grab_get(E_Client *ec, const char *name);
E_API void                        e_policy_visibility_client_grab_release(E_Vis_Grab *grab);
E_API void                        e_policy_visibility_uniconify_render_disable_set(E_Client *ec, Eina_Bool disable);

EINTERN void                      e_policy_visibility_client_defer_move(E_Client *ec, int x, int y);

#endif
