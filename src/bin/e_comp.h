#ifdef E_TYPEDEFS
typedef struct _E_Comp                       E_Comp;
typedef struct _E_Comp_Wl_Client_Data        E_Comp_Client_Data;
typedef struct _E_Comp_Wl_Data               E_Comp_Wl_Data;
typedef struct _E_Comp_Connected_Client_Info E_Comp_Connected_Client_Info;

# define E_COMP_TYPE (int) 0xE0b01003

# define E_LAYER_COUNT 31
# define E_CLIENT_LAYER_COUNT 19

typedef enum _E_Layer
{
   E_LAYER_BOTTOM = -100,
   E_LAYER_BG = -1, // zone bg stuff
   E_LAYER_DESKTOP = 0, // desktop objects: fileman, gadgets, shelves
   E_LAYER_DESKTOP_TOP = 10, // raised desktop objects: gadgets
   E_LAYER_CLIENT_DESKTOP = 100, //shelves
   E_LAYER_CLIENT_BELOW = 150,
   E_LAYER_CLIENT_NORMAL = 200,
   E_LAYER_CLIENT_ABOVE = 250,
   E_LAYER_CLIENT_EDGE = 300,
   E_LAYER_CLIENT_FULLSCREEN = 350,
   E_LAYER_CLIENT_EDGE_FULLSCREEN = 400,
   E_LAYER_CLIENT_POPUP = 450,
   E_LAYER_CLIENT_TOP = 500,
   E_LAYER_CLIENT_DRAG = 550,
   E_LAYER_CLIENT_PRIO = 600,
   E_LAYER_CLIENT_NOTIFICATION_LOW = 650,
   E_LAYER_CLIENT_NOTIFICATION_NORMAL = 700,
   E_LAYER_CLIENT_NOTIFICATION_HIGH = 750,
   E_LAYER_CLIENT_NOTIFICATION_TOP = 800,
   E_LAYER_CLIENT_ALERT_LOW = 850,
   E_LAYER_CLIENT_ALERT = 900,
   E_LAYER_CLIENT_ALERT_HIGH = 950,
   E_LAYER_CLIENT_CURSOR = 990,
   E_LAYER_POPUP = 999, // popups
   E_LAYER_EFFECT = 1999,
   E_LAYER_DESK_OBJECT_BELOW = 3000,
   E_LAYER_DESK_OBJECT = 3100,
   E_LAYER_DESK_OBJECT_ABOVE = 3200,
   E_LAYER_MENU = 5000, // menus
   E_LAYER_DESKLOCK = 9999, // desklock
   E_LAYER_MAX = 32767 // EVAS_LAYER_MAX
} E_Layer;

#else
# ifndef E_COMP_H
#  define E_COMP_H

# include "e_comp_cfdata.h"

extern E_API int E_EVENT_COMPOSITOR_DISABLE;
extern E_API int E_EVENT_COMPOSITOR_ENABLE;
extern E_API int E_EVENT_COMPOSITOR_FPS_UPDATE;

typedef void (*E_Comp_Cb)(void);

typedef struct _E_Comp_Hook E_Comp_Hook;

typedef enum _E_Comp_Hook_Point
{
   E_COMP_HOOK_PREPARE_PLANE,
   E_COMP_HOOK_LAST
} E_Comp_Hook_Point;

typedef void (*E_Comp_Hook_Cb)(void *data, E_Comp *c);

struct _E_Comp_Hook
{
   EINA_INLIST;
   E_Comp_Hook_Point hookpoint;
   E_Comp_Hook_Cb    func;
   void               *data;
   unsigned char       delete_me : 1;
};

struct _E_Comp
{
   E_Object e_obj_inherit;
   int w, h;

   Ecore_Window  win; // input overlay
   Ecore_Window  root;
   Ecore_Evas     *ee;
   Ecore_Window  ee_win;
   //Evas_Object    *elm;
   Evas           *evas;
   Evas_Object    *bg_blank_object;
   Eina_List      *zones;
   E_Pointer      *pointer;
   Eina_List *clients;
   unsigned int new_clients;

   Eina_List *pre_render_cbs; /* E_Comp_Cb */

   E_Comp_Wl_Data *wl_comp_data;

   E_Pixmap_Type comp_type; //for determining X/Wayland/

   Eina_Stringshare *name;
   struct {
      Evas_Object *obj;
      Eina_Inlist *clients; /* E_Client, bottom to top */
      unsigned int clients_count;
   } layers[E_LAYER_COUNT];

   struct
   {
      Evas_Object *rect;
      Evas_Object *obj;
      Ecore_Event_Handler *key_handler;
      E_Comp_Object_Autoclose_Cb del_cb;
      E_Comp_Object_Key_Cb key_cb;
      void *data;
   } autoclose;

   E_Comp_Screen *e_comp_screen;

   Eina_List *debug_rects;
   Eina_List *ignore_wins;

   Eina_List      *updates;
   Eina_List      *post_updates;
   Ecore_Animator *render_animator;
   Ecore_Job      *update_job;
   Evas_Object    *fps_bg;
   Evas_Object    *fps_fg;
   Ecore_Job      *screen_job;
   int             animating;
   double          frametimes[122];
   int             frameskip;
   double          fps;

   Ecore_Window    block_win;
   int             block_count; //number of times block window has been requested

   Ecore_Window    cm_selection; //FIXME: move to comp_x ?
   E_Client       *nocomp_ec;

   Ecore_Timer    *nocomp_delay_timer;
   Ecore_Timer    *nocomp_override_timer;
   int             nocomp_override; //number of times hwc override has been requested
   Eina_Bool       nocomp : 1; // TODO: remove it from E_Comp!!
   Eina_Bool       hwc : 1;
   Eina_Bool       hwc_reuse_cursor_buffer;
   Eina_Bool       hwc_sync_mode_change;
   Eina_Bool       hwc_use_detach;
   Eina_Bool       hwc_ignore_primary;
   Eina_Bool       use_native_type_buffer : 1; // use native buffer type
   Eina_Bool       avoid_afill; // avoid to fill the alpha channel as 0xff. that means filling 0x00.

   int depth;
   unsigned int    input_key_grabs;
   unsigned int    input_mouse_grabs;

   E_Comp_Cb        grab_cb;
   E_Comp_Cb        bindings_grab_cb;
   E_Comp_Cb        bindings_ungrab_cb;

   Eina_Bool       gl : 1;
   Eina_Bool       grabbed : 1;
   Eina_Bool       saver : 1;
   Eina_Bool       shape_queue_blocked : 1;
   Eina_Bool       calc_fps : 1;
   Eina_Bool       canvas_render_delayed : 1;

   Eina_List      *connected_clients;
   Eina_List      *launchscrns; // list of dummy clients for launchscreen image.

   Eina_List      *render_list;

   int norender;

   void           (*func_memory_dump) (void);
};

struct _E_Comp_Connected_Client_Info
{
   const char *name;
   int pid;
   int uid;
   int gid;
};

typedef enum
{
   E_COMP_ENGINE_NONE = 0,
   E_COMP_ENGINE_SW = 1,
   E_COMP_ENGINE_GL = 2
} E_Comp_Engine;

extern E_API E_Comp *e_comp;
extern E_API E_Comp_Wl_Data *e_comp_wl;

EINTERN Eina_Bool e_comp_init(void);
E_API E_Comp *e_comp_new(void);
E_API int e_comp_internal_save(void);
EINTERN int e_comp_shutdown(void);
E_API void e_comp_deferred_job(void);
E_API void e_comp_render_queue(void);
E_API void e_comp_client_post_update_add(E_Client *ec);
E_API E_Comp_Config *e_comp_config_get(void);
E_API const Eina_List *e_comp_list(void);
E_API void e_comp_shadows_reset(void);
E_API Ecore_Window e_comp_top_window_at_xy_get(Evas_Coord x, Evas_Coord y);
E_API void e_comp_util_wins_print(void);
E_API void e_comp_ignore_win_add(E_Pixmap_Type type, Ecore_Window win);
E_API void e_comp_ignore_win_del(E_Pixmap_Type type, Ecore_Window win);
E_API Eina_Bool e_comp_ignore_win_find(Ecore_Window win);
E_API void e_comp_override_del(void);
E_API void e_comp_override_add(void);
E_API E_Comp *e_comp_find_by_window(Ecore_Window win);
E_API void e_comp_override_timed_pop(void);
E_API unsigned int e_comp_e_object_layer_get(const E_Object *obj);
E_API void e_comp_layer_name_get(unsigned int layer, char *buff, int buff_size);
E_API Eina_Bool e_comp_grab_input(Eina_Bool mouse, Eina_Bool kbd);
E_API void e_comp_ungrab_input(Eina_Bool mouse, Eina_Bool kbd);
E_API void e_comp_gl_set(Eina_Bool set);
E_API Eina_Bool e_comp_gl_get(void);

E_API void e_comp_button_bindings_grab_all(void);
E_API void e_comp_button_bindings_ungrab_all(void);
E_API void e_comp_client_redirect_toggle(E_Client *ec);
E_API Eina_Bool e_comp_util_object_is_above_nocomp(Evas_Object *obj);

E_API Eina_Bool e_comp_util_kbd_grabbed(void);
E_API Eina_Bool e_comp_util_mouse_grabbed(void);

E_API void e_comp_client_override_del(E_Client *ec);
E_API void e_comp_client_override_add(E_Client *ec);

E_API void e_comp_client_render_list_add(E_Client *ec);

static inline Eina_Bool
e_comp_util_client_is_fullscreen(const E_Client *ec)
{
   if ((!ec->visible) || (ec->input_only))
     return EINA_FALSE;
   return ((ec->client.x == 0) && (ec->client.y == 0) &&
       ((ec->client.w) >= e_comp->w) &&
       ((ec->client.h) >= e_comp->h) &&
       (!ec->argb) && (!ec->shaped)
       );
}

E_API void e_comp_post_update_add(E_Client *ec);
E_API void e_comp_post_update_purge(E_Client *ec);

E_API E_Comp_Hook *e_comp_hook_add(E_Comp_Hook_Point hookpoint, E_Comp_Hook_Cb func, const void *data);
E_API void e_comp_hook_del(E_Comp_Hook *ph);
EINTERN void e_comp_hook_call(E_Comp_Hook_Point hookpoint, void *data EINA_UNUSED);

EINTERN Eina_Bool e_comp_is_on_overlay(E_Client *ec);
EINTERN E_Zone *e_comp_zone_find(const char *output_id);
E_API Eina_List *e_comp_vis_ec_list_get(E_Zone *zone); // visible ec list sorted by z order

E_API Eina_Bool e_comp_socket_init(const char *name);

EINTERN void      e_comp_hwc_deactive_set(Eina_Bool set);
EINTERN Eina_Bool e_comp_hwc_deactive_get(void);
EINTERN void      e_comp_hwc_multi_plane_set(Eina_Bool set);
EINTERN Eina_Bool e_comp_hwc_multi_plane_get(void);
EINTERN void      e_comp_hwc_client_end(E_Client *ec, const char *location);
E_API   void      e_comp_hwc_end(const char *location);

#endif
#endif
