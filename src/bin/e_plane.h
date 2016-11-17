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

   Eina_Bool             reserved_memory;

   tdm_layer            *tlayer;
   tdm_info_layer        info;
   tbm_surface_h         tsurface;

   E_Comp_Wl_Buffer_Ref  displaying_buffer_ref;
   tbm_surface_h         displaying_buffer_tsurface;

   E_Plane_Renderer     *renderer;
   E_Output             *output;

   unsigned int          buffer_flags;
   E_Plane_Commit_Data  *pending_commit_data;
   Eina_Bool             need_to_unset_commit;

   /* true if plane's ec is set or unset.
    * false when E_Event_Plane_Win_Change has been generated.
    */
   Eina_Bool             need_ev;

   E_Plane_Role          role;
};

struct _E_Plane_Commit_Data {
   tbm_surface_h  tsurface;
   E_Plane       *plane;
   E_Client      *ec;
   E_Comp_Wl_Buffer_Ref  buffer_ref;
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
EINTERN Eina_Bool            e_plane_hwc_setup(E_Plane *plane);
EINTERN Eina_Bool            e_plane_fetch(E_Plane *plane);
EINTERN void                 e_plane_unfetch(E_Plane *plane);
EINTERN E_Plane_Commit_Data *e_plane_commit_data_aquire(E_Plane *plane);
EINTERN void                 e_plane_commit_data_release(E_Plane_Commit_Data *data);
EINTERN Eina_Bool            e_plane_is_reserved(E_Plane *plane);
EINTERN void                 e_plane_reserved_set(E_Plane *plane, Eina_Bool set);
EINTERN void                 e_plane_hwc_trace_debug(Eina_Bool onoff);
EINTERN Eina_Bool            e_plane_render(E_Plane *plane);

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

#endif
#endif
