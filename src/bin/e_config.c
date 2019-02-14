#include "e.h"

E_API E_Config *e_config = NULL;

/* local subsystem functions */
static void      _e_config_save_cb(void *data);
static void      _e_config_free(E_Config *cfg);
static Eina_Bool _e_config_cb_timer(void *data);
static int       _e_config_eet_close_handle(Eet_File *ef, char *file);

/* local subsystem globals */
static int _e_config_save_block = 0;
static const char *_e_config_profile = NULL;
static int _e_config_save_queued = 0;
static Ecore_Idle_Enterer *_idle_enter = NULL;

static E_Config_DD *_e_config_edd = NULL;
static E_Config_DD *_e_config_module_edd = NULL;
static E_Config_DD *_e_config_theme_edd = NULL;
static E_Config_DD *_e_config_desktop_bg_edd = NULL;
static E_Config_DD *_e_config_desklock_bg_edd = NULL;
static E_Config_DD *_e_config_desktop_name_edd = NULL;
static E_Config_DD *_e_config_desktop_window_profile_edd = NULL;
static E_Config_DD *_e_config_env_var_edd = NULL;
static E_Config_DD *_e_config_client_type_edd = NULL;
static E_Config_DD *_e_config_policy_desk_edd = NULL;
static E_Config_DD *_e_config_socket_access_edd = NULL;
static E_Config_DD *_e_config_aux_hint_supported_edd = NULL;

E_API int E_EVENT_CONFIG_MODE_CHANGED = 0;

#if 0
static E_Dialog *_e_config_error_dialog = NULL;

static void
_e_config_error_dialog_cb_delete(void *dia)
{
   if (dia == _e_config_error_dialog)
     _e_config_error_dialog = NULL;
}
#endif

static void
_e_config_edd_shutdown(void)
{
   E_CONFIG_DD_FREE(_e_config_edd);
   E_CONFIG_DD_FREE(_e_config_module_edd);
   E_CONFIG_DD_FREE(_e_config_theme_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_bg_edd);
   E_CONFIG_DD_FREE(_e_config_desklock_bg_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_name_edd);
   E_CONFIG_DD_FREE(_e_config_desktop_window_profile_edd);
   E_CONFIG_DD_FREE(_e_config_env_var_edd);
   E_CONFIG_DD_FREE(_e_config_client_type_edd);
   E_CONFIG_DD_FREE(_e_config_policy_desk_edd);
   E_CONFIG_DD_FREE(_e_config_socket_access_edd);
   E_CONFIG_DD_FREE(_e_config_aux_hint_supported_edd);
}

static void
_e_config_edd_init(Eina_Bool old)
{
   _e_config_desktop_bg_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Background", E_Config_Desktop_Background);
#undef T
#undef D
#define T E_Config_Desktop_Background
#define D _e_config_desktop_bg_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, file, STR);

   _e_config_desktop_name_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Name", E_Config_Desktop_Name);
#undef T
#undef D
#define T E_Config_Desktop_Name
#define D _e_config_desktop_name_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, name, STR);

   _e_config_desktop_window_profile_edd = E_CONFIG_DD_NEW("E_Config_Desktop_Window_Profile", E_Config_Desktop_Window_Profile);
#undef T
#undef D
#define T E_Config_Desktop_Window_Profile
#define D _e_config_desktop_window_profile_edd
   E_CONFIG_VAL(D, T, zone, INT);
   E_CONFIG_VAL(D, T, desk_x, INT);
   E_CONFIG_VAL(D, T, desk_y, INT);
   E_CONFIG_VAL(D, T, profile, STR);

   _e_config_module_edd = E_CONFIG_DD_NEW("E_Config_Module", E_Config_Module);
#undef T
#undef D
#define T E_Config_Module
#define D _e_config_module_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, enabled, UCHAR);
   E_CONFIG_VAL(D, T, delayed, UCHAR);
   E_CONFIG_VAL(D, T, priority, INT);

   _e_config_env_var_edd = E_CONFIG_DD_NEW("E_Config_Env_Var", E_Config_Env_Var);
#undef T
#undef D
#define T E_Config_Env_Var
#define D _e_config_env_var_edd
   E_CONFIG_VAL(D, T, var, STR);
   E_CONFIG_VAL(D, T, val, STR);
   E_CONFIG_VAL(D, T, unset, UCHAR);

   _e_config_client_type_edd = E_CONFIG_DD_NEW("E_Config_Client_Type", E_Config_Client_Type);
#undef T
#undef D
#define T E_Config_Client_Type
#define D _e_config_client_type_edd
   E_CONFIG_VAL(D, T, name, STR);
   E_CONFIG_VAL(D, T, clas, STR);
   E_CONFIG_VAL(D, T, window_type, INT);
   E_CONFIG_VAL(D, T, client_type, INT);

   _e_config_policy_desk_edd = E_CONFIG_DD_NEW("E_Config_Policy_Desk", E_Config_Policy_Desk);
#undef T
#undef D
#define T E_Config_Policy_Desk
#define D _e_config_policy_desk_edd
   E_CONFIG_VAL(D, T, zone_num, UINT);
   E_CONFIG_VAL(D, T, x, INT);
   E_CONFIG_VAL(D, T, y, INT);
   E_CONFIG_VAL(D, T, enable, INT);

   _e_config_socket_access_edd = E_CONFIG_DD_NEW("E_Config_Socket_Access", E_Config_Socket_Access);
#undef T
#undef D
#define T E_Config_Socket_Access
#define D _e_config_socket_access_edd
   E_CONFIG_VAL(D, T, sock_access.use, UCHAR);
   E_CONFIG_VAL(D, T, sock_access.name, STR);
   E_CONFIG_VAL(D, T, sock_access.owner, STR);
   E_CONFIG_VAL(D, T, sock_access.group, STR);
   E_CONFIG_VAL(D, T, sock_access.permissions, INT);
   E_CONFIG_VAL(D, T, sock_access.smack.use, UCHAR);
   E_CONFIG_VAL(D, T, sock_access.smack.name, STR);
   E_CONFIG_VAL(D, T, sock_access.smack.value, STR);
   E_CONFIG_VAL(D, T, sock_access.smack.flags, INT);
   E_CONFIG_VAL(D, T, sock_symlink_access.use, UCHAR);
   E_CONFIG_VAL(D, T, sock_symlink_access.link_name, STR);
   E_CONFIG_VAL(D, T, sock_symlink_access.owner, STR);
   E_CONFIG_VAL(D, T, sock_symlink_access.group, STR);
   E_CONFIG_VAL(D, T, sock_symlink_access.smack.name, STR);
   E_CONFIG_VAL(D, T, sock_symlink_access.smack.value, STR);
   E_CONFIG_VAL(D, T, sock_symlink_access.smack.flags, INT);

   _e_config_aux_hint_supported_edd = E_CONFIG_DD_NEW("E_Config_Aux_Hint_Supported", E_Config_Aux_Hint_Supported);
#undef T
#undef D
#define T E_Config_Aux_Hint_Supported
#define D _e_config_aux_hint_supported_edd
   E_CONFIG_VAL(D, T, name, STR);

   _e_config_edd = E_CONFIG_DD_NEW("E_Config", E_Config);
#undef T
#undef D
#define T E_Config
#define D _e_config_edd
   /**/ /* == already configurable via ipc */
   E_CONFIG_VAL(D, T, config_version, INT);
   E_CONFIG_VAL(D, T, desktop_default_background, STR);
   E_CONFIG_VAL(D, T, desktop_default_name, STR);
   E_CONFIG_VAL(D, T, desktop_default_window_profile, STR);
   E_CONFIG_LIST(D, T, desktop_backgrounds, _e_config_desktop_bg_edd);
   E_CONFIG_LIST(D, T, desktop_names, _e_config_desktop_name_edd);
   E_CONFIG_LIST(D, T, desktop_window_profiles, _e_config_desktop_window_profile_edd);
   E_CONFIG_VAL(D, T, framerate, DOUBLE);
   E_CONFIG_VAL(D, T, priority, INT);
   E_CONFIG_VAL(D, T, zone_desks_x_count, INT);
   E_CONFIG_VAL(D, T, zone_desks_y_count, INT);
   E_CONFIG_LIST(D, T, modules, _e_config_module_edd);
   E_CONFIG_VAL(D, T, window_placement_policy, INT);
   E_CONFIG_VAL(D, T, use_e_policy, INT);
   E_CONFIG_VAL(D, T, launcher.title, STR);
   E_CONFIG_VAL(D, T, launcher.clas, STR);
   E_CONFIG_VAL(D, T, launcher.type, UINT);
   E_CONFIG_LIST(D, T, policy_desks, _e_config_policy_desk_edd);
   E_CONFIG_VAL(D, T, use_softkey_service, INT);
   E_CONFIG_VAL(D, T, use_softkey, INT);
   E_CONFIG_VAL(D, T, softkey_size, INT);
   E_CONFIG_VAL(D, T, focus_policy, INT);
   E_CONFIG_VAL(D, T, focus_policy_ext, INT);
   E_CONFIG_VAL(D, T, focus_setting, INT);
   E_CONFIG_VAL(D, T, always_click_to_raise, INT);
   E_CONFIG_VAL(D, T, always_click_to_focus, INT);
   E_CONFIG_VAL(D, T, use_auto_raise, INT);
   E_CONFIG_VAL(D, T, maximize_policy, INT);
   E_CONFIG_VAL(D, T, allow_manip, INT);
   E_CONFIG_VAL(D, T, kill_if_close_not_possible, INT);
   E_CONFIG_VAL(D, T, kill_process, INT);
   E_CONFIG_VAL(D, T, kill_timer_wait, DOUBLE);
   E_CONFIG_VAL(D, T, ping_clients, INT);
   E_CONFIG_VAL(D, T, use_e_cursor, INT);
   E_CONFIG_VAL(D, T, cursor_size, INT);
   E_CONFIG_VAL(D, T, transient.move, INT);
   E_CONFIG_VAL(D, T, transient.resize, INT);
   E_CONFIG_VAL(D, T, transient.raise, INT);
   E_CONFIG_VAL(D, T, transient.lower, INT);
   E_CONFIG_VAL(D, T, transient.layer, INT);
   E_CONFIG_VAL(D, T, transient.desktop, INT);
   E_CONFIG_VAL(D, T, transient.iconify, INT);
   E_CONFIG_VAL(D, T, fullscreen_policy, INT);
   E_CONFIG_VAL(D, T, dpms_enable, INT);
   E_CONFIG_VAL(D, T, dpms_standby_enable, INT);
   E_CONFIG_VAL(D, T, dpms_suspend_enable, INT);
   E_CONFIG_VAL(D, T, dpms_off_enable, INT);
   E_CONFIG_VAL(D, T, dpms_standby_timeout, INT);
   E_CONFIG_VAL(D, T, dpms_suspend_timeout, INT);
   E_CONFIG_VAL(D, T, dpms_off_timeout, INT);
   E_CONFIG_VAL(D, T, no_dpms_on_fullscreen, UCHAR);
   E_CONFIG_VAL(D, T, mouse_hand, INT);
   E_CONFIG_VAL(D, T, border_raise_on_mouse_action, INT);
   E_CONFIG_VAL(D, T, border_raise_on_focus, INT);
   E_CONFIG_VAL(D, T, raise_on_revert_focus, INT);
   E_CONFIG_VAL(D, T, theme_default_border_style, STR);
   E_CONFIG_VAL(D, T, screen_limits, INT);
   E_CONFIG_VAL(D, T, ping_clients_interval, INT);
   E_CONFIG_VAL(D, T, border_keyboard.timeout, DOUBLE);
   E_CONFIG_VAL(D, T, border_keyboard.move.dx, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.move.dy, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.resize.dx, UCHAR);
   E_CONFIG_VAL(D, T, border_keyboard.resize.dy, UCHAR);
   E_CONFIG_VAL(D, T, scale.min, DOUBLE);
   E_CONFIG_VAL(D, T, scale.max, DOUBLE);
   E_CONFIG_VAL(D, T, scale.factor, DOUBLE);
   E_CONFIG_VAL(D, T, scale.profile_factor, DOUBLE);
   E_CONFIG_VAL(D, T, scale.inch_correction, DOUBLE);
   E_CONFIG_VAL(D, T, scale.inch_correction_bound, DOUBLE);
   E_CONFIG_VAL(D, T, scale.base_dpi, INT);
   E_CONFIG_VAL(D, T, scale.use_dpi, UCHAR);
   E_CONFIG_VAL(D, T, scale.use_custom, UCHAR);
   E_CONFIG_VAL(D, T, scale.for_tdm, UCHAR);
   E_CONFIG_VAL(D, T, show_cursor, UCHAR);
   E_CONFIG_VAL(D, T, idle_cursor, UCHAR);
   E_CONFIG_LIST(D, T, env_vars, _e_config_env_var_edd);
   E_CONFIG_VAL(D, T, xkb.only_label, INT);
   E_CONFIG_VAL(D, T, xkb.default_model, STR);
   E_CONFIG_VAL(D, T, xkb.dont_touch_my_damn_keyboard, UCHAR);
   E_CONFIG_VAL(D, T, xkb.use_cache, UCHAR);
   E_CONFIG_VAL(D, T, xkb.delay_held_key_input_to_focus, UINT);
   E_CONFIG_VAL(D, T, xkb.default_rmlvo.rules, STR);
   E_CONFIG_VAL(D, T, xkb.default_rmlvo.model, STR);
   E_CONFIG_VAL(D, T, xkb.default_rmlvo.layout, STR);
   E_CONFIG_VAL(D, T, xkb.default_rmlvo.variant, STR);
   E_CONFIG_VAL(D, T, xkb.default_rmlvo.options, STR);
   E_CONFIG_VAL(D, T, keyboard.repeat_delay, INT);
   E_CONFIG_VAL(D, T, keyboard.repeat_rate, INT);
   E_CONFIG_VAL(D, T, use_desktop_window_profile, INT);
#ifdef _F_ZONE_WINDOW_ROTATION_
   E_CONFIG_VAL(D, T, wm_win_rotation, UCHAR);
#endif
   E_CONFIG_VAL(D, T, screen_rotation_pre, UINT);
   E_CONFIG_VAL(D, T, screen_rotation_setting, UINT);
   E_CONFIG_VAL(D, T, screen_rotation_client_ignore, UCHAR);
   E_CONFIG_VAL(D, T, eom_enable, UCHAR);
   E_CONFIG_VAL(D, T, use_cursor_timer, INT);
   E_CONFIG_VAL(D, T, cursor_timer_interval, INT);
   E_CONFIG_LIST(D, T, client_types, _e_config_client_type_edd);
   E_CONFIG_VAL(D, T, comp_shadow_file, STR);
   E_CONFIG_VAL(D, T, sleep_for_dri, INT);
   E_CONFIG_VAL(D, T, create_wm_ready, INT);
   E_CONFIG_VAL(D, T, create_wm_start, INT);
   E_CONFIG_VAL(D, T, comp_canvas_bg.r, UCHAR);
   E_CONFIG_VAL(D, T, comp_canvas_bg.g, UCHAR);
   E_CONFIG_VAL(D, T, comp_canvas_bg.b, UCHAR);
   E_CONFIG_VAL(D, T, comp_canvas_bg.a, UCHAR);
   E_CONFIG_VAL(D, T, comp_canvas_bg.opmode, INT);
   E_CONFIG_VAL(D, T, delayed_load_idle_count, INT);
   E_CONFIG_VAL(D, T, use_buffer_flush, UCHAR);
   E_CONFIG_VAL(D, T, use_desk_smart_obj, UCHAR);
   E_CONFIG_LIST(D, T, sock_accesses, _e_config_socket_access_edd);
   E_CONFIG_LIST(D, T, aux_hint_supported, _e_config_aux_hint_supported_edd);
   E_CONFIG_VAL(D, T, use_module_srv.qp, UCHAR);
   E_CONFIG_VAL(D, T, launchscreen_timeout, DOUBLE);
   E_CONFIG_VAL(D, T, enable_conformant_ack, UCHAR);
   E_CONFIG_VAL(D, T, conformant_ack_timeout, DOUBLE);
   E_CONFIG_VAL(D, T, calc_vis_without_effect, UCHAR);
   E_CONFIG_VAL(D, T, save_win_buffer, UCHAR);
   E_CONFIG_VAL(D, T, hold_prev_win_img, UCHAR);
   E_CONFIG_VAL(D, T, indicator_plug_name, STR);
   E_CONFIG_VAL(D, T, launchscreen_without_timer, UCHAR);
   E_CONFIG_VAL(D, T, log_type, INT);
   E_CONFIG_VAL(D, T, rsm_buffer_release_mode, INT);
   E_CONFIG_VAL(D, T, deiconify_approve, UCHAR);
   E_CONFIG_VAL(D, T, use_pp_zoom, UCHAR);
   E_CONFIG_VAL(D, T, priority_control, UCHAR);
   E_CONFIG_VAL(D, T, comp_canvas_norender.use, UCHAR);
   E_CONFIG_VAL(D, T, qp_use_bg_rect, UCHAR);
   E_CONFIG_VAL(D, T, qp_handler.use_alpha, UCHAR);
   E_CONFIG_VAL(D, T, qp_handler.alpha, INT);
   E_CONFIG_VAL(D, T, gesture_service.wait_time, DOUBLE);
   E_CONFIG_VAL(D, T, gesture_service.wait_dist, INT);
}

static Eina_Bool
_e_config_idle_cb(void *data)
{
   if (_e_config_save_queued)
     {
        _e_config_save_cb(NULL);
        _e_config_save_queued = 0;
     }

   return ECORE_CALLBACK_RENEW;
}

/* externally accessible functions */
EINTERN int
e_config_init(void)
{
   E_EVENT_CONFIG_MODE_CHANGED = ecore_event_type_new();

   /* TIZEN_ONLY: We don't save e.cfg file.
    * Sometimes e.cfg file was broken after writing.
    */
   if (getenv("E_CONF_RO")) e_config_save_block_set(1);

   /* if environment var set - use this profile name */
   char *s = e_util_env_get("E_CONF_PROFILE");
   if (s)
     {
        _e_config_profile = eina_stringshare_add(s);
        E_FREE(s);
     }

   _e_config_edd_init(EINA_FALSE);

   e_config_load();
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_config, 0);

   e_config_save_queue();

   _idle_enter = ecore_idle_enterer_add(_e_config_idle_cb, NULL);
   return 1;
}

EINTERN int
e_config_shutdown(void)
{
   if (_idle_enter) ecore_idle_enterer_del(_idle_enter);
   _idle_enter = NULL;

   eina_stringshare_del(_e_config_profile);

   _e_config_edd_shutdown();

   return 1;
}

E_API void
e_config_load(void)
{
   int reload = 0;

   e_config = e_config_domain_load("e", _e_config_edd);
   if (e_config)
     {
        /* major version change - that means wipe and restart */
        if ((e_config->config_version) < E_CONFIG_FILE_EPOCH * 1000000)
          {
             /* your config is too old - need new defaults */
             _e_config_free(e_config);
             e_config = NULL;
             reload = 1;
             ecore_timer_add(1.0, _e_config_cb_timer,
                             _("Settings data needed upgrading. Your old settings have<br>"
                               "been wiped and a new set of defaults initialized. This<br>"
                               "will happen regularly during development, so don't report a<br>"
                               "bug. This simply means Enlightenment needs new settings<br>"
                               "data by default for usable functionality that your old<br>"
                               "settings simply lack. This new set of defaults will fix<br>"
                               "that by adding it in. You can re-configure things now to your<br>"
                               "liking. Sorry for the hiccup in your settings.<br>"));
          }
        /* config is too new? odd! suspect corruption? */
        else if (e_config->config_version > E_CONFIG_FILE_VERSION)
          {
             /* your config is too new - what the fuck??? */
             _e_config_free(e_config);
             e_config = NULL;
             reload = 1;
             ecore_timer_add(1.0, _e_config_cb_timer,
                             _("Your settings are NEWER than Enlightenment. This is very<br>"
                               "strange. This should not happen unless you downgraded<br>"
                               "Enlightenment or copied the settings from a place where<br>"
                               "a newer version of Enlightenment was running. This is bad and<br>"
                               "as a precaution your settings have been now restored to<br>"
                               "defaults. Sorry for the inconvenience.<br>"));
          }
        if (reload)
          {
             e_config_profile_del(e_config_profile_get());
             e_config_profile_set("default");
             e_config = e_config_domain_load("e", _e_config_edd);
          }
     }
   while (!e_config)
     {
        _e_config_edd_shutdown();
        _e_config_edd_init(EINA_TRUE);
        e_config = e_config_domain_load("e", _e_config_edd);
        /* I made a c&p error here and fucked the world, so this ugliness
         * will be my public mark of shame until E :/
         * -zmike, 2013
         */
        if (e_config)
          {
             /* this is essentially CONFIG_VERSION_CHECK(7) */
             INF("Performing config upgrade to %d.%d", 1, 7);
             _e_config_edd_shutdown();
             _e_config_edd_init(EINA_FALSE);
             break;
          }
#undef T
#undef D
        e_config_profile_set("default");
        if (!reload) e_config_profile_del(e_config_profile_get());
        if (getenv("E_CONF_RO")) e_config_save_block_set(1);
        e_error_message_show(_("Could not load e.cfg"));
        return;
     }

#define CONFIG_VERSION_CHECK(VERSION) \
  if (e_config->config_version - (E_CONFIG_FILE_EPOCH * 1000000) < (VERSION))

#define CONFIG_VERSION_UPDATE_INFO(VERSION) \
  INF("Performing config upgrade for %d.%d", E_CONFIG_FILE_EPOCH, VERSION);

   if (e_config->config_version < E_CONFIG_FILE_VERSION)
     {
        CONFIG_VERSION_CHECK(18)
          {
             ;
          }
        CONFIG_VERSION_CHECK(19)
          {
             CONFIG_VERSION_UPDATE_INFO(19);

             /* set (400, 25) as the default values of repeat delay, rate */
             e_config->keyboard.repeat_delay = 400;
             e_config->keyboard.repeat_rate = 25;
          }
     }

   e_config->config_version = E_CONFIG_FILE_VERSION;

   /* limit values so they are sane */
   E_CONFIG_LIMIT(e_config->framerate, 1.0, 200.0);
   E_CONFIG_LIMIT(e_config->priority, 0, 19);
   E_CONFIG_LIMIT(e_config->zone_desks_x_count, 1, 64);
   E_CONFIG_LIMIT(e_config->zone_desks_y_count, 1, 64);
   E_CONFIG_LIMIT(e_config->use_e_policy, 0, 1);
   E_CONFIG_LIMIT(e_config->use_softkey_service, 0, 1);
   E_CONFIG_LIMIT(e_config->use_softkey, 0, 1);
   E_CONFIG_LIMIT(e_config->softkey_size, 0, 42);
   E_CONFIG_LIMIT(e_config->window_placement_policy, E_WINDOW_PLACEMENT_SMART, E_WINDOW_PLACEMENT_MANUAL);
   E_CONFIG_LIMIT(e_config->focus_policy, 0, 2);
   E_CONFIG_LIMIT(e_config->focus_policy_ext, 0, 1);
   E_CONFIG_LIMIT(e_config->focus_setting, 0, 3);
   E_CONFIG_LIMIT(e_config->always_click_to_raise, 0, 1);
   E_CONFIG_LIMIT(e_config->always_click_to_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->use_auto_raise, 0, 1);
   E_CONFIG_LIMIT(e_config->maximize_policy, E_MAXIMIZE_FULLSCREEN, E_MAXIMIZE_DIRECTION);
   E_CONFIG_LIMIT(e_config->allow_manip, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_if_close_not_possible, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_process, 0, 1);
   E_CONFIG_LIMIT(e_config->kill_timer_wait, 0.0, 120.0);
   E_CONFIG_LIMIT(e_config->ping_clients, 0, 1);
   E_CONFIG_LIMIT(e_config->use_e_cursor, 0, 1);
   E_CONFIG_LIMIT(e_config->cursor_size, 0, 1024);
   E_CONFIG_LIMIT(e_config->dpms_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_standby_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_suspend_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_off_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->dpms_standby_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->dpms_suspend_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->dpms_off_timeout, 30, 5400);
   E_CONFIG_LIMIT(e_config->mouse_hand, 0, 1);
   E_CONFIG_LIMIT(e_config->border_raise_on_mouse_action, 0, 1);
   E_CONFIG_LIMIT(e_config->border_raise_on_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->raise_on_revert_focus, 0, 1);
   E_CONFIG_LIMIT(e_config->screen_limits, 0, 2);
   E_CONFIG_LIMIT(e_config->ping_clients_interval, 16, 1024);
   E_CONFIG_LIMIT(e_config->border_keyboard.move.dx, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.move.dy, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.resize.dx, 1, 255);
   E_CONFIG_LIMIT(e_config->border_keyboard.resize.dy, 1, 255);
   E_CONFIG_LIMIT(e_config->show_cursor, 0, 1);
   E_CONFIG_LIMIT(e_config->xkb.delay_held_key_input_to_focus, 0,5000); // 5000(ms) == 5(s)
   E_CONFIG_LIMIT(e_config->keyboard.repeat_delay, -1, 1000); // 1 second
   E_CONFIG_LIMIT(e_config->keyboard.repeat_rate, -1, 1000); // 1 second
   E_CONFIG_LIMIT(e_config->screen_rotation_pre, 0, 270);
   E_CONFIG_LIMIT(e_config->screen_rotation_setting, 0, 270);
   E_CONFIG_LIMIT(e_config->screen_rotation_client_ignore, 0, 1);
   E_CONFIG_LIMIT(e_config->eom_enable, 0, 1);
   E_CONFIG_LIMIT(e_config->use_cursor_timer, 0, 1);
   E_CONFIG_LIMIT(e_config->sleep_for_dri, 0, 1);
   E_CONFIG_LIMIT(e_config->create_wm_ready, 0, 1);
   E_CONFIG_LIMIT(e_config->create_wm_start, 0, 1);
   E_CONFIG_LIMIT(e_config->comp_canvas_bg.r, 0, 255);
   E_CONFIG_LIMIT(e_config->comp_canvas_bg.g, 0, 255);
   E_CONFIG_LIMIT(e_config->comp_canvas_bg.b, 0, 255);
   E_CONFIG_LIMIT(e_config->comp_canvas_bg.a, 0, 255);
   E_CONFIG_LIMIT(e_config->comp_canvas_bg.opmode, 0, 11);
   E_CONFIG_LIMIT(e_config->delayed_load_idle_count, 0, 100);
   E_CONFIG_LIMIT(e_config->use_buffer_flush, 0, 1);
   E_CONFIG_LIMIT(e_config->use_desk_smart_obj, 0, 1);
   E_CONFIG_LIMIT(e_config->use_module_srv.qp, 0, 1);
   E_CONFIG_LIMIT(e_config->launchscreen_timeout, 7.0, 100.0);
   E_CONFIG_LIMIT(e_config->enable_conformant_ack, 0, 1);
   E_CONFIG_LIMIT(e_config->conformant_ack_timeout, 3.0, 100.0);
   E_CONFIG_LIMIT(e_config->calc_vis_without_effect, 0, 1);
   E_CONFIG_LIMIT(e_config->save_win_buffer, 0, 1);
   E_CONFIG_LIMIT(e_config->hold_prev_win_img, 0, 1);
   E_CONFIG_LIMIT(e_config->launchscreen_without_timer, 0, 1);
   E_CONFIG_LIMIT(e_config->log_type, 0, 255);
   E_CONFIG_LIMIT(e_config->rsm_buffer_release_mode, 0, 2);
   E_CONFIG_LIMIT(e_config->deiconify_approve, 0, 1);
   E_CONFIG_LIMIT(e_config->use_pp_zoom, 0, 1);
   E_CONFIG_LIMIT(e_config->priority_control, 0, 1);
   E_CONFIG_LIMIT(e_config->comp_canvas_norender.use, 0, 1);
   E_CONFIG_LIMIT(e_config->qp_use_bg_rect, 0, 1);
   E_CONFIG_LIMIT(e_config->qp_handler.use_alpha, 0, 1);
   E_CONFIG_LIMIT(e_config->qp_handler.alpha, 0, 255);
}

E_API int
e_config_save(void)
{
   _e_config_save_cb(NULL);
   return e_config_domain_save("e", _e_config_edd, e_config);
}

E_API void
e_config_save_queue(void)
{
   // TODO: add ecore_timer_add and call _e_config_save_cb
   //_e_config_save_cb(NULL) moved to be called in idle time
   _e_config_save_queued = 1;
}

E_API const char *
e_config_profile_get(void)
{
   return _e_config_profile;
}

E_API void
e_config_profile_set(const char *prof)
{
   eina_stringshare_replace(&_e_config_profile, prof);
   e_util_env_set("E_CONF_PROFILE", _e_config_profile);
}

E_API char *
e_config_profile_dir_get(const char *prof)
{
   char buf[PATH_MAX];

   e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof);
   if (ecore_file_is_dir(buf)) return strdup(buf);
   e_prefix_data_snprintf(buf, sizeof(buf), "data/config/%s", prof);
   if (ecore_file_is_dir(buf)) return strdup(buf);
   return NULL;
}

static int
_cb_sort_files(char *f1, char *f2)
{
   return strcmp(f1, f2);
}

E_API Eina_List *
e_config_profile_list(void)
{
   Eina_List *files;
   char buf[PATH_MAX], *p;
   Eina_List *flist = NULL;
   size_t len;

   len = e_user_dir_concat_static(buf, "config");
   if (len + 1 >= (int)sizeof(buf))
     return NULL;

   files = ecore_file_ls(buf);

   buf[len] = '/';
   len++;

   p = buf + len;
   len = sizeof(buf) - len;
   if (files)
     {
        char *file;

        files = eina_list_sort(files, 0, (Eina_Compare_Cb)_cb_sort_files);
        EINA_LIST_FREE(files, file)
          {
             if (eina_strlcpy(p, file, len) >= len)
               {
                  free(file);
                  continue;
               }
             if (ecore_file_is_dir(buf))
               flist = eina_list_append(flist, file);
             else
               free(file);
          }
     }
   len = e_prefix_data_concat_static(buf, "data/config");
   if (len + 1 >= sizeof(buf))
     return NULL;

   files = ecore_file_ls(buf);

   buf[len] = '/';
   len++;

   p = buf + len;
   len = sizeof(buf) - len;
   if (files)
     {
        char *file;
        files = eina_list_sort(files, 0, (Eina_Compare_Cb)_cb_sort_files);
        EINA_LIST_FREE(files, file)
          {
             if (eina_strlcpy(p, file, len) >= len)
               {
                  free(file);
                  continue;
               }
             if (ecore_file_is_dir(buf))
               {
                  const Eina_List *l;
                  const char *tmp;
                  EINA_LIST_FOREACH(flist, l, tmp)
                    if (!strcmp(file, tmp)) break;

                  if (!l) flist = eina_list_append(flist, file);
                  else free(file);
               }
             else
               free(file);
          }
     }
   return flist;
}

E_API void
e_config_profile_add(const char *prof)
{
   char buf[4096];
   if (e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof) >= sizeof(buf))
     return;
   ecore_file_mkdir(buf);
}

E_API void
e_config_profile_del(const char *prof)
{
   char buf[4096];
   if (e_user_dir_snprintf(buf, sizeof(buf), "config/%s", prof) >= sizeof(buf))
     return;
   ecore_file_recursive_rm(buf);
}

E_API void
e_config_save_block_set(int block)
{
   _e_config_save_block = block;
}

E_API int
e_config_save_block_get(void)
{
   return _e_config_save_block;
}

void *
_e_config_domain_user_load(const char *domain, E_Config_DD *edd)
{
   Eet_File *ef;
   char buf[4096];
   void *data = NULL;

   e_user_dir_snprintf(buf, sizeof(buf), "config/%s/%s.cfg",
                       _e_config_profile, domain);
   ef = eet_open(buf, EET_FILE_MODE_READ);
   if (ef)
     {
        data = eet_data_read(ef, edd, "config");
        _e_config_eet_close_handle(ef, buf);
        if (data)
          {
             ELOGF("CFG", "Loaded %s", NULL, buf);
             return data;
          }
     }

   ELOGF("CFG", "Could not load %s", NULL, buf);

   return data;
}

/**
 * Loads configurations from file located in the working profile
 * The configurations are stored in a struct declated by the
 * macros E_CONFIG_DD_NEW and E_CONFIG_<b>TYPE</b>
 *
 * @param domain of the configuration file.
 * @param edd to struct definition
 * @return returns allocated struct on success, if unable to find config returns null
 */
E_API void *
e_config_domain_load(const char *domain, E_Config_DD *edd)
{
   if (getenv("E_CONF_RO"))
     {
        return e_config_domain_system_load(domain, edd);
     }
   else
     {
        void *data = _e_config_domain_user_load(domain, edd);

        if (!data)
          {
             // load data from system and save it for next loading
             void *sys_data = e_config_domain_system_load(domain, edd);
             e_config_domain_save(domain, edd, sys_data);
             data = _e_config_domain_user_load(domain, edd);
          }
        if (data) return data;

        ELOGF("CFG", "Enlightenment has an error again while loading domain %s", NULL, domain);
     }

   return e_config_domain_system_load(domain, edd);
}

E_API void *
e_config_domain_system_load(const char *domain, E_Config_DD *edd)
{
   Eet_File *ef;
   char buf[4096];
   void *data = NULL;

   e_prefix_data_snprintf(buf, sizeof(buf), "data/config/%s/%s.cfg",
                          _e_config_profile, domain);
   ef = eet_open(buf, EET_FILE_MODE_READ);
   if (ef)
     {
        data = eet_data_read(ef, edd, "config");
        _e_config_eet_close_handle(ef, buf);
        if (data)
          {
             ELOGF("CFG", "Loaded %s", NULL, buf);
             return data;
          }

        ELOGF("CFG", "Could not read data %s", NULL, buf);
     }

   ELOGF("CFG", "Could not load %s", NULL, buf);

   return data;
}

#if 0
static void
_e_config_mv_error(const char *from, const char *to)
{
   E_Dialog *dia;
   char buf[8192];

   if (_e_config_error_dialog) return;

   dia = e_dialog_new(NULL, "E", "_sys_error_logout_slow");
   EINA_SAFETY_ON_NULL_RETURN(dia);

   e_dialog_title_set(dia, _("Enlightenment Settings Write Problems"));
   e_dialog_icon_set(dia, "dialog-error", 64);
   snprintf(buf, sizeof(buf),
            _("Enlightenment has had an error while moving config files<br>"
              "from:<br>"
              "%s<br>"
              "<br>"
              "to:<br>"
              "%s<br>"
              "<br>"
              "The rest of the write has been aborted for safety.<br>"),
            from, to);
   e_dialog_text_set(dia, buf);
   e_dialog_button_add(dia, _("OK"), NULL, NULL, NULL);
   e_dialog_button_focus_num(dia, 0);
   //elm_win_center(dia->win, 1, 1);
   e_object_del_attach_func_set(E_OBJECT(dia),
                                _e_config_error_dialog_cb_delete);
   e_dialog_show(dia);
   _e_config_error_dialog = dia;
}
#endif

E_API int
e_config_profile_save(void)
{
#if 0
   Eet_File *ef;
   char buf[4096], buf2[4096];
   int ok = 0;

   /* FIXME: check for other sessions fo E running */
   e_user_dir_concat_static(buf, "config/profile.cfg");
   e_user_dir_concat_static(buf2, "config/profile.cfg.tmp");

   ef = eet_open(buf2, EET_FILE_MODE_WRITE);
   if (ef)
     {
        ok = eet_write(ef, "config", _e_config_profile,
                       strlen(_e_config_profile), 0);
        if (_e_config_eet_close_handle(ef, buf2))
          {
             Eina_Bool ret = EINA_TRUE;

             if (_e_config_revisions > 0)
               {
                  int i;
                  char bsrc[4096], bdst[4096];

                  for (i = _e_config_revisions; i > 1; i--)
                    {
                       e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/profile.%i.cfg", i - 1);
                       e_user_dir_snprintf(bdst, sizeof(bdst), "config/profile.%i.cfg", i);
                       if ((ecore_file_exists(bsrc)) &&
                           (ecore_file_size(bsrc)))
                         {
                            ret = ecore_file_mv(bsrc, bdst);
                            if (!ret)
                              {
                                 _e_config_mv_error(bsrc, bdst);
                                 break;
                              }
                         }
                    }
                  if (ret)
                    {
                       e_user_dir_snprintf(bsrc, sizeof(bsrc), "config/profile.cfg");
                       e_user_dir_snprintf(bdst, sizeof(bdst), "config/profile.1.cfg");
                       ret = ecore_file_mv(bsrc, bdst);
//                       if (!ret)
//                          _e_config_mv_error(bsrc, bdst);
                    }
               }
             ret = ecore_file_mv(buf2, buf);
             if (!ret) _e_config_mv_error(buf2, buf);
          }
        ecore_file_unlink(buf2);
     }
   return ok;
#else
   return 1;
#endif
}

/**
 * Saves configurations to file located in the working profile
 * The configurations are read from a struct declated by the
 * macros E_CONFIG_DD_NEW and E_CONFIG_<b>TYPE</b>
 *
 * @param domain  name of the configuration file.
 * @param edd pointer to struct definition
 * @param data struct to save as configuration file
 * @return 1 if save success, 0 on failure
 */
E_API int
e_config_domain_save(const char *domain, E_Config_DD *edd, const void *data)
{
   Eet_File *ef;
   char buf[4096], buf2[4096];
   int ok = 0, ret;
   size_t len, len2;

   if (_e_config_save_block) return 0;
   /* FIXME: check for other sessions fo E running */
   len = e_user_dir_snprintf(buf, sizeof(buf), "config/%s", _e_config_profile);
   if (len + 1 >= sizeof(buf)) return 0;

   ecore_file_mkdir(buf);

   buf[len] = '/';
   len++;

   len2 = eina_strlcpy(buf + len, domain, sizeof(buf) - len);
   if (len2 + sizeof(".cfg") >= sizeof(buf) - len) return 0;

   len += len2;

   memcpy(buf + len, ".cfg", sizeof(".cfg"));
   len += sizeof(".cfg") - 1;

   if (len + sizeof(".tmp") >= sizeof(buf)) return 0;
   memcpy(buf2, buf, len);
   memcpy(buf2 + len, ".tmp", sizeof(".tmp"));

   ef = eet_open(buf2, EET_FILE_MODE_WRITE);
   if (ef)
     {
        ok = eet_data_write(ef, edd, "config", data, 1);
        if (_e_config_eet_close_handle(ef, buf2))
          {
             ret = ecore_file_mv(buf2, buf);
             if (!ret)
               ERR("*** Error saving config. ***");
          }
        ecore_file_unlink(buf2);
     }
   return ok;
}

E_API void
e_config_mode_changed(void)
{
   ecore_event_add(E_EVENT_CONFIG_MODE_CHANGED, NULL, NULL, NULL);
}

/* local subsystem functions */
static void
_e_config_save_cb(void *data EINA_UNUSED)
{
   e_config_profile_save();
   e_module_save_all();
   e_config_domain_save("e", _e_config_edd, e_config);
}

static void
_e_config_free(E_Config *ecf)
{
   E_Config_Module *em;
   E_Config_Env_Var *evr;
   E_Config_Desktop_Window_Profile *wp;
   E_Config_Policy_Desk *pd;
   E_Config_Socket_Access *sa;
   E_Config_Aux_Hint_Supported *auxhint;

   if (!ecf) return;

   EINA_LIST_FREE(ecf->desktop_window_profiles, wp)
     {
        eina_stringshare_del(wp->profile);
        E_FREE(wp);
     }

   eina_stringshare_del(ecf->xkb.default_model);
   eina_stringshare_del(ecf->xkb.default_rmlvo.rules);
   eina_stringshare_del(ecf->xkb.default_rmlvo.model);
   eina_stringshare_del(ecf->xkb.default_rmlvo.layout);
   eina_stringshare_del(ecf->xkb.default_rmlvo.variant);
   eina_stringshare_del(ecf->xkb.default_rmlvo.options);

   EINA_LIST_FREE(ecf->modules, em)
     {
        if (em->name) eina_stringshare_del(em->name);
        E_FREE(em);
     }
   if (ecf->comp_shadow_file) eina_stringshare_del(ecf->comp_shadow_file);
   if (ecf->desktop_default_background) eina_stringshare_del(ecf->desktop_default_background);
   if (ecf->desktop_default_name) eina_stringshare_del(ecf->desktop_default_name);
   if (ecf->desktop_default_window_profile) eina_stringshare_del(ecf->desktop_default_window_profile);
   if (ecf->theme_default_border_style) eina_stringshare_del(ecf->theme_default_border_style);
   EINA_LIST_FREE(ecf->env_vars, evr)
     {
        if (evr->var) eina_stringshare_del(evr->var);
        if (evr->val) eina_stringshare_del(evr->val);
        E_FREE(evr);
     }

   EINA_LIST_FREE(ecf->policy_desks, pd)
      E_FREE(pd);

   eina_stringshare_del(ecf->launcher.title);
   eina_stringshare_del(ecf->launcher.clas);

   EINA_LIST_FREE(ecf->sock_accesses, sa)
     {
        eina_stringshare_del(sa->sock_access.name);
        eina_stringshare_del(sa->sock_access.owner);
        eina_stringshare_del(sa->sock_access.group);
        eina_stringshare_del(sa->sock_access.smack.name);
        eina_stringshare_del(sa->sock_access.smack.value);
        eina_stringshare_del(sa->sock_symlink_access.link_name);
        eina_stringshare_del(sa->sock_symlink_access.owner);
        eina_stringshare_del(sa->sock_symlink_access.group);
        eina_stringshare_del(sa->sock_symlink_access.smack.name);
        eina_stringshare_del(sa->sock_symlink_access.smack.value);
        E_FREE(sa);
     }

   EINA_LIST_FREE(ecf->aux_hint_supported, auxhint)
     {
        eina_stringshare_del(auxhint->name);
        E_FREE(auxhint);
     }

   E_FREE(ecf);
}

static Eina_Bool
_e_config_cb_timer(void *data)
{
   e_util_dialog_show(_("Settings Upgraded"), "%s", (char *)data);
   return 0;
}

static int
_e_config_eet_close_handle(Eet_File *ef, char *file)
{
   Eet_Error err;
   char *erstr = NULL;

   err = eet_close(ef);
   switch (err)
     {
      case EET_ERROR_NONE:
        /* all good - no error */
        break;

      case EET_ERROR_BAD_OBJECT:
        erstr = _("The EET file handle is bad.");
        break;

      case EET_ERROR_EMPTY:
        erstr = _("The file data is empty.");
        break;

      case EET_ERROR_NOT_WRITABLE:
        erstr = _("The file is not writable. Perhaps the disk is read-only<br>or you lost permissions to your files.");
        break;

      case EET_ERROR_OUT_OF_MEMORY:
        erstr = _("Memory ran out while preparing the write.<br>Please free up memory.");
        break;

      case EET_ERROR_WRITE_ERROR:
        erstr = _("This is a generic error.");
        break;

      case EET_ERROR_WRITE_ERROR_FILE_TOO_BIG:
        erstr = _("The settings file is too large.<br>It should be very small (a few hundred KB at most).");
        break;

      case EET_ERROR_WRITE_ERROR_IO_ERROR:
        erstr = _("You have I/O errors on the disk.<br>Maybe it needs replacing?");
        break;

      case EET_ERROR_WRITE_ERROR_OUT_OF_SPACE:
        erstr = _("You ran out of space while writing the file.");
        break;

      case EET_ERROR_WRITE_ERROR_FILE_CLOSED:
        erstr = _("The file was closed while writing.");
        break;

      case EET_ERROR_MMAP_FAILED:
        erstr = _("Memory-mapping (mmap) of the file failed.");
        break;

      case EET_ERROR_X509_ENCODING_FAILED:
        erstr = _("X509 Encoding failed.");
        break;

      case EET_ERROR_SIGNATURE_FAILED:
        erstr = _("Signature failed.");
        break;

      case EET_ERROR_INVALID_SIGNATURE:
        erstr = _("The signature was invalid.");
        break;

      case EET_ERROR_NOT_SIGNED:
        erstr = _("Not signed.");
        break;

      case EET_ERROR_NOT_IMPLEMENTED:
        erstr = _("Feature not implemented.");
        break;

      case EET_ERROR_PRNG_NOT_SEEDED:
        erstr = _("PRNG was not seeded.");
        break;

      case EET_ERROR_ENCRYPT_FAILED:
        erstr = _("Encryption failed.");
        break;

      case EET_ERROR_DECRYPT_FAILED:
        erstr = _("Decryption failed.");
        break;

      default: /* if we get here eet added errors we don't know */
        erstr = _("The error is unknown to Enlightenment.");
        break;
     }
   if (erstr)
     {
        // TODO - yigl
#if 0
        /* delete any partially-written file */
        ecore_file_unlink(file);
        /* only show dialog for first error - further ones are likely */
        /* more of the same error */
        if (!_e_config_error_dialog)
          {
             E_Dialog *dia;

             dia = e_dialog_new(NULL, "E", "_sys_error_logout_slow");
             if (dia)
               {
                  char buf[8192];

                  e_dialog_title_set(dia, _("Enlightenment Settings Write Problems"));
                  e_dialog_icon_set(dia, "dialog-error", 64);
                  snprintf(buf, sizeof(buf),
                           _("Enlightenment has had an error while writing<br>"
                             "its config file.<br>"
                             "%s<br>"
                             "<br>"
                             "The file where the error occurred was:<br>"
                             "%s<br>"
                             "<br>"
                             "This file has been deleted to avoid corrupt data.<br>"),
                           erstr, file);
                  e_dialog_text_set(dia, buf);
                  e_dialog_button_add(dia, _("OK"), NULL, NULL, NULL);
                  e_dialog_button_focus_num(dia, 0);
                  //elm_win_center(dia->win, 1, 1);
                  e_object_del_attach_func_set(E_OBJECT(dia),
                                               _e_config_error_dialog_cb_delete);
                  e_dialog_show(dia);
                  _e_config_error_dialog = dia;
               }
          }
        return 0;
#else
        ELOGF("CFG", "EET ERROR %s", NULL, erstr);
        return 0;
#endif
     }
   return 1;
}
