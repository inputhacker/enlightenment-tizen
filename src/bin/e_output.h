#ifdef E_TYPEDEFS

#include <tbm_surface.h>

typedef struct _E_Output               E_Output;
typedef struct _E_Output_Mode          E_Output_Mode;
typedef struct _E_Eom_Output_Buffer    E_EomOutputBuffer, *E_EomOutputBufferPtr;
typedef struct _E_Eom_Output_Pp        E_EomOutputPp,     *E_EomOutputPpPtr;
typedef enum   _E_Output_Dpms          E_OUTPUT_DPMS;
typedef enum   _E_Output_Display_Mode  E_Output_Display_Mode;

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

#define OUTPUT_NAME_LEN 64

enum _E_Output_Dpms
{
   E_OUTPUT_DPMS_ON,
   E_OUTPUT_DPMS_STANDBY,
   E_OUTPUT_DPMS_SUSPEND,
   E_OUTPUT_DPMS_OFF,
};

enum _E_Output_Display_Mode
{
   E_OUTPUT_DISPLAY_MODE_NONE,
   E_OUTPUT_DISPLAY_MODE_MIRROR,
   E_OUTPUT_DISPLAY_MODE_PRESENTATION,
   E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION,    /* It is used for delayed runnig of Presentation mode */
};

struct _E_Output_Mode
{
   int    w, h; // resolution width and height
   double refresh; // refresh in hz
   Eina_Bool preferred : 1; // is this the preferred mode for the device?
   Eina_Bool current : 1;

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
   Eina_Bool            dpms_async;

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
   Ecore_Event_Filter *touch_up_handler;

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
   E_Hwc *hwc;
   Eina_Bool     tdm_hwc;

   /* external */
   Eina_Bool                external_set;
   Eina_Bool                tdm_mirror;
   E_Output                *mirror_src_output;
   E_Output_Display_Mode    display_mode;
   tdm_layer               *overlay_layer;
   Eina_Bool                need_overlay_pp;
   E_Client                *presentation_ec;
   /* If attribute has been set while external output is disconnected
    * then show black screen and wait until EOM client start sending
    * buffers. After expiring of the delay start mirroring */
   Ecore_Timer *delay_timer;

   Eina_Bool fake_config;
};

enum _E_Output_Hook_Point
{
   E_OUTPUT_HOOK_DPMS_CHANGE,
   E_OUTPUT_HOOK_CONNECT_STATUS_CHANGE,
   E_OUTPUT_HOOK_MODE_CHANGE,
   E_OUTPUT_HOOK_ADD,
   E_OUTPUT_HOOK_REMOVE,
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

/*brief The output prop value union */
typedef union {
   void    *ptr;
   int32_t   s32;
   uint32_t  u32;
   int64_t   s64;
   uint64_t  u64;
} output_prop_value;

/*The output value type enumeration */
typedef enum {
   OUTPUT_PROP_VALUE_TYPE_UNKNOWN,
   OUTPUT_PROP_VALUE_TYPE_PTR,
   OUTPUT_PROP_VALUE_TYPE_INT32,
   OUTPUT_PROP_VALUE_TYPE_UINT32,
   OUTPUT_PROP_VALUE_TYPE_INT64,
   OUTPUT_PROP_VALUE_TYPE_UINT64,
} output_prop_value_type;

/* The property of the output */
typedef struct _output_prop {
   unsigned int id;
   char name[OUTPUT_NAME_LEN];
   output_prop_value_type type;
} output_prop;

EINTERN Eina_Bool         e_output_init(void);
EINTERN void              e_output_shutdown(void);
EINTERN E_Output        * e_output_new(E_Comp_Screen *e_comp_screen, int index);
EINTERN void              e_output_del(E_Output *output);
EINTERN Eina_Bool         e_output_rotate(E_Output *output, int rotate);
EINTERN Eina_Bool         e_output_update(E_Output *output);
EINTERN Eina_Bool         e_output_mode_apply(E_Output *output, E_Output_Mode *mode);
EINTERN Eina_Bool         e_output_mode_change(E_Output *output, E_Output_Mode *mode);
EINTERN Eina_Bool         e_output_commit(E_Output *output);
EINTERN Eina_Bool         e_output_render(E_Output *output);
EINTERN Eina_Bool         e_output_hwc_setup(E_Output *output);
EINTERN E_Output_Mode   * e_output_best_mode_find(E_Output *output);
EINTERN Eina_List       * e_output_mode_list_get(E_Output *output);
EINTERN E_Output_Mode   * e_output_current_mode_get(E_Output *output);
EINTERN Eina_Bool         e_output_connected(E_Output *output);
E_API Eina_Bool           e_output_dpms_set(E_Output *output, E_OUTPUT_DPMS val);
E_API E_OUTPUT_DPMS       e_output_dpms_get(E_Output *output);
EINTERN Eina_Bool         e_output_dpms_async_check(E_Output *output);
EINTERN void              e_output_size_get(E_Output *output, int *w, int *h);
EINTERN void              e_output_phys_size_get(E_Output *output, int *phys_w, int *phys_h);
EINTERN E_Plane         * e_output_default_fb_target_get(E_Output *output);
EINTERN Eina_Bool         e_output_fake_config_set(E_Output *output, int w, int h);
EINTERN Eina_Bool         e_output_zoom_set(E_Output *output, double zoomx, double zoomy, int cx, int cy);
EINTERN Eina_Bool         e_output_zoom_get(E_Output *output, double *zoomx, double *zoomy, int *cx, int *cy);
EINTERN void              e_output_zoom_unset(E_Output *output);
EINTERN Eina_Bool         e_output_capture(E_Output *output, tbm_surface_h surface, Eina_Bool auto_rotate, Eina_Bool sync, E_Output_Capture_Cb func, void *data);
EINTERN Eina_Bool         e_output_stream_capture_queue(E_Output *output, tbm_surface_h surface, E_Output_Capture_Cb func, void *data);
EINTERN Eina_Bool         e_output_stream_capture_dequeue(E_Output *output, tbm_surface_h surface);
EINTERN Eina_Bool         e_output_stream_capture_start(E_Output *output);
EINTERN void              e_output_stream_capture_stop(E_Output *output);
EINTERN const char      * e_output_output_id_get(E_Output *output);

EINTERN Eina_Bool         e_output_external_connect_display_set(E_Output *output);
EINTERN void              e_output_external_disconnect_display_set(E_Output *output);
EINTERN Eina_Bool         e_output_external_update(E_Output *output);
EINTERN Eina_Bool         e_output_external_mode_change(E_Output *output, E_Output_Mode *mode);
EINTERN Eina_Bool         e_output_mirror_set(E_Output *output, E_Output *src_output);
EINTERN void              e_output_mirror_unset(E_Output *output);
EINTERN Eina_Bool         e_output_presentation_wait_set(E_Output *output, E_Client *ec);
EINTERN Eina_Bool         e_output_presentation_update(E_Output *output, E_Client *ec);
EINTERN void              e_output_presentation_unset(E_Output *output);
EINTERN E_Client        * e_output_presentation_ec_get(E_Output *output);

EINTERN E_Output_Display_Mode  e_output_display_mode_get(E_Output *output);

EINTERN void              e_output_norender_push(E_Output *output);
EINTERN void              e_output_norender_pop(E_Output *output);
EINTERN int               e_output_norender_get(E_Output *output);

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
EINTERN void              e_output_zoom_rotating_check(E_Output *output);

E_API Eina_Bool           e_output_available_properties_get(E_Output *output, const output_prop **props, int *count);
E_API Eina_Bool           e_output_property_get(E_Output *output, unsigned int id, output_prop_value *value);
E_API Eina_Bool           e_output_property_set(E_Output *output, unsigned int id, output_prop_value value);

#endif
#endif
