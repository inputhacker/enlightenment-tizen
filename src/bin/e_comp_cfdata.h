#ifdef E_TYPEDEFS
typedef struct _E_Comp_Config E_Comp_Config;
typedef struct _E_Comp_Match  E_Comp_Match;
#else
#ifndef E_COMP_CFDATA_H
#define E_COMP_CFDATA_H

#define E_COMP_VERSION 1
struct _E_Comp_Config
{
   int           version;
   const char   *shadow_style;
   const char   *effect_file; // effect edj path for effect object
   const char   *effect_style; // name of effect group to apply
   const char   *depth_in_style; // name of effect group to apply for depth in type
   const char   *bg_effect_style; // name of effect group for background
   int           engine;
   int           max_unmapped_time;
   int           min_unmapped_time;
   int           fps_average_range;
   unsigned char fps_corner;
   unsigned char fps_show;
   unsigned char indirect;
   unsigned char texture_from_pixmap;
   unsigned char lock_fps;
   unsigned char grab;
   unsigned char vsync;
   unsigned char swap_mode;
   unsigned char send_flush;
   unsigned char send_dump;
   unsigned char nocomp_fs;
   unsigned char nocomp_use_timer;
   double        nocomp_begin_timeout;
   unsigned char hwc;
   unsigned char hwc_use_multi_plane; // 0: regards hwc layer is single. comp or nocomp switches only, 1: regards hwc layer is multiple
   unsigned char hwc_deactive; // 0: run hwc policy if capable of, 1: not to run hwc policy
   unsigned char hwc_reuse_cursor_buffer; // 0: none, 1: reuse hwc cursor buffer when cursor image is changed
   unsigned char hwc_sync_mode_change; // 0: none, 1: synchronize scanout buffer when hwc mode change in reserved memory plane
   unsigned char hwc_ignore_primary; // 0: none, 1: hwc use plane has lowest zpos and support rgb to default ui layer
   unsigned char smooth_windows;
   unsigned char nofade;
   double        first_draw_delay;
   Eina_Bool skip_first_damage;
   Eina_Bool disable_screen_effects;
   Eina_Bool enable_advanced_features;
   const char   *launch_file;
   // the following options add the "/fast" suffix to the normal groups
   Eina_Bool fast_popups;
   Eina_Bool fast_borders;
   Eina_Bool fast_menus;
   Eina_Bool fast_overrides;
   Eina_Bool fast_objects;

   struct
   {
      Eina_Bool disable_popups;
      Eina_List *popups;    // used for e popups
      Eina_Bool disable_borders;
      Eina_List *borders;    // used for borders
      Eina_Bool disable_overrides;
      Eina_List *overrides;    // used for client menus, tooltips etc.
      Eina_Bool disable_menus;
      Eina_List *menus;    // used for e menus
      Eina_Bool disable_objects;
      Eina_List *objects;    // used for e objects which are not popups or menus
   } match;

   unsigned char dim_rect_enable;
   unsigned char dim_rect_effect;
   Eina_Bool input_log_enable;
};

struct _E_Comp_Match
{
   const char *title; // glob - used for borders, NULL if not to be used
   const char *name; // glob - used for borders, overrides, popups, NULL if not to be used
   const char *clas; // glob - used for borders, overrides, NULL if not to be used
   const char *role; // glob - used for borders

   const char *shadow_style; // shadow style to use
   const char *visibility_effect; // effect to use when showing and hiding

   int         primary_type; // Ecore_X_Window_Type - used for borders, overrides, first one found - ECORE_X_WINDOW_TYPE_UNKNOWN if not to be used
   char        borderless; // used for borders, 0 == dont use, 1 == borderless, -1 == not borderless
   char        dialog; // used for borders, 0 == don't use, 1 == dialog, -1 == not dialog
   char        accepts_focus; // used for borders, 0 == don't use, 1 == accepts focus, -1 == does not accept focus
   char        vkbd; // used for borders, 0 == don't use, 1 == is vkbd, -1 == not vkbd
   char        argb; // used for borders, overrides, popups, menus, 0 == don't use, 1 == is argb, -1 == not argb
   char        fullscreen; // used for borders, 0 == don't use, 1 == is fullscreen, -1 == not fullscreen
   char        modal; // used for borders, 0 == don't use, 1 == is modal, -1 == not modal
   char        focus; // used for setting focus state (on popups): 1 is focused, unset is use regular logic
   char        urgent; // used for setting urgent state (on popups): 1 is urgent, unset is use regular logic
   char        no_shadow; // set whether shadow is disabled
};

E_API void           e_comp_cfdata_edd_init(E_Config_DD **conf_edd, E_Config_DD **match_edd);
E_API E_Comp_Config *e_comp_cfdata_config_new(void);
E_API void           e_comp_cfdata_config_free(E_Comp_Config *cfg);
E_API void           e_comp_cfdata_match_free(E_Comp_Match *m);

#endif
#endif
