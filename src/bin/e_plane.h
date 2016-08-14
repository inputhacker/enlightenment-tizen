#ifdef E_TYPEDEFS

typedef enum _E_Plane_Type
{
   E_PLANE_TYPE_INVALID,
   E_PLANE_TYPE_VIDEO,
   E_PLANE_TYPE_OVERLAY,
   E_PLANE_TYPE_CURSOR
} E_Plane_Type;

typedef enum _E_Plane_Color
{
   E_PLANE_COLOR_INVALID,
   E_PLANE_COLOR_YUV,
   E_PLANE_COLOR_RGB
} E_Plane_Color;

typedef struct _E_Plane                      E_Plane;
typedef struct _E_Plane_Commit_Data          E_Plane_Commit_Data;
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

   E_Plane_Renderer     *renderer;
   E_Output             *output;
};

struct _E_Plane_Commit_Data {
   tbm_surface_h  tsurface;
   E_Plane       *plane;
   E_Client      *ec;
   E_Comp_Wl_Buffer_Ref  buffer_ref;
};

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

E_API Eina_Bool              e_plane_type_set(E_Plane *plane, E_Plane_Type type);
E_API E_Plane_Type           e_plane_type_get(E_Plane *plane);
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
