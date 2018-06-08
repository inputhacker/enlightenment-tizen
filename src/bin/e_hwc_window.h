#ifdef E_TYPEDEFS

typedef struct _E_Hwc_Window                     E_Hwc_Window;
typedef struct _E_Hwc_Window_Target              E_Hwc_Window_Target;
typedef struct _E_Hwc_Window_Commit_Data         E_Hwc_Window_Commit_Data;

#else
#ifndef E_HWC_WINDOW_H
#define E_HWC_WINDOW_H

typedef enum _E_Hwc_Window_State
{
   E_HWC_WINDOW_STATE_NONE,
   E_HWC_WINDOW_STATE_CLIENT,
   E_HWC_WINDOW_STATE_DEVICE,
   E_HWC_WINDOW_STATE_VIDEO,
   E_HWC_WINDOW_STATE_DEVICE_CANDIDATE,
   E_HWC_WINDOW_STATE_CURSOR
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
   E_HWC_WINDOW_ACTIVATION_STATE_NONE = 0,
   E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED,
   E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED,
} E_Hwc_Window_Activation_State;

struct _E_Hwc_Window
{
   E_Client                      *ec;
   E_Hwc                  *hwc;
   tdm_hwc_window                *thwc_window;
   int                            zpos;
   Eina_Bool                      is_target;
   Eina_Bool                      is_video;
   Eina_Bool                      is_cursor;
   Eina_Bool                      is_deleted;
   Eina_Bool                      update_exist;
   tbm_surface_h                  tsurface;
   E_Hwc_Window_Activation_State  activation_state; /* hwc_window has occupied the hw layer or not */
   tdm_hwc_window_info info;

   E_Hwc_Window_State             state;
   E_Hwc_Window_State             prev_state;
   E_Hwc_Window_Transition        transition;
   E_Hwc_Window_Transition        uncompleted_transition;

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref  buffer_ref;
      tbm_surface_h         tsurface;
   } display_info;

   E_Hwc_Window_Commit_Data       *commit_data;

   Eina_Bool hwc_acceptable;
   Eina_Bool need_change_buffer_transform;

   tbm_surface_h       cursor_tsurface;
   int                 cursor_rotation;

   Eina_Bool is_device_to_client_transition;
};

struct _E_Hwc_Window_Target
{
   E_Hwc_Window        hwc_window; /* don't move this field */
   E_Hwc               *hwc;

   Ecore_Evas         *ee;
   Evas               *evas;
   int                 event_fd;
   Ecore_Fd_Handler   *event_hdlr;

   tbm_surface_queue_h queue;

   /* a surface the rendering is currently performing at */
   tbm_surface_h       dequeued_tsurface;
   Eina_List          *ee_rendered_hw_list;

   Eina_Bool skip_surface_set;
};

struct _E_Hwc_Window_Commit_Data {
   E_Comp_Wl_Buffer_Ref  buffer_ref;
   tbm_surface_h         tsurface;
};

EINTERN Eina_Bool          e_hwc_window_init(E_Hwc *hwc);
EINTERN void               e_hwc_window_deinit(E_Hwc *hwc); // TODO:

EINTERN E_Hwc_Window      *e_hwc_window_new(E_Hwc *hwc, E_Client *ec, E_Hwc_Window_State state);
EINTERN void               e_hwc_window_free(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_zpos_set(E_Hwc_Window *hwc_window, int zpos);
EINTERN int                e_hwc_window_zpos_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_compsition_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_buffer_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_target(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_video(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_cursor(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_buffer_fetch(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_commit_data_aquire(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_target_surface_queue_can_dequeue(E_Hwc_Window_Target *target_hwc_window);
EINTERN Eina_Bool          e_hwc_window_target_enabled(E_Hwc_Window_Target *target_hwc_window);
EINTERN Eina_Bool          e_hwc_window_target_buffer_fetch(E_Hwc_Window_Target *target_hwc_window);
EINTERN Eina_List         *e_hwc_window_target_window_ee_rendered_hw_list_get(E_Hwc_Window_Target *target_window);

EINTERN Eina_Bool          e_hwc_window_activate(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_deactivate(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window);
EINTERN tbm_surface_h      e_hwc_window_displaying_surface_get(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state);
EINTERN E_Hwc_Window_State e_hwc_window_state_get(E_Hwc_Window *hwc_window);
EINTERN void               e_hwc_window_prev_state_update(E_Hwc_Window *hwc_window);
EINTERN E_Hwc_Window_State e_hwc_window_prev_state_get(E_Hwc_Window *hwc_window);

EINTERN void               e_hwc_window_render_list_add(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_is_on_target_window(E_Hwc_Window *hwc_window);

EINTERN const char        *e_hwc_window_state_string_get(E_Hwc_Window_State hwc_window_state);

#endif // E_HWC_WINDOW_H
#endif