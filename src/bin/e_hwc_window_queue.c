#include "e.h"

# include <wayland-tbm-server.h>
# if HAVE_LIBGOMP
# include <omp.h>
# endif

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#define E_HWC_WINDOW_HOOK_APPEND(list, type, callback, data) \
  do \
    { \
       E_Hwc_Window_Hook *_ch; \
       _ch = e_hwc_window_hook_add(type, callback, data); \
       assert(_ch); \
       list = eina_list_append(list, _ch); \
    } \
  while (0)

#define EHWQERR(f, ec, hwc, ehwq, x...)                          \
   do                                                            \
     {                                                           \
        ERR("EWL|%20.20s|              |             |%8s|"f,  \
            "HWC-WINQ", (e_hwc_output_id_get(hwc)), ##x);      \
     }                                                           \
   while (0)

#define EHWQINF(f, ec, hwc, ehwq, x...)                          \
   do                                                            \
     {                                                           \
        if ((!ec) && (!ehwq))                                    \
          INF("EWL|%20.20s|              |             |%8s|"f,  \
              "HWC-WINQ", (e_hwc_output_id_get(hwc)), ##x);      \
        else                                                     \
          INF("EWL|%20.20s|win:0x%08zx|ec:%8p| ehwq:%8p|%8s| "f, \
              "HWC-WINQ",                                        \
              (e_client_util_win_get(ec)),                       \
              (ec),                                              \
              (ehwq), (e_hwc_output_id_get(hwc)),                \
              ##x);                                              \
     }                                                           \
   while (0)

#define EHWQTRACE(f, ec, hwc, ehwq, x...)                             \
   do                                                                 \
     {                                                                \
        if (ehwq_trace)                                               \
          {                                                           \
             if ((!ec) && (!ehwq))                                    \
               INF("EWL|%20.20s|              |             |%8s|"f,  \
                   "HWC-WINQ", (e_hwc_output_id_get(hwc)), ##x);      \
             else                                                     \
               INF("EWL|%20.20s|win:0x%08zx|ec:%8p| ehwq:%8p|%8s| "f, \
                   "HWC-WINQ",                                        \
                   (e_client_util_win_get(ec)),                       \
                   (ec),                                              \
                   (ehwq), (e_hwc_output_id_get(hwc)),                \
                   ##x);                                              \
          }                                                           \
     }                                                                \
   while (0)

static Eina_Bool ehwq_trace = EINA_FALSE;

static Eina_Bool _e_hwc_window_queue_buffers_retrieve_done(E_Hwc_Window_Queue *queue);
static void _e_hwc_window_queue_unset(E_Hwc_Window_Queue *queue);

typedef struct _E_Hwc_Window_Queue_Manager E_Hwc_Window_Queue_Manager;

struct _E_Hwc_Window_Queue_Manager
{
   Eina_Hash *hwc_winq_hash;
};

static Eina_List *hwc_window_queue_window_hooks = NULL;
static Eina_List *hwc_window_queue_comp_wl_hooks = NULL;
static E_Hwc_Window_Queue_Manager *_hwc_winq_mgr = NULL;

struct wayland_tbm_client_queue *
_user_cqueue_get(E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Comp_Wl_Client_Data *cdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, NULL);

   if (!ec) return NULL;

   cdata = (E_Comp_Wl_Client_Data *)e_pixmap_cdata_get(ec->pixmap);
   if (!cdata) return NULL;

   wl_surface = cdata->wl_surface;
   if (!wl_surface) return NULL;

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);

   return cqueue;
}

static uint32_t
_comp_wl_buffer_flags_get(E_Comp_Wl_Buffer *buffer)
{
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   uint32_t flags = 0;

   if (!buffer) return 0;

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
       case E_COMP_WL_BUFFER_TYPE_TBM:
         if (buffer->resource)
           flags = wayland_tbm_server_get_buffer_flags(wl_comp_data->tbm.server, buffer->resource);
         else
           flags = 0;
         break;
       default:
         flags = 0;
         break;
     }

   return flags;
}

static E_Comp_Wl_Buffer *
_comp_wl_buffer_get(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cdata = NULL;

   if (!ec) return NULL;

   cdata = ec->comp_data;
   if (!cdata) return NULL;

   return cdata->buffer_ref.buffer;
}

static tbm_surface_h
_backup_tsurface_create(tbm_surface_h tsurface)
{
   tbm_surface_h new_tsurface = NULL;
   tbm_surface_info_s src_info, dst_info;
   int ret = TBM_SURFACE_ERROR_NONE;

   ret = tbm_surface_map(tsurface, TBM_SURF_OPTION_READ, &src_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        EHWQERR("fail to map the tsurface.", NULL, NULL, NULL);
        return NULL;
     }

   new_tsurface = tbm_surface_create(src_info.width, src_info.height, src_info.format);
   if (!new_tsurface)
     {
        EHWQERR("fail to allocate the new_tsurface.", NULL, NULL, NULL);
        tbm_surface_unmap(tsurface);
        return NULL;
     }

   ret = tbm_surface_map(new_tsurface, TBM_SURF_OPTION_WRITE, &dst_info);
   if (ret != TBM_SURFACE_ERROR_NONE)
     {
        EHWQERR("fail to map the new_tsurface.", NULL, NULL, NULL);
        tbm_surface_destroy(new_tsurface);
        tbm_surface_unmap(tsurface);
        return NULL;
     }

   /* copy from src to dst */
#if HAVE_LIBGOMP
# define LIBGOMP_COPY_THREAD_NUM 4
# define LIBGOMP_COPY_PAGE_SIZE getpagesize()
# define PAGE_ALIGN(addr) ((addr)&(~((LIBGOMP_COPY_PAGE_SIZE)-1)))
   if (src_info.planes[0].size > (LIBGOMP_COPY_THREAD_NUM * LIBGOMP_COPY_PAGE_SIZE))
     {
        size_t step[2];
        step[0] = PAGE_ALIGN(src_info.planes[0].size / LIBGOMP_COPY_THREAD_NUM);
        step[1] = src_info.planes[0].size - (step[0] * (LIBGOMP_COPY_THREAD_NUM - 1));

        omp_set_num_threads(LIBGOMP_COPY_THREAD_NUM);
        #pragma omp parallel
        #pragma omp sections
          {
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr,
                         src_info.planes[0].ptr,
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + step[0],
                         src_info.planes[0].ptr + step[0],
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + (step[0] * 2),
                         src_info.planes[0].ptr + (step[0] * 2),
                         step[0]);
               }
             #pragma omp section
               {
                  memcpy(dst_info.planes[0].ptr + (step[0] * 3),
                         src_info.planes[0].ptr + (step[0] * 3),
                         step[1]);
               }
          }
     }
   else
     {
        memcpy(dst_info.planes[0].ptr,
               src_info.planes[0].ptr,
               src_info.planes[0].size);
     }
#else /* HAVE_LIBGOMP */
   memcpy(dst_info.planes[0].ptr, src_info.planes[0].ptr, src_info.planes[0].size);
#endif /* end of HAVE_LIBGOMP */

   tbm_surface_unmap(new_tsurface);
   tbm_surface_unmap(tsurface);

   return new_tsurface;
}

static E_Comp_Wl_Buffer *
_comp_wl_backup_buffer_get(tbm_surface_h tsurface)
{
   tbm_surface_h backup_tsurface = NULL;
   E_Comp_Wl_Buffer *backup_buffer = NULL;

   backup_tsurface = _backup_tsurface_create(tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(backup_tsurface, NULL);

   backup_buffer = e_comp_wl_tbm_buffer_get(backup_tsurface);
   if (!backup_buffer)
     {
        EHWQERR("Fail to e_comp_wl_tbm_buffer_get", NULL, NULL, NULL);
        tbm_surface_internal_unref(backup_tsurface);
        return NULL;
     }

   tbm_surface_internal_unref(backup_tsurface);

   return backup_buffer;
}

static tbm_surface_queue_h
_get_tbm_surface_queue()
{
   return e_comp->e_comp_screen->tqueue;
}

static tbm_surface_queue_h
_e_hwc_window_queue_tqueue_acquire(E_Hwc_Window *hwc_window)
{
   tdm_error error = TDM_ERROR_NONE;
   tbm_surface_queue_h tqueue;

   if (e_hwc_window_is_target(hwc_window))
     tqueue = _get_tbm_surface_queue();
   else
     {
        tqueue = tdm_hwc_window_acquire_buffer_queue(hwc_window->thwc_window, &error);

        EHWQINF("Acquire buffer queue ehw:%p tq:%p",
                hwc_window->ec, hwc_window->hwc, NULL, hwc_window, tqueue);
     }

   if (!tqueue)
     {
        EHWQERR("fail to tdm_hwc_window_get_buffer_queue hwc_win:%p tdm_error:%d",
            hwc_window->ec, hwc_window->queue->hwc, hwc_window->queue, hwc_window, error);
        return NULL;
     }

   return tqueue;
}

static void
_e_hwc_window_queue_tqueue_release(tbm_surface_queue_h tqueue, E_Hwc_Window *hwc_window)
{
   tdm_hwc_window_release_buffer_queue(hwc_window->thwc_window, tqueue);

   EHWQINF("Release buffer queue ehw:%p tq:%p",
                hwc_window->ec, hwc_window->hwc, NULL, hwc_window, tqueue);
}

static E_Hwc_Window_Queue_Buffer *
_e_hwc_window_queue_buffer_create(E_Hwc_Window_Queue *queue, tbm_surface_h tsurface)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(tsurface, NULL);

   queue_buffer = E_NEW(E_Hwc_Window_Queue_Buffer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, NULL);

   queue_buffer->queue = queue;
   queue_buffer->exported = EINA_FALSE;
   queue_buffer->usable = EINA_FALSE;
   queue_buffer->released = EINA_TRUE;
   queue_buffer->tsurface = tsurface;

   return queue_buffer;
}

static void
_e_hwc_window_queue_buffer_destroy(E_Hwc_Window_Queue_Buffer *queue_buffer)
{
   EINA_SAFETY_ON_FALSE_RETURN(queue_buffer);

   if (queue_buffer->exported_wl_buffer)
     wl_list_remove(&queue_buffer->exported_destroy_listener.link);

   E_FREE(queue_buffer);
}

static Eina_Bool
_e_hwc_window_queue_user_pending_set_add(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
   e_object_ref(E_OBJECT(hwc_window));
   queue->user_pending_set = eina_list_append(queue->user_pending_set, hwc_window);

   return EINA_TRUE;
}

static void
_e_hwc_window_queue_user_pending_set_remove(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
   Eina_List *l, *ll;
   E_Hwc_Window *tmp_hwc_window = NULL;

   EINA_LIST_FOREACH_SAFE(queue->user_pending_set, l, ll, tmp_hwc_window)
     {
        if(tmp_hwc_window != hwc_window) continue;

        queue->user_pending_set = eina_list_remove_list(queue->user_pending_set, l);
        e_object_unref(E_OBJECT(hwc_window));
     }
}

static void
_e_hwc_window_queue_exported_buffer_destroy_cb(struct wl_listener *listener, void *data)
{
   E_Hwc_Window_Queue *queue = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   struct wl_resource *wl_buffer = NULL;
   E_Hwc_Window *hwc_window = NULL;

   queue_buffer = container_of(listener, E_Hwc_Window_Queue_Buffer, exported_destroy_listener);

   wl_buffer = (struct wl_resource *)data;
   if (wl_buffer != queue_buffer->exported_wl_buffer) return;

   queue = queue_buffer->queue;
   queue_buffer->exported = EINA_FALSE;
   queue_buffer->exported_wl_buffer = NULL;
   wl_list_remove(&queue_buffer->exported_destroy_listener.link);

   if ((queue->state == E_HWC_WINDOW_QUEUE_STATE_SET) ||
       (queue->state == E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER))
     queue->state = E_HWC_WINDOW_QUEUE_STATE_SET_INVALID;

   if (!queue_buffer->acquired && queue_buffer->dequeued)
     e_hwc_window_queue_buffer_release(queue, queue_buffer);

   queue_buffer->usable = EINA_FALSE;

   hwc_window = queue->user;
   EHWQTRACE("DES ts:%p tq:%p wl_buffer:%p",
             (hwc_window ? hwc_window->ec : NULL), queue->hwc, queue,
             queue_buffer->tsurface, queue->tqueue,
             queue_buffer->exported_wl_buffer);

   if (queue->state != E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING) return;

   if (_e_hwc_window_queue_buffers_retrieve_done(queue))
     _e_hwc_window_queue_unset(queue);
}

static void
_e_hwc_window_queue_exported_buffer_detach_cb(struct wayland_tbm_client_queue *cqueue,
                                              tbm_surface_h tsurface,
                                              void *data)
{
   E_Hwc_Window_Queue *queue = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(data);

   queue = (E_Hwc_Window_Queue *)data;

   queue_buffer = e_hwc_window_queue_buffer_find(queue, tsurface);
   if (!queue_buffer) return;

   user = queue->user;

   if ((queue->state == E_HWC_WINDOW_QUEUE_STATE_SET) ||
       (queue->state == E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER))
     queue->state = E_HWC_WINDOW_QUEUE_STATE_SET_INVALID;

   EHWQTRACE("DET ts:%p tq:%p wl_buffer:%p",
             (user ? user->ec : NULL), queue->hwc, queue,
             queue_buffer->tsurface, queue->tqueue,
             queue_buffer->exported_wl_buffer);

   if (!queue_buffer->acquired && queue_buffer->dequeued)
     e_hwc_window_queue_buffer_release(queue, queue_buffer);

   queue_buffer->usable = EINA_FALSE;
}

static Eina_Bool
_e_hwc_window_queue_buffer_export(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_buffer = NULL;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, EINA_FALSE);

   user = queue->user;
   EINA_SAFETY_ON_NULL_RETURN_VAL(user, EINA_FALSE);

   cqueue = _user_cqueue_get(user->ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   if (queue_buffer->exported) return EINA_TRUE;

   /* export the tbm_surface(wl_buffer) to the client_queue */
   wl_buffer = wayland_tbm_server_client_queue_export_buffer2(cqueue, queue_buffer->tsurface,
                                                              E_HWC_WINDOW_QUEUE_BUFFER_FLAGS,
                                                              _e_hwc_window_queue_exported_buffer_detach_cb,
                                                              NULL,
                                                              (void *)queue);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(wl_buffer, EINA_FALSE);

   EHWQTRACE("EXP ts:%p tq:%p wl_buffer:%p",
             (user ? user->ec : NULL), queue->hwc, queue,
             queue_buffer->tsurface, queue->tqueue, wl_buffer);

   queue_buffer->exported = EINA_TRUE;
   queue_buffer->exported_wl_buffer = wl_buffer;
   queue_buffer->exported_destroy_listener.notify = _e_hwc_window_queue_exported_buffer_destroy_cb;
   wl_resource_add_destroy_listener(wl_buffer, &queue_buffer->exported_destroy_listener);

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_queue_buffer_send(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   E_Hwc_Window *hwc_window = NULL;
   struct wayland_tbm_client_queue * cqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue->tqueue, EINA_FALSE);

   if (!tbm_surface_queue_can_dequeue(queue->tqueue, 0)) return EINA_FALSE;

   hwc_window = queue->user;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   cqueue = _user_cqueue_get(hwc_window->ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   queue_buffer = e_hwc_window_queue_buffer_dequeue(queue);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, EINA_FALSE);

   if (queue_buffer->usable) return EINA_TRUE;

   if (!queue_buffer->exported || !queue_buffer->exported_wl_buffer)
     {
        EHWQERR("Not exported queue_buffer:%p tsurface:%p",
                (hwc_window ? hwc_window->ec : NULL), queue->hwc, queue,
                queue_buffer, queue_buffer->tsurface);
        return EINA_FALSE;
     }

   /* send the buffer_usable to the wl_tbm client */
   wayland_tbm_server_client_queue_send_buffer_usable(cqueue, queue_buffer->exported_wl_buffer);
   queue_buffer->usable = EINA_TRUE;

   EHWQTRACE("USA ts:%p tq:%p wl_buffer:%p ehw:%p",
             (hwc_window ? hwc_window->ec : NULL), queue->hwc,
             queue,
             queue_buffer->tsurface, queue->tqueue,
             queue_buffer->exported_wl_buffer,
             hwc_window);

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_queue_buffers_export(E_Hwc_Window_Queue *queue)
{
   Eina_List *l = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   EINA_LIST_FOREACH(queue->buffers, l, queue_buffer)
     {
        if (!_e_hwc_window_queue_buffer_export(queue, queue_buffer))
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_queue_buffers_hand_over(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
  if (e_hwc_window_queue_buffer_can_dequeue(queue))
     {
        if (!_e_hwc_window_queue_buffers_export(queue))
          {
             EHWQERR("fail to queue_buffers_export STATE_SET_WAITING ehw:%p", NULL, queue->hwc, queue, hwc_window);
             return EINA_FALSE;
          }

        if (!_e_hwc_window_queue_buffer_send(queue))
          {
             EHWQERR("fail to queue_dequeue_buffer_send STATE_SET_WAITING ehw:%p", NULL, queue->hwc, queue, hwc_window);
             return EINA_FALSE;
          }

        queue->state = E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER;

        EHWQINF("Set Waiting buffer user ehw:%p -- {%s}",
                hwc_window->ec, queue->hwc, queue, hwc_window,
                (hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN"));
     }
   else
     {
        queue->state = E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_DEQUEUEABLE;

        EHWQINF("Set Waiting dequeueable user ehw:%p -- {%s}",
                hwc_window->ec, queue->hwc, queue, hwc_window,
                (hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN"));
     }

   return EINA_TRUE;
}

static void
_e_hwc_window_queue_wait_usable_cb(struct wayland_tbm_client_queue *cqueue, void *data)
{
   E_Hwc_Window_Queue *queue = (E_Hwc_Window_Queue *)data;

   if (!queue->user) return;

   if (queue->state != E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_WAIT_USABLE) return;

   EHWQINF("Get wait usable event ehw:%p -- {%s}",
           queue->user->ec, queue->hwc, queue, queue->user,
           (queue->user->ec ? queue->user->ec->icccm.title : "UNKNOWN"));

   if (!_e_hwc_window_queue_buffers_hand_over(queue, queue->user))
      EHWQERR("fail to _e_hwc_window_queue_buffers_hand_over", NULL, queue->hwc, queue);
}

static Eina_Bool
_e_hwc_window_queue_buffers_retrieve(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
   if (queue->user)
     e_hwc_window_deactivate(queue->user);

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_queue_buffers_retrieve_done(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *l = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);

   EINA_LIST_FOREACH(queue->buffers, l, queue_buffer)
     {
        if (queue_buffer->exported)
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_hwc_window_queue_cb_dequeueable(tbm_surface_queue_h surface_queue, void *data)
{
   E_Hwc_Window_Queue *queue = NULL;

   queue = (E_Hwc_Window_Queue *)data;
   EINA_SAFETY_ON_NULL_RETURN(queue);

   if (queue->state == E_HWC_WINDOW_QUEUE_STATE_SET)
     {
        if (!_e_hwc_window_queue_buffer_send(queue))
          EHWQERR("fail to queue_dequeue_buffer_send STATE_SET",
                  (queue->user ? queue->user->ec : NULL), queue->hwc, queue);
     }
   else if (queue->state == E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_DEQUEUEABLE)
     {
        if (!_e_hwc_window_queue_buffers_hand_over(queue, queue->user))
          EHWQERR("fail to queue_buffers_hand_over SET_WAITING_DEQUEUEABLE",
                  (queue->user ? queue->user->ec : NULL), queue->hwc, queue);
     }
}

static void
_e_hwc_window_queue_free(E_Hwc_Window_Queue *queue)
{
   EINA_SAFETY_ON_NULL_RETURN(queue);

   EHWQINF("Free", NULL, queue->hwc, queue);

   E_FREE(queue);
}

static void
_e_hwc_window_queue_destroy(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   if (!queue) return;

   if (_hwc_winq_mgr && queue->tqueue)
     eina_hash_del(_hwc_winq_mgr->hwc_winq_hash, &queue->tqueue, queue);

   EHWQINF("Destroy tq:%p", NULL, queue->hwc, queue, queue->tqueue);

   EINA_LIST_FREE(queue->buffers, queue_buffer)
     _e_hwc_window_queue_buffer_destroy(queue_buffer);

   wl_signal_emit(&queue->destroy_signal, queue);

   queue->tqueue = NULL;

   e_object_del(E_OBJECT(queue));
}

static Eina_Bool
_e_hwc_window_queue_prepare_set(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
   struct wayland_tbm_client_queue *cqueue = NULL;
   E_Client *ec = NULL;

   if (eina_list_data_find(queue->user_pending_set, hwc_window) == hwc_window)
     _e_hwc_window_queue_user_pending_set_remove(queue, hwc_window);

   ec = hwc_window->ec;
   if (!ec)
     {
        EHWQERR("client is deleted", NULL, queue->hwc, queue);
        return EINA_FALSE;
     }

   if ((queue->width != ec->w) || (queue->height != ec->h))
     {
        EHWQERR("size mismatch queue(%dx%d) client(%dx%d)",
                ec, queue->hwc, queue, queue->width, queue->height, ec->w, ec->h);
        return EINA_FALSE;
     }

   cqueue = _user_cqueue_get(hwc_window->ec);
   if (!cqueue)
     {
        EHWQERR("fail to get wayland_tbm_client_queue", hwc_window->ec, queue->hwc, queue);
        return EINA_FALSE;
     }

   if (!wayland_tbm_server_client_queue_set_wait_usable_cb(cqueue,
                                                           _e_hwc_window_queue_wait_usable_cb,
                                                           queue))
     {
        EHWQERR("fail to wayland_tbm_server_client_queue_set_wait_usable_cb",
                hwc_window->ec, queue->hwc, queue);
        return EINA_FALSE;
     }

   /* set the hwc_window to the queue */
   queue->user = hwc_window;
   e_object_ref(E_OBJECT(queue->user));

   tbm_surface_queue_add_dequeuable_cb(queue->tqueue,
                                       _e_hwc_window_queue_cb_dequeueable,
                                       (void *)queue);

   e_hwc_window_activate(hwc_window, queue);

   queue->state = E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_WAIT_USABLE;

   e_object_ref(E_OBJECT(queue));

   return EINA_TRUE;
}

static void
_e_hwc_window_queue_set(E_Hwc_Window_Queue *queue)
{
   /* sends all dequeueable buffers */
   while(tbm_surface_queue_can_dequeue(queue->tqueue, 0))
     {
        if (!_e_hwc_window_queue_buffer_send(queue))
          {
             EHWQERR("fail to queue_dequeue_buffer_send QUEUE_STATE_SET ehw:%p",
                     NULL, queue->hwc, queue, queue->user);
             return;
          }
     }

   /* set the queue_state_set */
   queue->state = E_HWC_WINDOW_QUEUE_STATE_SET;

   EHWQINF("Set user ehw:%p -- {%s}",
           queue->user->ec, queue->hwc, queue, queue->user,
           (queue->user->ec ? queue->user->ec->icccm.title : "UNKNOWN"));
}

static void
_e_hwc_window_queue_prepare_unset(E_Hwc_Window_Queue *queue)
{
   struct wayland_tbm_client_queue *cqueue = NULL;
   E_Hwc_Window *hwc_window = queue->user;

   if (hwc_window)
     {
        cqueue = _user_cqueue_get(hwc_window->ec);
        if (cqueue)
          wayland_tbm_server_client_queue_set_wait_usable_cb(cqueue, NULL, NULL);
     }

   tbm_surface_queue_remove_dequeuable_cb(queue->tqueue,
                                          _e_hwc_window_queue_cb_dequeueable,
                                          (void *)queue);

   /* queue retrieve the buffers from the hwc_window */
   _e_hwc_window_queue_buffers_retrieve(queue, queue->user);
}

static void
_e_hwc_window_queue_unset(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *l = NULL;

   if (queue->state == E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING)
     {
        queue->user = queue->user_waiting_unset;
        queue->user_waiting_unset = NULL;
     }

   EINA_LIST_FOREACH(queue->buffers, l, queue_buffer)
     {
        if (!queue_buffer->acquired && queue_buffer->dequeued)
          e_hwc_window_queue_buffer_release(queue, queue_buffer);
     }

   /* release the tqueue */
   _e_hwc_window_queue_tqueue_release(queue->tqueue, queue->user);

   EHWQINF("Unset user ehw:%p -- {%s}",
           queue->user->ec, queue->hwc, queue, queue->user,
           (queue->user->ec ? queue->user->ec->icccm.title : "UNKNOWN"));

   /* unset the hwc_window from the queue */
   e_object_unref(E_OBJECT(queue->user));
   queue->user = NULL;

   /* set the queue_state_unset */
   queue->state = E_HWC_WINDOW_QUEUE_STATE_UNSET;

   /* deal with the hwc_window pending to set the queue */
   if (eina_list_count(queue->user_pending_set))
     {
        hwc_window = eina_list_nth(queue->user_pending_set, 0);
        if (!_e_hwc_window_queue_prepare_set(queue, hwc_window))
          {
             EHWQERR("fail to queue_prepare_set for user_pending_set hwc_window:%p", NULL, queue->hwc, queue, hwc_window);
             _e_hwc_window_queue_tqueue_release(queue->tqueue, hwc_window);
          }
     }

   e_object_unref(E_OBJECT(queue));
}

static void
_e_hwc_window_queue_cb_buffer_change(void *data, E_Client *ec)
{
   E_Hwc_Window *hwc_window = NULL;
   E_Comp_Wl_Buffer *comp_buffer = NULL, *backup_buffer = NULL;
   tbm_surface_h tsurface = NULL;
   uint32_t flags = 0;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   hwc_window = ec->hwc_window;
   if (!hwc_window) return;
   if ((hwc_window->queue) && (e_hwc_window_device_state_available_get(hwc_window)))
     return;

   comp_buffer = _comp_wl_buffer_get(ec);
   if (!comp_buffer) return;
   if (!comp_buffer->tbm_surface) return;

   tsurface = comp_buffer->tbm_surface;

   if (comp_buffer->resource)
     {
        flags = _comp_wl_buffer_flags_get(comp_buffer);
        if (flags !=  E_HWC_WINDOW_QUEUE_BUFFER_FLAGS)
          return;
     }
   else
     {
        if (tsurface != hwc_window->display.buffer.tsurface)
          return;

        if (!hwc_window->display.buffer.from_queue)
          return;
     }

   backup_buffer = _comp_wl_backup_buffer_get(tsurface);
   EINA_SAFETY_ON_NULL_RETURN(backup_buffer);

   EHWQTRACE("Backup buffer set ehw:%p origin:%p tsurface:%p",
             hwc_window->ec, hwc_window->hwc, NULL, hwc_window,
             comp_buffer->tbm_surface, backup_buffer->tbm_surface);

   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, backup_buffer);
   e_pixmap_resource_set(ec->pixmap, backup_buffer);
   e_pixmap_dirty(ec->pixmap);
   e_pixmap_refresh(ec->pixmap);
}

void
_e_hwc_window_queue_cb_accepted_state_change(void *data, E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_State state;
   E_Hwc_Window_Queue *queue = NULL;

   if (!hwc_window->queue) return;
   if (e_hwc_window_is_target(hwc_window)) return;

   queue = hwc_window->queue;
   state = e_hwc_window_accepted_state_get(hwc_window);

   if ((state == E_HWC_WINDOW_STATE_DEVICE) &&
       (queue->state == E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER))
     _e_hwc_window_queue_set(queue);
}

static void
_e_hwc_window_queue_cb_destroy(tbm_surface_queue_h surface_queue, void *data)
{
   E_Hwc_Window_Queue *queue = (E_Hwc_Window_Queue *)data;

   if (!queue) return;

   _e_hwc_window_queue_destroy(queue);
}

static E_Hwc_Window_Queue *
_e_hwc_window_queue_create(tbm_surface_queue_h tqueue)
{
   E_Hwc_Window_Queue *queue = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *dequeued_tsurface = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_h tsurface = NULL;
   tbm_surface_h *surfaces = NULL;
   int size = 0, get_size = 0, i = 0;

   queue = E_OBJECT_ALLOC(E_Hwc_Window_Queue, E_HWC_WINDOW_QUEUE_TYPE, _e_hwc_window_queue_free);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);

   while (tbm_surface_queue_can_dequeue(tqueue, 0))
     {
        /* dequeue */
        tsq_err = tbm_surface_queue_dequeue(tqueue, &tsurface);
        EINA_SAFETY_ON_FALSE_GOTO(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, fail);

        dequeued_tsurface = eina_list_append(dequeued_tsurface, tsurface);
     }

   EINA_LIST_FREE(dequeued_tsurface, tsurface)
     tbm_surface_queue_release(tqueue, tsurface);

   size = tbm_surface_queue_get_size(tqueue);
   EINA_SAFETY_ON_FALSE_GOTO(size > 0, fail);

   surfaces = E_NEW(tbm_surface_h, size);
   EINA_SAFETY_ON_NULL_GOTO(surfaces, fail);

   tsq_err = tbm_surface_queue_get_surfaces(tqueue, surfaces, &get_size);
   EINA_SAFETY_ON_FALSE_GOTO(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, fail);

   for (i = 0; i < get_size; i++)
     {
        queue_buffer = _e_hwc_window_queue_buffer_create(queue, surfaces[i]);
        if (!queue_buffer)
          {
             EHWQERR("fail to e_hwc_window_queue_buffer_create", NULL, queue->hwc, queue);
             goto fail;
          }

        queue->buffers = eina_list_append(queue->buffers, queue_buffer);
     }

   tsq_err = tbm_surface_queue_add_destroy_cb(tqueue,
                                              _e_hwc_window_queue_cb_destroy,
                                              queue);
   EINA_SAFETY_ON_FALSE_GOTO(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, fail);

   queue->width = tbm_surface_queue_get_height(tqueue);
   queue->height = tbm_surface_queue_get_width(tqueue);
   queue->format = tbm_surface_queue_get_format(tqueue);
   queue->tqueue = tqueue;

   wl_signal_init(&queue->destroy_signal);

   if (surfaces) E_FREE(surfaces);

   EHWQINF("Create tq:%p", NULL, queue->hwc, queue, tqueue);

   return queue;

fail:
   if (surfaces) E_FREE(surfaces);
   if (queue)
     {
        EINA_LIST_FREE(queue->buffers, queue_buffer)
          _e_hwc_window_queue_buffer_destroy(queue_buffer);

        E_FREE(queue);
     }

   return NULL;
}

static E_Hwc_Window_Queue *
_e_hwc_window_queue_get(tbm_surface_queue_h tqueue)
{
   E_Hwc_Window_Queue *queue = NULL;

   queue = eina_hash_find(_hwc_winq_mgr->hwc_winq_hash, &tqueue);
   if (!queue)
     {
        queue = _e_hwc_window_queue_create(tqueue);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(queue, NULL);

        eina_hash_add(_hwc_winq_mgr->hwc_winq_hash, &tqueue, queue);
     }

   return queue;
}

EINTERN Eina_Bool
e_hwc_window_queue_init(void)
{
   _hwc_winq_mgr = E_NEW(E_Hwc_Window_Queue_Manager, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, EINA_FALSE);

   _hwc_winq_mgr->hwc_winq_hash = eina_hash_pointer_new(NULL);

   E_HWC_WINDOW_HOOK_APPEND(hwc_window_queue_window_hooks, E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE,
                            _e_hwc_window_queue_cb_accepted_state_change, NULL);
   E_COMP_WL_HOOK_APPEND(hwc_window_queue_comp_wl_hooks, E_COMP_WL_HOOK_BUFFER_CHANGE,
                         _e_hwc_window_queue_cb_buffer_change, NULL);

   return EINA_TRUE;
}

EINTERN void
e_hwc_window_queue_deinit(void)
{
   if (!_hwc_winq_mgr) return;

   E_FREE_LIST(hwc_window_queue_window_hooks, e_hwc_window_hook_del);
   E_FREE_LIST(hwc_window_queue_comp_wl_hooks, e_comp_wl_hook_del);

   E_FREE_FUNC(_hwc_winq_mgr->hwc_winq_hash, eina_hash_free);

   E_FREE(_hwc_winq_mgr);
   _hwc_winq_mgr = NULL;
}

EINTERN E_Hwc_Window_Queue *
e_hwc_window_queue_user_set(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Queue *queue = NULL;
   tbm_surface_queue_h tqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   /* tqueue = _e_hwc_window_queue_tqueue_acquire(hwc_window) */
   tqueue = _e_hwc_window_queue_tqueue_acquire(hwc_window);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tqueue, NULL);

   /* queue = _e_hwc_window_queue_get(tqueue) */
   queue = _e_hwc_window_queue_get(tqueue);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);

   /* set the hwc to the queue */
   if (queue->hwc != hwc_window->hwc)
     queue->hwc = hwc_window->hwc;

   if (e_hwc_window_is_target(hwc_window))
     {
        queue->is_target = EINA_TRUE;
        return queue;
     }

   if (queue->user ||
       queue->state == E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING)
     {
        _e_hwc_window_queue_user_pending_set_add(queue, hwc_window);

        EHWQINF("Add user_pending_set ehw:%p {%s} user:{%s} unset_waiting:%d",
                hwc_window->ec, queue->hwc, queue, hwc_window,
                (hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN"),
                (queue->user&&queue->user->ec?queue->user->ec->icccm.title : "UNKNOWN"),
                (queue->state == E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING?1:0));

        return queue;
     }

   /* _e_hwc_window_queue_set_prepare */
   if (!_e_hwc_window_queue_prepare_set(queue, hwc_window))
     {
        EHWQERR("fail to queue_prepare_set hwc_window:%p", NULL, queue->hwc, queue, hwc_window);
        _e_hwc_window_queue_tqueue_release(queue->tqueue, hwc_window);
        return NULL;
     }

   return queue;
}

EINTERN void
e_hwc_window_queue_user_unset(E_Hwc_Window_Queue *queue, E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN(_hwc_winq_mgr);
   EINA_SAFETY_ON_NULL_RETURN(queue);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if (eina_list_data_find(queue->user_pending_set, hwc_window) == hwc_window)
     {
        _e_hwc_window_queue_user_pending_set_remove(queue, hwc_window);

        /* _e_hwc_window_queue_tqueue_release(tqueue, hwc_window) */
        _e_hwc_window_queue_tqueue_release(queue->tqueue, hwc_window);

        EHWQINF("Remove user_pending_set ehw:%p -- {%s}",
                hwc_window->ec, queue->hwc, queue, hwc_window,
                (hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN"));

        return;
     }

   if (hwc_window != queue->user) return;

   /* prepare the unset conditions */
   _e_hwc_window_queue_prepare_unset(queue);

   if (_e_hwc_window_queue_buffers_retrieve_done(queue))
     _e_hwc_window_queue_unset(queue);
   else
     {
        queue->state = E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING;
        queue->user_waiting_unset = queue->user;
        queue->user = NULL;

        EHWQINF("Unset Waiting user ehw:%p -- {%s}",
                hwc_window->ec, queue->hwc, queue, hwc_window,
                (hwc_window->ec ? hwc_window->ec->icccm.title : "UNKNOWN"));
     }
}

EINTERN E_Hwc_Window_Queue_Buffer *
e_hwc_window_queue_buffer_find(E_Hwc_Window_Queue *queue, tbm_surface_h tsurface)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *l = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tsurface, EINA_FALSE);

   EINA_LIST_FOREACH(queue->buffers, l, queue_buffer)
     {
        if (queue_buffer->tsurface == tsurface)
          return queue_buffer;
     }

   return NULL;
}

EINTERN Eina_Bool
e_hwc_window_queue_buffer_can_dequeue(E_Hwc_Window_Queue *queue)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, EINA_FALSE);

   if (!queue->tqueue) return EINA_FALSE;

   if (tbm_surface_queue_can_dequeue(queue->tqueue, 0))
     return EINA_TRUE;

   return EINA_FALSE;
}

EINTERN E_Hwc_Window_Queue_Buffer *
e_hwc_window_queue_buffer_dequeue(E_Hwc_Window_Queue *queue)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_h tsurface = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);

   if (!queue->tqueue) return NULL;
   if (!tbm_surface_queue_can_dequeue(queue->tqueue, 0)) return NULL;

   tsq_err = tbm_surface_queue_dequeue(queue->tqueue, &tsurface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, NULL);

   queue_buffer = e_hwc_window_queue_buffer_find(queue, tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, NULL);

   queue_buffer->released = EINA_FALSE;
   queue_buffer->dequeued = EINA_TRUE;

   user = queue->user;
   EHWQTRACE("DEQ ts:%p tq:%p",
             (user ? user->ec : NULL), queue->hwc, queue, queue_buffer->tsurface, queue->tqueue);

   return queue_buffer;
}

EINTERN Eina_Bool
e_hwc_window_queue_buffer_enqueue(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, EINA_FALSE);

   if (!queue->tqueue) return EINA_FALSE;

   queue_buffer->dequeued = EINA_FALSE;

   user = queue->user;
   EHWQTRACE("ENQ ts:%p tq:%p",
             (user ? user->ec : NULL), queue->hwc, queue, queue_buffer->tsurface, queue->tqueue);

   tsq_err = tbm_surface_queue_enqueue(queue->tqueue, queue_buffer->tsurface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_Queue_Buffer *
e_hwc_window_queue_buffer_acquire(E_Hwc_Window_Queue *queue)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_h tsurface = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);

   if (!queue->tqueue) return NULL;

   if (!tbm_surface_queue_can_acquire(queue->tqueue, 0)) return NULL;

   tsq_err = tbm_surface_queue_acquire(queue->tqueue, &tsurface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, NULL);

   queue_buffer = e_hwc_window_queue_buffer_find(queue, tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, NULL);

   queue_buffer->released = EINA_FALSE;
   queue_buffer->dequeued = EINA_FALSE;
   queue_buffer->acquired = EINA_TRUE;

   user = queue->user;
   EHWQTRACE("ACQ ts:%p tq:%p",
             (user ? user->ec : NULL), queue->hwc, queue, queue_buffer->tsurface, queue->tqueue);

   return queue_buffer;
}

EINTERN Eina_Bool
e_hwc_window_queue_buffer_release(E_Hwc_Window_Queue *queue, E_Hwc_Window_Queue_Buffer *queue_buffer)
{
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   E_Hwc_Window *user = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue_buffer, EINA_FALSE);

   if (!queue->tqueue) return EINA_FALSE;

   if (queue_buffer->released) return EINA_TRUE;

   queue_buffer->released = EINA_TRUE;
   queue_buffer->dequeued = EINA_FALSE;
   queue_buffer->acquired = EINA_FALSE;

   user = queue->user;
   EHWQTRACE("REL ts:%p tq:%p",
             (user ? user->ec : NULL), queue->hwc, queue, queue_buffer->tsurface, queue->tqueue);

   tsq_err = tbm_surface_queue_release(queue->tqueue, queue_buffer->tsurface);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tsq_err == TBM_SURFACE_QUEUE_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_queue_clear(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, EINA_FALSE);

   while ((queue_buffer = e_hwc_window_queue_buffer_acquire(queue)))
     e_hwc_window_queue_buffer_release(queue, queue_buffer);

   return EINA_TRUE;
}

EINTERN Eina_List *
e_hwc_window_queue_acquirable_buffers_get(E_Hwc_Window_Queue *queue)
{
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *acquirable_buffers = NULL;
   tbm_surface_queue_error_e tsq_err = TBM_SURFACE_QUEUE_ERROR_NONE;
   tbm_surface_h *tsurfaces = NULL;
   int queue_size = 0;
   int num = 0, i = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(_hwc_winq_mgr, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(queue->tqueue, NULL);

   queue_size = tbm_surface_queue_get_size(queue->tqueue);
   if (!queue_size) return NULL;

   tsurfaces = E_NEW(tbm_surface_h, queue_size);
   if (!tsurfaces)
     {
        EHWQERR("failed to alloc tsurfaces", NULL, queue->hwc, queue);
        return NULL;
     }

   tsq_err = tbm_surface_queue_get_acquirable_surfaces(queue->tqueue, tsurfaces, &num);
   if (tsq_err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        EHWQERR("failed to tbm_surface_queue_get_acquirable_surfaces", NULL, queue->hwc, queue);
        E_FREE(tsurfaces);
        return NULL;
     }

   for (i = 0; i < num; i++)
     {
        tbm_surface_h tsurface = tsurfaces[i];
        if (!tsurface) continue;

        queue_buffer = e_hwc_window_queue_buffer_find(queue, tsurface);
        if (!queue_buffer) continue;

        acquirable_buffers = eina_list_append(acquirable_buffers, queue_buffer);
     }

   E_FREE(tsurfaces);

   return acquirable_buffers;
}

EINTERN void
e_hwc_window_queue_trace_debug(Eina_Bool onoff)
{
   if (onoff == ehwq_trace) return;
   ehwq_trace = onoff;
   INF("EHWQ: hwc trace_debug is %s", onoff?"ON":"OFF");
}

static const char*
_e_hwc_window_queue_state_string_get(E_Hwc_Window_Queue_State state)
{
   switch (state)
    {
     case E_HWC_WINDOW_QUEUE_STATE_UNSET:
       return "UNSET";
     case E_HWC_WINDOW_QUEUE_STATE_UNSET_WAITING:
       return "UNSET_WAITING";
     case E_HWC_WINDOW_QUEUE_STATE_SET:
       return "SET";
     case E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_WAIT_USABLE:
       return "SET_WAITING_WAIT_USABLE";
     case E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_BUFFER:
       return "SET_WAITING_BUFFER";
     case E_HWC_WINDOW_QUEUE_STATE_SET_WAITING_DEQUEUEABLE:
       return "SET_WAITING_DEQUEUEABLE";
     case E_HWC_WINDOW_QUEUE_STATE_SET_INVALID:
       return "SET_INVALID";
     default:
       return "UNKNOWN";
    }
}

EINTERN void
e_hwc_window_queue_debug_info_get(Eldbus_Message_Iter *iter)
{
   Eldbus_Message_Iter *line_array;
   Eina_Iterator *hash_iter;
   E_Hwc_Window *hwc_window = NULL;
   E_Hwc_Window_Queue *queue = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;
   Eina_List *l;
   int idx = 0;
   int buf_idx = 0;
   int pending_set_idx = 0;
   char info_str[1024];

   eldbus_message_iter_arguments_append(iter, "as", &line_array);
   if (!_hwc_winq_mgr)
     {
        eldbus_message_iter_basic_append(line_array,
                                         's',
                                         "E_Hwc_Window_Queue not initialized..");
        eldbus_message_iter_container_close(iter, line_array);
        return;
     }

   /* PROVIDER */
   hash_iter = eina_hash_iterator_data_new(_hwc_winq_mgr->hwc_winq_hash);
   EINA_ITERATOR_FOREACH(hash_iter, queue)
     {
        if (!queue) continue;

        snprintf(info_str, sizeof(info_str),
                "[%2d] Queue(%8p) tqueue(%8p) target(%d) state(%s) user(%8p):win(0x%08zx)"
                " unset_waiting_user(%8p):win(0x%08zx)",
                ++idx, queue, queue->tqueue, queue->is_target,
                _e_hwc_window_queue_state_string_get(queue->state),
                queue->user,
                queue->user ? e_client_util_win_get(queue->user->ec) : 0,
                queue->user_waiting_unset,
                queue->user_waiting_unset ? e_client_util_win_get(queue->user_waiting_unset->ec) : 0);
        eldbus_message_iter_basic_append(line_array, 's', info_str);

        pending_set_idx = 0;
        EINA_LIST_FOREACH(queue->user_pending_set, l, hwc_window)
          {
             snprintf(info_str, sizeof(info_str),
                      " └─ [%2d] pending_set user(%8p):win(0x%08zx)",
                      ++pending_set_idx, hwc_window, e_client_util_win_get(hwc_window->ec));
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }

        buf_idx = 0;
        EINA_LIST_FOREACH(queue->buffers, l, queue_buffer)
          {
             snprintf(info_str, sizeof(info_str),
                      " └─ [%2d] Queue_Buffer(%8p) tsurface(%8p) exported_wl_buffer(%u)"
                      " exported(%d) usable(%d) released(%d) acquired(%d) dequeued(%d)",
                      ++buf_idx, queue_buffer, queue_buffer->tsurface,
                      queue_buffer->exported_wl_buffer ? wl_resource_get_id(queue_buffer->exported_wl_buffer) : 0,
                      queue_buffer->exported, queue_buffer->usable, queue_buffer->released,
                      queue_buffer->acquired, queue_buffer->dequeued);
             eldbus_message_iter_basic_append(line_array, 's', info_str);
          }
        eldbus_message_iter_basic_append(line_array, 's', "");
     }
   eina_iterator_free(hash_iter);

   eldbus_message_iter_container_close(iter, line_array);
}
