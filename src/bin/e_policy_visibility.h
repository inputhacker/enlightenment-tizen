#ifdef E_TYPEDEFS

typedef struct _E_Pol_Vis_Hook E_Pol_Vis_Hook;

typedef enum _E_Pol_Vis_Hook_Type
{
   E_POL_VIS_HOOK_TYPE_FG_SET,
   E_POL_VIS_HOOK_TYPE_LAST,
} E_Pol_Vis_Hook_Type;

typedef Eina_Bool (*E_Pol_Vis_Hook_Cb)(void *data, E_Client *ec);

#else
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
E_API Eina_Bool                   e_policy_visibility_client_iconify(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_uniconify(E_Client *ec, Eina_Bool raise);
E_API Eina_Bool                   e_policy_visibility_client_activate(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_layer_lower(E_Client *ec, E_Layer layer);
E_API E_Vis_Grab                 *e_policy_visibility_client_grab_get(E_Client *ec, const char *name);
E_API void                        e_policy_visibility_client_grab_release(E_Vis_Grab *grab);
E_API Eina_Bool                   e_policy_visibility_client_grab_cancel(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_hide_job_cancel(E_Client *ec);
E_API E_Pol_Vis_Hook             *e_policy_visibility_hook_add(E_Pol_Vis_Hook_Type type, E_Pol_Vis_Hook_Cb cb, const void *data);
E_API void                        e_policy_visibility_hook_del(E_Pol_Vis_Hook *h);
E_API Eina_Bool                   e_policy_visibility_client_is_iconic(E_Client *ec);
EINTERN Eina_Bool                 e_policy_visibility_client_is_uniconic(E_Client *ec);
E_API Eina_Bool                   e_policy_visibility_client_is_uniconify_render_running(E_Client *ec);
E_API void                        e_policy_visibility_client_below_uniconify_skip_set(E_Client *ec, Eina_Bool skip);

EINTERN void                      e_policy_visibility_client_defer_move(E_Client *ec);
EINTERN void                      e_vis_client_send_pre_visibility_event(E_Client *ec);
E_API   void                      e_vis_client_check_send_pre_visibility_event(E_Client *ec, Eina_Bool raise);

#endif
#endif
