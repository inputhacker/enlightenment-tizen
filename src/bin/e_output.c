#include "e.h"

#define EOERR(f, output, x...)                                   \
   do                                                            \
     {                                                           \
        if (!output)                                             \
          ERR("EWL|%20.20s|              |             |%8s|"f,  \
              "OUTPUT", "Unknown", ##x);                         \
        else                                                     \
          ERR("EWL|%20.20s|              |             |%8s|"f,  \
              "OUTPUT", (output->id), ##x);                      \
     }                                                           \
   while (0)

#define EOINF(f, output, x...)                                   \
   do                                                            \
     {                                                           \
        if (!output)                                             \
          INF("EWL|%20.20s|              |             |%8s|"f,  \
              "OUTPUT", "Unknown", ##x);                         \
        else                                                     \
          INF("EWL|%20.20s|              |             |%8s|"f,  \
              "OUTPUT", (output->id), ##x);                      \
     }                                                           \
   while (0)

#define DUMP_FPS 30
#define EOM_DELAY_CONNECT_CHECK_TIMEOUT 3.0
#define EOM_DELAY_CHECK_TIMEOUT 1.0
/*
#define EOM_DUMP_PRESENTATION_BUFFERS
*/

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

typedef struct _E_Output_Capture E_Output_Capture;
typedef struct _E_Output_Layer E_Output_Layer;

typedef struct _E_Eom_Buffer         E_EomBuffer,       *E_EomBufferPtr;
typedef struct _E_Eom_Pp_Data        E_EomPpData,       *E_EomPpDataPtr;

typedef void   (*E_EomEndShowingEventPtr)  (E_Output *output, tbm_surface_h srfc, void * user_data);

struct _E_Output_Capture
{
   E_Output *output;
   tdm_capture *tcapture;
   tbm_surface_h surface;
   E_Output_Capture_Cb func;
   void *data;
   Eina_Bool in_using;
   Eina_Bool dequeued;
};

struct _E_Output_Layer
{
   tdm_layer *layer;
   int zpos;
};

struct _E_Eom_Output_Buffer
{
   E_Output *output;
   tbm_surface_h tbm_surface;
   E_EomEndShowingEventPtr cb_func;
   void *cb_user_data;
};

struct _E_Eom_Buffer
{
   E_Comp_Wl_Buffer *wl_buffer;
   E_Comp_Wl_Buffer_Ref comp_wl_buffer_ref;

   /* double reference to avoid sigterm crash */
   E_Comp_Wl_Buffer_Ref comp_wl_buffer_ref_2;
};

struct _E_Eom_Output_Pp
{
   tdm_pp *pp;
   tbm_surface_queue_h queue;
   tdm_info_pp pp_info;
};

struct _E_Eom_Pp_Data
{
   E_Output *output;
   E_EomBufferPtr eom_buffer;
   tbm_surface_h tsurface;
};

static int _e_output_hooks_delete = 0;
static int _e_output_hooks_walking = 0;

static Eina_Inlist *_e_output_hooks[] =
{
   [E_OUTPUT_HOOK_DPMS_CHANGE] = NULL,
   [E_OUTPUT_HOOK_CONNECT_STATUS_CHANGE] = NULL,
   [E_OUTPUT_HOOK_MODE_CHANGE] = NULL,
   [E_OUTPUT_HOOK_ADD] = NULL,
   [E_OUTPUT_HOOK_REMOVE] = NULL,
};

static int _e_output_intercept_hooks_delete = 0;
static int _e_output_intercept_hooks_walking = 0;

static Eina_Inlist *_e_output_intercept_hooks[] =
{
   [E_OUTPUT_INTERCEPT_HOOK_DPMS_ON] = NULL,
   [E_OUTPUT_INTERCEPT_HOOK_DPMS_STANDBY] = NULL,
   [E_OUTPUT_INTERCEPT_HOOK_DPMS_SUSPEND] = NULL,
   [E_OUTPUT_INTERCEPT_HOOK_DPMS_OFF] = NULL,
};

static Eina_Bool _e_output_capture(E_Output *output, tbm_surface_h tsurface, Eina_Bool auto_rotate);
static void _e_output_vblank_handler(tdm_output *output, unsigned int sequence,
                                     unsigned int tv_sec, unsigned int tv_usec, void *data);

static unsigned int
_e_output_aligned_width_get(E_Output *output, tbm_surface_h tsurface)
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
        EOERR("not supported format: %x", output, surf_info.format);
     }

   return aligned_width;
}

static Eina_Bool
_e_output_presentation_check(void *data)
{
   E_Output *output;
   E_Output *primary_output;

   if (!data) return ECORE_CALLBACK_CANCEL;

   output = (E_Output *)data;

   if (e_output_display_mode_get(output) == E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION)
     {
        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        e_output_mirror_set(output, primary_output);
     }

   output->delay_timer = NULL;

   return ECORE_CALLBACK_CANCEL;
}

static inline void
_e_output_display_mode_set(E_Output *output, E_Output_Display_Mode display_mode)
{
   if (output == NULL) return;
   if (output->display_mode == display_mode) return;

   output->display_mode = display_mode;
}

static void
_e_output_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Output_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_OUTPUT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_output_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_output_hooks[x] = eina_inlist_remove(_e_output_hooks[x], EINA_INLIST_GET(ch));
         free(ch);
       }
}

static void
_e_output_hook_call(E_Output_Hook_Point hookpoint, E_Output *output)
{
   E_Output_Hook *ch;

   _e_output_hooks_walking++;
   EINA_INLIST_FOREACH(_e_output_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, output);
     }
   _e_output_hooks_walking--;
   if ((_e_output_hooks_walking == 0) && (_e_output_hooks_delete > 0))
     _e_output_hooks_clean();
}

static void
_e_output_intercept_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Output_Intercept_Hook *ch;
   unsigned int x;
   for (x = 0; x < E_OUTPUT_INTERCEPT_HOOK_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_output_intercept_hooks[x], l, ch)
       {
          if (!ch->delete_me) continue;
          _e_output_intercept_hooks[x] = eina_inlist_remove(_e_output_intercept_hooks[x], EINA_INLIST_GET(ch));
         free(ch);
       }
}

static Eina_Bool
_e_output_intercept_hook_call(E_Output_Intercept_Hook_Point hookpoint, E_Output *output)
{
   E_Output_Intercept_Hook *ch;
   Eina_Bool res = EINA_TRUE;

   _e_output_intercept_hooks_walking++;
   EINA_INLIST_FOREACH(_e_output_intercept_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        res = ch->func(ch->data, output);
        if (res == EINA_FALSE) break;
     }
   _e_output_intercept_hooks_walking--;
   if ((_e_output_intercept_hooks_walking == 0) && (_e_output_intercept_hooks_delete > 0))
     _e_output_intercept_hooks_clean();

   return res;
}

static E_Client *
_e_output_zoom_top_visible_ec_get()
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

static int
_e_output_zoom_get_angle(E_Output *output)
{
   E_Client *ec = NULL;
   int ec_angle = 0;

   ec = _e_output_zoom_top_visible_ec_get();
   if (ec)
     ec_angle = ec->e.state.rot.ang.curr;

   return ec_angle;
}

static void
_e_output_zoom_raw_xy_get(E_Output *output, int *x, int *y)
{
   int w = 0, h = 0;

   e_output_size_get(output, &w, &h);

   if (w <= 0 || h <= 0)
     return;

   if ((output->zoom_conf.init_screen_rotation == 0) || (output->zoom_conf.init_screen_rotation == 180))
     {
        if (output->zoom_conf.current_screen_rotation == 0)
          {
             *x = output->zoom_conf.init_cx;
             *y = output->zoom_conf.init_cy;
          }
        else if (output->zoom_conf.current_screen_rotation == 90)
          {
             *x = (float)w / h * output->zoom_conf.init_cy;
             *y = h - (float)h / w * output->zoom_conf.init_cx - 1;
          }
        else if (output->zoom_conf.current_screen_rotation == 180)
          {
             *x = w - output->zoom_conf.init_cx - 1;
             *y = h - output->zoom_conf.init_cy - 1;
          }
        else /* output->zoom_conf.current_screen_rotation == 270 */
          {
             *x = w - (float)w / h * output->zoom_conf.init_cy - 1;
             *y = (float)h / w * output->zoom_conf.init_cx;
          }
     }
   else /* (output->zoom_conf.init_screen_rotation == 90) || (output->zoom_conf.init_screen_rotation == 270) */
     {
        if (output->zoom_conf.current_screen_rotation == 0)
          {
             *x = (float)w / h * output->zoom_conf.init_cx;
             *y = (float)h / w * output->zoom_conf.init_cy;
          }
        else if (output->zoom_conf.current_screen_rotation == 90)
          {
             *x = output->zoom_conf.init_cy;
             *y = h - output->zoom_conf.init_cx - 1;
          }
        else if (output->zoom_conf.current_screen_rotation == 180)
          {
             *x = w - (float)w / h * output->zoom_conf.init_cx - 1;
             *y = h - (float)h / w * output->zoom_conf.init_cy - 1;
          }
        else /* output->zoom_conf.current_screen_rotation == 270 */
          {
             *x = w - output->zoom_conf.init_cy - 1;
             *y = output->zoom_conf.init_cx;
          }
     }
}

static void
_e_output_zoom_scaled_rect_get(int out_w, int out_h, double zoomx, double zoomy, int cx, int cy, Eina_Rectangle *rect)
{
   double x, y;
   double dx, dy;

   rect->w = (int)((double)out_w / zoomx);
   rect->h = (int)((double)out_h / zoomy);

   x = 0 - cx;
   y = 0 - cy;

   x = (((double)x) * zoomx);
   y = (((double)y) * zoomy);

   x = x + cx;
   y = y + cy;

   if (x == 0)
     dx = 0;
   else
     dx = 0 - x;

   if (y == 0)
     dy = 0;
   else
     dy = 0 - y;

   rect->x = (int)(dx / zoomx);
   rect->y = (int)(dy / zoomy);
}

static void
_e_output_zoom_coordinate_cal(E_Output *output)
{
   int x = 0, y = 0;
   int w = 0, h = 0;
   int zoomx = 0, zoomy = 0;
   int rotation_diff = 0;

   rotation_diff = (360 + output->zoom_conf.current_screen_rotation - output->zoom_conf.init_screen_rotation) % 360;

   e_output_size_get(output, &w, &h);

   _e_output_zoom_raw_xy_get(output, &x, &y);

   output->zoom_conf.adjusted_cx = x;
   output->zoom_conf.adjusted_cy = y;

   if (rotation_diff == 90 || rotation_diff == 270)
     {
        zoomx = output->zoom_conf.zoomy;
        zoomy = output->zoom_conf.zoomx;
     }
   else
     {
        zoomx = output->zoom_conf.zoomx;
        zoomy = output->zoom_conf.zoomy;
     }

   /* get the scaled rect */
   _e_output_zoom_scaled_rect_get(w, h, zoomx, zoomy, x, y,
                                  &output->zoom_conf.rect);
}

static Eina_Bool
_e_output_zoom_touch_transform(E_Output *output, Eina_Bool set)
{
   E_Input_Device *dev = NULL;
   Eina_Bool ret = EINA_FALSE;
   const Eina_List *l;
   E_Output *primary_output = NULL;
   int w = 0, h = 0;

   EINA_LIST_FOREACH(e_input_devices_get(), l, dev)
     {
        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        if (primary_output != NULL)
          break;
     }

   if (!primary_output)
     {
        EOERR("fail get primary_output", output);
        return EINA_FALSE;
     }

   if (set)
     ret = e_input_device_touch_transformation_set(dev,
                                                     output->zoom_conf.rect_touch.x, output->zoom_conf.rect_touch.y,
                                                     output->zoom_conf.rect_touch.w, output->zoom_conf.rect_touch.h);
   else
     {
        e_output_size_get(output, &w, &h);
        ret = e_input_device_touch_transformation_set(dev, 0, 0, w, h);
     }

   if (ret != EINA_TRUE)
     EOERR("fail e_input_device_touch_transformation_set", output);

   return ret;
}

static Eina_Bool
_e_output_cb_ecore_event_filter(void *data, void *loop_data EINA_UNUSED, int type, void *event EINA_UNUSED)
{
   E_Output *output = NULL;
   E_Input_Device *dev = NULL;

   if (type != ECORE_EVENT_MOUSE_BUTTON_UP)
     return ECORE_CALLBACK_PASS_ON;

   if (!data)
     return ECORE_CALLBACK_PASS_ON;

   output = data;

   dev = eina_list_data_get(e_input_devices_get());
   if (!dev)
     {
        EOERR("fail get e_input_device", output);
        return ECORE_CALLBACK_PASS_ON;
     }

   if (output->zoom_conf.need_touch_set)
     {
        if (e_input_device_touch_pressed_get(dev) == 0)
          {
             _e_output_zoom_touch_transform(output, EINA_TRUE);
             output->zoom_conf.need_touch_set = EINA_FALSE;

             E_FREE_FUNC(output->touch_up_handler, ecore_event_filter_del);
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_output_zoom_touch_set(E_Output *output)
{
   Eina_Bool ret = EINA_FALSE;
   E_Input_Device *dev = NULL;

   if (output->zoom_conf.need_touch_set) return EINA_TRUE;

   dev = eina_list_data_get(e_input_devices_get());
   if (!dev)
     {
        EOERR("fail get e_input_device", output);
        return EINA_FALSE;
     }

   if (e_input_device_touch_pressed_get(dev))
     {
        if (output->touch_up_handler == NULL)
          output->touch_up_handler = ecore_event_filter_add(NULL,
                                                            _e_output_cb_ecore_event_filter,
                                                            NULL, output);

        output->zoom_conf.need_touch_set = EINA_TRUE;
        return EINA_TRUE;
     }
   output->zoom_conf.need_touch_set = EINA_FALSE;

   ret = _e_output_zoom_touch_transform(output, EINA_TRUE);

   return ret;
}

static Eina_Bool
_e_output_zoom_touch_unset(E_Output *output)
{
   Eina_Bool ret = EINA_FALSE;

   if (!output) return EINA_FALSE;

   output->zoom_conf.need_touch_set = EINA_FALSE;

   if (output->touch_up_handler)
     E_FREE_FUNC(output->touch_up_handler, ecore_event_filter_del);

   ret = _e_output_zoom_touch_transform(output, EINA_FALSE);

   return ret;
}

static Eina_Bool
_e_output_animating_check()
{
   E_Client *ec = NULL;

   E_CLIENT_FOREACH(ec)
     {
        if (ec->visible && !ec->input_only)
          {
             if (e_comp_object_is_animating(ec->frame))
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_output_render_update(E_Output *output)
{
   E_Client *ec = NULL;

   if (_e_output_animating_check())
     return;

   E_CLIENT_FOREACH(ec)
     {
        if (ec->visible && !ec->input_only)
          e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
     }

   e_output_render(output);
}

static E_Client *
_e_output_top_visible_ec_get()
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

static int
_e_output_top_ec_angle_get(void)
{
   E_Client *ec = NULL;

   ec = _e_output_top_visible_ec_get();
   if (ec)
     return ec->e.state.rot.ang.curr;

   return 0;
}

static void
_e_output_zoom_touch_rect_get(E_Output *output)
{
   int x = 0, y = 0;
   int w = 0, h = 0;
   int zoomx = 0, zoomy = 0;
   int rotation_diff = 0;

   rotation_diff = (360 + output->zoom_conf.current_screen_rotation - output->zoom_conf.init_screen_rotation) % 360;

   if (output->zoom_conf.current_screen_rotation == 0 || output->zoom_conf.current_screen_rotation == 180)
     e_output_size_get(output, &w, &h);
   else
     e_output_size_get(output, &h, &w);

   if ((rotation_diff == 90) || (rotation_diff == 270))
     {
        x = (float)h / w * output->zoom_conf.init_cx;
        y = (float)w / h * output->zoom_conf.init_cy;
        zoomx = output->zoom_conf.zoomy;
        zoomy = output->zoom_conf.zoomx;
     }
   else
     {
        x = output->zoom_conf.init_cx;
        y = output->zoom_conf.init_cy;
        zoomx = output->zoom_conf.zoomx;
        zoomy = output->zoom_conf.zoomy;
     }

   _e_output_zoom_scaled_rect_get(w, h, zoomx, zoomy, x, y,
                                  &output->zoom_conf.rect_touch);
}

static void
_e_output_zoom_rotate(E_Output *output)
{
   E_Plane *ep = NULL;
   Eina_List *l;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN(output);

   e_output_size_get(output, &w, &h);

   _e_output_zoom_coordinate_cal(output);
   _e_output_zoom_touch_rect_get(output);

   EOINF("zoom_rect rotate(x:%d,y:%d) (w:%d,h:%d)",
         output, output->zoom_conf.rect.x, output->zoom_conf.rect.y,
         output->zoom_conf.rect.w, output->zoom_conf.rect.h);

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        EINA_LIST_FOREACH(output->planes, l, ep)
          {
             if (!e_plane_is_fb_target(ep)) continue;

             e_plane_zoom_set(ep, &output->zoom_conf.rect);
             break;
          }

        if (!_e_output_zoom_touch_set(output))
          EOERR("fail _e_output_zoom_touch_set", output);

        /* update the ecore_evas */
        _e_output_render_update(output);
     }
   else
     {
        e_hwc_windows_zoom_set(output->hwc, &output->zoom_conf.rect);

        if (!_e_output_zoom_touch_set(output))
          EOERR("fail _e_output_zoom_touch_set", output);

        /* update the ecore_evas */
        if (e_hwc_windows_pp_commit_possible_check(output->hwc))
          _e_output_render_update(output);
     }
}

EINTERN void
e_output_zoom_rotating_check(E_Output *output)
{
   int angle = 0;

   angle = _e_output_zoom_get_angle(output);
   if ((output->zoom_conf.current_angle != angle) ||
      (output->zoom_conf.current_screen_rotation != output->config.rotation))
     {
        output->zoom_conf.current_angle = angle;
        output->zoom_conf.current_screen_rotation = output->config.rotation;
        _e_output_zoom_rotate(output);
     }
}

static Eina_Bool
_e_output_visible_client_check(E_Output *output)
{
   Eina_Rectangle r;
   E_Client *ec;
   Eina_Bool found = EINA_FALSE;
   int x, y, w, h;
   E_Zone *zone = NULL;
   E_Comp_Wl_Client_Data *cdata = NULL;
   E_Output *zone_output = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        zone_output = e_output_find(zone->output_id);
        if (!zone_output) continue;
        if (zone_output != output) continue;

        EINA_RECTANGLE_SET(&r, zone->x, zone->y, zone->w, zone->h);

        E_CLIENT_REVERSE_FOREACH(ec)
          {
              if (e_object_is_del(E_OBJECT(ec))) continue;
              if (e_client_util_ignored_get(ec)) continue;
              if (!ec->frame) continue;
              if (ec->is_cursor) continue;
              if (!ec->visible) continue;
              if (!evas_object_visible_get(ec->frame)) continue;
              cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
              if (cdata && cdata->sub.data) continue; /* skip subsurface */
              if (cdata && !cdata->mapped) continue;
              if (ec->iconic) continue;
              e_client_geometry_get(ec, &x, &y, &w, &h);
              if (E_INTERSECTS(x, y, w, h, r.x, r.y, r.w, r.h))
                {
                  found = EINA_TRUE;
                  break;
                }
          }
     }

   return found;
}

static void
_e_output_dpms_on_render(E_Output *output)
{
   if (!output) return;

   if (_e_output_visible_client_check(output))
     ecore_event_add(E_EVENT_COMPOSITOR_ENABLE, NULL, NULL, NULL);
}

static void
_e_output_client_resize(int w, int h)
{
   E_Client *ec = NULL;
   E_Comp_Client_Data *cdata = NULL;

   E_CLIENT_FOREACH(ec)
     {
        if ((ec->visible && !ec->input_only) ||
           (e_client_util_name_get(ec) != NULL && !ec->input_only))
          {
             cdata = ec->comp_data;
             if (cdata == NULL) continue;
             if (cdata->shell.configure_send == NULL) continue;

             cdata->shell.configure_send(ec->comp_data->shell.surface, 0, w ,h);
          }
     }
}

static void
_e_output_primary_update(E_Output *output)
{
   Eina_Bool ret;

   e_output_update(output);

   if (e_output_connected(output))
     {
        E_Output_Mode *mode = NULL;
        int w, h;

        e_comp_canvas_norender_push();

        mode = e_output_best_mode_find(output);
        if (!mode)
          {
             EOERR("fail to get best mode.", output);
             return;
          }

        ret = e_output_mode_apply(output, mode);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_mode_apply.", output);
             return;
          }

        output->fake_config = EINA_FALSE;

        ret = e_output_dpms_set(output, E_OUTPUT_DPMS_ON);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_dpms.", output);
             return;
          }

        e_output_size_get(output, &w, &h);
        if (w == e_comp->w && h == e_comp->h)
          {
             e_comp_canvas_norender_pop();
             return;
          }

        ecore_evas_resize(e_comp->ee, mode->w, mode->h);
        e_comp->w = mode->w;
        e_comp->h = mode->h;

        ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

        _e_output_client_resize(e_comp->w, e_comp->h);

        e_comp_canvas_norender_pop();
     }
   else
     {
        output->fake_config = EINA_TRUE;

        ret = e_output_dpms_set(output, E_OUTPUT_DPMS_OFF);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_dpms.", output);
             return;
          }
     }
}

static void
_e_output_cb_output_change(tdm_output *toutput,
                                  tdm_output_change_type type,
                                  tdm_value value,
                                  void *user_data)
{
   E_Output *output = NULL;
   E_Output *primary = NULL;
   E_OUTPUT_DPMS edpms;
   tdm_output_dpms tdpms;
   tdm_output_conn_status status;
   static Eina_Bool override = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN(toutput);
   EINA_SAFETY_ON_NULL_RETURN(user_data);

   output = (E_Output *)user_data;

   switch (type)
     {
      case TDM_OUTPUT_CHANGE_CONNECTION:
        status = (tdm_output_conn_status)value.u32;
        if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED ||
            status == TDM_OUTPUT_CONN_STATUS_CONNECTED)
          {
             primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
             EINA_SAFETY_ON_NULL_RETURN(primary);

             if (primary == output)
               _e_output_primary_update(output);
             else
               e_output_external_update(output);
          }
        break;
       case TDM_OUTPUT_CHANGE_DPMS:
        tdpms = (tdm_output_dpms)value.u32;
        primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        EINA_SAFETY_ON_NULL_RETURN(primary);

        if (tdpms == TDM_OUTPUT_DPMS_OFF)
          {
             edpms = E_OUTPUT_DPMS_OFF;
             if (!override && (output == primary))
               {
                  e_comp_override_add();
                  override = EINA_TRUE;
               }
          }
        else if (tdpms == TDM_OUTPUT_DPMS_ON)
          {
             edpms = E_OUTPUT_DPMS_ON;
             if (override && (output == primary))
               {
                  e_comp_override_del();
                  override = EINA_FALSE;
               }
             _e_output_dpms_on_render(output);
          }
        else if (tdpms == TDM_OUTPUT_DPMS_STANDBY) edpms = E_OUTPUT_DPMS_STANDBY;
        else if (tdpms == TDM_OUTPUT_DPMS_SUSPEND) edpms = E_OUTPUT_DPMS_SUSPEND;
        else edpms = output->dpms;

        output->dpms = edpms;

        _e_output_hook_call(E_OUTPUT_HOOK_DPMS_CHANGE, output);

        break;
       default:
        break;
     }
}

static Eina_Bool
_e_output_fb_over_plane_check(E_Output *output)
{
   Eina_List *p_l;
   E_Plane *ep, *fb;
   Eina_Bool check = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   fb = e_output_default_fb_target_get(output);

   EINA_LIST_REVERSE_FOREACH(output->planes, p_l, ep)
     {
        if (!ep) continue;

        if (ep == fb)
          break;

        if (ep->display_info.tsurface)
          check = EINA_TRUE;
     }

   return check;
}

static void
_e_output_update_fps()
{
   static double time = 0.0;
   static double lapse = 0.0;
   static int cframes = 0;
   static int flapse = 0;

   if (e_comp->calc_fps)
     {
        double dt;
        double tim = ecore_time_get();

        dt = tim - e_comp->frametimes[0];
        e_comp->frametimes[0] = tim;

        time += dt;
        cframes++;

        if (lapse == 0.0)
          {
             lapse = tim;
             flapse = cframes;
          }
        else if ((tim - lapse) >= 0.5)
          {
             e_comp->fps = (cframes - flapse) / (tim - lapse);
             lapse = tim;
             flapse = cframes;
             time = 0.0;
          }
     }
}

EINTERN Eina_Bool
e_output_init(void)
{
   return EINA_TRUE;
}

EINTERN void
e_output_shutdown(void)
{

}

static char *
_output_type_to_str(tdm_output_type output_type)
{
   if (output_type == TDM_OUTPUT_TYPE_Unknown) return "Unknown";
   else if (output_type == TDM_OUTPUT_TYPE_VGA) return "VGA";
   else if (output_type == TDM_OUTPUT_TYPE_DVII) return "DVII";
   else if (output_type == TDM_OUTPUT_TYPE_DVID) return "DVID";
   else if (output_type == TDM_OUTPUT_TYPE_DVIA) return "DVIA";
   else if (output_type == TDM_OUTPUT_TYPE_SVIDEO) return "SVIDEO";
   else if (output_type == TDM_OUTPUT_TYPE_LVDS) return "LVDS";
   else if (output_type == TDM_OUTPUT_TYPE_Component) return "Component";
   else if (output_type == TDM_OUTPUT_TYPE_9PinDIN) return "9PinDIN";
   else if (output_type == TDM_OUTPUT_TYPE_DisplayPort) return "DisplayPort";
   else if (output_type == TDM_OUTPUT_TYPE_HDMIA) return "HDMIA";
   else if (output_type == TDM_OUTPUT_TYPE_HDMIB) return "HDMIB";
   else if (output_type == TDM_OUTPUT_TYPE_TV) return "TV";
   else if (output_type == TDM_OUTPUT_TYPE_eDP) return "eDP";
   else if (output_type == TDM_OUTPUT_TYPE_DSI) return "DSI";
   else return "Unknown";
}

static int
_e_output_cb_planes_sort(const void *d1, const void *d2)
{
   E_Plane *plane1 = (E_Plane *)d1;
   E_Plane *plane2 = (E_Plane *)d2;

   if (!plane1) return(1);
   if (!plane2) return(-1);

   return (plane1->zpos - plane2->zpos);
}

static tdm_capture *
_e_output_tdm_capture_create(E_Output *output, tdm_capture_capability cap)
{
   tdm_error error = TDM_ERROR_NONE;
   tdm_capture *tcapture = NULL;
   tdm_capture_capability capabilities;
   E_Comp_Screen *e_comp_screen = NULL;

   e_comp_screen = e_comp->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);

   error = tdm_display_get_capture_capabilities(e_comp_screen->tdisplay, &capabilities);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(error == TDM_ERROR_NONE, EINA_FALSE);

   if (!(capabilities & cap))
     return NULL;

   tcapture = tdm_output_create_capture(output->toutput, &error);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("create tdm_capture failed", output);
        return NULL;
     }

   return tcapture;
}

static void
_e_output_center_rect_get (int src_w, int src_h, int dst_w, int dst_h, Eina_Rectangle *fit)
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

static Eina_Bool
_e_output_capture_position_get(E_Output *output, int dst_w, int dst_h, Eina_Rectangle *fit, Eina_Bool rotate)
{
   int output_w = 0, output_h = 0;

   e_output_size_get(output, &output_w, &output_h);

   if (output_w == 0 || output_h == 0)
     return EINA_FALSE;

   if (rotate)
     _e_output_center_rect_get(output_h, output_w, dst_w, dst_h, fit);
   else
     _e_output_center_rect_get(output_w, output_h, dst_w, dst_h, fit);

   return EINA_TRUE;
}

static E_Output_Capture *
_e_output_tdm_stream_capture_find_data(E_Output *output, tbm_surface_h tsurface)
{
   E_Output_Capture *cdata = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(output->stream_capture.data, l, cdata)
     {
        if (!cdata) continue;

        if (cdata->surface == tsurface)
          return cdata;
     }

   return NULL;
}

static Eina_Bool
_e_output_tdm_stream_capture_stop(void *data)
{
   E_Output *output = data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, ECORE_CALLBACK_CANCEL);

   if (output->stream_capture.tcapture)
     {
        DBG("output stream capture stop.");
        tdm_capture_destroy(output->stream_capture.tcapture);
        output->stream_capture.tcapture = NULL;
     }

   output->stream_capture.timer = NULL;

   return ECORE_CALLBACK_CANCEL;
}

static void
_e_output_tdm_stream_capture_done_handler(tdm_capture *tcapture,
                                          tbm_surface_h tsurface, void *user_data)
{
   E_Output *output = NULL;
   E_Output_Capture *cdata = NULL;

   output = (E_Output *)user_data;

   tbm_surface_internal_unref(tsurface);

   cdata = _e_output_tdm_stream_capture_find_data(output, tsurface);
   if (cdata)
     {
        output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);
        if (!cdata->dequeued)
          cdata->func(output, tsurface, cdata->data);
        E_FREE(cdata);
     }

   if (!output->stream_capture.start)
     {
        if (eina_list_count(output->stream_capture.data) == 0)
          output->stream_capture.timer = ecore_timer_add((double)1 / DUMP_FPS,
                                         _e_output_tdm_stream_capture_stop, output);
     }
}

static Eina_Bool
_e_output_tdm_capture_info_set(E_Output *output, tdm_capture *tcapture, tbm_surface_h tsurface,
                               tdm_capture_type type, Eina_Bool auto_rotate)
{
   tdm_error error = TDM_ERROR_NONE;
   tdm_info_capture capture_info;
   tbm_error_e tbm_error = TBM_ERROR_NONE;
   tbm_surface_info_s surf_info;
   Eina_Rectangle dst_pos;
   unsigned int width;
   int angle = 0;
   int output_angle = 0;
   Eina_Bool ret;
   Eina_Bool rotate_check = EINA_FALSE;

   tbm_error = tbm_surface_get_info(tsurface, &surf_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tbm_error == TBM_ERROR_NONE, EINA_FALSE);

   width = _e_output_aligned_width_get(output, tsurface);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(width == 0, EINA_FALSE);

   memset(&capture_info, 0, sizeof(tdm_info_capture));
   capture_info.dst_config.size.h = width;
   capture_info.dst_config.size.v = surf_info.height;
   capture_info.dst_config.format = surf_info.format;
   capture_info.transform = TDM_TRANSFORM_NORMAL;

   angle = _e_output_top_ec_angle_get();
   output_angle = output->config.rotation;

   if (auto_rotate &&
      (((angle + output_angle) % 360 == 90) || ((angle + output_angle) % 360 == 270)))
     rotate_check = EINA_TRUE;

   ret = _e_output_capture_position_get(output, surf_info.width, surf_info.height, &dst_pos, rotate_check);
   if (ret)
     {
        capture_info.dst_config.pos.x = dst_pos.x;
        capture_info.dst_config.pos.y = dst_pos.y;
        capture_info.dst_config.pos.w = dst_pos.w;
        capture_info.dst_config.pos.h = dst_pos.h;

        if (rotate_check)
          {
             int tmp = (angle + output_angle) % 360;
             if (tmp == 90)
               capture_info.transform = TDM_TRANSFORM_90;
             else if (tmp == 180)
               capture_info.transform = TDM_TRANSFORM_180;
             else if (tmp == 270)
               capture_info.transform = TDM_TRANSFORM_270;
          }
        else if (auto_rotate && output_angle == 90)
          capture_info.transform = TDM_TRANSFORM_90;
        else if (auto_rotate && output_angle == 180)
          capture_info.transform = TDM_TRANSFORM_180;
        else if (auto_rotate && output_angle == 270)
          capture_info.transform = TDM_TRANSFORM_270;
     }
   else
     {
        capture_info.dst_config.pos.x = 0;
        capture_info.dst_config.pos.y = 0;
        capture_info.dst_config.pos.w = surf_info.width;
        capture_info.dst_config.pos.h = surf_info.height;
     }

   capture_info.type = type;

   error = tdm_capture_set_info(tcapture, &capture_info);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("tdm_capture set_info failed", output);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_output_tdm_capture_done_handler(tdm_capture *tcapture, tbm_surface_h tsurface, void *user_data)
{
   E_Output *output = NULL;
   E_Output_Capture *cdata = NULL;

   cdata = (E_Output_Capture *)user_data;
   output = cdata->output;

   tbm_surface_internal_unref(tsurface);

   cdata->func(output, tsurface, cdata->data);

   tdm_capture_destroy(cdata->tcapture);

   E_FREE(cdata);

   DBG("tdm_capture done.(%p)", tsurface);
}

static Eina_Bool
_e_output_tdm_capture(E_Output *output, tdm_capture *tcapture,
                      tbm_surface_h tsurface, E_Output_Capture_Cb func, void *data)
{
   tdm_error error = TDM_ERROR_NONE;
   E_Output_Capture *cdata = NULL;

   cdata = E_NEW(E_Output_Capture, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, EINA_FALSE);

   cdata->output = output;
   cdata->tcapture = tcapture;
   cdata->data = data;
   cdata->func = func;

   tbm_surface_internal_ref(tsurface);

   error = tdm_capture_set_done_handler(tcapture, _e_output_tdm_capture_done_handler, cdata);
   EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

   error = tdm_capture_attach(tcapture, tsurface);
   EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

   error = tdm_capture_commit(tcapture);
   EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

   return EINA_TRUE;

fail:
   tbm_surface_internal_unref(tsurface);

   if (cdata)
     E_FREE(cdata);

   return EINA_FALSE;
}

static Eina_Bool
_e_output_stream_capture_cb_timeout(void *data)
{
   E_Output *output = data;
   E_Output_Capture *cdata = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_GOTO(output, done);

   if (!output->stream_capture.start)
     {
        EINA_LIST_FREE(output->stream_capture.data, cdata)
          {
             tbm_surface_internal_unref(cdata->surface);

             E_FREE(cdata);
          }

        if (output->stream_capture.tcapture)
          {
             tdm_capture_destroy(output->stream_capture.tcapture);
             output->stream_capture.tcapture = NULL;
          }

        DBG("output stream capture stop.");

        output->stream_capture.timer = NULL;

        return ECORE_CALLBACK_CANCEL;
     }

   EINA_LIST_FOREACH(output->stream_capture.data, l, cdata)
     {
        if (!cdata->in_using) break;
     }

   /* can be null when client doesn't queue a buffer previously */
   if (!cdata)
     goto done;

   cdata->in_using = EINA_TRUE;

   tbm_surface_internal_unref(cdata->surface);

   output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);

   cdata->func(output, cdata->surface, cdata->data);
   E_FREE(cdata);

done:
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_output_tdm_stream_capture(E_Output *output, tdm_capture *tcapture,
                             tbm_surface_h tsurface, E_Output_Capture_Cb func, void *data)
{
   tdm_error error = TDM_ERROR_NONE;
   E_Output_Capture *cdata = NULL;
   E_Output_Capture *tmp_cdata = NULL;
   Eina_List *l, *ll;

   cdata = E_NEW(E_Output_Capture, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, EINA_FALSE);

   cdata->output = output;
   cdata->tcapture = tcapture;
   cdata->surface = tsurface;
   cdata->data = data;
   cdata->func = func;

   tbm_surface_internal_ref(tsurface);

   output->stream_capture.data = eina_list_append(output->stream_capture.data, cdata);

   if (output->stream_capture.start)
     {
        if (e_output_dpms_get(output))
          {
             if (!output->stream_capture.timer)
               output->stream_capture.timer = ecore_timer_add((double)1 / DUMP_FPS,
                                                              _e_output_stream_capture_cb_timeout, output);
             EINA_SAFETY_ON_NULL_RETURN_VAL(output->stream_capture.timer, EINA_FALSE);

             return EINA_TRUE;
          }
        else if (output->stream_capture.timer)
          {
             ecore_timer_del(output->stream_capture.timer);
             output->stream_capture.timer = NULL;

             EINA_LIST_FOREACH_SAFE(output->stream_capture.data, l, ll, tmp_cdata)
               {
                  if (!tmp_cdata) continue;

                  if (!tmp_cdata->in_using)
                    {
                       tmp_cdata->in_using = EINA_TRUE;

                       error = tdm_capture_attach(tcapture, tsurface);
                       if (error != TDM_ERROR_NONE)
                         {
                            EOERR("tdm_capture_attach fail", output);
                            output->stream_capture.data = eina_list_remove_list(output->stream_capture.data, l);
                            tmp_cdata->func(tmp_cdata->output, tmp_cdata->surface, tmp_cdata->data);
                            E_FREE(tmp_cdata);

                            return EINA_FALSE;
                         }
                    }
               }
             error = tdm_capture_commit(tcapture);
             if (error != TDM_ERROR_NONE)
               {
                  EOERR("tdm_capture_commit fail", output);
                  return EINA_FALSE;
               }
          }
        else
          {
             cdata->in_using = EINA_TRUE;

             error = tdm_capture_attach(tcapture, tsurface);
             EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

             error = tdm_capture_commit(tcapture);
             EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);
          }
     }
   else
     {
        if (e_output_dpms_get(output))
          return EINA_TRUE;

        cdata->in_using = EINA_TRUE;

        error = tdm_capture_attach(tcapture, tsurface);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);
     }

   return EINA_TRUE;

fail:
   output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);

   tbm_surface_internal_unref(tsurface);

   if (cdata)
     E_FREE(cdata);

   return EINA_FALSE;
}

static Eina_Bool
_e_output_watch_vblank_timer(void *data)
{
   E_Output *output = data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, ECORE_CALLBACK_RENEW);

   _e_output_vblank_handler(NULL, 0, 0, 0, (void *)output);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_output_watch_vblank(E_Output *output)
{
   tdm_error ret;
   int per_vblank;

   /* If not DPMS_ON, we call vblank handler directly to dump screen */
   if (e_output_dpms_get(output))
     {
        if (!output->stream_capture.timer)
          output->stream_capture.timer = ecore_timer_add((double)1 / DUMP_FPS,
                                                         _e_output_watch_vblank_timer, output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(output->stream_capture.timer, EINA_FALSE);

        return EINA_TRUE;
     }
   else if (output->stream_capture.timer)
     {
        ecore_timer_del(output->stream_capture.timer);
        output->stream_capture.timer = NULL;
     }

   if (output->stream_capture.wait_vblank)
     return EINA_TRUE;

   per_vblank = output->config.mode.refresh / DUMP_FPS;
   if (per_vblank == 0)
     per_vblank = 1;

   ret = tdm_output_wait_vblank(output->toutput, per_vblank, 0,
                                _e_output_vblank_handler, output);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);

   output->stream_capture.wait_vblank = EINA_TRUE;

   return EINA_TRUE;
}

static void
_e_output_vblank_handler(tdm_output *toutput, unsigned int sequence,
                         unsigned int tv_sec, unsigned int tv_usec, void *data)
{
   E_Output *output = data;
   E_Output_Capture *cdata = NULL;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN(output);

   output->stream_capture.wait_vblank = EINA_FALSE;

   if (!output->stream_capture.start)
     {
        EINA_LIST_FREE(output->stream_capture.data, cdata)
          {
             tbm_surface_internal_unref(cdata->surface);

             E_FREE(cdata);
          }

        if (output->stream_capture.timer)
          {
             ecore_timer_del(output->stream_capture.timer);
             output->stream_capture.timer = NULL;
          }
        DBG("output stream capture stop.");

        return;
     }

   EINA_LIST_FOREACH(output->stream_capture.data, l, cdata)
     {
        if (!cdata->in_using) break;
     }

   /* can be null when client doesn't queue a buffer previously */
   if (!cdata)
     return;

   output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);

   ret = _e_output_capture(output, cdata->surface, EINA_FALSE);
   if (ret == EINA_FALSE)
     EOERR("capture fail", output);

   tbm_surface_internal_unref(cdata->surface);

   cdata->func(output, cdata->surface, cdata->data);

   E_FREE(cdata);

   /* timer is a substitution for vblank during dpms off. so if timer is running,
    * we don't watch vblank events recursively.
    */
   if (!output->stream_capture.timer)
     _e_output_watch_vblank(output);
}

static Eina_Bool
_e_output_vblank_stream_capture(E_Output *output, tbm_surface_h tsurface,
                                E_Output_Capture_Cb func, void *data)
{
   E_Output_Capture *cdata = NULL;
   Eina_Bool ret = EINA_FALSE;

   cdata = E_NEW(E_Output_Capture, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(cdata, EINA_FALSE);

   cdata->output = output;
   cdata->surface = tsurface;
   cdata->data = data;
   cdata->func = func;

   tbm_surface_internal_ref(tsurface);

   output->stream_capture.data = eina_list_append(output->stream_capture.data, cdata);

   if (output->stream_capture.start)
     {
        ret = _e_output_watch_vblank(output);
        if (ret == EINA_FALSE)
          {
             output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);
             tbm_surface_internal_unref(tsurface);
             E_FREE(cdata);

             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

static void
_e_output_capture_showing_rect_get(Eina_Rectangle *out_rect, Eina_Rectangle *dst_rect, Eina_Rectangle *showing_rect)
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
_e_output_capture_src_crop_get(E_Output *output, tdm_layer *layer, Eina_Rectangle *fit, Eina_Rectangle *showing_rect)
{
   tdm_info_layer info;
   tdm_error error = TDM_ERROR_NONE;
   const tdm_output_mode *mode = NULL;
   float ratio_x, ratio_y;
   Eina_Rectangle out_rect;
   Eina_Rectangle dst_rect;

   fit->x = 0;
   fit->y = 0;
   fit->w = 0;
   fit->h = 0;

   error = tdm_output_get_mode(output->toutput, &mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(error == TDM_ERROR_NONE, EINA_FALSE);

   out_rect.x = 0;
   out_rect.y = 0;
   out_rect.w = mode->hdisplay;
   out_rect.h = mode->vdisplay;

   error = tdm_layer_get_info(layer, &info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(error == TDM_ERROR_NONE, EINA_FALSE);

   dst_rect.x = info.dst_pos.x;
   dst_rect.y = info.dst_pos.y;
   dst_rect.w = info.dst_pos.w;
   dst_rect.h = info.dst_pos.h;

   _e_output_capture_showing_rect_get(&out_rect, &dst_rect, showing_rect);

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

static void
_e_output_capture_dst_crop_get(E_Output *output, E_Comp_Wl_Video_Buf *tmp, E_Comp_Wl_Video_Buf *dst, tdm_layer *layer,
                               int w, int h, Eina_Rectangle *pos, Eina_Rectangle *showing_pos,
                               Eina_Rectangle *dst_crop, int rotate)
{
   tdm_info_layer info;
   tdm_error error = TDM_ERROR_NONE;

   dst_crop->x = 0;
   dst_crop->y = 0;
   dst_crop->w = 0;
   dst_crop->h = 0;

   error = tdm_layer_get_info(layer, &info);
   EINA_SAFETY_ON_FALSE_RETURN(error == TDM_ERROR_NONE);

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
        EOERR("get_cropinfo: unknown case error", output);
     }
}

static Eina_Bool
_e_output_capture_src_crop_get_hwc_window(E_Output *output, E_Hwc_Window *hwc_window, Eina_Rectangle *fit, Eina_Rectangle *showing_rect)
{
   tdm_error error = TDM_ERROR_NONE;
   const tdm_output_mode *mode = NULL;
   float ratio_x, ratio_y;
   Eina_Rectangle out_rect;
   Eina_Rectangle dst_rect;

   fit->x = 0;
   fit->y = 0;
   fit->w = 0;
   fit->h = 0;

   error = tdm_output_get_mode(output->toutput, &mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(error == TDM_ERROR_NONE, EINA_FALSE);

   out_rect.x = 0;
   out_rect.y = 0;
   out_rect.w = mode->hdisplay;
   out_rect.h = mode->vdisplay;

   dst_rect.x = hwc_window->info.dst_pos.x;
   dst_rect.y = hwc_window->info.dst_pos.y;
   dst_rect.w = hwc_window->info.dst_pos.w;
   dst_rect.h = hwc_window->info.dst_pos.h;

   _e_output_capture_showing_rect_get(&out_rect, &dst_rect, showing_rect);

   fit->x = hwc_window->info.src_config.pos.x;
   fit->y = hwc_window->info.src_config.pos.y;

   if (hwc_window->info.transform % 2 == 0)
     {
        ratio_x = (float)hwc_window->info.src_config.pos.w / dst_rect.w;
        ratio_y = (float)hwc_window->info.src_config.pos.h / dst_rect.h;

        fit->w = showing_rect->w * ratio_x;
        fit->h = showing_rect->h * ratio_y;
     }
   else
     {
        ratio_x = (float)hwc_window->info.src_config.pos.w / dst_rect.h;
        ratio_y = (float)hwc_window->info.src_config.pos.h / dst_rect.w;

        fit->w = showing_rect->h * ratio_x;
        fit->h = showing_rect->w * ratio_y;
     }

   return EINA_TRUE;
}

static void
_e_output_capture_dst_crop_get_hwc_window(E_Output *output, E_Hwc_Window *hwc_window, E_Comp_Wl_Video_Buf *tmp, E_Comp_Wl_Video_Buf *dst,
                               int w, int h, Eina_Rectangle *pos, Eina_Rectangle *showing_pos,
                               Eina_Rectangle *dst_crop, int rotate)
{
   dst_crop->x = 0;
   dst_crop->y = 0;
   dst_crop->w = 0;
   dst_crop->h = 0;

   if (hwc_window->info.src_config.pos.w == w && hwc_window->info.src_config.pos.h == h &&
       pos->x == 0 && pos->y == 0 && pos->w == tmp->width && pos->h == tmp->height)
     {
        dst_crop->x = pos->x;
        dst_crop->y = pos->y;
        dst_crop->w = pos->w;
        dst_crop->h = pos->h;
     }
   else if ((w == pos->w) && (h == pos->h) && (showing_pos->w == pos->w) && (showing_pos->h == pos->h))
     {
        dst_crop->x = hwc_window->info.dst_pos.x + pos->x;
        dst_crop->y = hwc_window->info.dst_pos.y + pos->y;
        dst_crop->w = hwc_window->info.dst_pos.w;
        dst_crop->h = hwc_window->info.dst_pos.h;
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
        EOERR("get_cropinfo: unknown case error", output);
     }
}

static int
_e_output_layer_sort_cb(const void *d1, const void *d2)
{
   E_Output_Layer *e_layer_1 = (E_Output_Layer *)d1;
   E_Output_Layer *e_layer_2 = (E_Output_Layer *)d2;

   if (!e_layer_1) return(1);
   if (!e_layer_2) return(-1);

   return (e_layer_1->zpos - e_layer_2->zpos);
}

static Eina_Bool
_e_output_vbuf_capture_hwc_window(E_Output *output, E_Comp_Wl_Video_Buf *vbuf, int rotate, Eina_Bool rotate_check)
{
   int width = 0, height = 0;
   E_Hwc *hwc = NULL;
   E_Hwc_Window *hwc_window = NULL;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   e_output_size_get(output, &width, &height);
   if (width == 0 || height == 0)
     return ret;

   hwc = output->hwc;
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);

   EINA_LIST_FOREACH(hwc->hwc_windows, l, hwc_window)
     {
        E_Comp_Wl_Video_Buf *tmp = NULL;
        tbm_surface_h surface = NULL;
        Eina_Rectangle showing_pos = {0, };
        Eina_Rectangle dst_pos = {0, };
        Eina_Rectangle src_crop = {0, };
        Eina_Rectangle dst_crop = {0, };

        if (!hwc_window) continue;
        if (e_hwc_window_is_video(hwc_window)) continue;

        surface = e_hwc_window_displaying_surface_get(hwc_window);
        if (!surface) continue;

        tmp = e_comp_wl_video_buffer_create_tbm(surface);
        if (tmp == NULL) continue;

        ret = _e_output_capture_src_crop_get_hwc_window(output, hwc_window, &src_crop, &showing_pos);
        if (ret == EINA_FALSE)
          {
             e_comp_wl_video_buffer_unref(tmp);
             continue;
          }

        ret = _e_output_capture_position_get(output, vbuf->width, vbuf->height, &dst_pos, rotate_check);
        if (ret == EINA_FALSE)
          {
             e_comp_wl_video_buffer_unref(tmp);
             continue;
          }

        _e_output_capture_dst_crop_get_hwc_window(output, hwc_window, tmp, vbuf, width, height,
                                       &dst_pos, &showing_pos, &dst_crop, rotate);

        e_comp_wl_video_buffer_convert(tmp, vbuf,
                                       src_crop.x, src_crop.y, src_crop.w, src_crop.h,
                                       dst_crop.x, dst_crop.y, dst_crop.w, dst_crop.h,
                                       EINA_TRUE, rotate, 0, 0);

        e_comp_wl_video_buffer_unref(tmp);
     }

   ret = EINA_TRUE;

   return ret;
}
static Eina_Bool
_e_output_vbuf_capture(E_Output *output, E_Comp_Wl_Video_Buf *vbuf, int rotate, Eina_Bool rotate_check)
{
   tdm_error error = TDM_ERROR_NONE;
   int width = 0, height = 0;
   int i, count;
   E_Output_Layer *e_layer = NULL;
   Eina_List *layers = NULL;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   e_output_size_get(output, &width, &height);
   if (width == 0 || height == 0)
     return ret;

   error = tdm_output_get_layer_count(output->toutput, &count);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(error == TDM_ERROR_NONE, ret);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(count >= 0, ret);

   for (i = 0; i < count; i++)
     {
        int zpos;
        tdm_layer *layer;
        E_Output_Layer *e_layer = E_NEW(E_Output_Layer, 1);
        EINA_SAFETY_ON_NULL_GOTO(e_layer, release);

        layers = eina_list_append(layers, e_layer);

        layer = tdm_output_get_layer(output->toutput, i, &error);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, release);

        tdm_layer_get_zpos(layer, &zpos);
        e_layer->layer = layer;
        e_layer->zpos = zpos;
     }
   layers = eina_list_sort(layers, eina_list_count(layers), _e_output_layer_sort_cb);

   EINA_LIST_FOREACH(layers, l, e_layer)
     {
        E_Comp_Wl_Video_Buf *tmp = NULL;
        tdm_layer *layer;
        tdm_layer_capability capability;
        tbm_surface_h surface = NULL;
        Eina_Rectangle showing_pos = {0, };
        Eina_Rectangle dst_pos = {0, };
        Eina_Rectangle src_crop = {0, };
        Eina_Rectangle dst_crop = {0, };
        Eina_Bool ret;

        if (!e_layer) continue;

        if (e_layer->zpos < 0) continue;
        layer = e_layer->layer;

        error = tdm_layer_get_capabilities(layer, &capability);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, release);

        if (capability & TDM_LAYER_CAPABILITY_VIDEO)
          continue;

        surface = tdm_layer_get_displaying_buffer(layer, &error);
        if (surface == NULL)
          continue;

        tmp = e_comp_wl_video_buffer_create_tbm(surface);
        if (tmp == NULL)
          continue;

        ret = _e_output_capture_src_crop_get(output, layer, &src_crop, &showing_pos);
        if (ret == EINA_FALSE)
          {
             e_comp_wl_video_buffer_unref(tmp);
             continue;
          }

        ret = _e_output_capture_position_get(output, vbuf->width, vbuf->height, &dst_pos, rotate_check);
        if (ret == EINA_FALSE)
          {
             e_comp_wl_video_buffer_unref(tmp);
             continue;
          }

        _e_output_capture_dst_crop_get(output, tmp, vbuf, layer, width, height,
                                       &dst_pos, &showing_pos, &dst_crop, rotate);

        e_comp_wl_video_buffer_convert(tmp, vbuf,
                                       src_crop.x, src_crop.y, src_crop.w, src_crop.h,
                                       dst_crop.x, dst_crop.y, dst_crop.w, dst_crop.h,
                                       EINA_TRUE, rotate, 0, 0);

        e_comp_wl_video_buffer_unref(tmp);
     }

   ret = EINA_TRUE;

release:
   if (layers)
     {
        E_Output_Layer *e_layer = NULL;
        Eina_List *l, *ll;

        EINA_LIST_FOREACH_SAFE(layers, l, ll, e_layer)
          {
             E_FREE(e_layer);
          }
        eina_list_free(layers);
     }

   return ret;
}

static Eina_Bool
_e_output_capture(E_Output *output, tbm_surface_h tsurface, Eina_Bool auto_rotate)
{
   E_Comp_Wl_Video_Buf *vbuf = NULL;
   Eina_Bool ret = EINA_FALSE;
   int angle = 0;
   int output_angle = 0;
   int rotate = 0;
   Eina_Bool rotate_check = EINA_FALSE;

   vbuf = e_comp_wl_video_buffer_create_tbm(tsurface);
   EINA_SAFETY_ON_NULL_RETURN_VAL(vbuf, EINA_FALSE);

   e_comp_wl_video_buffer_clear(vbuf);

   angle = _e_output_top_ec_angle_get();
   output_angle = output->config.rotation;

   if (auto_rotate &&
      ((angle + output_angle) % 360 == 90 || (angle + output_angle) % 360 == 270))
     rotate_check = EINA_TRUE;

   if (rotate_check)
     {
        int tmp = (angle + output_angle) % 360;

        if (tmp == 90)
          rotate = 90;
        else if (tmp == 180)
          rotate = 180;
        else if (tmp == 270)
          rotate = 270;
     }
   else if (auto_rotate && output_angle == 90)
     rotate = 90;
   else if (auto_rotate && output_angle == 180)
     rotate = 180;
   else if (auto_rotate && output_angle == 270)
     rotate = 270;

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_WINDOWS)
     ret = _e_output_vbuf_capture_hwc_window(output, vbuf, rotate, rotate_check);
   else
     ret = _e_output_vbuf_capture(output, vbuf, rotate, rotate_check);

   e_comp_wl_video_buffer_unref(vbuf);

   return ret;
}

static void
_e_output_tdm_stream_capture_support(E_Output *output)
{
   tdm_error error = TDM_ERROR_NONE;
   tdm_capture_capability capabilities;
   E_Comp_Screen *e_comp_screen = NULL;

   e_comp_screen = e_comp->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN(e_comp_screen);

   error = tdm_display_get_capture_capabilities(e_comp_screen->tdisplay, &capabilities);
   if (error != TDM_ERROR_NONE)
     {
        EOINF("TDM Display has no capture capability.", output);
        return;
     }

   if (capabilities & TDM_CAPTURE_CAPABILITY_STREAM)
     output->stream_capture.possible_tdm_capture = EINA_TRUE;
}

static void
_e_output_external_rect_get(E_Output *output, int src_w, int src_h, int dst_w, int dst_h, Eina_Rectangle *rect)
{
   int angle = 0;
   int output_angle = 0;
   Eina_Bool rotate_check = EINA_FALSE;

   angle = _e_output_top_ec_angle_get();
   output_angle = output->config.rotation;

   if (((angle + output_angle) % 360 == 90) || ((angle + output_angle) % 360 == 270))
     rotate_check = EINA_TRUE;

   if (rotate_check)
     _e_output_center_rect_get(src_h, src_w, dst_w, dst_h, rect);
   else
     _e_output_center_rect_get(src_w, src_h, dst_w, dst_h, rect);
}

static Eina_Bool
_e_output_planes_commit(E_Output *output)
{
   E_Plane *plane = NULL, *fb_target = NULL;
   Eina_List *l;
   Eina_Bool fb_commit = EINA_FALSE;

   fb_target = e_output_fb_target_get(output);

   /* fetch the fb_target at first */
   fb_commit = e_plane_fetch(fb_target);
   // TODO: to be fixed. check fps of fb_target currently.
   if (fb_commit) _e_output_update_fps();

   if (output->zoom_conf.unset_skip == EINA_TRUE)
     {
        output->zoom_conf.unset_skip = EINA_FALSE;
        if (output->dpms != E_OUTPUT_DPMS_OFF)
          {
             e_output_zoom_rotating_check(output);
             if (!e_plane_pp_commit(fb_target))
               EOERR("fail to e_plane_pp_commit", output);
             return EINA_TRUE;
          }
     }

   /* set planes */
   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        /* skip the fb_target fetch because we do this previously */
        if (e_plane_is_fb_target(plane)) continue;

        /* if the plane is the candidate to unset,
           set the plane to be unset_try */
        if (e_plane_is_unset_candidate(plane))
          e_plane_unset_try_set(plane, EINA_TRUE);

        /* if the plane is trying to unset,
         * 1. if fetching the fb is not available, continue.
         * 2. if fetching the fb is available, verify the unset commit check.  */
        if (e_plane_is_unset_try(plane))
          {
            if (!e_plane_unset_commit_check(plane, fb_commit))
              continue;
          }

        if (!e_plane_set_commit_check(plane, fb_commit)) continue;

        /* fetch the surface to the plane */
        if (!e_plane_fetch(plane)) continue;

        if (e_plane_is_unset_try(plane))
          e_plane_unset_try_set(plane, EINA_FALSE);
     }

   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        if (e_plane_is_fetch_retry(plane))
          {
             if (!e_plane_fetch(plane)) continue;
             if (e_plane_is_fb_target(plane))
               {
                  fb_commit = EINA_TRUE;
                  _e_output_update_fps();
               }
          }
     }

   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        if (e_plane_is_unset_try(plane)) continue;

        if ((output->dpms == E_OUTPUT_DPMS_OFF) || output->fake_config)
          {
             if (!e_plane_offscreen_commit(plane))
               EOERR("fail to e_plane_offscreen_commit", output);
          }
        else
          {
             if ((output->zoom_set) && e_plane_is_fb_target(plane))
               {
                  e_output_zoom_rotating_check(output);
                  if (!e_plane_pp_commit(plane))
                    EOERR("fail to e_plane_pp_commit", output);
               }
             else
               {
                  if (!e_plane_commit(plane))
                    EOERR("fail to e_plane_commit", output);
               }
          }
     }

   return EINA_TRUE;

}

static Eina_Bool
_e_output_planes_init(E_Output *output)
{
   E_Plane *plane = NULL;
   E_Plane *default_fb = NULL;
   tdm_output *toutput = output->toutput;
   int num_layers, i;

   tdm_output_get_layer_count(toutput, &num_layers);
   if (num_layers < 1)
     {
        EOERR("fail to get tdm_output_get_layer_count\n", output);
        goto fail;
     }
   output->plane_count = num_layers;
   EOINF("num_planes %i", output, output->plane_count);

   if (!e_plane_init())
     {
        EOERR("fail to e_plane_init.", output);
        goto fail;
     }

   for (i = 0; i < output->plane_count; i++)
     {
        plane = e_plane_new(output, i);
        if (!plane)
          {
             EOERR("fail to create the e_plane.", output);
             goto fail;
          }
        output->planes = eina_list_append(output->planes, plane);
     }

   output->planes = eina_list_sort(output->planes, eina_list_count(output->planes), _e_output_cb_planes_sort);

   default_fb = e_output_default_fb_target_get(output);
   if (!default_fb)
     {
        EOERR("fail to get default_fb_target plane", output);
        goto fail;
     }

   if (!e_plane_fb_target_set(default_fb, EINA_TRUE))
     {
        EOERR("fail to set fb_target plane", output);
        goto fail;
     }

   return EINA_TRUE;

fail:
   return EINA_FALSE;
}

EINTERN E_Output *
e_output_new(E_Comp_Screen *e_comp_screen, int index)
{
   E_Output *output = NULL;
   tdm_output *toutput = NULL;
   tdm_error error;
   char *id = NULL;
   char *name;
   int size = 0;
   tdm_output_type output_type;
   int min_w, min_h, max_w, max_h, preferred_align;
   tdm_output_capability output_caps = 0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, NULL);

   output = E_NEW(E_Output, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   output->index = index;

   toutput = tdm_display_get_output(e_comp_screen->tdisplay, index, NULL);
   if (!toutput) goto fail;
   output->toutput = toutput;

   error = tdm_output_add_change_handler(toutput, _e_output_cb_output_change, output);
   if (error != TDM_ERROR_NONE)
     WRN("fail to tdm_output_add_change_handler");

   error = tdm_output_get_output_type(toutput, &output_type);
   if (error != TDM_ERROR_NONE) goto fail;
   output->toutput_type = output_type;

   error = tdm_output_get_cursor_available_size(toutput, &min_w, &min_h, &max_w, &max_h, &preferred_align);
   if (error == TDM_ERROR_NONE)
     {
        output->cursor_available.min_w = min_w;
        output->cursor_available.min_h = min_h;
        output->cursor_available.max_w = min_w;
        output->cursor_available.max_h = min_h;
        output->cursor_available.preferred_align = preferred_align;
     }
   else
     {
        output->cursor_available.min_w = -1;
        output->cursor_available.min_h = -1;
        output->cursor_available.max_w = -1;
        output->cursor_available.max_h = -1;
        output->cursor_available.preferred_align = -1;
     }

   name = _output_type_to_str(output_type);
   size = strlen(name) + 4;

   id = calloc(1, size);
   if (!id) goto fail;
   snprintf(id, size, "%s-%d", name, index);

   output->id = id;
   EOINF("(%d) output_id = %s", output, index, output->id);

   output->e_comp_screen = e_comp_screen;

   _e_output_tdm_stream_capture_support(output);

   error = tdm_output_get_capabilities(toutput, &output_caps);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("fail to tdm_output_get_capabilities", output);
        goto fail;
     }

   /* The E20 works the hwc_windows policy when tdm_output supports hwc capability e20.
    * The E_Plane, E_Plane_Renderer resource is not used in E20.
    */
   if (output_caps & TDM_OUTPUT_CAPABILITY_HWC)
     output->tdm_hwc = EINA_TRUE;
   else
     if (!_e_output_planes_init(output))
       goto fail;

   if (output_caps & TDM_OUTPUT_CAPABILITY_ASYNC_DPMS)
     output->dpms_async = EINA_TRUE;

   if (output_caps & TDM_OUTPUT_CAPABILITY_MIRROR)
     output->tdm_mirror = EINA_TRUE;

   /* call output add hook */
   _e_output_hook_call(E_OUTPUT_HOOK_ADD, output);

   return output;

fail:
   if (output) e_output_del(output);

   return NULL;
}

EINTERN void
e_output_del(E_Output *output)
{
   E_Plane *plane;
   E_Output_Mode *m;

   if (!output) return;

   /* call output remove hook */
   _e_output_hook_call(E_OUTPUT_HOOK_REMOVE, output);

   if (output->hwc) e_hwc_del(output->hwc);

   e_plane_shutdown();

   if (output->id) free(output->id);
   if (output->info.screen) free(output->info.screen);
   if (output->info.name) free(output->info.name);
   if (output->info.edid) free(output->info.edid);

   tdm_output_remove_change_handler(output->toutput, _e_output_cb_output_change, output);

   EINA_LIST_FREE(output->info.modes, m) free(m);

   EINA_LIST_FREE(output->planes, plane) e_plane_free(plane);
   free(output);
}

EINTERN Eina_Bool
e_output_rotate(E_Output *output, int rotate)
{
   unsigned int transform = WL_OUTPUT_TRANSFORM_NORMAL;
   int rot_dif;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   /* FIXME: currently the screen size can't be changed in runtime. To make it
    * possible, the output mode should be changeable first.
    */
   rot_dif = output->config.rotation - rotate;
   if (rot_dif < 0) rot_dif = -rot_dif;

   if ((rot_dif % 180) && (output->config.geom.w != output->config.geom.h))
     {
        EOERR("output size(%dx%d) should be square.", output,
            output->config.geom.w, output->config.geom.h);
        return EINA_FALSE;
     }

   switch (rotate)
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

   output->config.rotation = rotate;

   e_comp_wl_output_init(output->id, output->info.name,
                         output->info.screen,
                         output->config.geom.x, output->config.geom.y,
                         output->config.geom.w, output->config.geom.h,
                         output->info.size.w, output->info.size.h,
                         output->config.mode.refresh, 0, transform);

   ELOGF("TRANSFORM", "output(%s) transform(%d)", NULL, output->info.name, transform);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_update(E_Output *output)
{
   E_Output_Mode *m = NULL;
   Eina_List *modes = NULL;
   Eina_Bool connected = EINA_TRUE;
   tdm_error error;
   tdm_output_conn_status status;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   error = tdm_output_get_conn_status(output->toutput, &status);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("failt to get conn status.", output);
        return EINA_FALSE;
     }

   if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED) connected = EINA_FALSE;

   if (connected)
     {
        /* disconnect --> connect */
        if (connected != output->info.connected)
          {
             char *name;
             const char *screen;
             const char *maker;
             unsigned int phy_w, phy_h;
             const tdm_output_mode *tmodes = NULL;
             int num_tmodes = 0;
             unsigned int pipe = 0;
             int size = 0;

             error = tdm_output_get_model_info(output->toutput, &maker, &screen, NULL);
             if (error != TDM_ERROR_NONE)
               {
                  EOERR("fail to get model info.", output);
                  return EINA_FALSE;
               }

             /* we apply the screen rotation only for the primary output */
             error = tdm_output_get_pipe(output->toutput, &pipe);
             if (error == TDM_ERROR_NONE && pipe == 0)
               output->config.rotation = e_comp->e_comp_screen->rotation;

             if (maker)
               {
                  size = strlen(output->id) + 1 + strlen(maker) + 1;
                  name = calloc(1, size);
                  if (!name) return EINA_FALSE;
                  snprintf(name, size, "%s-%s", output->id, maker);
               }
             else
               {
                  size = strlen(output->id) + 1;
                  name = calloc(1, size);
                  if (!name) return EINA_FALSE;
                  snprintf(name, size, "%s", output->id);
               }
             EOINF("screen = %s, name = %s", output, screen, name);

             error = tdm_output_get_physical_size(output->toutput, &phy_w, &phy_h);
             if (error != TDM_ERROR_NONE)
               {
                  EOERR("fail to get physical_size.", output);
                  free(name);
                  return EINA_FALSE;
               }

             error = tdm_output_get_available_modes(output->toutput, &tmodes, &num_tmodes);
             if (error != TDM_ERROR_NONE || num_tmodes == 0)
               {
                  EOERR("fail to get tmodes", output);
                  free(name);
                  return EINA_FALSE;
               }

             for (i = 0; i < num_tmodes; i++)
               {
                  E_Output_Mode *rmode;

                  rmode = E_NEW(E_Output_Mode, 1);
                  if (!rmode) continue;

                  if (tmodes[i].type & TDM_OUTPUT_MODE_TYPE_PREFERRED)
                     rmode->preferred = EINA_TRUE;

                  rmode->w = tmodes[i].hdisplay;
                  rmode->h = tmodes[i].vdisplay;
                  rmode->refresh = tmodes[i].vrefresh;
                  rmode->tmode = &tmodes[i];

                  modes = eina_list_append(modes, rmode);
               }

             /* resetting the output->info */
             if (output->info.screen) free(output->info.screen);
             if (output->info.name) free(output->info.name);
             EINA_LIST_FREE(output->info.modes, m) free(m);

             output->info.screen = strdup(screen);
             output->info.name = name;
             output->info.modes = modes;
             output->info.size.w = phy_w;
             output->info.size.h = phy_h;

             output->info.connected = EINA_TRUE;

             EOINF("id(%s) connected..", output, output->id);
          }

#if 0
        /* check the crtc setting */
        if (status != TDM_OUTPUT_CONN_STATUS_MODE_SETTED)
          {
              const tdm_output_mode *mode = NULL;

              error = tdm_output_get_mode(output->toutput, &mode);
              if (error != TDM_ERROR_NONE || mode == NULL)
                {
                   EOERR("fail to get mode.", output);
                   return EINA_FALSE;
                }

              output->config.geom.x = 0;
              output->config.geom.y = 0;
              output->config.geom.w = mode->hdisplay;
              output->config.geom.h = mode->vdisplay;

              output->config.mode.w = mode->hdisplay;
              output->config.mode.h = mode->vdisplay;
              output->config.mode.refresh = mode->vrefresh;

              output->config.enabled = 1;

              EOINF("'%s' %i %i %ix%i", output, output->info.name,
                     output->config.geom.x, output->config.geom.y,
                     output->config.geom.w, output->config.geom.h);
          }
#endif

     }
   else
     {
        output->info.connected = EINA_FALSE;

        /* reset output info */
        if (output->info.screen)
          {
             free(output->info.screen);
             output->info.screen = NULL;
          }
        if (output->info.name)
          {
             free(output->info.name);
             output->info.name = NULL;
          }
        EINA_LIST_FREE(output->info.modes, m) free(m);
        output->info.modes = NULL;

        output->info.size.w = 0;
        output->info.size.h = 0;

        /* reset output config */
        output->config.geom.x = 0;
        output->config.geom.y = 0;
        output->config.geom.w = 0;
        output->config.geom.h = 0;

        output->config.mode.w = 0;
        output->config.mode.h = 0;
        output->config.mode.refresh = 0;

        output->config.rotation = 0;
        output->config.priority = 0;
        output->config.enabled = 0;

        EOINF("Disconnected", output);
     }

   /* the index of the tdm_output is higher, the tdm_output is important.
   the priority of the output is higher, the output is more important. */
   output->config.priority = 100 - output->index;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_mode_apply(E_Output *output, E_Output_Mode *mode)
{
   tdm_error error;
   E_Output_Mode *current_mode = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mode, EINA_FALSE);

   if (!output->info.connected)
     {
        EOERR("output is not connected.", output);
        return EINA_FALSE;
     }

   current_mode = e_output_current_mode_get(output);
   if (current_mode != NULL)
     {
        if (current_mode == mode)
          return EINA_TRUE;
     }

   error = tdm_output_set_mode(output->toutput, mode->tmode);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("fail to set tmode.", output);
        return EINA_FALSE;
     }

   if (current_mode != NULL)
     current_mode->current = EINA_FALSE;
   mode->current = EINA_TRUE;

   output->config.geom.x = 0;
   output->config.geom.y = 0;
   output->config.geom.w = mode->w;
   output->config.geom.h = mode->h;

   output->config.mode.w = mode->w;
   output->config.mode.h = mode->h;
   output->config.mode.refresh = mode->refresh;

   output->config.enabled = 1;

   EOINF("'%s' %i %i %ix%i %i %i", output, output->info.name,
       output->config.geom.x, output->config.geom.y,
       output->config.geom.w, output->config.geom.h,
       output->config.rotation, output->config.priority);

   EOINF("rotation = %d", output, output->config.rotation);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_mode_change(E_Output *output, E_Output_Mode *mode)
{
   E_Output *primary_output = NULL;
   E_Output_Mode *emode = NULL;
   Eina_List *l;
   Eina_Bool found = EINA_FALSE;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mode, EINA_FALSE);

   /* support only primay output */
   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(primary_output, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(output == primary_output, EINA_FALSE);

   if (e_output_connected(output) != EINA_TRUE)
     return EINA_FALSE;

   EINA_LIST_FOREACH(output->info.modes, l, emode)
     {
        if (mode == emode)
          {
             found = EINA_TRUE;
             break;
          }
     }
   EINA_SAFETY_ON_FALSE_RETURN_VAL(found == EINA_TRUE, EINA_FALSE);

   e_comp_canvas_norender_push();

   if (e_output_mode_apply(output, mode) == EINA_FALSE)
     {
        EOERR("fail to e_output_mode_apply.", output);
        return EINA_FALSE;
     }

   e_output_size_get(output, &w, &h);
   if (w == e_comp->w && h == e_comp->h)
     {
        _e_output_render_update(output);
        e_comp_canvas_norender_pop();
        return EINA_TRUE;
     }

   ecore_evas_resize(e_comp->ee, mode->w, mode->h);
   e_comp->w = mode->w;
   e_comp->h = mode->h;

   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

   _e_output_client_resize(e_comp->w, e_comp->h);

   e_comp_canvas_norender_pop();

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_hwc_setup(E_Output *output)
{
   E_Hwc *hwc = NULL;
   Eina_List *l, *ll;
   E_Plane *plane = NULL;
   E_Output *primary_output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   /* available only the primary output now. */
   if (output->hwc)
     {
        EOINF("Already has the HWC.", output);
        return EINA_TRUE;
     }

   hwc = e_hwc_new(output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(hwc, EINA_FALSE);
   output->hwc = hwc;

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        if (primary_output == output)
          {
             /* ecore evas engine setup */
             EINA_LIST_FOREACH_SAFE(output->planes, l, ll, plane)
               {
                  if (plane->is_fb)
                    {
                       if (!e_plane_setup(plane)) return EINA_FALSE;
                       else return EINA_TRUE;
                    }
               }
          }
        else
          {
             return EINA_TRUE;
          }
     }
   else
     {
#if 0
       Evas_Object *canvas_bg = NULL;
       unsigned int r, g, b, a;

        /* set the color of the canvas_gb object */
        r = 0; g = 0; b = 0; a = 1;
        canvas_bg = e_comp->bg_blank_object;
        evas_object_color_set(canvas_bg, r, g, b, a);
#endif
        return EINA_TRUE;
     }

   return EINA_FALSE;
}


EINTERN E_Output_Mode *
e_output_best_mode_find(E_Output *output)
{
   Eina_List *l = NULL;
   E_Output_Mode *mode = NULL;
   E_Output_Mode *best_mode = NULL;
   int size = 0;
   int best_size = 0;
   double best_refresh = 0.0;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->info.modes, NULL);

  if (!output->info.connected)
     {
        EOERR("output is not connected.", output);
        return NULL;
     }

   EINA_LIST_FOREACH(output->info.modes, l, mode)
     {
        size = mode->w + mode->h;

        if (mode->preferred)
          {
             best_mode = mode;
             best_size = size;
             best_refresh = mode->refresh;
             break;
          }

        if (size > best_size)
          {
             best_mode = mode;
             best_size = size;
             best_refresh = mode->refresh;
             continue;
          }
        if (size == best_size && mode->refresh > best_refresh)
          {
             best_mode = mode;
             best_refresh = mode->refresh;
          }
     }

   return best_mode;
}

EINTERN Eina_List *
e_output_mode_list_get(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->info.modes, NULL);

   return output->info.modes;
}

EINTERN E_Output_Mode *
e_output_current_mode_get(E_Output *output)
{
   Eina_List *l;
   E_Output_Mode *emode = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   EINA_LIST_FOREACH(output->info.modes, l, emode)
     {
        if (emode->current)
          return emode;
     }

   return NULL;
}

EINTERN Eina_Bool
e_output_connected(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return output->info.connected;
}

E_API Eina_Bool
e_output_dpms_set(E_Output *output, E_OUTPUT_DPMS val)
{
   E_Output_Intercept_Hook_Point hookpoint;
   tdm_output_dpms tval;
   tdm_error error;
   Eina_List *l;
   E_Zone *zone;
   E_Output *output_primary = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   if (output_primary == output)
     {
        /* FIXME: The zone controlling should be moved to e_zone */
        EINA_LIST_FOREACH(e_comp->zones, l, zone)
          {
             if (val == E_OUTPUT_DPMS_ON)
               e_zone_display_state_set(zone, E_ZONE_DISPLAY_STATE_ON);
             else if (val == E_OUTPUT_DPMS_OFF)
               e_zone_display_state_set(zone, E_ZONE_DISPLAY_STATE_OFF);
          }
     }

   if (val == E_OUTPUT_DPMS_OFF)
     {
        E_Plane *ep;
        EINA_LIST_FOREACH(output->planes, l, ep)
          {
             e_plane_dpms_off(ep);
          }
     }

   if (val == E_OUTPUT_DPMS_ON) hookpoint = E_OUTPUT_INTERCEPT_HOOK_DPMS_ON;
   else if (val == E_OUTPUT_DPMS_STANDBY) hookpoint = E_OUTPUT_INTERCEPT_HOOK_DPMS_STANDBY;
   else if (val == E_OUTPUT_DPMS_SUSPEND) hookpoint = E_OUTPUT_INTERCEPT_HOOK_DPMS_SUSPEND;
   else hookpoint = E_OUTPUT_INTERCEPT_HOOK_DPMS_OFF;

   if (!_e_output_intercept_hook_call(hookpoint, output))
     return EINA_TRUE;

   if (val == E_OUTPUT_DPMS_ON) tval = TDM_OUTPUT_DPMS_ON;
   else if (val == E_OUTPUT_DPMS_STANDBY) tval = TDM_OUTPUT_DPMS_STANDBY;
   else if (val == E_OUTPUT_DPMS_SUSPEND) tval = TDM_OUTPUT_DPMS_SUSPEND;
   else tval = TDM_OUTPUT_DPMS_OFF;

   if (output->dpms_async)
     error = tdm_output_set_dpms_async(output->toutput, tval);
   else
     error = tdm_output_set_dpms(output->toutput, tval);
   if (error != TDM_ERROR_NONE)
     {
        EOERR("fail to set the dpms(value:%d).", output, tval);
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

E_API E_OUTPUT_DPMS
e_output_dpms_get(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, E_OUTPUT_DPMS_OFF);

   return output->dpms;
}

EINTERN Eina_Bool
e_output_dpms_async_check(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return output->dpms_async;
}

EINTERN void
e_output_size_get(E_Output *output, int *w, int *h)
{
   EINA_SAFETY_ON_NULL_RETURN(output);

   *w = output->config.mode.w;
   *h = output->config.mode.h;
}

EINTERN void
e_output_phys_size_get(E_Output *output, int *phys_w, int *phys_h)
{
   EINA_SAFETY_ON_NULL_RETURN(output);

   *phys_w = output->info.size.w;
   *phys_h = output->info.size.h;
}

EINTERN Eina_Bool
e_output_fake_config_set(E_Output *output, int w, int h)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   output->config.geom.x = 0;
   output->config.geom.y = 0;
   output->config.geom.w = w;
   output->config.geom.h = h;

   output->config.mode.w = w;
   output->config.mode.h = h;
   output->config.mode.refresh = 30;
   output->config.enabled = 1;

   output->fake_config = EINA_TRUE;

   return EINA_TRUE;
}


EINTERN Eina_Bool
e_output_render(E_Output *output)
{
   E_Plane *plane = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        EINA_LIST_REVERSE_FOREACH(output->planes, l, plane)
          {
             if (!e_plane_render(plane))
               {
                   EOERR("fail to e_plane_render.", output);
                   return EINA_FALSE;
               }
          }
     }
   else
     {
        /* render the only primary output */
        if (output != e_comp_screen_primary_output_get(output->e_comp_screen))
          return EINA_TRUE;

        if (!e_hwc_windows_render(output->hwc))
          {
             EOERR("fail to e_hwc_windows_render.", output);
             return EINA_FALSE;
          }
     }

  return EINA_TRUE;
}

static int boot_launch = 0;

EINTERN Eina_Bool
e_output_commit(E_Output *output)
{
   E_Output *output_primary = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!output->config.enabled)
     {
        WRN("E_Output disconnected");
        return EINA_FALSE;
     }

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_primary, EINA_FALSE);

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        e_hwc_planes_apply(output->hwc);

        if (output == output_primary)
          {
             if (!_e_output_planes_commit(output))
               {
                   EOERR("fail to _e_output_commit.", output);
                   return EINA_FALSE;
               }
          }
        else
          {
             if (!e_hwc_planes_external_commit(output->hwc))
               {
                  EOERR("fail e_hwc_planes_external_commit", output);
                  return EINA_FALSE;
               }
          }
     }
   else
     {
        if (output == output_primary)
          {
             if (!e_hwc_windows_commit(output->hwc))
               {
                  EOERR("fail e_hwc_windows_commit", output);
                  return EINA_FALSE;
               }
          }
        else
          {
             /* trigger the output_external_update at the launching time */
             if (!boot_launch)
               {
                  boot_launch = 1;
                  e_output_external_update(output);
               }

             if (!e_hwc_windows_external_commit(output->hwc))
               {
                  EOERR("fail e_hwc_windows_external_commit", output);
                  return EINA_FALSE;
               }
          }
     }

   return EINA_TRUE;
}

EINTERN const char *
e_output_output_id_get(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   return output->id;
}

E_API E_Output *
e_output_find(const char *id)
{
   E_Output *output;
   E_Comp_Screen *e_comp_screen;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(id, NULL);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if (!strcmp(output->id, id)) return output;
     }
   return NULL;
}

E_API const Eina_List *
e_output_planes_get(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, NULL);

   return output->planes;
}

E_API void
e_output_util_planes_print(void)
{
   Eina_List *l, *ll, *p_l;
   E_Output * output = NULL;
   E_Comp_Screen *e_comp_screen = NULL;

   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        E_Plane *plane;
        E_Client *ec;

        if (!output || !output->planes) continue;

        if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
          {
             fprintf(stderr, "HWC in %s .. \n", output->id);
             fprintf(stderr, "HWC \tzPos \t on_plane \t\t\t\t on_prepare \t \n");

             EINA_LIST_REVERSE_FOREACH(output->planes, p_l, plane)
               {
                  ec = plane->ec;
                  if (ec) fprintf(stderr, "HWC \t[%d]%s\t %s (%8p)",
                                  plane->zpos,
                                  plane->is_primary ? "--" : "  ",
                                  ec->icccm.title, ec->frame);

                  ec = plane->prepare_ec;
                  if (ec) fprintf(stderr, "\t\t\t %s (%8p)",
                                  ec->icccm.title, ec->frame);
                  fputc('\n', stderr);
               }
             fputc('\n', stderr);
          }
     }
}

E_API Eina_Bool
e_output_is_fb_composing(E_Output *output)
{
   Eina_List *p_l;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, p_l, ep)
     {
        if (e_plane_is_fb_target(ep))
          {
             if(ep->ec == NULL) return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

E_API Eina_Bool
e_output_is_fb_full_compositing(E_Output *output)
{
   Eina_List *p_l;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, p_l, ep)
     if(ep->ec) return EINA_FALSE;

   return EINA_TRUE;
}

E_API E_Plane *
e_output_fb_target_get(E_Output *output)
{
   Eina_List *p_l;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, p_l, ep)
     {
        if (e_plane_is_fb_target(ep))
          return ep;
     }

   return NULL;
}

EINTERN E_Plane *
e_output_default_fb_target_get(E_Output *output)
{
   Eina_List *p_l;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   if (e_comp->hwc_ignore_primary)
     {
        /* find lowest zpos graphic type layer */
        EINA_LIST_FOREACH(output->planes, p_l, ep)
          {
             Eina_Bool available_rgb = EINA_FALSE;
             const tbm_format *formats;
             int count, i;

             if (e_plane_type_get(ep) != E_PLANE_TYPE_GRAPHIC) continue;

             if (!e_plane_available_formats_get(ep, &formats, &count))
                continue;

             for (i = 0; i < count; i++)
               {
                  if (formats[i] == TBM_FORMAT_ARGB8888 ||
                      formats[i] == TBM_FORMAT_XRGB8888)
                    {
                       available_rgb = EINA_TRUE;
                       break;
                    }
               }

             if (!available_rgb) continue;

             return ep;
          }
     }
   else
     {
        /* find primary layer */
        EINA_LIST_FOREACH(output->planes, p_l, ep)
          {
             if (ep->is_primary)
               return ep;
          }
     }

   return NULL;
}

E_API E_Output *
e_output_find_by_index(int index)
{
   E_Output *output;
   E_Comp_Screen *e_comp_screen;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, NULL);

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if (output->index == index)
          return output;
     }

   return NULL;
}

E_API E_Plane *
e_output_plane_get_by_zpos(E_Output *output, int zpos)
{
   Eina_List *p_l;
   E_Plane *ep;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->planes, EINA_FALSE);

   EINA_LIST_FOREACH(output->planes, p_l, ep)
     {
        if (ep->zpos == zpos)
          return ep;
     }

   return NULL;
}

EINTERN Eina_Bool
e_output_zoom_set(E_Output *output, double zoomx, double zoomy, int cx, int cy)
{
   E_Output *output_primary = NULL;
   E_Plane *ep = NULL;
   int w, h;
   int angle = 0;

   if (!e_comp_screen_pp_support())
     {
        EOINF("Comp Screen does not support the Zoom.", output);
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (cx < 0 || cy < 0) return EINA_FALSE;
   if (zoomx <= 0 || zoomy <= 0) return EINA_FALSE;

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_primary, EINA_FALSE);

   if (output != output_primary)
     {
        EOERR("Only Primary Output can support the Zoom.", output);
        return EINA_FALSE;
     }

   e_output_size_get(output, &w, &h);
   if (output->config.rotation % 180 == 0)
     {
        if (cx >= w || cy >= h)
          return EINA_FALSE;
     }
   else
     {
        if (cx >= h || cy >= w)
          return EINA_FALSE;
     }

   angle = _e_output_zoom_get_angle(output);

   output->zoom_conf.zoomx = zoomx;
   output->zoom_conf.zoomy = zoomy;
   output->zoom_conf.init_cx = cx;
   output->zoom_conf.init_cy = cy;
   output->zoom_conf.init_angle = angle;
   output->zoom_conf.current_angle = angle;
   output->zoom_conf.init_screen_rotation = output->config.rotation;
   output->zoom_conf.current_screen_rotation = output->config.rotation;

   _e_output_zoom_coordinate_cal(output);
   _e_output_zoom_touch_rect_get(output);

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        ep = e_output_fb_target_get(output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(ep, EINA_FALSE);

        if (!output->zoom_set)
          {
             if (_e_output_fb_over_plane_check(output))
               output->zoom_conf.unset_skip = EINA_TRUE;
          }

        e_hwc_planes_multi_plane_set(output->hwc, EINA_FALSE);

        if (!e_plane_zoom_set(ep, &output->zoom_conf.rect))
          {
             EOERR("e_plane_zoom_set failed.", output);
             output->zoom_conf.unset_skip = EINA_FALSE;
             e_hwc_planes_multi_plane_set(output->hwc, EINA_TRUE);

             return EINA_FALSE;
          }

        /* update the ecore_evas */
        if (e_plane_pp_commit_possible_check(ep))
          _e_output_render_update(output);
     }
   else
     {
        if (!e_hwc_windows_zoom_set(output->hwc, &output->zoom_conf.rect))
          {
             EOERR("e_hwc_windows_zoom_set failed.", output);
             return EINA_FALSE;
          }

        /* update the ecore_evas */
        if (e_hwc_windows_pp_commit_possible_check(output->hwc))
          _e_output_render_update(output);
     }

   if (!_e_output_zoom_touch_set(output))
     EOERR("fail _e_output_zoom_touch_set", output);

   if (!output->zoom_set) output->zoom_set = EINA_TRUE;

   EOINF("Zoom set output:%s, zoom(x:%f, y:%f, cx:%d, cy:%d) rect(x:%d, y:%d, w:%d, h:%d)",
         output, output->id, zoomx, zoomy, cx, cy,
         output->zoom_conf.rect.x, output->zoom_conf.rect.y, output->zoom_conf.rect.w, output->zoom_conf.rect.h);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_zoom_get(E_Output *output, double *zoomx, double *zoomy, int *cx, int *cy)
{
   E_Output *output_primary = NULL;

   if (!e_comp_screen_pp_support())
     {
        EOINF("Comp Screen does not support the Zoom.", output);
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_primary, EINA_FALSE);

   if (output != output_primary)
     {
        EOERR("Only Primary Output can support the Zoom.", output);
        return EINA_FALSE;
     }

   if (zoomx) *zoomx = output->zoom_conf.zoomx;
   if (zoomy) *zoomy = output->zoom_conf.zoomy;
   if (cx) *cx = output->zoom_conf.init_cx;
   if (cy) *cy = output->zoom_conf.init_cy;

   return EINA_TRUE;
}

EINTERN void
e_output_zoom_unset(E_Output *output)
{
   E_Plane *ep = NULL;

   EINA_SAFETY_ON_NULL_RETURN(output);

   if (!output->zoom_set) return;

   output->zoom_set = EINA_FALSE;
   output->zoom_conf.unset_skip = EINA_FALSE;

   if (!_e_output_zoom_touch_unset(output))
     EOERR("fail _e_output_zoom_touch_unset", output);

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        ep = e_output_fb_target_get(output);
        if (ep) e_plane_zoom_unset(ep);

        e_hwc_planes_multi_plane_set(output->hwc, EINA_TRUE);
     }
   else
     {
        e_hwc_windows_zoom_unset(output->hwc);
     }

   output->zoom_conf.zoomx = 0;
   output->zoom_conf.zoomy = 0;
   output->zoom_conf.init_cx = 0;
   output->zoom_conf.init_cy = 0;
   output->zoom_conf.init_angle = 0;
   output->zoom_conf.current_angle = 0;
   output->zoom_conf.adjusted_cx = 0;
   output->zoom_conf.adjusted_cy = 0;
   output->zoom_conf.rect.x = 0;
   output->zoom_conf.rect.y = 0;
   output->zoom_conf.rect.w = 0;
   output->zoom_conf.rect.h = 0;
   output->zoom_conf.rect_touch.x = 0;
   output->zoom_conf.rect_touch.y = 0;
   output->zoom_conf.rect_touch.w = 0;
   output->zoom_conf.rect_touch.h = 0;

   /* update the ecore_evas */
   _e_output_render_update(output);

   ELOGF("ZOOM", "e_output_zoom_unset: output:%s", NULL, output->id);
}

E_API E_Output_Hook *
e_output_hook_add(E_Output_Hook_Point hookpoint, E_Output_Hook_Cb func, const void *data)
{
   E_Output_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_OUTPUT_HOOK_LAST, NULL);
   ch = E_NEW(E_Output_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_output_hooks[hookpoint] = eina_inlist_append(_e_output_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_output_hook_del(E_Output_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_output_hooks_walking == 0)
     {
        _e_output_hooks[ch->hookpoint] = eina_inlist_remove(_e_output_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_output_hooks_delete++;
}

E_API E_Output_Intercept_Hook *
e_output_intercept_hook_add(E_Output_Intercept_Hook_Point hookpoint, E_Output_Intercept_Hook_Cb func, const void *data)
{
   E_Output_Intercept_Hook *ch;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_OUTPUT_INTERCEPT_HOOK_LAST, NULL);
   ch = E_NEW(E_Output_Intercept_Hook, 1);
   if (!ch) return NULL;
   ch->hookpoint = hookpoint;
   ch->func = func;
   ch->data = (void*)data;
   _e_output_intercept_hooks[hookpoint] = eina_inlist_append(_e_output_intercept_hooks[hookpoint], EINA_INLIST_GET(ch));
   return ch;
}

E_API void
e_output_intercept_hook_del(E_Output_Intercept_Hook *ch)
{
   ch->delete_me = 1;
   if (_e_output_intercept_hooks_walking == 0)
     {
        _e_output_intercept_hooks[ch->hookpoint] = eina_inlist_remove(_e_output_intercept_hooks[ch->hookpoint], EINA_INLIST_GET(ch));
        free(ch);
     }
   else
     _e_output_intercept_hooks_delete++;
}

EINTERN Eina_Bool
e_output_capture(E_Output *output, tbm_surface_h tsurface, Eina_Bool auto_rotate, Eina_Bool sync, E_Output_Capture_Cb func, void *data)
{
   Eina_Bool ret = EINA_FALSE;
   tdm_capture *tcapture = NULL;

   if (e_output_dpms_get(output))
     {
        func(output, tsurface, data);
        return EINA_TRUE;
     }

   if (sync)
     {
       ret = _e_output_capture(output, tsurface, auto_rotate);
       EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

       DBG("capture done(%p)", tsurface);
       func(output, tsurface, data);

       return EINA_TRUE;
     }

   //TODO : temp code. if tdm support hwc mode tdm capture, have to change to use tdm capture.
   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_WINDOWS)
     {
        ret = _e_output_capture(output, tsurface, auto_rotate);
        EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

        DBG("capture done(%p)", tsurface);
        func(output, tsurface, data);
     }
   else
     {
        tcapture = _e_output_tdm_capture_create(output, TDM_CAPTURE_CAPABILITY_ONESHOT);
        if (tcapture)
          {
             ret = _e_output_tdm_capture_info_set(output, tcapture, tsurface, TDM_CAPTURE_TYPE_ONESHOT, auto_rotate);
             EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

             ret = _e_output_tdm_capture(output, tcapture, tsurface, func, data);
             EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);
          }
        else
          {
             ret = _e_output_capture(output, tsurface, auto_rotate);
             EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

             DBG("capture done(%p)", tsurface);
             func(output, tsurface, data);
          }
     }

   return EINA_TRUE;

fail:
   if (tcapture)
     tdm_capture_destroy(tcapture);

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_output_stream_capture_queue(E_Output *output, tbm_surface_h tsurface, E_Output_Capture_Cb func, void *data)
{
   Eina_Bool ret = EINA_FALSE;
   tdm_capture *tcapture = NULL;
   tdm_error error = TDM_ERROR_NONE;

   if (output->stream_capture.possible_tdm_capture)
     {
        if (!output->stream_capture.tcapture)
          {
             tcapture = _e_output_tdm_capture_create(output, TDM_CAPTURE_CAPABILITY_STREAM);
             EINA_SAFETY_ON_NULL_RETURN_VAL(tcapture, EINA_FALSE);

             ret = _e_output_tdm_capture_info_set(output, tcapture, tsurface, TDM_CAPTURE_TYPE_STREAM, EINA_FALSE);
             EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, fail);

             error = tdm_capture_set_done_handler(tcapture,
                                                  _e_output_tdm_stream_capture_done_handler, output);
             EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);

             output->stream_capture.tcapture = tcapture;

             DBG("create tcapture(%p)", tcapture);
          }
        else
          {
             tcapture = output->stream_capture.tcapture;
          }

        ret = _e_output_tdm_stream_capture(output, tcapture, tsurface, func, data);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == EINA_TRUE, EINA_FALSE);
     }
   else
     {
        ret = _e_output_vblank_stream_capture(output, tsurface, func, data);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == EINA_TRUE, EINA_FALSE);
     }

   return EINA_TRUE;

fail:
   tdm_capture_destroy(tcapture);

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_output_stream_capture_dequeue(E_Output *output, tbm_surface_h tsurface)
{
   E_Output_Capture *cdata = NULL;

   cdata = _e_output_tdm_stream_capture_find_data(output, tsurface);

   if (!cdata) return EINA_FALSE;

   if (!cdata->in_using)
     {
        output->stream_capture.data = eina_list_remove(output->stream_capture.data, cdata);

        tbm_surface_internal_unref(tsurface);
        E_FREE(cdata);
     }
   else
     cdata->dequeued = EINA_TRUE;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_stream_capture_start(E_Output *output)
{
   tdm_error error = TDM_ERROR_NONE;
   int count = 0;

   if (output->stream_capture.start) return EINA_TRUE;

   count = eina_list_count(output->stream_capture.data);
   if (count == 0)
     {
        EOERR("no queued buffer", output);
        return EINA_FALSE;
     }

   DBG("output stream capture start.");

   output->stream_capture.start = EINA_TRUE;

   if (output->stream_capture.possible_tdm_capture)
     {
        if (e_output_dpms_get(output))
          {
             if (!output->stream_capture.timer)
               output->stream_capture.timer = ecore_timer_add((double)1 / DUMP_FPS,
                                                              _e_output_stream_capture_cb_timeout, output);
             EINA_SAFETY_ON_NULL_GOTO(output->stream_capture.timer, fail);

             return EINA_TRUE;
          }

        error = tdm_capture_commit(output->stream_capture.tcapture);
        EINA_SAFETY_ON_FALSE_GOTO(error == TDM_ERROR_NONE, fail);
     }
   else
     _e_output_watch_vblank(output);

   return EINA_TRUE;

fail:
   output->stream_capture.start = EINA_FALSE;

   return EINA_FALSE;
}

EINTERN void
e_output_stream_capture_stop(E_Output *output)
{
   E_Output_Capture *cdata = NULL;
   Eina_Bool capturing = EINA_FALSE;
   Eina_List *l;

   if (!output->stream_capture.start) return;

   output->stream_capture.start = EINA_FALSE;

   if (eina_list_count(output->stream_capture.data) == 0)
     {
        if (!output->stream_capture.timer)
          {
             output->stream_capture.timer = ecore_timer_add((double)1 / DUMP_FPS,
                                            _e_output_tdm_stream_capture_stop, output);
             return;
          }
     }

   if (!output->stream_capture.timer && !output->stream_capture.wait_vblank)
     {
        EINA_LIST_FOREACH(output->stream_capture.data, l, cdata)
          {
             if (cdata->in_using)
               capturing = EINA_TRUE;
          }

        if (capturing == EINA_FALSE)
          {
             EINA_LIST_FREE(output->stream_capture.data, cdata)
               {
                  tbm_surface_internal_unref(cdata->surface);

                  E_FREE(cdata);
               }

             if (output->stream_capture.tcapture)
               {
                  tdm_capture_destroy(output->stream_capture.tcapture);
                  output->stream_capture.tcapture = NULL;
               }
          }

        DBG("output stream capture stop.");
     }
}

EINTERN Eina_Bool
e_output_external_connect_display_set(E_Output *output)
{
   E_Output *primary_output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (e_output_display_mode_get(output) == E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION)
     {
        EOINF("Start Wait Presentation", output);

        /* the fallback timer for not setting the presentation. */
        if (output->delay_timer) ecore_timer_del(output->delay_timer);
        output->delay_timer = ecore_timer_add(EOM_DELAY_CONNECT_CHECK_TIMEOUT, _e_output_presentation_check, output);
     }
   else
     {
        EOINF("Start Mirroring", output);

        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        if (!e_output_mirror_set(output, primary_output))
          {
             EOERR("e_output_mirror_set fails.", output);
             return EINA_FALSE;
          }
     }

   EOINF("e_output_external_connect_display_set done: display_mode:%d", output, e_output_display_mode_get(output));

   return EINA_TRUE;
}

EINTERN void
e_output_external_disconnect_display_set(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN(output);

   switch (e_output_display_mode_get(output))
     {
      case E_OUTPUT_DISPLAY_MODE_NONE:
        break;
      case E_OUTPUT_DISPLAY_MODE_MIRROR:
        /* unset mirror */
        e_output_mirror_unset(output);
        break;
      case E_OUTPUT_DISPLAY_MODE_PRESENTATION:
        /* only change the display_mode */
        _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION);
        break;
      case E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION:
        /* delete presentation_delay_timer */
        if (output->delay_timer)
          {
             ecore_timer_del(output->delay_timer);
             output->delay_timer = NULL;
          }
        break;
      default:
        EOERR("unknown display_mode:%d", output, output->display_mode);
        break;
     }

   EOINF("e_output_external_disconnect_display_set done.", output);
}

EINTERN Eina_Bool
e_output_external_update(E_Output *output)
{
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output_Mode *mode = NULL;
   E_Output *output_pri = NULL;
   Eina_Bool ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   e_comp_screen = e_comp->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);

   output_pri = e_comp_screen_primary_output_get(e_comp_screen);
   if (!output_pri)
     {
        e_error_message_show(_("Fail to get the primary output!\n"));
        return EINA_FALSE;
     }

   if (output_pri == output)
     return EINA_FALSE;


   ret = e_output_update(output);
   if (ret == EINA_FALSE)
     {
        EOERR("fail e_output_update.", output);
        return EINA_FALSE;
     }

   if (e_output_connected(output))
     {
        mode = e_output_best_mode_find(output);
        if (!mode)
          {
             EOERR("fail to get best mode.", output);
             return EINA_FALSE;
          }

        ret = e_output_mode_apply(output, mode);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_mode_apply.", output);
             return EINA_FALSE;
          }
        ret = e_output_dpms_set(output, E_OUTPUT_DPMS_ON);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_dpms.", output);
             return EINA_FALSE;
          }

        ret = e_output_hwc_setup(output);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_hwc_setup.", output);
             return EINA_FALSE;
          }

        _e_output_hook_call(E_OUTPUT_HOOK_CONNECT_STATUS_CHANGE, output);

        ret = e_output_external_connect_display_set(output);
        if (ret == EINA_FALSE)
          {
             EOERR("fail to e_output_external_connect_display_set.", output);
             return EINA_FALSE;
          }

        EOINF("Connect the external output", output);
     }
   else
     {
        EOINF("Disconnect the external output", output);

        _e_output_hook_call(E_OUTPUT_HOOK_CONNECT_STATUS_CHANGE, output);

        e_output_external_disconnect_display_set(output);

        if (output->hwc)
          {
             e_hwc_del(output->hwc);
             output->hwc = NULL;
          }

        if (!e_output_dpms_set(output, E_OUTPUT_DPMS_OFF))
          {
             EOERR("fail to e_output_dpms.", output);
             return EINA_FALSE;
          }
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_external_mode_change(E_Output *output, E_Output_Mode *mode)
{
   E_Output_Mode *emode = NULL, *current_emode = NULL;
   Eina_List *l;
   Eina_Bool found = EINA_FALSE;
   E_Output *output_primary = NULL;
   E_Plane *ep = NULL;
   int w, h, p_w, p_h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mode, EINA_FALSE);

   if (e_output_connected(output) != EINA_TRUE)
     return EINA_FALSE;

   current_emode = e_output_current_mode_get( output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(current_emode, EINA_FALSE);

   if (current_emode == mode)
     return EINA_TRUE;

   EINA_LIST_FOREACH(output->info.modes, l, emode)
     {
        if (mode == emode)
          {
             found = EINA_TRUE;
             break;
          }
     }
   EINA_SAFETY_ON_FALSE_RETURN_VAL(found == EINA_TRUE, EINA_FALSE);

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_primary, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(output_primary == output, EINA_FALSE);

   e_output_size_get(output, &w, &h);
   e_output_size_get(output_primary, &p_w, &p_h);

   e_comp_canvas_norender_push();

   if (e_output_mode_apply(output, mode) == EINA_FALSE)
     {
        EOERR("fail to e_output_mode_apply.", output);
        e_comp_canvas_norender_pop();
        return EINA_FALSE;
     }

   _e_output_external_rect_get(output_primary, p_w, p_h, w, h, &output->zoom_conf.rect);

   /* call mode change hook */
   _e_output_hook_call(E_OUTPUT_HOOK_MODE_CHANGE, output);

   EOINF("mode change output: (%dx%d)", output, w, h);
   if (e_output_display_mode_get(output) == E_OUTPUT_DISPLAY_MODE_PRESENTATION)
     {
        _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION);
        if (output->delay_timer) ecore_timer_del(output->delay_timer);
        output->delay_timer = ecore_timer_add(EOM_DELAY_CONNECT_CHECK_TIMEOUT, _e_output_presentation_check, output);
     }

   if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
     {
        ep = e_output_fb_target_get(output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(ep, EINA_FALSE);

        e_plane_external_reset(ep, &output->zoom_conf.rect);
     }
   else
     {
        /* TODO: HWC Windows */;
     }

   _e_output_render_update(output_primary);
   e_comp_canvas_norender_pop();

   EOINF("e_output_external_reset done.(%dx%d)", output, mode->w, mode->h);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_mirror_set(E_Output *output, E_Output *src_output)
{
   tdm_error ret;
   int w, h, p_w, p_h;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(src_output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output->hwc, EINA_FALSE);

   if (output->display_mode == E_OUTPUT_DISPLAY_MODE_MIRROR)
     {
        EOINF("Already Set MIRROR_MODE", output);
        return EINA_TRUE;
     }

   output->tdm_mirror = EINA_FALSE;
   if (output->tdm_mirror)
     {
        EOINF("TDM supports the output mirroring.", output);

        ret = tdm_output_set_mirror(output->toutput, src_output->toutput, TDM_TRANSFORM_NORMAL);
        if (ret != TDM_ERROR_NONE)
          {
             EOINF("tdm_output_set_mirror fails.", output);
             return EINA_FALSE;
          }
     }
   else
     {
        e_output_size_get(output, &w, &h);
        e_output_size_get(src_output, &p_w, &p_h);

        _e_output_external_rect_get(src_output, p_w, p_h, w, h, &output->zoom_conf.rect);

        if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
          {
             if (!e_hwc_planes_mirror_set(output->hwc, src_output->hwc, &output->zoom_conf.rect))
               {
                  EOERR("e_hwc_planes_mirror_set failed.", output);
                  return EINA_FALSE;
               }
          }
        else
          {
             /* set the target_buffer of the src_hwc to the target_buffer of the dst_hwc with zoom rect */
             if (!e_hwc_windows_mirror_set(output->hwc, src_output->hwc, &output->zoom_conf.rect))
               {
                  EOERR("e_hwc_windows_mirror_set failed.", output);
                  return EINA_FALSE;
               }
          }
     }

   output->mirror_src_output = src_output;

   /* make the src_hwc be full gl-compositing */
   e_hwc_deactive_set(src_output->hwc, EINA_TRUE);

   _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_MIRROR);
   output->external_set = EINA_TRUE;

   /* update the ecore_evas of the src_output */
   _e_output_render_update(src_output);

   EOINF("e_output_mirror_set done: E_OUTPUT_DISPLAY_MODE_MIRROR", output);

   return EINA_TRUE;
}

EINTERN void
e_output_mirror_unset(E_Output *output)
{
   E_Output *src_output;
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN(output);

   EOINF("e_output_mirror_unset: E_OUTPUT_DISPLAY_MODE_NONE", output);

   src_output = output->mirror_src_output;

   /* update the ecore_evas of the src_output */
   _e_output_render_update(src_output);

   output->external_set = EINA_FALSE;
   _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_NONE);

   e_hwc_deactive_set(src_output->hwc, EINA_FALSE);

   output->mirror_src_output = NULL;

   if (output->tdm_mirror)
     {
        ret = tdm_output_unset_mirror(output->toutput);
        if (ret != TDM_ERROR_NONE)
          EOERR("tdm_output_unset_mirror fails.", output);
     }
   else
     {
        if (e_hwc_policy_get(output->hwc) == E_HWC_POLICY_PLANES)
          e_hwc_planes_mirror_unset(output->hwc);
        else
          e_hwc_windows_mirror_unset(output->hwc);
     }
}

EINTERN Eina_Bool
e_output_presentation_wait_set(E_Output *output, E_Client *ec)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ec, EINA_FALSE);

   _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION);

   /* the ec does not commit the buffer to the exernal output
    * Therefore, it needs the timer to prevent the eternal waiting.
    */
   if (output->delay_timer)
     {
        ecore_timer_del(output->delay_timer);
        output->delay_timer = ecore_timer_add(EOM_DELAY_CHECK_TIMEOUT, _e_output_presentation_check, output);
     }

   EOINF("e_output_presentation_wait_set done: E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION", output);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_presentation_update(E_Output *output, E_Client *ec)
{
   E_Hwc *hwc;
   E_Output_Display_Mode display_mode;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ec, EINA_FALSE);

   hwc = output->hwc;
   EINA_SAFETY_ON_FALSE_RETURN_VAL(hwc, EINA_FALSE);

   display_mode = e_output_display_mode_get(output);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(display_mode == E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION, EINA_FALSE);

   /* delete the delay timer on E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION */
   if (output->delay_timer) ecore_timer_del(output->delay_timer);
   output->delay_timer = NULL;

   if (e_hwc_policy_get(hwc) == E_HWC_POLICY_PLANES)
     {
        if (!e_hwc_planes_presentation_update(hwc, ec))
          {
             EOERR("e_hwc_planes_presentation_update fails.", output);
             return EINA_FALSE;
          }
     }
   else
     {
        if (!e_hwc_windows_presentation_update(hwc, ec))
          {
             EOERR("e_hwc_windows_presentation_update fails.", output);
             return EINA_FALSE;
          }
     }

   output->presentation_ec = ec;
   _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_PRESENTATION);

   output->external_set = EINA_TRUE;

   EOINF("e_output_presentation_update done: E_OUTPUT_DISPLAY_MODE_PRESENTATION", output);

   return EINA_TRUE;
}

EINTERN void
e_output_presentation_unset(E_Output *output)
{
   E_Hwc *hwc;

   EINA_SAFETY_ON_FALSE_RETURN(output);

   hwc = output->hwc;
   EINA_SAFETY_ON_FALSE_RETURN(hwc);

   /* delete the delay timer on E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION */
   if (output->delay_timer) ecore_timer_del(output->delay_timer);
   output->delay_timer = NULL;

   output->external_set = EINA_FALSE;

   _e_output_display_mode_set(output, E_OUTPUT_DISPLAY_MODE_NONE);
   output->presentation_ec = NULL;

   if (e_hwc_policy_get(hwc) == E_HWC_POLICY_PLANES)
     e_hwc_planes_presentation_update(hwc, NULL);
   else
     e_hwc_windows_presentation_update(hwc, NULL);
}

EINTERN E_Client *
e_output_presentation_ec_get(E_Output *output)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(output, NULL);

   return output->presentation_ec;
}

EINTERN E_Output_Display_Mode
e_output_display_mode_get(E_Output *output)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(output, E_OUTPUT_DISPLAY_MODE_NONE);

   return output->display_mode;
}

EINTERN void
e_output_norender_push(E_Output *output)
{
   EINA_SAFETY_ON_FALSE_RETURN(output);

   e_hwc_norender_push(output->hwc);
}

EINTERN void
e_output_norender_pop(E_Output *output)
{
   EINA_SAFETY_ON_FALSE_RETURN(output);

   e_hwc_norender_pop(output->hwc);
}

EINTERN int
e_output_norender_get(E_Output *output)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(output, 0);

   return e_hwc_norender_get(output->hwc);
}

static const char *
_e_output_prop_name_get_by_id(E_Output *output, unsigned int id)
{
   const output_prop *props;
   int i, count = 0;

   if (!e_output_available_properties_get(output, &props, &count))
     {
        EOERR("e_output_available_properties_get failed.", output);
        return NULL;
     }

   for (i = 0; i < count; i++)
     {
        if (props[i].id == id)
          return props[i].name;
     }

   EOERR("No available property: id %d", output, id);

   return NULL;
}

E_API Eina_Bool
e_output_available_properties_get(E_Output *output, const output_prop **props, int *count)
{
   const tdm_prop *tprops;
   tdm_error ret;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(count, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   ret = tdm_output_get_available_properties(output->toutput, &tprops, count);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, ret);

   *props = (output_prop *)tprops;

   EOINF(">>>>>>>> Available OUTPUT props : count = %d", output, *count);
   for (i = 0; i < *count; i++)
     EOINF("   [%d] %s, %u", output, i, tprops[i].name, tprops[i].id);

   return EINA_TRUE;
}

E_API Eina_Bool
e_output_property_get(E_Output *output, unsigned int id, output_prop_value *value)
{
   tdm_value tvalue;
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(value, EINA_FALSE);

   ret = tdm_output_get_property(output->toutput, id, &tvalue);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, EINA_FALSE);

   memcpy(&value->ptr, &tvalue.ptr, sizeof(tdm_value));

   return EINA_TRUE;
}

E_API Eina_Bool
e_output_property_set(E_Output *output, unsigned int id, output_prop_value value)
{
   const char *name;
   tdm_value tvalue;
   tdm_error ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   name = _e_output_prop_name_get_by_id(output, id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(name, EINA_FALSE);

   memcpy(&tvalue.ptr, &value.ptr, sizeof(output_prop_value));

   ret = tdm_output_set_property(output->toutput, id, tvalue);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(ret != TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}
