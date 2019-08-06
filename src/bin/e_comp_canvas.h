#ifdef E_TYPEDEFS



#else
#ifndef E_COMP_CANVAS_H
#define E_COMP_CANVAS_H

extern EINTERN int E_EVENT_COMPOSITOR_RESIZE;

EINTERN Eina_Bool e_comp_canvas_init(int w, int h);
EINTERN void e_comp_canvas_clear(void);
EINTERN void e_comp_all_freeze(void);
EINTERN void e_comp_all_thaw(void);
E_API E_Zone * e_comp_zone_xy_get(Evas_Coord x, Evas_Coord y);
EINTERN E_Zone * e_comp_zone_number_get(int num);
EINTERN E_Zone * e_comp_zone_id_get(int id);
EINTERN E_Desk * e_comp_desk_window_profile_get(const char *profile);
EINTERN void e_comp_canvas_zone_update(E_Zone *zone);
EINTERN void e_comp_canvas_update(void);
EINTERN void e_comp_canvas_fake_layers_init(void);
EINTERN void e_comp_canvas_fps_toggle(void);
EINTERN E_Layer e_comp_canvas_layer_map_to(unsigned int layer);
E_API unsigned int e_comp_canvas_layer_map(E_Layer layer);
E_API unsigned int e_comp_canvas_client_layer_map(E_Layer layer);
EINTERN E_Layer e_comp_canvas_client_layer_map_nearest(int layer);
EINTERN void e_comp_canvas_keys_grab(void);
EINTERN void e_comp_canvas_keys_ungrab(void);
EINTERN void e_comp_canvas_feed_mouse_up(unsigned int activate_time);

E_API void    e_comp_canvas_norender_push(void);
E_API void    e_comp_canvas_norender_pop(void);
EINTERN int   e_comp_canvas_norender_get(void);

/* the following functions are used for adjusting root window coordinates
 * to/from canvas coordinates.
 * this ensures correct positioning when running E as a nested compositor
 *
 * - use the "adjust" functions to go root->canvas
 * - use the "unadjust" functions to go canvas->root
 */
static inline int
e_comp_canvas_x_root_unadjust(int x)
{
   int cx;

   ecore_evas_geometry_get(e_comp->ee, &cx, NULL, NULL, NULL);
   return x + cx;
}

static inline int
e_comp_canvas_y_root_unadjust(int y)
{
   int cy;

   ecore_evas_geometry_get(e_comp->ee, NULL, &cy, NULL, NULL);
   return y + cy;
}

static inline int
e_comp_canvas_x_root_adjust(int x)
{
   int cx;

   ecore_evas_geometry_get(e_comp->ee, &cx, NULL, NULL, NULL);
   return x - cx;
}

static inline int
e_comp_canvas_y_root_adjust(int y)
{
   int cy;

   ecore_evas_geometry_get(e_comp->ee, NULL, &cy, NULL, NULL);
   return y - cy;
}

#endif
#endif
