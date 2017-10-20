#ifdef E_TYPEDEFS

typedef struct _E_Window                     E_Window;
typedef struct _E_Window_Target              E_Window_Target;
typedef struct _E_Window_Commit_Data         E_Window_Commit_Data;

#else
#ifndef E_WINDOW_H
#define E_WINDOW_H

typedef enum _E_Window_State
{
   E_WINDOW_STATE_NONE,
   E_WINDOW_STATE_CLIENT,
   E_WINDOW_STATE_DEVICE,
   E_WINDOW_STATE_CLIENT_CANDIDATE,
   E_WINDOW_STATE_VIDEO
} E_Window_State;

typedef enum _E_Window_Activation_State
{
   E_WINDOW_ACTIVATION_STATE_NONE = 0,
   E_WINDOW_ACTIVATION_STATE_ACTIVATED,
   E_WINDOW_ACTIVATION_STATE_DEACTIVATED,
} E_Window_Activation_State;

struct _E_Window
{
   E_Client                      *ec;
   E_Output                      *output;
   tdm_hwc_window                *hwc_wnd;
   int                            zpos;
   int                            skip_flag;
   Eina_Bool                      is_visible;
   tdm_hwc_window_composition_t   type;
   Eina_Bool                      is_target;
   Eina_Bool                      is_video;
   Eina_Bool                      is_deleted;
   Eina_Bool                      update_exist;
   tbm_surface_h                  tsurface;
   E_Window_Activation_State      activation_state; /* window has occupied the hw layer or not */

   E_Window_State                 state;

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref  buffer_ref;
      tbm_surface_h         tsurface;
   } display_info;

   E_Window_Commit_Data *commit_data;

   /* whether E20 has to notify this e_window about the end of composition */
   Eina_Bool                      get_notified_about_composition_end;
   uint64_t                       frame_num;  /* the absolute number of frame to be notified about */

   /* whether an e_client owned by this window got composited on the fb_target */
   Eina_Bool                      got_composited;
};

struct _E_Window_Target
{
   E_Window            window; /* don't move this field */

   Ecore_Evas         *ee;
   Evas               *evas;
   int                 event_fd;

   tbm_surface_queue_h queue;

   uint64_t            render_cnt;
};

struct _E_Window_Commit_Data {
   E_Comp_Wl_Buffer_Ref  buffer_ref;
   tbm_surface_h         tsurface;
};

EINTERN Eina_Bool             e_window_set_ec(E_Window *window, E_Client *ec);
EINTERN Eina_Bool             e_window_init(void);
EINTERN E_Window             *e_window_new(E_Output *output);
EINTERN void                  e_window_free(E_Window *window);
EINTERN Eina_Bool             e_window_set_zpos(E_Window *window, int zpos);
EINTERN Eina_Bool             e_window_set_skip_flag(E_Window *window);
EINTERN Eina_Bool             e_window_unset_skip_flag(E_Window *window);
EINTERN Eina_Bool             e_window_mark_visible(E_Window *window);
EINTERN Eina_Bool             e_window_mark_unvisible(E_Window *window);
EINTERN Eina_Bool             e_window_update(E_Window *window);
EINTERN Eina_Bool             e_window_is_target(E_Window *window);
EINTERN Eina_Bool             e_window_is_video(E_Window *window);
EINTERN Eina_Bool             e_window_fetch(E_Window *window);
EINTERN void                  e_window_unfetch(E_Window *window);
EINTERN E_Window_Commit_Data *e_window_commit_data_aquire(E_Window *window);
EINTERN Eina_Bool             e_window_commit_data_release(E_Window *window);
EINTERN Eina_Bool             e_window_target_surface_queue_can_dequeue(E_Window_Target *target_window);
EINTERN tbm_surface_h         e_window_target_surface_queue_acquire(E_Window_Target *target_window);
EINTERN void                  e_window_target_surface_queue_release(E_Window_Target *target_window, tbm_surface_h tsurface);
EINTERN uint64_t              e_window_target_get_current_renderer_cnt(E_Window_Target *target_window);
EINTERN Eina_Bool             e_window_prepare_commit(E_Window *window);
EINTERN Eina_Bool             e_window_offscreen_commit(E_Window *window);
EINTERN Eina_Bool             e_window_activate(E_Window *window);
EINTERN Eina_Bool             e_window_deactivate(E_Window *window);
EINTERN Eina_Bool             e_window_is_on_hw_overlay(E_Window *window);
EINTERN tbm_surface_h         e_window_get_displaying_surface(E_Window *window);

EINTERN Eina_Bool             e_window_set_state(E_Window *window, E_Window_State state);
EINTERN E_Window_State        e_window_get_state(E_Window *window);

EINTERN Eina_Bool             e_window_get_notified_about_composition_end(E_Window *window, uint64_t offset);

#endif
#endif
