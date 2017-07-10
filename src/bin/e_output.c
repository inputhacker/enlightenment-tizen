#include "e.h"

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
_e_output_zoom_get_angle(E_Output *eout)
{
   E_Client *ec = NULL;
   int angle = 0;
   int ec_angle = 0;

   ec = _e_output_zoom_top_visible_ec_get();
   if (ec)
     ec_angle = ec->e.state.rot.ang.curr;

   angle = (ec_angle + eout->config.rotation) % 360;

   return angle;
}

static void
_e_output_zoom_coordinate_cal_with_angle(E_Output *eout, int angle)
{
   int x, y;
   int w, h;

   if (angle == 0 || angle == 180)
     {
        w = eout->config.geom.w;
        h = eout->config.geom.h;
     }
   else
     {
        w = eout->config.geom.h;
        h = eout->config.geom.w;
     }

   if (angle == eout->zoom_conf.init_angle)
     {
        if (angle == 0)
          {
             eout->zoom_conf.adjusted_cx = eout->zoom_conf.init_cx;
             eout->zoom_conf.adjusted_cy = eout->zoom_conf.init_cy;
          }
        else if (angle == 90)
          {
             eout->zoom_conf.adjusted_cx = eout->zoom_conf.init_cy;
             eout->zoom_conf.adjusted_cy = w - eout->zoom_conf.init_cx - 1;
          }
        else if (angle == 180)
          {
             eout->zoom_conf.adjusted_cx = w - eout->zoom_conf.init_cx - 1;
             eout->zoom_conf.adjusted_cy = h - eout->zoom_conf.init_cy - 1;
          }
        else /* angle == 270 */
          {
             eout->zoom_conf.adjusted_cx = h - eout->zoom_conf.init_cy - 1;
             eout->zoom_conf.adjusted_cy = eout->zoom_conf.init_cx;
          }
     }
   else
     {
        if ((angle % 180) == (eout->zoom_conf.init_angle % 180)) /* 180 changed from init, don't have to cal ratio */
          {
             x = eout->zoom_conf.init_cx;
             y = eout->zoom_conf.init_cy;
          }
        else /* 90 or 270 changed from init, need ratio cal*/
          {
             if (angle == 90 || angle == 270)
               {
                  x = (float)eout->config.geom.h / eout->config.geom.w * eout->zoom_conf.init_cx;
                  y = (float)eout->config.geom.w / eout->config.geom.h * eout->zoom_conf.init_cy;
               }
             else /* 0 or 180 */
               {
                  x = (float)eout->config.geom.w / eout->config.geom.h * eout->zoom_conf.init_cx;
                  y = (float)eout->config.geom.h / eout->config.geom.w * eout->zoom_conf.init_cy;
               }
          }
        if (angle == 0)
          {
             eout->zoom_conf.adjusted_cx = x;
             eout->zoom_conf.adjusted_cy = y;
          }
        else if (angle == 90)
          {
             eout->zoom_conf.adjusted_cx = y;
             eout->zoom_conf.adjusted_cy = w - x - 1;
          }
        else if (angle == 180)
          {
             eout->zoom_conf.adjusted_cx = w - x - 1;
             eout->zoom_conf.adjusted_cy = h - y - 1;
          }
        else /* angle == 270 */
          {
             eout->zoom_conf.adjusted_cx = h - y - 1;
             eout->zoom_conf.adjusted_cy = x;
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

static Eina_Bool
_e_output_zoom_touch_set(E_Output *eout, Eina_Bool set)
{
   Ecore_Drm_Device *dev = NULL;
   Eina_Bool ret = EINA_FALSE;
   const Eina_List *l;
   Ecore_Drm_Output *primary_output = NULL;
   int w = 0, h = 0;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        primary_output = ecore_drm_output_primary_get(dev);
        if (primary_output != NULL)
          break;
     }

   if (!primary_output)
     {
        ERR("fail get primary_output");
        return EINA_FALSE;
     }

   if (set)
     ret = ecore_drm_device_touch_transformation_set(dev,
                                                     eout->zoom_conf.rect.x, eout->zoom_conf.rect.y,
                                                     eout->zoom_conf.rect.w, eout->zoom_conf.rect.h);
   else
     {
        e_output_size_get(eout, &w, &h);
        ret = ecore_drm_device_touch_transformation_set(dev, 0, 0, w, h);
     }

   if (ret != EINA_TRUE)
     ERR("fail ecore_drm_device_touch_transformation_set");

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

static void
_e_output_zoom_rotate(E_Output *eout)
{
   E_Plane *ep = NULL;
   Eina_List *l;
   int w, h;

   EINA_SAFETY_ON_NULL_RETURN(eout);

   e_output_size_get(eout, &w, &h);

   _e_output_zoom_coordinate_cal_with_angle(eout, eout->zoom_conf.current_angle);

   /* get the scaled rect */
   _e_output_zoom_scaled_rect_get(w, h, eout->zoom_conf.zoomx, eout->zoom_conf.zoomy,
                                  eout->zoom_conf.adjusted_cx, eout->zoom_conf.adjusted_cy, &eout->zoom_conf.rect);
   DBG("zoom_rect rotate(x:%d,y:%d) (w:%d,h:%d)",
       eout->zoom_conf.rect.x, eout->zoom_conf.rect.y, eout->zoom_conf.rect.w, eout->zoom_conf.rect.h);

   EINA_LIST_FOREACH(eout->planes, l, ep)
     {
        if (!e_plane_is_fb_target(ep)) continue;

        e_plane_zoom_set(ep, &eout->zoom_conf.rect);
        break;
     }

   /* update the ecore_evas */
   _e_output_render_update(eout);
}

static void
_e_output_zoom_rotating_check(E_Output *output)
{
   int angle = 0;

   angle = _e_output_zoom_get_angle(output);
   if (output->zoom_conf.current_angle != angle)
     {
        output->zoom_conf.current_angle = angle;
        _e_output_zoom_rotate(output);
     }
}

static void
_e_output_cb_output_change(tdm_output *toutput,
                                  tdm_output_change_type type,
                                  tdm_value value,
                                  void *user_data)
{
   E_Output *e_output = NULL;
   E_OUTPUT_DPMS edpms;
   tdm_output_dpms tdpms = (tdm_output_dpms)value.u32;

   EINA_SAFETY_ON_NULL_RETURN(toutput);
   EINA_SAFETY_ON_NULL_RETURN(user_data);

   e_output = (E_Output *)user_data;

   switch (type)
     {
       case TDM_OUTPUT_CHANGE_DPMS:
          if (tdpms == TDM_OUTPUT_DPMS_OFF) edpms = E_OUTPUT_DPMS_OFF;
          else if (tdpms == TDM_OUTPUT_DPMS_ON) edpms = E_OUTPUT_DPMS_ON;
          else if (tdpms == TDM_OUTPUT_DPMS_STANDBY) edpms = E_OUTPUT_DPMS_STANDBY;
          else if (tdpms == TDM_OUTPUT_DPMS_SUSPEND) edpms = E_OUTPUT_DPMS_SUSPEND;
          else edpms = e_output->dpms;

          e_output->dpms = edpms;
          break;
       default:
          break;
     }
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
   /* nothing */
   return EINA_TRUE;
}

EINTERN void
e_output_shutdown(void)
{
   ;
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

EINTERN E_Output *
e_output_new(E_Comp_Screen *e_comp_screen, int index)
{
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   E_Plane *default_fb = NULL;
   tdm_output *toutput = NULL;
   tdm_error error;
   char *id = NULL;
   char *name;
   int num_layers;
   int i;
   int size = 0;
   tdm_output_type output_type;
   int min_w, min_h, max_w, max_h, preferred_align;

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
   INF("E_OUTPUT: (%d) output_id = %s", index, output->id);

   tdm_output_get_layer_count(toutput, &num_layers);
   if (num_layers < 1)
     {
        ERR("fail to get tdm_output_get_layer_count\n");
        goto fail;
     }
   output->plane_count = num_layers;
   INF("E_OUTPUT: num_planes %i", output->plane_count);

   if (!e_plane_init())
     {
        ERR("fail to e_plane_init.");
        goto fail;
     }

   for (i = 0; i < output->plane_count; i++)
     {
        plane = e_plane_new(output, i);
        if (!plane)
          {
             ERR("fail to create the e_plane.");
             goto fail;
          }
        output->planes = eina_list_append(output->planes, plane);
     }

   output->planes = eina_list_sort(output->planes, eina_list_count(output->planes), _e_output_cb_planes_sort);

   default_fb = e_output_default_fb_target_get(output);
   if (!default_fb)
     {
        ERR("fail to get default_fb_target plane");
        goto fail;
     }

   if (!e_plane_fb_target_set(default_fb, EINA_TRUE))
     {
        ERR("fail to set fb_target plane");
        goto fail;
     }

   output->e_comp_screen = e_comp_screen;

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
        ERR("output size(%dx%d) should be squre.",
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
        ERR("failt to get conn status.");
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
                  ERR("fail to get model info.");
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
             INF("E_OUTPUT: screen = %s, name = %s", screen, name);

             error = tdm_output_get_physical_size(output->toutput, &phy_w, &phy_h);
             if (error != TDM_ERROR_NONE)
               {
                  ERR("fail to get physical_size.");
                  free(name);
                  return EINA_FALSE;
               }

             error = tdm_output_get_available_modes(output->toutput, &tmodes, &num_tmodes);
             if (error != TDM_ERROR_NONE || num_tmodes == 0)
               {
                  ERR("fail to get tmodes");
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

             INF("E_OUTPUT: id(%s) connected..", output->id);
          }

#if 0
        /* check the crtc setting */
        if (status != TDM_OUTPUT_CONN_STATUS_MODE_SETTED)
          {
              const tdm_output_mode *mode = NULL;

              error = tdm_output_get_mode(output->toutput, &mode);
              if (error != TDM_ERROR_NONE || mode == NULL)
                {
                   ERR("fail to get mode.");
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

              INF("E_OUTPUT: '%s' %i %i %ix%i", output->info.name,
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

        INF("E_OUTPUT: disconnected.. id: %s", output->id);
     }

   /* the index of the tdm_output is higher, the tdm_output is important.
   the priority of the e_output is higher, the e_output is more important. */
   output->config.priority = 100 - output->index;

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_mode_apply(E_Output *output, E_Output_Mode *mode)
{
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!output->info.connected)
     {
        ERR("output is not connected.");
        return EINA_FALSE;
     }

   error = tdm_output_set_mode(output->toutput, mode->tmode);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to set tmode.");
        return EINA_FALSE;
     }

   output->config.geom.x = 0;
   output->config.geom.y = 0;
   output->config.geom.w = mode->w;
   output->config.geom.h = mode->h;

   output->config.mode.w = mode->w;
   output->config.mode.h = mode->h;
   output->config.mode.refresh = mode->refresh;

   output->config.enabled = 1;

   INF("E_OUTPUT: '%s' %i %i %ix%i %i %i", output->info.name,
       output->config.geom.x, output->config.geom.y,
       output->config.geom.w, output->config.geom.h,
       output->config.rotation, output->config.priority);

   INF("E_OUTPUT: rotation = %d", output->config.rotation);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_setup(E_Output *output)
{
   Eina_List *l, *ll;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(output->planes, l, ll, plane)
     {
        if (plane->is_fb)
          {
             if (!e_plane_setup(plane)) return EINA_FALSE;
             else return EINA_TRUE;
          }
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
        ERR("output is not connected.");
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

EINTERN Eina_Bool
e_output_connected(E_Output *output)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   return output->info.connected;
}

EINTERN Eina_Bool
e_output_dpms_set(E_Output *output, E_OUTPUT_DPMS val)
{
   tdm_output_dpms tval;
   Eina_Bool ret = EINA_TRUE;
   tdm_error error;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (val == E_OUTPUT_DPMS_OFF) tval = TDM_OUTPUT_DPMS_OFF;
   else if (val == E_OUTPUT_DPMS_ON) tval = TDM_OUTPUT_DPMS_ON;
   else if (val == E_OUTPUT_DPMS_STANDBY) tval = TDM_OUTPUT_DPMS_STANDBY;
   else if (val == E_OUTPUT_DPMS_SUSPEND) tval = TDM_OUTPUT_DPMS_SUSPEND;
   else ret = EINA_FALSE;

   if (!ret) return EINA_FALSE;

   error = tdm_output_set_dpms(output->toutput, tval);
   if (error != TDM_ERROR_NONE)
     {
        ERR("fail to set the dpms(value:%d).", tval);
        return EINA_FALSE;
     }

   output->dpms = val;

   return EINA_TRUE;
}

EINTERN void
e_output_size_get(E_Output *output, int *w, int *h)
{
   EINA_SAFETY_ON_NULL_RETURN(output);

   *w = output->config.mode.w;
   *h = output->config.mode.h;
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
   output->config.mode.refresh = 60;
   output->config.enabled = 1;

   return EINA_TRUE;
}


EINTERN Eina_Bool
e_output_render(E_Output *output)
{
   E_Plane *plane = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_REVERSE_FOREACH(output->planes, l, plane)
     {
        if (!e_plane_render(plane))
         {
            ERR("fail to e_plane_render.");
            return EINA_FALSE;
         }
     }

  return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_commit(E_Output *output)
{
   E_Plane *plane = NULL, *fb_target = NULL;
   Eina_List *l;
   Eina_Bool fb_commit = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!output->config.enabled)
     {
        WRN("E_Output disconnected");
        return EINA_FALSE;
     }

   fb_target = e_output_fb_target_get(output);

   /* fetch the fb_target at first */
   fb_commit = e_plane_fetch(fb_target);
   if (fb_commit && (output->dpms == E_OUTPUT_DPMS_OFF))
     e_plane_unfetch(fb_target);

   if (output->zoom_set)
     {
        /* unset check */
        EINA_LIST_FOREACH(output->planes, l, plane)
          {
             /* skip the fb_target fetch because we do this previously */
             if (e_plane_is_fb_target(plane)) continue;
             if (!e_plane_is_unset_candidate(plane)) continue;

             e_plane_unset_try_set(plane, EINA_TRUE);

             /* if the plane is trying to unset,
              * 1. if fetching the fb is not available, continue.
              * 2. if fetching the fb is available, verify the unset commit check.  */
             if (e_plane_is_unset_try(plane))
               {
                  if (!fb_commit) continue;
                  if (!e_plane_unset_commit_check(plane)) continue;
               }

             /* fetch the surface to the plane */
             if (!e_plane_fetch(plane)) continue;

             if (output->dpms == E_OUTPUT_DPMS_OFF)
               e_plane_unfetch(plane);

             if (e_plane_is_unset_try(plane))
               e_plane_unset_try_set(plane, EINA_FALSE);

             if (!e_plane_commit(plane))
               ERR("fail to e_plane_commit");
          }

        /* zoom commit only primary */
        if (!fb_commit) return EINA_TRUE;

        _e_output_zoom_rotating_check(output);

        /* zoom commit */
        if (!e_plane_pp_commit(fb_target))
          ERR("fail to e_plane_pp_commit");
     }
   else
     {
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
                 if (!fb_commit) continue;
                 if (!e_plane_unset_commit_check(plane)) continue;
               }

             /* fetch the surface to the plane */
             if (!e_plane_fetch(plane)) continue;

             if (output->dpms == E_OUTPUT_DPMS_OFF)
               e_plane_unfetch(plane);

             if (e_plane_is_unset_try(plane))
               e_plane_unset_try_set(plane, EINA_FALSE);
          }

        if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

        EINA_LIST_FOREACH(output->planes, l, plane)
          {
             if (e_plane_is_unset_try(plane)) continue;

             if (!e_plane_commit(plane))
               ERR("fail to e_plane_commit");

             // TODO: to be fixed. check fps of fb_target currently.
             if (fb_commit) _e_output_update_fps();
          }
     }

   return EINA_TRUE;
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

        fprintf(stderr, "HWC in %s .. \n", output->id);
        fprintf(stderr, "HWC \tzPos \t on_plane \t\t\t\t on_prepare \t \n");

        EINA_LIST_REVERSE_FOREACH(output->planes, p_l, plane)
          {
             ec = plane->ec;
             if (ec) fprintf(stderr, "HWC \t[%d]%s\t %s (0x%08x)",
                             plane->zpos,
                             plane->is_primary ? "--" : "  ",
                             ec->icccm.title, (unsigned int)ec->frame);

             ec = plane->prepare_ec;
             if (ec) fprintf(stderr, "\t\t\t %s (0x%08x)",
                             ec->icccm.title, (unsigned int)ec->frame);
             fputc('\n', stderr);
          }
        fputc('\n', stderr);
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

   return EINA_FALSE;
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

   /* find lowest zpos graphic type layer */
   EINA_LIST_FOREACH(output->planes, p_l, ep)
     {
        if (ep->is_primary)
          return ep;
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
e_output_zoom_set(E_Output *eout, double zoomx, double zoomy, int cx, int cy)
{
   E_Plane *ep = NULL;
   int w, h;
   int angle = 0;

   if (!e_comp_screen_pp_support())
     {
        WRN("Comp Screen does not support the Zoom.");
        return EINA_FALSE;
     }

   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);

   e_output_size_get(eout, &w, &h);
   angle = _e_output_zoom_get_angle(eout);

   if (cx < 0 || cy < 0) return EINA_FALSE;
   if (zoomx <= 0 || zoomy <= 0) return EINA_FALSE;
   if (angle % 180 == 0)
     {
        if (cx >= w || cy >= h) return EINA_FALSE;
     }
   else
     {
        if (cx >= h || cy >= w) return EINA_FALSE;
     }

   ep = e_output_fb_target_get(eout);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ep, EINA_FALSE);

#ifdef ENABLE_HWC_MULTI
   e_comp_hwc_multi_plane_set(EINA_FALSE);
#endif

   eout->zoom_conf.zoomx = zoomx;
   eout->zoom_conf.zoomy = zoomy;
   eout->zoom_conf.init_cx = cx;
   eout->zoom_conf.init_cy = cy;
   eout->zoom_conf.init_angle = angle;
   eout->zoom_conf.current_angle = angle;

   _e_output_zoom_coordinate_cal_with_angle(eout, angle);

   /* get the scaled rect */
   _e_output_zoom_scaled_rect_get(w, h, eout->zoom_conf.zoomx, eout->zoom_conf.zoomy,
                                  eout->zoom_conf.adjusted_cx, eout->zoom_conf.adjusted_cy, &eout->zoom_conf.rect);

   if (!e_plane_zoom_set(ep, &eout->zoom_conf.rect))
     {
        ERR("e_plane_zoom_set failed.");
#ifdef ENABLE_HWC_MULTI
        e_comp_hwc_multi_plane_set(EINA_TRUE);
#endif
        return EINA_FALSE;
     }

   if (!_e_output_zoom_touch_set(eout, EINA_TRUE))
     ERR("fail _e_output_zoom_touch_set");

   if (!eout->zoom_set) eout->zoom_set = EINA_TRUE;
   DBG("zoom set output:%s, zoom(x:%f, y:%f, cx:%d, cy:%d) rect(x:%d, y:%d, w:%d, h:%d)",
       eout->id, zoomx, zoomy, cx, cy,
       eout->zoom_conf.rect.x, eout->zoom_conf.rect.y, eout->zoom_conf.rect.w, eout->zoom_conf.rect.h);

   /* update the ecore_evas */
   _e_output_render_update(eout);

   return EINA_TRUE;
}

EINTERN void
e_output_zoom_unset(E_Output *eout)
{
   E_Plane *ep = NULL;

   EINA_SAFETY_ON_NULL_RETURN(eout);

   if (!eout->zoom_set) return;

   ep = e_output_fb_target_get(eout);
   EINA_SAFETY_ON_NULL_RETURN(ep);

   if (!_e_output_zoom_touch_set(eout, EINA_FALSE))
     ERR("fail _e_output_zoom_touch_set");

   eout->zoom_conf.zoomx = 0;
   eout->zoom_conf.zoomy = 0;
   eout->zoom_conf.init_cx = 0;
   eout->zoom_conf.init_cy = 0;
   eout->zoom_conf.init_angle = 0;
   eout->zoom_conf.current_angle = 0;
   eout->zoom_conf.adjusted_cx = 0;
   eout->zoom_conf.adjusted_cy = 0;
   eout->zoom_conf.rect.x = 0;
   eout->zoom_conf.rect.y = 0;
   eout->zoom_conf.rect.w = 0;
   eout->zoom_conf.rect.h = 0;

   e_plane_zoom_unset(ep);

   eout->zoom_set = EINA_FALSE;

#ifdef ENABLE_HWC_MULTI
   e_comp_hwc_multi_plane_set(EINA_TRUE);
#endif

   /* update the ecore_evas */
   _e_output_render_update(eout);

   DBG("e_output_zoom_unset: output:%s", eout->id);
}

