#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <wayland-server.h>
#include <screenshooter-server-protocol.h>
#include <tizen-extension-server-protocol.h>
#include <wayland-tbm-server.h>

#define DUMP_FPS     30

typedef struct _E_Mirror
{
   struct wl_resource *resource;
   struct wl_resource *shooter;
   struct wl_resource *output;

   Eina_Bool started;
   enum tizen_screenmirror_stretch stretch;

   Eina_List *buffer_queue;
   E_Comp_Wl_Output *wl_output;
   E_Output *e_output;

   /* converter info */
   Eina_List *buffer_clear_check;

   struct wl_listener client_destroy_listener;

   Eina_Bool oneshot_client_destroy;
} E_Mirror;

typedef struct _E_Mirror_Buffer
{
   E_Comp_Wl_Video_Buf *vbuf;
   E_Comp_Wl_Video_Buf *tmp;

   E_Mirror *mirror;

   Eina_Bool in_use;
   Eina_Bool dirty;

   /* in case of shm buffer */
   struct wl_listener destroy_listener;
} E_Mirror_Buffer;

static uint mirror_format_table[] =
{
   TBM_FORMAT_ARGB8888,
   TBM_FORMAT_XRGB8888,
   TBM_FORMAT_NV12,
   TBM_FORMAT_NV21,
};

#define NUM_MIRROR_FORMAT   (sizeof(mirror_format_table) / sizeof(mirror_format_table[0]))

static E_Mirror *keep_stream_mirror;
static Eina_Bool screenshot_auto_rotation;
static Eina_List *mirror_list;

static void _e_tz_screenmirror_destroy(E_Mirror *mirror);
static void _e_tz_screenmirror_buffer_dequeue(E_Mirror_Buffer *buffer);
static void _e_tz_screenmirror_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data);

static Eina_Bool
_e_screenmirror_privilege_check(struct wl_client *client)
{
   Eina_Bool res = EINA_FALSE;
   pid_t pid = 0;
   uid_t uid = 0;

   wl_client_get_credentials(client, &pid, &uid, NULL);

   /* pass privilege check if super user */
   if (uid == 0)
     return EINA_TRUE;

   res = e_security_privilege_check(pid, uid, E_PRIVILEGE_SCREENSHOT);
   if (!res)
     {
        ELOGF("EOM",
              "Privilege Check Failed! DENY screenshot pid:%d",
              NULL, pid);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_tz_screenmirror_buffer_check(struct wl_resource *resource)
{
   if (wl_shm_buffer_get(resource) ||
       wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, resource))
     return EINA_TRUE;

   ERR("unrecognized buffer");

   return EINA_FALSE;
}

static void
_e_tz_screenmirror_drm_buffer_clear_check(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;
   E_Comp_Wl_Video_Buf *vbuf, *dst;
   Eina_List *l;
   uint32_t buf_id;

   dst = buffer->vbuf;
   buf_id = wl_resource_get_id(dst->resource);

   EINA_LIST_FOREACH(mirror->buffer_clear_check, l, vbuf)
     {
        uint32_t id;

        id = wl_resource_get_id(vbuf->resource);
        if (id == buf_id)
          return;
     }

   e_comp_wl_video_buffer_clear(dst);
   mirror->buffer_clear_check = eina_list_append(mirror->buffer_clear_check, dst);
}

static Eina_Bool
_e_tz_screenmirror_tmp_buffer_create(E_Mirror_Buffer *buffer)
{
   tbm_surface_h tbm_surface = NULL;
   E_Comp_Wl_Video_Buf *vbuf = NULL;

   tbm_surface = tbm_surface_create(buffer->vbuf->width, buffer->vbuf->height, buffer->vbuf->tbmfmt);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, EINA_FALSE);

   vbuf = e_comp_wl_video_buffer_create_tbm(tbm_surface);
   if (vbuf == NULL)
     {
        tbm_surface_destroy(tbm_surface);
        return EINA_FALSE;
     }

   e_comp_wl_video_buffer_clear(vbuf);
   buffer->tmp = vbuf;

   return EINA_TRUE;
}

static void
_e_tz_screenmirror_copy_tmp_buffer(E_Mirror_Buffer *buffer)
{
   tbm_surface_h tbm_surface = NULL;

   EINA_SAFETY_ON_NULL_RETURN(buffer->tmp);
   EINA_SAFETY_ON_NULL_RETURN(buffer->vbuf);

   e_comp_wl_video_buffer_copy(buffer->tmp, buffer->vbuf);

   tbm_surface = buffer->tmp->tbm_surface;

   e_comp_wl_video_buffer_unref(buffer->tmp);
   buffer->tmp = NULL;

   tbm_surface_destroy(tbm_surface);
}

static void
_e_tz_screenmirror_buffer_free(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;

   if (buffer->tmp)
     _e_tz_screenmirror_copy_tmp_buffer(buffer);

   /* then, dequeue and send dequeue event */
   _e_tz_screenmirror_buffer_dequeue(buffer);

   if (buffer->destroy_listener.notify)
     {
        wl_list_remove(&buffer->destroy_listener.link);
        buffer->destroy_listener.notify = NULL;
     }

   if (buffer->vbuf)
     {
        e_comp_wl_video_buffer_free_func_del(buffer->vbuf, _e_tz_screenmirror_buffer_cb_free, buffer);
        e_comp_wl_video_buffer_unref(buffer->vbuf);

        mirror->buffer_clear_check = eina_list_remove(mirror->buffer_clear_check, buffer->vbuf);
     }

   E_FREE(buffer);
}

static void
_e_tz_screenmirror_capture_stream_done(void *data)
{
   E_Mirror *mirror = data;

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (mirror != keep_stream_mirror)
     return;

   e_output_stream_capture_stop(mirror->e_output);
}

static E_Mirror_Buffer *
_e_tz_screenmirror_mirrorbuf_find(E_Mirror *mirror, tbm_surface_h surf)
{
   Eina_List *l;
   E_Mirror_Buffer *buffer = NULL;

   if (!mirror->buffer_queue)
     return NULL;

   EINA_LIST_FOREACH(mirror->buffer_queue, l, buffer)
     {
        if (!buffer || !buffer->vbuf)
          continue;

        if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
          {
             if (!buffer->tmp || !buffer->tmp->tbm_surface)
               continue;

             if (buffer->tmp->tbm_surface == surf)
               {
                  _e_tz_screenmirror_copy_tmp_buffer(buffer);
                  return buffer;
               }
          }
        else
          {
             if (!buffer->vbuf->tbm_surface)
               continue;

             if (buffer->vbuf->tbm_surface == surf)
               return buffer;
          }
     }

   return NULL;
}

static void
_e_output_stream_capture_cb(E_Output *eout, tbm_surface_h tsurface, void *user_data)
{
   E_Mirror *mirror = user_data;
   E_Mirror_Buffer *buffer = NULL;

   if (mirror != keep_stream_mirror)
     {
        ERR("_e_output_stream_capture_cb, mirror different fail");
        return;
     }
//   DBG("_e_output_stream_capture_cb, tsurface(%p)", tsurface);

   buffer = _e_tz_screenmirror_mirrorbuf_find(mirror, tsurface);
   if (buffer == NULL)
     {
        ERR("_e_output_stream_capture_cb: find mirror buffer failed");
        return;
     }

   _e_tz_screenmirror_buffer_dequeue(buffer);
}

static void
_e_tz_screenmirror_buffer_queue(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;
   Eina_Bool ret;
   E_Comp_Wl_Video_Buf *vbuf;

   mirror->buffer_queue = eina_list_append(mirror->buffer_queue, buffer);

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
          {
             ERR("tmp buffer create fail");
             goto fail;
          }

        vbuf = buffer->tmp;
     }
   else
     vbuf = buffer->vbuf;
   EINA_SAFETY_ON_NULL_GOTO(vbuf, fail);

   _e_tz_screenmirror_drm_buffer_clear_check(buffer);

   ret = e_output_stream_capture_queue(mirror->e_output, vbuf->tbm_surface, _e_output_stream_capture_cb, mirror);
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

   return;

fail:
   mirror->buffer_queue = eina_list_remove(mirror->buffer_queue, buffer);
   _e_tz_screenmirror_buffer_free(buffer);
}

static void
_e_tz_screenmirror_buffer_dequeue(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;
   E_Comp_Wl_Video_Buf *vbuf = NULL;

   EINA_SAFETY_ON_NULL_RETURN(mirror);
   if (!mirror->buffer_queue || !eina_list_data_find_list(mirror->buffer_queue, buffer))
     return;

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     vbuf = buffer->tmp;
   else
     vbuf = buffer->vbuf;

   if (vbuf)
     e_output_stream_capture_dequeue(mirror->e_output, vbuf->tbm_surface);

   buffer->in_use = EINA_FALSE;
   mirror->buffer_queue = eina_list_remove(mirror->buffer_queue, buffer);

   /* resource == shooter means that we're using weston screenshooter
    * In case of wetson screenshooter, send a done event. Otherwise, send
    * a dequeued event for tizen_screenmirror.
    */
   if (mirror->resource == mirror->shooter)
     {
        if (!mirror->oneshot_client_destroy)
          screenshooter_send_done(mirror->resource);
     }
   else
     tizen_screenmirror_send_dequeued(mirror->resource, buffer->vbuf->resource);
}

static void
_e_tz_screenmirror_buffer_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Mirror_Buffer *buffer = container_of(listener, E_Mirror_Buffer, destroy_listener);

   if (buffer->destroy_listener.notify)
     {
        wl_list_remove(&buffer->destroy_listener.link);
        buffer->destroy_listener.notify = NULL;
     }
}

static void
_e_tz_screenmirror_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Mirror_Buffer *buffer = data;
   E_Mirror *mirror = buffer->mirror;

   if (mirror->resource == mirror->shooter)
     mirror->oneshot_client_destroy = EINA_TRUE;

   _e_tz_screenmirror_buffer_free(buffer);
}

static E_Mirror_Buffer*
_e_tz_screenmirror_buffer_get(E_Mirror *mirror, struct wl_resource *resource)
{
   E_Mirror_Buffer *buffer = NULL;
   struct wl_listener *listener;

   listener = wl_resource_get_destroy_listener(resource, _e_tz_screenmirror_buffer_cb_destroy);
   if (listener)
     return container_of(listener, E_Mirror_Buffer, destroy_listener);

   if (!(buffer = E_NEW(E_Mirror_Buffer, 1)))
     return NULL;

   /* FIXME: this is very tricky. DON'T add listner after e_comp_wl_video_buffer_create. */
   buffer->destroy_listener.notify = _e_tz_screenmirror_buffer_cb_destroy;
   wl_resource_add_destroy_listener(resource, &buffer->destroy_listener);

   buffer->vbuf = e_comp_wl_video_buffer_create(resource);
   EINA_SAFETY_ON_NULL_GOTO(buffer->vbuf, fail_get);

   buffer->mirror = mirror;

   DBG("capture buffer: %c%c%c%c %dx%d (%d,%d,%d) (%d,%d,%d)",
       FOURCC_STR(buffer->vbuf->tbmfmt),
       buffer->vbuf->width, buffer->vbuf->height,
       buffer->vbuf->pitches[0], buffer->vbuf->pitches[1], buffer->vbuf->pitches[2],
       buffer->vbuf->offsets[0], buffer->vbuf->offsets[1], buffer->vbuf->offsets[2]);

   e_comp_wl_video_buffer_free_func_add(buffer->vbuf, _e_tz_screenmirror_buffer_cb_free, buffer);

   return buffer;
fail_get:
   E_FREE(buffer);
   return NULL;
}

static void
_e_tz_screenmirror_cb_client_destroy(struct wl_listener *listener, void *data)
{
   E_Mirror *mirror = container_of(listener, E_Mirror, client_destroy_listener);

   if (mirror->resource == mirror->shooter)
     {
        mirror->oneshot_client_destroy = EINA_TRUE;
        return;
     }
   _e_tz_screenmirror_destroy(mirror);
}

static E_Mirror*
_e_tz_screenmirror_create(struct wl_client *client, struct wl_resource *shooter_resource, struct wl_resource *output_resource)
{
   E_Mirror *mirror = NULL;

   mirror = E_NEW(E_Mirror, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mirror, NULL);

   mirror->stretch = TIZEN_SCREENMIRROR_STRETCH_KEEP_RATIO;
   mirror->shooter = shooter_resource;
   mirror->output = output_resource;
   mirror->wl_output = wl_resource_get_user_data(mirror->output);
   EINA_SAFETY_ON_NULL_GOTO(mirror->wl_output, fail_create);

   mirror->e_output = e_output_find(mirror->wl_output->id);
   EINA_SAFETY_ON_NULL_GOTO(mirror->e_output, fail_create);

   mirror_list = eina_list_append(mirror_list, mirror);

   mirror->client_destroy_listener.notify = _e_tz_screenmirror_cb_client_destroy;
   wl_client_add_destroy_listener(client, &mirror->client_destroy_listener);

   mirror->oneshot_client_destroy = EINA_FALSE;

   return mirror;

fail_create:
   E_FREE(mirror);

   return NULL;
}

static Eina_Bool
_e_tz_screenmirror_find_mirror(E_Mirror *mirror)
{
   if (!eina_list_data_find(mirror_list, mirror))
     return EINA_FALSE;
   else
     return EINA_TRUE;
}

static void
_e_tz_screenmirror_destroy(E_Mirror *mirror)
{
   E_Mirror_Buffer *buffer;
   Eina_List *l, *ll;

   if (!mirror)
     return;

   if (!_e_tz_screenmirror_find_mirror(mirror))
     return;
   mirror_list = eina_list_remove(mirror_list, mirror);

   if (mirror->client_destroy_listener.notify)
     wl_list_remove(&mirror->client_destroy_listener.link);
   mirror->client_destroy_listener.notify = NULL;

   wl_resource_set_destructor(mirror->resource, NULL);

   eina_list_free(mirror->buffer_clear_check);
   mirror->buffer_clear_check = NULL;

   EINA_LIST_FOREACH_SAFE(mirror->buffer_queue, l, ll, buffer)
     {
        if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
          e_output_stream_capture_dequeue(mirror->e_output, buffer->tmp->tbm_surface);
        else
          e_output_stream_capture_dequeue(mirror->e_output, buffer->vbuf->tbm_surface);

        _e_tz_screenmirror_buffer_free(buffer);
     }
   mirror->buffer_queue = NULL;

   _e_tz_screenmirror_capture_stream_done(mirror);

   if (keep_stream_mirror == mirror)
     keep_stream_mirror = NULL;

   free(mirror);
#if 0
   if (e_comp_wl_video_buffer_list_length() > 0)
     e_comp_wl_video_buffer_list_print(NULL);
#endif
}

static void
destroy_tz_screenmirror(struct wl_resource *resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);

   _e_tz_screenmirror_destroy(mirror);
}

static void
_e_tz_screenmirror_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_tz_screenmirror_cb_set_stretch(struct wl_client *client, struct wl_resource *resource, uint32_t stretch)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("_e_tz_screenmirror_cb_set_stretch: priv check failed");
        return;
     }

   if (mirror->stretch == stretch)
     return;

   mirror->stretch = stretch;
}

static void
_e_tz_screenmirror_cb_queue(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer_resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);
   E_Mirror_Buffer *buffer;

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("_e_tz_screenmirror_cb_queue: priv check failed");
        return;
     }

   if (!_e_tz_screenmirror_buffer_check(buffer_resource))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "tizen_screenshooter failed: wrong buffer resource");
        return;
     }

   buffer = _e_tz_screenmirror_buffer_get(mirror, buffer_resource);
   if (!buffer)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   _e_tz_screenmirror_buffer_queue(buffer);
}

static void
_e_tz_screenmirror_cb_dequeue(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer_resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);
   E_Mirror_Buffer *buffer;
   E_Comp_Wl_Video_Buf *vbuf = NULL;

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("_e_tz_screenmirror_cb_dequeue: priv check failed");
        return;
     }

   if (!_e_tz_screenmirror_buffer_check(buffer_resource))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "tizen_screenshooter failed: wrong buffer resource");
        return;
     }

   buffer = _e_tz_screenmirror_buffer_get(mirror, buffer_resource);
   if (!buffer || !eina_list_data_find_list(mirror->buffer_queue, buffer))
     return;

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     vbuf = buffer->tmp;
   else
     vbuf = buffer->vbuf;

   e_output_stream_capture_dequeue(mirror->e_output, vbuf->tbm_surface);

   _e_tz_screenmirror_buffer_dequeue(buffer);
}

static void
_e_tz_screenmirror_cb_start(struct wl_client *client, struct wl_resource *resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("_e_tz_screenmirror_cb_start: priv check failed");
        return;
     }

   if (mirror->started) return;

   mirror->started = EINA_TRUE;

   if (!mirror->buffer_queue)
     return;

   e_output_stream_capture_start(mirror->e_output);
}

static void
_e_tz_screenmirror_cb_stop(struct wl_client *client, struct wl_resource *resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);

   EINA_SAFETY_ON_NULL_RETURN(mirror);

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("_e_tz_screenmirror_cb_stop: priv check failed");
        return;
     }

   if (!mirror->started) return;

   mirror->started = EINA_FALSE;

   e_output_stream_capture_stop(mirror->e_output);

   tizen_screenmirror_send_stop(resource);
}

static const struct tizen_screenmirror_interface _e_tz_screenmirror_interface = {
     _e_tz_screenmirror_cb_destroy,
     _e_tz_screenmirror_cb_set_stretch,
     _e_tz_screenmirror_cb_queue,
     _e_tz_screenmirror_cb_dequeue,
     _e_tz_screenmirror_cb_start,
     _e_tz_screenmirror_cb_stop
};

static void
_e_tz_screenshooter_get_screenmirror(struct wl_client *client,
                                     struct wl_resource *resource,
                                     uint32_t id,
                                     struct wl_resource *output)
{
   int version = wl_resource_get_version(resource);
   E_Mirror *mirror;

   if (keep_stream_mirror != NULL)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   mirror = _e_tz_screenmirror_create(client, resource, output);
   if (!mirror)
     {
        wl_resource_post_no_memory(resource);
        return;
     }
   keep_stream_mirror = mirror;

   mirror->resource = wl_resource_create(client, &tizen_screenmirror_interface, version, id);
   if (mirror->resource == NULL)
     {
        _e_tz_screenmirror_destroy(mirror);
        wl_client_post_no_memory(client);
        keep_stream_mirror = NULL;
        return;
     }

   wl_resource_set_implementation(mirror->resource, &_e_tz_screenmirror_interface,
                                  mirror, destroy_tz_screenmirror);

   if (_e_screenmirror_privilege_check(client) == EINA_TRUE)
     DBG("_e_tz_screenshooter_get_screenmirror: priv check success");
   else
     DBG("_e_tz_screenshooter_get_screenmirror: priv check failed");

   tizen_screenmirror_send_content(mirror->resource, TIZEN_SCREENMIRROR_CONTENT_NORMAL);
}

static void
_e_tz_screenshooter_set_oneshot_auto_rotation(struct wl_client *client,
                                              struct wl_resource *resource,
                                              uint32_t set)
{
   DBG("_e_tz_screenshooter_set_oneshot_auto_rotation: %d", set);

   if (set)
     screenshot_auto_rotation = EINA_TRUE;
   else
     screenshot_auto_rotation = EINA_FALSE;
}

static void
_e_tz_screenshooter_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_screenshooter_interface _e_tz_screenshooter_interface =
{
   _e_tz_screenshooter_get_screenmirror,
   _e_tz_screenshooter_set_oneshot_auto_rotation,
   _e_tz_screenshooter_destroy,
};

static void
_e_tz_screenshooter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   int i;

   if (!(res = wl_resource_create(client, &tizen_screenshooter_interface, version, id)))
     {
        ERR("Could not create tizen_screenshooter resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   if (_e_screenmirror_privilege_check(client) == EINA_TRUE)
     tizen_screenshooter_send_screenshooter_notify(res, EINA_TRUE);
   else
     tizen_screenshooter_send_screenshooter_notify(res, EINA_FALSE);

   wl_resource_set_implementation(res, &_e_tz_screenshooter_interface, NULL, NULL);

   for (i = 0; i < NUM_MIRROR_FORMAT; i++)
     tizen_screenshooter_send_format(res, mirror_format_table[i]);
}

static void
_e_output_capture_oneshot_cb(E_Output *eout, tbm_surface_h tsurface, void *user_data)
{
   E_Mirror *mirror;
   E_Mirror_Buffer *buffer = (E_Mirror_Buffer *)user_data;

   EINA_SAFETY_ON_NULL_RETURN(buffer);

   mirror = buffer->mirror;

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     _e_tz_screenmirror_copy_tmp_buffer(buffer);

   _e_tz_screenmirror_buffer_free(buffer);

   _e_tz_screenmirror_destroy(mirror);

//   DBG("_e_output_capture_oneshot_cb done");
}

static void
_e_screenshooter_cb_shoot(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *output_resource,
                          struct wl_resource *buffer_resource)
{
   E_Mirror *mirror;
   E_Mirror_Buffer *buffer;
   Eina_Bool ret;
   E_Comp_Wl_Video_Buf *vbuf = NULL;

   if (!_e_tz_screenmirror_buffer_check(buffer_resource))
     {
        wl_resource_post_error(resource, WL_DISPLAY_ERROR_INVALID_OBJECT,
                               "tizen_screenshooter failed: wrong buffer resource");
        return;
     }

   mirror = _e_tz_screenmirror_create(client, resource, output_resource);
   if (!mirror)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   /* resource == shooter means that we're using weston screenshooter */
   mirror->resource = mirror->shooter;

   if (_e_screenmirror_privilege_check(client) == EINA_FALSE)
     {
        ERR("priv check failed");
        screenshooter_send_done(mirror->resource);
        goto privilege_fail;
     }

   buffer = _e_tz_screenmirror_buffer_get(mirror, buffer_resource);
   if (!buffer)
     {
        wl_resource_post_no_memory(resource);
        _e_tz_screenmirror_destroy(mirror);
        return;
     }
   e_comp_wl_video_buffer_clear(buffer->vbuf);

   mirror->buffer_queue = eina_list_append(mirror->buffer_queue, buffer);

   if (e_output_dpms_get(mirror->e_output))
     {
        ERR("dpms on fail");
        goto dump_done;
     }

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
          {
             ERR("tmp buffer create fail");
             goto dump_done;
          }

        vbuf = buffer->tmp;
     }
   else
     vbuf = buffer->vbuf;
   EINA_SAFETY_ON_NULL_GOTO(vbuf, dump_done);

   e_comp_wl_video_buffer_clear(vbuf);

   ret = e_output_capture(mirror->e_output, vbuf->tbm_surface, screenshot_auto_rotation, EINA_FALSE, _e_output_capture_oneshot_cb, buffer);
   if (ret) return;
   else ERR("capture fail");

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     _e_tz_screenmirror_copy_tmp_buffer(buffer);

dump_done:
   _e_tz_screenmirror_buffer_free(buffer);

privilege_fail:
   _e_tz_screenmirror_destroy(mirror);
}

static const struct screenshooter_interface _e_screenshooter_interface =
{
   _e_screenshooter_cb_shoot
};

static void
_e_screenshooter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &screenshooter_interface, MIN(version, 1), id)))
     {
        ERR("Could not create screenshooter resource");
        wl_client_post_no_memory(client);
        return;
     }

   screenshot_auto_rotation = EINA_TRUE;

   wl_resource_set_implementation(res, &_e_screenshooter_interface, NULL, NULL);
}

EINTERN int
e_comp_wl_screenshooter_init(void)
{
   if (!e_comp_wl) return 0;
   if (!e_comp_wl->wl.disp) return 0;

   /* try to add screenshooter to wayland globals */
   if (!wl_global_create(e_comp_wl->wl.disp, &screenshooter_interface, 1,
                         NULL, _e_screenshooter_cb_bind))
     {
        ERR("Could not add screenshooter to wayland globals");
        return 0;
     }

   /* try to add tizen_screenshooter to wayland globals */
   if (!wl_global_create(e_comp_wl->wl.disp, &tizen_screenshooter_interface, 1,
                         NULL, _e_tz_screenshooter_cb_bind))
     {
        ERR("Could not add tizen_screenshooter to wayland globals");
        return 0;
     }

   return 1;
}

EINTERN void
e_comp_wl_screenshooter_shutdown(void)
{
}
