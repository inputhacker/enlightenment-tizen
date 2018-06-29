#ifdef E_TYPEDEFS

# define E_HWC_WINDOW_QUEUE_BUFFER_FLAGS 7777

typedef struct _E_Hwc_Window_Queue               E_Hwc_Window_Queue;
typedef struct _E_Hwc_Window_Queue_Buffer        E_Hwc_Window_Queue_Buffer;

typedef enum _E_Hwc_Window_Queue_State
{
   E_HWC_WINDOW_QUEUE_STATE_UNSET = 0,
   E_HWC_WINDOW_QUEUE_STATE_PENDING_SET,
   E_HWC_WINDOW_QUEUE_STATE_SET,
   E_HWC_WINDOW_QUEUE_STATE_PENDING_UNSET,
} E_Hwc_Window_Queue_State;

#else
#ifndef E_HWC_WINDOW_QUEUE_H
#define E_HWC_WINDOW_QUEUE_H

struct _E_Hwc_Window_Queue
{
   tbm_surface_queue_h               tqueue;
   Eina_List                        *buffers;
   struct wl_signal                  destroy_signal;

   E_Hwc_Window                     *user;
   Eina_List                        *waiting_user;
   E_Hwc_Window_Queue_State          state;

   Eina_Bool                         is_target;
};

struct _E_Hwc_Window_Queue_Buffer
{
   E_Hwc_Window_Queue            *queue;
   E_Hwc_Window                  *user;
   tbm_surface_h                  tsurface;

   struct wl_resource            *exported_wl_buffer;
   struct wl_listener             exported_destroy_listener;
   Eina_Bool                      exported;
   Eina_Bool                      usable;
   Eina_Bool                      released;
   Eina_Bool                      acquired;
};

EINTERN Eina_Bool            e_hwc_window_queue_init(E_Hwc *hwc);
EINTERN void                 e_hwc_window_queue_deinit(void);
EINTERN E_Hwc_Window_Queue * e_hwc_window_queue_get(E_Hwc_Window *hwc_window);
EINTERN Eina_Bool            e_hwc_window_queue_target_set(E_Hwc_Window_Queue *queue, Eina_Bool target);
EINTERN Eina_Bool            e_hwc_window_queue_user_set(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window);
EINTERN void                 e_hwc_window_queue_user_unset(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window);

EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_get(E_Hwc_Window_Queue *queue, tbm_surface_h tsurface);
EINTERN Eina_Bool                   e_hwc_window_queue_can_dequeue(E_Hwc_Window_Queue *queue);
EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_dequeue(E_Hwc_Window_Queue *queue);
EINTERN Eina_Bool                   e_hwc_window_queue_buffer_enqueue(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer);
EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_acquire(E_Hwc_Window_Queue *queue);
EINTERN Eina_Bool                   e_hwc_window_queue_buffer_release(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer);

#endif // E_HWC_WINDOW_QUEUE_H
#endif
