#ifdef E_TYPEDEFS

typedef struct _E_Hwc_Window                      E_Hwc_Window;
typedef struct _E_Hwc_Window_Target               E_Hwc_Window_Target;
typedef struct _E_Hwc_Window_Buffer               E_Hwc_Window_Buffer;
typedef struct _E_Hwc_Window_Commit_Data          E_Hwc_Window_Commit_Data;
typedef struct _E_Hwc_Window_Hook                 E_Hwc_Window_Hook;
typedef void (*E_Hwc_Window_Hook_Cb) (void *data, E_Hwc_Window *hwc_window);

#else
#ifndef E_HWC_WINDOW_H
#define E_HWC_WINDOW_H

#define E_HWC_WINDOW_TYPE (int)0xE0b11003
#define E_HWC_WINDOW_ZPOS_NONE -999

#define EHW_C(b,m)              (b ? ((b) >> (m)) & 0xFF : ' ')
#define EHW_FOURCC_STR(id)      EHW_C(id,0), EHW_C(id,8), EHW_C(id,16), EHW_C(id,24)

typedef enum _E_Hwc_Window_State
{
   E_HWC_WINDOW_STATE_NONE,
   E_HWC_WINDOW_STATE_CLIENT,
   E_HWC_WINDOW_STATE_DEVICE,
   E_HWC_WINDOW_STATE_VIDEO,
   E_HWC_WINDOW_STATE_CURSOR,
} E_Hwc_Window_State;

typedef enum _E_Hwc_Window_Transition
{
   E_HWC_WINDOW_TRANSITION_NONE_TO_NONE,
   E_HWC_WINDOW_TRANSITION_NONE_TO_CLIENT,
   E_HWC_WINDOW_TRANSITION_NONE_TO_DEVICE,
   E_HWC_WINDOW_TRANSITION_NONE_TO_CURSOR,
   E_HWC_WINDOW_TRANSITION_CLIENT_TO_NONE,
   E_HWC_WINDOW_TRANSITION_CLIENT_TO_CLIENT,
   E_HWC_WINDOW_TRANSITION_CLIENT_TO_DEVICE,
   E_HWC_WINDOW_TRANSITION_CLIENT_TO_CURSOR,
   E_HWC_WINDOW_TRANSITION_DEVICE_TO_NONE,
   E_HWC_WINDOW_TRANSITION_DEVICE_TO_CLIENT,
   E_HWC_WINDOW_TRANSITION_DEVICE_TO_DEVICE,
   E_HWC_WINDOW_TRANSITION_CURSOR_TO_NONE,
   E_HWC_WINDOW_TRANSITION_CURSOR_TO_CLIENT,
   E_HWC_WINDOW_TRANSITION_CURSOR_TO_CURSOR
} E_Hwc_Window_Transition;

typedef enum _E_Hwc_Window_Activation_State
{
   E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED = 0,
   E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED,
} E_Hwc_Window_Activation_State;

typedef enum _E_Hwc_Window_Hook_Point
{
   E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE,
   E_HWC_WINDOW_HOOK_LAST
} E_Hwc_Window_Hook_Point;

struct _E_Hwc_Window_Hook
{
   EINA_INLIST;
   E_Hwc_Window_Hook_Point hookpoint;
   E_Hwc_Window_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

struct _E_Hwc_Window_Buffer
{
   tbm_surface_h                  tsurface;
   E_Hwc_Window_Queue            *queue;
   struct wl_listener             queue_destroy_listener;
   Eina_Bool                      from_queue;
};

struct _E_Hwc_Window
{
   E_Object                       e_obj_inherit;

   E_Client                      *ec;
   E_Hwc                         *hwc;
   tdm_hwc_window                *thwc_window;
   int                            zpos;
   Eina_Bool                      is_target;
   Eina_Bool                      is_video;
   Eina_Bool                      is_cursor;
   Eina_Bool                      is_deleted;
   Eina_Bool                      set_name;
   Eina_Bool                      device_state_available;

   E_Hwc_Window_Activation_State  activation_state; /* hwc_window has occupied the hw layer or not */

   E_Hwc_Window_State             state;
   E_Hwc_Window_State             accepted_state;
   E_Hwc_Window_Transition        transition;
   int                            transition_failures;

   E_Hwc_Window_Buffer            buffer;

   tdm_hwc_window_info            info;
   Eina_List                     *prop_list;
   E_Hwc_Window_Commit_Data      *commit_data;

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref        buffer_ref;
      E_Hwc_Window_Buffer         buffer;
      tdm_hwc_window_info         info;
   } display;

   struct
   {
      int                         rotation;
      E_Comp_Wl_Buffer           *buffer;
      void                       *img_ptr;
      int                         img_w;
      int                         img_h;
      int                         img_stride;
   } cursor;
   struct wl_listener             cursor_buffer_destroy_listener;

   struct
   {
      /* for fps */
      double               fps;
      double               old_fps;
      double               frametimes[122];
      double               time;
      double               lapse;
      int                  cframes;
      int                  flapse;
   } fps;

   int                            constraints;

   E_Hwc_Window_Queue            *queue;
   struct wl_listener             queue_destroy_listener;

   Eina_Bool                      render_target;
   Eina_Bool                      need_redirect;
   Eina_Bool                      on_rendered_target;

   unsigned int                   restriction;
};

struct _E_Hwc_Window_Target
{
   E_Hwc_Window        hwc_window; /* don't move this field */
   E_Hwc              *hwc;

   Ecore_Evas         *ee;
   Evas               *evas;
   int                 event_fd;
   Ecore_Fd_Handler   *event_hdlr;

   /* a surface the rendering is currently performing at */
   tbm_surface_h       dequeued_tsurface;
   Eina_List          *rendering_tsurfaces;
   Eina_List          *rendered_windows;
   Eina_Bool           is_rendering;
   int                 max_transition_failures;

   Eina_Bool skip_surface_set;
};

struct _E_Hwc_Window_Commit_Data {
   E_Comp_Wl_Buffer_Ref   buffer_ref;
   E_Hwc_Window_Buffer    buffer;
   tdm_hwc_window_info    info;
};

EINTERN Eina_Bool               e_hwc_window_init(void);
EINTERN void                    e_hwc_window_deinit(void);

EINTERN E_Hwc_Window           *e_hwc_window_new(E_Hwc *hwc, E_Client *ec, E_Hwc_Window_State state);
EINTERN void                    e_hwc_window_free(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool               e_hwc_window_zpos_set(E_Hwc_Window *hwc_window, int zpos);
EINTERN int                     e_hwc_window_zpos_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_composition_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_info_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_buffer_fetch(E_Hwc_Window *hwc_window, Eina_Bool tdm_set);
EINTERN Eina_Bool               e_hwc_window_prop_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_is_target(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_is_video(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_is_cursor(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool               e_hwc_window_commit_data_acquire(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool               e_hwc_window_activate(E_Hwc_Window *hwc_window, E_Hwc_Window_Queue *queue);
EINTERN Eina_Bool               e_hwc_window_deactivate(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window);
EINTERN tbm_surface_h           e_hwc_window_displaying_surface_get(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool               e_hwc_window_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state, Eina_Bool composition_update);
EINTERN E_Hwc_Window_State      e_hwc_window_state_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_accepted_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state);
EINTERN E_Hwc_Window_State      e_hwc_window_accepted_state_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_device_state_available_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_device_state_available_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_transition_set(E_Hwc_Window *hwc_window, E_Hwc_Window_Transition transition);
EINTERN E_Hwc_Window_Transition e_hwc_window_transition_get(E_Hwc_Window *hwc_window);
EINTERN const char*             e_hwc_window_transition_string_get(E_Hwc_Window_Transition transition);
EINTERN const char*             e_hwc_window_restriction_string_get(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool               e_hwc_window_constraints_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_rendered_window_update(E_Hwc_Window *hwc_window);
EINTERN void                    e_hwc_window_buffer_set(E_Hwc_Window *hwc_window, tbm_surface_h tsurface, E_Hwc_Window_Queue *queue);
EINTERN const char             *e_hwc_window_state_string_get(E_Hwc_Window_State hwc_window_state);
EINTERN const char             *e_hwc_window_name_get(E_Hwc_Window *hwc_window);
EINTERN void                    e_hwc_window_name_set(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_set_property(E_Hwc_Window *hwc_window, unsigned int id, const char *name, tdm_value value, Eina_Bool force);
EINTERN Eina_Bool               e_hwc_window_get_property(E_Hwc_Window *hwc_window, unsigned int id, tdm_value *value);
EINTERN void                    e_hwc_window_client_type_override(E_Hwc_Window *hwc_window);

EINTERN E_Hwc_Window_Hook      *e_hwc_window_hook_add(E_Hwc_Window_Hook_Point hookpoint, E_Hwc_Window_Hook_Cb func, const void *data);
EINTERN void                    e_hwc_window_hook_del(E_Hwc_Window_Hook *ch);

EINTERN void                    e_hwc_window_trace_debug(Eina_Bool onoff);
EINTERN void                    e_hwc_window_commit_data_buffer_dump(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_fps_get(E_Hwc_Window *hwc_window, double *fps);

EINTERN Eina_Bool               e_hwc_window_pp_rendered_window_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool               e_hwc_window_pp_commit_data_acquire(E_Hwc_Window *hwc_window, Eina_Bool pp_hwc_mode);

#endif // E_HWC_WINDOW_H
#endif
