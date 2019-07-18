#ifdef E_TYPEDEFS

# define E_HWC_WINDOW_QUEUE_BUFFER_FLAGS 7777

typedef struct _E_Hwc_Window_Queue               E_Hwc_Window_Queue;
typedef struct _E_Hwc_Window_Queue_Buffer        E_Hwc_Window_Queue_Buffer;

typedef enum _E_Hwc_Window_Queue_State
{
   E_HWC_WINDOW_QUEUE_STATE_UNSET = 0,
   E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING,
   E_HWC_WINDOW_QUEUE_STATE_SET,
   E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_WAIT_USABLE,  /* waiting state until the wait_usable request comes */
   E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER,       /* waiting state until the exported buffer comes */
   E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_DEQUEUEABLE,  /* waiting state until the dequeueable buffer gets */
   E_HWC_WINDOW_QUEUE_STATE_SET_INVALID,
} E_Hwc_Window_Queue_State;

#else
#ifndef E_HWC_WINDOW_QUEUE_H
#define E_HWC_WINDOW_QUEUE_H

#define E_HWC_WINDOW_QUEUE_TYPE (int)0xE0b11004

struct _E_Hwc_Window_Queue
{
   E_Object                          e_obj_inherit;

   E_Hwc                            *hwc;
   tbm_surface_queue_h               tqueue;
   Eina_List                        *buffers;
   struct wl_signal                  destroy_signal;

   E_Hwc_Window                     *user;
   E_Hwc_Window                     *user_waiting_unset;
   Eina_List                        *user_pending_set;
   E_Hwc_Window_Queue_State          state;

   Eina_Bool                         is_target;


   int                               width;
   int                               height;
   tbm_format                        format;
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
   Eina_Bool                      dequeued;
};

EINTERN Eina_Bool            e_hwc_window_queue_init(void);
EINTERN void                 e_hwc_window_queue_deinit(void);
EINTERN E_Hwc_Window_Queue * e_hwc_window_queue_user_set(E_Hwc_Window *hwc_window);
EINTERN void                 e_hwc_window_queue_user_unset(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window);

EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_find(E_Hwc_Window_Queue *queue, tbm_surface_h tsurface);
EINTERN Eina_Bool                   e_hwc_window_queue_buffer_can_dequeue(E_Hwc_Window_Queue *queue);
EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_dequeue(E_Hwc_Window_Queue *queue);
EINTERN Eina_Bool                   e_hwc_window_queue_buffer_enqueue(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer);
EINTERN E_Hwc_Window_Queue_Buffer * e_hwc_window_queue_buffer_acquire(E_Hwc_Window_Queue *queue);
EINTERN Eina_Bool                   e_hwc_window_queue_buffer_release(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer);
EINTERN Eina_Bool                   e_hwc_window_queue_clear(E_Hwc_Window_Queue *queue);

EINTERN Eina_List *                 e_hwc_window_queue_acquirable_buffers_get(E_Hwc_Window_Queue *queue);

EINTERN void                        e_hwc_window_queue_trace_debug(Eina_Bool onoff);
EINTERN void                        e_hwc_window_queue_debug_info_get(Eldbus_Message_Iter *iter);

#endif // E_HWC_WINDOW_QUEUE_H
#endif
