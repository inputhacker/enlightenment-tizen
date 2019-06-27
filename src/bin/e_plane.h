#ifdef E_TYPEDEFS

typedef enum _E_Plane_Type
{
   E_PLANE_TYPE_INVALID,
   E_PLANE_TYPE_VIDEO,
   E_PLANE_TYPE_GRAPHIC,
   E_PLANE_TYPE_CURSOR
} E_Plane_Type;

typedef enum _E_Plane_Role
{
   E_PLANE_ROLE_NONE,
   E_PLANE_ROLE_VIDEO,
   E_PLANE_ROLE_OVERLAY,
   E_PLANE_ROLE_CURSOR
} E_Plane_Role;

typedef enum _E_Plane_Color
{
   E_PLANE_COLOR_INVALID,
   E_PLANE_COLOR_YUV,
   E_PLANE_COLOR_RGB
} E_Plane_Color;

typedef struct _E_Plane                      E_Plane;
typedef struct _E_Plane_Commit_Data          E_Plane_Commit_Data;
typedef struct _E_Event_Plane_Win_Change     E_Event_Plane_Win_Change;
typedef struct _E_Plane_Hook                 E_Plane_Hook;
typedef void (*E_Plane_Hook_Cb) (void *data, E_Plane *plane);

#else
#ifndef E_PLANE_H
#define E_PLANE_H

#define E_PLANE_TYPE (int)0xE0b11001

#include "e_comp_screen.h"
#include "e_output.h"
#include "e_plane_renderer.h"
#include "e_comp_wl.h"

struct _E_Plane
{
   int                   index;
   int                   zpos;
   const char           *name;
   E_Plane_Type          type;
   E_Plane_Color         color;
   Eina_Bool             is_primary;
   Eina_Bool             is_fb;        // fb target
   Eina_Bool             is_reserved;  // surface assignment reserved

   E_Client             *ec;
   E_Client             *prepare_ec;
   Eina_Bool             ec_redirected;

   Eina_Bool             reserved_memory;

   tdm_layer            *tlayer;
   tdm_info_layer        info;
   tbm_surface_h         tsurface;

   E_Plane_Renderer     *renderer;
   E_Output             *output;

   unsigned int          buffer_flags;
   Eina_Bool             wait_commit;
   Eina_List            *commit_data_list;
   Eina_Bool             unset_candidate;
   Eina_Bool             unset_try;
   Eina_Bool             unset_commit;
   int                   unset_counter;

   Eina_Bool             is_video;
   Eina_Bool             reserved_video;

   Eina_Bool             fetch_retry;

   E_Plane              *fb_change;
   int                   fb_change_counter;

   Eina_Bool             commit_per_vblank;

   /* true if plane's ec is set or unset.
    * false when E_Event_Plane_Win_Change has been generated.
    */
   Eina_Bool             need_ev;

   E_Plane_Role          role;

   Eina_Bool             skip_surface_set;

   Eina_List            *available_formats;

   /* for pp */
   tdm_pp               *tpp;
   Eina_List            *pp_data_list;
   Eina_List            *pending_pp_data_list;
   Eina_List            *pending_pp_commit_data_list;
   tbm_surface_queue_h   pp_tqueue;
   tbm_surface_h         pp_tsurface;
   Eina_Bool             pp_set_info;
   Eina_Bool             pp_set;
   Eina_Bool             pp_commit;
   Eina_Bool             pp_layer_commit;
   E_Plane_Commit_Data  *pp_layer_commit_data;
   Eina_Rectangle        pp_rect;

   /* for external */
   Eina_Bool             is_external;
   Eina_Rectangle        mirror_rect;
   E_Output_Ext_State    ext_state;
   E_Output             *output_primary;

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref  buffer_ref;
      tbm_surface_h         tsurface;
      E_Plane_Renderer     *renderer;
      E_Client             *ec;
   } display_info;

   double               fps;
   double               old_fps;
   double               frametimes[122];
   double               time;
   double               lapse;
   int                  cframes;
   int                  flapse;

   int                  set_counter;
   Eina_Bool            fb_sync_wait;
   Eina_Bool            fb_sync_done;

   Eina_Bool            unset_ec_pending;
};

struct _E_Plane_Commit_Data {
   tbm_surface_h         tsurface;
   E_Plane              *plane;
   E_Plane_Renderer     *renderer;
   E_Client             *ec;
   E_Comp_Wl_Buffer_Ref  buffer_ref;
};

typedef enum _E_Plane_Hook_Point
{
   E_PLANE_HOOK_VIDEO_SET,
   E_PLANE_HOOK_UNSET,
   E_PLANE_HOOK_LAST
} E_Plane_Hook_Point;

struct _E_Plane_Hook
{
   EINA_INLIST;
   E_Plane_Hook_Point hookpoint;
   E_Plane_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

struct _E_Event_Plane_Win_Change
{
   E_Plane *ep;
   E_Client *ec;
};

E_API extern int E_EVENT_PLANE_WIN_CHANGE;

EINTERN Eina_Bool            e_plane_init(void);
EINTERN void                 e_plane_shutdown(void);
EINTERN E_Plane             *e_plane_new(E_Output *output, int index);
EINTERN void                 e_plane_free(E_Plane *plane);
EINTERN Eina_Bool            e_plane_setup(E_Plane *plane);
EINTERN Eina_Bool            e_plane_fetch(E_Plane *plane);
EINTERN void                 e_plane_unfetch(E_Plane *plane);
EINTERN E_Plane_Commit_Data *e_plane_commit_data_aquire(E_Plane *plane);
EINTERN void                 e_plane_commit_data_release(E_Plane *plane, E_Plane_Commit_Data *data);
EINTERN void                 e_plane_hwc_trace_debug(Eina_Bool onoff);
EINTERN Eina_Bool            e_plane_render(E_Plane *plane);
EINTERN Eina_Bool            e_plane_commit(E_Plane *plane);
EINTERN Eina_Bool            e_plane_offscreen_commit(E_Plane *plane);
EINTERN void                 e_plane_show_state(E_Plane *plane);
EINTERN Eina_Bool            e_plane_is_unset_candidate(E_Plane *plane);
EINTERN Eina_Bool            e_plane_is_unset_try(E_Plane *plane);
EINTERN void                 e_plane_unset_try_set(E_Plane *plane, Eina_Bool set);
EINTERN Eina_Bool            e_plane_unset_commit_check(E_Plane *plane, Eina_Bool fb_commit);
EINTERN Eina_Bool            e_plane_set_commit_check(E_Plane *plane, Eina_Bool fb_commit);
EINTERN Eina_Bool            e_plane_is_fetch_retry(E_Plane *plane);
EINTERN Eina_Bool            e_plane_fb_target_set(E_Plane *plane, Eina_Bool set);
EINTERN Eina_Bool            e_plane_available_formats_get(E_Plane *plane, const tbm_format **formats, int *count);
EINTERN Eina_Bool            e_plane_pp_commit(E_Plane *plane);
EINTERN Eina_Bool            e_plane_pp_commit_possible_check(E_Plane *plane);
EINTERN Eina_Bool            e_plane_zoom_set(E_Plane *plane, Eina_Rectangle *rect);
EINTERN void                 e_plane_zoom_unset(E_Plane *plane);
EINTERN Eina_Bool            e_plane_fps_get(E_Plane *plane, double *fps);
EINTERN void                 e_plane_dpms_off(E_Plane *plane);
EINTERN Eina_Bool            e_plane_external_fetch(E_Plane *plane);
EINTERN Eina_Bool            e_plane_external_commit(E_Plane *plane);
EINTERN Eina_Bool            e_plane_external_set(E_Plane *plane, Eina_Rectangle *rect, E_Output_Ext_State state);
EINTERN Eina_Bool            e_plane_external_reset(E_Plane *plane, Eina_Rectangle *rect);
EINTERN void                 e_plane_external_unset(E_Plane *plane);
EINTERN void                 e_plane_renderer_unset(E_Plane *plane);
EINTERN void                 e_plane_renderer_clean(E_Plane *plane);


E_API Eina_Bool              e_plane_type_set(E_Plane *plane, E_Plane_Type type);
E_API E_Plane_Type           e_plane_type_get(E_Plane *plane);
E_API Eina_Bool              e_plane_role_set(E_Plane *plane, E_Plane_Role role);
E_API E_Plane_Role           e_plane_role_get(E_Plane *plane);
E_API E_Client              *e_plane_ec_get(E_Plane *plane);
E_API Eina_Bool              e_plane_ec_set(E_Plane *plane, E_Client *ec);
E_API E_Client              *e_plane_ec_prepare_get(E_Plane *plane);
E_API Eina_Bool              e_plane_ec_prepare_set(E_Plane *plane, E_Client *ec);
E_API const char            *e_plane_ec_prepare_set_last_error_get(E_Plane *plane);
E_API Eina_Bool              e_plane_is_primary(E_Plane *plane);
E_API Eina_Bool              e_plane_is_cursor(E_Plane *plane);
E_API E_Plane_Color          e_plane_color_val_get(E_Plane *plane);
E_API Eina_Bool              e_plane_is_fb_target(E_Plane *plane);
E_API Eina_Bool              e_plane_is_reserved(E_Plane *plane);
E_API void                   e_plane_reserved_set(E_Plane *plane, Eina_Bool set);

E_API Eina_Bool              e_plane_video_usable(E_Plane *plane);
E_API Eina_Bool              e_plane_video_set(E_Plane *plane, Eina_Bool set, Eina_Bool *wait);

E_API E_Plane_Hook          *e_plane_hook_add(E_Plane_Hook_Point hookpoint, E_Plane_Hook_Cb func, const void *data);
E_API void                   e_plane_hook_del(E_Plane_Hook *ch);

#endif
#endif
