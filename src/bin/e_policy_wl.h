#ifndef E_MOD_WL_H
#define E_MOD_WL_H

#include "config.h"
#include <e.h>
#include "e_policy_private_data.h"

Eina_Bool e_policy_wl_init(void);
void      e_policy_wl_shutdown(void);
Eina_Bool e_policy_wl_defer_job(void);
void      e_policy_wl_client_add(E_Client *ec);
void      e_policy_wl_client_del(E_Client *ec);
void      e_policy_wl_pixmap_del(E_Pixmap *cp);

typedef struct _E_Policy_Wl_Hook E_Policy_Wl_Hook;
typedef void (*E_Policy_Wl_Hook_Cb)(void *data, pid_t pid);

typedef enum _E_Policy_Wl_Hook_Point
{
   E_POLICY_WL_HOOK_BASE_OUTPUT_RESOLUTION_GET,
   E_POLICY_WL_HOOK_LAST,
} E_Policy_Wl_Hook_Point;

struct _E_Policy_Wl_Hook
{
   EINA_INLIST;
   E_Policy_Wl_Hook_Point hookpoint;
   E_Policy_Wl_Hook_Cb    func;
   void                  *data;
   unsigned char          delete_me : 1;
};

/* hook */
E_API E_Policy_Wl_Hook* e_policy_wl_hook_add(E_Policy_Wl_Hook_Point hookpoint, E_Policy_Wl_Hook_Cb func, const void *data);
E_API void              e_policy_wl_hook_del(E_Policy_Wl_Hook *epwh);

/* visibility */
void      e_policy_wl_visibility_send(E_Client *ec, int vis);

/* iconify */
Eina_Bool  e_policy_wl_iconify_state_supported_get(E_Client *ec);
void       e_policy_wl_iconify_state_change_send(E_Client *ec, int iconic);
E_API void e_policy_wl_iconify(E_Client *ec);
E_API void e_policy_wl_uniconify(E_Client *ec);

/* position */
void      e_policy_wl_position_send(E_Client *ec);

/* notification */
void      e_policy_wl_notification_level_fetch(E_Client *ec);

/* window screenmode */
void      e_policy_wl_win_scrmode_apply(void);

/* aux_hint */
void      e_policy_wl_aux_hint_init(void);
void      e_policy_wl_aux_message_send(E_Client *ec, const char *key, const char *val, Eina_List *options);

/* window brightness */
Eina_Bool e_policy_wl_win_brightness_apply(E_Client *ec);

/* tzsh common */
typedef struct wl_resource* (*E_Policy_Wl_Tzsh_Ext_Hook_Cb)(struct wl_client* client, struct wl_resource* res, uint32_t id);
E_API Eina_Bool e_tzsh_extension_add(const char* name, E_Policy_Wl_Tzsh_Ext_Hook_Cb cb);
E_API void      e_tzsh_extension_del(const char* name);


/* tzsh quickpanel */
E_API void e_tzsh_qp_state_visible_update(E_Client *ec, Eina_Bool vis, E_Quickpanel_Type type);
E_API void e_tzsh_qp_state_orientation_update(E_Client *ec, int ridx, E_Quickpanel_Type type);
E_API void e_tzsh_qp_state_scrollable_update(E_Client *ec, Eina_Bool scrollable, E_Quickpanel_Type type);

/* tzsh indicator */
EINTERN void e_tzsh_indicator_srv_property_change_send(E_Client *ec, int angle);
EINTERN void e_tzsh_indicator_srv_property_update(E_Client *ec);
EINTERN void e_tzsh_indicator_srv_ower_win_update(E_Zone *zone);

/* tzsh shared widget launch */
EINTERN Eina_Bool e_tzsh_shared_widget_launch_prepare_send(E_Client *callee_ec, uint32_t state);

/* indicator */
void         e_policy_wl_indicator_flick_send(E_Client *ec);

/* cbhm */
EINTERN void e_policy_wl_clipboard_data_selected_send(E_Client *ec);

/* aux_message */
E_API void e_policy_wl_stack_changed_send(E_Client *ec);

/* activate */
E_API void e_policy_wl_activate(E_Client *ec);

#endif /* E_MOD_WL_H */
