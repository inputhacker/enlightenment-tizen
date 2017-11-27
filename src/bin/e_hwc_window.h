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
   E_HWC_WINDOW_STATE_CLIENT_CANDIDATE,
   E_HWC_WINDOW_STATE_VIDEO,
   E_HWC_WINDOW_STATE_DEVICE_CANDIDATE
} E_Hwc_Window_State;

typedef enum _E_Hwc_Window_Activation_State
{
   E_HWC_WINDOW_ACTIVATION_STATE_NONE = 0,
   E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED,
   E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED,
} E_Hwc_Window_Activation_State;

struct _E_Hwc_Window
{
   E_Client                      *ec;
   E_Output                      *output;
   tdm_hwc_window                *hwc_wnd;
   int                            zpos;
   int                            is_excluded; /* whether hwc_wnd excluded from being handled by hwc */
   tdm_hwc_window_composition     type;
   Eina_Bool                      is_target;
   Eina_Bool                      is_video;
   Eina_Bool                      is_deleted;
   Eina_Bool                      update_exist;
   tbm_surface_h                  tsurface;
   E_Hwc_Window_Activation_State  activation_state; /* hwc_window has occupied the hw layer or not */

   E_Hwc_Window_State             state;

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref  buffer_ref;
      tbm_surface_h         tsurface;
   } display_info;

   E_Hwc_Window_Commit_Data       *commit_data;

   /* whether E20 has to notify this E_Hwc_Window about the need to unset
    * candidate client type */
   Eina_Bool                      get_notified_about_need_unset_cc_type;
   uint64_t                       frame_num;  /* the absolute number of frame to be notified about */

   /* whether an e_client owned by this hwc_window got composited on the fb_target */
   Eina_Bool                      got_composited;
   Eina_Bool                      need_unset_cc_type; /* need unset candidate client type */
   /* number of buffers in front of the buffer which contains the e_client
    * owned by this hwc_window*/
   int delay;

   Eina_Bool hwc_acceptable;
};

struct _E_Hwc_Window_Target
{
   E_Hwc_Window        hwc_window; /* don't move this field */

   Ecore_Evas         *ee;
   Evas               *evas;
   int                 event_fd;

   tbm_surface_queue_h queue;

   uint64_t            render_cnt;
};

struct _E_Hwc_Window_Commit_Data {
   E_Comp_Wl_Buffer_Ref  buffer_ref;
   tbm_surface_h         tsurface;
};

EINTERN Eina_Bool          e_hwc_window_init(E_Output_Hwc *output_hwc);
EINTERN void               e_hwc_window_deinit(E_Output_Hwc *output_hwc); // TODO:

EINTERN E_Hwc_Window      *e_hwc_window_new(E_Output_Hwc *output_hwc, E_Client *ec);
EINTERN void               e_hwc_window_free(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_set_zpos(E_Hwc_Window *hwc_window, int zpos);
EINTERN Eina_Bool          e_hwc_window_update(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_target(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_video(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_fetch(E_Hwc_Window *hwc_window);
EINTERN void               e_hwc_window_unfetch(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_commit_data_aquire(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_target_surface_queue_can_dequeue(E_Hwc_Window_Target *target_hwc_window);
EINTERN tbm_surface_h      e_hwc_window_target_surface_queue_acquire(E_Hwc_Window_Target *target_hwc_window);
EINTERN void               e_hwc_window_target_surface_queue_release(E_Hwc_Window_Target *target_hwc_window, tbm_surface_h tsurface);
EINTERN uint64_t           e_hwc_window_target_get_current_renderer_cnt(E_Hwc_Window_Target *target_hwc_window);

EINTERN Eina_Bool          e_hwc_window_activate(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_deactivate(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool          e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window);
EINTERN tbm_surface_h      e_hwc_window_get_displaying_surface(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_set_state(E_Hwc_Window *hwc_window, E_Hwc_Window_State state);
EINTERN E_Hwc_Window_State e_hwc_window_get_state(E_Hwc_Window *hwc_window);

EINTERN Eina_Bool          e_hwc_window_get_notified_about_need_unset_cc_type(E_Hwc_Window *hwc_window, E_Hwc_Window_Target *target_hwc_window, uint64_t offset);

#endif // E_HWC_WINDOW_H
#endif
