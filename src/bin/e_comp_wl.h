#ifdef E_TYPEDEFS
#else
# ifndef E_COMP_WL_H
#  define E_COMP_WL_H

/* NB: Turn off shadow warnings for Wayland includes */
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wshadow"
#  define WL_HIDE_DEPRECATED
#  include <wayland-server.h>
#  pragma GCC diagnostic pop

#  include <xkbcommon/xkbcommon.h>

#  ifdef __linux__
#   include <linux/input.h>
#  else
#   define BTN_LEFT 0x110
#   define BTN_RIGHT 0x111
#   define BTN_MIDDLE 0x112
#   define BTN_SIDE 0x113
#   define BTN_EXTRA 0x114
#   define BTN_FORWARD 0x115
#   define BTN_BACK 0x116
#  endif

#  define container_of(ptr, type, member) \
   ({ \
      const __typeof__( ((type *)0)->member ) *__mptr = (ptr); \
      (type *)( (char *)__mptr - offsetof(type,member) ); \
   })

#include <Evas_GL.h>
#include <tbm_surface.h>

#define E_COMP_WL_TOUCH_MAX 10

typedef struct _E_Comp_Wl_Aux_Hint  E_Comp_Wl_Aux_Hint;
typedef struct _E_Comp_Wl_Buffer E_Comp_Wl_Buffer;
typedef struct _E_Comp_Wl_Buffer_Ref E_Comp_Wl_Buffer_Ref;
typedef struct _E_Comp_Wl_Buffer_Viewport E_Comp_Wl_Buffer_Viewport;
typedef struct _E_Comp_Wl_Subsurf_Data E_Comp_Wl_Subsurf_Data;
typedef struct _E_Comp_Wl_Surface_State E_Comp_Wl_Surface_State;
typedef struct _E_Comp_Wl_Client_Data E_Comp_Wl_Client_Data;
typedef struct _E_Comp_Wl_Data E_Comp_Wl_Data;
typedef struct _E_Comp_Wl_Output E_Comp_Wl_Output;
typedef struct _E_Comp_Wl_Hook E_Comp_Wl_Hook;
typedef struct _E_Comp_Wl_Intercept_Hook E_Comp_Wl_Intercept_Hook;


typedef enum _E_Comp_Wl_Buffer_Type
{
   E_COMP_WL_BUFFER_TYPE_NONE = 0,
   E_COMP_WL_BUFFER_TYPE_SHM = 1,
   E_COMP_WL_BUFFER_TYPE_NATIVE = 2,
   E_COMP_WL_BUFFER_TYPE_VIDEO = 3,
   E_COMP_WL_BUFFER_TYPE_TBM = 4,
} E_Comp_Wl_Buffer_Type;

typedef enum _E_Comp_Wl_Hook_Point
{
   E_COMP_WL_HOOK_SHELL_SURFACE_READY,
   E_COMP_WL_HOOK_SUBSURFACE_CREATE,
   E_COMP_WL_HOOK_BUFFER_CHANGE,
   E_COMP_WL_HOOK_CLIENT_REUSE,
   E_COMP_WL_HOOK_LAST,
} E_Comp_Wl_Hook_Point;

typedef enum _E_Comp_Wl_Sh_Surf_Role
{
   E_COMP_WL_SH_SURF_ROLE_NONE = 0,
   E_COMP_WL_SH_SURF_ROLE_TOPLV = 1,
   E_COMP_WL_SH_SURF_ROLE_POPUP = 2,
} E_Comp_Wl_Sh_Surf_Role;

typedef enum _E_Comp_Wl_Intercept_Hook_Point
{
   E_COMP_WL_INTERCEPT_HOOK_CURSOR_TIMER_MOUSE_IN,
   E_COMP_WL_INTERCEPT_HOOK_CURSOR_TIMER_MOUSE_OUT,
   E_COMP_WL_INTERCEPT_HOOK_CURSOR_TIMER_MOUSE_MOVE,
   E_COMP_WL_INTERCEPT_HOOK_LAST,
} E_Comp_Wl_Intercept_Hook_Point;

typedef void (*E_Comp_Wl_Hook_Cb) (void *data, E_Client *ec);
typedef Eina_Bool (*E_Comp_Wl_Intercept_Hook_Cb) (void *data, E_Client *ec);

struct _E_Comp_Wl_Aux_Hint
{
   int           id;
   const char   *hint;
   const char   *val;
   Eina_Bool     changed;
   Eina_Bool     deleted;
};

struct _E_Comp_Wl_Buffer
{
   E_Comp_Wl_Buffer_Type type;
   struct wl_resource *resource;
   struct wl_signal destroy_signal;
   struct wl_listener destroy_listener;
   struct wl_shm_buffer *shm_buffer;
   tbm_surface_h tbm_surface;
   struct
   {
      Eina_Stringshare *owner_name;
      void *owner_ptr;
   } debug_info;
   int32_t w, h;
   int transform; // the value of wl_tbm.set_buffer_transform
   uint32_t busy;
};

struct _E_Comp_Wl_Buffer_Ref
{
   E_Comp_Wl_Buffer *buffer;
   struct wl_listener destroy_listener;
   Eina_Bool          destroy_listener_usable;
};

struct _E_Comp_Wl_Buffer_Viewport {
   struct
     {
        uint32_t transform;   /* wl_surface.set_buffer_transform */
        int32_t scale;        /* wl_surface.set_scaling_factor */

        /* If src_width != wl_fixed_from_int(-1), then and only then src_* are used. */
        wl_fixed_t src_x, src_y;
        wl_fixed_t src_width, src_height;
     } buffer;

   struct
     {
        /* If width == -1, the size is inferred from the buffer. */
        int32_t width, height;
     } surface;

   int changed;

   /* When screen or window is rotated, a transformed buffer could be
    * attached after attaching a few buffers. So to detect when the transformed
    * buffer exactly, we need to know the status of waiting the transformed buffer.
    */
   uint32_t wait_for_transform_change;
};

struct _E_Comp_Wl_Surface_State
{
   int sx, sy;
   int bw, bh;
   E_Comp_Wl_Buffer *buffer;
   struct wl_listener buffer_destroy_listener;
   Eina_List *damages, *buffer_damages, *frames;
   Eina_Tiler *input, *opaque;
   E_Comp_Wl_Buffer_Viewport buffer_viewport;
   Eina_Bool new_attach : 1;
   Eina_Bool has_data : 1;
};

struct _E_Comp_Wl_Subsurf_Data
{
   struct wl_resource *resource;

   E_Client *parent;

   struct
     {
        int x, y;
        Eina_Bool set;
     } position;

   E_Comp_Wl_Surface_State cached;
   E_Comp_Wl_Buffer_Ref cached_buffer_ref;

   Eina_Bool synchronized;
   Eina_Bool stand_alone;

   struct
     {
        E_Client *offscreen_parent;
     } remote_surface;
};

struct _E_Comp_Wl_Data
{
   struct
     {
        struct wl_display *disp;
        struct wl_event_loop *loop;
        Evas_GL *gl;
        Evas_GL_Config *glcfg;
        Evas_GL_Context *glctx;
        Evas_GL_Surface *glsfc;
        Evas_GL_API *glapi;
     } wl;

   struct
     {
        struct
          {
             struct wl_signal create;
             struct wl_signal activate;
             struct wl_signal kill;
          } surface;
        /* NB: At the moment, we don't need these */
        /*      struct wl_signal destroy; */
        /*      struct wl_signal activate; */
        /*      struct wl_signal transform; */
        /*      struct wl_signal kill; */
        /*      struct wl_signal idle; */
        /*      struct wl_signal wake; */
        /*      struct wl_signal session; */
        /*      struct  */
        /*        { */
        /*           struct wl_signal created; */
        /*           struct wl_signal destroyed; */
        /*           struct wl_signal moved; */
        /*        } seat, output; */
     } signals;

   struct
     {
        Eina_List *resources;
        Eina_List *focused;
        Eina_Bool enabled : 1;
        xkb_mod_index_t mod_shift, mod_caps;
        xkb_mod_index_t mod_ctrl, mod_alt;
        xkb_mod_index_t mod_super;
        xkb_mod_mask_t mod_depressed, mod_latched, mod_locked;
        xkb_layout_index_t mod_group;
        struct wl_array keys;
        struct wl_array routed_keys;
        struct wl_resource *focus;
        int mod_changed;
        int repeat_delay;
        int repeat_rate;
        unsigned int num_devices;
     } kbd;

   struct
     {
        Eina_List *resources;
        wl_fixed_t x, y;
        wl_fixed_t grab_x, grab_y;
        uint32_t button;
        Ecore_Timer *hide_tmr;
        E_Client *ec;
        Eina_Bool enabled : 1;
        unsigned int num_devices;
     } ptr;

   struct
     {
        Eina_List *resources;
        Eina_Bool enabled : 1;
        unsigned int num_devices;
        unsigned int pressed;
        E_Client *faked_ec;
     } touch;

   struct
     {
        struct wl_global *global;
        Eina_List *resources;
        uint32_t version;
        char *name;

        struct
          {
             struct wl_global *global;
             struct wl_resource *resource;
          } im;
     } seat;

   struct
     {
        struct wl_global *global;
        struct wl_resource *resource;
        Eina_Hash *data_resources;
     } mgr;

   struct
     {
        void *data_source;
        uint32_t serial;
        struct wl_signal signal;
        struct wl_listener data_source_listener;
        E_Client *target;

        struct wl_resource *cbhm;
        Eina_List *data_only_list;
     } selection;

   struct
     {
        void *source;
        struct wl_listener listener;
        E_Client *xwl_owner;
     } clipboard;

   struct
     {
        void *data_source;
        E_Client *icon;
        uint32_t serial;
        struct wl_signal signal;
        struct wl_listener data_source_listener;
        struct wl_client *client;
        struct wl_resource *focus;
        Eina_Bool enabled : 1;
     } dnd;

   struct
     {
        struct wl_resource *resource;
        uint32_t edges;
     } resize;

   struct
     {
        struct xkb_keymap *keymap;
        struct xkb_context *context;
        struct xkb_state *state;
        int fd;
        size_t size;
        char *area;
     } xkb;

   struct
     {
        Eina_Bool underlay;
        Eina_Bool scaler;
     } available_hw_accel;

   struct
     {
        void *server;
     } tbm;

   struct
     {
        struct wl_global *global;
        struct wl_client *client;
     } screenshooter;

   struct
     {
        struct wl_global *global;
     } video;

   Eina_List *outputs;

   Ecore_Fd_Handler *fd_hdlr;
   Ecore_Idler *idler;

   struct wl_client *xwl_client;
   Eina_List *xwl_pending;

   E_Drag *drag;
   E_Client *drag_client;
   void *drag_source;
   void *drag_offer;
};

struct _E_Comp_Wl_Client_Data
{
   struct wl_resource *wl_surface;

   Ecore_Timer *on_focus_timer;

   struct
     {
        E_Comp_Wl_Subsurf_Data *data;

        Eina_List *list;
        Eina_List *list_pending;
        Eina_Bool list_changed : 1;

        Eina_List *below_list;
        Eina_List *below_list_pending;
        Evas_Object *below_obj;

        Eina_Bool restacking : 1;

        struct wl_resource *watcher;
     } sub;

   /* regular surface resource (wl_compositor_create_surface) */
   struct wl_resource *surface;
   struct wl_signal destroy_signal;
   struct wl_signal apply_viewport_signal;

   struct
     {
        /* shell surface resource */
        struct wl_resource *surface;

        void (*configure_send)(struct wl_resource *resource, uint32_t edges, int32_t width, int32_t height);
        void (*configure)(struct wl_resource *resource, Evas_Coord x, Evas_Coord y, Evas_Coord w, Evas_Coord h);
        void (*ping)(struct wl_resource *resource);
        void (*map)(struct wl_resource *resource);
        void (*unmap)(struct wl_resource *resource);
        Eina_Rectangle window;
     } shell;

   E_Comp_Wl_Buffer_Ref buffer_ref;
   E_Comp_Wl_Surface_State pending;

   Eina_List *frames;

   struct
     {
        int32_t x, y;
     } popup;

   struct
     {
        struct wl_resource *viewport;
        E_Comp_Wl_Buffer_Viewport buffer_viewport;
     } scaler;

   struct
     {
        Eina_Bool enabled : 1;
        Eina_Bool start : 1;

        unsigned int scount, stime;
        int sx, sy, dx, dy;
        int prev_degree, cur_degree;
     } transform;

   struct
     {
        Eina_Bool  changed : 1;
        Eina_List *hints;
        Eina_Bool  use_msg : 1;
     } aux_hint;

   /* before applying viewport */
   int width_from_buffer;
   int height_from_buffer;

   /* after applying viewport */
   int width_from_viewport;
   int height_from_viewport;

   Eina_Bool keep_buffer : 1;
   Eina_Bool mapped : 1;
   Eina_Bool has_extern_parent : 1;
   Eina_Bool need_commit_extern_parent : 1;
   Eina_Bool change_icon : 1;
   Eina_Bool need_reparent : 1;
   Eina_Bool reparented : 1;
   Eina_Bool evas_init : 1;
   Eina_Bool first_damage : 1;
   Eina_Bool set_win_type : 1;
   Eina_Bool frame_update : 1;
   Eina_Bool maximize_pre : 1;
   Eina_Bool focus_update : 1;
   Eina_Bool opaque_state : 1;
   Eina_Bool video_client : 1;
   Eina_Bool has_video_client : 1;
   Eina_Bool never_hwc : 1;          //  force window not to do hwc
   unsigned char accepts_focus : 1;
   unsigned char conformant : 1;
   E_Window_Type win_type;
   E_Layer layer;

   struct
   {
      unsigned char win_type : 1;
      unsigned char layer : 1;
   } fetch;

   E_Devicemgr_Input_Device *last_device_ptr;
   E_Devicemgr_Input_Device *last_device_touch;
   E_Devicemgr_Input_Device *last_device_kbd;

   E_Util_Transform *viewport_transform;

   struct
     {
        E_Client *onscreen_parent;
        Eina_List *regions;  //list of onscreen region (Eina_Rectangle *)
     } remote_surface;

   /* xdg shell v6 resource: it should be moved to member of struct shell */
   struct
     {
        E_Comp_Wl_Sh_Surf_Role role;
        struct wl_resource *res_role; /* zxdg_toplevel_v6 or zxdg_popup_v6 */
     } sh_v6;
};

struct _E_Comp_Wl_Output
{
   struct wl_global *global;
   Eina_List *resources;
   const char *id, *make, *model;
   int x, y, w, h;
   int phys_width, phys_height;
   unsigned int refresh;
   unsigned int subpixel;
   unsigned int transform;
   double scale;

   /* added for screenshot ability */
   struct wl_output *wl_output;
   struct wl_buffer *buffer;
   void *data;

   /* configured_resolution */
   int configured_resolution_w;
   int configured_resolution_h;
};

struct _E_Comp_Wl_Hook
{
   EINA_INLIST;
   E_Comp_Wl_Hook_Point hookpoint;
   E_Comp_Wl_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

struct _E_Comp_Wl_Intercept_Hook
{
   EINA_INLIST;
   E_Comp_Wl_Intercept_Hook_Point hookpoint;
   E_Comp_Wl_Intercept_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

E_API Eina_Bool e_comp_wl_init(void);
EINTERN void e_comp_wl_shutdown(void);

E_API void e_comp_wl_deferred_job(void);

EINTERN void e_comp_wl_surface_destroy(struct wl_resource *resource);
EINTERN void e_comp_wl_surface_attach(E_Client *ec, E_Comp_Wl_Buffer *buffer);
E_API Eina_Bool e_comp_wl_surface_commit(E_Client *ec);
EINTERN Eina_Bool e_comp_wl_subsurface_commit(E_Client *ec);
E_API void e_comp_wl_buffer_reference(E_Comp_Wl_Buffer_Ref *ref, E_Comp_Wl_Buffer *buffer);
E_API E_Comp_Wl_Buffer *e_comp_wl_buffer_get(struct wl_resource *resource, E_Client *ec);
E_API Eina_Bool e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource);

E_API struct wl_signal e_comp_wl_surface_create_signal_get(void);
E_API Eina_Bool e_comp_wl_output_init(const char *id, const char *make, const char *model, int x, int y, int w, int h, int pw, int ph, unsigned int refresh, unsigned int subpixel, unsigned int transform);
E_API void e_comp_wl_output_remove(const char *id);

EINTERN Eina_Bool e_comp_wl_key_down(Ecore_Event_Key *ev);
EINTERN Eina_Bool e_comp_wl_key_up(Ecore_Event_Key *ev);
E_API Eina_Bool e_comp_wl_evas_handle_mouse_button(E_Client *ec, uint32_t timestamp, uint32_t button_id, uint32_t state);
E_API void        e_comp_wl_touch_cancel(void);

E_API E_Comp_Wl_Hook *e_comp_wl_hook_add(E_Comp_Wl_Hook_Point hookpoint, E_Comp_Wl_Hook_Cb func, const void *data);
E_API void e_comp_wl_hook_del(E_Comp_Wl_Hook *ch);

E_API E_Comp_Wl_Intercept_Hook *e_comp_wl_intercept_hook_add(E_Comp_Wl_Intercept_Hook_Point hookpoint, E_Comp_Wl_Intercept_Hook_Cb func, const void *data);
E_API void e_comp_wl_intercept_hook_del(E_Comp_Wl_Intercept_Hook *ch);

E_API void e_comp_wl_shell_surface_ready(E_Client *ec);

EINTERN Eina_Bool e_comp_wl_video_subsurface_has(E_Client *ec);
EINTERN Eina_Bool e_comp_wl_normal_subsurface_has(E_Client *ec);
E_API   E_Client* e_comp_wl_topmost_parent_get(E_Client *ec);

E_API enum wl_output_transform e_comp_wl_output_buffer_transform_get(E_Client *ec);
E_API void e_comp_wl_map_size_cal_from_buffer(E_Client *ec);
E_API void e_comp_wl_map_size_cal_from_viewport(E_Client *ec);
E_API void e_comp_wl_map_apply(E_Client *ec);

E_API void e_comp_wl_input_cursor_timer_enable_set(Eina_Bool enabled);
E_API void e_comp_wl_send_event_device(struct wl_client *wc, uint32_t timestamp, Ecore_Device *dev, uint32_t serial);

EINTERN Eina_Bool e_comp_wl_key_send(E_Client *ec, int keycode, Eina_Bool pressed, Ecore_Device *dev, uint32_t time);
EINTERN Eina_Bool e_comp_wl_touch_send(E_Client *ec, int idx, int x, int y, Eina_Bool pressed, Ecore_Device *dev, double radius_x, double radius_y, double pressure, double angle, uint32_t time);
EINTERN Eina_Bool e_comp_wl_touch_update_send(E_Client *ec, int idx, int x, int y, Ecore_Device *dev, double radius_x, double radius_y, double pressure, double angle, uint32_t time);
EINTERN Eina_Bool e_comp_wl_touch_cancel_send(E_Client *ec);
EINTERN Eina_Bool e_comp_wl_mouse_button_send(E_Client *ec, int buttons, Eina_Bool pressed, Ecore_Device *dev, uint32_t time);
EINTERN Eina_Bool e_comp_wl_mouse_move_send(E_Client *ec, int x, int y, Ecore_Device *dev, uint32_t time);
EINTERN Eina_Bool e_comp_wl_mouse_wheel_send(E_Client *ec, int direction, int z, Ecore_Device *dev, uint32_t time);
EINTERN Eina_Bool e_comp_wl_mouse_in_send(E_Client *ec, int x, int y, Ecore_Device *dev, uint32_t time);
EINTERN Eina_Bool e_comp_wl_mouse_out_send(E_Client *ec, Ecore_Device *dev, uint32_t time);
EINTERN void e_comp_wl_mouse_in_renew(E_Client *ec, int buttons, int x, int y, void *data, Evas_Modifier *modifiers, Evas_Lock *locks, unsigned int timestamp, Evas_Event_Flags event_flags, Evas_Device *dev, Evas_Object *event_src);
EINTERN void e_comp_wl_mouse_out_renew(E_Client *ec, int buttons, int x, int y, void *data, Evas_Modifier *modifiers, Evas_Lock *locks, unsigned int timestamp, Evas_Event_Flags event_flags, Evas_Device *dev, Evas_Object *event_src);
E_API Eina_Bool e_comp_wl_key_process(Ecore_Event_Key *ev, int type);

EINTERN Eina_Bool e_comp_wl_cursor_hide(E_Client *ec);

E_API void e_comp_wl_pos_convert(int width, int height, int transform, int scale, int sx, int sy, int *bx, int *by);
E_API void e_comp_wl_pos_convert_inverse(int width, int height, int transform, int scale, int bx, int by, int *sx, int *sy);
E_API void e_comp_wl_rect_convert(int width, int height, int transform, int scale, int sx, int sy, int sw, int sh, int *bx, int *by, int *bw, int *bh);
E_API void e_comp_wl_rect_convert_inverse(int width, int height, int transform, int scale, int bx, int by, int bw, int bh, int *sx, int *sy, int *sw, int *sh);
E_API E_Comp_Wl_Output* e_comp_wl_output_find(E_Client *ec);

EINTERN void	  e_comp_wl_feed_focus_in(E_Client *ec);

E_API void e_comp_wl_subsurface_stack_update(E_Client *ec);

E_API extern int E_EVENT_WAYLAND_GLOBAL_ADD;

EINTERN Eina_Bool e_comp_wl_commit_sync_client_geometry_add(E_Client *ec, E_Client_Demand_Geometry mode, uint32_t serial, int32_t x, int32_t y, int32_t w, int32_t h);
EINTERN void e_comp_wl_trace_serial_debug(Eina_Bool on);
EINTERN Eina_Bool e_comp_wl_commit_sync_configure(E_Client *ec);

EINTERN Eina_Bool         e_comp_wl_pid_output_configured_resolution_send(pid_t pid, int w, int h);
# endif
#endif
