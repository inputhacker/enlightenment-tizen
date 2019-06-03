#ifdef E_TYPEDEFS

typedef struct _E_Pol_Vis_Hook E_Pol_Vis_Hook;

typedef enum _E_Pol_Vis_Hook_Type
{
   E_POL_VIS_HOOK_TYPE_FG_SET,
   E_POL_VIS_HOOK_TYPE_UNICONIFY_RENDER_RUNNING,
   E_POL_VIS_HOOK_TYPE_LOWER,
   E_POL_VIS_HOOK_TYPE_HIDE,
   E_POL_VIS_HOOK_TYPE_LAST,
} E_Pol_Vis_Hook_Type;

typedef enum _E_Vis_Job_Type
{
   E_VIS_JOB_TYPE_NONE                     = 0,
   E_VIS_JOB_TYPE_SHOW                     = (1 << 0),
   E_VIS_JOB_TYPE_HIDE                     = (1 << 1),
   E_VIS_JOB_TYPE_RAISE                    = (1 << 2),
   E_VIS_JOB_TYPE_LOWER                    = (1 << 3),
   E_VIS_JOB_TYPE_ACTIVATE                 = (1 << 4),
   E_VIS_JOB_TYPE_UNICONIFY                = (1 << 5),
   E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY  = (1 << 6),
   E_VIS_JOB_TYPE_LAYER_LOWER              = (1 << 7),
   E_VIS_JOB_TYPE_DEFER_MOVE               = (1 << 8),
   E_VIS_JOB_TYPE_ICONIFY                  = (1 << 9),
} E_Vis_Job_Type;

#define E_VIS_JOB_TYPE_ALL (E_VIS_JOB_TYPE_SHOW                    | \
                            E_VIS_JOB_TYPE_HIDE                    | \
                            E_VIS_JOB_TYPE_RAISE                   | \
                            E_VIS_JOB_TYPE_LOWER                   | \
                            E_VIS_JOB_TYPE_ACTIVATE                | \
                            E_VIS_JOB_TYPE_UNICONIFY               | \
                            E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY | \
                            E_VIS_JOB_TYPE_LAYER_LOWER             | \
                            E_VIS_JOB_TYPE_DEFER_MOVE              | \
                            E_VIS_JOB_TYPE_ICONIFY)

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
E_API E_Vis_Grab                 *e_policy_visibility_client_filtered_grab_get(E_Client *ec, E_Vis_Job_Type type, const char *name);
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
EINTERN void                      e_vis_client_check_send_pre_visibility_event(E_Client *ec, Eina_Bool raise);

#endif
#endif
