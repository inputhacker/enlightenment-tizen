#include "e.h"
#include "e_comp_wl_screenshooter_server.h"

#include <wayland-tbm-server.h>

/* handle include for printing uint64_t */
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define COMPOSITOR_VERSION 4

E_API int E_EVENT_WAYLAND_GLOBAL_ADD = -1;
#include "session-recovery-server-protocol.h"

#ifndef EGL_HEIGHT
# define EGL_HEIGHT			0x3056
#endif
#ifndef EGL_WIDTH
# define EGL_WIDTH			0x3057
#endif

/* Resource Data Mapping: (wl_resource_get_user_data)
 *
 * wl_surface == e_pixmap
 * wl_region == eina_tiler
 * wl_subsurface == e_client
 *
 */

static void _e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized);
static void _e_comp_wl_subsurface_restack(E_Client *ec);
static void _e_comp_wl_subsurface_restack_bg_rectangle(E_Client *ec);
static void _e_comp_wl_subsurface_check_below_bg_rectangle(E_Client *ec);
static void _e_comp_wl_subsurface_show(E_Client *ec);
static void _e_comp_wl_subsurface_hide(E_Client *ec);

static E_Client * _e_comp_wl_client_usable_get(pid_t pid, E_Pixmap *ep);

/* local variables */
typedef struct _E_Comp_Wl_Transform_Context
{
   E_Client *ec;
   int direction;
   int degree;
} E_Comp_Wl_Transform_Context;

typedef struct _E_Comp_Wl_Key_Data
{
   uint32_t key;
   Ecore_Device *dev;
} E_Comp_Wl_Key_Data;

static Eina_List *handlers = NULL;
static E_Client *cursor_timer_ec = NULL;
static Eina_Bool need_send_leave = EINA_TRUE;
static Eina_Bool need_send_released = EINA_FALSE;
static Eina_Bool need_send_motion = EINA_TRUE;

static int _e_comp_wl_hooks_delete = 0;
static int _e_comp_wl_hooks_walking = 0;

static Eina_Hash *_last_keydev_hash = NULL;

static Eina_Inlist *_e_comp_wl_hooks[] =
{
   [E_COMP_WL_HOOK_SHELL_SURFACE_READY] = NULL,
   [E_COMP_WL_HOOK_SUBSURFACE_CREATE] = NULL,
   [E_COMP_WL_HOOK_BUFFER_CHANGE] = NULL,
};

static Eina_List *hooks = NULL;

/* local functions */
static void
_e_comp_wl_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Comp_Wl_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_COMP_WL_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_comp_wl_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_comp_wl_hooks[x] = eina_inlist_remove(_e_comp_wl_hooks[x], EINA_INLIST_GET(ch));
         free(ch); 
       }
}

static void
_e_comp_wl_hook_call(E_Comp_Wl_Hook_Point hookpoint, E_Client *ec)
{
   E_Comp_Wl_Hook *ch;

   e_object_ref(E_OBJECT(ec));
   _e_comp_wl_hooks_walking++;
   EINA_INLIST_FOREACH(_e_comp_wl_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, ec);
        if (e_object_is_del(E_OBJECT(ec)))
          break;
     }
   _e_comp_wl_hooks_walking--;
   if ((_e_comp_wl_hooks_walking == 0) && (_e_comp_wl_hooks_delete > 0))
     _e_comp_wl_hooks_clean();
   e_object_unref(E_OBJECT(ec));
}

static void
_e_comp_wl_configure_send(E_Client *ec, Eina_Bool edges, Eina_Bool send_size)
{
   int w, h;

   if (send_size)
     {
        if (e_comp_object_frame_exists(ec->frame))
          w = ec->client.w, h = ec->client.h;
        else
          w = ec->w, h = ec->h;
     }
   else
     w = h = 0;

   ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                       edges * e_comp_wl->resize.edges,
                                       w, h);
}

static void
_e_comp_wl_focus_down_set(E_Client *ec EINA_UNUSED)
{
   // do nothing
}

static void
_e_comp_wl_focus_check(void)
{
   E_Client *ec;

   if (stopping) return;
   ec = e_client_focused_get();
   if ((!ec) || e_pixmap_is_x(ec->pixmap))
     e_grabinput_focus(e_comp->ee_win, E_FOCUS_METHOD_PASSIVE);
}

static Eina_Bool
_e_comp_wl_cb_read(void *data EINA_UNUSED, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   /* dispatch pending wayland events */
   wl_event_loop_dispatch(e_comp_wl->wl.loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static void
_e_comp_wl_cb_prepare(void *data EINA_UNUSED, Ecore_Fd_Handler *hdlr EINA_UNUSED)
{
   /* flush pending client events */
   wl_display_flush_clients(e_comp_wl->wl.disp);
}

E_API void
e_comp_wl_map_size_cal_from_buffer(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   E_Comp_Wl_Buffer *buffer;
   int32_t width, height;

   buffer = e_pixmap_resource_get(ec->pixmap);
   if (!buffer)
     {
        ec->comp_data->width_from_buffer = 0;
        ec->comp_data->height_from_buffer = 0;
        return;
     }

   switch (vp->buffer.transform)
     {
      case WL_OUTPUT_TRANSFORM_90:
      case WL_OUTPUT_TRANSFORM_270:
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:
      case WL_OUTPUT_TRANSFORM_FLIPPED_270:
        width = buffer->h / vp->buffer.scale;
        height = buffer->w / vp->buffer.scale;
        break;
      default:
        width = buffer->w / vp->buffer.scale;
        height = buffer->h / vp->buffer.scale;
        break;
     }

   ec->comp_data->width_from_buffer = width;
   ec->comp_data->height_from_buffer = height;
}

E_API void
e_comp_wl_map_size_cal_from_viewport(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   int32_t width, height;

   width = ec->comp_data->width_from_buffer;
   height = ec->comp_data->height_from_buffer;

   if (width == 0 && height == 0) return;

   if (width != 0 && vp->surface.width != -1)
     {
        ec->comp_data->width_from_viewport = vp->surface.width;
        ec->comp_data->height_from_viewport = vp->surface.height;
        return;
     }

   if (width != 0 && vp->buffer.src_width != wl_fixed_from_int(-1))
     {
        int32_t w = wl_fixed_to_int(wl_fixed_from_int(1) - 1 + vp->buffer.src_width);
        int32_t h = wl_fixed_to_int(wl_fixed_from_int(1) - 1 + vp->buffer.src_height);
        ec->comp_data->width_from_viewport = w ?: 1;
        ec->comp_data->height_from_viewport = h ?: 1;
        return;
     }

   ec->comp_data->width_from_viewport = width;
   ec->comp_data->height_from_viewport = height;
}

static void
_e_comp_wl_map_transform(int width, int height, uint32_t transform, int32_t scale, int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_FLIPPED:     *dx = width  - sx, *dy = sy;          break;
      case WL_OUTPUT_TRANSFORM_90:          *dx = height - sy, *dy = sx;          break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_90:  *dx = height - sy, *dy = width  - sx; break;
      case WL_OUTPUT_TRANSFORM_180:         *dx = width  - sx, *dy = height - sy; break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_180: *dx = sx,          *dy = height - sy; break;
      case WL_OUTPUT_TRANSFORM_270:         *dx = sy,          *dy = width - sx;  break;
      case WL_OUTPUT_TRANSFORM_FLIPPED_270: *dx = sy,          *dy = sx;          break;
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:                              *dx = sx,          *dy = sy;          break;
     }

   *dx *= scale;
   *dy *= scale;
}

static void
_e_comp_wl_map_transform_rect(int width, int height,
                              uint32_t transform, int32_t scale,
                              Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   int x1, x2, y1, y2;

   x1 = srect->x;
   y1 = srect->y;
   x2 = srect->x + srect->w;
   y2 = srect->y + srect->h;

   _e_comp_wl_map_transform(width, height, transform, scale, x1, y1, &x1, &y1);
   _e_comp_wl_map_transform(width, height, transform, scale, x2, y2, &x2, &y2);

   drect->x = MIN(x1, x2);
   drect->y = MIN(y1, y2);
   drect->w = MAX(x1, x2) - drect->x;
   drect->h = MAX(y1, y2) - drect->y;
}

static void
_e_comp_wl_map_scaler_surface_to_buffer(E_Client *ec, int sx, int sy, int *bx, int *by)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   double src_width, src_height;
   double src_x, src_y;

   if (vp->buffer.src_width == wl_fixed_from_int(-1))
     {
        if (vp->surface.width == -1)
          {
             *bx = sx;
             *by = sy;
             return;
          }

        src_x = 0.0;
        src_y = 0.0;
        src_width = ec->comp_data->width_from_buffer;
        src_height = ec->comp_data->height_from_buffer;
     }
   else
     {
        src_x = wl_fixed_to_double(vp->buffer.src_x);
        src_y = wl_fixed_to_double(vp->buffer.src_y);
        src_width = wl_fixed_to_double(vp->buffer.src_width);
        src_height = wl_fixed_to_double(vp->buffer.src_height);
     }

   *bx = sx * src_width / ec->comp_data->width_from_viewport + src_x;
   *by = sy * src_height / ec->comp_data->height_from_viewport + src_y;
}

static void
_e_comp_wl_surface_to_buffer_rect(E_Client *ec, Eina_Rectangle *srect, Eina_Rectangle *drect)
{
   E_Comp_Wl_Buffer_Viewport *vp = &ec->comp_data->scaler.buffer_viewport;
   int xf1, yf1, xf2, yf2;

   if (!ec->comp_data->sub.data)
     {
        *drect = *srect;
        return;
     }

   /* first transform box coordinates if the scaler is set */

   xf1 = srect->x;
   yf1 = srect->y;
   xf2 = srect->x + srect->w;
   yf2 = srect->y + srect->h;

   _e_comp_wl_map_scaler_surface_to_buffer(ec, xf1, yf1, &xf1, &yf1);
   _e_comp_wl_map_scaler_surface_to_buffer(ec, xf2, yf2, &xf2, &yf2);

   srect->x = MIN(xf1, xf2);
   srect->y = MIN(yf1, yf2);
   srect->w = MAX(xf1, xf2) - srect->x;
   srect->h = MAX(yf1, yf2) - srect->y;

   _e_comp_wl_map_transform_rect(ec->comp_data->width_from_buffer,
                                 ec->comp_data->height_from_buffer,
                                 vp->buffer.transform, vp->buffer.scale,
                                 srect, drect);
}

static E_Client*
_e_comp_wl_topmost_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || !ec->comp_data->sub.data)
      return ec;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        if (!parent->comp_data || !parent->comp_data->sub.data)
          return parent;

        parent = parent->comp_data->sub.data->parent;
     }

   return ec;
}

EINTERN Eina_Bool
e_comp_wl_video_subsurface_has(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (!ec->comp_data) return EINA_FALSE;

   if (ec->comp_data->video_client)
     return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     if (e_comp_wl_video_subsurface_has(subc))
        return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     if (e_comp_wl_video_subsurface_has(subc))
        return EINA_TRUE;

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_comp_wl_normal_subsurface_has(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (!ec->comp_data) return EINA_FALSE;

   /* if a leaf client is not video cliet */
   if (ec->comp_data->sub.data && !ec->comp_data->sub.below_list &&
       !ec->comp_data->sub.below_list_pending && !ec->comp_data->video_client)
     return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     if (e_comp_wl_normal_subsurface_has(subc))
        return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     if (e_comp_wl_normal_subsurface_has(subc))
        return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_comp_wl_extern_parent_commit(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     _e_comp_wl_extern_parent_commit(subc);

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     _e_comp_wl_extern_parent_commit(subc);

   _e_comp_wl_subsurface_parent_commit(ec, EINA_TRUE);
}

E_API void
e_comp_wl_map_apply(E_Client *ec)
{
   E_Comp_Wl_Buffer_Viewport *vp;
   E_Comp_Wl_Subsurf_Data *sdata;
   E_Comp_Wl_Client_Data *cdata;
   int x1, y1, x2, y2, x, y;
   int dx, dy;

   if (!ec || !ec->comp_data) return;

   cdata = ec->comp_data;
   sdata = ec->comp_data->sub.data;
   if (sdata)
     {
        dx = sdata->position.x;
        dy = sdata->position.y;

        if (sdata->parent)
          {
             dx += sdata->parent->x;
             dy += sdata->parent->y;
          }

        if (sdata->remote_surface.offscreen_parent)
          {
             E_Client *offscreen_parent = sdata->remote_surface.offscreen_parent;
             Eina_Rectangle *rect;
             Eina_List *l;

             EINA_LIST_FOREACH(offscreen_parent->comp_data->remote_surface.regions, l, rect)
               {
                  /* TODO: If there are one more regions, it means that provider's offscreen
                   * is displayed by one more remote_surfaces. Have to consider it later. At
                   * this time, just consider only one remote_surface.
                   */
                  dx += rect->x;
                  dy += rect->y;
                  break;
               }
          }
     }
   else
     {
        dx = ec->x;
        dy = ec->y;
     }

   evas_object_geometry_get(ec->frame, &x, &y, NULL, NULL);
   if (x != dx || y != dy)
     evas_object_move(ec->frame, dx, dy);

   if (!cdata->viewport_transform)
     {
        cdata->viewport_transform = e_util_transform_new();
        e_client_transform_core_add(ec, cdata->viewport_transform);
     }

   e_util_transform_viewport_set(cdata->viewport_transform, dx, dy,
                                 ec->comp_data->width_from_viewport,
                                 ec->comp_data->height_from_viewport);

   vp = &ec->comp_data->scaler.buffer_viewport;

   if (vp->buffer.src_width == wl_fixed_from_int(-1))
     {
        x1 = 0.0;
        y1 = 0.0;
        x2 = ec->comp_data->width_from_buffer;
        y2 = ec->comp_data->height_from_buffer;
     }
   else
     {
        x1 = wl_fixed_to_int(vp->buffer.src_x);
        y1 = wl_fixed_to_int(vp->buffer.src_y);
        x2 = wl_fixed_to_int(vp->buffer.src_x + vp->buffer.src_width);
        y2 = wl_fixed_to_int(vp->buffer.src_y + vp->buffer.src_height);
     }

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x1, y1, &x, &y);
   e_util_transform_texcoord_set(cdata->viewport_transform, 0, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x2, y1, &x, &y);
   e_util_transform_texcoord_set(cdata->viewport_transform, 1, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x2, y2, &x, &y);
   e_util_transform_texcoord_set(cdata->viewport_transform, 2, x, y);

   _e_comp_wl_map_transform(ec->comp_data->width_from_buffer, ec->comp_data->height_from_buffer,
                            vp->buffer.transform, vp->buffer.scale,
                            x1, y2, &x, &y);
   e_util_transform_texcoord_set(cdata->viewport_transform, 3, x, y);

   e_client_transform_core_update(ec);
}

static void
_e_comp_wl_evas_cb_show(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;
   E_Client *topmost;

   if (!(ec = data)) return;
   if (e_object_is_del(data)) return;

   if (!ec->override) e_hints_window_visible_set(ec);

   if ((!ec->override) && (!ec->re_manage) && (!ec->comp_data->reparented) &&
       (!ec->comp_data->need_reparent))
     {
        ec->comp_data->need_reparent = EINA_TRUE;
        ec->visible = EINA_TRUE;
     }
   if (!e_client_util_ignored_get(ec))
     {
        ec->take_focus = !starting;
        EC_CHANGED(ec);
     }

   if (!ec->comp_data->need_reparent)
     {
        if ((ec->hidden) || (ec->iconic))
          {
             evas_object_hide(ec->frame);
//             e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
          }
        else if (!ec->internal_elm_win)
          evas_object_show(ec->frame);
     }

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_show(tmp->frame);

   topmost = _e_comp_wl_topmost_parent_get(ec);
   if (topmost == ec && (ec->comp_data->sub.list || ec->comp_data->sub.below_list))
     _e_comp_wl_subsurface_show(ec);

   if (ec->comp_data->sub.below_obj)
     evas_object_show(ec->comp_data->sub.below_obj);
}

static void
_e_comp_wl_evas_cb_hide(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec, *tmp;
   Eina_List *l;
   E_Client *topmost;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* Uncommonly some clients's final buffer can be skipped if the client
    * requests unmap of its surface right after wl_surface@commit.
    * So if this client evas object is hidden state and client is already
    * unmmapped, we can consider to clear pixmap image here mandatorily.
    */
   if (!ec->comp_data->mapped)
     e_pixmap_image_clear(ec->pixmap, 1);

   EINA_LIST_FOREACH(ec->e.state.video_child, l, tmp)
     evas_object_hide(tmp->frame);

   topmost = _e_comp_wl_topmost_parent_get(ec);
   if (topmost == ec && (ec->comp_data->sub.list || ec->comp_data->sub.below_list))
     _e_comp_wl_subsurface_hide(ec);

   if (ec->comp_data->sub.below_obj)
     evas_object_hide(ec->comp_data->sub.below_obj);
}

static void
_e_comp_wl_evas_cb_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec;
   E_Client *subc;
   Eina_List *l;
   int x, y;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;
        x = ec->x + subc->comp_data->sub.data->position.x;
        y = ec->y + subc->comp_data->sub.data->position.y;
        evas_object_move(subc->frame, x, y);

        if (subc->comp_data->scaler.viewport)
          {
             E_Comp_Wl_Client_Data *cdata = subc->comp_data;
             if (cdata->viewport_transform)
               e_comp_wl_map_apply(subc);
          }
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;
        x = ec->x + subc->comp_data->sub.data->position.x;
        y = ec->y + subc->comp_data->sub.data->position.y;
        evas_object_move(subc->frame, x, y);

        if (subc->comp_data->scaler.viewport)
          {
             E_Comp_Wl_Client_Data *cdata = subc->comp_data;
             if (cdata->viewport_transform)
               e_comp_wl_map_apply(subc);
          }
     }

   if (ec->comp_data->sub.below_obj)
     evas_object_move(ec->comp_data->sub.below_obj, ec->x, ec->y);
}

static void
_e_comp_wl_send_touch_cancel(E_Client *ec)
{
   Eina_List *l;
   struct wl_resource *res;
   struct wl_client *wc;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;
   if ((ec->ignored) && (!ec->remote_surface.provider)) return;

   wc = wl_resource_get_client(ec->comp_data->surface);

   EINA_LIST_FOREACH(e_comp->wl_comp_data->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;

        wl_touch_send_cancel(res);
     }
}

static void
_e_comp_wl_touch_cancel(void)
{
   if (!e_comp_wl->ptr.ec)
     return;

   if (!need_send_released)
     return;

   _e_comp_wl_send_touch_cancel(e_comp_wl->ptr.ec);

   need_send_released = EINA_FALSE;
   need_send_motion = EINA_FALSE;
}

static void
_e_comp_wl_evas_cb_restack(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Client *ec;
   E_Client *topmost;

   if (!(ec = data) || !ec->comp_data) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->comp_data->sub.restacking) return;

   if (ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
     _e_comp_wl_touch_cancel();

   /* return if ec isn't both a parent of a subsurface and a subsurface itself */
   if (!ec->comp_data->sub.list && !ec->comp_data->sub.below_list && !ec->comp_data->sub.data)
     {
        if (ec->comp_data->sub.below_obj)
          _e_comp_wl_subsurface_restack_bg_rectangle(ec);
        return;
     }

   topmost = _e_comp_wl_topmost_parent_get(ec);

   _e_comp_wl_subsurface_restack(topmost);
   _e_comp_wl_subsurface_restack_bg_rectangle(topmost);

   //To update client stack list
   if ((ec->comp_data->sub.data) &&
       (ec->comp_data->sub.data->parent))
     {
        E_Client *parent;
        Evas_Object *o;

        parent = ec->comp_data->sub.data->parent;

        if ((parent->comp_data->sub.list) &&
            (eina_list_data_find(parent->comp_data->sub.list, ec)))
          {
             //stack above done
             o = evas_object_below_get(ec->frame);
             e_comp_object_layer_update(ec->frame, o, NULL);
          }
        else if ((parent->comp_data->sub.below_list) &&
                 (eina_list_data_find(parent->comp_data->sub.below_list, ec)))
          {
             //stack below done
             o = evas_object_above_get(ec->frame);
             e_comp_object_layer_update(ec->frame, NULL, o);
          }
     }
}

static E_Comp_Wl_Input_Device *
_e_comp_wl_device_last_device_get(Ecore_Device_Class dev_class)
{
   switch (dev_class)
     {
      case ECORE_DEVICE_CLASS_MOUSE:
         return e_comp_wl->input_device_manager.last_device_ptr;
      case ECORE_DEVICE_CLASS_KEYBOARD:
         return e_comp_wl->input_device_manager.last_device_kbd;
      case ECORE_DEVICE_CLASS_TOUCH:
         return e_comp_wl->input_device_manager.last_device_touch;
      default:
         return NULL;;
     }
   return NULL;
}

static void
_e_comp_wl_device_last_device_set(Ecore_Device_Class dev_class, E_Comp_Wl_Input_Device *device)
{
   switch (dev_class)
     {
      case ECORE_DEVICE_CLASS_MOUSE:
         e_comp_wl->input_device_manager.last_device_ptr = device;
         break;
      case ECORE_DEVICE_CLASS_KEYBOARD:
         e_comp_wl->input_device_manager.last_device_kbd = device;
         break;
      case ECORE_DEVICE_CLASS_TOUCH:
         e_comp_wl->input_device_manager.last_device_touch = device;
         break;
      default:
         break;
     }
}

static E_Comp_Wl_Input_Device *
_e_comp_wl_device_client_last_device_get(E_Client *ec, Ecore_Device_Class dev_class)
{
   switch (dev_class)
     {
      case ECORE_DEVICE_CLASS_MOUSE:
         return ec->comp_data->last_device_ptr;
      case ECORE_DEVICE_CLASS_KEYBOARD:
         return ec->comp_data->last_device_kbd;
      case ECORE_DEVICE_CLASS_TOUCH:
         return ec->comp_data->last_device_touch;
      default:
         return NULL;;
     }
   return NULL;
}

static void
_e_comp_wl_device_client_last_device_set(E_Client *ec, Ecore_Device_Class dev_class, E_Comp_Wl_Input_Device *device)
{
   switch (dev_class)
     {
      case ECORE_DEVICE_CLASS_MOUSE:
         ec->comp_data->last_device_ptr = device;
         break;
      case ECORE_DEVICE_CLASS_KEYBOARD:
         ec->comp_data->last_device_kbd = device;
         break;
      case ECORE_DEVICE_CLASS_TOUCH:
         ec->comp_data->last_device_touch = device;
         break;
      default:
         break;
     }
}

static void
_e_comp_wl_device_send_event_device(E_Client *ec, Evas_Device *dev, uint32_t timestamp)
{
   E_Comp_Wl_Input_Device *last_device, *ec_last_device, *input_dev;
   struct wl_resource *dev_res;
   const char *dev_name;
   Ecore_Device_Class dev_class;
   struct wl_client *wc;
   uint32_t serial;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(dev);

   if (!ec) return;
   if (ec->cur_mouse_action || e_comp_wl->drag)
     return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->ignored) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   dev_class = (Ecore_Device_Class)evas_device_class_get(dev);
   dev_name = evas_device_description_get(dev);
   last_device = _e_comp_wl_device_last_device_get(dev_class);
   ec_last_device = _e_comp_wl_device_client_last_device_get(ec, dev_class);

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(e_comp_wl->input_device_manager.device_list, l, input_dev)
     {
        if (!eina_streq(input_dev->identifier, dev_name) || (input_dev->clas != dev_class)) continue;
        if ((!last_device) || (last_device != input_dev) || (!ec_last_device) || (ec_last_device != input_dev))
          {
             _e_comp_wl_device_last_device_set(dev_class, input_dev);
             _e_comp_wl_device_client_last_device_set(ec, dev_class, input_dev);

             EINA_LIST_FOREACH(input_dev->resources, ll, dev_res)
               {
                  if (wl_resource_get_client(dev_res) != wc) continue;
                  tizen_input_device_send_event_device(dev_res, serial, input_dev->identifier, timestamp);
               }
          }
     }
}

static void
_e_comp_wl_device_send_last_event_device(E_Client *ec, Ecore_Device_Class dev_class, uint32_t timestamp)
{
   E_Comp_Wl_Input_Device *last_device;
   struct wl_resource *dev_res;
   struct wl_client *wc;
   uint32_t serial;
   Eina_List *l;

   if (!ec->comp_data || !ec->comp_data->surface) return;

   last_device = _e_comp_wl_device_last_device_get(dev_class);
   if (!last_device) return;

   _e_comp_wl_device_client_last_device_set(ec, dev_class, last_device);

   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(last_device->resources, l, dev_res)
     {
        if (wl_resource_get_client(dev_res) != wc) continue;
        tizen_input_device_send_event_device(dev_res, serial, last_device->identifier, timestamp);
     }
 }

 static void
_e_comp_wl_send_event_device(struct wl_client *wc, uint32_t timestamp, Ecore_Device *dev, uint32_t serial)
{
   E_Comp_Wl_Input_Device *input_dev;
   struct wl_resource *dev_res;
   const char *dev_name;
   Eina_List *l, *ll;

   EINA_SAFETY_ON_NULL_RETURN(dev);

   dev_name = ecore_device_identifier_get(dev);

   EINA_LIST_FOREACH(e_comp_wl->input_device_manager.device_list, l, input_dev)
     {
        if (!eina_streq(input_dev->identifier, dev_name)) continue;
        _e_comp_wl_device_last_device_set(ecore_device_class_get(dev), input_dev);

        EINA_LIST_FOREACH(input_dev->resources, ll, dev_res)
          {
             if (wl_resource_get_client(dev_res) != wc) continue;
             tizen_input_device_send_event_device(dev_res, serial, input_dev->identifier, timestamp);
          }
     }
}

static void
_e_comp_wl_cursor_reload(E_Client *ec)
{
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;
   int cx, cy;

   if (e_comp->pointer->o_ptr && (!evas_object_visible_get(e_comp->pointer->o_ptr)))
     e_pointer_object_set(e_comp->pointer, NULL, 0, 0);

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   cx = wl_fixed_to_int(e_comp_wl->ptr.x) - ec->client.x;
   cy = wl_fixed_to_int(e_comp_wl->ptr.y) - ec->client.y;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_enter(res, serial, ec->comp_data->surface,
                              wl_fixed_from_int(cx), wl_fixed_from_int(cy));
        ec->pointer_enter_sent = EINA_TRUE;
     }
}

static Eina_Bool
_e_comp_wl_cursor_timer(void *data)
{
   E_Client *ec = data;

   e_comp_wl_cursor_hide(ec);

   return ECORE_CALLBACK_CANCEL;
}

static void
_e_comp_wl_device_send_axis(const char *dev_name, Evas_Device_Class dev_class, E_Client *ec, enum tizen_input_device_axis_type axis_type, double value)
{
   E_Comp_Wl_Input_Device *input_dev;
   struct wl_resource *dev_res;
   struct wl_client *wc;
   Eina_List *l, *ll;
   wl_fixed_t f_value;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   f_value = wl_fixed_from_double(value);
   wc = wl_resource_get_client(ec->comp_data->surface);

   EINA_LIST_FOREACH(e_comp_wl->input_device_manager.device_list, l, input_dev)
     {
        if ((strcmp(input_dev->identifier, dev_name)) || (input_dev->clas != (Ecore_Device_Class)dev_class)) continue;
        EINA_LIST_FOREACH(input_dev->resources, ll, dev_res)
          {
             if (wl_resource_get_client(dev_res) != wc) continue;
             tizen_input_device_send_axis(dev_res, axis_type, f_value);
          }
     }
}

static void
_e_comp_wl_device_handle_axes(const char *dev_name, Evas_Device_Class dev_class, E_Client *ec, double radius_x, double radius_y, double pressure, double angle)
{
   if (e_comp_wl->input_device_manager.multi.radius_x != radius_x)
     {
        _e_comp_wl_device_send_axis(dev_name, dev_class, ec, TIZEN_INPUT_DEVICE_AXIS_TYPE_RADIUS_X, radius_x);
        e_comp_wl->input_device_manager.multi.radius_x = radius_x;
     }
   if (e_comp_wl->input_device_manager.multi.radius_y != radius_y)
     {
        _e_comp_wl_device_send_axis(dev_name, dev_class, ec, TIZEN_INPUT_DEVICE_AXIS_TYPE_RADIUS_Y, radius_y);
        e_comp_wl->input_device_manager.multi.radius_y = radius_y;
     }
   if (e_comp_wl->input_device_manager.multi.pressure != pressure)
     {
        _e_comp_wl_device_send_axis(dev_name, dev_class, ec, TIZEN_INPUT_DEVICE_AXIS_TYPE_PRESSURE, pressure);
        e_comp_wl->input_device_manager.multi.pressure = pressure;
     }
   if (e_comp_wl->input_device_manager.multi.angle != angle)
     {
        _e_comp_wl_device_send_axis(dev_name, dev_class, ec, TIZEN_INPUT_DEVICE_AXIS_TYPE_ANGLE, angle);
        e_comp_wl->input_device_manager.multi.angle = angle;
     }
}

static void
_e_comp_wl_evas_cb_mouse_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   Evas_Event_Mouse_In *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;

   ev = event;
   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->comp_data || !ec->comp_data->surface) return;

   e_comp_wl->ptr.ec = ec;
   if (e_comp_wl->drag)
     {
        e_comp_wl_data_device_send_enter(ec);
        return;
     }

   if (e_config->use_cursor_timer)
     {
        if (e_comp_wl->ptr.hide_tmr)
          {
             ecore_timer_del(e_comp_wl->ptr.hide_tmr);
             cursor_timer_ec = ec;
             e_comp_wl->ptr.hide_tmr = ecore_timer_add(e_config->cursor_timer_interval, _e_comp_wl_cursor_timer, ec);
          }
        else
          {
             if (e_pointer_is_hidden(e_comp->pointer))
               return;
          }
     }

   if (!eina_list_count(e_comp_wl->ptr.resources)) return;
   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;

        _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_MOUSE, ev->timestamp);

        wl_pointer_send_enter(res, serial, ec->comp_data->surface,
                              wl_fixed_from_int(ev->canvas.x - ec->client.x),
                              wl_fixed_from_int(ev->canvas.y - ec->client.y));
        ec->pointer_enter_sent = EINA_TRUE;
     }
}

static void
_e_comp_wl_evas_cb_mouse_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Out *ev;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;
   Eina_Bool inside_check;

   ev = event;

   if (!(ec = data)) return;
   inside_check = E_INSIDE(ev->canvas.x, ev->canvas.y,
                          ec->client.x, ec->client.y, ec->client.w, ec->client.h);
   if (ec->cur_mouse_action && inside_check) return;
   if (e_object_is_del(E_OBJECT(e_comp))) return;

   /* FIXME? this is a hack to just reset the cursor whenever we mouse out. not sure if accurate */
   {
      Evas_Object *o;

      ecore_evas_cursor_get(e_comp->ee, &o, NULL, NULL, NULL);
      if ((e_comp->pointer->o_ptr != o) && (e_comp->wl_comp_data->ptr.enabled))
        {
           if ((!e_config->use_cursor_timer) || (!e_pointer_is_hidden(e_comp->pointer)))
             e_pointer_object_set(e_comp->pointer, NULL, 0, 0);
        }
   }

   if (e_comp_wl->ptr.ec == ec)
     e_comp_wl->ptr.ec = NULL;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!ec->comp_data || !ec->comp_data->surface) return;

   if (e_comp_wl->drag)
     {
        e_comp_wl_data_device_send_leave(ec);
        return;
     }

   if (e_config->use_cursor_timer && !e_comp_wl->ptr.hide_tmr)
     {
        if (e_pointer_is_hidden(e_comp->pointer))
          return;
     }

   if (!eina_list_count(e_comp_wl->ptr.resources)) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        if (ec->pointer_enter_sent == EINA_FALSE) continue;

        _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_MOUSE, ev->timestamp);

        wl_pointer_send_leave(res, serial, ec->comp_data->surface);
        ec->pointer_enter_sent = EINA_FALSE;
     }
}

static void
_e_comp_wl_send_touch(E_Client *ec, int idx, int canvas_x, int canvas_y, uint32_t timestamp, Eina_Bool pressed)
{
   Eina_List *l;
   struct wl_client *wc;
   struct wl_resource *res;
   wl_fixed_t x, y;
   uint32_t serial;
   E_Comp_Config *comp_conf = NULL;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (pressed)
     {
        x = wl_fixed_from_int(canvas_x - ec->client.x);
        y = wl_fixed_from_int(canvas_y - ec->client.y);
     }

   comp_conf = e_comp_config_get();

   EINA_LIST_FOREACH(e_comp_wl->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;
        TRACE_INPUT_BEGIN(_e_comp_wl_send_touch);
        if (pressed)
          {
             if (comp_conf && comp_conf->input_log_enable)
               INF("[Server] Touch Down (id: %d, time: %d, x:%d, y:%d)\n", idx, timestamp, canvas_x - ec->client.x, canvas_y - ec->client.y);

             wl_touch_send_down(res, serial, timestamp, ec->comp_data->surface, idx, x, y); //id 0 for the 1st finger
          }
        else
          {
             if (comp_conf && comp_conf->input_log_enable)
               INF("[Server] Touch Up (id: %d, time: %d, x:%d, y:%d)\n", idx, timestamp, canvas_x - ec->client.x, canvas_y - ec->client.y);

             wl_touch_send_up(res, serial, timestamp, idx);
          }
        TRACE_INPUT_END();
     }
}

static void
_e_comp_wl_send_touch_move(E_Client *ec, int idx, int canvas_x, int canvas_y, uint32_t timestamp)
{
   Eina_List *l;
   struct wl_client *wc;
   struct wl_resource *res;
   wl_fixed_t x, y;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);

   x = wl_fixed_from_int(canvas_x - ec->client.x);
   y = wl_fixed_from_int(canvas_y - ec->client.y);

   EINA_LIST_FOREACH(e_comp_wl->touch.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_touch_check(res)) continue;
        wl_touch_send_motion(res, timestamp, idx, x, y);
     }
}

static void
_e_comp_wl_send_mouse_move(E_Client *ec, int x, int y, unsigned int timestamp)
{
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_motion(res, timestamp,
                               wl_fixed_from_int(x - ec->client.x),
                               wl_fixed_from_int(y - ec->client.y));
     }
}

static void
_e_comp_wl_cursor_move_timer_control(E_Client *ec)
{
   if (!e_config->use_cursor_timer) return;

   if (e_comp_wl->ptr.hide_tmr)
     {
        if (cursor_timer_ec == ec)
          {
             ecore_timer_interval_set(e_comp_wl->ptr.hide_tmr, e_config->cursor_timer_interval);
             ecore_timer_reset(e_comp_wl->ptr.hide_tmr);
          }
        else
          {
             ecore_timer_del(e_comp_wl->ptr.hide_tmr);
             cursor_timer_ec = ec;
             e_comp_wl->ptr.hide_tmr = ecore_timer_add(e_config->cursor_timer_interval, _e_comp_wl_cursor_timer, ec);
          }
     }
   else
     {
        cursor_timer_ec = ec;
        e_comp_wl->ptr.hide_tmr = ecore_timer_add(e_config->cursor_timer_interval, _e_comp_wl_cursor_timer, ec);
     }
}

static void
_e_comp_wl_evas_cb_mouse_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Move *ev;
   Evas_Device *dev = NULL;
   const char *dev_name;

   ev = event;

   e_comp->wl_comp_data->ptr.x = wl_fixed_from_int(ev->cur.canvas.x);
   e_comp->wl_comp_data->ptr.y = wl_fixed_from_int(ev->cur.canvas.y);

   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->ignored) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   if ((!need_send_motion) && (!need_send_released) && (ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)) return;

   if ((!e_comp_wl->drag_client) ||
       (!e_client_has_xwindow(e_comp_wl->drag_client)))
     {
        dev = ev->dev;
        dev_name = evas_device_description_get(dev);

        if (dev && (evas_device_class_get(dev) == EVAS_DEVICE_CLASS_TOUCH))
          {
             if (e_comp_wl->touch.pressed & (1 << 0))
               {
                  _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);
                  if (dev_name)
                    _e_comp_wl_device_handle_axes(dev_name, evas_device_class_get(dev),
                                                  ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
                  _e_comp_wl_send_touch_move(ec, 0, ev->cur.canvas.x, ev->cur.canvas.y, ev->timestamp);
               }
             e_pointer_touch_move(e_comp->pointer, ev->cur.output.x, ev->cur.output.y);
          }
        else
          {
             _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);
             _e_comp_wl_send_mouse_move(ec, ev->cur.canvas.x, ev->cur.canvas.y, ev->timestamp);
             e_pointer_mouse_move(e_comp->pointer, ev->cur.output.x, ev->cur.output.y);

             if (e_config->use_cursor_timer)
               {
                 if (e_pointer_is_hidden(e_comp->pointer))
                   _e_comp_wl_cursor_reload(ec);
               }
          }
        _e_comp_wl_cursor_move_timer_control(ec);
     }
}

static void
_e_comp_wl_evas_handle_mouse_button_to_touch(E_Client *ec, uint32_t timestamp, int canvas_x, int canvas_y, Eina_Bool flag)
{
   if (ec->cur_mouse_action || e_comp_wl->drag) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;
   if (ec->ignored) return;

   e_comp_wl->ptr.button = BTN_LEFT;

   _e_comp_wl_send_touch(ec, 0, canvas_x, canvas_y, timestamp, flag);
}

static void
_e_comp_wl_send_mouse_out(E_Client *ec)
{
   struct wl_resource *res;
   struct wl_client *wc;
   uint32_t serial;
   Eina_List *l;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_leave(res, serial, ec->comp_data->surface);
     }
}

static void
_e_comp_wl_evas_cb_mouse_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Mouse_Down *ev = event;
   Evas_Device *dev = NULL;
   const char *dev_name;
   E_Client *focused;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   dev = ev->dev;
   dev_name = evas_device_description_get(dev);

   _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);

   if (dev &&  (evas_device_class_get(dev) == EVAS_DEVICE_CLASS_TOUCH))
     {
        if (dev_name)
          _e_comp_wl_device_handle_axes(dev_name, evas_device_class_get(dev),
                                        ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
        _e_comp_wl_evas_handle_mouse_button_to_touch(ec, ev->timestamp, ev->canvas.x, ev->canvas.y, EINA_TRUE);
        e_pointer_touch_move(e_comp->pointer, ev->output.x, ev->output.y);
        e_comp_wl->touch.pressed |= (1 << 0);
     }
   else
     {
        e_comp_wl_evas_handle_mouse_button(ec, ev->timestamp, ev->button,
                                           WL_POINTER_BUTTON_STATE_PRESSED);
        e_pointer_mouse_move(e_comp->pointer, ev->output.x, ev->output.y);
     }

   need_send_released = EINA_TRUE;

   focused = e_client_focused_get();
   if ((focused) && (ec != focused))
     {
        if (need_send_leave)
          {
             need_send_leave = EINA_FALSE;
             _e_comp_wl_device_send_event_device(focused, dev, ev->timestamp);
             _e_comp_wl_send_mouse_out(focused);
          }
        e_focus_event_mouse_down(ec);
     }
   else
     need_send_leave = EINA_TRUE;
}

static void
_e_comp_wl_evas_cb_mouse_up(void *data, Evas *evas, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Mouse_Up *ev = event;
   Evas_Device *dev = NULL;
   const char *dev_name;
   Evas_Event_Flags flags;

   if (!ec) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!need_send_released)
     {
        need_send_motion = EINA_TRUE;
     }

   flags = evas_event_default_flags_get(evas);
   if (flags & EVAS_EVENT_FLAG_ON_HOLD) goto finish;

   dev = ev->dev;
   dev_name = evas_device_description_get(dev);

   _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);

   if (dev && (evas_device_class_get(dev) == EVAS_DEVICE_CLASS_TOUCH))
     {
        if (dev_name)
          _e_comp_wl_device_handle_axes(dev_name, evas_device_class_get(dev),
                                        ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
        _e_comp_wl_evas_handle_mouse_button_to_touch(ec, ev->timestamp, ev->canvas.x, ev->canvas.y, EINA_FALSE);
        e_comp_wl->touch.pressed &= ~(1 << 0);
     }
   else
     e_comp_wl_evas_handle_mouse_button(ec, ev->timestamp, ev->button,
                                        WL_POINTER_BUTTON_STATE_RELEASED);

finish:
   need_send_released = EINA_FALSE;
}

static void
_e_comp_wl_mouse_wheel_send(E_Client *ec, int direction, int z, int timestamp)
{
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t axis, dir;

   if (direction == 0)
     axis = WL_POINTER_AXIS_VERTICAL_SCROLL;
   else
     axis = WL_POINTER_AXIS_HORIZONTAL_SCROLL;

   if (z < 0)
     dir = -wl_fixed_from_int(abs(z));
   else
     dir = wl_fixed_from_int(z);

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   wc = wl_resource_get_client(ec->comp_data->surface);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        wl_pointer_send_axis(res, timestamp, axis, dir);
     }
}

static void
_e_comp_wl_evas_cb_mouse_wheel(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec;
   Evas_Event_Mouse_Wheel *ev;

   ev = event;
   if (!(ec = data)) return;
   if (ec->cur_mouse_action) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->ignored) return;

   if (!ec->comp_data || !ec->comp_data->surface) return;

   if (!eina_list_count(e_comp_wl->ptr.resources))
     return;

   _e_comp_wl_device_send_event_device(ec, ev->dev, ev->timestamp);

  _e_comp_wl_mouse_wheel_send(ec, ev->direction, ev->z, ev->timestamp);
}

static void
_e_comp_wl_evas_cb_multi_down(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Multi_Down *ev = event;
   Evas_Device *dev = NULL;
   const char *dev_name;
   Evas_Device_Class dev_class;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   /* Do not deliver emulated single touch events to client */
   if (ev->device == 0) return;

   dev = ev->dev;
   if (dev && (dev_name = evas_device_description_get(dev)))
     {
        dev_class = evas_device_class_get(dev);
        _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);
        _e_comp_wl_device_handle_axes(dev_name, dev_class, ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
     }

   _e_comp_wl_send_touch(ec, ev->device, ev->canvas.x, ev->canvas.y, ev->timestamp, EINA_TRUE);
   e_comp_wl->touch.pressed |= (1 << ev->device);
}

static void
_e_comp_wl_evas_cb_multi_up(void *data, Evas *evas, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Multi_Up *ev = event;
   Evas_Device *dev = NULL;
   const char *dev_name;
   Evas_Device_Class dev_class;
   Evas_Event_Flags flags;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   /* Do not deliver emulated single touch events to client */
   if (ev->device == 0) return;

   flags = evas_event_default_flags_get(evas);
   if (flags & EVAS_EVENT_FLAG_ON_HOLD) return;

   dev = ev->dev;
   if (dev && (dev_name = evas_device_description_get(dev)))
     {
        dev_class = evas_device_class_get(dev);
        _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);
        _e_comp_wl_device_handle_axes(dev_name, dev_class, ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
     }

   _e_comp_wl_send_touch(ec, ev->device, 0, 0, ev->timestamp, EINA_FALSE);
   e_comp_wl->touch.pressed &= ~(1 << ev->device);
}

static void
_e_comp_wl_evas_cb_multi_move(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event)
{
   E_Client *ec = data;
   Evas_Event_Multi_Move *ev = event;
   Evas_Device *dev = NULL;
   const char *dev_name;
   Evas_Device_Class dev_class;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data || !ec->comp_data->surface) return;

   /* Do not deliver emulated single touch events to client */
   if (ev->device == 0) return;

   if (e_comp_wl->touch.pressed & (1 << ev->device))
     {
        dev = ev->dev;
        if (dev && (dev_name = evas_device_description_get(dev)))
          {
             dev_class = evas_device_class_get(dev);
             _e_comp_wl_device_send_event_device(ec, dev, ev->timestamp);
             _e_comp_wl_device_handle_axes(dev_name, dev_class, ec, ev->radius_x, ev->radius_y, ev->pressure, ev->angle);
          }
     
        _e_comp_wl_send_touch_move(ec, ev->device, ev->cur.canvas.x, ev->cur.canvas.y, ev->timestamp);
     }
}

static void
_e_comp_wl_client_priority_adjust(int pid, int set, int adj, Eina_Bool use_adj, Eina_Bool adj_child, Eina_Bool do_child)
{
   Eina_List *files;
   char *file, buff[PATH_MAX];
   FILE *f;
   int pid2, ppid;
   int num_read;
   int n;

   if (use_adj)
     n = (getpriority(PRIO_PROCESS, pid) + adj);
   else
     n = set;

   setpriority(PRIO_PROCESS, pid, n);

   if (adj_child)
     use_adj = EINA_TRUE;

   if (!do_child) return;

   files = ecore_file_ls("/proc");
   EINA_LIST_FREE(files, file)
      {
         if (!isdigit(file[0]))
           continue;

         snprintf(buff, sizeof(buff), "/proc/%s/stat", file);
         if ((f = fopen(buff, "r")))
           {
              pid2 = -1;
              ppid = -1;
              num_read = fscanf(f, "%i %*s %*s %i %*s", &pid2, &ppid);
              fclose(f);
              if (num_read == 2 && ppid == pid)
                _e_comp_wl_client_priority_adjust(pid2, set,
                                                  adj, use_adj,
                                                  adj_child, do_child);
           }

         free(file);
      }
}

static void
_e_comp_wl_client_priority_raise(E_Client *ec)
{
   if (ec->netwm.pid <= 0) return;
   if (ec->netwm.pid == getpid()) return;
   _e_comp_wl_client_priority_adjust(ec->netwm.pid,
                                     e_config->priority - 1, -1,
                                     EINA_FALSE, EINA_TRUE, EINA_FALSE);
}

static void
_e_comp_wl_client_priority_normal(E_Client *ec)
{
   if (ec->netwm.pid <= 0) return;
   if (ec->netwm.pid == getpid()) return;
   _e_comp_wl_client_priority_adjust(ec->netwm.pid, e_config->priority, 1,
                                     EINA_FALSE, EINA_TRUE, EINA_FALSE);
}

static Eina_Bool
_e_comp_wl_evas_cb_focus_in_timer(E_Client *ec)
{
   uint32_t serial;
   E_Comp_Wl_Key_Data *k;
   struct wl_resource *res;
   Eina_List *l;
   double t;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   ec->comp_data->on_focus_timer = NULL;

   if (!e_comp_wl->kbd.focused) return EINA_FALSE;
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   t = ecore_time_unix_get();

   EINA_LIST_FOREACH(e_comp_wl->kbd.focused, l, res)
     {
        wl_array_for_each(k, &e_comp_wl->kbd.keys)
          {
             _e_comp_wl_send_event_device(wl_resource_get_client(res), t, k->dev, serial);
             wl_keyboard_send_key(res, serial, t,
                                  k->key, WL_KEYBOARD_KEY_STATE_PRESSED);
          }
     }
   return EINA_FALSE;
}

/* It is called in the following cases:
 *  When a normal ec->frame has focus.
 *  Or launching image ec is replaced to the real ec.
 */
EINTERN void
e_comp_wl_feed_focus_in(E_Client *ec)
{
   E_Client *focused;
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->iconic) return;

   /* block spurious focus events */
   focused = e_client_focused_get();
   if ((focused) && (ec != focused)) return;

   /* raise client priority */
   _e_comp_wl_client_priority_raise(ec);

   wc = wl_resource_get_client(ec->comp_data->surface);

   EINA_LIST_FOREACH(e_comp_wl->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) == wc)
          {
             if (!eina_list_data_find(e_comp_wl->kbd.focused, res))
               e_comp_wl->kbd.focused = eina_list_append(e_comp_wl->kbd.focused, res);
          }
     }

   if (!e_comp_wl->kbd.focused) return;
   e_comp_wl->kbd.focus = ec->comp_data->surface;
   e_comp_wl_input_keyboard_enter_send(ec);
   e_comp_wl_data_device_keyboard_focus_set();
   ec->comp_data->on_focus_timer =
      ecore_timer_add(((e_config->xkb.delay_held_key_input_to_focus)/1000.0),
                      (Ecore_Task_Cb)_e_comp_wl_evas_cb_focus_in_timer, ec);
   int rotation = ec->e.state.rot.ang.curr;
   if (e_comp->pointer->rotation != rotation)
     e_pointer_rotation_set(e_comp->pointer, rotation);
}

static void
_e_comp_wl_evas_cb_focus_in(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   if (!(ec = data)) return;
   e_comp_wl_feed_focus_in(ec);
}

static void
_e_comp_wl_evas_cb_focus_out(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;
   struct wl_resource *res;
   uint32_t serial;
   E_Comp_Wl_Key_Data *k;
   Eina_List *l, *ll;
   double t;

   if (!(ec = data)) return;

   if (!ec->comp_data) return;

   E_FREE_FUNC(ec->comp_data->on_focus_timer, ecore_timer_del);

   /* lower client priority */
   if (!e_object_is_del(data))
     _e_comp_wl_client_priority_normal(ec);


   /* update keyboard modifier state */
   wl_array_for_each(k, &e_comp_wl->kbd.keys)
      e_comp_wl_input_keyboard_state_update(k->key, EINA_FALSE);

   if (!ec->comp_data->surface) return;

   if (!eina_list_count(e_comp_wl->kbd.resources)) return;

   /* send keyboard_leave to all keyboard resources */
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   t = ecore_time_unix_get();

   EINA_LIST_FOREACH_SAFE(e_comp_wl->kbd.focused, l, ll, res)
     {
        wl_array_for_each(k, &e_comp_wl->kbd.keys)
          {
             _e_comp_wl_send_event_device(wl_resource_get_client(res), t, k->dev, serial);
              wl_keyboard_send_key(res, serial, t,
                                   k->key, WL_KEYBOARD_KEY_STATE_RELEASED);
          }
        wl_keyboard_send_leave(res, serial, ec->comp_data->surface);
        e_comp_wl->kbd.focused =
           eina_list_remove_list(e_comp_wl->kbd.focused, l);
     }
}

static void
_e_comp_wl_evas_cb_resize(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;

   if ((ec->shading) || (ec->shaded)) return;
   if (!ec->comp_data->shell.configure_send) return;

   /* TODO: calculate x, y with transfrom object */
   if ((e_client_util_resizing_get(ec)) && (!ec->transformed) && (e_comp_wl->resize.edges))
     {
        int x, y;

        x = ec->mouse.last_down[ec->moveinfo.down.button - 1].w;
        y = ec->mouse.last_down[ec->moveinfo.down.button - 1].h;
        if (e_comp_object_frame_exists(ec->frame))
          e_comp_object_frame_wh_unadjust(ec->frame, x, y, &x, &y);

        switch (ec->resize_mode)
          {
           case E_POINTER_RESIZE_TL:
           case E_POINTER_RESIZE_L:
           case E_POINTER_RESIZE_BL:
             x += ec->mouse.last_down[ec->moveinfo.down.button - 1].mx -
               ec->mouse.current.mx;
             break;
           case E_POINTER_RESIZE_TR:
           case E_POINTER_RESIZE_R:
           case E_POINTER_RESIZE_BR:
             x += ec->mouse.current.mx - ec->mouse.last_down[ec->moveinfo.down.button - 1].mx;
             break;
           default:
             break;;
          }
        switch (ec->resize_mode)
          {
           case E_POINTER_RESIZE_TL:
           case E_POINTER_RESIZE_T:
           case E_POINTER_RESIZE_TR:
             y += ec->mouse.last_down[ec->moveinfo.down.button - 1].my -
               ec->mouse.current.my;
             break;
           case E_POINTER_RESIZE_BL:
           case E_POINTER_RESIZE_B:
           case E_POINTER_RESIZE_BR:
             y += ec->mouse.current.my - ec->mouse.last_down[ec->moveinfo.down.button - 1].my;
             break;
           default:
             break;
          }
        x = E_CLAMP(x, 1, x);
        y = E_CLAMP(y, 1, y);
        ec->comp_data->shell.configure_send(ec->comp_data->shell.surface,
                                            e_comp_wl->resize.edges,
                                            x, y);
     }
   else if ((!ec->fullscreen) && (!ec->maximized) &&
            (!ec->comp_data->maximize_pre))
     _e_comp_wl_configure_send(ec, 1, 1);

   if (ec->comp_data->sub.below_obj)
     evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
}

static void
_e_comp_wl_evas_cb_state_update(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec = data;

   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */

   if (ec->comp_data->shell.configure_send)
     _e_comp_wl_configure_send(ec, 0, 0);
   ec->comp_data->maximize_pre = 0;
}

static void
_e_comp_wl_evas_cb_maximize_pre(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec = data;

   ec->comp_data->maximize_pre = 1;
}

static void
_e_comp_wl_evas_cb_delete_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;

   e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap));

   e_object_del(E_OBJECT(ec));

   _e_comp_wl_focus_check();

   /* TODO: Delete request send ??
    * NB: No such animal wrt wayland */
}

static void
_e_comp_wl_evas_cb_kill_request(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;

   e_comp_ignore_win_del(E_PIXMAP_TYPE_WL, e_pixmap_window_get(ec->pixmap));
   if (ec->comp_data)
     {
        if (ec->comp_data->reparented)
          e_client_comp_hidden_set(ec, EINA_TRUE);
     }

   evas_object_pass_events_set(ec->frame, EINA_TRUE);
   if (ec->visible) evas_object_hide(ec->frame);
   if (!ec->internal) e_object_del(E_OBJECT(ec));

   _e_comp_wl_focus_check();
}

static void
_e_comp_wl_evas_cb_ping(void *data, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (!(ec->comp_data)) return;
   if (!(ec->comp_data->shell.ping)) return;
   if (!(ec->comp_data->shell.surface)) return;

   ec->comp_data->shell.ping(ec->comp_data->shell.surface);
}

static void
_e_comp_wl_evas_cb_color_set(void *data, Evas_Object *obj, void *event EINA_UNUSED)
{
   E_Client *ec;
   int a = 0;

   if (!(ec = data)) return;
   evas_object_color_get(obj, NULL, NULL, NULL, &a);
   if (ec->netwm.opacity == a) return;
   ec->netwm.opacity = a;
   ec->netwm.opacity_changed = EINA_TRUE;
}

static void
_e_comp_wl_buffer_reference_cb_destroy(struct wl_listener *listener, void *data)
{
   E_Comp_Wl_Buffer_Ref *ref;

   ref = container_of(listener, E_Comp_Wl_Buffer_Ref, destroy_listener);
   if ((E_Comp_Wl_Buffer *)data != ref->buffer) return;
   ref->buffer = NULL;
   ref->destroy_listener_usable = EINA_FALSE;
}

static void
_e_comp_wl_buffer_cb_destroy(struct wl_listener *listener, void *data EINA_UNUSED)
{
   E_Comp_Wl_Buffer *buffer;

   buffer = container_of(listener, E_Comp_Wl_Buffer, destroy_listener);

   DBG("Wl Buffer Destroy: b %p owner '%s'(%p)",
       buffer, buffer->debug_info.owner_name, buffer->debug_info.owner_ptr);

   /* remove debug info */
   eina_stringshare_del(buffer->debug_info.owner_name);

   wl_signal_emit(&buffer->destroy_signal, buffer);
   free(buffer);
}

static void
_e_comp_wl_client_evas_init(E_Client *ec)
{
   if (!ec || !ec->comp_data) return;
   if (ec->comp_data->evas_init) return;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,        _e_comp_wl_evas_cb_show,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,        _e_comp_wl_evas_cb_hide,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,        _e_comp_wl_evas_cb_move,        ec);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESTACK,     _e_comp_wl_evas_cb_restack,     ec);


   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_IN,    EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_in,    ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_OUT,   EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_out,   ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_MOVE,  EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_move,  ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_DOWN,  EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_down,  ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_UP,    EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_up,    ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MOUSE_WHEEL, EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_mouse_wheel, ec);

   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_DOWN, EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_down, ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_UP,   EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_up,   ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_MULTI_MOVE, EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_multi_move, ec);

   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_FOCUS_IN,    EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_focus_in,    ec);
   evas_object_event_callback_priority_add(ec->frame, EVAS_CALLBACK_FOCUS_OUT,   EVAS_CALLBACK_PRIORITY_AFTER, _e_comp_wl_evas_cb_focus_out,   ec);

   if (!ec->override)
     {
        evas_object_smart_callback_add(ec->frame, "client_resize",   _e_comp_wl_evas_cb_resize,       ec);
        evas_object_smart_callback_add(ec->frame, "maximize_done",   _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "unmaximize_done", _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "maximize_pre",    _e_comp_wl_evas_cb_maximize_pre, ec);
        evas_object_smart_callback_add(ec->frame, "unmaximize_pre",  _e_comp_wl_evas_cb_maximize_pre, ec);
        evas_object_smart_callback_add(ec->frame, "fullscreen",      _e_comp_wl_evas_cb_state_update, ec);
        evas_object_smart_callback_add(ec->frame, "unfullscreen",    _e_comp_wl_evas_cb_state_update, ec);
     }

   /* setup delete/kill callbacks */
   evas_object_smart_callback_add(ec->frame, "delete_request", _e_comp_wl_evas_cb_delete_request, ec);
   evas_object_smart_callback_add(ec->frame, "kill_request",   _e_comp_wl_evas_cb_kill_request,   ec);

   /* setup ping callback */
   evas_object_smart_callback_add(ec->frame, "ping",           _e_comp_wl_evas_cb_ping,           ec);
   evas_object_smart_callback_add(ec->frame, "color_set",      _e_comp_wl_evas_cb_color_set,      ec);

   ec->comp_data->evas_init = EINA_TRUE;
}

static Eina_Bool
_e_comp_wl_cb_randr_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   Eina_List *l;
   E_Output *eout;
   E_Comp_Screen *e_comp_screen;
   unsigned int transform = WL_OUTPUT_TRANSFORM_NORMAL;

   if (!e_comp) return ECORE_CALLBACK_RENEW;
   if (!e_comp->e_comp_screen) return ECORE_CALLBACK_RENEW;
   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, eout)
     {
        if (!eout->config.enabled)
          {
             e_comp_wl_output_remove(eout->id);
             continue;
          }

        switch (eout->config.rotation)
          {
           case 90:
             transform = WL_OUTPUT_TRANSFORM_90;
             break;
           case 180:
             transform = WL_OUTPUT_TRANSFORM_180;
             break;
           case 270:
             transform = WL_OUTPUT_TRANSFORM_270;
             break;
           case 0:
           default:
             transform = WL_OUTPUT_TRANSFORM_NORMAL;
             break;
          }

        if (!e_comp_wl_output_init(eout->id, eout->info.name,
                                   eout->info.screen,
                                   eout->config.geom.x, eout->config.geom.y,
                                   eout->config.geom.w, eout->config.geom.h,
                                   eout->info.size.w, eout->info.size.h,
                                   eout->config.mode.refresh, 0, transform))
          ERR("Could not initialize screen %s", eout->info.name);
     }

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_comp_wl_cb_comp_object_add(void *data EINA_UNUSED, int type EINA_UNUSED, E_Event_Comp_Object *ev)
{
   E_Client *ec;

   /* try to get the client from the object */
   if (!(ec = e_comp_object_client_get(ev->comp_object)))
     return ECORE_CALLBACK_RENEW;

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return ECORE_CALLBACK_RENEW;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL)
     return ECORE_CALLBACK_RENEW;

   /* if we have not setup evas callbacks for this client, do it */
   if (!ec->comp_data->evas_init) _e_comp_wl_client_evas_init(ec);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_comp_wl_cb_mouse_move(void *d EINA_UNUSED, int t EINA_UNUSED, Ecore_Event_Mouse_Move *ev)
{
   e_comp_wl->ptr.x = wl_fixed_from_int(ev->x);
   e_comp_wl->ptr.y = wl_fixed_from_int(ev->y);
   if (e_comp_wl->selection.target &&
       (!e_client_has_xwindow(e_comp_wl->selection.target)) &&
       e_comp_wl->drag)
     {
        struct wl_resource *res;
        int x, y;

        res = e_comp_wl_data_find_for_client(wl_resource_get_client(e_comp_wl->selection.target->comp_data->surface));
        EINA_SAFETY_ON_NULL_RETURN_VAL(res, ECORE_CALLBACK_RENEW);

        x = ev->x - e_comp_wl->selection.target->client.x;
        y = ev->y - e_comp_wl->selection.target->client.y;

        if (e_comp_wl->drag_client)
          evas_object_move(e_comp_wl->drag_client->frame, ev->x, ev->y);

        wl_data_device_send_motion(res, ev->timestamp, wl_fixed_from_int(x), wl_fixed_from_int(y));
     }
   if (e_comp_wl->drag &&
       e_comp_wl->drag_client &&
       e_client_has_xwindow(e_comp_wl->drag_client))
     {
        _e_comp_wl_send_mouse_move(e_comp_wl->drag_client, ev->x, ev->y, ev->timestamp);
        e_pointer_mouse_move(e_comp->pointer, ev->x, ev->y);
        if (!e_config->use_cursor_timer)
          {
             if (e_pointer_is_hidden(e_comp->pointer))
               _e_comp_wl_cursor_reload(e_comp_wl->ptr.ec);
          }
        _e_comp_wl_cursor_move_timer_control(NULL);
     }

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_comp_wl_cb_mouse_button_cancel(void *d EINA_UNUSED, int t EINA_UNUSED, Ecore_Event_Mouse_Button *ev)
{
   if (e_comp_wl->ptr.ec)
     _e_comp_wl_send_touch_cancel(e_comp_wl->ptr.ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_wl_cb_zone_display_state_change(void *d EINA_UNUSED, int t EINA_UNUSED, E_Event_Zone_Display_State_Change *ev EINA_UNUSED)
{
   if (e_comp_wl->ptr.ec && need_send_released)
     {
        _e_comp_wl_send_touch_cancel(e_comp_wl->ptr.ec);

        need_send_released = EINA_FALSE;
      }

    return ECORE_CALLBACK_PASS_ON;
 }

static Eina_Bool
_e_comp_wl_cb_client_rot_change_end(void *d EINA_UNUSED, int t EINA_UNUSED, E_Event_Client_Rotation_Change_End *ev EINA_UNUSED)
{
   E_Client *focused_ec;
   int rotation;

   focused_ec = e_client_focused_get();
   if (!focused_ec) return ECORE_CALLBACK_PASS_ON;

   rotation = focused_ec->e.state.rot.ang.curr;
   if (e_comp->pointer->rotation != rotation)
     e_pointer_rotation_set(e_comp->pointer, rotation);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_wl_subsurface_restack(E_Client *ec)
{
   E_Client *subc, *temp;
   Eina_List *l;

   if (!ec || !ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   temp = ec;
   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc || !subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        subc->comp_data->sub.restacking = EINA_TRUE;
        evas_object_stack_above(subc->frame, temp->frame);
        subc->comp_data->sub.restacking = EINA_FALSE;
        temp = subc;
     }

   temp = ec;
   EINA_LIST_REVERSE_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc || !subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        subc->comp_data->sub.restacking = EINA_TRUE;
        evas_object_stack_below(subc->frame, temp->frame);
        subc->comp_data->sub.restacking = EINA_FALSE;
        temp = subc;
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     _e_comp_wl_subsurface_restack(subc);

   EINA_LIST_REVERSE_FOREACH(ec->comp_data->sub.below_list, l, subc)
     _e_comp_wl_subsurface_restack(subc);
}

static void
_e_comp_wl_subsurface_restack_bg_rectangle(E_Client *ec)
{
   E_Client *bottom = ec;

   if (!ec->comp_data->sub.below_obj)
     return;

   while (bottom)
     {
        evas_object_stack_below(ec->comp_data->sub.below_obj, bottom->frame);
        bottom = eina_list_nth(bottom->comp_data->sub.below_list, 0);
     }
}

static void
_e_comp_wl_subsurface_show(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;

        if (e_pixmap_resource_get(subc->pixmap) && !subc->comp_data->mapped)
          {
             subc->visible = EINA_TRUE;
             evas_object_show(subc->frame);
             subc->comp_data->mapped = 1;
          }
        _e_comp_wl_subsurface_show(subc);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;

        if (e_pixmap_resource_get(subc->pixmap) && !subc->comp_data->mapped)
          {
             subc->visible = EINA_TRUE;
             evas_object_show(subc->frame);
             subc->comp_data->mapped = 1;
          }
        _e_comp_wl_subsurface_show(subc);
     }
}

static void
_e_comp_wl_subsurface_hide(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        if (!subc->comp_data->sub.data->stand_alone)
          {
             if (subc->comp_data->mapped)
               {
                  subc->visible = EINA_FALSE;
                  evas_object_hide(subc->frame);
                  subc->comp_data->mapped = 0;
               }
             _e_comp_wl_subsurface_hide(subc);
          }
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        if (!subc->comp_data->sub.data->stand_alone)
          {
             if (subc->comp_data->mapped)
               {
                  subc->visible = EINA_FALSE;
                  evas_object_hide(subc->frame);
                  subc->comp_data->mapped = 0;
               }
             _e_comp_wl_subsurface_hide(subc);
          }
     }
}

static E_Client*
_e_comp_wl_subsurface_invisible_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data->sub.data)
      return NULL;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        /* in case of topmost */
        if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return NULL;

        if (!parent->comp_data->sub.data)
          return (!evas_object_visible_get(parent->frame)) ? parent : NULL;

        if (!evas_object_visible_get(parent->frame)){
          if (e_pixmap_resource_get(parent->pixmap))
            return parent;

          if (!parent->comp_data->sub.data->parent)
            return parent;
        }

        parent = parent->comp_data->sub.data->parent;
     }

   return NULL;
}

static Eina_Bool
_e_comp_wl_subsurface_can_show(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata = ec->comp_data->sub.data;
   E_Client *invisible_parent;
   E_Client *topmost;

   /* if it's not subsurface */
   if (!sdata)
     return EINA_FALSE;

   invisible_parent = _e_comp_wl_subsurface_invisible_parent_get(ec);
   topmost = _e_comp_wl_topmost_parent_get(ec);

   /* if topmost is composited by compositor && if there is a invisible parent */
   if (topmost->redirected && invisible_parent)
     return EINA_FALSE;

   return EINA_TRUE;
}

static Eina_Bool
_e_comp_wl_surface_subsurface_order_commit(E_Client *ec)
{
   E_Client *subc, *epc;
   Eina_List *l;
   Eina_Bool need_restack = EINA_FALSE;

   if (!ec->comp_data) return EINA_FALSE;

   if (ec->comp_data->sub.data && (epc = ec->comp_data->sub.data->parent))
     if (epc->comp_data->sub.list_changed)
       need_restack = _e_comp_wl_surface_subsurface_order_commit(epc);

   if (!ec->comp_data->sub.list_changed) return (EINA_FALSE | need_restack);
   ec->comp_data->sub.list_changed = EINA_FALSE;

   /* TODO: need to check more complicated subsurface tree */
   EINA_LIST_FOREACH(ec->comp_data->sub.list_pending, l, subc)
     {
        ec->comp_data->sub.list = eina_list_remove(ec->comp_data->sub.list, subc);
        ec->comp_data->sub.list = eina_list_append(ec->comp_data->sub.list, subc);

        _e_comp_wl_surface_subsurface_order_commit(subc);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     {
        ec->comp_data->sub.below_list = eina_list_remove(ec->comp_data->sub.below_list, subc);
        ec->comp_data->sub.below_list = eina_list_append(ec->comp_data->sub.below_list, subc);

        _e_comp_wl_surface_subsurface_order_commit(subc);
     }

   return EINA_TRUE;
}

static void
_e_comp_wl_surface_state_size_update(E_Client *ec, E_Comp_Wl_Surface_State *state)
{
   Eina_Rectangle *window;

   if (!e_pixmap_size_get(ec->pixmap, &state->bw, &state->bh)) return;
   if (e_comp_object_frame_exists(ec->frame)) return;
   window = &ec->comp_data->shell.window;
   if ((!ec->borderless) && /* FIXME temporarily added this check code
                             * to prevent updating E_Client's size by frame */
       (window->x || window->y || window->w || window->h))
     {
        e_comp_object_frame_geometry_set(ec->frame,
                                         -window->x,
                                         (window->x + window->w) - state->bw,
                                         -window->y,
                                         (window->y + window->h) - state->bh);
     }
   else
     e_comp_object_frame_geometry_set(ec->frame, 0, 0, 0, 0);
}

static void
_e_comp_wl_surface_state_cb_buffer_destroy(struct wl_listener *listener, void *data EINA_UNUSED)
{
   E_Comp_Wl_Surface_State *state;

   state =
     container_of(listener, E_Comp_Wl_Surface_State, buffer_destroy_listener);
   state->buffer = NULL;
}

static void
_e_comp_wl_surface_state_init(E_Comp_Wl_Surface_State *state, int w, int h)
{
   state->new_attach = EINA_FALSE;
   state->buffer = NULL;
   state->buffer_destroy_listener.notify =
     _e_comp_wl_surface_state_cb_buffer_destroy;
   state->sx = state->sy = 0;

   state->input = eina_tiler_new(w, h);
   eina_tiler_tile_size_set(state->input, 1, 1);

   state->opaque = eina_tiler_new(w, h);
   eina_tiler_tile_size_set(state->opaque, 1, 1);

   state->buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
   state->buffer_viewport.buffer.scale = 1;
   state->buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   state->buffer_viewport.surface.width = -1;
   state->buffer_viewport.changed = 0;
}

static void
_e_comp_wl_surface_state_finish(E_Comp_Wl_Surface_State *state)
{
   struct wl_resource *cb;
   Eina_Rectangle *dmg;

   EINA_LIST_FREE(state->frames, cb)
     wl_resource_destroy(cb);

   EINA_LIST_FREE(state->damages, dmg)
     eina_rectangle_free(dmg);

   EINA_LIST_FREE(state->buffer_damages, dmg)
     eina_rectangle_free(dmg);

   if (state->opaque) eina_tiler_free(state->opaque);
   state->opaque = NULL;

   if (state->input) eina_tiler_free(state->input);
   state->input = NULL;

   if (state->buffer) wl_list_remove(&state->buffer_destroy_listener.link);
   state->buffer = NULL;
}

static void
_e_comp_wl_surface_state_buffer_set(E_Comp_Wl_Surface_State *state, E_Comp_Wl_Buffer *buffer)
{
   if (state->buffer == buffer) return;
   if (state->buffer)
     wl_list_remove(&state->buffer_destroy_listener.link);
   state->buffer = buffer;
   if (state->buffer)
     wl_signal_add(&state->buffer->destroy_signal,
                   &state->buffer_destroy_listener);
}

static void
_e_comp_wl_surface_state_commit(E_Client *ec, E_Comp_Wl_Surface_State *state)
{
   Eina_Rectangle *dmg;
   Eina_Bool placed = EINA_TRUE;
   int x = 0, y = 0;
   int w, h;
   E_Comp_Wl_Buffer *buffer;
   struct wl_resource *cb;
   Eina_List *l, *ll;

   if ((ec->ignored) &&
       (ec->comp_data->shell.surface || ec->internal))
     {
        EC_CHANGED(ec);
        ec->new_client = 1;
        e_comp->new_clients++;
        e_client_unignore(ec);
     }

   ec->comp_data->scaler.buffer_viewport = state->buffer_viewport;

   if (state->new_attach)
     e_comp_wl_surface_attach(ec, state->buffer);

   /* emit a apply_viewport signal when the information of viewport and buffer is ready */
   wl_signal_emit(&ec->comp_data->apply_viewport_signal,
                  &ec->comp_data->surface);

   _e_comp_wl_surface_state_buffer_set(state, NULL);

   if ((state->new_attach) ||
       (state->buffer_viewport.changed))
     {
        _e_comp_wl_surface_state_size_update(ec, state);
        e_comp_wl_map_size_cal_from_viewport(ec);

        if (ec->changes.pos)
          {
             e_comp_object_frame_xy_unadjust(ec->frame,
                                             ec->x, ec->y,
                                             &x, &y);
          }
        else
          {
             x = ec->client.x;
             y = ec->client.y;
          }

        if (ec->new_client) placed = ec->placed;

        if (!ec->lock_client_size)
          {
             w = ec->w;
             h = ec->h;

             ec->client.w = state->bw;
             ec->client.h = state->bh;

             e_comp_object_frame_wh_adjust(ec->frame,
                                           ec->client.w, ec->client.h,
                                           &ec->w, &ec->h);

             if ((w != ec->w) || (h != ec->h))
               {
                  ec->changes.size = 1;
                  EC_CHANGED(ec);
               }
          }
     }

   /* map or unmap ec */
   if (!e_pixmap_usable_get(ec->pixmap))
     {
        /* unmap ec */
        if (ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) &&
                 (ec->comp_data->shell.unmap))
               {
                  ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
               }
             else if ((ec->internal) ||
                      (ec->comp_data->sub.data) ||
                      (ec == e_comp_wl->drag_client))
               {
                  ec->visible = EINA_FALSE;
                  evas_object_hide(ec->frame);
                  ec->comp_data->mapped = 0;
               }
          }

        if ((ec->comp_data->sub.below_obj) &&
            (evas_object_visible_get(ec->comp_data->sub.below_obj)))
          {
             evas_object_hide(ec->comp_data->sub.below_obj);
          }
     }
   else
     {
        /* map ec */
        if (!ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) &&
                 (ec->comp_data->shell.map))
               {
                  ec->comp_data->shell.map(ec->comp_data->shell.surface);
               }
             else if ((ec->internal) ||
                      (_e_comp_wl_subsurface_can_show(ec)) ||
                      (ec == e_comp_wl->drag_client))
               {
                  ec->visible = EINA_TRUE;
                  ec->ignored = 0;
                  evas_object_show(ec->frame);
                  ec->comp_data->mapped = 1;
               }
          }

        if ((ec->comp_data->sub.below_obj) &&
            (!evas_object_visible_get(ec->comp_data->sub.below_obj)) &&
            (evas_object_visible_get(ec->frame)))
          {
             evas_object_show(ec->comp_data->sub.below_obj);
          }
     }

   if ((state->new_attach) ||
       (state->buffer_viewport.changed))
     {
        if ((ec->comp_data->shell.surface) &&
            (ec->comp_data->shell.configure))
          {
             ec->comp_data->shell.configure(ec->comp_data->shell.surface,
                                            x, y,
                                            ec->w, ec->h);
          }
        else
          {
             if ((e_comp_wl->drag) &&
                 (e_comp_wl->drag_client) &&
                 (e_comp_wl->drag_client == ec))
               {
                  e_comp_wl->drag->dx -= state->sx;
                  e_comp_wl->drag->dy -= state->sy;

                  e_drag_move(e_comp_wl->drag,
                              e_comp_wl->drag->x + state->sx,
                              e_comp_wl->drag->y + state->sy);

                  e_drag_resize(e_comp_wl->drag,
                                state->bw, state->bh);
               }
             else
               {
                  e_client_util_move_resize_without_frame(ec, x, y, ec->w, ec->h);
               }
          }

        if (ec->new_client)
          {
             ec->placed = placed;
             ec->want_focus |= ec->icccm.accepts_focus && (!ec->override);
          }
     }

   if (ec->comp_data->scaler.buffer_viewport.changed)
     e_comp_wl_map_apply(ec);

   /* resize transform object */
   if (ec->transformed)
     e_client_transform_update(ec);

   state->sx = 0;
   state->sy = 0;
   state->new_attach = EINA_FALSE;

   EINA_LIST_FOREACH_SAFE(ec->comp_data->frames, l, ll, cb)
     {
        wl_callback_send_done(cb, ecore_time_unix_get() * 1000);
        wl_resource_destroy(cb);
     }

   /* insert state frame callbacks into comp_data->frames
    * NB: This clears state->frames list */
   ec->comp_data->frames = eina_list_merge(ec->comp_data->frames,
                                           state->frames);
   state->frames = NULL;

   buffer = e_pixmap_resource_get(ec->pixmap);

   /* put state damages into surface */
   if (ec->frame)
     {
        /* FIXME: workaround for bad wayland egl driver which doesn't send damage request */
        if (!eina_list_count(state->damages) && !eina_list_count(state->buffer_damages))
          {
             if ((ec->comp_data->buffer_ref.buffer) &&
                 (ec->comp_data->buffer_ref.buffer->type == E_COMP_WL_BUFFER_TYPE_NATIVE))
               {
                  e_comp_object_damage(ec->frame,
                                       0, 0,
                                       ec->comp_data->buffer_ref.buffer->w,
                                       ec->comp_data->buffer_ref.buffer->h);
               }
          }
        else
          {
             Eina_List *damages = NULL;

             if (eina_list_count(state->buffer_damages))
               {
                  damages = eina_list_clone(state->buffer_damages);
                  state->buffer_damages = eina_list_free(state->buffer_damages);
               }

             EINA_LIST_FREE(state->damages, dmg)
               {
                  if (ec->comp_data->sub.data &&
                      (ec->comp_data->scaler.buffer_viewport.surface.width != -1 ||
                       ec->comp_data->scaler.buffer_viewport.buffer.src_width != wl_fixed_from_int(-1)))
                    {
                       Eina_Rectangle temp = {0,};
                       /* change to the buffer cordinate if subsurface */
                       _e_comp_wl_surface_to_buffer_rect(ec, dmg, &temp);
                       *dmg = temp;
                    }

                  damages = eina_list_append(damages, dmg);
               }

             EINA_LIST_FREE(damages, dmg)
               {
                  /* not creating damage for ec that shows a underlay video */
                  if (state->buffer_viewport.changed ||
                      !e_comp->wl_comp_data->available_hw_accel.underlay ||
                      !buffer || buffer->type != E_COMP_WL_BUFFER_TYPE_VIDEO)
                    e_comp_object_damage(ec->frame, dmg->x, dmg->y, dmg->w, dmg->h);

                  eina_rectangle_free(dmg);
               }
          }
     }

   /* put state opaque into surface */
   e_pixmap_image_opaque_set(ec->pixmap, 0, 0, 0, 0);
   if (state->opaque)
     {
        Eina_Rectangle *rect;
        Eina_Iterator *itr;

        itr = eina_tiler_iterator_new(state->opaque);
        EINA_ITERATOR_FOREACH(itr, rect)
          {
             Eina_Rectangle r;

             EINA_RECTANGLE_SET(&r, rect->x, rect->y, rect->w, rect->h);
             E_RECTS_CLIP_TO_RECT(r.x, r.y, r.w, r.h, 0, 0, state->bw, state->bh);
             e_pixmap_image_opaque_set(ec->pixmap, r.x, r.y, r.w, r.h);
             break;
          }

        eina_iterator_free(itr);
     }

   /* put state input into surface */
   if ((state->input) &&
       (!eina_tiler_empty(state->input)))
     {
        Eina_Tiler *src, *tmp;

        tmp = eina_tiler_new(state->bw, state->bh);
        eina_tiler_tile_size_set(tmp, 1, 1);
        eina_tiler_rect_add(tmp,
                            &(Eina_Rectangle){0, 0, state->bw, state->bh});
        if ((src = eina_tiler_intersection(state->input, tmp)))
          {
             Eina_Rectangle *rect;
             Eina_Iterator *itr;

             e_comp_object_input_objs_del(ec->frame);
             itr = eina_tiler_iterator_new(src);
             EINA_ITERATOR_FOREACH(itr, rect)
               {
                  e_comp_object_input_area_set(ec->frame,
                                               rect->x, rect->y,
                                               rect->w, rect->h);
               }

             eina_iterator_free(itr);
             eina_tiler_free(src);
          }
        else
          e_comp_object_input_area_set(ec->frame, 0, 0, ec->w, ec->h);

        eina_tiler_free(tmp);

        /* clear input tiler */
        eina_tiler_clear(state->input);
     }

   _e_comp_wl_subsurface_check_below_bg_rectangle(ec);

   if (((buffer) &&
        (buffer->type == E_COMP_WL_BUFFER_TYPE_VIDEO)) &&
       (e_comp->wl_comp_data->available_hw_accel.underlay))
     {
        e_pixmap_image_clear(ec->pixmap, 1);
     }

   state->buffer_viewport.changed = 0;
}

static void
_e_comp_wl_surface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   DBG("Surface Cb Destroy: %d", wl_resource_get_id(resource));
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_surface_cb_attach(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *buffer_resource, int32_t sx, int32_t sy)
{
   E_Client *ec;
   E_Comp_Wl_Buffer *buffer = NULL;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (buffer_resource)
     {
        if (!(buffer = e_comp_wl_buffer_get(buffer_resource, ec)))
          {
             ERR("Could not get buffer from resource");
             wl_client_post_no_memory(client);
             return;
          }
     }

   _e_comp_wl_surface_state_buffer_set(&ec->comp_data->pending, buffer);

   ec->comp_data->pending.sx = sx;
   ec->comp_data->pending.sy = sy;
   ec->comp_data->pending.new_attach = EINA_TRUE;
}

static void
_e_comp_wl_surface_cb_damage(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Client *ec;
   Eina_Rectangle *dmg = NULL;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!(dmg = eina_rectangle_new(x, y, w, h))) return;

   ec->comp_data->pending.damages =
     eina_list_append(ec->comp_data->pending.damages, dmg);
}

static void
_e_comp_wl_frame_cb_destroy(struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)))
     {
        if (!e_object_delay_del_ref_get(E_OBJECT(ec)))
          return;
     }
   if (!ec->comp_data) return;

   if (ec->comp_data->frames)
     {
        ec->comp_data->frames =
          eina_list_remove(ec->comp_data->frames, resource);
     }

   if (ec->comp_data->pending.frames)
     {
        ec->comp_data->pending.frames =
          eina_list_remove(ec->comp_data->pending.frames, resource);
     }
}

static void
_e_comp_wl_surface_cb_frame(struct wl_client *client, struct wl_resource *resource, uint32_t callback)
{
   E_Client *ec;
   struct wl_resource *res;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* create frame callback */
   if (!(res =
         wl_resource_create(client, &wl_callback_interface, 1, callback)))
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, NULL, ec, _e_comp_wl_frame_cb_destroy);

   ec->comp_data->pending.frames =
     eina_list_prepend(ec->comp_data->pending.frames, res);
}

static void
_e_comp_wl_surface_cb_opaque_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (ec->comp_data->pending.opaque)
     eina_tiler_clear(ec->comp_data->pending.opaque);
   if (region_resource)
     {
        Eina_Tiler *tmp;

        if (!(tmp = wl_resource_get_user_data(region_resource)))
          return;

        eina_tiler_union(ec->comp_data->pending.opaque, tmp);

        if (!eina_tiler_empty(ec->comp_data->pending.opaque))
          {
             if (ec->argb)
               {
                  ec->argb = EINA_FALSE;
                  EC_CHANGED(ec);
                  e_comp_object_alpha_set(ec->frame, EINA_FALSE);
               }
          }
     }
   else
     {
        if (!ec->argb)
          {
             ec->argb = EINA_TRUE;
             EC_CHANGED(ec);
             e_comp_object_alpha_set(ec->frame, EINA_TRUE);
          }
     }
}

static void
_e_comp_wl_surface_cb_input_region_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *region_resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (ec->comp_data->pending.input)
     eina_tiler_clear(ec->comp_data->pending.input);
   if (region_resource)
     {
        Eina_Tiler *tmp;

        if (!(tmp = wl_resource_get_user_data(region_resource)))
          return;

        if (eina_tiler_empty(tmp))
          {
             ELOGF("COMP", "         |unset input rect", NULL, NULL);
             e_comp_object_input_objs_del(ec->frame);
             e_comp_object_input_area_set(ec->frame, -1, -1, 1, 1);
          }
        else
          eina_tiler_union(ec->comp_data->pending.input, tmp);
     }
   else
     {
        eina_tiler_rect_add(ec->comp_data->pending.input,
                            &(Eina_Rectangle){0, 0, ec->client.w, ec->client.h});
     }
}

static void
_e_comp_wl_surface_cb_commit(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec, *subc;
   Eina_List *l;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (ec->comp_data->need_commit_extern_parent)
     {
        ec->comp_data->need_commit_extern_parent = 0;
        _e_comp_wl_extern_parent_commit(ec);
     }

   if (e_comp_wl_remote_surface_commit(ec)) return;
   if (e_comp_wl_subsurface_commit(ec)) return;

   e_comp_wl_surface_commit(ec);

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (ec != subc)
          _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (ec != subc)
          _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
     }
}

static void
_e_comp_wl_surface_cb_buffer_transform_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t transform)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (transform < 0 || transform > WL_OUTPUT_TRANSFORM_FLIPPED_270)
     {
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_INVALID_TRANSFORM,
                               "buffer transform must be a valid transform "
                               "('%d' specified)", transform);
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.transform = transform;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_comp_wl_surface_cb_buffer_scale_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t scale)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (scale < 1)
     {
        wl_resource_post_error(resource,
                               WL_SURFACE_ERROR_INVALID_SCALE,
                               "buffer scale must be at least one "
                               "('%d' specified)", scale);
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.scale = scale;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_comp_wl_surface_cb_damage_buffer(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   E_Client *ec;
   Eina_Rectangle *dmg = NULL;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   if (!(dmg = eina_rectangle_new(x, y, w, h))) return;

   ec->comp_data->pending.buffer_damages =
     eina_list_append(ec->comp_data->pending.buffer_damages, dmg);
}

static const struct wl_surface_interface _e_surface_interface =
{
   _e_comp_wl_surface_cb_destroy,
   _e_comp_wl_surface_cb_attach,
   _e_comp_wl_surface_cb_damage,
   _e_comp_wl_surface_cb_frame,
   _e_comp_wl_surface_cb_opaque_region_set,
   _e_comp_wl_surface_cb_input_region_set,
   _e_comp_wl_surface_cb_commit,
   _e_comp_wl_surface_cb_buffer_transform_set,
   _e_comp_wl_surface_cb_buffer_scale_set,
   _e_comp_wl_surface_cb_damage_buffer,
};

static void
_e_comp_wl_surface_render_stop(E_Client *ec)
{
   /* FIXME: this may be fine after e_pixmap can create textures for wl clients? */
   //if ((!ec->internal) && (!e_comp_gl_get()))
     ec->dead = 1;

   /* check if internal animation is running */
   if (e_comp_object_is_animating(ec->frame)) return;
   /* check if external animation is running */
   if (evas_object_data_get(ec->frame, "effect_running")) return;

   evas_object_hide(ec->frame);
}

static void
_e_comp_wl_surface_destroy(struct wl_resource *resource)
{
   E_Client *ec;
   struct wl_resource *res;
   Eina_List *l, *ll;

   if (!(ec = wl_resource_get_user_data(resource))) return;

   if (ec == e_client_focused_get())
     {
        EINA_LIST_FOREACH_SAFE(e_comp_wl->kbd.focused, l, ll, res)
          {
             if (wl_resource_get_client(res) ==
                 wl_resource_get_client(ec->comp_data->surface))
               e_comp_wl->kbd.focused =
                  eina_list_remove_list(e_comp_wl->kbd.focused, l);
          }
     }

   ec->comp_data->surface = NULL;
   ec->comp_data->wl_surface = NULL;
   e_pixmap_win_id_del(ec->pixmap);

   _e_comp_wl_surface_render_stop(ec);
   e_object_del(E_OBJECT(ec));
}

static void
_e_comp_wl_compositor_cb_surface_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   struct wl_resource *res;
   E_Pixmap *ep = NULL;
   E_Client *ec = NULL;
   pid_t pid;
   int internal = 0;

   DBG("Compositor Cb Surface Create: %d", id);

   TRACE_DS_BEGIN(COMP_WL:SURFACE CREATE CB);

   /* try to create an internal surface */
   if (!(res = wl_resource_create(client, &wl_surface_interface,
                                  wl_resource_get_version(resource), id)))
     {
        ERR("Could not create compositor surface");
        wl_client_post_no_memory(client);
        TRACE_DS_END();
        return;
     }

   DBG("\tCreated Resource: %d", wl_resource_get_id(res));

   /* set implementation on resource */
   wl_resource_set_implementation(res, &_e_surface_interface, NULL,
                                  _e_comp_wl_surface_destroy);

   wl_client_get_credentials(client, &pid, NULL, NULL);
   if (pid == getpid())
     {
        /* pixmap of internal win was supposed to be created at trap show */
        internal = 1;
        ec = e_pixmap_find_client(E_PIXMAP_TYPE_WL, (uintptr_t)id);
     }
   else
     {
        if ((ep = e_pixmap_find(E_PIXMAP_TYPE_WL, (uintptr_t)res)))
          {
             ERR("There is e_pixmap already, Delete old e_pixmap %p", ep);
             e_pixmap_win_id_del(ep);
             ep = NULL;
          }
     }

   if (!ec)
     {
        /* try to create new pixmap */
        if (!(ep = e_pixmap_new(E_PIXMAP_TYPE_WL, res)))
          {
             ERR("Could not create new pixmap");
             wl_resource_destroy(res);
             wl_client_post_no_memory(client);
             TRACE_DS_END();
             return;
          }

        E_Comp_Wl_Client_Data *cdata = e_pixmap_cdata_get(ep);
        if (cdata)
          cdata->wl_surface = res;

        DBG("\tUsing Pixmap: %p", ep);

        if (!(ec = _e_comp_wl_client_usable_get(pid, ep)))
          {
             ec = e_client_new(ep, 0, internal);
          }
     }
   if (ec)
     {
        if (!ec->netwm.pid)
          ec->netwm.pid = pid;
        if (ec->new_client)
          e_comp->new_clients--;
        ec->new_client = 0;
        if ((!ec->client.w) && (ec->client.h))
          ec->client.w = ec->client.h = 1;
        ec->comp_data->surface = res;
     }

   /* set reference to pixmap so we can fetch it later */
   DBG("\tUsing Client: %p", ec);
   wl_resource_set_user_data(res, ec);

   /* emit surface create signal */
   wl_signal_emit(&e_comp_wl->signals.surface.create, res);

   TRACE_DS_END();
}

static void
_e_comp_wl_region_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   DBG("Region Destroy: %d", wl_resource_get_id(resource));
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_region_cb_add(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   Eina_Tiler *tiler;

   DBG("Region Add: %d", wl_resource_get_id(resource));
   DBG("\tGeom: %d %d %d %d", x, y, w, h);

   /* get the tiler from the resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     eina_tiler_rect_add(tiler, &(Eina_Rectangle){x, y, w, h});
}

static void
_e_comp_wl_region_cb_subtract(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y, int32_t w, int32_t h)
{
   Eina_Tiler *tiler;

   DBG("Region Subtract: %d", wl_resource_get_id(resource));
   DBG("\tGeom: %d %d %d %d", x, y, w, h);

   /* get the tiler from the resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     eina_tiler_rect_del(tiler, &(Eina_Rectangle){x, y, w, h});
}

static const struct wl_region_interface _e_region_interface =
{
   _e_comp_wl_region_cb_destroy,
   _e_comp_wl_region_cb_add,
   _e_comp_wl_region_cb_subtract
};

static void
_e_comp_wl_compositor_cb_region_destroy(struct wl_resource *resource)
{
   Eina_Tiler *tiler;

   DBG("Compositor Region Destroy: %d", wl_resource_get_id(resource));

   /* try to get the tiler from the region resource */
   if ((tiler = wl_resource_get_user_data(resource)))
     eina_tiler_free(tiler);
}

static void
_e_comp_wl_compositor_cb_region_create(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
   Eina_Tiler *tiler;
   struct wl_resource *res;

   DBG("Region Create: %d", wl_resource_get_id(resource));

   /* try to create new tiler */
   if (!(tiler = eina_tiler_new(e_comp->w, e_comp->h)))
     {
        ERR("Could not create Eina_Tiler");
        wl_resource_post_no_memory(resource);
        return;
     }

   /* set tiler size */
   eina_tiler_tile_size_set(tiler, 1, 1);

   if (!(res = wl_resource_create(client, &wl_region_interface, 1, id)))
     {
        ERR("\tFailed to create region resource");
        wl_resource_post_no_memory(resource);
        return;
     }

   wl_resource_set_implementation(res, &_e_region_interface, tiler,
                                  _e_comp_wl_compositor_cb_region_destroy);
}

static const struct wl_compositor_interface _e_comp_interface =
{
   _e_comp_wl_compositor_cb_surface_create,
   _e_comp_wl_compositor_cb_region_create
};

static void
_e_comp_wl_pname_get(pid_t pid, char *name, int size)
{
   if (!name) return;

   FILE *h;
   char proc[512], pname[512];
   size_t len;

   snprintf(proc, 512,"/proc/%d/cmdline", pid);

   h = fopen(proc, "r");
   if (!h) return;

   len = fread(pname, sizeof(char), 512, h);
   if (len > 0)
     pname[len - 1] = '\0';
   else
     strncpy(pname, "NO NAME", sizeof(pname));

   fclose(h);

   strncpy(name, pname, size);
}

static void
_e_comp_wl_pname_print(pid_t pid)
{
   FILE *h;
   char proc[512], pname[512];
   size_t len;

   snprintf(proc, 512,"/proc/%d/cmdline", pid);

   h = fopen(proc, "r");
   if (!h) return;

   len = fread(pname, sizeof(char), 512, h);
   if (len > 0)
     pname[len - 1] = '\0';
   else
     strncpy(pname, "NO NAME", sizeof(pname));

   fclose(h);

   ELOGF("COMP", "         |%s", NULL, NULL, pname);
}


static void
_e_comp_wl_compositor_cb_unbind(struct wl_resource *res_comp)
{
   struct wl_client *client;
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;

   client = wl_resource_get_client(res_comp);
   if (client)
     wl_client_get_credentials(client,
                               &pid,
                               &uid,
                               &gid);

   ELOGF("COMP",
         "UNBIND   |res_comp:0x%08x|client:0x%08x|%d|%d|%d",
         NULL, NULL,
         (unsigned int)res_comp,
         (unsigned int)client,
         pid, uid, gid);

   E_Comp *comp;
   if ((comp = wl_resource_get_user_data(res_comp)))
     {
        Eina_List *l;
        E_Comp_Connected_Client_Info *cinfo;
        EINA_LIST_FOREACH(comp->connected_clients, l, cinfo)
          {
             if (cinfo->pid == pid)
               break;
             cinfo = NULL;
          }
        if (cinfo)
          {
             if (cinfo->name)
               eina_stringshare_del(cinfo->name);
             comp->connected_clients = eina_list_remove(comp->connected_clients, cinfo);
             E_FREE(cinfo);
          }
     }
}

static void
_e_comp_wl_compositor_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   struct wl_resource *res;
   pid_t pid = 0;
   uid_t uid = 0;
   gid_t gid = 0;

   if (!(res =
         wl_resource_create(client, &wl_compositor_interface,
                            version, id)))
     {
        ERR("Could not create compositor resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_comp_interface, e_comp, _e_comp_wl_compositor_cb_unbind);

   wl_client_get_credentials(client, &pid, &uid, &gid);

   ELOGF("COMP",
         "BIND     |res_comp:0x%08x|client:0x%08x|%d|%d|%d",
         NULL, NULL,
         (unsigned int)res,
         (unsigned int)client,
         pid, uid, gid);

   _e_comp_wl_pname_print(pid);

   char name[512];
   _e_comp_wl_pname_get(pid, name, sizeof(name));

   E_Comp_Connected_Client_Info *cinfo;
   cinfo = E_NEW(E_Comp_Connected_Client_Info, 1);
   if (cinfo)
     {
        cinfo->name = eina_stringshare_add(name);
        cinfo->pid = pid;
        cinfo->uid = uid;
        cinfo->gid = gid;
        e_comp->connected_clients= eina_list_append(e_comp->connected_clients, cinfo);
     }
}

static void
_e_comp_wl_subsurface_destroy_sdata(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!ec || !ec->comp_data || !ec->comp_data->sub.data) return;

   sdata = ec->comp_data->sub.data;
   if (sdata->parent)
     {
        /* remove this client from parents sub list */
        sdata->parent->comp_data->sub.list =
          eina_list_remove(sdata->parent->comp_data->sub.list, ec);
        sdata->parent->comp_data->sub.list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.list_pending, ec);
        sdata->parent->comp_data->sub.below_list =
          eina_list_remove(sdata->parent->comp_data->sub.below_list, ec);
        sdata->parent->comp_data->sub.below_list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.below_list_pending, ec);
     }

   _e_comp_wl_surface_state_finish(&sdata->cached);
   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   /* the client is getting deleted, which means the pixmap will be getting
    * freed. We need to unset the surface user data */
   /* wl_resource_set_user_data(ec->comp_data->surface, NULL); */

   E_FREE(sdata);

   ec->comp_data->sub.data = NULL;
}

static void
_e_comp_wl_subsurface_destroy(struct wl_resource *resource)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (!e_object_unref(E_OBJECT(ec))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   _e_comp_wl_subsurface_destroy_sdata(ec);
}

static Eina_Bool
_e_comp_wl_subsurface_synchronized_get(E_Comp_Wl_Subsurf_Data *sdata)
{
   while (sdata)
     {
        if (sdata->synchronized) return EINA_TRUE;
        if (!sdata->parent) return EINA_FALSE;
        sdata = sdata->parent->comp_data->sub.data;
     }

   return EINA_FALSE;
}

static void
_e_comp_wl_subsurface_bg_evas_cb_resize(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data->sub.below_obj)
      evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
}

static void
_e_comp_wl_subsurface_mask_set(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   /* if a leaf client is not video cliet */
   if (!ec->comp_data->sub.below_list && !ec->comp_data->sub.below_list_pending && ec->comp_data->video_client)
     if (ec->comp_data->video_client && !e_comp_object_mask_has(ec->frame))
       {
          e_comp_object_mask_set(ec->frame, EINA_TRUE);
          return;
       }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     _e_comp_wl_subsurface_mask_set(subc);

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     _e_comp_wl_subsurface_mask_set(subc);
}

static void
_e_comp_wl_subsurface_check_below_bg_rectangle(E_Client *ec)
{
   short layer;

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (ec->comp_data->sub.below_obj) return;
   if (ec->comp_data->sub.data)
     {
         E_Client *topmost = _e_comp_wl_topmost_parent_get(ec);
         if (!topmost || e_object_is_del(E_OBJECT(topmost)) || !topmost->comp_data) return;
         if (topmost->comp_data->sub.data) return;
         if (topmost->comp_data->sub.below_obj) return;
         _e_comp_wl_subsurface_check_below_bg_rectangle(topmost);
         return;
     }
   if (!ec->comp_data->sub.below_list && !ec->comp_data->sub.below_list_pending) return;
   if (ec->argb) return;
   if (!e_comp_wl_normal_subsurface_has(ec)) return;

   ELOGF("COMP", "         |bg_rectangle", NULL, ec);

   /* create a bg rectangle if topmost window is 24 depth window */
   ec->comp_data->sub.below_obj = evas_object_rectangle_add(e_comp->evas);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->sub.below_obj);

   layer = evas_object_layer_get(ec->frame);
   evas_object_layer_set(ec->comp_data->sub.below_obj, layer);
   evas_object_render_op_set(ec->comp_data->sub.below_obj, EVAS_RENDER_COPY);

   /* It's more reasonable to use the transparent color instead of black because
    * we can show the alpha value of the 24 depth topmost window.
    */
   evas_object_color_set(ec->comp_data->sub.below_obj, 0x00, 0x00, 0x00, 0x00);
   evas_object_move(ec->comp_data->sub.below_obj, ec->x, ec->y);
   evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
   evas_object_name_set(ec->comp_data->sub.below_obj, "below_bg_rectangle");

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,
                                  _e_comp_wl_subsurface_bg_evas_cb_resize, ec);

   /* set alpha only if SW path */
   e_comp_object_alpha_set(ec->frame, EINA_TRUE);

   /* force update for changing alpha value. If the native surface has been already
    * set before, changing alpha value can't be applied to egl image.
    */
   e_comp_object_native_surface_set(ec->frame, EINA_FALSE);
   e_pixmap_image_refresh(ec->pixmap);
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   _e_comp_wl_subsurface_restack(ec);
   _e_comp_wl_subsurface_restack_bg_rectangle(ec);

   if (evas_object_visible_get(ec->frame))
     evas_object_show(ec->comp_data->sub.below_obj);

   _e_comp_wl_subsurface_mask_set(ec);
}

static void
_e_comp_wl_subsurface_commit_to_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;
   struct wl_resource *cb;
   Eina_List *l, *ll;
   Eina_Iterator *itr;
   Eina_Rectangle *rect;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit to Cache");

   /* move pending damage to cached */
   EINA_LIST_FOREACH_SAFE(cdata->pending.damages, l, ll, rect)
     eina_list_move_list(&sdata->cached.damages, &cdata->pending.damages, l);

   EINA_LIST_FOREACH_SAFE(cdata->pending.buffer_damages, l, ll, rect)
     eina_list_move_list(&sdata->cached.buffer_damages, &cdata->pending.buffer_damages, l);

   if (cdata->pending.new_attach)
     {
        sdata->cached.new_attach = EINA_TRUE;
        _e_comp_wl_surface_state_buffer_set(&sdata->cached,
                                            cdata->pending.buffer);
        e_comp_wl_buffer_reference(&sdata->cached_buffer_ref,
                                   cdata->pending.buffer);
     }

   sdata->cached.sx = cdata->pending.sx;
   sdata->cached.sy = cdata->pending.sy;
   /* sdata->cached.buffer = cdata->pending.buffer; */

   /* When subsurface is sync mode, the commit of subsurface can happen before
    * a parent surface is committed. In this case, we can't show a attached
    * buffer on screen.
    */
   //sdata->cached.new_attach = cdata->pending.new_attach;

   sdata->cached.buffer_viewport.changed |= cdata->pending.buffer_viewport.changed;
   sdata->cached.buffer_viewport.buffer =cdata->pending.buffer_viewport.buffer;
   sdata->cached.buffer_viewport.surface = cdata->pending.buffer_viewport.surface;

   _e_comp_wl_surface_state_buffer_set(&cdata->pending, NULL);
   cdata->pending.sx = 0;
   cdata->pending.sy = 0;
   cdata->pending.new_attach = EINA_FALSE;
   cdata->pending.buffer_viewport.changed = 0;

   /* copy cdata->pending.opaque into sdata->cached.opaque */
   itr = eina_tiler_iterator_new(cdata->pending.opaque);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.opaque, rect);
   eina_iterator_free(itr);

   /* repeat for input */
   itr = eina_tiler_iterator_new(cdata->pending.input);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.input, rect);
   eina_iterator_free(itr);

   EINA_LIST_FOREACH_SAFE(cdata->pending.frames, l, ll, cb)
     {
        if (cb)
          eina_list_move_list(&sdata->cached.frames,
                              &cdata->pending.frames,
                              l);
     }

   sdata->cached.has_data = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_commit_from_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit from Cache");

   _e_comp_wl_surface_state_commit(ec, &sdata->cached);

   if (!e_comp_object_damage_exists(ec->frame))
     e_pixmap_image_clear(ec->pixmap, 1);

   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   if (_e_comp_wl_surface_subsurface_order_commit(ec))
     {
        E_Client *topmost = _e_comp_wl_topmost_parent_get(ec);
        _e_comp_wl_subsurface_restack(topmost);
        _e_comp_wl_subsurface_restack_bg_rectangle(topmost);
     }
}

static void
_e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized)
{
   E_Client *parent;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;
   if (!(parent = sdata->parent)) return;

   if (sdata->position.set)
     {
        evas_object_move(ec->frame, parent->x + sdata->position.x,
                         parent->y + sdata->position.y);
        sdata->position.set = EINA_FALSE;
     }

   if ((parent_synchronized) || (sdata->synchronized))
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->cached.has_data)
          _e_comp_wl_subsurface_commit_from_cache(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
     }
}

static void
_e_comp_wl_subsurface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subsurface_cb_position_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Position Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->position.x = x;
   sdata->position.y = y;
   sdata->position.set = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_above(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Above: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;
   if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_append_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_below(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Below: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;
   if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_prepend_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_sync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Sync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_desync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Desync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_FALSE;
}

static const struct wl_subsurface_interface _e_subsurface_interface =
{
   _e_comp_wl_subsurface_cb_destroy,
   _e_comp_wl_subsurface_cb_position_set,
   _e_comp_wl_subsurface_cb_place_above,
   _e_comp_wl_subsurface_cb_place_below,
   _e_comp_wl_subsurface_cb_sync_set,
   _e_comp_wl_subsurface_cb_desync_set
};

EAPI Eina_Bool
e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource)
{
   struct wl_client *client;
   struct wl_resource *res;
   E_Comp_Wl_Subsurf_Data *sdata;
   E_Client *offscreen_parent = NULL;

   /* try to get the wayland client from the surface resource */
   if (!(client = wl_resource_get_client(surface_resource)))
     {
        ERR("Could not get client from resource %d",
            wl_resource_get_id(surface_resource));
        return EINA_FALSE;
     }

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return EINA_FALSE;

   if (!epc || e_object_is_del(E_OBJECT(epc)))
     {
        if (ec->comp_data->sub.watcher)
          tizen_subsurface_watcher_send_message(ec->comp_data->sub.watcher, TIZEN_SUBSURFACE_WATCHER_MSG_PARENT_ID_INVALID);

        /* We have to create a subsurface resource here even though it's error case
         * because server will send the fatal error when a client destroy a subsurface object.
         * Otherwise, server will kill a client by the fatal error.
         */
        if (!(res = wl_resource_create(client, &wl_subsurface_interface, 1, id)))
          {
             ERR("Failed to create subsurface resource");
             wl_resource_post_no_memory(surface_resource);
             return EINA_FALSE;
          }

        wl_resource_set_implementation(res, &_e_subsurface_interface, NULL, NULL);

        ERR("tizen_policy failed: invalid parent");
        return EINA_FALSE;
     }

   // reparent remote surface provider's subsurfaces
   if (epc->comp_data->remote_surface.onscreen_parent)
     {
        offscreen_parent = epc;
        epc = epc->comp_data->remote_surface.onscreen_parent;
     }

   // check parent relationship is a cycle
     {
        E_Client *parent = epc;

        while(parent)
          {
             if (ec == parent)
               {
                  ERR("Subsurface parent relationship is a cycle : [child win : %x, %s], [parent win : %x, %s]",
                      e_client_util_win_get(ec), e_client_util_name_get(ec),
                      e_client_util_win_get(epc), e_client_util_name_get(epc));

                  return EINA_FALSE;
               }

             if (parent->comp_data->sub.data)
                parent = parent->comp_data->sub.data->parent;
             else
                break;
          }
     }

   /* try to allocate subsurface data */
   if (!(sdata = E_NEW(E_Comp_Wl_Subsurf_Data, 1)))
     {
        ERR("Could not allocate space for subsurface data");
        goto dat_err;
     }

   /* try to create the subsurface resource */
   if (!(res = wl_resource_create(client, &wl_subsurface_interface, 1, id)))
     {
        ERR("Failed to create subsurface resource");
        wl_resource_post_no_memory(surface_resource);
        goto res_err;
     }

   /* set resource implementation */
   wl_resource_set_implementation(res, &_e_subsurface_interface, ec,
                                  _e_comp_wl_subsurface_destroy);
   e_object_ref(E_OBJECT(ec));

   _e_comp_wl_surface_state_init(&sdata->cached, ec->w, ec->h);

   /* set subsurface data properties */
   sdata->cached_buffer_ref.buffer = NULL;
   sdata->resource = res;
   sdata->synchronized = EINA_TRUE;
   sdata->parent = epc;
   sdata->remote_surface.offscreen_parent = offscreen_parent;

   /* set subsurface client properties */
   ec->borderless = EINA_TRUE;
   ec->argb = EINA_TRUE;
   ec->lock_border = EINA_TRUE;
   ec->lock_focus_in = ec->lock_focus_out = EINA_TRUE;
   ec->netwm.state.skip_taskbar = EINA_TRUE;
   ec->netwm.state.skip_pager = EINA_TRUE;
   ec->no_shape_cut = EINA_TRUE;
   ec->border_size = 0;

   ELOGF("COMP", "         |subsurface_parent:%p", NULL, ec, epc);
   if (offscreen_parent)
     ELOGF("COMP", "         |offscreen_parent:%p", NULL, ec, offscreen_parent);

   if (epc)
     {
        if (epc->frame)
          {
            short layer = evas_object_layer_get(epc->frame);
            evas_object_layer_set(ec->frame, layer);
          }

        if (epc->comp_data)
          {
             /* append this client to the parents subsurface list */
             epc->comp_data->sub.list =
               eina_list_append(epc->comp_data->sub.list, ec);
             epc->comp_data->sub.list_changed = EINA_TRUE;
          }

        /* TODO: add callbacks ?? */
     }

   ec->comp_data->surface = surface_resource;
   ec->comp_data->sub.data = sdata;

   ec->lock_user_location = 0;
   ec->lock_client_location = 0;
   ec->lock_user_size = 0;
   ec->lock_client_size = 0;
   ec->lock_client_stacking = 0;
   ec->lock_user_shade = 0;
   ec->lock_client_shade = 0;
   ec->lock_user_maximize = 0;
   ec->lock_client_maximize = 0;
   ec->changes.need_maximize = 0;
   ec->maximized = E_MAXIMIZE_NONE;
   EC_CHANGED(ec);

   ec->new_client = ec->netwm.ping = EINA_TRUE;
   e_comp->new_clients++;
   e_client_unignore(ec);

   _e_comp_wl_hook_call(E_COMP_WL_HOOK_SUBSURFACE_CREATE, ec);
   return EINA_TRUE;

res_err:
   free(sdata);
dat_err:
   return EINA_FALSE;
}

static void
_e_comp_wl_surface_sub_list_free(Eina_List *sub_list)
{
   E_Client *subc;

   EINA_LIST_FREE(sub_list, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        subc->comp_data->sub.data->parent = NULL;

        if (subc->comp_data->sub.watcher)
          tizen_subsurface_watcher_send_message(subc->comp_data->sub.watcher, TIZEN_SUBSURFACE_WATCHER_MSG_PARENT_ID_DESTROYED);
     }
}

static void
_e_comp_wl_subcompositor_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subcompositor_cb_subsurface_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   E_Client *ec, *epc = NULL;
   static const char where[] = "get_subsurface: wl_subsurface@";

   if (!(ec = wl_resource_get_user_data(surface_resource))) return;
   if (!(epc = wl_resource_get_user_data(parent_resource))) return;

   if (ec == epc)
     {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d cannot be its own parent",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_object_is_del(E_OBJECT(epc))) return;

   /* check if this surface is already a sub-surface */
   if ((ec->comp_data) && (ec->comp_data->sub.data))
     {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d is already a sub-surface",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   /* try to create a new subsurface */
   if (!e_comp_wl_subsurface_create(ec, epc, id, surface_resource))
     ERR("Failed to create subsurface for surface %d",
         wl_resource_get_id(surface_resource));
}

static const struct wl_subcompositor_interface _e_subcomp_interface =
{
   _e_comp_wl_subcompositor_cb_destroy,
   _e_comp_wl_subcompositor_cb_subsurface_get
};

static void
_e_comp_wl_subcompositor_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res =
         wl_resource_create(client, &wl_subcompositor_interface,
                            version, id)))
     {
        ERR("Could not create subcompositor resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_subcomp_interface, e_comp, NULL);

   /* TODO: add handlers for client iconify/uniconify */
}

static void
_e_comp_wl_sr_cb_provide_uuid(struct wl_client *client EINA_UNUSED, struct wl_resource *resource EINA_UNUSED, const char *uuid)
{
   DBG("Provide UUID callback called for UUID: %s", uuid);
}

static const struct session_recovery_interface _e_session_recovery_interface =
{
   _e_comp_wl_sr_cb_provide_uuid,
};

static void
_e_comp_wl_session_recovery_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version EINA_UNUSED, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &session_recovery_interface, 1, id)))
     {
        ERR("Could not create session_recovery interface");
        wl_client_post_no_memory(client);
        return;
     }

   /* set implementation on resource */
   wl_resource_set_implementation(res, &_e_session_recovery_interface, e_comp, NULL);
}

static void
_e_comp_wl_screenshooter_cb_shoot(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *output_resource, struct wl_resource *buffer_resource)
{
   E_Comp_Wl_Output *output;
   E_Comp_Wl_Buffer *buffer;
   struct wl_shm_buffer *shm_buffer;
   int stride;
   void *pixels = NULL, *d;
   Eina_Bool res;

   output = wl_resource_get_user_data(output_resource);
   buffer = e_comp_wl_buffer_get(buffer_resource, NULL);

   if (!output) return;

   if (!buffer)
     {
        wl_resource_post_no_memory(resource);
        return;
     }

   EINA_SAFETY_ON_NULL_RETURN(e_comp_wl->screenshooter.read_pixels);

   if ((buffer->w < output->w) || (buffer->h < output->h))
     {
        ERR("Buffer size less than output");
        /* send done with bad buffer error */
        return;
     }

   stride = buffer->w * sizeof(int);

   pixels = malloc(stride * buffer->h);
   if (!pixels)
     {
        /* send done with bad buffer error */
        ERR("Could not allocate space for destination");
        return;
     }

   res = e_comp_wl->screenshooter.read_pixels(output, pixels);
   if (!res)
     {
        free(pixels);
        return;
     }

   shm_buffer = wl_shm_buffer_get(buffer->resource);
   if (!shm_buffer)
     {
        ERR("Could not get shm_buffer from resource");
        free(pixels);
        return;
     }

   stride = wl_shm_buffer_get_stride(shm_buffer);
   d = wl_shm_buffer_get_data(shm_buffer);
   if (!d)
     {
        ERR("Could not get buffer data");
        free(pixels);
        return;
     }

   wl_shm_buffer_begin_access(shm_buffer);
   memcpy(d, pixels, buffer->h * stride);
   wl_shm_buffer_end_access(shm_buffer);

   screenshooter_send_done(resource);

   free(pixels);
}

static const struct screenshooter_interface _e_screenshooter_interface =
{
   _e_comp_wl_screenshooter_cb_shoot
};

static void
_e_comp_wl_screenshooter_cb_bind(struct wl_client *client, void *data, uint32_t version EINA_UNUSED, uint32_t id)
{
   struct wl_resource *res;

   res = wl_resource_create(client, &screenshooter_interface, 1, id);
   if (!res)
     {
        ERR("Could not create screenshooter resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_screenshooter_interface, data, NULL);
}

static void
_e_comp_wl_client_cb_new(void *data EINA_UNUSED, E_Client *ec)
{
   Ecore_Window win;

   /* make sure this is a wayland client */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   TRACE_DS_BEGIN(COMP_WL:CLIENT NEW HOOK);

   /* get window id from pixmap */
   win = e_pixmap_window_get(ec->pixmap);

   /* ignore fake root windows */
   if ((ec->override) && ((ec->x == -77) && (ec->y == -77)))
     {
        e_comp_ignore_win_add(E_PIXMAP_TYPE_WL, win);
        e_object_del(E_OBJECT(ec));
        TRACE_DS_END();
        return;
     }

   if (!(ec->comp_data = E_NEW(E_Comp_Client_Data, 1)))
     {
        ERR("Could not allocate new client data structure");
        TRACE_DS_END();
        return;
     }

   wl_signal_init(&ec->comp_data->destroy_signal);
   wl_signal_init(&ec->comp_data->apply_viewport_signal);

   _e_comp_wl_surface_state_init(&ec->comp_data->pending, ec->w, ec->h);

   /* set initial client properties */
   ec->argb = EINA_FALSE;
   ec->no_shape_cut = EINA_TRUE;
   ec->redirected = ec->ignored = 1;
   ec->border_size = 0;

   /* NB: could not find a better place to do this, BUT for internal windows,
    * we need to set delete_request else the close buttons on the frames do
    * basically nothing */
   if ((ec->internal) || (ec->internal_elm_win))
     ec->icccm.delete_request = EINA_TRUE;

   /* set initial client data properties */
   ec->comp_data->mapped = EINA_FALSE;
   ec->comp_data->first_damage = ec->internal;

   ec->comp_data->need_reparent = !ec->internal;

   ec->comp_data->scaler.buffer_viewport.buffer.transform = WL_OUTPUT_TRANSFORM_NORMAL;
   ec->comp_data->scaler.buffer_viewport.buffer.scale = 1;
   ec->comp_data->scaler.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   ec->comp_data->scaler.buffer_viewport.surface.width = -1;

   E_Comp_Client_Data *p_cdata = e_pixmap_cdata_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN(p_cdata);
   ec->comp_data->accepts_focus = p_cdata->accepts_focus;
   ec->comp_data->conformant = p_cdata->conformant;
   ec->comp_data->aux_hint = p_cdata->aux_hint;
   ec->comp_data->win_type = p_cdata->win_type;
   ec->comp_data->layer = p_cdata->layer;
   ec->comp_data->fetch.win_type = p_cdata->fetch.win_type;
   ec->comp_data->fetch.layer = p_cdata->fetch.layer;
   ec->comp_data->video_client = p_cdata->video_client;

   e_pixmap_cdata_set(ec->pixmap, ec->comp_data);

   TRACE_DS_END();
}

static void
_e_comp_wl_client_cb_del(void *data EINA_UNUSED, E_Client *ec)
{
   /* Eina_Rectangle *dmg; */
   struct wl_resource *cb;

   /* make sure this is a wayland client */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   TRACE_DS_BEGIN(COMP_WL:CLIENT DEL CB);

   if ((!ec->already_unparented) && (ec->comp_data->reparented))
     _e_comp_wl_focus_down_set(ec);

   ec->already_unparented = EINA_TRUE;
   if (ec->comp_data->reparented)
     {
        /* reset pixmap parent window */
        e_pixmap_parent_window_set(ec->pixmap, 0);
     }

   if (ec->comp_data->sub.data)
     _e_comp_wl_subsurface_destroy_sdata(ec);

   if (ec->comp_data->sub.below_obj)
     evas_object_del(ec->comp_data->sub.below_obj);

   /* remove sub list */
   /* TODO: if parent is set by onscreen_parent of remote surface? */
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.list);
   ec->comp_data->sub.list = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.list_pending);
   ec->comp_data->sub.list_pending = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.below_list);
   ec->comp_data->sub.below_list = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.below_list_pending);
   ec->comp_data->sub.below_list_pending = NULL;

   if (ec->comp_data->sub.watcher)
     wl_resource_destroy(ec->comp_data->sub.watcher);

   if ((ec->parent) && (ec->parent->modal == ec))
     {
        ec->parent->lock_close = EINA_FALSE;
        ec->parent->modal = NULL;
     }

   wl_signal_emit(&ec->comp_data->destroy_signal, &ec->comp_data->surface);

   _e_comp_wl_surface_state_finish(&ec->comp_data->pending);

   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, NULL);

   EINA_LIST_FREE(ec->comp_data->frames, cb)
     wl_resource_destroy(cb);

   EINA_LIST_FREE(ec->comp_data->pending.frames, cb)
     wl_resource_destroy(cb);

   if (ec->comp_data->surface)
     wl_resource_set_user_data(ec->comp_data->surface, NULL);

   if (ec->internal_elm_win)
     _e_comp_wl_surface_render_stop(ec);
   _e_comp_wl_focus_check();

   if (ec->comp_data->aux_hint.hints)
     {
        E_Comp_Wl_Aux_Hint *hint;
        EINA_LIST_FREE(ec->comp_data->aux_hint.hints, hint)
          {
             eina_stringshare_del(hint->hint);
             eina_stringshare_del(hint->val);
             E_FREE(hint);
          }
     }

   if (cursor_timer_ec == ec)
     {
        E_FREE_FUNC(e_comp_wl->ptr.hide_tmr, ecore_timer_del);
        cursor_timer_ec = NULL;
     }

   if (e_comp_wl->selection.cbhm == ec->comp_data->surface)
     e_comp_wl->selection.cbhm = NULL;

   if (ec->comp_data->viewport_transform)
     {
        e_client_transform_core_remove(ec, ec->comp_data->viewport_transform);
        e_util_transform_del(ec->comp_data->viewport_transform);
        ec->comp_data->viewport_transform = NULL;
     }

   e_pixmap_cdata_set(ec->pixmap, NULL);

   E_FREE(ec->comp_data);

   _e_comp_wl_focus_check();

   TRACE_DS_END();
}

static void
_e_comp_wl_client_cb_focus_set(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* send configure */
   if (ec->comp_data->shell.configure_send)
     {
        if (ec->comp_data->shell.surface)
          _e_comp_wl_configure_send(ec, 0, 0);
     }

   e_comp_wl->kbd.focus = ec->comp_data->surface;
}

static void
_e_comp_wl_client_cb_focus_unset(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* send configure */
   if (ec->comp_data->shell.configure_send)
     {
        if (ec->comp_data->shell.surface)
          _e_comp_wl_configure_send(ec, 0, 0);
     }

   _e_comp_wl_focus_check();

   if (e_comp_wl->kbd.focus == ec->comp_data->surface)
     e_comp_wl->kbd.focus = NULL;
}

static void
_e_comp_wl_client_cb_resize_begin(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;
   if (ec->keyboard_resizing) return;

   /* do nothing currently */
   ;
}

static void
_e_comp_wl_client_cb_resize_end(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   e_comp_wl->resize.edges = 0;
   e_comp_wl->resize.resource = NULL;

   if (ec->pending_resize)
     {
        ec->changes.pos = EINA_TRUE;
        ec->changes.size = EINA_TRUE;
        EC_CHANGED(ec);
     }

   E_FREE_LIST(ec->pending_resize, free);
}

static void
_e_comp_wl_client_cb_move_end(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;
}

static void
_e_comp_wl_client_cb_iconify(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* DON'T iconify subsurface. When iconfied, buffer will be released. */
   _e_comp_wl_subsurface_hide(ec);
}

static void
_e_comp_wl_client_cb_uniconify(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   _e_comp_wl_subsurface_show(ec);
}

static E_Client *
_e_comp_wl_client_usable_get(pid_t pid, E_Pixmap *ep)
{
   /* NOTE: this will return usable E_Client for a surface of specified process
    * by pid. it doesn't care whatever this surfaces is for but care only what
    * is owner process of the surface.
    */

   E_Client *ec = NULL, *_ec = NULL;
   Eina_List *l;

   /* find launchscreen client list */
   if (e_comp->launchscrns)
     {
        EINA_LIST_FOREACH(e_comp->launchscrns, l, _ec)
          {
             if (_ec->netwm.pid == pid)
               {
                  ec = _ec;
                  break;
               }
          }
        if (ec)
          {
             E_Pixmap *oldep = NULL;

             if (ec->comp_data)
               {
                  /* do NOT replace with the client having comp data */
                  return NULL;
               }

             e_comp->launchscrns = eina_list_remove(e_comp->launchscrns, ec);

             oldep = e_client_pixmap_change(ec, ep);
             if (oldep)
               {
                  e_pixmap_win_id_del(oldep);
                  e_pixmap_free(oldep);
               }

             if (ec->internal)
               ec->internal = 0;

             /* to set-up comp data */
             _e_comp_wl_client_cb_new(NULL, ec);
             ec->ignored = 0;
             if (!ec->comp_data) return NULL;
             _e_comp_wl_client_evas_init(ec);
          }
     }

   return ec;
}

static void
_e_comp_wl_cb_output_unbind(struct wl_resource *resource)
{
   E_Comp_Wl_Output *output;

   if (!(output = wl_resource_get_user_data(resource))) return;

   output->resources = eina_list_remove(output->resources, resource);
}

static void
_e_comp_wl_cb_output_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   E_Comp_Wl_Output *output;
   struct wl_resource *resource;

   if (!(output = data)) return;

   resource =
     wl_resource_create(client, &wl_output_interface, version, id);
   if (!resource)
     {
        wl_client_post_no_memory(client);
        return;
     }

   DBG("Bound Output: %s", output->id);
   DBG("\tGeom: %d %d %d %d", output->x, output->y, output->w, output->h);

   output->resources = eina_list_append(output->resources, resource);

   wl_resource_set_implementation(resource, NULL, output,
                                  _e_comp_wl_cb_output_unbind);
   wl_resource_set_user_data(resource, output);

   wl_output_send_geometry(resource, output->x, output->y,
                           output->phys_width, output->phys_height,
                           output->subpixel, output->make ?: "",
                           output->model ?: "", output->transform);

   if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
     wl_output_send_scale(resource, output->scale);

   /* 3 == preferred + current */
   wl_output_send_mode(resource, 3, output->w, output->h, output->refresh);

   if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
     wl_output_send_done(resource);
}

static void
_e_comp_wl_gl_init(void *data EINA_UNUSED)
{
   Evas *evas = NULL;
   Evas_GL *evasgl = NULL;
   Evas_GL_API *glapi = NULL;
   Evas_GL_Context *ctx = NULL;
   Evas_GL_Surface *sfc = NULL;
   Evas_GL_Config *cfg = NULL;
   Eina_Bool res;
   const char *name;

   if (!e_comp_gl_get()) return;

   name = ecore_evas_engine_name_get(e_comp->ee);
   if (!name) return;
   if (strcmp(name, "gl_drm")) return;

   /* create dummy evas gl to bind wayland display of enlightenment to egl display */
   e_main_ts("\tE_Comp_Wl_GL Init");

   /* if wl_drm module doesn't call e_comp_canvas_init yet,
    * then we should get evas from ecore_evas.
    */
   if (e_comp->evas)
     evas = e_comp->evas;
   else
     evas = ecore_evas_get(e_comp->ee);

   evasgl = evas_gl_new(evas);
   EINA_SAFETY_ON_NULL_GOTO(evasgl, err);

   glapi = evas_gl_api_get(evasgl);
   EINA_SAFETY_ON_NULL_GOTO(glapi, err);
   EINA_SAFETY_ON_NULL_GOTO(glapi->evasglBindWaylandDisplay, err);

   cfg = evas_gl_config_new();
   EINA_SAFETY_ON_NULL_GOTO(cfg, err);

   sfc = evas_gl_surface_create(evasgl, cfg, 1, 1);
   EINA_SAFETY_ON_NULL_GOTO(sfc, err);

   ctx = evas_gl_context_create(evasgl, NULL);
   EINA_SAFETY_ON_NULL_GOTO(ctx, err);

   res = evas_gl_make_current(evasgl, sfc, ctx);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   res = glapi->evasglBindWaylandDisplay(evasgl, e_comp_wl->wl.disp);
   EINA_SAFETY_ON_FALSE_GOTO(res, err);

   evas_gl_config_free(cfg);

   e_comp_wl->wl.gl = evasgl;
   e_comp_wl->wl.glapi = glapi;
   e_comp_wl->wl.glsfc = sfc;
   e_comp_wl->wl.glctx = ctx;

   /* for native surface */
   e_comp->gl = 1;

   e_main_ts("\tE_Comp_Wl_GL Init Done");

   return;

err:
   evas_gl_config_free(cfg);
   evas_gl_make_current(evasgl, NULL, NULL);
   evas_gl_context_destroy(evasgl, ctx);
   evas_gl_surface_destroy(evasgl, sfc);
   evas_gl_free(evasgl);
}

// FIXME
#if 0
static void
_e_comp_wl_gl_popup_cb_close(void *data,
                             Evas_Object *obj EINA_UNUSED,
                             void *event_info EINA_UNUSED)
{
   evas_object_del(data);
}

static void
_e_comp_wl_gl_popup_cb_focus(void *data,
                             Evas_Object *obj EINA_UNUSED,
                             void *event_info EINA_UNUSED)
{
   elm_object_focus_set(data, EINA_TRUE);
}
#endif

static Eina_Bool
_e_comp_wl_gl_idle(void *data)
{
   if (!e_comp->gl)
     {
        /* show warning window to notify failure of gl init */
        // TODO: yigl
#if 0
        Evas_Object *win, *bg, *popup, *btn;

        win = elm_win_add(NULL, "compositor warning", ELM_WIN_BASIC);
        elm_win_title_set(win, "Compositor Warning");
        elm_win_autodel_set(win, EINA_TRUE);
        elm_win_borderless_set(win, EINA_TRUE);
        elm_win_role_set(win, "notification-low");
        elm_win_alpha_set(win, EINA_TRUE);

        bg = evas_object_rectangle_add(evas_object_evas_get(win));
        evas_object_size_hint_weight_set(bg, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        elm_win_resize_object_add(win, bg);
        evas_object_color_set(bg, 125, 125, 125, 125);
        evas_object_show(bg);

        popup = elm_popup_add(win);
        elm_object_text_set(popup,
                            _( "Your screen does not support OpenGL.<br>"
                               "Falling back to software engine."));
        elm_object_part_text_set(popup, "title,text", "Compositor Warning");

        btn = elm_button_add(popup);
        elm_object_text_set(btn, "Close");
        elm_object_part_content_set(popup, "button1", btn);
        evas_object_show(btn);

        evas_object_smart_callback_add(win, "focus,in", _e_comp_wl_gl_popup_cb_focus, popup);
        evas_object_smart_callback_add(btn, "unpressed", _e_comp_wl_gl_popup_cb_close, win);

        evas_object_show(popup);
        evas_object_show(win);
#endif
     }

   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_comp_wl_compositor_create(void)
{
   E_Comp_Wl_Data *cdata;
   const char *name;
   int fd = 0;
   E_Module *_mod;
   Eina_Bool res;

   /* create new compositor data */
   if (!(cdata = E_NEW(E_Comp_Wl_Data, 1)))
     {
       ERR("Could not create compositor data: %m");
       return EINA_FALSE;
     }

   /* set compositor wayland data */
   e_comp_wl = e_comp->wl_comp_data = cdata;

   /* try to create a wayland display */
   if (!(cdata->wl.disp = wl_display_create()))
     {
        ERR("Could not create a Wayland display: %m");
        goto disp_err;
     }

   /* try to setup wayland socket */
   if (!(name = wl_display_add_socket_auto(cdata->wl.disp)))
     {
        ERR("Could not create Wayland display socket: %m");
        PRCTL("[Winsys] Could not create Wayland display socket: /run/wayland-0");
        goto sock_err;
     }

   res = e_comp_socket_init(name);
   EINA_SAFETY_ON_FALSE_GOTO(res, sock_err);
   PRCTL("[Winsys] change permission and create sym link for %s", name);

   /* set wayland display environment variable */
   e_env_set("WAYLAND_DISPLAY", name);

   /* initialize compositor signals */
   wl_signal_init(&cdata->signals.surface.create);
   wl_signal_init(&cdata->signals.surface.activate);
   wl_signal_init(&cdata->signals.surface.kill);

   /* cdata->output.transform = WL_OUTPUT_TRANSFORM_NORMAL; */
   /* cdata->output.scale = e_scale; */

   /* try to add compositor to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &wl_compositor_interface,
                         COMPOSITOR_VERSION, e_comp,
                         _e_comp_wl_compositor_cb_bind))
     {
        ERR("Could not add compositor to wayland globals: %m");
        goto comp_global_err;
     }

   /* try to add subcompositor to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &wl_subcompositor_interface, 1,
                         e_comp, _e_comp_wl_subcompositor_cb_bind))
     {
        ERR("Could not add subcompositor to wayland globals: %m");
        goto comp_global_err;
     }

   /* try to add session_recovery to wayland globals */
   if (!wl_global_create(cdata->wl.disp, &session_recovery_interface, 1,
                         e_comp, _e_comp_wl_session_recovery_cb_bind))
     {
        ERR("Could not add session_recovery to wayland globals: %m");
        goto comp_global_err;
     }

   /* initialize shm mechanism */
   wl_display_init_shm(cdata->wl.disp);

   cdata->screenshooter.global =
     wl_global_create(cdata->wl.disp, &screenshooter_interface, 1,
                      e_comp, _e_comp_wl_screenshooter_cb_bind);
   if (!cdata->screenshooter.global)
     {
        ERR("Could not create screenshooter global: %m");
        goto comp_global_err;
     }

   /* _e_comp_wl_cb_randr_change(NULL, 0, NULL); */

   /* try to init data manager */
   if (!e_comp_wl_data_manager_init())
     {
        ERR("Could not initialize data manager");
        goto data_err;
     }

   /* try to init input */
   if (!e_comp_wl_input_init())
     {
        ERR("Could not initialize input");
        goto input_err;
     }

   if (e_comp_gl_get())
     _e_comp_wl_gl_init(NULL);

   /* get the wayland display loop */
   cdata->wl.loop = wl_display_get_event_loop(cdata->wl.disp);

   /* get the file descriptor of the wayland event loop */
   fd = wl_event_loop_get_fd(cdata->wl.loop);

   /* create a listener for wayland main loop events */
   cdata->fd_hdlr =
     ecore_main_fd_handler_add(fd, (ECORE_FD_READ | ECORE_FD_ERROR),
                               _e_comp_wl_cb_read, cdata, NULL, NULL);
   ecore_main_fd_handler_prepare_callback_set(cdata->fd_hdlr,
                                              _e_comp_wl_cb_prepare, cdata);

   /* load shell module */
   _mod = e_module_new("wl_desktop_shell");
   EINA_SAFETY_ON_NULL_GOTO(_mod, input_err);

   if (!e_module_enable(_mod))
     {
        ERR("Fail to enable wl_desktop_shell module");
        goto input_err;
     }

   return EINA_TRUE;

input_err:
   e_comp_wl_data_manager_shutdown();
data_err:
comp_global_err:
   e_env_unset("WAYLAND_DISPLAY");
sock_err:
   wl_display_destroy(cdata->wl.disp);
disp_err:
   free(cdata);
   return EINA_FALSE;
}

static void
_e_comp_wl_gl_shutdown(void)
{
   if (!e_comp_wl->wl.gl) return;

   e_comp_wl->wl.glapi->evasglUnbindWaylandDisplay(e_comp_wl->wl.gl, e_comp_wl->wl.disp);

   evas_gl_make_current(e_comp_wl->wl.gl, NULL, NULL);
   evas_gl_context_destroy(e_comp_wl->wl.gl, e_comp_wl->wl.glctx);
   evas_gl_surface_destroy(e_comp_wl->wl.gl, e_comp_wl->wl.glsfc);
   evas_gl_free(e_comp_wl->wl.gl);

   e_comp_wl->wl.glsfc = NULL;
   e_comp_wl->wl.glctx = NULL;
   e_comp_wl->wl.glapi = NULL;
   e_comp_wl->wl.gl = NULL;
}

static void
_e_comp_wl_client_cb_destroy(struct wl_listener *l, void *data)
{
   struct wl_client *client = (struct wl_client *)data;

   eina_hash_del_by_key(_last_keydev_hash, client);

   wl_list_remove(&l->link);
   E_FREE(l);
}

static void
_e_comp_wl_client_destroy_listener_add(struct wl_client *client)
{
   struct wl_listener *destroy_listener;

   EINA_SAFETY_ON_NULL_RETURN(client);
   destroy_listener = wl_client_get_destroy_listener(client, _e_comp_wl_client_cb_destroy);
   if (destroy_listener) return;

   destroy_listener = E_NEW(struct wl_listener, 1);
   EINA_SAFETY_ON_NULL_RETURN(destroy_listener);

   destroy_listener->notify = _e_comp_wl_client_cb_destroy;
   wl_client_add_destroy_listener(client, destroy_listener);
}

static Eina_Bool
_e_comp_wl_keydev_hash_free(const Eina_Hash *hash, const void *key, void *data, void *fdata)
{
   struct wl_listener *destroy_listener;
   struct wl_client *wc;

   wc = (struct wl_client *)key;

   destroy_listener = wl_client_get_destroy_listener(wc, _e_comp_wl_client_cb_destroy);

   wl_list_remove(&destroy_listener->link);
   E_FREE(destroy_listener);

   return EINA_TRUE;
}

/* public functions */

/**
 * Creates and initializes a Wayland compositor with ecore.
 * Registers callback handlers for keyboard and mouse activity
 * and other client events.
 *
 * @returns true on success, false if initialization failed.
 */
E_API Eina_Bool
e_comp_wl_init(void)
{
   TRACE_DS_BEGIN(COMP_WL:INIT);

   /* try to create a wayland compositor */
   if (!_e_comp_wl_compositor_create())
     {
        e_error_message_show(_("Enlightenment cannot create a Wayland Compositor!\n"));
        TRACE_DS_END();
        return EINA_FALSE;
     }

#ifdef HAVE_WAYLAND_TBM
   e_comp_wl_tbm_init();
#endif
   e_comp_wl_remote_surface_init();

   e_pixmap_init();

   /* add event handlers to catch E events */
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_SCREEN_CHANGE,            _e_comp_wl_cb_randr_change,        NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_COMP_OBJECT_ADD,         _e_comp_wl_cb_comp_object_add,     NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_MOVE,          _e_comp_wl_cb_mouse_move,          NULL);
   E_LIST_HANDLER_APPEND(handlers, ECORE_EVENT_MOUSE_BUTTON_CANCEL, _e_comp_wl_cb_mouse_button_cancel, NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DISPLAY_STATE_CHANGE, _e_comp_wl_cb_zone_display_state_change, NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_ROTATION_CHANGE_END, _e_comp_wl_cb_client_rot_change_end, NULL);

   /* add hooks to catch e_client events */
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_NEW_CLIENT,   _e_comp_wl_client_cb_new,          NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_DEL,          _e_comp_wl_client_cb_del,          NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_FOCUS_SET,    _e_comp_wl_client_cb_focus_set,    NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_FOCUS_UNSET,  _e_comp_wl_client_cb_focus_unset,  NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_RESIZE_BEGIN, _e_comp_wl_client_cb_resize_begin, NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_RESIZE_END,   _e_comp_wl_client_cb_resize_end,   NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_MOVE_END,     _e_comp_wl_client_cb_move_end,     NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_ICONIFY,      _e_comp_wl_client_cb_iconify,      NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_UNICONIFY,    _e_comp_wl_client_cb_uniconify,    NULL);

   E_EVENT_WAYLAND_GLOBAL_ADD = ecore_event_type_new();
   _last_keydev_hash = eina_hash_pointer_new(NULL);

   TRACE_DS_END();
   return EINA_TRUE;
}

E_API void
e_comp_wl_deferred_job(void)
{
   ecore_idle_enterer_add(_e_comp_wl_gl_idle, NULL);
}

/**
 * Get the signal that is fired for the creation of a Wayland surface.
 *
 * @returns the corresponding Wayland signal
 */
E_API struct wl_signal
e_comp_wl_surface_create_signal_get(void)
{
   return e_comp_wl->signals.surface.create;
}

/* internal functions */
EINTERN void
e_comp_wl_shutdown(void)
{
   /* free handlers */
   E_FREE_LIST(handlers, ecore_event_handler_del);
   E_FREE_LIST(hooks, e_client_hook_del);
   _e_comp_wl_gl_shutdown();

#ifdef HAVE_WAYLAND_TBM
   e_comp_wl_tbm_shutdown();
#endif
   e_comp_wl_remote_surface_shutdown();

   eina_hash_foreach(_last_keydev_hash, _e_comp_wl_keydev_hash_free, NULL);
   eina_hash_free(_last_keydev_hash);

   e_pixmap_shutdown();

   e_comp_wl_input_shutdown();

   // TODO: yigl
#if 0
   E_Comp_Wl_Output *output;

   if (e_comp_wl->screenshooter.global)
     wl_global_destroy(e_comp_wl->screenshooter.global);

   EINA_LIST_FREE(e_comp_wl->outputs, output)
     {
        if (output->id) eina_stringshare_del(output->id);
        if (output->make) eina_stringshare_del(output->make);
        if (output->model) eina_stringshare_del(output->model);
        free(output);
     }

   /* delete fd handler */
   if (e_comp_wl->fd_hdlr) ecore_main_fd_handler_del(e_comp_wl->fd_hdlr);

   E_FREE_FUNC(e_comp_wl->ptr.hide_tmr, ecore_timer_del);
   cursor_timer_ec = NULL;

   /* free allocated data structure */
   free(e_comp_wl);
#endif
}

static void
e_comp_wl_surface_event_simple_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

EINTERN void
e_comp_wl_surface_attach(E_Client *ec, E_Comp_Wl_Buffer *buffer)
{
   E_Event_Client *ev;
   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;

   e_comp_wl_buffer_reference(&ec->comp_data->buffer_ref, buffer);

   /* set usable early because shell module checks this */
   e_pixmap_usable_set(ec->pixmap, (buffer != NULL));
   e_pixmap_resource_set(ec->pixmap, buffer);
   e_pixmap_dirty(ec->pixmap);
   e_pixmap_refresh(ec->pixmap);

   _e_comp_wl_surface_state_size_update(ec, &ec->comp_data->pending);
   e_comp_wl_map_size_cal_from_buffer(ec);

   /* wm-policy module uses it */
   _e_comp_wl_hook_call(E_COMP_WL_HOOK_BUFFER_CHANGE, ec);

   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(E_EVENT_CLIENT_BUFFER_CHANGE, ev,
                   (Ecore_End_Cb)e_comp_wl_surface_event_simple_free, NULL);
}

E_API Eina_Bool
e_comp_wl_surface_commit(E_Client *ec)
{
   Eina_Bool ignored;

   _e_comp_wl_surface_state_commit(ec, &ec->comp_data->pending);
   if (!e_comp_object_damage_exists(ec->frame))
     e_pixmap_image_clear(ec->pixmap, 1);

   ignored = ec->ignored;

   if (_e_comp_wl_surface_subsurface_order_commit(ec))
     {
        E_Client *topmost = _e_comp_wl_topmost_parent_get(ec);
        _e_comp_wl_subsurface_restack(topmost);
        _e_comp_wl_subsurface_restack_bg_rectangle(topmost);
     }

   if (!e_pixmap_usable_get(ec->pixmap))
     {
        if (ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.unmap))
               ec->comp_data->shell.unmap(ec->comp_data->shell.surface);
             else if (e_client_has_xwindow(ec) || ec->internal || ec->comp_data->sub.data ||
                      (ec == e_comp_wl->drag_client))
               {
                  ec->visible = EINA_FALSE;
                  evas_object_hide(ec->frame);
                  ec->comp_data->mapped = 0;
               }
          }

        if (ec->comp_data->sub.below_obj && evas_object_visible_get(ec->comp_data->sub.below_obj))
          evas_object_hide(ec->comp_data->sub.below_obj);
     }
   else
     {
        if (!ec->comp_data->mapped)
          {
             if ((ec->comp_data->shell.surface) && (ec->comp_data->shell.map))
               ec->comp_data->shell.map(ec->comp_data->shell.surface);
             else if (e_client_has_xwindow(ec) || ec->internal || _e_comp_wl_subsurface_can_show(ec) ||
                      (ec == e_comp_wl->drag_client))
               {
                  ec->visible = EINA_TRUE;
                  ec->ignored = 0;
                  evas_object_show(ec->frame);
                  ec->comp_data->mapped = 1;
               }
          }

        if (ec->comp_data->sub.below_obj && !evas_object_visible_get(ec->comp_data->sub.below_obj)
            && evas_object_visible_get(ec->frame))
          evas_object_show(ec->comp_data->sub.below_obj);
     }
   ec->ignored = ignored;
   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_subsurface_commit(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   /* check for valid subcompositor data */
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return EINA_FALSE;
   if (!(sdata = ec->comp_data->sub.data)) return EINA_FALSE;

   if (_e_comp_wl_subsurface_synchronized_get(sdata))
     _e_comp_wl_subsurface_commit_to_cache(ec);
   else
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->position.set)
          {
             E_Client *parent = sdata->parent;
             if (parent)
               {
                  evas_object_move(ec->frame, parent->x + sdata->position.x,
                                   parent->y + sdata->position.y);
                  sdata->position.set = EINA_FALSE;
               }
          }

        if (sdata->cached.has_data)
          {
             _e_comp_wl_subsurface_commit_to_cache(ec);
             _e_comp_wl_subsurface_commit_from_cache(ec);
          }
        else
          e_comp_wl_surface_commit(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
     }

   return EINA_TRUE;
}

E_API void
e_comp_wl_buffer_reference(E_Comp_Wl_Buffer_Ref *ref, E_Comp_Wl_Buffer *buffer)
{
   if ((ref->buffer) && (buffer != ref->buffer))
     {
        ref->buffer->busy--;
        if (ref->buffer->busy == 0)
          {
             if (ref->buffer->type == E_COMP_WL_BUFFER_TYPE_TBM)
               {
                  e_comp_wl_tbm_buffer_destroy(ref->buffer);
               }
             else
               {
                  if (!wl_resource_get_client(ref->buffer->resource)) return;
                  wl_buffer_send_release(ref->buffer->resource);
#ifdef HAVE_WAYLAND_TBM
                  wayland_tbm_server_increase_buffer_sync_timeline(e_comp_wl->tbm.server,
                                                                   ref->buffer->resource, 1);
#endif
               }
          }

        if (ref->destroy_listener_usable)
          {
             wl_list_remove(&ref->destroy_listener.link);
             ref->destroy_listener_usable = EINA_FALSE;
          }
     }

   if ((buffer) && (buffer != ref->buffer))
     {
        buffer->busy++;
        wl_signal_add(&buffer->destroy_signal, &ref->destroy_listener);
        ref->destroy_listener_usable = EINA_TRUE;
     }

   ref->buffer = buffer;
   ref->destroy_listener.notify = _e_comp_wl_buffer_reference_cb_destroy;
}

/**
 * Get the buffer for a given resource.
 *
 * Retrieves the Wayland SHM buffer for the resource and
 * uses it to create a new E_Comp_Wl_Buffer object. This
 * buffer will be freed when the resource is destroyed.
 *
 * @param resource that owns the desired buffer
 * @returns a new E_Comp_Wl_Buffer object
 */
E_API E_Comp_Wl_Buffer *
e_comp_wl_buffer_get(struct wl_resource *resource, E_Client *ec)
{
   E_Comp_Wl_Buffer *buffer = NULL;
   struct wl_listener *listener;
   struct wl_shm_buffer *shmbuff;
   Eina_Bool res;
   tbm_surface_h tbm_surf;

   listener =
     wl_resource_get_destroy_listener(resource, _e_comp_wl_buffer_cb_destroy);
   if (listener)
     return container_of(listener, E_Comp_Wl_Buffer, destroy_listener);

   if (!(buffer = E_NEW(E_Comp_Wl_Buffer, 1))) return NULL;

   shmbuff = wl_shm_buffer_get(resource);

   if (shmbuff)
     {
        buffer->type = E_COMP_WL_BUFFER_TYPE_SHM;

        buffer->w = wl_shm_buffer_get_width(shmbuff);
        buffer->h = wl_shm_buffer_get_height(shmbuff);
     }
   else
     {
        if ((ec) && (ec->comp_data->video_client))
          {
             buffer->type = E_COMP_WL_BUFFER_TYPE_VIDEO;
             buffer->w = buffer->h = 1;
          }
        else if (e_comp->gl)
          {
             buffer->type = E_COMP_WL_BUFFER_TYPE_NATIVE;

             res = e_comp_wl->wl.glapi->evasglQueryWaylandBuffer(e_comp_wl->wl.gl,
                                                                 resource,
                                                                 EVAS_GL_WIDTH,
                                                                 &buffer->w);
             EINA_SAFETY_ON_FALSE_GOTO(res, err);

             res = e_comp_wl->wl.glapi->evasglQueryWaylandBuffer(e_comp_wl->wl.gl,
                                                                 resource,
                                                                 EVAS_GL_HEIGHT,
                                                                 &buffer->h);
             EINA_SAFETY_ON_FALSE_GOTO(res, err);
          }
        else
          {
             tbm_surf = wayland_tbm_server_get_surface(e_comp_wl->tbm.server, resource);
             if (!tbm_surf)
               goto err;

             buffer->type = E_COMP_WL_BUFFER_TYPE_NATIVE;
             buffer->w = tbm_surface_get_width(tbm_surf);
             buffer->h = tbm_surface_get_height(tbm_surf);
             buffer->tbm_surface = tbm_surf;
          }
     }
   buffer->shm_buffer = shmbuff;

   buffer->resource = resource;
   wl_signal_init(&buffer->destroy_signal);
   buffer->destroy_listener.notify = _e_comp_wl_buffer_cb_destroy;
   wl_resource_add_destroy_listener(resource, &buffer->destroy_listener);

   if (ec)
     {
        buffer->debug_info.owner_ptr = (void *)ec;
        buffer->debug_info.owner_name = eina_stringshare_add(ec->icccm.name?:"");
     }

   DBG("Wl Buffer Create: b %p owner '%s'(%p)",
       buffer, buffer->debug_info.owner_name, buffer->debug_info.owner_ptr);

   return buffer;

err:
   ERR("Invalid resource:%u", wl_resource_get_id(resource));
   E_FREE(buffer);
   return NULL;
}

static E_Comp_Wl_Output *
_e_comp_wl_output_get(Eina_List *outputs, const char *id)
{
   Eina_List *l;
   E_Comp_Wl_Output *output;

   EINA_LIST_FOREACH(outputs, l, output)
     {
       if (!strcmp(output->id, id))
         return output;
     }

   return NULL;
}

/**
 * Initializes information about one display output.
 *
 * Adds or updates the given data about a single display output,
 * with an id matching the provided id.
 *
 * @param id         identification of output to be added or changed
 * @param make       manufacturer name of the display output
 * @param model      model name of the display output
 * @param x          output's top left corner x coordinate
 * @param y          output's top left corner y coordinate
 * @param w          output's width in pixels
 * @param h          output's height in pixels
 * @param pw         output's physical width in millimeters
 * @param ph         output's physical height in millimeters
 * @param refresh    output's refresh rate in mHz
 * @param subpixel   output's subpixel layout
 * @param transform  output's rotation and/or mirror transformation
 *
 * @returns True if a display output object could be added or updated
 */
E_API Eina_Bool
e_comp_wl_output_init(const char *id, const char *make, const char *model,
                      int x, int y, int w, int h, int pw, int ph,
                      unsigned int refresh, unsigned int subpixel,
                      unsigned int transform)
{
   E_Comp_Wl_Output *output;
   Eina_List *l2;
   struct wl_resource *resource;

   /* retrieve named output; or create it if it doesn't exist */
   output = _e_comp_wl_output_get(e_comp_wl->outputs, id);
   if (!output)
     {
        if (!(output = E_NEW(E_Comp_Wl_Output, 1))) return EINA_FALSE;

        if (id) output->id = eina_stringshare_add(id);
        if (make)
          output->make = eina_stringshare_add(make);
        else
          output->make = eina_stringshare_add("unknown");
        if (model)
          output->model = eina_stringshare_add(model);
        else
          output->model = eina_stringshare_add("unknown");

        e_comp_wl->outputs = eina_list_append(e_comp_wl->outputs, output);

        output->global = 
          wl_global_create(e_comp_wl->wl.disp, &wl_output_interface,
                           2, output, _e_comp_wl_cb_output_bind);

        output->resources = NULL;
        output->scale = e_scale;
     }

   /* update the output details */
   output->x = x;
   output->y = y;
   output->w = w;
   output->h = h;
   output->phys_width = pw;
   output->phys_height = ph;
   output->refresh = refresh;
   output->subpixel = subpixel;
   output->transform = transform;

   if (output->scale <= 0)
     output->scale = e_scale;

   /* if we have bound resources, send updates */
   EINA_LIST_FOREACH(output->resources, l2, resource)
     {
        wl_output_send_geometry(resource,
                                output->x, output->y,
                                output->phys_width,
                                output->phys_height,
                                output->subpixel,
                                output->make ?: "", output->model ?: "",
                                output->transform);

        if (wl_resource_get_version(resource) >= WL_OUTPUT_SCALE_SINCE_VERSION)
          wl_output_send_scale(resource, output->scale);

        /* 3 == preferred + current */
        wl_output_send_mode(resource, 3, output->w, output->h, output->refresh);

        if (wl_resource_get_version(resource) >= WL_OUTPUT_DONE_SINCE_VERSION)
          wl_output_send_done(resource);
     }

   return EINA_TRUE;
}

E_API void
e_comp_wl_output_remove(const char *id)
{
   E_Comp_Wl_Output *output;

   output = _e_comp_wl_output_get(e_comp_wl->outputs, id);
   if (output)
     {
        e_comp_wl->outputs = eina_list_remove(e_comp_wl->outputs, output);

        /* wl_global_destroy(output->global); */

        /* eina_stringshare_del(output->id); */
        /* eina_stringshare_del(output->make); */
        /* eina_stringshare_del(output->model); */

        /* free(output); */
     }
}

static void
_e_comp_wl_key_send(Ecore_Event_Key *ev, enum wl_keyboard_key_state state, Eina_List *key_list, Eina_Bool focused)
{
   struct wl_resource *res;
   Eina_List *l;
   uint32_t serial, keycode;
   struct wl_client *wc;
   Ecore_Device *last_dev;
   E_Comp_Config *comp_conf = NULL;

   keycode = (ev->keycode - 8);

   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   comp_conf = e_comp_config_get();

   EINA_LIST_FOREACH(key_list, l, res)
     {
        wc = wl_resource_get_client(res);
        if (!focused && wc != ev->data) continue;
        TRACE_INPUT_BEGIN(_e_comp_wl_key_send);
        last_dev = eina_hash_find(_last_keydev_hash, wc);
        if (!last_dev)
          {
             _e_comp_wl_client_destroy_listener_add(wc);
             eina_hash_direct_add(_last_keydev_hash, wc, ev->dev);
             _e_comp_wl_send_event_device(ev->data, ev->timestamp, ev->dev, serial);
          }
        else if (last_dev != ev->dev)
          {
             eina_hash_modify(_last_keydev_hash, wc, ev->dev);
             _e_comp_wl_send_event_device(ev->data, ev->timestamp, ev->dev, serial);
          }

        if (comp_conf && comp_conf->input_log_enable)
          INF("[Server] Key %s (time: %d)\n", (state ? "Down" : "Up"), ev->timestamp);

        wl_keyboard_send_key(res, serial, ev->timestamp,
                             keycode, state);
        TRACE_INPUT_END();
     }
}

EINTERN Eina_Bool
e_comp_wl_key_down(Ecore_Event_Key *ev)
{
   E_Client *ec = NULL;
   struct wl_client *wc = NULL;
   uint32_t keycode;
   E_Comp_Wl_Key_Data *end, *k;

   if ((e_comp->comp_type != E_PIXMAP_TYPE_WL) || (ev->window != e_comp->ee_win))
     {
        return EINA_FALSE;
     }

   keycode = (ev->keycode - 8);
   if (!(e_comp_wl = e_comp->wl_comp_data))
     {
        return EINA_FALSE;
     }

#ifndef E_RELEASE_BUILD
   if ((ev->modifiers & ECORE_EVENT_MODIFIER_CTRL) &&
       ((ev->modifiers & ECORE_EVENT_MODIFIER_ALT) ||
       (ev->modifiers & ECORE_EVENT_MODIFIER_ALTGR)) &&
       eina_streq(ev->key, "BackSpace"))
     {
        exit(0);
     }
#endif

   ec = e_client_focused_get();
   if (ec && ec->comp_data && ec->comp_data->surface)
     wc = wl_resource_get_client(ec->comp_data->surface);

   if (ev->data)
     {
        if ((wc != ev->data) && (ev->data != (void *)0x1))
          {
             _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_PRESSED, e_comp_wl->kbd.resources, EINA_FALSE);
          }
        else
          {
             ec = NULL;
             end = (E_Comp_Wl_Key_Data *)e_comp_wl->kbd.routed_keys.data + (e_comp_wl->kbd.routed_keys.size / sizeof(*k));

             for (k = e_comp_wl->kbd.routed_keys.data; k < end; k++)
               {
                  /* ignore server-generated key repeats */
                  if (k->key == keycode)
                    {
                       return EINA_FALSE;
                    }
               }

             if (ev->data == (void *)0x1) return EINA_FALSE;

             if ((!e_client_action_get()) && (!e_comp->input_key_grabs))
               {
                  ec = e_client_focused_get();
                  if (ec && ec->comp_data && ec->comp_data->surface && e_comp_wl->kbd.focused)
                    {
                       _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_PRESSED, e_comp_wl->kbd.focused, EINA_TRUE);

                       /* A key only sent to clients is added to the list */
                       e_comp_wl->kbd.routed_keys.size = (const char *)end - (const char *)e_comp_wl->kbd.routed_keys.data;
                       if (!(k = wl_array_add(&e_comp_wl->kbd.routed_keys, sizeof(*k))))
                         {
                            DBG("wl_array_add: Out of memory\n");
                            return EINA_FALSE;
                         }
                       k->key = keycode;
                       k->dev = ev->dev;
                    }
               }

             /* update modifier state */
             e_comp_wl_input_keyboard_state_update(keycode, EINA_TRUE);
          }

        return !!ec;
     }

   ec = NULL;

   end = (E_Comp_Wl_Key_Data *)e_comp_wl->kbd.keys.data + (e_comp_wl->kbd.keys.size / sizeof(*k));

   for (k = e_comp_wl->kbd.keys.data; k < end; k++)
     {
        /* ignore server-generated key repeats */
        if (k->key == keycode)
          {
             return EINA_FALSE;
          }
     }

   if ((!e_client_action_get()) && (!e_comp->input_key_grabs))
     {
        ec = e_client_focused_get();
        if (ec && ec->comp_data && ec->comp_data->surface && e_comp_wl->kbd.focused)
          {
             ev->data = wc;
             _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_PRESSED, e_comp_wl->kbd.focused, EINA_TRUE);

             /* A key only sent to clients is added to the list */
             e_comp_wl->kbd.keys.size = (const char *)end - (const char *)e_comp_wl->kbd.keys.data;
             if (!(k = wl_array_add(&e_comp_wl->kbd.keys, sizeof(*k))))
               {
                  DBG("wl_array_add: Out of memory\n");
                  return EINA_FALSE;
               }
             k->key = keycode;
             k->dev = ev->dev;
          }
     }

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(keycode, EINA_TRUE);

   return !!ec;
}

EINTERN Eina_Bool
e_comp_wl_key_up(Ecore_Event_Key *ev)
{
   E_Client *ec = NULL;
   struct wl_client *wc = NULL;
   uint32_t keycode, delivered_key;
   E_Comp_Wl_Key_Data *end, *k;

   if ((e_comp->comp_type != E_PIXMAP_TYPE_WL) ||
       (ev->window != e_comp->ee_win))
     {
        return EINA_FALSE;
     }

   keycode = (ev->keycode - 8);
   delivered_key = 0;
   if (!(e_comp_wl = e_comp->wl_comp_data))
     {
        return EINA_FALSE;
     }

   ec = e_client_focused_get();

   if (ec && ec->comp_data && ec->comp_data->surface)
     wc = wl_resource_get_client(ec->comp_data->surface);

   if (ev->data)
     {
        end = (E_Comp_Wl_Key_Data *)e_comp_wl->kbd.routed_keys.data + (e_comp_wl->kbd.routed_keys.size / sizeof(*k));
        for (k = e_comp_wl->kbd.routed_keys.data; k < end; k++)
          {
             if (k->key == keycode)
               {
                  *k = *--end;
                  delivered_key = 1;
               }
          }
        e_comp_wl->kbd.routed_keys.size =
          (const char *)end - (const char *)e_comp_wl->kbd.routed_keys.data;

        if (wc != ev->data)
          {
             _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_RELEASED, e_comp_wl->kbd.resources, EINA_FALSE);
          }
        else
          {
             ec = NULL;

             if ((delivered_key) ||
                 ((!e_client_action_get()) && (!e_comp->input_key_grabs)))
               {
                  ec = e_client_focused_get();

                  if (e_comp_wl->kbd.focused)
                    {
                       _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_RELEASED, e_comp_wl->kbd.focused, EINA_FALSE);
                    }
               }

             /* update modifier state */
             e_comp_wl_input_keyboard_state_update(keycode, EINA_FALSE);
          }
        return !!ec;
     }

   ec = NULL;

   end = (E_Comp_Wl_Key_Data *)e_comp_wl->kbd.keys.data + (e_comp_wl->kbd.keys.size / sizeof(*k));
   for (k = e_comp_wl->kbd.keys.data; k < end; k++)
     {
        if (k->key == keycode)
          {
             *k = *--end;
             delivered_key = 1;
          }
     }

   e_comp_wl->kbd.keys.size =
     (const char *)end - (const char *)e_comp_wl->kbd.keys.data;

   /* If a key down event have been sent to clients, send a key up event to client for garantee key event sequence pair. (down/up) */
   if ((delivered_key) ||
       ((!e_client_action_get()) && (!e_comp->input_key_grabs)))
     {
        ec = e_client_focused_get();

        if (e_comp_wl->kbd.focused)
          {
             ev->data = wc;
             _e_comp_wl_key_send(ev, WL_KEYBOARD_KEY_STATE_RELEASED, e_comp_wl->kbd.focused, EINA_TRUE);
          }
     }

   /* update modifier state */
   e_comp_wl_input_keyboard_state_update(keycode, EINA_FALSE);

   return !!ec;
}

E_API Eina_Bool
e_comp_wl_evas_handle_mouse_button(E_Client *ec, uint32_t timestamp, uint32_t button_id, uint32_t state)
{
   Eina_List *l;
   struct wl_client *wc;
   uint32_t serial, btn;
   struct wl_resource *res;
   E_Comp_Config *comp_conf = NULL;

   if (ec->cur_mouse_action || e_comp_wl->drag)
     return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (ec->ignored) return EINA_FALSE;

   switch (button_id)
     {
      case 1:  btn = BTN_LEFT;   break;
      case 2:  btn = BTN_MIDDLE; break;
      case 3:  btn = BTN_RIGHT;  break;
      default: btn = button_id;  break;
     }

   e_comp_wl->ptr.button = btn;

   if (!ec->comp_data || !ec->comp_data->surface) return EINA_FALSE;

   if (!eina_list_count(e_comp_wl->ptr.resources))
     return EINA_TRUE;

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   comp_conf = e_comp_config_get();

   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (!e_comp_wl_input_pointer_check(res)) continue;
        TRACE_INPUT_BEGIN(e_comp_wl_evas_handle_mouse_button);

        if (comp_conf && comp_conf->input_log_enable)
          INF("[Server] Mouse Button %s (btn: %d, time: %d)\n", (state ? "Down" : "Up"), btn, timestamp);

        wl_pointer_send_button(res, serial, timestamp, btn, state);
        TRACE_INPUT_END();
     }
   return EINA_TRUE;
}

E_API void
e_comp_wl_touch_cancel(void)
{
   _e_comp_wl_touch_cancel();
}

E_API E_Comp_Wl_Hook *
e_comp_wl_hook_add(E_Comp_Wl_Hook_Point hookpoint, E_Comp_Wl_Hook_Cb func, const void *data)
{
   E_Comp_Wl_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_COMP_WL_HOOK_LAST, NULL);
   ch = E_NEW(E_Comp_Wl_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_comp_wl_hooks[hookpoint] = eina_inlist_append(_e_comp_wl_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_comp_wl_hook_del(E_Comp_Wl_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_comp_wl_hooks_walking == 0)
     {
        _e_comp_wl_hooks[ch->hookpoint] = eina_inlist_remove(_e_comp_wl_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_comp_wl_hooks_delete++;
}

E_API void
e_comp_wl_shell_surface_ready(E_Client *ec)
{
   if (!ec) return;

   _e_comp_wl_hook_call(E_COMP_WL_HOOK_SHELL_SURFACE_READY, ec);
}

E_API void
e_comp_wl_input_cursor_timer_enable_set(Eina_Bool enabled)
{
   e_config->use_cursor_timer = !!enabled;

   if (e_config->use_cursor_timer == EINA_FALSE)
     {
        if (e_comp_wl->ptr.hide_tmr)
          {
             ecore_timer_del(e_comp_wl->ptr.hide_tmr);
             e_comp_wl->ptr.hide_tmr = NULL;
          }
        cursor_timer_ec = NULL;

        if (e_pointer_is_hidden(e_comp->pointer))
          {
             _e_comp_wl_cursor_reload(e_comp_wl->ptr.ec);
          }
     }
}

EINTERN Eina_Bool
e_comp_wl_key_send(E_Client *ec, int keycode, Eina_Bool pressed, Ecore_Device *dev, uint32_t time)
{
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial, wl_keycode;
   enum wl_keyboard_key_state state;
   E_Comp_Config *comp_conf = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   wl_keycode = keycode - 8;
   EINA_SAFETY_ON_TRUE_RETURN_VAL(wl_keycode <= 0, EINA_FALSE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   if (pressed) state = WL_KEYBOARD_KEY_STATE_PRESSED;
   else state = WL_KEYBOARD_KEY_STATE_RELEASED;

   comp_conf = e_comp_config_get();

   EINA_LIST_FOREACH(e_comp_wl->kbd.resources, l, res)
     {
        if (wl_resource_get_client(res) != wc) continue;
        if (dev) _e_comp_wl_send_event_device(wc, time, dev, serial);
        else _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_KEYBOARD, time);

        if (comp_conf && comp_conf->input_log_enable)
          INF("[Server] Key %s (time: %d)\n", (state ? "Down" : "Up"), time);

        wl_keyboard_send_key(res, serial, time,
                             wl_keycode, state);
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_touch_send(E_Client *ec, int idx, int x, int y, Eina_Bool pressed, Ecore_Device *dev, double radius_x, double radius_y, double pressure, double angle, uint32_t time)
{
   struct wl_client *wc;
   uint32_t serial;
   E_Comp_Wl_Input_Device *device = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   if (!dev) device = _e_comp_wl_device_last_device_get(ECORE_DEVICE_CLASS_TOUCH);

   wc = wl_resource_get_client(ec->comp_data->surface);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (dev)
     {
        _e_comp_wl_send_event_device(wc, time, dev, serial);
        _e_comp_wl_device_handle_axes(ecore_device_identifier_get(dev), ECORE_DEVICE_CLASS_TOUCH, ec, radius_x, radius_y, pressure, angle);
     }
   else if (device)
     {
        _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_TOUCH, time);
        _e_comp_wl_device_handle_axes(device->identifier, device->clas, ec, radius_x, radius_y, pressure, angle);
     }

   x = x + ec->client.x;
   y = y + ec->client.y;

   _e_comp_wl_send_touch(ec, idx, x, y, time, pressed);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_touch_update_send(E_Client *ec, int idx, int x, int y, Ecore_Device *dev, double radius_x, double radius_y, double pressure, double angle, uint32_t time)
{
   E_Comp_Wl_Input_Device *device;
   uint32_t serial;
   struct wl_client *wc;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   if (!dev) device = _e_comp_wl_device_last_device_get(ECORE_DEVICE_CLASS_TOUCH);

   wc = wl_resource_get_client(ec->comp_data->surface);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (dev)
     {
        _e_comp_wl_send_event_device(wc, time, dev, serial);
        _e_comp_wl_device_handle_axes(ecore_device_identifier_get(dev), ECORE_DEVICE_CLASS_TOUCH, ec, radius_x, radius_y, pressure, angle);
     }
   else if (device)
     {
        _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_TOUCH, time);
        _e_comp_wl_device_handle_axes(device->identifier, device->clas, ec, radius_x, radius_y, pressure, angle);
     }

   x = x + ec->client.x;
   y = y + ec->client.y;

   _e_comp_wl_send_touch_move(ec, idx, x, y, time);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_touch_cancel_send(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   _e_comp_wl_send_touch_cancel(ec);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_mouse_button_send(E_Client *ec, int buttons, Eina_Bool pressed, Ecore_Device *dev, uint32_t time)
{
   uint32_t serial;
   struct wl_client *wc;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (dev) _e_comp_wl_send_event_device(wc, time, dev, serial);
   else _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_MOUSE, time);

   if (pressed)
     e_comp_wl_evas_handle_mouse_button(ec, time, buttons,
                                          WL_POINTER_BUTTON_STATE_PRESSED);
   else
     e_comp_wl_evas_handle_mouse_button(ec, time, buttons,
                                          WL_POINTER_BUTTON_STATE_RELEASED);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_mouse_move_send(E_Client *ec, int x, int y, Ecore_Device *dev, uint32_t time)
{
   uint32_t serial;
   struct wl_client *wc;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (dev) _e_comp_wl_send_event_device(wc, time, dev, serial);
   else _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_MOUSE, time);

   x = x + ec->client.x;
   y = y + ec->client.y;

   _e_comp_wl_send_mouse_move(ec, x, y, time);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_mouse_wheel_send(E_Client *ec, int direction, int z, Ecore_Device *dev, uint32_t time)
{
   uint32_t serial;
   struct wl_client *wc;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_wl, EINA_FALSE);

   wc = wl_resource_get_client(ec->comp_data->surface);
   if (!time) time = (uint32_t)(ecore_time_get() * 1000);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);

   if (dev) _e_comp_wl_send_event_device(wc, time, dev, serial);
   else _e_comp_wl_device_send_last_event_device(ec, ECORE_DEVICE_CLASS_MOUSE, time);

   _e_comp_wl_mouse_wheel_send(ec, direction, z, time);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_cursor_hide(E_Client *ec)
{
   struct wl_resource *res;
   struct wl_client *wc;
   Eina_List *l;
   uint32_t serial;

   e_pointer_object_set(e_comp->pointer, NULL, 0, 0);

   if (e_comp_wl->ptr.hide_tmr)
     {
        ecore_timer_del(e_comp_wl->ptr.hide_tmr);
        e_comp_wl->ptr.hide_tmr = NULL;
     }
   cursor_timer_ec = NULL;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if (!ec->comp_data || !ec->comp_data->surface) return EINA_FALSE;
   wc = wl_resource_get_client(ec->comp_data->surface);
   serial = wl_display_next_serial(e_comp_wl->wl.disp);
   EINA_LIST_FOREACH(e_comp_wl->ptr.resources, l, res)
     {
        if (!e_comp_wl_input_pointer_check(res)) continue;
        if (wl_resource_get_client(res) != wc) continue;
        if (ec->pointer_enter_sent == EINA_FALSE) continue;
        wl_pointer_send_leave(res, serial, ec->comp_data->surface);
        ec->pointer_enter_sent = EINA_FALSE;
     }

   return EINA_TRUE;
}
