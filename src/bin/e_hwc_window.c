#include "e.h"

# include <pixman.h>
# include <wayland-tbm-server.h>
#include "services/e_service_quickpanel.h"

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#define EHWERR(f, ec, hwc, ehw, x...)                                      \
   do                                                                      \
     {                                                                     \
        if ((!ec) && (!ehw))                                               \
          ERR("EWL|%20.20s|            |             |%9s|"f,              \
              "HWC-WIN", (e_hwc_output_id_get(hwc)), ##x);                 \
        else if (!ec)                                                      \
          ERR("EWL|%20.20s|            |             |%9s|ehw :%8p|"f,     \
              "HWC-WIN", (e_hwc_output_id_get(hwc)),(ehw), ##x);           \
        else                                                               \
          ERR("EWL|%20.20s|w:0x%08zx|ec:%8p|%9s|ehw: %8p|"f,               \
              "HWC-WIN",                                                   \
              (e_client_util_win_get(ec)),                                 \
              (ec),                                                        \
              (e_hwc_output_id_get(hwc)), (ehw),                           \
              ##x);                                                        \
     }                                                                     \
   while (0)

#define EHWINF(f, ec, hwc, ehw, x...)                                      \
   do                                                                      \
     {                                                                     \
        if ((!ec) && (!ehw))                                               \
          INF("EWL|%20.20s|            |             |%9s|"f,              \
              "HWC-WIN", (e_hwc_output_id_get(hwc)), ##x);                 \
        else if (!ec)                                                      \
          INF("EWL|%20.20s|            |             |%9s|ehw :%8p|"f,     \
              "HWC-WIN", (e_hwc_output_id_get(hwc)),(ehw), ##x);           \
        else                                                               \
          INF("EWL|%20.20s|w:0x%08zx|ec:%8p|%9s|ehw :%8p|"f,               \
              "HWC-WIN",                                                   \
              (e_client_util_win_get(ec)),                                 \
              (ec),                                                        \
              (e_hwc_output_id_get(hwc)), (ehw),                           \
              ##x);                                                        \
     }                                                                     \
   while (0)

#define EHWTRACE(f, ec, hwc, ehw, x... )                                   \
   do                                                                      \
     {                                                                     \
        if (ehw_trace)                                                     \
          {                                                                \
             if ((!ec) && (!ehw))                                          \
               INF("EWL|%20.20s|            |             |%9s|"f,         \
                   "HWC-WIN", (e_hwc_output_id_get(hwc)), ##x);            \
             else if (!ec)                                                 \
               INF("EWL|%20.20s|            |             |%9s|ehw :%8p|"f,\
                   "HWC-WIN", (e_hwc_output_id_get(hwc)),(ehw), ##x);      \
             else                                                          \
               INF("EWL|%20.20s|w:0x%08zx|ec:%8p|%9s|ehw :%8p|"f,          \
                   "HWC-WIN",                                              \
                   (e_client_util_win_get(ec)),                            \
                   (ec),                                                   \
                   (e_hwc_output_id_get(hwc)), (ehw),                      \
                   ##x);                                                   \
          }                                                                \
     }                                                                     \
   while (0)

typedef enum _E_Hwc_Window_Restriction
{
   E_HWC_WINDOW_RESTRICTION_NONE,
   E_HWC_WINDOW_RESTRICTION_DELETED,
   E_HWC_WINDOW_RESTRICTION_OVERRIDE,
   E_HWC_WINDOW_RESTRICTION_ANIMATING,
   E_HWC_WINDOW_RESTRICTION_BUFFER,
   E_HWC_WINDOW_RESTRICTION_VIEWPORT,
   E_HWC_WINDOW_RESTRICTION_NEVER_HWC,
   E_HWC_WINDOW_RESTRICTION_TRANSFORM,
   E_HWC_WINDOW_RESTRICTION_BUFFER_TYPE,
   E_HWC_WINDOW_RESTRICTION_OUTPUT,
   E_HWC_WINDOW_RESTRICTION_MIN_WIDTH,
   E_HWC_WINDOW_RESTRICTION_MIN_HEIGHT,
   E_HWC_WINDOW_RESTRICTION_TOUCH_PRESS,
   E_HWC_WINDOW_RESTRICTION_OUTPUT_TRANSFORM,
   E_HWC_WINDOW_RESTRICTION_UI_SUBSURFACE,
   E_HWC_WINDOW_RESTRICTION_CONTENT_IMAGE,
   E_HWC_WINDOW_RESTRICTION_QUICKPANEL_OPEN,
} E_Hwc_Window_Restriction;

static Eina_Bool ehw_trace = EINA_FALSE;
static Eina_List *hwc_window_client_hooks = NULL;
static Eina_List *hwc_window_event_hdlrs = NULL;

static int _e_hwc_window_hooks_delete = 0;
static int _e_hwc_window_hooks_walking = 0;

typedef struct _Hwc_Window_Prop
{
   unsigned int id;
   char name[TDM_NAME_LEN];
   tdm_value value;
} Hwc_Window_Prop;

static Eina_Inlist *_e_hwc_window_hooks[] =
{
   [E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE] = NULL,
};

static void
_e_hwc_window_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Hwc_Window_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_HWC_WINDOW_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_hwc_window_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_hwc_window_hooks[x] = eina_inlist_remove(_e_hwc_window_hooks[x], EINA_INLIST_GET(ch));
          free(ch);
       }
}

static void
_e_hwc_window_hook_call(E_Hwc_Window_Hook_Point hookpoint, E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Hook *ch;

   _e_hwc_window_hooks_walking++;
   EINA_INLIST_FOREACH(_e_hwc_window_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, hwc_window);
     }
   _e_hwc_window_hooks_walking--;
   if ((_e_hwc_window_hooks_walking == 0) && (_e_hwc_window_hooks_delete > 0))
     _e_hwc_window_hooks_clean();
}

static E_Comp_Wl_Buffer *
_e_hwc_window_comp_wl_buffer_get(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Client_Data *cdata = NULL;

   if (!ec) return NULL;

   cdata = ec->comp_data;
   if (!cdata) return NULL;

   return cdata->buffer_ref.buffer;
}

struct wayland_tbm_client_queue *
_get_wayland_tbm_client_queue(E_Client *ec)
{
   struct wayland_tbm_client_queue * cqueue = NULL;
   struct wl_resource *wl_surface = NULL;
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   E_Comp_Wl_Client_Data *cdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_comp_data, NULL);

   if (!ec) return NULL;

   cdata = (E_Comp_Wl_Client_Data *)e_pixmap_cdata_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, NULL);

   wl_surface = cdata->wl_surface;
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_surface, NULL);

   cqueue = wayland_tbm_server_client_queue_get(wl_comp_data->tbm.server, wl_surface);
   if (!cqueue)
     {
        EHWINF("has no wl_tbm_server_client_queue. -- {%25s}, state:%s, zpos:%d, deleted:%s",
               ec, ec->hwc_window->hwc, ec->hwc_window,
               ec->icccm.title, e_hwc_window_state_string_get(ec->hwc_window->state),
               ec->hwc_window->zpos, (ec->hwc_window->is_deleted ? "yes" : "no"));
     }

   return cqueue;
}

static tdm_hwc_window_composition
_get_composition_type(E_Hwc_Window_State state)
{
   tdm_hwc_window_composition composition_type = TDM_HWC_WIN_COMPOSITION_NONE;

   switch (state)
     {
      case E_HWC_WINDOW_STATE_NONE:
        composition_type = TDM_HWC_WIN_COMPOSITION_NONE;
        break;
      case E_HWC_WINDOW_STATE_CLIENT:
        composition_type = TDM_HWC_WIN_COMPOSITION_CLIENT;
        break;
      case E_HWC_WINDOW_STATE_DEVICE:
        composition_type = TDM_HWC_WIN_COMPOSITION_DEVICE;
        break;
      case E_HWC_WINDOW_STATE_CURSOR:
        composition_type = TDM_HWC_WIN_COMPOSITION_CURSOR;
        break;
      case E_HWC_WINDOW_STATE_VIDEO:
        composition_type = TDM_HWC_WIN_COMPOSITION_VIDEO;
        break;
      default:
        composition_type = TDM_HWC_WIN_COMPOSITION_NONE;
        EHWERR("unknown state of hwc_window.", NULL, NULL, NULL);
     }

   return composition_type;
}

static unsigned int
_get_aligned_width(tbm_surface_h tsurface)
{
   unsigned int aligned_width = 0;
   tbm_surface_info_s surf_info;

   tbm_surface_get_info(tsurface, &surf_info);

   switch (surf_info.format)
     {
      case TBM_FORMAT_YUV420:
      case TBM_FORMAT_YVU420:
      case TBM_FORMAT_YUV422:
      case TBM_FORMAT_YVU422:
      case TBM_FORMAT_NV12:
      case TBM_FORMAT_NV21:
        aligned_width = surf_info.planes[0].stride;
        break;
      case TBM_FORMAT_YUYV:
      case TBM_FORMAT_UYVY:
        aligned_width = surf_info.planes[0].stride >> 1;
        break;
      case TBM_FORMAT_ARGB8888:
      case TBM_FORMAT_XRGB8888:
        aligned_width = surf_info.planes[0].stride >> 2;
        break;
      default:
        EHWERR("not supported format: %x", NULL, NULL, NULL, surf_info.format);
     }

   return aligned_width;
}

static void
_e_hwc_window_buffer_cb_queue_destroy(struct wl_listener *listener, void *data)
{
   E_Hwc_Window_Buffer *window_buffer = NULL;

   window_buffer = container_of(listener, E_Hwc_Window_Buffer, queue_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(window_buffer);

   if ((E_Hwc_Window_Queue *)data != window_buffer->queue) return;

   window_buffer->queue = NULL;
}

static void
_e_hwc_window_buffer_set(E_Hwc_Window_Buffer *window_buffer,
                         tbm_surface_h tsurface,
                         E_Hwc_Window_Queue *queue)
{
   if (window_buffer->queue != queue)
     {
        if (window_buffer->queue)
          wl_list_remove(&window_buffer->queue_destroy_listener.link);

        if (queue)
          {
             wl_signal_add(&queue->destroy_signal, &window_buffer->queue_destroy_listener);
             window_buffer->queue_destroy_listener.notify = _e_hwc_window_buffer_cb_queue_destroy;
          }
     }

   if (queue)
     window_buffer->from_queue = EINA_TRUE;
   else
     window_buffer->from_queue = EINA_FALSE;

   window_buffer->queue = queue;
   window_buffer->tsurface = tsurface;
}

static void
_e_hwc_window_cb_queue_destroy(struct wl_listener *listener, void *data)
{
   E_Hwc_Window *hwc_window = NULL;

   hwc_window = container_of(listener, E_Hwc_Window, queue_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if ((E_Hwc_Window_Queue *)data != hwc_window->queue) return;

   hwc_window->queue = NULL;
   hwc_window->constraints &= ~TDM_HWC_WIN_CONSTRAINT_BUFFER_QUEUE;
}

static Eina_Bool
_e_hwc_window_buffer_queue_set(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Queue *queue = NULL;
   struct wayland_tbm_client_queue *cqueue = NULL;

   cqueue = _get_wayland_tbm_client_queue(hwc_window->ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cqueue, EINA_FALSE);

   queue = e_hwc_window_queue_user_set(hwc_window);
   if (!queue)
     {
         EHWERR("fail to e_hwc_window_queue_user_set", hwc_window->ec, hwc_window->hwc, hwc_window);
         hwc_window->queue = NULL;
         return EINA_FALSE;
     }

   wl_signal_add(&queue->destroy_signal, &hwc_window->queue_destroy_listener);
   hwc_window->queue_destroy_listener.notify = _e_hwc_window_cb_queue_destroy;
   hwc_window->queue = queue;

   EHWTRACE("Set constranints BUFFER_QUEUE -- {%s}",
             hwc_window->ec, hwc_window->hwc, hwc_window, e_client_util_name_get(hwc_window->ec));

   return EINA_TRUE;
}

static void
_e_hwc_window_buffer_queue_unset(E_Hwc_Window *hwc_window)
{
   /* reset the TDM_HWC_WIN_CONSTRAINT_BUFFER_QUEUE */
   if (hwc_window->queue)
     {
        e_hwc_window_queue_user_unset(hwc_window->queue, hwc_window);
        wl_list_remove(&hwc_window->queue_destroy_listener.link);
        hwc_window->queue = NULL;
     }

    EHWTRACE("Unset constranints BUFFER_QUEUE -- {%s}",
              hwc_window->ec, hwc_window->hwc, hwc_window, e_client_util_name_get(hwc_window->ec));
}

static void
_e_hwc_window_constraints_reset(E_Hwc_Window *hwc_window)
{
   _e_hwc_window_buffer_queue_unset(hwc_window);

   hwc_window->constraints = TDM_HWC_WIN_CONSTRAINT_NONE;

   EHWTRACE("Reset constranints -- {%s}",
            hwc_window->ec, hwc_window->hwc, hwc_window, e_client_util_name_get(hwc_window->ec));
}

static void
_e_hwc_window_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Output *output;
   E_Zone *zone;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_RETURN(zone);
   EINA_SAFETY_ON_NULL_RETURN(zone->output_id);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_RETURN(output);

   /* If an e_client belongs to the e_output managed by hwc_plane policy,
    * there's no need to deal with hwc_windows. */
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     return;

   if (!ec->hwc_window) return;
   if (e_hwc_window_is_video(ec->hwc_window)) return;

   _e_hwc_window_constraints_reset(ec->hwc_window);

   e_hwc_window_free(ec->hwc_window);
}

static Eina_Bool
_e_hwc_window_client_cb_zone_set(void *data, int type, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   E_Zone *zone;
   E_Output *output;
   E_Hwc_Window *hwc_window = NULL;

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_GOTO(ec, end);

   zone = ec->zone;
   EINA_SAFETY_ON_NULL_GOTO(zone, end);
   EINA_SAFETY_ON_NULL_GOTO(zone->output_id, end);

   output = e_output_find(zone->output_id);
   EINA_SAFETY_ON_NULL_GOTO(output, end);

   /* If an e_client belongs to the e_output managed by hwc_plane policy,
    * there's no need to deal with hwc_windows. */
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     return ECORE_CALLBACK_PASS_ON;

   if (e_object_is_del(E_OBJECT(ec)))
     return ECORE_CALLBACK_PASS_ON;

   if (ec->hwc_window)
     {
        /* we manage the video window in the video module */
        if (e_hwc_window_is_video(ec->hwc_window)) goto end;
        if (ec->hwc_window->hwc == output->hwc) goto end;

        e_hwc_window_free(ec->hwc_window);
     }

   hwc_window = e_hwc_window_new(output->hwc, ec, E_HWC_WINDOW_STATE_NONE);
   EINA_SAFETY_ON_NULL_GOTO(hwc_window, end);

   EHWINF("set on eout:%p, zone_id:%d.",
          ec, hwc_window->hwc, hwc_window, output, zone->id);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static tbm_surface_h
_e_hwc_window_client_surface_acquire(E_Hwc_Window *hwc_window)
{
   E_Comp_Wl_Buffer *buffer = _e_hwc_window_comp_wl_buffer_get(hwc_window);
   E_Comp_Wl_Data *wl_comp_data = (E_Comp_Wl_Data *)e_comp->wl_comp_data;
   tbm_surface_h tsurface = NULL;

   if (!buffer) return NULL;

   switch (buffer->type)
     {
       case E_COMP_WL_BUFFER_TYPE_SHM:
         break;
       case E_COMP_WL_BUFFER_TYPE_NATIVE:
       case E_COMP_WL_BUFFER_TYPE_VIDEO:
         tsurface = wayland_tbm_server_get_surface(wl_comp_data->tbm.server, buffer->resource);
         break;
       case E_COMP_WL_BUFFER_TYPE_TBM:
         tsurface = buffer->tbm_surface;
         break;
       default:
         EHWERR("not supported buffer type:%d", hwc_window->ec, hwc_window->hwc, hwc_window, buffer->type);
         break;
     }

   return tsurface;
}

static void
_e_hwc_window_cb_cursor_buffer_destroy(struct wl_listener *listener, void *data)
{
   E_Hwc_Window *hwc_window = NULL;

   hwc_window = container_of(listener, E_Hwc_Window, cursor_buffer_destroy_listener);
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if ((E_Comp_Wl_Buffer *)data != hwc_window->cursor.buffer) return;

   if (hwc_window->cursor.buffer)
     wl_list_remove(&hwc_window->cursor_buffer_destroy_listener.link);

   hwc_window->cursor.buffer = NULL;
}

static Eina_Bool
_e_hwc_window_cursor_image_update(E_Hwc_Window *hwc_window)
{
   E_Client *ec = hwc_window->ec;
   E_Comp_Wl_Buffer *buffer = NULL;
   E_Pointer *pointer = NULL;
   int img_w, img_h, img_stride;
   void *img_ptr = NULL;
   tdm_error error;

   pointer = e_pointer_get(ec);
   buffer = _e_hwc_window_comp_wl_buffer_get(hwc_window);
   if (!buffer || !pointer)
     {
        if (hwc_window->cursor.img_ptr)
          {
             error = tdm_hwc_window_set_cursor_image(hwc_window->thwc_window, 0, 0, 0, NULL);
             if (error != TDM_ERROR_NONE)
               {
                  EHWERR("fail to set cursor image to thwc(%p)", hwc_window->ec, hwc_window->hwc, hwc_window, hwc_window->thwc_window);
                  return EINA_FALSE;
               }

             if (hwc_window->cursor.buffer)
               wl_list_remove(&hwc_window->cursor_buffer_destroy_listener.link);

             hwc_window->cursor.buffer = NULL;
             hwc_window->cursor.rotation = 0;
             hwc_window->cursor.img_ptr = NULL;
             hwc_window->cursor.img_w = 0;
             hwc_window->cursor.img_h = 0;
             hwc_window->cursor.img_stride = 0;

             return EINA_TRUE;
          }

        return EINA_FALSE;
     }

   /* cursor image is the shm image from the wl_clients */
   if (buffer->type == E_COMP_WL_BUFFER_TYPE_SHM)
     {
        img_ptr = wl_shm_buffer_get_data(buffer->shm_buffer);
        if (!img_ptr)
          {
             EHWERR("Failed get data shm buffer", hwc_window->ec, hwc_window->hwc, hwc_window);
             return EINA_FALSE;
          }
        img_w = wl_shm_buffer_get_width(buffer->shm_buffer);
        img_h = wl_shm_buffer_get_height(buffer->shm_buffer);
        img_stride = wl_shm_buffer_get_stride(buffer->shm_buffer);
     }
   else
     {
        EHWERR("unkown buffer type:%d", NULL, hwc_window->hwc, hwc_window, ec->comp_data->buffer_ref.buffer->type);
        return EINA_FALSE;
     }

   /* no changes, no need to update the cursor image */
   if ((hwc_window->cursor.buffer == buffer) &&
       (hwc_window->cursor.rotation == pointer->rotation))
     return EINA_FALSE;

   error = tdm_hwc_window_set_cursor_image(hwc_window->thwc_window, img_w, img_h, img_stride, img_ptr);
   if (error != TDM_ERROR_NONE)
     {
        EHWERR("fail to set cursor image to thwc(%p)", hwc_window->ec, hwc_window->hwc, hwc_window, hwc_window->thwc_window);
        return EINA_FALSE;
     }

   if (hwc_window->cursor.buffer)
     wl_list_remove(&hwc_window->cursor_buffer_destroy_listener.link);

   hwc_window->cursor.buffer = buffer;
   wl_signal_add(&buffer->destroy_signal, &hwc_window->cursor_buffer_destroy_listener);
   hwc_window->cursor_buffer_destroy_listener.notify = _e_hwc_window_cb_cursor_buffer_destroy;
   hwc_window->cursor.rotation = pointer->rotation;
   hwc_window->cursor.img_ptr = img_ptr;
   hwc_window->cursor.img_w = img_w;
   hwc_window->cursor.img_h = img_h;
   hwc_window->cursor.img_stride = img_stride;

   return EINA_TRUE;
}

static void
_e_hwc_window_cursor_position_get(E_Pointer *ptr, int width, int height, unsigned int *x, unsigned int *y)
{
   int rotation;;

   rotation = ptr->rotation;

   switch (rotation)
     {
      case 0:
        *x = ptr->x - ptr->hot.x;
        *y = ptr->y - ptr->hot.y;
        break;
      case 90:
        *x = ptr->x - ptr->hot.y;
        *y = ptr->y + ptr->hot.x - width;
        break;
      case 180:
        *x = ptr->x + ptr->hot.x - width;
        *y = ptr->y + ptr->hot.y - height;
        break;
      case 270:
        *x = ptr->x + ptr->hot.y - height;
        *y = ptr->y - ptr->hot.x;
        break;
      default:
        *x = ptr->x - ptr->hot.x;
        *y = ptr->y - ptr->hot.y;
        break;
     }
}

static void
_e_hwc_window_update_fps(E_Hwc_Window *hwc_window)
{
   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - hwc_window->fps.frametimes[0];
        hwc_window->fps.frametimes[0] = tim;

        hwc_window->fps.time += dt;
        hwc_window->fps.cframes++;

        if (hwc_window->fps.lapse == 0.0)
          {
             hwc_window->fps.lapse = tim;
             hwc_window->fps.flapse = hwc_window->fps.cframes;
          }
        else if ((tim - hwc_window->fps.lapse) >= 0.5)
          {
             hwc_window->fps.fps = (hwc_window->fps.cframes - hwc_window->fps.flapse) /
                                   (tim - hwc_window->fps.lapse);
             hwc_window->fps.lapse = tim;
             hwc_window->fps.flapse = hwc_window->fps.cframes;
             hwc_window->fps.time = 0.0;
          }
     }
}

static void
_e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   E_Hwc *hwc = NULL;
   Hwc_Window_Prop *prop;
   E_Output *output = NULL;
   tdm_output *toutput = NULL;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_GOTO(hwc, done);

   hwc->hwc_windows = eina_list_remove(hwc->hwc_windows, hwc_window);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_GOTO(hwc->output, done);

   toutput = output->toutput;
   EINA_SAFETY_ON_NULL_GOTO(toutput, done);

   if(hwc_window->prop_list)
     {
        EINA_LIST_FREE(hwc_window->prop_list, prop)
          {
             free(prop);
          }
     }

   if (hwc_window->thwc_window)
     tdm_hwc_window_destroy(hwc_window->thwc_window);

   EHWINF("Free", NULL, hwc_window->hwc, hwc_window);

done:
   if (hwc_window->cursor.buffer)
     wl_list_remove(&hwc_window->cursor_buffer_destroy_listener.link);

   if (hwc_window->queue)
     wl_list_remove(&hwc_window->queue_destroy_listener.link);

   if (hwc_window->buffer.queue)
     wl_list_remove(&hwc_window->buffer.queue_destroy_listener.link);

   if (hwc_window->display.buffer.queue)
     wl_list_remove(&hwc_window->display.buffer.queue_destroy_listener.link);

   E_FREE(hwc_window);
}

static E_Hwc_Window_Commit_Data *
_e_hwc_window_commit_data_acquire_device(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, NULL);

   memcpy(&commit_data->info, &hwc_window->info, sizeof(tdm_hwc_window_info));

   _e_hwc_window_buffer_set(&commit_data->buffer, hwc_window->buffer.tsurface,
                            hwc_window->buffer.queue);

   tbm_surface_internal_ref(commit_data->buffer.tsurface);

   _e_hwc_window_update_fps(hwc_window);

   if (!e_hwc_window_is_target(hwc_window) &&
       !e_hwc_window_is_video(hwc_window))
     e_comp_wl_buffer_reference(&commit_data->buffer_ref,
                                _e_hwc_window_comp_wl_buffer_get(hwc_window));

   return commit_data;
}

EINTERN Eina_Bool
e_hwc_window_init(void)
{
   E_LIST_HOOK_APPEND(hwc_window_client_hooks, E_CLIENT_HOOK_DEL,
                      _e_hwc_window_client_cb_del, NULL);
   E_LIST_HANDLER_APPEND(hwc_window_event_hdlrs, E_EVENT_CLIENT_ZONE_SET,
                         _e_hwc_window_client_cb_zone_set, NULL);

   return EINA_TRUE;
}

EINTERN void
e_hwc_window_deinit(void)
{
   E_FREE_LIST(hwc_window_client_hooks, e_client_hook_del);
   E_FREE_LIST(hwc_window_event_hdlrs, ecore_event_handler_del);
}

EINTERN void
e_hwc_window_free(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   EHWINF("Del", hwc_window->ec, hwc_window->hwc, hwc_window);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   ec->hwc_window = NULL;
   hwc_window->ec = NULL;
   hwc_window->is_deleted = EINA_TRUE;

   if (hwc_window->queue)
     {
        e_hwc_window_queue_user_unset(hwc_window->queue, hwc_window);
        hwc_window->queue = NULL;
     }

   e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_NONE, EINA_TRUE);

   e_object_del(E_OBJECT(hwc_window));
}

EINTERN E_Hwc_Window *
e_hwc_window_new(E_Hwc *hwc, E_Client *ec, E_Hwc_Window_State state)
{
   E_Hwc_Window *hwc_window = NULL;
   tdm_hwc *thwc;;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), NULL);

   if (ec->hwc_window) goto end;

   thwc = hwc->thwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc, EINA_FALSE);

   hwc_window = E_OBJECT_ALLOC(E_Hwc_Window, E_HWC_WINDOW_TYPE, _e_hwc_window_free);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   hwc_window->hwc = hwc;
   hwc_window->ec = ec;
   hwc_window->zpos = E_HWC_WINDOW_ZPOS_NONE;
   hwc_window->render_target = EINA_TRUE;
   hwc_window->device_state_available = EINA_TRUE;

   hwc_window->thwc_window = tdm_hwc_create_window(thwc, &error);
   if (error != TDM_ERROR_NONE)
     {
        EHWERR("cannot create tdm_hwc_window for thwc(%p)", hwc_window->ec, hwc_window->hwc, hwc_window, thwc);
        E_FREE(hwc_window);
        return NULL;
     }

   /* cursor window */
   if (e_policy_client_is_cursor(ec))
     hwc_window->is_cursor = EINA_TRUE;

   /* set the hwc window to the e client */
   ec->hwc_window = hwc_window;

   hwc->hwc_windows = eina_list_append(hwc->hwc_windows, hwc_window);

   EHWINF("is created on eout:%p, zone_id:%d video:%d cursor:%d",
          hwc_window->ec, hwc_window->hwc, hwc_window, hwc->output, ec->zone->id,
          hwc_window->is_video, hwc_window->is_cursor);

end:
   /* video window */
   if (state == E_HWC_WINDOW_STATE_VIDEO)
     ec->hwc_window->is_video = EINA_TRUE;

   e_hwc_window_state_set(ec->hwc_window, state, EINA_TRUE);

   return ec->hwc_window;
}

EINTERN Eina_Bool
e_hwc_window_zpos_set(E_Hwc_Window *hwc_window, int zpos)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->zpos != zpos) hwc_window->zpos = zpos;

   return EINA_TRUE;
}

EINTERN int
e_hwc_window_zpos_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->state == E_HWC_WINDOW_STATE_NONE) return -999;

   return hwc_window->zpos;
}

EINTERN Eina_Bool
e_hwc_window_composition_update(E_Hwc_Window *hwc_window)
{
   tdm_hwc_window *thwc_window;
   tdm_hwc_window_composition composition_type;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (e_hwc_window_is_target(hwc_window))
     {
        EHWERR("target window cannot update at e_hwc_window_composition_update.", hwc_window->ec, hwc_window->hwc, hwc_window);
        return EINA_FALSE;
     }

   thwc_window = hwc_window->thwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc_window, EINA_FALSE);

   /* set composition type */
   composition_type = _get_composition_type(hwc_window->state);
   error = tdm_hwc_window_set_composition_type(hwc_window->thwc_window, composition_type);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_target(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_target;
}

EINTERN Eina_Bool
e_hwc_window_is_video(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_video;
}

EINTERN Eina_Bool
e_hwc_window_is_cursor(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->is_cursor;
}

static Eina_Bool
_e_hwc_window_cursor_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   E_Hwc *hwc = NULL;
   E_Output *output = NULL;
   E_Client *ec = NULL;
   E_Pointer *pointer = NULL;

   if (!e_hwc_window_is_cursor(hwc_window)) return EINA_FALSE;

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   pointer = e_pointer_get(ec);
   if (!pointer) return EINA_TRUE;

   hwc_win_info->src_config.format = TBM_FORMAT_ARGB8888;
   hwc_win_info->src_config.pos.x = 0;
   hwc_win_info->src_config.pos.y = 0;
   hwc_win_info->src_config.pos.w = hwc_window->cursor.img_w;
   hwc_win_info->src_config.pos.h = hwc_window->cursor.img_h;

   hwc_win_info->src_config.size.h = hwc_window->cursor.img_stride >> 2;
   EINA_SAFETY_ON_TRUE_RETURN_VAL(hwc_win_info->src_config.size.h == 0, EINA_FALSE);
   hwc_win_info->src_config.size.v = hwc_window->cursor.img_h;

   _e_hwc_window_cursor_position_get(pointer,
                                     hwc_win_info->src_config.pos.w,
                                     hwc_win_info->src_config.pos.h,
                                     &hwc_win_info->dst_pos.x,
                                     &hwc_win_info->dst_pos.y);

   if (output->config.rotation > 0)
     {
        int bw, bh;
        int dst_x, dst_y;

        e_pixmap_size_get(ec->pixmap, &bw, &bh);
        e_comp_wl_rect_convert(ec->zone->w, ec->zone->h,
                               output->config.rotation / 90, 1,
                               hwc_win_info->dst_pos.x, hwc_win_info->dst_pos.y,
                               bw, bh,
                               &dst_x, &dst_y,
                               NULL, NULL);

        hwc_win_info->dst_pos.x = dst_x;
        hwc_win_info->dst_pos.y = dst_y;
     }

   hwc_win_info->dst_pos.w = hwc_window->cursor.img_w;
   hwc_win_info->dst_pos.h = hwc_window->cursor.img_h;

   // TODO: need to calculation with cursor.rotation and output->config.rotation?
   hwc_win_info->transform = hwc_window->cursor.rotation;

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   E_Hwc *hwc = NULL;
   E_Output *output = NULL;
   E_Client *ec = NULL;
   tbm_surface_h tsurface = NULL;
   tbm_surface_info_s surf_info = {0};
   int x, y, w, h;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   hwc = hwc_window->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   output = hwc->output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   tsurface = hwc_window->buffer.tsurface;
   if (!tsurface) return EINA_TRUE;

   tbm_surface_get_info(tsurface, &surf_info);

   hwc_win_info->src_config.format = surf_info.format;
   hwc_win_info->src_config.pos.x = 0;
   hwc_win_info->src_config.pos.y = 0;
   hwc_win_info->src_config.pos.w = surf_info.width;
   hwc_win_info->src_config.pos.h = surf_info.height;

   hwc_win_info->src_config.size.h = _get_aligned_width(tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(hwc_win_info->src_config.size.h == 0, EINA_FALSE);
   hwc_win_info->src_config.size.v = surf_info.height;

   e_client_geometry_get(ec, &x, &y, &w, &h);

   hwc_win_info->dst_pos.x = x;
   hwc_win_info->dst_pos.y = y;

   if (output->config.rotation > 0)
     {
        int bw, bh;
        int dst_x, dst_y;

        e_pixmap_size_get(ec->pixmap, &bw, &bh);
        e_comp_wl_rect_convert(ec->zone->w, ec->zone->h,
                               output->config.rotation / 90, 1,
                               hwc_win_info->dst_pos.x, hwc_win_info->dst_pos.y,
                               bw, bh,
                               &dst_x, &dst_y,
                               NULL, NULL);

        hwc_win_info->dst_pos.x = dst_x;
        hwc_win_info->dst_pos.y = dst_y;
     }

   hwc_win_info->dst_pos.w = w;
   hwc_win_info->dst_pos.h = h;

   // TODO: need to calculation with ec(window) rotation and output->config.rotation?
   hwc_win_info->transform = TDM_TRANSFORM_NORMAL;

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_target_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   tbm_surface_h tsurface = NULL;
   tbm_surface_info_s surf_info = {0};

   if (!e_hwc_window_is_target(hwc_window)) return EINA_FALSE;

   tsurface = hwc_window->buffer.tsurface;
   if (!tsurface) return EINA_TRUE;

   tbm_surface_get_info(tsurface, &surf_info);

   hwc_win_info->src_config.format = surf_info.format;
   hwc_win_info->src_config.pos.x = 0;
   hwc_win_info->src_config.pos.y = 0;
   hwc_win_info->src_config.pos.w = surf_info.width;
   hwc_win_info->src_config.pos.h = surf_info.height;

   hwc_win_info->src_config.size.h = _get_aligned_width(tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(hwc_win_info->src_config.size.h == 0, EINA_FALSE);
   hwc_win_info->src_config.size.v = surf_info.height;

   hwc_win_info->dst_pos.x = 0;
   hwc_win_info->dst_pos.y = 0;
   hwc_win_info->dst_pos.w = surf_info.width;
   hwc_win_info->dst_pos.h = surf_info.height;

   // TODO: need to calculation with ec(window) rotation and output->config.rotation?
   hwc_win_info->transform = TDM_TRANSFORM_NORMAL;

   return EINA_TRUE;
}

static Eina_Bool
_e_hwc_window_video_info_get(E_Hwc_Window *hwc_window, tdm_hwc_window_info *hwc_win_info)
{
   E_Client *ec = NULL;
   E_Client_Video_Info vinfo;

   if (!e_hwc_window_is_video(hwc_window)) return EINA_FALSE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!e_client_video_info_get(ec, &vinfo))
     {
        EHWERR("Video window does not get the video info.", hwc_window->ec, hwc_window->hwc, hwc_window);
        return EINA_FALSE;
     }

   memcpy(&hwc_win_info->src_config, &vinfo.src_config, sizeof(tdm_info_config));
   memcpy(&hwc_win_info->dst_pos, &vinfo.dst_pos, sizeof(tdm_pos));
   hwc_win_info->transform = vinfo.transform;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_info_update(E_Hwc_Window *hwc_window)
{
   tdm_hwc_window_info hwc_win_info = {0};

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_deleted) return EINA_FALSE;

   if (e_hwc_window_is_cursor(hwc_window))
     {
        if (!_e_hwc_window_cursor_info_get(hwc_window, &hwc_win_info))
          {
             EHWERR("fail to _e_hwc_window_cursor_info_get",
                    hwc_window->ec, hwc_window->hwc, hwc_window);
             return EINA_FALSE;
          }
     }
   else if (e_hwc_window_is_video(hwc_window))
     {
        if (!_e_hwc_window_video_info_get(hwc_window, &hwc_win_info))
          {
             EHWERR("fail to _e_hwc_window_video_info_get",
                    hwc_window->ec, hwc_window->hwc, hwc_window);
             return EINA_FALSE;
          }
     }
   else if (hwc_window->is_target)
     {
        if (!_e_hwc_window_target_info_get(hwc_window, &hwc_win_info))
          {
             EHWERR("fail to _e_hwc_window_target_info_get",
                    hwc_window->ec, hwc_window->hwc, hwc_window);
             return EINA_FALSE;
          }
     }
   else
     {
        if (!_e_hwc_window_info_get(hwc_window, &hwc_win_info))
          {
             EHWERR("fail to _e_hwc_window_info_get",
                    hwc_window->ec, hwc_window->hwc, hwc_window);
             return EINA_FALSE;
          }
     }

   if (memcmp(&hwc_window->info, &hwc_win_info, sizeof(tdm_hwc_window_info)))
     {
        tdm_error error;

        memcpy(&hwc_window->info, &hwc_win_info, sizeof(tdm_hwc_window_info));

        if (!e_hwc_window_is_target(hwc_window))
          {
             error = tdm_hwc_window_set_info(hwc_window->thwc_window, &hwc_window->info);
             EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);
          }

        EHWTRACE("INF src(%dx%d+%d+%d size:%dx%d fmt:%c%c%c%c) dst(%dx%d+%d+%d) trans(%d)",
                  hwc_window->ec, hwc_window->hwc, hwc_window,
                  hwc_window->info.src_config.pos.w, hwc_window->info.src_config.pos.h,
                  hwc_window->info.src_config.pos.x, hwc_window->info.src_config.pos.y,
                  hwc_window->info.src_config.size.h, hwc_window->info.src_config.size.v,
                  EHW_FOURCC_STR(hwc_window->info.src_config.format),
                  hwc_window->info.dst_pos.w, hwc_window->info.dst_pos.h,
                  hwc_window->info.dst_pos.x, hwc_window->info.dst_pos.y,
                  hwc_window->info.transform);

        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_window_buffer_fetch(E_Hwc_Window *hwc_window, Eina_Bool tdm_set)
{
   tbm_surface_h tsurface = NULL;
   tdm_hwc_window *thwc_window = NULL;
   tdm_error error = TDM_ERROR_NONE;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL, *queue_buffer2 = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   thwc_window = hwc_window->thwc_window;
   EINA_SAFETY_ON_NULL_RETURN_VAL(thwc_window, EINA_FALSE);

   if (e_hwc_window_is_cursor(hwc_window))
     {
        if (!_e_hwc_window_cursor_image_update(hwc_window))
          return EINA_FALSE;

        EHWTRACE("FET img_ptr:%p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
                 hwc_window->ec, hwc_window->hwc, hwc_window,
                 hwc_window->cursor.img_ptr, e_hwc_window_name_get(hwc_window),
                 e_hwc_window_state_string_get(hwc_window->state),
                 hwc_window->zpos, (hwc_window->is_deleted ? "yes" : "no"));

        return EINA_TRUE;
     }

   if (hwc_window->is_deleted)
     {
        tsurface = NULL;
        if (!hwc_window->buffer.tsurface) return EINA_FALSE;
     }
   /* for video we set buffer in the video module */
   else if (e_hwc_window_is_video(hwc_window))
     {
        tsurface = e_client_video_tbm_surface_get(hwc_window->ec);

        if (tsurface == hwc_window->buffer.tsurface) return EINA_FALSE;
     }
   else
     {
        /* acquire the surface */
        tsurface = _e_hwc_window_client_surface_acquire(hwc_window);
        if (tsurface == hwc_window->buffer.tsurface) return EINA_FALSE;
     }

   if (hwc_window->buffer.tsurface && hwc_window->buffer.queue &&
       hwc_window->buffer.tsurface != hwc_window->display.buffer.tsurface)
     {
        queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->buffer.queue, hwc_window->buffer.tsurface);
        if (queue_buffer)
          e_hwc_window_queue_buffer_release(hwc_window->buffer.queue, queue_buffer);
     }

   if (tsurface && hwc_window->queue)
     {
        queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->queue, tsurface);
        if (queue_buffer)
          {
            e_hwc_window_queue_buffer_enqueue(hwc_window->queue, queue_buffer);
            queue_buffer2 = e_hwc_window_queue_buffer_acquire(hwc_window->queue);
            while (queue_buffer != queue_buffer2)
              {
                  e_hwc_window_queue_buffer_release(hwc_window->queue, queue_buffer2);
                  queue_buffer2 = e_hwc_window_queue_buffer_acquire(hwc_window->queue);
                  if (!queue_buffer2)
                    {
                      EHWERR("fail to acquire buffer:%p tsurface:%p",
                          hwc_window->ec, hwc_window->hwc, hwc_window,
                          queue_buffer, queue_buffer->tsurface);
                      return EINA_FALSE;
                    }
              }
          }
     }

   EHWTRACE("FET ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s cursor:%d video:%d",
            hwc_window->ec, hwc_window->hwc, hwc_window,
            tsurface, e_hwc_window_name_get(hwc_window),
            e_hwc_window_state_string_get(hwc_window->state),
            hwc_window->zpos, (hwc_window->is_deleted ? "yes" : "no"),
            e_hwc_window_is_cursor(hwc_window), e_hwc_window_is_video(hwc_window));

   /* exist tsurface for update hwc_window */
   if (tsurface)
     _e_hwc_window_buffer_set(&hwc_window->buffer, tsurface, hwc_window->queue);
   else
     _e_hwc_window_buffer_set(&hwc_window->buffer, NULL, NULL);

   if (tdm_set)
     error = tdm_hwc_window_set_buffer(thwc_window, hwc_window->buffer.tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(error != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_prop_update(E_Hwc_Window *hwc_window)
{
   Hwc_Window_Prop *prop;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   EINA_LIST_FREE(hwc_window->prop_list, prop)
     {
        if (!e_hwc_window_set_property(hwc_window, prop->id, prop->name, prop->value, EINA_TRUE))
          EHWERR("cannot update prop", hwc_window->ec, hwc_window->hwc, hwc_window);
        free(prop);
        ret = EINA_TRUE;
     }

   return ret;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_acquire(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;

    if (hwc_window->accepted_state == E_HWC_WINDOW_STATE_CURSOR)
      {
        commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

        e_comp_wl_buffer_reference(&commit_data->buffer_ref,
                                   _e_hwc_window_comp_wl_buffer_get(hwc_window));
      }
    else if (hwc_window->accepted_state == E_HWC_WINDOW_STATE_DEVICE ||
             hwc_window->accepted_state == E_HWC_WINDOW_STATE_VIDEO)
     {
        if (!hwc_window->buffer.tsurface) return EINA_FALSE;
        if ((hwc_window->buffer.tsurface == hwc_window->display.buffer.tsurface) &&
            (!memcmp(&hwc_window->info, &hwc_window->display.info, sizeof(tdm_hwc_window_info))))
          return EINA_FALSE;

        commit_data = _e_hwc_window_commit_data_acquire_device(hwc_window);
        EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);
     }
   else
     {
        if (!hwc_window->display.buffer.tsurface) return EINA_FALSE;

        commit_data = E_NEW(E_Hwc_Window_Commit_Data, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);

        _e_hwc_window_buffer_set(&commit_data->buffer, NULL, NULL);
     }

   EHWTRACE("COM ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
            hwc_window->ec, hwc_window->hwc, hwc_window,
            commit_data->buffer.tsurface,
            e_hwc_window_name_get(hwc_window),
            e_hwc_window_state_string_get(hwc_window->state),
            hwc_window->zpos, (hwc_window->is_deleted ? "yes" : "no"));

   e_object_ref(E_OBJECT(hwc_window));

   hwc_window->commit_data = commit_data;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_commit_data_release(E_Hwc_Window *hwc_window)
{
   tbm_surface_h tsurface = NULL;
   E_Hwc_Window_Queue *queue = NULL;
   E_Hwc_Window_Queue_Buffer *queue_buffer = NULL;

   /* we don't have data to release */
   if (!hwc_window->commit_data) return EINA_FALSE;

   tsurface = hwc_window->commit_data->buffer.tsurface;
   queue = hwc_window->commit_data->buffer.queue;

   EHWTRACE("DON ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s (Window)",
            hwc_window->ec, hwc_window->hwc, hwc_window,
            tsurface, e_hwc_window_name_get(hwc_window),
            e_hwc_window_state_string_get(hwc_window->state),
            hwc_window->zpos, (hwc_window->is_deleted ? "yes" : "no"));

   if (e_hwc_window_is_cursor(hwc_window))
     {
        e_comp_wl_buffer_reference(&hwc_window->commit_data->buffer_ref, NULL);
        free(hwc_window->commit_data);
        hwc_window->commit_data = NULL;
        e_object_unref(E_OBJECT(hwc_window));

        return EINA_TRUE;
     }

   if (!tsurface)
     {
        if (hwc_window->display.buffer.tsurface)
          {
             if (hwc_window->display.buffer.queue)
               {
                  queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->display.buffer.queue,
                                                               hwc_window->display.buffer.tsurface);
                  if (queue_buffer)
                    e_hwc_window_queue_buffer_release(hwc_window->display.buffer.queue, queue_buffer);
               }

             tbm_surface_internal_unref(hwc_window->display.buffer.tsurface);
             e_object_unref(E_OBJECT(hwc_window));
          }

        e_comp_wl_buffer_reference(&hwc_window->display.buffer_ref, NULL);
        _e_hwc_window_buffer_set(&hwc_window->display.buffer, NULL, NULL);

        CLEAR(hwc_window->display.info);
     }
   else
     {
        /* release and unreference the previous surface */
        if (hwc_window->display.buffer.tsurface)
          {
             if ((hwc_window->display.buffer.queue) &&
                 (hwc_window->display.buffer.tsurface != tsurface))
               {
                  queue_buffer = e_hwc_window_queue_buffer_find(hwc_window->display.buffer.queue,
                                                               hwc_window->display.buffer.tsurface);
                  if (queue_buffer)
                    e_hwc_window_queue_buffer_release(hwc_window->display.buffer.queue, queue_buffer);
               }

             tbm_surface_internal_unref(hwc_window->display.buffer.tsurface);
             e_object_unref(E_OBJECT(hwc_window));
          }

        if (hwc_window->commit_data->buffer_ref.buffer)
          e_comp_wl_buffer_reference(&hwc_window->display.buffer_ref,
                                     hwc_window->commit_data->buffer_ref.buffer);
        /* update hwc_window display info */
        _e_hwc_window_buffer_set(&hwc_window->display.buffer, tsurface, queue);

        memcpy(&hwc_window->display.info, &hwc_window->commit_data->info, sizeof(tdm_hwc_window_info));

        e_object_ref(E_OBJECT(hwc_window));
     }

   e_comp_wl_buffer_reference(&hwc_window->commit_data->buffer_ref, NULL);
   _e_hwc_window_buffer_set(&hwc_window->commit_data->buffer, NULL, NULL);

   free(hwc_window->commit_data);
   hwc_window->commit_data = NULL;
   e_object_unref(E_OBJECT(hwc_window));

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_activate(E_Hwc_Window *hwc_window, E_Hwc_Window_Queue *queue)
{
   struct wayland_tbm_client_queue *cqueue = NULL;
   int flush = 0;
   int queue_size = 0, queue_width = 0, queue_height = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window->ec, EINA_FALSE);

   if (hwc_window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED)
     return EINA_TRUE;

   if (e_hwc_window_is_cursor(hwc_window)) return EINA_TRUE;

   if (queue)
     {
        flush = 1;
        queue_size = tbm_surface_queue_get_size(queue->tqueue);
        queue_width = tbm_surface_queue_get_width(queue->tqueue);
        queue_height = tbm_surface_queue_get_height(queue->tqueue);
     }

   cqueue = _get_wayland_tbm_client_queue(hwc_window->ec);
   if (cqueue)
     wayland_tbm_server_client_queue_activate(cqueue, 0, queue_size,
                                              queue_width, queue_height, flush);

   EHWINF("Activate -- ehwq:%p {%s}",
          hwc_window->ec, hwc_window->hwc, hwc_window, queue,
          e_hwc_window_name_get(hwc_window));

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_ACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_deactivate(E_Hwc_Window *hwc_window)
{
   struct wayland_tbm_client_queue * cqueue = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window->ec, EINA_FALSE);

   if (hwc_window->activation_state == E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED)
     return EINA_TRUE;

   if (e_hwc_window_is_cursor(hwc_window)) return EINA_TRUE;

   cqueue = _get_wayland_tbm_client_queue(hwc_window->ec);
   if (cqueue)
     wayland_tbm_server_client_queue_deactivate(cqueue);

   EHWINF("Deactivate -- {%s}",
          hwc_window->ec, hwc_window->hwc, hwc_window,
          e_hwc_window_name_get(hwc_window));

   hwc_window->activation_state = E_HWC_WINDOW_ACTIVATION_STATE_DEACTIVATED;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_is_on_hw_overlay(E_Hwc_Window *hwc_window)
{
   E_Hwc_Window_State accepted_state = E_HWC_WINDOW_STATE_NONE;
   E_Hwc_Window_State state = E_HWC_WINDOW_STATE_NONE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   accepted_state = hwc_window->accepted_state;
   state = hwc_window->state;

   if ((accepted_state == E_HWC_WINDOW_STATE_DEVICE) ||
       (accepted_state == E_HWC_WINDOW_STATE_CURSOR) ||
       (accepted_state == E_HWC_WINDOW_STATE_VIDEO))
     {
        if (accepted_state == state)
          return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN tbm_surface_h
e_hwc_window_displaying_surface_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, NULL);

   return hwc_window->display.buffer.tsurface;
}

EINTERN Eina_Bool
e_hwc_window_accepted_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->accepted_state == state) return EINA_TRUE;

   hwc_window->accepted_state = state;

   if (hwc_window->accepted_state == E_HWC_WINDOW_STATE_NONE)
     hwc_window->zpos = E_HWC_WINDOW_ZPOS_NONE;

   EHWINF("Set Accepted state:%s -- {%s}",
           hwc_window->ec, hwc_window->hwc, hwc_window, e_hwc_window_state_string_get(state),
           e_hwc_window_name_get(hwc_window));

   _e_hwc_window_hook_call(E_HWC_WINDOW_HOOK_ACCEPTED_STATE_CHANGE, hwc_window);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_accepted_state_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->accepted_state;
}

EINTERN Eina_Bool
e_hwc_window_state_set(E_Hwc_Window *hwc_window, E_Hwc_Window_State state, Eina_Bool composition_update)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->state == state) return EINA_TRUE;

   hwc_window->state = state;

   /* update the composition type */
   if (composition_update)
     {
        if (!e_hwc_window_composition_update(hwc_window))
          EHWERR("Cannot update window composition", hwc_window->ec, hwc_window->hwc, hwc_window);
     }

   /* zpos is -999 at state none */
   if (state == E_HWC_WINDOW_STATE_NONE)
     e_hwc_window_zpos_set(hwc_window, -999);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_State
e_hwc_window_state_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->state;
}

EINTERN Eina_Bool
e_hwc_window_device_state_available_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   return hwc_window->device_state_available;
}

// if ec has invalid buffer or scaled( transformed ) or forced composite(never_hwc)
EINTERN Eina_Bool
e_hwc_window_device_state_available_update(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Output *eout = NULL;
   int minw = 0, minh = 0;
   int transform;
   Eina_Bool available = EINA_TRUE;
   E_Hwc_Window_Restriction restriction = E_HWC_WINDOW_RESTRICTION_NONE;
   int count;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_deleted)
     {
        restriction = E_HWC_WINDOW_RESTRICTION_DELETED;
        available = EINA_FALSE;
        goto finish;
     }

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (ec->comp_override > 0)
     {
        restriction = E_HWC_WINDOW_RESTRICTION_OVERRIDE;
        available = EINA_FALSE;
        goto finish;
     }

   if (e_comp_object_is_animating(ec->frame))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_ANIMATING;
        available = EINA_FALSE;
        goto finish;
     }

   cdata = (E_Comp_Wl_Client_Data*)ec->comp_data;
   if ((!cdata) || (!cdata->buffer_ref.buffer))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_BUFFER;
        available = EINA_FALSE;
        goto finish;
     }

   if ((cdata->width_from_buffer != cdata->width_from_viewport) ||
       (cdata->height_from_buffer != cdata->height_from_viewport))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_VIEWPORT;
        available = EINA_FALSE;
        goto finish;
     }

   if (cdata->never_hwc)
     {
        restriction = E_HWC_WINDOW_RESTRICTION_NEVER_HWC;
        available = EINA_FALSE;
        goto finish;
     }

   if (e_client_transform_core_enable_get(ec))
     {
        /* allow device if ec has only transform of base_output_resolution */
        count = e_client_transform_core_transform_count_get(ec);
        if ((!ec->base_output_resolution.transform) || (count > 1))
          {
             restriction = E_HWC_WINDOW_RESTRICTION_TRANSFORM;
             available = EINA_FALSE;
             goto finish;
          }
     }

   switch (cdata->buffer_ref.buffer->type)
     {
      case E_COMP_WL_BUFFER_TYPE_NATIVE:
      case E_COMP_WL_BUFFER_TYPE_TBM:
         break;
      case E_COMP_WL_BUFFER_TYPE_SHM:
         if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
           break;
      default:
        restriction = E_HWC_WINDOW_RESTRICTION_BUFFER_TYPE;
        available = EINA_FALSE;
        goto finish;
     }

   eout = e_output_find(ec->zone->output_id);
   if (!eout)
     {
        restriction = E_HWC_WINDOW_RESTRICTION_OUTPUT;
        available = EINA_FALSE;
        goto finish;
     }

   tdm_output_get_available_size(eout->toutput, &minw, &minh, NULL, NULL, NULL);

   if ((minw > 0) && (minw > cdata->buffer_ref.buffer->w))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_MIN_WIDTH;
        available = EINA_FALSE;
        goto finish;
     }

   if ((minh > 0) && (minh > cdata->buffer_ref.buffer->h))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_MIN_HEIGHT;
        available = EINA_FALSE;
        goto finish;
     }

   /* If a client doesn't watch the ignore_output_transform events, we can't show
    * a client buffer to HW overlay directly when the buffer transform is not same
    * with output transform. If a client watch the ignore_output_transform events,
    * we can control client's buffer transform. In this case, we don't need to
    * check client's buffer transform here.
    */
   transform = e_comp_wl_output_buffer_transform_get(ec);
   if ((eout->config.rotation / 90) != transform)
     {
        if (e_comp_screen_rotation_ignore_output_transform_watch(ec))
          {
             if (e_comp_wl->touch.pressed)
               {
                  restriction = E_HWC_WINDOW_RESTRICTION_TOUCH_PRESS;
                  available = EINA_FALSE;
                  goto finish;
               }
          }
        else
          {
             restriction = E_HWC_WINDOW_RESTRICTION_OUTPUT_TRANSFORM;
             available = EINA_FALSE;
             goto finish;
          }
     }

   // if there is UI subfrace, it means need to composite
   if (e_client_normal_client_has(ec))
     {
        restriction = E_HWC_WINDOW_RESTRICTION_UI_SUBSURFACE;
        available = EINA_FALSE;
        goto finish;
     }

   // if ec->frame is not for client buffer (e.g. launchscreen)
   if (e_comp_object_content_type_get(ec->frame) != E_COMP_OBJECT_CONTENT_TYPE_INT_IMAGE)
     {
        restriction = E_HWC_WINDOW_RESTRICTION_CONTENT_IMAGE;
        available = EINA_FALSE;
        goto finish;
     }

   // if there is a ec which is lower than quickpanel and quickpanel is opened.
   if (E_POLICY_QUICKPANEL_LAYER >= evas_object_layer_get(ec->frame))
     {
        // check whether quickpanel is open than break
        if (e_config->use_desk_smart_obj && e_qps_visible_get())
          {
             restriction = E_HWC_WINDOW_RESTRICTION_QUICKPANEL_OPEN;
             available = EINA_FALSE;
             goto finish;
          }
     }

finish:
   hwc_window->restriction = restriction;

   if (hwc_window->device_state_available == available) return EINA_FALSE;

   hwc_window->device_state_available = available;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_transition_set(E_Hwc_Window *hwc_window, E_Hwc_Window_Transition transition)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->transition == transition) return EINA_TRUE;

   hwc_window->transition = transition;

   if (transition == E_HWC_WINDOW_TRANSITION_NONE_TO_NONE)
     hwc_window->transition_failures = 0;

   if ((transition == E_HWC_WINDOW_TRANSITION_DEVICE_TO_CLIENT) ||
       (transition == E_HWC_WINDOW_TRANSITION_DEVICE_TO_NONE))
     _e_hwc_window_constraints_reset(hwc_window);

   if (transition)
     EHWTRACE(" [%25s] is on TRANSITION [%s -> %s].",
              hwc_window->ec, hwc_window->hwc, hwc_window, e_hwc_window_name_get(hwc_window),
              e_hwc_window_state_string_get(hwc_window->accepted_state),
              e_hwc_window_state_string_get(hwc_window->state));

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_Transition
e_hwc_window_transition_get(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, E_HWC_WINDOW_STATE_NONE);

   return hwc_window->transition;
}

EINTERN Eina_Bool
e_hwc_window_constraints_update(E_Hwc_Window *hwc_window)
{
   tdm_error terror;
   int constraints;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(hwc_window, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(hwc_window->thwc_window, EINA_FALSE);

   /* get the constraints from libtdm */
   terror = tdm_hwc_window_get_constraints(hwc_window->thwc_window, &constraints);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(terror != TDM_ERROR_NONE, EINA_FALSE);

   if (hwc_window->constraints == constraints) return EINA_TRUE;

   if (constraints)
     {
        if (constraints & TDM_HWC_WIN_CONSTRAINT_BUFFER_QUEUE)
          {
             if (!_e_hwc_window_buffer_queue_set(hwc_window))
               {
                  EHWERR("fail to _e_hwc_window_buffer_queue_set", hwc_window->ec, hwc_window->hwc, hwc_window);
                  return EINA_FALSE;
               }
          }
        else
          _e_hwc_window_buffer_queue_unset(hwc_window);

        EHWTRACE("Set constranints:%x -- {%s}",
                  hwc_window->ec, hwc_window->hwc, hwc_window, constraints, e_client_util_name_get(hwc_window->ec));

        hwc_window->constraints = constraints;
     }
   else
     _e_hwc_window_constraints_reset(hwc_window);

   return EINA_TRUE;
}

static void
_e_hwc_window_client_recover(E_Hwc_Window *hwc_window)
{
   E_Comp_Wl_Buffer *recover_buffer = NULL;;
   tbm_surface_h tsurface =NULL;
   E_Client *ec = NULL;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN(ec);

   recover_buffer = _e_hwc_window_comp_wl_buffer_get(hwc_window);
   if (!recover_buffer)
     {
        tsurface = e_hwc_window_displaying_surface_get(hwc_window);
        if (!tsurface) return;

        recover_buffer = e_comp_wl_tbm_buffer_get(tsurface);
        EINA_SAFETY_ON_NULL_RETURN(recover_buffer);
     }

   EHWTRACE("Recover ts:%p -- {%s}",
            hwc_window->ec, hwc_window->hwc, hwc_window, recover_buffer->tbm_surface,
            e_hwc_window_name_get(hwc_window));

   /* force update */
   e_comp_wl_surface_attach(ec, recover_buffer);

   e_hwc_window_buffer_fetch(hwc_window, EINA_TRUE);
}

static Eina_Bool
_e_hwc_window_rendered_window_set(E_Hwc_Window *hwc_window, Eina_Bool set)
{
   E_Client *ec = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (hwc_window->render_target == set) return EINA_TRUE;

   if (set)
     {
         _e_hwc_window_client_recover(hwc_window);

        if (hwc_window->need_redirect)
          {
             e_pixmap_image_refresh(ec->pixmap);
             e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
             e_comp_object_dirty(ec->frame);
             e_comp_object_render(ec->frame);

             e_comp_object_redirected_set(ec->frame, EINA_TRUE);
             hwc_window->need_redirect = EINA_FALSE;

             EHWTRACE("Redirect -- {%s}",
                      hwc_window->ec, hwc_window->hwc, hwc_window, e_hwc_window_name_get(hwc_window));
          }
     }
   else
     {
        if (hwc_window->ec->redirected)
          {
             e_comp_object_redirected_set(ec->frame, EINA_FALSE);
             hwc_window->need_redirect = EINA_TRUE;

             EHWTRACE("Unredirect -- {%s}",
                      hwc_window->ec, hwc_window->hwc, hwc_window, e_hwc_window_name_get(hwc_window));
          }
     }

   hwc_window->render_target = set;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_rendered_window_update(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;
   E_Pointer *pointer = NULL;
   E_Hwc_Window_State state;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_deleted) return EINA_TRUE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (hwc_window->is_cursor)
     pointer = e_pointer_get(hwc_window->ec);

   state = e_hwc_window_state_get(hwc_window);

   switch(state)
     {
       case E_HWC_WINDOW_STATE_DEVICE:
       case E_HWC_WINDOW_STATE_CURSOR:
         _e_hwc_window_rendered_window_set(hwc_window, EINA_FALSE);
         if (pointer)
           e_pointer_hwc_set(pointer, EINA_TRUE);
         break;
       case E_HWC_WINDOW_STATE_CLIENT:
       case E_HWC_WINDOW_STATE_NONE:
         _e_hwc_window_rendered_window_set(hwc_window, EINA_TRUE);
         if (pointer)
           e_pointer_hwc_set(pointer, EINA_FALSE);
         break;
       case E_HWC_WINDOW_STATE_VIDEO:
       default:
         break;
     }

   return EINA_TRUE;
}

EINTERN void
e_hwc_window_buffer_set(E_Hwc_Window *hwc_window, tbm_surface_h tsurface, E_Hwc_Window_Queue *queue)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   _e_hwc_window_buffer_set(&hwc_window->buffer, tsurface,queue);
}

EINTERN void
e_hwc_window_client_type_override(E_Hwc_Window *hwc_window)
{
   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if (hwc_window->state == E_HWC_WINDOW_STATE_CLIENT) return;

   e_hwc_window_device_state_available_update(hwc_window);
   e_hwc_window_state_set(hwc_window, E_HWC_WINDOW_STATE_CLIENT, EINA_TRUE);
   _e_hwc_window_constraints_reset(hwc_window);
   e_hwc_window_rendered_window_update(hwc_window);

   EHWTRACE("set client override", hwc_window->ec, hwc_window->hwc, hwc_window);
}

EINTERN const char*
e_hwc_window_state_string_get(E_Hwc_Window_State hwc_window_state)
{
    switch (hwc_window_state)
    {
     case E_HWC_WINDOW_STATE_NONE:
       return "NO"; // None
     case E_HWC_WINDOW_STATE_CLIENT:
       return "CL"; // Client
     case E_HWC_WINDOW_STATE_DEVICE:
       return "DV"; // Device
     case E_HWC_WINDOW_STATE_VIDEO:
       return "VD"; // Video
     case E_HWC_WINDOW_STATE_CURSOR:
       return "CS"; // Cursor
     default:
       return "UNKNOWN";
    }
}

EINTERN const char*
e_hwc_window_transition_string_get(E_Hwc_Window_Transition transition)
{
   switch (transition)
    {
     case E_HWC_WINDOW_TRANSITION_NONE_TO_NONE:
       return "NOtoNO";
     case E_HWC_WINDOW_TRANSITION_NONE_TO_CLIENT:
       return "NOtoCL";
     case E_HWC_WINDOW_TRANSITION_NONE_TO_DEVICE:
       return "NOtoDV";
     case E_HWC_WINDOW_TRANSITION_NONE_TO_CURSOR:
       return "NOtoCS";
     case E_HWC_WINDOW_TRANSITION_CLIENT_TO_NONE:
       return "CLtoNO";
     case E_HWC_WINDOW_TRANSITION_CLIENT_TO_CLIENT:
       return "CLtoCL";
     case E_HWC_WINDOW_TRANSITION_CLIENT_TO_DEVICE:
       return "CLtoDV";
     case E_HWC_WINDOW_TRANSITION_CLIENT_TO_CURSOR:
       return "CLtoCS";
     case E_HWC_WINDOW_TRANSITION_DEVICE_TO_NONE:
       return "DVtoNO";
     case E_HWC_WINDOW_TRANSITION_DEVICE_TO_CLIENT:
       return "DVtoCL";
     case E_HWC_WINDOW_TRANSITION_DEVICE_TO_DEVICE:
       return "DVtoDV";
     case E_HWC_WINDOW_TRANSITION_CURSOR_TO_NONE:
       return "CStoNO";
     case E_HWC_WINDOW_TRANSITION_CURSOR_TO_CLIENT:
       return "CStoCL";
     case E_HWC_WINDOW_TRANSITION_CURSOR_TO_CURSOR:
       return "CStoCS";
     default:
       return "UNKNOWN";
    }
}

EINTERN const char*
e_hwc_window_restriction_string_get(E_Hwc_Window *hwc_window)
{
   if (!hwc_window) return "UNKNOWN";

   switch (hwc_window->restriction)
    {
     case E_HWC_WINDOW_RESTRICTION_NONE:
       return "none";
     case E_HWC_WINDOW_RESTRICTION_DELETED:
       return "deleted";
     case E_HWC_WINDOW_RESTRICTION_OVERRIDE:
       return "override";
     case E_HWC_WINDOW_RESTRICTION_ANIMATING:
       return "animating";
     case E_HWC_WINDOW_RESTRICTION_BUFFER:
       return "buffer";
     case E_HWC_WINDOW_RESTRICTION_VIEWPORT:
       return "viewport";
     case E_HWC_WINDOW_RESTRICTION_NEVER_HWC:
       return "never hwc";
     case E_HWC_WINDOW_RESTRICTION_TRANSFORM:
       return "transform";
     case E_HWC_WINDOW_RESTRICTION_BUFFER_TYPE:
       return "buffer type";
     case E_HWC_WINDOW_RESTRICTION_OUTPUT:
       return "output";
     case E_HWC_WINDOW_RESTRICTION_MIN_WIDTH:
       return "min width";
     case E_HWC_WINDOW_RESTRICTION_MIN_HEIGHT:
       return "min height";
     case E_HWC_WINDOW_RESTRICTION_TOUCH_PRESS:
       return "touch press";
     case E_HWC_WINDOW_RESTRICTION_OUTPUT_TRANSFORM:
       return "transform";
     case E_HWC_WINDOW_RESTRICTION_UI_SUBSURFACE:
       return "ui subsurface";
     case E_HWC_WINDOW_RESTRICTION_CONTENT_IMAGE:
       return "content image";
     case E_HWC_WINDOW_RESTRICTION_QUICKPANEL_OPEN:
       return "quickpanel open";
     default:
       return "UNKNOWN";
    }
}

EINTERN const char*
e_hwc_window_name_get(E_Hwc_Window *hwc_window)
{
   const char *name;

   if (!hwc_window)
     return "UNKNOWN";

   if (hwc_window->is_target)
     return "@TARGET WINDOW@";

   if (!hwc_window->ec)
     return "UNKNOWN";

   name = e_client_util_name_get(hwc_window->ec);
   if (!name)
     return "UNKNOWN";

   return name;
}

EINTERN void
e_hwc_window_name_set(E_Hwc_Window *hwc_window)
{
   const char *name = NULL;
   tdm_error ret;
   Eina_Bool no_name = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN(hwc_window);

   if (hwc_window->set_name) return;

   name = e_client_util_name_get(hwc_window->ec);
   if (!name)
     {
        name = "UNKNOWN";
        no_name = EINA_TRUE;
     }

   ret = tdm_hwc_window_set_name(hwc_window->thwc_window, name);
   EINA_SAFETY_ON_TRUE_RETURN(ret != TDM_ERROR_NONE);

   /* the name may be set later */
   if (no_name) return;

   hwc_window->set_name = EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_set_property(E_Hwc_Window *hwc_window, unsigned int id, const char *name, tdm_value value, Eina_Bool force)
{
   E_Client *ec = NULL;
   const Eina_List *l = NULL;
   Hwc_Window_Prop *prop = NULL;
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (force)
     {
        /* set the property on the fly */
        ret = tdm_hwc_window_set_property(hwc_window->thwc_window, id, value);
        EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

        EHWTRACE("Set Property: property(%s) value(%d)) -- {%s}",
                  hwc_window->ec, hwc_window->hwc, hwc_window,
                  name, (unsigned int)value.u32,
                  e_hwc_window_name_get(hwc_window));
     }
   else
     {
        /* change the vaule of the property if prop_list already has the property */
        EINA_LIST_FOREACH(hwc_window->prop_list, l, prop)
          {
             if (!strncmp(name, prop->name, TDM_NAME_LEN))
               {
                 EHWTRACE("Change Property: property(%s) update value(%d -> %d) -- {%s}",
                           hwc_window->ec, hwc_window->hwc, hwc_window,
                           prop->name, (unsigned int)prop->value.u32, (unsigned int)value.u32,
                           e_hwc_window_name_get(hwc_window));
                  prop->value.u32 = value.u32;
                  return EINA_TRUE;
               }
          }

        /* store the properties and commit at the hwc_commit time */
        prop = calloc(1, sizeof(Hwc_Window_Prop));
        EINA_SAFETY_ON_NULL_RETURN_VAL(prop, EINA_FALSE);
        prop->value.u32 = value.u32;
        prop->id = id;
        memcpy(prop->name, name, sizeof(TDM_NAME_LEN));
        hwc_window->prop_list = eina_list_append(hwc_window->prop_list, prop);

        EHWTRACE("Set Property: property(%s) value(%d)) -- {%s}",
                  hwc_window->ec, hwc_window->hwc, hwc_window,
                  prop->name, (unsigned int)value.u32,
                  e_hwc_window_name_get(hwc_window));
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_get_property(E_Hwc_Window *hwc_window, unsigned int id, tdm_value *value)
{
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(value, EINA_FALSE);

   ret = tdm_hwc_window_get_property(hwc_window->thwc_window, id, value);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

EINTERN E_Hwc_Window_Hook *
e_hwc_window_hook_add(E_Hwc_Window_Hook_Point hookpoint, E_Hwc_Window_Hook_Cb func, const void *data)
{
   E_Hwc_Window_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_HWC_WINDOW_HOOK_LAST, NULL);
   ch = E_NEW(E_Hwc_Window_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_hwc_window_hooks[hookpoint] = eina_inlist_append(_e_hwc_window_hooks[hookpoint],
                                                       EINA_INLIST_GET(ch));
   return ch;
}

EINTERN void
e_hwc_window_hook_del(E_Hwc_Window_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_hwc_window_hooks_walking == 0)
     {
        _e_hwc_window_hooks[ch->hookpoint] = eina_inlist_remove(_e_hwc_window_hooks[ch->hookpoint],
                                                                EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_hwc_window_hooks_delete++;
}

EINTERN void
e_hwc_window_trace_debug(Eina_Bool onoff)
{
   if (onoff == ehw_trace) return;
   ehw_trace = onoff;
   INF("EHW: hwc trace_debug is %s", onoff?"ON":"OFF");
}

EINTERN void
e_hwc_window_commit_data_buffer_dump(E_Hwc_Window *hwc_window)
{
   char fname[64];

   EINA_SAFETY_ON_FALSE_RETURN(hwc_window);

   if (!hwc_window->commit_data) return;
   if (!hwc_window->commit_data->buffer.tsurface) return;

   if (hwc_window->is_target)
     snprintf(fname, sizeof(fname), "hwc_commit_composite_%p",
              hwc_window);
   else
     snprintf(fname, sizeof(fname), "hwc_commit_0x%08zx_%p",
              e_client_util_win_get(hwc_window->ec), hwc_window);

   tbm_surface_internal_dump_buffer(hwc_window->commit_data->buffer.tsurface,
                                    fname);
}

EINTERN Eina_Bool
e_hwc_window_fps_get(E_Hwc_Window *hwc_window, double *fps)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->fps.old_fps == hwc_window->fps.fps)
     return EINA_FALSE;

   if (hwc_window->fps.fps > 0.0)
     {
        *fps = hwc_window->fps.fps;
        hwc_window->fps.old_fps = hwc_window->fps.fps;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_hwc_window_pp_rendered_window_update(E_Hwc_Window *hwc_window)
{
   E_Client *ec = NULL;
   E_Pointer *pointer = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc_window, EINA_FALSE);

   if (hwc_window->is_deleted) return EINA_TRUE;

   ec = hwc_window->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (hwc_window->is_cursor)
     pointer = e_pointer_get(hwc_window->ec);

   if (e_hwc_window_is_video(hwc_window))
     return EINA_TRUE;

   _e_hwc_window_rendered_window_set(hwc_window, EINA_FALSE);
   if (pointer)
     e_pointer_hwc_set(pointer, EINA_TRUE);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_hwc_window_pp_commit_data_acquire(E_Hwc_Window *hwc_window, Eina_Bool pp_hwc_mode)
{
   E_Hwc_Window_Commit_Data *commit_data = NULL;

   /* if pp_hwc_mode, there is only 1 client state window */
   if (pp_hwc_mode)
     {
        if (!hwc_window->buffer.tsurface) return EINA_FALSE;
        if ((hwc_window->buffer.tsurface == hwc_window->display.buffer.tsurface) &&
            (!memcmp(&hwc_window->info, &hwc_window->display.info, sizeof(tdm_hwc_window_info))))
          return EINA_FALSE;
        if (hwc_window->state != E_HWC_WINDOW_STATE_CLIENT)
          {
             if (hwc_window->state != E_HWC_WINDOW_STATE_VIDEO)
               return EINA_FALSE;
          }

        commit_data = _e_hwc_window_commit_data_acquire_device(hwc_window);
        EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);
     }
   else
     {
        if (hwc_window->accepted_state == E_HWC_WINDOW_STATE_DEVICE ||/* composited buffer */
             hwc_window->accepted_state == E_HWC_WINDOW_STATE_VIDEO)
          {
             if (!hwc_window->buffer.tsurface) return EINA_FALSE;
             if ((hwc_window->buffer.tsurface == hwc_window->display.buffer.tsurface) &&
                 (!memcmp(&hwc_window->info, &hwc_window->display.info, sizeof(tdm_hwc_window_info))))
               return EINA_FALSE;

             commit_data = _e_hwc_window_commit_data_acquire_device(hwc_window);
             EINA_SAFETY_ON_NULL_RETURN_VAL(commit_data, EINA_FALSE);
          }
        else
          return EINA_FALSE;
     }

   EHWTRACE("COM ts:%10p ------- {%25s}, state:%s, zpos:%d, deleted:%s",
            hwc_window->ec, hwc_window->hwc, hwc_window,
            commit_data->buffer.tsurface,
            e_hwc_window_name_get(hwc_window),
            e_hwc_window_state_string_get(hwc_window->state),
            hwc_window->zpos, (hwc_window->is_deleted ? "yes" : "no"));

   e_object_ref(E_OBJECT(hwc_window));

   hwc_window->commit_data = commit_data;

   return EINA_TRUE;
}
