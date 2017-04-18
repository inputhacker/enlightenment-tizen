#include "e.h"

#include <Evas_Engine_GL_Drm.h>

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
#if 0
   Evas_Engine_Info_GL_Drm *einfo;

   /* TODO: enable hwc according to the conf->hwc */
   if (e_comp_gl_get())
     {
        /* get the evas_engine_gl_drm information */
        einfo = (Evas_Engine_Info_GL_Drm *)evas_engine_info_get(e_comp->evas);
        if (!einfo) return EINA_FALSE;
        /* enable hwc to evas engine gl_drm */
        einfo->info.hwc_enable = EINA_TRUE;
     }
#endif
   return EINA_TRUE;
}

EINTERN void
e_output_shutdown(void)
{
   ;
}

EINTERN E_Output *
e_output_drm_new(Ecore_Drm_Output *output)
{
   E_Output *eout = NULL;
   int i;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, NULL);

   eout = E_NEW(E_Output, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, NULL);

   eout->info.name = ecore_drm_output_name_get(output);
   printf("E_OUTPUT: .... out %s\n", eout->info.name);

    // TODO: get proper value using libtdm
   eout->plane_count = 1;
   printf("COMP TDM: planes %i\n", eout->plane_count);
   for (i = 0; i < eout->plane_count; i++)
     {
        printf("COMP TDM: added plane %i\n", i);
        // TODO: primary layer condition (0 is temp condition)
        e_plane_new(eout, i);
     }

   eout->output = output;

   return eout;
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
   if (!id) return NULL;
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
             int size = 0;

             error = tdm_output_get_model_info(output->toutput, &maker, &screen, NULL);
             if (error != TDM_ERROR_NONE)
               {
                  ERR("fail to get model info.");
                  return EINA_FALSE;
               }

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

   INF("E_OUTPUT: '%s' %i %i %ix%i", output->info.name,
       output->config.geom.x, output->config.geom.y,
       output->config.geom.w, output->config.geom.h);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_output_hwc_setup(E_Output *output)
{
   Eina_List *l, *ll;
   E_Plane *plane = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(output->planes, l, ll, plane)
     {
        if (plane->is_primary)
          {
             if (!e_plane_hwc_setup(plane)) return EINA_FALSE;
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
        return EINA_FALSE;
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

static char *
_e_output_drm_model_get(Ecore_Drm_Output *output)
{
   const char *model;

   model = ecore_drm_output_model_get(output);
   if (!model) return NULL;

   return strdup(model);
}

EINTERN Eina_Bool
e_output_drm_update(E_Output *eout)
{
   Eina_List *m = NULL;
   Eina_List *modes = NULL;
   Eina_Bool connected;
   E_Comp_Screen *e_comp_screen;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp->e_comp_screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eout, EINA_FALSE);

   e_comp_screen = e_comp->e_comp_screen;

   connected = ecore_drm_output_connected_get(eout->output);
   if (connected)
     {
        /* disconnect --> connect */
        if (connected != eout->info.connected)
          {
             int len = 0;
             char *id;
             char *screen;
             char *edid;
             int phy_w, phy_h;
             Ecore_Drm_Output_Mode *omode;

             screen = _e_output_drm_model_get(eout->output);
             edid = ecore_drm_output_edid_get(eout->output);
             if (eout->info.edid)
               id = malloc(strlen(eout->info.name) + 1 + strlen(eout->info.edid) + 1);
             else
               id = malloc(strlen(eout->info.name) + 1 + 1);
             if (!id)
               {
                  free(edid);
                  return EINA_FALSE;
               }
             len = strlen(eout->info.name);
             strncpy(id, eout->info.name, len + 1);
             strncat(id, "/", 1);
             if (eout->info.edid) strncat(id, edid, strlen(edid));

             printf("E_OUTPUT: ...... screen: %s\n", id);

             ecore_drm_output_physical_size_get(eout->output, &phy_w, &phy_h);

             EINA_LIST_FOREACH(ecore_drm_output_modes_get(eout->output), m, omode)
               {
                  E_Output_Mode *rmode;

                  rmode = E_NEW(E_Output_Mode, 1);
                  if (!rmode) continue;

                  if (omode->flags & DRM_MODE_TYPE_PREFERRED)
                    rmode->preferred = EINA_TRUE;

                  rmode->w = omode->width;
                  rmode->h = omode->height;
                  rmode->refresh = omode->refresh;

                  modes = eina_list_append(modes, rmode);
               }

             free(eout->id);
             free(eout->info.screen);
             free(eout->info.edid);
             EINA_LIST_FREE(eout->info.modes, m) free(m);

             eout->id = id;
             eout->info.screen = screen;
             eout->info.edid = edid;
             eout->info.modes = modes;
             eout->info.size.w = phy_w;
             eout->info.size.h = phy_h;

             eout->info.connected = EINA_TRUE;

             printf("E_OUTPUT: connected.. id: %s\n", eout->id);
          }

        /* check the crtc setting */
        const Eina_List *l;
        int i;
        unsigned int refresh;
        Ecore_Drm_Device *dev;

        EINA_LIST_FOREACH(e_comp_screen->devices, l, dev)
          {
             if (ecore_drm_output_primary_get(dev) == eout->output)
               eout->config.priority = 100;

             for (i = 0; i < dev->crtc_count; i++)
               {
                  if (dev->crtcs[i] == ecore_drm_output_crtc_id_get(eout->output))
                    {
                       ecore_drm_output_position_get(eout->output, &eout->config.geom.x,
                                                     &eout->config.geom.y);
                       ecore_drm_output_crtc_size_get(eout->output, &eout->config.geom.w,
                                                      &eout->config.geom.h);
                       ecore_drm_output_current_resolution_get(eout->output,
                                                               &eout->config.mode.w,
                                                               &eout->config.mode.h,
                                                               &refresh);
                       eout->config.mode.refresh = refresh;
                       eout->config.enabled =
                           ((eout->config.mode.w != 0) && (eout->config.mode.h != 0));

                       printf("E_OUTPUT: '%s' %i %i %ix%i\n", eout->info.name,
                              eout->config.geom.x, eout->config.geom.y,
                              eout->config.geom.w, eout->config.geom.h);
                       break;
                    }
               }
          }
     }
   else
     {
        eout->info.connected = EINA_FALSE;

        /* reset eout info */
        free(eout->id);
        free(eout->info.screen);
        free(eout->info.edid);
        EINA_LIST_FREE(eout->info.modes, m) free(m);

        eout->id = malloc(strlen(eout->info.name) + 1 + 1);
        eout->info.size.w = 0;
        eout->info.size.h = 0;

        printf("E_OUTPUT: disconnected.. id: %s\n", eout->id);
     }

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
   E_Plane *plane = NULL;
   Eina_List *l;
   Eina_Bool fb_commit = EINA_FALSE;
   Eina_Bool fb_hwc_on = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   if (!output->config.enabled)
     {
        WRN("E_Output disconnected");
        return EINA_FALSE;
     }

   /* set planes */
   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        if (e_plane_is_fb_target(plane) && plane->ec)
          fb_hwc_on = EINA_TRUE;

        if (!e_plane_fetch(plane)) continue;

        if (output->dpms == E_OUTPUT_DPMS_OFF)
          {
             if (!plane->need_unset_commit)
               e_plane_unfetch(plane);
          }

        if (e_plane_is_fb_target(plane))
          fb_commit = EINA_TRUE;
     }

   if (output->dpms == E_OUTPUT_DPMS_OFF) return EINA_TRUE;

   EINA_LIST_FOREACH(output->planes, l, plane)
     {
        if (plane->need_unset_commit && !fb_hwc_on && !fb_commit)
          continue;

        if (e_plane_is_fb_target(plane) && fb_commit)
          _e_output_update_fps();

        if (!e_plane_commit(plane))
          ERR("fail to e_plane_commit");
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
