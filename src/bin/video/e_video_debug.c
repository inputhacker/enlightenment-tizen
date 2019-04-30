#include "e_video_internal.h"

static Eina_Bool video_to_primary = EINA_FALSE;
static Eina_Bool video_punch = EINA_FALSE;
static Evas_Object *punch_obj = NULL;

E_API Eina_Bool
e_video_debug_display_primary_plane_value_get(void)
{
   return video_to_primary;
}

E_API void
e_video_debug_display_primary_plane_set(Eina_Bool set)
{
   video_to_primary = set;
}

EINTERN Eina_Bool
e_video_debug_punch_value_get(void)
{
   return video_punch;
}

EINTERN void
e_video_debug_punch_set(Eina_Bool set)
{
   video_punch = set;
}

EINTERN void
e_video_debug_screen_punch_set(int x, int y, int w, int h, int a, int r, int g, int b)
{
   if (!punch_obj)
     {
        punch_obj = evas_object_rectangle_add(e_comp->evas);
        evas_object_layer_set(punch_obj, EVAS_LAYER_MAX);
        evas_object_render_op_set(punch_obj, EVAS_RENDER_COPY);
     }

   evas_object_color_set(punch_obj, r, g, b, a);

   if (w == 0 || h == 0)
     evas_output_size_get(e_comp->evas, &w, &h);

   evas_object_move(punch_obj, x, y);
   evas_object_resize(punch_obj, w, h);
   evas_object_show(punch_obj);
}

EINTERN void
e_video_debug_screen_punch_unset(void)
{
   if (!punch_obj)
     return;

   evas_object_del(punch_obj);
   punch_obj = NULL;
}
