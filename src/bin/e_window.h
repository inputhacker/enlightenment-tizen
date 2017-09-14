#ifdef E_TYPEDEFS

typedef struct _E_Window                     E_Window;
typedef struct _E_Window_Target              E_Window_Target;
typedef struct _E_Window_Commit_Data         E_Window_Commit_Data;

#else
#ifndef E_WINDOW_H
#define E_WINDOW_H

struct _E_Window
{
   E_Client                      *ec;
   E_Output                      *output;
   tdm_hwc_window                *hwc_wnd;
   int                            zpos;
   int                            skip_flag;
   tdm_hwc_window_composition_t   type;
   Eina_Bool                      is_target;
   Eina_Bool                      wait_commit;
   Eina_Bool                      update_exist;
   tbm_surface_h                  tsurface;
   tbm_surface_h                  displaying_tsurface;
   Eina_Bool                      activated; /* window occupied the hw layer */

   /* current display information */
   struct
   {
      E_Comp_Wl_Buffer_Ref  buffer_ref;
      tbm_surface_h         tsurface;
   } display_info;

   E_Window_Commit_Data *commit_data;
};

struct _E_Window_Target
{
   E_Window            window; /* don't move this field */

   Ecore_Evas         *ee;
   Evas               *evas;
   Ecore_Fd_Handler   *event_hdlr;
   int                 event_fd;

   tbm_surface_queue_h queue;
};

struct _E_Window_Commit_Data {
   E_Comp_Wl_Buffer_Ref  buffer_ref;
   tbm_surface_h         tsurface;
};

EINTERN Eina_Bool             e_window_init(void);
EINTERN E_Window             *e_window_new(E_Output *output);
EINTERN void                  e_window_free(E_Window *window);
EINTERN Eina_Bool             e_window_set_zpos(E_Window *window, int zpos);
EINTERN Eina_Bool             e_window_set_skip_flag(E_Window *window);
EINTERN Eina_Bool             e_window_unset_skip_flag(E_Window *window);
EINTERN Eina_Bool             e_window_update(E_Window *window);
EINTERN Eina_Bool             e_window_is_target(E_Window *window);
EINTERN Eina_Bool             e_window_fetch(E_Window *window);
EINTERN void                  e_window_unfetch(E_Window *window);
EINTERN E_Window_Commit_Data *e_window_commit_data_aquire(E_Window *window);
EINTERN void                  e_window_commit_data_release(E_Window *window);
EINTERN Eina_Bool             e_window_target_surface_queue_can_dequeue(E_Window_Target *target_window);
EINTERN tbm_surface_h         e_window_target_surface_queue_acquire(E_Window_Target *target_window);
EINTERN void                  e_window_target_surface_queue_release(E_Window_Target *target_window, tbm_surface_h tsurface);
EINTERN Eina_Bool             e_window_prepare_commit(E_Window *window);
EINTERN Eina_Bool             e_window_activate(E_Window *window);
EINTERN Eina_Bool             e_window_deactivate(E_Window *window);

#endif
#endif
