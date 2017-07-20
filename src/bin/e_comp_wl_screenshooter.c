#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <wayland-server.h>
#include <Ecore_Wayland.h>
#include <screenshooter-server-protocol.h>
#include <tizen-extension-server-protocol.h>
#include <tdm.h>

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

   tdm_output *tdm_output;
   tdm_layer *tdm_primary_layer;
   tdm_capture *capture;
   Ecore_Timer *capture_timer;

   /* vblank info */
   int per_vblank;
   Eina_Bool wait_vblank;

   /* timer info when dpms off */
   Ecore_Timer *timer;

   /* converter info */
   tdm_pp *pp;
   Eina_List *ui_buffer_list;
   Eina_List *buffer_clear_check;

   /* rotation info */
   int eout_rotate;
   int angle;
   Eina_Bool rotate_change;

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
static void _e_tz_screenmirror_vblank_handler(tdm_output *output, unsigned int sequence,
                                              unsigned int tv_sec, unsigned int tv_usec,
                                              void *data);

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
              NULL, NULL, pid);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_tz_screenmirror_center_rect (int src_w, int src_h, int dst_w, int dst_h, Eina_Rectangle *fit)
{
   float rw, rh;

   if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || !fit)
     return;

   rw = (float)src_w / dst_w;
   rh = (float)src_h / dst_h;

   if (rw > rh)
     {
        fit->w = dst_w;
        fit->h = src_h / rw;
        fit->x = 0;
        fit->y = (dst_h - fit->h) / 2;
     }
   else if (rw < rh)
     {
        fit->w = src_w / rh;
        fit->h = dst_h;
        fit->x = (dst_w - fit->w) / 2;
        fit->y = 0;
     }
   else
     {
        fit->w = dst_w;
        fit->h = dst_h;
        fit->x = 0;
        fit->y = 0;
     }

   if (fit->x % 2)
     fit->x = fit->x - 1;
}

void
_e_tz_screenmirror_rect_scale (int src_w, int src_h, int dst_w, int dst_h, Eina_Rectangle *scale)
{
   float ratio;
   Eina_Rectangle center;

   if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0 || !scale)
     return;

   _e_tz_screenmirror_center_rect(src_w, src_h, dst_w, dst_h, &center);

   ratio = (float)center.w / src_w;

   scale->x = scale->x * ratio + center.x;
   scale->y = scale->y * ratio + center.y;
   scale->w = scale->w * ratio;
   scale->h = scale->h * ratio;
}

static Eina_Bool
_e_tz_screenmirror_cb_timeout(void *data)
{
   E_Mirror *mirror = data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(mirror, ECORE_CALLBACK_RENEW);

   _e_tz_screenmirror_vblank_handler(NULL, 0, 0, 0, (void*)mirror);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_tz_screenmirror_watch_vblank(E_Mirror *mirror)
{
   tdm_error ret;

   if (mirror != keep_stream_mirror)
     return EINA_FALSE;

   /* If not DPMS_ON, we call vblank handler directly to dump screen */
   if (e_output_dpms_get(mirror->e_output))
     {
        if (!mirror->timer)
          mirror->timer = ecore_timer_add((double)1/DUMP_FPS,
                                          _e_tz_screenmirror_cb_timeout, mirror);
        EINA_SAFETY_ON_NULL_RETURN_VAL(mirror->timer, EINA_FALSE);

        return EINA_TRUE;
     }
   else if (mirror->timer)
     {
        ecore_timer_del(mirror->timer);
        mirror->timer = NULL;
     }

   if (mirror->wait_vblank)
     return EINA_TRUE;

   ret = tdm_output_wait_vblank(mirror->tdm_output, mirror->per_vblank, 0,
                                _e_tz_screenmirror_vblank_handler, mirror);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   mirror->wait_vblank = EINA_TRUE;

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
_e_tz_screenmirror_ui_buffer_cb_free(E_Comp_Wl_Video_Buf *vbuf, void *data)
{
   E_Mirror *mirror = (E_Mirror*)data;

   EINA_SAFETY_ON_NULL_RETURN(mirror);
   mirror->ui_buffer_list = eina_list_remove(mirror->ui_buffer_list, vbuf);
}

static E_Comp_Wl_Video_Buf*
_e_tz_screenmirror_ui_buffer_get(E_Mirror *mirror)
{
   E_Comp_Wl_Video_Buf *vbuf;
   tbm_surface_h buffer;
   Eina_List *l;
   tdm_error err = TDM_ERROR_NONE;

   buffer = tdm_layer_get_displaying_buffer(mirror->tdm_primary_layer, &err);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, NULL);

   EINA_LIST_FOREACH(mirror->ui_buffer_list, l, vbuf)
      if (vbuf->tbm_surface == buffer)
        return vbuf;

   vbuf = e_comp_wl_video_buffer_create_tbm(buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   e_comp_wl_video_buffer_free_func_add(vbuf, _e_tz_screenmirror_ui_buffer_cb_free, mirror);
   mirror->ui_buffer_list = eina_list_append(mirror->ui_buffer_list, vbuf);

   return vbuf;
}

static E_Comp_Wl_Video_Buf*
_e_tz_screenmirror_buffer_by_tbm_surface_get(E_Mirror *mirror, tbm_surface_h buffer)
{
   E_Comp_Wl_Video_Buf *vbuf;
   Eina_List *l;

   EINA_LIST_FOREACH(mirror->ui_buffer_list, l, vbuf)
      if (vbuf->tbm_surface == buffer)
        return vbuf;

   vbuf = e_comp_wl_video_buffer_create_tbm(buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, NULL);

   e_comp_wl_video_buffer_free_func_add(vbuf, _e_tz_screenmirror_ui_buffer_cb_free, mirror);
   mirror->ui_buffer_list = eina_list_append(mirror->ui_buffer_list, vbuf);

   return vbuf;
}

static void
_e_tz_screenmirror_pp_destroy(E_Mirror *mirror)
{
   if (!mirror->pp)
     return;

   tdm_pp_destroy(mirror->pp);
   mirror->pp = NULL;
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
_e_tz_screenmirror_showing_rect_get(Eina_Rectangle *out_rect, Eina_Rectangle *dst_rect, Eina_Rectangle *showing_rect)
{
   showing_rect->x = dst_rect->x;
   showing_rect->y = dst_rect->y;

   if (dst_rect->x >= out_rect->w)
     showing_rect->w = 0;
   else if (dst_rect->x + dst_rect->w > out_rect->w)
     showing_rect->w = out_rect->w - dst_rect->x;
   else
     showing_rect->w = dst_rect->w;

   if (dst_rect->y >= out_rect->h)
     showing_rect->h = 0;
   else if (dst_rect->y + dst_rect->h > out_rect->h)
     showing_rect->h = out_rect->h - dst_rect->y;
   else
     showing_rect->h = dst_rect->h;
}

static Eina_Bool
_e_tz_screenmirror_src_crop_get(E_Mirror *mirror, tdm_layer *layer, Eina_Rectangle *fit, Eina_Rectangle *showing_rect)
{
   tdm_info_layer info;
   tdm_error err = TDM_ERROR_NONE;
   const tdm_output_mode *mode = NULL;
   float ratio_x, ratio_y;
   Eina_Rectangle out_rect;
   Eina_Rectangle dst_rect;

   fit->x = 0;
   fit->y = 0;
   fit->w = 0;
   fit->h = 0;

   tdm_output_get_mode(mirror->tdm_output, &mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   out_rect.x = 0;
   out_rect.y = 0;
   out_rect.w = mode->hdisplay;
   out_rect.h = mode->vdisplay;

   err = tdm_layer_get_info(layer, &info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   dst_rect.x = info.dst_pos.x;
   dst_rect.y = info.dst_pos.y;
   dst_rect.w = info.dst_pos.w;
   dst_rect.h = info.dst_pos.h;

   _e_tz_screenmirror_showing_rect_get(&out_rect, &dst_rect, showing_rect);

   fit->x = info.src_config.pos.x;
   fit->y = info.src_config.pos.y;

   if (info.transform % 2 == 0)
     {
        ratio_x = (float)info.src_config.pos.w / dst_rect.w;
        ratio_y = (float)info.src_config.pos.h / dst_rect.h;

        fit->w = showing_rect->w * ratio_x;
        fit->h = showing_rect->h * ratio_y;
     }
   else
     {
        ratio_x = (float)info.src_config.pos.w / dst_rect.h;
        ratio_y = (float)info.src_config.pos.h / dst_rect.w;

        fit->w = showing_rect->h * ratio_x;
        fit->h = showing_rect->w * ratio_y;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_tz_screenmirror_position_get(E_Mirror *mirror, E_Comp_Wl_Video_Buf *vbuf, Eina_Rectangle *fit)
{
   E_Comp_Wl_Video_Buf *ui = NULL;

   ui = _e_tz_screenmirror_ui_buffer_get(mirror);
   if (ui == NULL)
     {
        ERR("_e_tz_screenmirror_position_get: get ui buf failed");
        return EINA_FALSE;
     }

   if (screenshot_auto_rotation &&
       ((mirror->angle + mirror->eout_rotate) % 360 == 90 || (mirror->angle + mirror->eout_rotate) % 360 == 270))
     _e_tz_screenmirror_center_rect(ui->height, ui->width, vbuf->width, vbuf->height, fit);
   else
     _e_tz_screenmirror_center_rect(ui->width, ui->height, vbuf->width, vbuf->height, fit);

   return EINA_TRUE;
}

static void
_e_tz_screenmirror_dump_get_cropinfo(E_Comp_Wl_Video_Buf *tmp, E_Comp_Wl_Video_Buf *dst, tdm_layer *layer,
                                     int w, int h, Eina_Rectangle *pos, Eina_Rectangle *showing_pos,
                                     Eina_Rectangle *dst_crop, int rotate)
{
   tdm_info_layer info;
   tdm_error err = TDM_ERROR_NONE;

   dst_crop->x = 0;
   dst_crop->y = 0;
   dst_crop->w = 0;
   dst_crop->h = 0;

   err = tdm_layer_get_info(layer, &info);
   EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);

   if (info.src_config.pos.w == w && info.src_config.pos.h == h &&
       pos->x == 0 && pos->y == 0 && pos->w == tmp->width && pos->h == tmp->height)
     {
        dst_crop->x = pos->x;
        dst_crop->y = pos->y;
        dst_crop->w = pos->w;
        dst_crop->h = pos->h;
     }
   else if ((w == pos->w) && (h == pos->h) && (showing_pos->w == pos->w) && (showing_pos->h == pos->h))
     {
        dst_crop->x = info.dst_pos.x + pos->x;
        dst_crop->y = info.dst_pos.y + pos->y;
        dst_crop->w = info.dst_pos.w;
        dst_crop->h = info.dst_pos.h;
     }
   else if (rotate == 0)
     {
        dst_crop->x = showing_pos->x * pos->w / w + pos->x;
        dst_crop->y = showing_pos->y * pos->h / h + pos->y;
        dst_crop->w = showing_pos->w * pos->w / w;
        dst_crop->h = showing_pos->h * pos->h / h;
     }
   else if (rotate == 90)
     {
        dst_crop->x = (h - showing_pos->y - showing_pos->h) * pos->w / h + pos->x;
        dst_crop->y = showing_pos->x * pos->h / w + pos->y;
        dst_crop->w = showing_pos->h * pos->w / h;
        dst_crop->h = showing_pos->w * pos->h / w;
     }
   else if (rotate == 180)
     {
        dst_crop->x = (w - showing_pos->x - showing_pos->w) * pos->w / w + pos->x;
        dst_crop->y = (h - showing_pos->y - showing_pos->h) * pos->h / h + pos->y;
        dst_crop->w = showing_pos->w * pos->w / w;
        dst_crop->h = showing_pos->h * pos->h / h;
     }
   else if (rotate == 270)
     {
        dst_crop->x = showing_pos->y * pos->w / h + pos->x;
        dst_crop->y = (w - showing_pos->x - showing_pos->w) * pos->h / w + pos->y;
        dst_crop->w = showing_pos->h * pos->w / h;
        dst_crop->h = showing_pos->w * pos->h / w;
     }
   else
     {
        dst_crop->x = pos->x;
        dst_crop->y = pos->y;
        dst_crop->w = pos->w;
        dst_crop->h = pos->h;
        ERR("_e_tz_screenmirror_dump_get_cropinfo: unknown case error");
     }
}

static Eina_Bool
_e_tz_screenmirror_video_buffer_convert(E_Mirror *mirror, E_Comp_Wl_Video_Buf *vbuf)
{
   E_Comp_Wl_Video_Buf *ui = NULL;
   tdm_error err = TDM_ERROR_NONE;
   int count;
   int i;
   int rotate = 0;

   ui = _e_tz_screenmirror_ui_buffer_get(mirror);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ui, EINA_FALSE);

   if (mirror->rotate_change)
     {
        int angle = 0;

        angle = (mirror->angle + mirror->eout_rotate) % 360;
        if (angle == 90)
          rotate = 90;
        else if (angle == 180)
          rotate = 180;
        else if (angle == 270)
          rotate = 270;
     }
   else if (screenshot_auto_rotation && mirror->eout_rotate == 90)
     rotate = 90;
   else if (screenshot_auto_rotation && mirror->eout_rotate == 180)
     rotate = 180;
   else if (screenshot_auto_rotation && mirror->eout_rotate == 270)
     rotate = 270;

   err = tdm_output_get_layer_count(mirror->tdm_output, &count);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(count >= 0, EINA_FALSE);

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer;
        tdm_layer_capability capability;
        tbm_surface_h surface = NULL;
        E_Comp_Wl_Video_Buf *tmp = NULL;
        Eina_Rectangle showing_pos = {0, };
        Eina_Rectangle dst_pos = {0, };
        Eina_Rectangle src_crop = {0, };
        Eina_Rectangle dst_crop = {0, };

        layer = tdm_output_get_layer(mirror->tdm_output, i, &err);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        if (layer != mirror->tdm_primary_layer)
          {
             err = tdm_layer_get_capabilities(layer, &capability);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
             if (capability & TDM_LAYER_CAPABILITY_VIDEO)
               continue;

             surface = tdm_layer_get_displaying_buffer(layer, &err);
             if (surface == NULL)
               continue;

             tmp = _e_tz_screenmirror_buffer_by_tbm_surface_get(mirror, surface);
             if (tmp == NULL)
               continue;

             _e_tz_screenmirror_src_crop_get(mirror, layer, &src_crop, &showing_pos);
          }
        else
          {
             tmp = ui;
             src_crop.x = showing_pos.x = 0;
             src_crop.y = showing_pos.y = 0;
             src_crop.w = showing_pos.w = tmp->width;
             src_crop.h = showing_pos.h = tmp->height;
          }

        _e_tz_screenmirror_position_get(mirror, vbuf, &dst_pos);

        _e_tz_screenmirror_dump_get_cropinfo(tmp, vbuf, layer, ui->width, ui->height,
                                             &dst_pos, &showing_pos, &dst_crop, rotate);
        e_comp_wl_video_buffer_convert(tmp, vbuf,
                                       src_crop.x, src_crop.y, src_crop.w, src_crop.h,
                                       dst_crop.x, dst_crop.y, dst_crop.w, dst_crop.h,
                                       EINA_TRUE, rotate, 0, 0);
     }

   return EINA_TRUE;
}

static void
_e_tz_screenmirror_stillshot(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;
   E_Comp_Wl_Video_Buf *ui, *dst;
   tdm_error err = TDM_ERROR_NONE;
   int count;
   int i;
   int rotate = 0;

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
          {
             ERR("_e_tz_screenmirror_stillshot: tmp buffer create fail");
             return;
          }

        dst = buffer->tmp;
     }
   else
     dst = buffer->vbuf;
   EINA_SAFETY_ON_NULL_RETURN(dst);

   e_comp_wl_video_buffer_clear(dst);

   _e_tz_screenmirror_video_buffer_convert(mirror, dst);

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     _e_tz_screenmirror_copy_tmp_buffer(buffer);
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
_e_tz_screenmirror_capture_oneshot_done_handler(tdm_capture *capture, tbm_surface_h buffer, void *user_data)
{
   E_Mirror_Buffer *mirror_buffer = user_data;
   E_Mirror *mirror = mirror_buffer->mirror;

   if (mirror_buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     _e_tz_screenmirror_copy_tmp_buffer(mirror_buffer);

   _e_tz_screenmirror_destroy(mirror);

   DBG("_e_tz_screenmirror_capture_oneshot_done");
}

static Eina_Bool
_e_tz_screenmirror_capture_stream_done(void *data)
{
   E_Mirror *mirror = data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(mirror, ECORE_CALLBACK_CANCEL);

   if (mirror != keep_stream_mirror)
     return ECORE_CALLBACK_CANCEL;

   if (mirror->capture)
     {
        tdm_capture_destroy(mirror->capture);
        mirror->capture = NULL;
     }

   return ECORE_CALLBACK_CANCEL;
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
_e_tz_screenmirror_capture_stream_done_handler(tdm_capture *capture, tbm_surface_h surf, void *user_data)
{
   E_Mirror *mirror = user_data;
   E_Mirror_Buffer *buffer = NULL;

   if (mirror != keep_stream_mirror)
     return;

   buffer = _e_tz_screenmirror_mirrorbuf_find(mirror, surf);
   if (buffer == NULL)
     {
        ERR("_e_tz_screenmirror_capture_stream_done_handler: find mirror buffer failed");
        return;
     }

   _e_tz_screenmirror_buffer_dequeue(buffer);

   if (mirror->started == EINA_FALSE)
     {
        if (eina_list_count(mirror->buffer_queue) == 0)
          mirror->capture_timer = ecore_timer_add((double)1/DUMP_FPS, _e_tz_screenmirror_capture_stream_done, mirror);
     }
}

static E_Client *
_e_tz_screenmirror_top_visible_ec_get()
{
   E_Client *ec;
   Evas_Object *o;
   E_Comp_Wl_Client_Data *cdata;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");

        /* check e_client and skip e_clients not intersects with zone */
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->iconic) continue;
        if (ec->visible == 0) continue;
        if (!(ec->visibility.obscured == 0 || ec->visibility.obscured == 1)) continue;
        if (!ec->frame) continue;
        if (!evas_object_visible_get(ec->frame)) continue;
        /* if ec is subsurface, skip this */
        cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
        if (cdata && cdata->sub.data) continue;

        return ec;
     }

   return NULL;
}

static void
_e_tz_screenmirror_get_angle(E_Mirror *mirror)
{
   E_Client *ec = NULL;
   E_Output *eout = NULL;

   mirror->eout_rotate = 0;
   mirror->angle = 0;

   ec = _e_tz_screenmirror_top_visible_ec_get();
   if (ec)
     {
        mirror->angle = ec->e.state.rot.ang.curr;

        eout = e_output_find(ec->zone->output_id);
        if (eout)
          mirror->eout_rotate = eout->config.rotation;
     }
}

static Eina_Bool
_e_tz_screenmirror_tdm_capture_handle_set(E_Mirror_Buffer *buffer, tdm_capture *capture, tdm_capture_capability cap)
{
   E_Mirror *mirror = buffer->mirror;
   tdm_error err = TDM_ERROR_NONE;

   if (cap == TDM_CAPTURE_CAPABILITY_ONESHOT)
     err = tdm_capture_set_done_handler(capture, _e_tz_screenmirror_capture_oneshot_done_handler, buffer);
   else
     err = tdm_capture_set_done_handler(capture, _e_tz_screenmirror_capture_stream_done_handler, mirror);
   if (err != TDM_ERROR_NONE)
     {
        ERR("_e_tz_screenmirror_tdm_capture_support: tdm_capture set_handler failed");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_tz_screenmirror_tdm_capture_support(E_Mirror_Buffer *buffer, tdm_capture_capability cap)
{
   E_Mirror *mirror = buffer->mirror;
   tdm_error err = TDM_ERROR_NONE;
   tdm_capture *capture = NULL;
   tdm_info_capture capture_info;
   tdm_capture_capability capabilities;
   Eina_Rectangle dst_pos;

   err = tdm_display_get_capture_capabilities(e_comp->e_comp_screen->tdisplay, &capabilities);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(err == TDM_ERROR_NO_CAPABILITY, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   if (capabilities & cap)
     {
        if (mirror->capture)
          return EINA_TRUE;

        capture = tdm_output_create_capture(mirror->tdm_output, &err);
        if (err != TDM_ERROR_NONE)
          {
             ERR("_e_tz_screenmirror_tdm_capture_support: create tdm_capture failed");
             return EINA_FALSE;
          }

        memset(&capture_info, 0, sizeof(tdm_info_capture));
        capture_info.dst_config.size.h = buffer->vbuf->width_from_pitch;
        capture_info.dst_config.size.v = buffer->vbuf->height;
        capture_info.dst_config.pos.x = 0;
        capture_info.dst_config.pos.y = 0;
        capture_info.dst_config.pos.w = buffer->vbuf->width;
        capture_info.dst_config.pos.h = buffer->vbuf->height;
        capture_info.dst_config.format = buffer->vbuf->tbmfmt;
        capture_info.transform = TDM_TRANSFORM_NORMAL;
        if (cap == TDM_CAPTURE_CAPABILITY_ONESHOT)
          capture_info.type = TDM_CAPTURE_TYPE_ONESHOT;
        else
          capture_info.type = TDM_CAPTURE_TYPE_STREAM;

        if (_e_tz_screenmirror_position_get(mirror, buffer->vbuf, &dst_pos))
          {
             capture_info.dst_config.pos.x = dst_pos.x;
             capture_info.dst_config.pos.y = dst_pos.y;
             capture_info.dst_config.pos.w = dst_pos.w;
             capture_info.dst_config.pos.h = dst_pos.h;

             if (mirror->rotate_change)
               {
                  int tmp;

                  tmp = (mirror->angle + mirror->eout_rotate) % 360;
                  if (tmp == 90)
                    capture_info.transform = TDM_TRANSFORM_90;
                  else if (tmp == 180)
                    capture_info.transform = TDM_TRANSFORM_180;
                  else if (tmp == 270)
                    capture_info.transform = TDM_TRANSFORM_270;
               }
             else if (screenshot_auto_rotation && mirror->eout_rotate == 90)
               capture_info.transform = TDM_TRANSFORM_90;
             else if (screenshot_auto_rotation && mirror->eout_rotate == 180)
               capture_info.transform = TDM_TRANSFORM_180;
             else if (screenshot_auto_rotation && mirror->eout_rotate == 270)
               capture_info.transform = TDM_TRANSFORM_270;
          }

        err = tdm_capture_set_info(capture, &capture_info);
        if (err != TDM_ERROR_NONE)
          {
             ERR("_e_tz_screenmirror_tdm_capture_support: tdm_capture set_info failed");
             tdm_capture_destroy(capture);
             return EINA_FALSE;
          }

        if (_e_tz_screenmirror_tdm_capture_handle_set(buffer, capture, cap) == EINA_FALSE)
          {
             tdm_capture_destroy(capture);
             return EINA_FALSE;
          }
        mirror->capture = capture;

        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_tz_screenmirror_capture_cb_timeout(void *data)
{
   E_Mirror *mirror = data;
   E_Mirror_Buffer *buffer;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_GOTO(mirror, done);

   EINA_LIST_FOREACH(mirror->buffer_queue, l, buffer)
     {
        if (!buffer->in_use) break;
     }

   /* can be null when client doesn't queue a buffer previously */
   if (!buffer)
     goto done;

   buffer->in_use = EINA_TRUE;

   _e_tz_screenmirror_buffer_dequeue(buffer);

done:
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_tz_screenmirror_tdm_capture_attach(E_Mirror *mirror, E_Mirror_Buffer *buffer)
{
   tdm_error err = TDM_ERROR_NONE;

   if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
     {
        if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
          {
             ERR("_e_tz_screenmirror_tdm_capture_attach: tmp buffer create fail");
             return EINA_FALSE;
          }

        err = tdm_capture_attach(mirror->capture, buffer->tmp->tbm_surface);
        if (err != TDM_ERROR_NONE)
          {
             e_comp_wl_video_buffer_unref(buffer->tmp);
             buffer->tmp = NULL;
             return EINA_FALSE;
          }
     }
   else
     {
        err = tdm_capture_attach(mirror->capture, buffer->vbuf->tbm_surface);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
     }

   return EINA_TRUE;
}

static void
_e_tz_screenmirror_buffer_queue(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;
   tdm_error err = TDM_ERROR_NONE;
   E_Mirror_Buffer *buffer_list;
   Eina_List *l;

   mirror->buffer_queue = eina_list_append(mirror->buffer_queue, buffer);

   if (mirror->started)
     {
        if (_e_tz_screenmirror_tdm_capture_support(buffer, TDM_CAPTURE_CAPABILITY_STREAM))
          {
             _e_tz_screenmirror_drm_buffer_clear_check(buffer);

             if (e_output_dpms_get(mirror->e_output))
               {
                  if (!mirror->timer)
                    {
                       eina_list_free(mirror->buffer_clear_check);
                       mirror->buffer_clear_check = NULL;
                       _e_tz_screenmirror_drm_buffer_clear_check(buffer);
                       mirror->timer = ecore_timer_add((double)1/DUMP_FPS, _e_tz_screenmirror_capture_cb_timeout, mirror);
                    }
                  EINA_SAFETY_ON_NULL_RETURN(mirror->timer);
                  return;
               }
             else if (mirror->timer)
               {
                  ecore_timer_del(mirror->timer);
                  mirror->timer = NULL;
                  EINA_LIST_FOREACH(mirror->buffer_queue, l, buffer_list)
                    {
                       if (!buffer_list->in_use)
                         {
                            buffer_list->in_use = EINA_TRUE;

                            if (!_e_tz_screenmirror_tdm_capture_attach(mirror, buffer_list))
                              {
                                 ERR("_e_tz_screenmirror_buffer_queue: attach fail");
                                 return;
                              }
                         }
                    }
                  err = tdm_capture_commit(mirror->capture);
                  EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);
               }
             else
               {
                  buffer->in_use = EINA_TRUE;

                  if (!_e_tz_screenmirror_tdm_capture_attach(mirror, buffer))
                    {
                       ERR("_e_tz_screenmirror_buffer_queue: attach fail");
                       return;
                    }
                  err = tdm_capture_commit(mirror->capture);
                  EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);
               }
          }
        else
          {
             if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
               {
                  if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
                    {
                       ERR("_e_tz_screenmirror_buffer_queue: tmp buffer create fail");
                       return;
                    }
               }
             _e_tz_screenmirror_watch_vblank(mirror);
          }
     }
   else
     {
        if (_e_tz_screenmirror_tdm_capture_support(buffer, TDM_CAPTURE_CAPABILITY_STREAM))
          {
             _e_tz_screenmirror_drm_buffer_clear_check(buffer);

             if (e_output_dpms_get(mirror->e_output))
               return;

             if (!_e_tz_screenmirror_tdm_capture_attach(mirror, buffer))
               {
                  ERR("_e_tz_screenmirror_buffer_queue: attach fail");
                  return;
               }
          }
        else
          {
             if (buffer->vbuf->type == E_COMP_WL_VIDEO_BUF_TYPE_SHM)
               {
                  if (!_e_tz_screenmirror_tmp_buffer_create(buffer))
                    {
                       ERR("_e_tz_screenmirror_buffer_queue: tmp buffer create fail");
                       return;
                    }
               }
          }
     }
}

static void
_e_tz_screenmirror_buffer_dequeue(E_Mirror_Buffer *buffer)
{
   E_Mirror *mirror = buffer->mirror;

   EINA_SAFETY_ON_NULL_RETURN(mirror);
   if (!mirror->buffer_queue || !eina_list_data_find_list(mirror->buffer_queue, buffer))
     return;

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

static Eina_Bool
_e_tz_screenmirror_tdm_capture_oneshot(E_Mirror *mirror, E_Mirror_Buffer *buffer)
{
   tdm_error err = TDM_ERROR_NONE;

   if (!_e_tz_screenmirror_tdm_capture_attach(mirror, buffer))
     {
        ERR("_e_tz_screenmirror_tdm_capture_oneshot: attach fail");
        return EINA_FALSE;
     }
   err = tdm_capture_commit(mirror->capture);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
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
_e_tz_screenmirror_vblank_handler(tdm_output *output, unsigned int sequence,
                                  unsigned int tv_sec, unsigned int tv_usec,
                                  void *data)
{
   E_Mirror *mirror = data;
   E_Mirror_Buffer *buffer;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN(mirror);
   if (mirror != keep_stream_mirror)
     return;

   mirror->wait_vblank = EINA_FALSE;

   EINA_LIST_FOREACH(mirror->buffer_queue, l, buffer)
     {
        if (!buffer->in_use) break;
     }

   /* can be null when client doesn't queue a buffer previously */
   if (!buffer)
     return;

   _e_tz_screenmirror_stillshot(buffer);
   _e_tz_screenmirror_buffer_dequeue(buffer);

   /* timer is a substitution for vblank during dpms off. so if timer is running,
    * we don't watch vblank events recursively.
    */
   if (!mirror->timer)
     _e_tz_screenmirror_watch_vblank(mirror);
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
   tdm_error err = TDM_ERROR_NONE;
   int count, i;

   mirror = E_NEW(E_Mirror, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mirror, NULL);

   mirror->stretch = TIZEN_SCREENMIRROR_STRETCH_KEEP_RATIO;
   mirror->shooter = shooter_resource;
   mirror->output = output_resource;
   mirror->wl_output = wl_resource_get_user_data(mirror->output);
   EINA_SAFETY_ON_NULL_GOTO(mirror->wl_output, fail_create);

   mirror->per_vblank = (mirror->wl_output->refresh / (DUMP_FPS * 1000));

   mirror->e_output = e_output_find(mirror->wl_output->id);
   EINA_SAFETY_ON_NULL_GOTO(mirror->e_output, fail_create);

   mirror->tdm_output = mirror->e_output->toutput;
   EINA_SAFETY_ON_NULL_GOTO(mirror->tdm_output, fail_create);

   err = tdm_output_get_layer_count(mirror->tdm_output, &count);
   EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, fail_create);
   EINA_SAFETY_ON_FALSE_GOTO(count >= 0, fail_create);

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer;
        tdm_layer_capability capability;

        layer = tdm_output_get_layer(mirror->tdm_output, i, &err);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, fail_create);

        err = tdm_layer_get_capabilities(layer, &capability);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, fail_create);

        if (capability & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             mirror->tdm_primary_layer = layer;
             break;
          }
     }
   EINA_SAFETY_ON_NULL_GOTO(mirror->tdm_primary_layer, fail_create);

   INF("per_vblank(%d)", mirror->per_vblank);

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
   E_Comp_Wl_Video_Buf *vbuf;

   if (!mirror)
     return;

   if (!_e_tz_screenmirror_find_mirror(mirror))
     return;
   mirror_list = eina_list_remove(mirror_list, mirror);

   if (mirror->capture_timer)
     ecore_timer_del(mirror->capture_timer);
   mirror->capture_timer = NULL;

   if (mirror->timer)
     ecore_timer_del(mirror->timer);
   mirror->timer = NULL;

   if (mirror->client_destroy_listener.notify)
     wl_list_remove(&mirror->client_destroy_listener.link);
   mirror->client_destroy_listener.notify = NULL;

   wl_resource_set_destructor(mirror->resource, NULL);

   _e_tz_screenmirror_pp_destroy(mirror);

   eina_list_free(mirror->buffer_clear_check);
   mirror->buffer_clear_check = NULL;

   EINA_LIST_FOREACH_SAFE(mirror->buffer_queue, l, ll, buffer)
      _e_tz_screenmirror_buffer_free(buffer);
   mirror->buffer_queue = NULL;

   EINA_LIST_FOREACH_SAFE(mirror->ui_buffer_list, l, ll, vbuf)
      e_comp_wl_video_buffer_unref(vbuf);
   mirror->ui_buffer_list = NULL;

   if (mirror->capture)
     tdm_capture_destroy(mirror->capture);
   mirror->capture = NULL;

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

   _e_tz_screenmirror_buffer_dequeue(buffer);
}

static void
_e_tz_screenmirror_cb_start(struct wl_client *client, struct wl_resource *resource)
{
   E_Mirror *mirror = wl_resource_get_user_data(resource);
   tdm_error err = TDM_ERROR_NONE;

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

   if (mirror->capture)
     {
        if (e_output_dpms_get(mirror->e_output))
          {
             if (!mirror->timer)
               mirror->timer = ecore_timer_add((double)1/DUMP_FPS, _e_tz_screenmirror_capture_cb_timeout, mirror);
             EINA_SAFETY_ON_NULL_RETURN(mirror->timer);
             return;
          }

        err = tdm_capture_commit(mirror->capture);
        EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);
     }
   else
     _e_tz_screenmirror_watch_vblank(mirror);
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

   _e_tz_screenmirror_pp_destroy(mirror);
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

static const struct tizen_screenshooter_interface _e_tz_screenshooter_interface =
{
   _e_tz_screenshooter_get_screenmirror,
   _e_tz_screenshooter_set_oneshot_auto_rotation
};

static void
_e_tz_screenshooter_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   int i;

   if (!(res = wl_resource_create(client, &tizen_screenshooter_interface, MIN(version, 1), id)))
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
_e_screenshooter_cb_shoot(struct wl_client *client,
                          struct wl_resource *resource,
                          struct wl_resource *output_resource,
                          struct wl_resource *buffer_resource)
{
   E_Mirror *mirror;
   E_Mirror_Buffer *buffer;

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
        ERR("_e_screenshooter_cb_shoot: priv check failed");
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
        ERR("_e_screenshooter_cb_shoot: dpms on fail");
        goto dump_done;
     }

   _e_tz_screenmirror_get_angle(mirror);
   if (screenshot_auto_rotation)
     if (mirror->angle == 90 || mirror->angle == 270)
       mirror->rotate_change = EINA_TRUE;

   if (_e_tz_screenmirror_tdm_capture_support(buffer, TDM_CAPTURE_CAPABILITY_ONESHOT))
     {
        if (_e_tz_screenmirror_tdm_capture_oneshot(mirror, buffer) == EINA_TRUE)
          return;
        else
          {
             tdm_capture_destroy(mirror->capture);
             mirror->capture = NULL;
          }
     }
   else
     _e_tz_screenmirror_stillshot(buffer);

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

EINTERN Eina_Bool
e_comp_wl_screenshooter_dump(tbm_surface_h tbm_surface)
{
   E_Mirror *mirror = NULL;
   E_Comp_Wl_Video_Buf *vbuf = NULL;
   tdm_error err = TDM_ERROR_NONE;
   int count, i;
   Eina_Bool ret = EINA_FALSE;

   mirror = E_NEW(E_Mirror, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mirror, EINA_FALSE);

   mirror->e_output = e_output_find_by_index(0);
   EINA_SAFETY_ON_NULL_GOTO(mirror->e_output, done);

   mirror->tdm_output = mirror->e_output->toutput;
   EINA_SAFETY_ON_NULL_GOTO(mirror->tdm_output, done);

   err = tdm_output_get_layer_count(mirror->tdm_output, &count);
   EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, done);
   EINA_SAFETY_ON_FALSE_GOTO(count >= 0, done);

   for (i = 0; i < count; i++)
     {
        tdm_layer *layer;
        tdm_layer_capability capability;

        layer = tdm_output_get_layer(mirror->tdm_output, i, &err);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, done);

        err = tdm_layer_get_capabilities(layer, &capability);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, done);

        if (capability & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             mirror->tdm_primary_layer = layer;
             break;
          }
     }
   EINA_SAFETY_ON_NULL_GOTO(mirror->tdm_primary_layer, done);

   vbuf = e_comp_wl_video_buffer_create_tbm(tbm_surface);
   EINA_SAFETY_ON_NULL_GOTO(vbuf, done);

   e_comp_wl_video_buffer_clear(vbuf);

   ret = _e_tz_screenmirror_video_buffer_convert(mirror, vbuf);
   if (!ret)
     ERR("_e_tz_screenmirror_video_buffer_convert fail");

done:
   if (mirror->ui_buffer_list)
     {
        Eina_List *l, *ll;
        E_Comp_Wl_Video_Buf *tmp = NULL;

         EINA_LIST_FOREACH_SAFE(mirror->ui_buffer_list, l, ll, tmp)
           e_comp_wl_video_buffer_unref(tmp);
     }

   if (vbuf)
     e_comp_wl_video_buffer_unref(vbuf);

   E_FREE(mirror);

   return ret;
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
