#ifdef E_TYPEDEFS

#include <tbm_surface.h>

typedef struct _E_Output            E_Output;
typedef struct _E_Output_Mode       E_Output_Mode;
typedef enum   _E_Output_Dpms       E_OUTPUT_DPMS;
typedef enum   _E_Output_Ext_State  E_Output_Ext_State;

typedef struct _E_Output_Hook       E_Output_Hook;
typedef enum   _E_Output_Hook_Point E_Output_Hook_Point;
typedef void (*E_Output_Hook_Cb) (void *data, E_Output *output);

typedef struct _E_Output_Intercept_Hook       E_Output_Intercept_Hook;
typedef enum   _E_Output_Intercept_Hook_Point E_Output_Intercept_Hook_Point;
typedef Eina_Bool (*E_Output_Intercept_Hook_Cb) (void *data, E_Output *output);

typedef void (*E_Output_Capture_Cb) (E_Output *output, tbm_surface_h surface, void *user_data);

#else
#ifndef E_OUTPUT_H
#define E_OUTPUT_H

#define E_OUTPUT_TYPE (int)0xE0b11002

#include "e_comp_screen.h"

enum _E_Output_Dpms
{
   E_OUTPUT_DPMS_ON,
   E_OUTPUT_DPMS_STANDBY,
   E_OUTPUT_DPMS_SUSPEND,
   E_OUTPUT_DPMS_OFF,
};

enum _E_Output_Ext_State
{
   E_OUTPUT_EXT_NONE,
   E_OUTPUT_EXT_MIRROR,
   E_OUTPUT_EXT_PRESENTATION,
};

struct _E_Output_Mode
{
   int    w, h; // resolution width and height
   double refresh; // refresh in hz
   Eina_Bool preferred : 1; // is this the preferred mode for the device?

   const tdm_output_mode *tmode;
};

struct _E_Output
{
   int index;
   char *id; // string id which is "name/edid";
   struct {
        char                 *screen; // name of the screen device attached
        char                 *name; // name of the output itself
        char                 *edid; // full edid data
        Eina_Bool             connected : 1; // some screen is plugged in or not
        Eina_List            *modes; // available screen modes here
        struct {
             int                w, h; // physical width and height in mm
        } size;
   } info;
   struct {
        Eina_Rectangle        geom; // the geometry that is set (as a result)
        E_Output_Mode         mode; // screen res/refresh to use
        int                   rotation; // 0, 90, 180, 270
        int                   priority; // larger num == more important
        Eina_Bool             enabled : 1; // should this monitor be enabled?
   } config;

   int                  plane_count;
   Eina_List           *planes;
   E_Zone              *zone;

   tdm_output           *toutput;
   tdm_output_type       toutput_type;

   E_Comp_Screen        *e_comp_screen;
   E_OUTPUT_DPMS        dpms;

   struct {
       int min_w, min_h;
       int max_w, max_h;
       int preferred_align;
   } cursor_available;

   Eina_Bool            zoom_set;
   struct
   {
      double            zoomx;
      double            zoomy;
      int               init_cx;
      int               init_cy;
      int               adjusted_cx;
      int               adjusted_cy;
      int               init_angle;
      int               current_angle;
      int               init_screen_rotation;
      int               current_screen_rotation;
      Eina_Rectangle    rect;
      Eina_Rectangle    rect_touch;
      Eina_Bool         need_touch_set;
      Eina_Bool         unset_skip;
   } zoom_conf;
   Ecore_Event_Handler *touch_up_handler;

   struct
   {
      tdm_capture      *tcapture;
      Eina_Bool         start;
      Eina_List        *data;
      Eina_Bool         possible_tdm_capture;
      Ecore_Timer      *timer;
      Eina_Bool         wait_vblank;
   } stream_capture;

   /* output hwc */
   E_Output_Hwc *output_hwc;
   Eina_Bool     tdm_hwc;
   Eina_Bool     wait_commit;

   /* external */
   Eina_Bool            external_set;
   Eina_Bool            external_pause;
   struct
   {
      E_Output_Ext_State state;
      E_Output_Ext_State current_state;
   } external_conf;
};

enum _E_Output_Hook_Point
{
   E_OUTPUT_HOOK_DPMS_CHANGE,
   E_OUTPUT_HOOK_LAST
};

struct _E_Output_Hook
{
   EINA_INLIST;
   E_Output_Hook_Point hookpoint;
   E_Output_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

enum _E_Output_Intercept_Hook_Point
{
   E_OUTPUT_INTERCEPT_HOOK_DPMS_ON,
   E_OUTPUT_INTERCEPT_HOOK_DPMS_STANDBY,
   E_OUTPUT_INTERCEPT_HOOK_DPMS_SUSPEND,
   E_OUTPUT_INTERCEPT_HOOK_DPMS_OFF,
   E_OUTPUT_INTERCEPT_HOOK_LAST
};

struct _E_Output_Intercept_Hook
{
   EINA_INLIST;
   E_Output_Intercept_Hook_Point hookpoint;
   E_Output_Intercept_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

EINTERN Eina_Bool         e_output_init(void);
EINTERN void              e_output_shutdown(void);
EINTERN E_Output        * e_output_new(E_Comp_Screen *e_comp_screen, int index);
EINTERN void              e_output_del(E_Output *output);
EINTERN Eina_Bool         e_output_rotate(E_Output *output, int rotate);
EINTERN Eina_Bool         e_output_update(E_Output *output);
EINTERN Eina_Bool         e_output_mode_apply(E_Output *output, E_Output_Mode *mode);
EINTERN Eina_Bool         e_output_commit(E_Output *output);
EINTERN Eina_Bool         e_output_render(E_Output *output);
EINTERN Eina_Bool         e_output_setup(E_Output *output);
EINTERN E_Output_Mode   * e_output_best_mode_find(E_Output *output);
EINTERN Eina_Bool         e_output_connected(E_Output *output);
EINTERN Eina_Bool         e_output_dpms_set(E_Output *output, E_OUTPUT_DPMS val);
E_API E_OUTPUT_DPMS       e_output_dpms_get(E_Output *output);
EINTERN void              e_output_size_get(E_Output *output, int *w, int *h);
EINTERN E_Plane         * e_output_default_fb_target_get(E_Output *output);
EINTERN Eina_Bool         e_output_fake_config_set(E_Output *output, int w, int h);
EINTERN Eina_Bool         e_output_zoom_set(E_Output *output, double zoomx, double zoomy, int cx, int cy);
EINTERN void              e_output_zoom_unset(E_Output *output);
EINTERN Eina_Bool         e_output_capture(E_Output *output, tbm_surface_h surface, Eina_Bool auto_rotate, Eina_Bool sync, E_Output_Capture_Cb func, void *data);
EINTERN Eina_Bool         e_output_stream_capture_queue(E_Output *output, tbm_surface_h surface, E_Output_Capture_Cb func, void *data);
EINTERN Eina_Bool         e_output_stream_capture_dequeue(E_Output *output, tbm_surface_h surface);
EINTERN Eina_Bool         e_output_stream_capture_start(E_Output *output);
EINTERN void              e_output_stream_capture_stop(E_Output *output);
EINTERN const char      * e_output_output_id_get(E_Output *output);
EINTERN Eina_Bool         e_output_external_set(E_Output *output, E_Output_Ext_State state);
EINTERN void              e_output_external_unset(E_Output *output);
EINTERN Eina_Bool         e_output_external_update(E_Output *output);
E_API E_Output          * e_output_find(const char *id);
E_API E_Output          * e_output_find_by_index(int index);
E_API const Eina_List   * e_output_planes_get(E_Output *output);
E_API void                e_output_util_planes_print(void);
E_API Eina_Bool           e_output_is_fb_composing(E_Output *output);
E_API Eina_Bool           e_output_is_fb_full_compositing(E_Output *output);
E_API E_Plane           * e_output_fb_target_get(E_Output *output);
E_API E_Plane           * e_output_plane_get_by_zpos(E_Output *output, int zpos);
E_API E_Output_Hook     * e_output_hook_add(E_Output_Hook_Point hookpoint, E_Output_Hook_Cb func, const void *data);
E_API void                e_output_hook_del(E_Output_Hook *ch);
E_API E_Output_Intercept_Hook * e_output_intercept_hook_add(E_Output_Intercept_Hook_Point hookpoint, E_Output_Intercept_Hook_Cb func, const void *data);
E_API void                e_output_intercept_hook_del(E_Output_Intercept_Hook *ch);

#endif
#endif
