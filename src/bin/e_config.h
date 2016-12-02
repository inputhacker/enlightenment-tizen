#ifdef E_TYPEDEFS

#define E_CONFIG_LIMIT(v, min, max) {if (v >= max) v = max; else if (v <= min) v = min; }

typedef struct _E_Config                        E_Config;
typedef struct _E_Config_Module                 E_Config_Module;
typedef struct _E_Config_Desktop_Name           E_Config_Desktop_Name;
typedef struct _E_Config_Desktop_Window_Profile E_Config_Desktop_Window_Profile;
typedef struct _E_Config_Desktop_Background     E_Config_Desktop_Background;
typedef struct _E_Config_Env_Var                E_Config_Env_Var;
typedef struct _E_Config_Client_Type            E_Config_Client_Type;
typedef struct _E_Config_Policy_Desk            E_Config_Policy_Desk;
typedef struct _E_Config_Socket_Access          E_Config_Socket_Access;
typedef struct _E_Config_Aux_Hint_Supported     E_Config_Aux_Hint_Supported;

#else
#ifndef E_CONFIG_H
#define E_CONFIG_H

/* increment this whenever we change config enough that you need new
 * defaults for e to work.
 */
#define E_CONFIG_FILE_EPOCH      1
/* increment this whenever a new set of config values are added but the users
 * config doesn't need to be wiped - simply new values need to be put in
 */
#define E_CONFIG_FILE_GENERATION 19
#define E_CONFIG_FILE_VERSION    ((E_CONFIG_FILE_EPOCH * 1000000) + E_CONFIG_FILE_GENERATION)

struct _E_Config
{
   int         config_version;
   const char *desktop_default_background;
   const char *desktop_default_name;
   const char *desktop_default_window_profile;
   Eina_List  *desktop_backgrounds;
   Eina_List  *desktop_names;
   Eina_List  *desktop_window_profiles;
   double      framerate;
   int         priority;
   int         zone_desks_x_count;
   int         zone_desks_y_count;
   Eina_List  *modules;
   int         use_e_policy;
   struct
     {
        const char   *title;
        const char   *clas;
        unsigned int  type;
     } launcher;
   Eina_List  *policy_desks;
   int         use_softkey;
   int         softkey_size;
   int         window_placement_policy;
   int         focus_policy;
   int         focus_policy_ext;
   int         focus_setting;
   int         always_click_to_raise;
   int         always_click_to_focus;
   int         use_auto_raise;
   int         maximize_policy;
   int         allow_manip;
   int         kill_if_close_not_possible;
   int         kill_process;
   double      kill_timer_wait;
   int         ping_clients;
   int         use_e_cursor;
   int         cursor_size;
   struct
   {
      int move;
      int resize;
      int raise;
      int lower;
      int layer;
      int desktop;
      int iconify;
   } transient;
   int                       fullscreen_policy;
   int                       dpms_enable;
   int                       dpms_standby_enable;
   int                       dpms_suspend_enable;
   int                       dpms_off_enable;
   int                       dpms_standby_timeout;
   int                       dpms_suspend_timeout;
   int                       dpms_off_timeout;
   unsigned char             no_dpms_on_fullscreen;
   int                       mouse_hand;
   int                       border_raise_on_mouse_action;
   int                       border_raise_on_focus;
   int                       raise_on_revert_focus;
   const char               *theme_default_border_style;
   int                       screen_limits;
   int                       ping_clients_interval;
   struct
   {
      double timeout;
      struct
      {
         unsigned char dx;
         unsigned char dy;
      } move;
      struct
      {
         unsigned char dx;
         unsigned char dy;
      } resize;
   } border_keyboard;
   struct
   {
      double        min;
      double        max;
      double        factor;
      int           base_dpi;
      unsigned char use_dpi;
      unsigned char use_custom;
   } scale;
   unsigned char show_cursor;
   unsigned char idle_cursor;
   Eina_List    *env_vars;
   struct
   {
      int          only_label;
      const char  *default_model;
      Eina_Bool    dont_touch_my_damn_keyboard;
      Eina_Bool    use_cache;
      unsigned int delay_held_key_input_to_focus;
      struct
      {
          const char *rules;
          const char *model;
          const char *layout;
          const char *variant;
          const char *options;
      } default_rmlvo;
   } xkb;
   struct
   {
      int repeat_delay; // delay in milliseconds since key down until repeating starts
      int repeat_rate; // the rate of repeating keys in characters per second
   } keyboard;
   int           use_desktop_window_profile;
#ifdef _F_ZONE_WINDOW_ROTATION_
   unsigned char wm_win_rotation;
#endif
   int use_cursor_timer;
   int cursor_timer_interval;
   Eina_List *client_types;
   const char *comp_shadow_file;
   int                       sleep_for_dri;
   int                       create_wm_ready;
   int                       create_wm_start;
   struct
   {
      unsigned char r, g, b, a;
      int opmode;
   } comp_canvas_bg;
   int delayed_load_idle_count;
   Eina_Bool use_buffer_flush;
   Eina_List *sock_accesses;
   Eina_List *aux_hint_supported;
   struct
   {
      Eina_Bool qp; /* quickpanel */
   } use_module_srv;
};

struct _E_Config_Desklock_Background
{
   const char *file;
   Eina_Bool hide_logo;
};

struct _E_Config_Env_Var
{
   const char   *var;
   const char   *val;
   unsigned char unset;
};

struct _E_Config_Syscon_Action
{
   const char *action;
   const char *params;
   const char *button;
   const char *icon;
   int         is_main;
};

struct _E_Config_Module
{
   const char   *name;
   unsigned char enabled;
   unsigned char delayed;
   int           priority;
};

struct _E_Config_Desktop_Background
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *file;
};

struct _E_Config_Desktop_Name
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *name;
};

struct _E_Config_Desktop_Window_Profile
{
   int         zone;
   int         desk_x;
   int         desk_y;
   const char *profile;
};

struct _E_Event_Config_Icon_Theme
{
   const char *icon_theme;
};

struct _E_Config_Client_Type
{
   const char     *name; /* icccm.class_name */
   const char     *clas; /* icccm.class */
   E_Window_Type   window_type; /* Ecore_X_Window_Type / E_Window_Type */
   int             client_type; /* E_Client_Type */
};

struct _E_Config_Policy_Desk
{
   unsigned int zone_num;
   int x, y;
   int enable;
};

struct _E_Config_Socket_Access
{
   struct
   {
      unsigned char use;
      const char   *name;
      const char   *owner;
      const char   *group;
      unsigned int  permissions;
      struct
      {
         unsigned char use;
         const char   *name;
         const char   *value;
         int           flags;
      } smack;
   } sock_access;
   struct
   {
      unsigned char use;
      const char   *link_name;
      const char   *owner;
      const char   *group;
      struct
      {
         const char   *name;
         const char   *value;
         int           flags;
      } smack;
   } sock_symlink_access;
};

struct _E_Config_Aux_Hint_Supported
{
   const char *name;
};

EINTERN int                   e_config_init(void);
EINTERN int                   e_config_shutdown(void);

E_API void                     e_config_load(void);

E_API int                      e_config_save(void);
E_API void                     e_config_save_queue(void);

E_API const char              *e_config_profile_get(void);
E_API char                    *e_config_profile_dir_get(const char *prof);
E_API void                     e_config_profile_set(const char *prof);
E_API Eina_List               *e_config_profile_list(void);
E_API void                     e_config_profile_add(const char *prof);
E_API void                     e_config_profile_del(const char *prof);

E_API void                     e_config_save_block_set(int block);
E_API int                      e_config_save_block_get(void);

E_API void                    *e_config_domain_load(const char *domain, E_Config_DD *edd);
E_API void                    *e_config_domain_system_load(const char *domain, E_Config_DD *edd);
E_API int                      e_config_profile_save(void);
E_API int                      e_config_domain_save(const char *domain, E_Config_DD *edd, const void *data);

E_API void                     e_config_mode_changed(void);

extern E_API E_Config *e_config;

extern E_API int E_EVENT_CONFIG_MODE_CHANGED;

#endif
#endif
