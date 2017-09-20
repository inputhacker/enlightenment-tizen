#include "e.h"
#include "Eeze.h"

#define PATH "/org/enlightenment/wm"
#define IFACE "org.enlightenment.wm.screen_rotation"

static Eldbus_Connection *e_comp_screen_conn;
static Eldbus_Service_Interface *e_comp_screen_iface;

static Eina_List *event_handlers = NULL;
#if 0 // E_INPUT
static Eina_Bool session_state = EINA_FALSE;
#endif

static Eina_Bool dont_set_e_input_keymap = EINA_FALSE;
static Eina_Bool dont_use_xkb_cache = EINA_FALSE;

E_API int              E_EVENT_SCREEN_CHANGE = 0;

enum
{
   E_COMP_SCREEN_SIGNAL_ROTATION_CHANGED = 0
};

typedef struct _E_Comp_Screen_Tzsr
{
   struct wl_resource *resource; /* tizen_screen_rotation */
   E_Client           *ec;
} E_Comp_Screen_Tzsr;

static Eina_List *tzsr_list;
static E_Client_Hook *tzsr_client_hook_del;

static E_Comp_Screen_Tzsr*
_tz_surface_rotation_find(E_Client *ec)
{
   E_Comp_Screen_Tzsr *tzsr;
   Eina_List *l;

   EINA_LIST_FOREACH(tzsr_list, l, tzsr)
     {
        if (tzsr->ec == ec)
          return tzsr;
     }

   return NULL;
}

static void
_tz_surface_rotation_free(E_Comp_Screen_Tzsr *tzsr)
{
   ELOGF("COMP", "|tzsr(%p)", NULL, tzsr->ec, tzsr);
   tzsr_list = eina_list_remove(tzsr_list, tzsr);
   free(tzsr);
}

static void
_tz_screen_rotation_cb_client_del(void *data, E_Client *ec)
{
   E_Comp_Screen_Tzsr *tzsr = _tz_surface_rotation_find(ec);
   if (!tzsr) return;
   _tz_surface_rotation_free(tzsr);
}

static void
_tz_screen_rotation_get_ignore_output_transform(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface)
{
   E_Comp_Screen_Tzsr *tzsr;
   E_Client *ec;

   ec = wl_resource_get_user_data(surface);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   tzsr = _tz_surface_rotation_find(ec);
   if (tzsr) return;

   tzsr = E_NEW(E_Comp_Screen_Tzsr, 1);
   if (!tzsr)
     {
        wl_client_post_no_memory(client);
        return;
     }

   tzsr->resource = resource;
   tzsr->ec = ec;

   ELOGF("COMP", "|tzsr(%p)", NULL, ec, tzsr);

   tzsr_list = eina_list_append(tzsr_list, tzsr);
}

static void
_tz_screen_rotation_iface_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct tizen_screen_rotation_interface _tz_screen_rotation_interface =
{
   _tz_screen_rotation_get_ignore_output_transform,
   _tz_screen_rotation_iface_cb_destroy,
};

static void
_tz_screen_rotation_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &tizen_screen_rotation_interface, version, id)))
     {
        ERR("Could not create tizen_screen_rotation resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_tz_screen_rotation_interface, NULL, NULL);
}

static Eldbus_Message *
_e_comp_screen_dbus_get_cb(const Eldbus_Service_Interface *iface, const Eldbus_Message *msg)
{
   Eldbus_Message *reply = eldbus_message_method_return_new(msg);
   int rotation = 0;

   if (e_comp && e_comp->e_comp_screen)
     rotation = e_comp->e_comp_screen->rotation;

   DBG("got screen-rotation 'get' request: %d", rotation);

   eldbus_message_arguments_append(reply, "i", rotation);

   return reply;
}

static const Eldbus_Method methods[] =
{
   {"get", NULL, ELDBUS_ARGS({"i", "int32"}), _e_comp_screen_dbus_get_cb, 0},
   {}
};

static const Eldbus_Signal signals[] = {
   [E_COMP_SCREEN_SIGNAL_ROTATION_CHANGED] = {"changed", ELDBUS_ARGS({ "i", "rotation" }), 0},
   {}
};

static const Eldbus_Service_Interface_Desc iface_desc = {
     IFACE, methods, signals, NULL, NULL, NULL
};

static Eina_Bool
_e_comp_screen_dbus_init(void *data EINA_UNUSED)
{
   E_Comp_Screen *e_comp_screen = e_comp->e_comp_screen;

   if (e_comp_screen_conn) return ECORE_CALLBACK_CANCEL;

   if (!e_comp_screen_conn)
     e_comp_screen_conn = eldbus_connection_get(ELDBUS_CONNECTION_TYPE_SYSTEM);

   if(!e_comp_screen_conn)
     {
        ecore_timer_add(1, _e_comp_screen_dbus_init, NULL);
        return ECORE_CALLBACK_CANCEL;
     }

   e_comp_screen_iface = eldbus_service_interface_register(e_comp_screen_conn,
                                                           PATH,
                                                           &iface_desc);
   EINA_SAFETY_ON_NULL_GOTO(e_comp_screen_iface, err);

   if (e_comp_screen->rotation)
     {
        eldbus_service_signal_emit(e_comp_screen_iface, E_COMP_SCREEN_SIGNAL_ROTATION_CHANGED, e_comp_screen->rotation);
        ELOGF("TRANSFORM", "screen-rotation sends signal: %d", NULL, NULL, e_comp_screen->rotation);
     }

   return ECORE_CALLBACK_CANCEL;

err:
   if (e_comp_screen_conn)
     {
        eldbus_connection_unref(e_comp_screen_conn);
        e_comp_screen_conn = NULL;
     }

   return ECORE_CALLBACK_CANCEL;
}

static char *
_layer_cap_to_str(tdm_layer_capability caps, tdm_layer_capability cap)
{
   if (caps & cap)
     {
        if (cap == TDM_LAYER_CAPABILITY_CURSOR) return "cursor ";
        else if (cap == TDM_LAYER_CAPABILITY_PRIMARY) return "primary ";
        else if (cap == TDM_LAYER_CAPABILITY_OVERLAY) return "overlay ";
        else if (cap == TDM_LAYER_CAPABILITY_GRAPHIC) return "graphics ";
        else if (cap == TDM_LAYER_CAPABILITY_VIDEO) return "video ";
        else if (cap == TDM_LAYER_CAPABILITY_TRANSFORM) return "transform ";
        else if (cap == TDM_LAYER_CAPABILITY_RESEVED_MEMORY) return "reserved_memory ";
        else if (cap == TDM_LAYER_CAPABILITY_NO_CROP) return "no_crop ";
        else return "unkown";
     }
   return "";
}

#if 0
static Eina_Bool
_e_comp_screen_cb_output_drm(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   const Eina_List *l;
   E_Output *output;
   Ecore_Drm_Event_Output *e;
   E_Comp_Screen *e_comp_screen = NULL;

   if (!(e = event)) goto end;
   if (!e_comp) goto end;
   if (!e_comp->e_comp_screen) goto end;

   e_comp_screen = e_comp->e_comp_screen;

   DBG("WL_DRM OUTPUT CHANGE");

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if ((!strcmp(output->info.name, e->name)) &&
            (!strcmp(output->info.screen, e->model)))
          {
             if (e->plug)
               {
                  if (!e_comp_wl_output_init(output->id, e->make, e->model,
                                             e->x, e->y, e->w, e->h,
                                             e->phys_width, e->phys_height,
                                             e->refresh, e->subpixel_order,
                                             e->transform))
                    {
                       ERR("Could not setup new output: %s", output->id);
                    }
               }
             else
               e_comp_wl_output_remove(output->id);

             break;
          }
     }

   /* previous calculation of e_scale gave unsuitable value because
    * there were no sufficient information to calculate dpi.
    * so it's considerable to re-calculate e_scale with output geometry.
    */
   double target_inch;
   int dpi;

   target_inch = (round((sqrt(e->phys_width * e->phys_width + e->phys_height * e->phys_height) / 25.4) * 10) / 10);
   dpi = (round((sqrt(e->w * e->w + e->h * e->h) / target_inch) * 10) / 10);

   e_scale_manual_update(dpi);

end:
   return ECORE_CALLBACK_PASS_ON;
}
#endif

static Eina_Bool
_e_comp_screen_commit_idle_cb(void *data EINA_UNUSED)
{
   Eina_List *l, *ll;
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;

   if (!e_comp->e_comp_screen) goto end;

   e_comp_screen = e_comp->e_comp_screen;

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        if (!output) continue;
        if (!output->config.enabled) continue;

        if (!e_output_commit(output))
             ERR("fail to commit e_comp_screen->outputs.");

        if (!e_output_render(output))
             ERR("fail to render e_comp_screen->outputs.");

     }

end:
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_comp_screen_cb_input_device_add(void *data, int type, void *event)
{
   E_Input_Event_Input_Device_Add *e;
   E_Comp *comp = data;

   if (!(e = event)) goto end;

   if (e->caps & E_INPUT_SEAT_POINTER)
     {
        if (comp->wl_comp_data->ptr.num_devices == 0)
          {
             e_pointer_object_set(comp->pointer, NULL, 0, 0);
             e_comp_wl_input_pointer_enabled_set(EINA_TRUE);
          }
        comp->wl_comp_data->ptr.num_devices++;
     }
   if (e->caps & E_INPUT_SEAT_KEYBOARD)
     {
        comp->wl_comp_data->kbd.num_devices++;
        e_comp_wl_input_keyboard_enabled_set(EINA_TRUE);
     }
   if (e->caps & E_INPUT_SEAT_TOUCH)
     {
        e_comp_wl_input_touch_enabled_set(EINA_TRUE);
        comp->wl_comp_data->touch.num_devices++;
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_comp_screen_cb_input_device_del(void *data, int type, void *event)
{
   E_Input_Event_Input_Device_Del *e;
   E_Comp *comp = data;

   if (!(e = event)) goto end;

   if (e->caps & E_INPUT_SEAT_POINTER)
     {
        comp->wl_comp_data->ptr.num_devices--;
        if (comp->wl_comp_data->ptr.num_devices == 0)
          {
             e_comp_wl_input_pointer_enabled_set(EINA_FALSE);
             e_pointer_object_set(comp->pointer, NULL, 0, 0);
             e_pointer_hide(e_comp->pointer);
          }
     }
   if (e->caps & E_INPUT_SEAT_KEYBOARD)
     {
        comp->wl_comp_data->kbd.num_devices--;
        if (comp->wl_comp_data->kbd.num_devices == 0)
          {
             e_comp_wl_input_keyboard_enabled_set(EINA_FALSE);
          }
     }
   if (e->caps & E_INPUT_SEAT_TOUCH)
     {
        comp->wl_comp_data->touch.num_devices--;
        if (comp->wl_comp_data->touch.num_devices == 0)
          {
             e_comp_wl_input_touch_enabled_set(EINA_FALSE);
          }
     }

end:

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_comp_screen_cb_ee_resize(Ecore_Evas *ee EINA_UNUSED)
{
   e_comp_canvas_update();
}

static E_Comp_Screen *
_e_comp_screen_new(E_Comp *comp)
{
   E_Comp_Screen *e_comp_screen = NULL;
   tdm_error error = TDM_ERROR_NONE;
   tdm_display_capability capabilities;
   const tbm_format *pp_formats;
   int count, i;

   e_comp_screen = E_NEW(E_Comp_Screen, 1);
   if (!e_comp_screen) return NULL;

   /* tdm display init */
   e_comp_screen->tdisplay = tdm_display_init(&error);
   if (!e_comp_screen->tdisplay)
     {
        ERR("fail to get tdm_display\n");
        free(e_comp_screen);
        return NULL;
     }

   /* tdm display init */
   e_comp_screen->bufmgr = tbm_bufmgr_init(-1);
   if (!e_comp_screen->bufmgr)
     {
        ERR("tbm_bufmgr_init failed\n");
        tdm_display_deinit(e_comp_screen->tdisplay);
        free(e_comp_screen);
        return NULL;
     }

   error = tdm_display_get_capabilities(e_comp_screen->tdisplay, &capabilities);
   if (error != TDM_ERROR_NONE)
     {
        ERR("tdm get_capabilities failed");
        tbm_bufmgr_deinit(e_comp_screen->bufmgr);
        tdm_display_deinit(e_comp_screen->tdisplay);
        free(e_comp_screen);
        return NULL;
     }

   /* check the pp_support */
   if (capabilities & TDM_DISPLAY_CAPABILITY_PP)
     {
        error = tdm_display_get_pp_available_formats(e_comp_screen->tdisplay, &pp_formats, &count);
        if (error != TDM_ERROR_NONE)
          ERR("fail to get available pp formats");
        else
          {
             e_comp_screen->pp_enabled = EINA_TRUE;
             for (i = 0 ; i < count ; i++)
               e_comp_screen->available_pp_formats = eina_list_append(e_comp_screen->available_pp_formats, &pp_formats[i]);
          }
     }

   if (e_comp_socket_init("tdm-socket"))
     PRCTL("[Winsys] change permission and create sym link for %s", "tdm-socket");

   return e_comp_screen;
}

static void
_e_comp_screen_del(E_Comp_Screen *e_comp_screen)
{
   Eina_List *l = NULL, *ll = NULL;
   tbm_format *formats;

   if (!e_comp_screen) return;

   if (e_comp_screen->pp_enabled)
     {
        EINA_LIST_FOREACH_SAFE(e_comp_screen->available_pp_formats, l, ll, formats)
          {
             if (!formats) continue;
             e_comp_screen->available_pp_formats = eina_list_remove(e_comp_screen->available_pp_formats, l);
          }
     }
   if (e_comp_screen->bufmgr) tbm_bufmgr_deinit(e_comp_screen->bufmgr);
   if (e_comp_screen->tdisplay) tdm_display_deinit(e_comp_screen->tdisplay);

   free(e_comp_screen);
}

static void
_e_comp_screen_deinit_outputs(E_Comp_Screen *e_comp_screen)
{
   E_Output *output;
   Eina_List *l, *ll;

   // free up e_outputs
   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        e_comp_screen->outputs = eina_list_remove_list(e_comp_screen->outputs, l);
        e_output_del(output);
     }

   e_output_shutdown();
}

static Eina_Bool
_e_comp_screen_init_outputs(E_Comp_Screen *e_comp_screen)
{
   E_Output *output = NULL;
   E_Output_Mode *mode = NULL;
   tdm_display *tdisplay = e_comp_screen->tdisplay;
   int num_outputs;
   int i;
   Eina_Bool scale_updated = EINA_FALSE;

   /* init e_output */
   if (!e_output_init())
     {
        ERR("fail to e_output_init.");
        return EINA_FALSE;
     }

   /* get the num of outputs */
   tdm_display_get_output_count(tdisplay, &num_outputs);
   if (num_outputs < 1)
     {
        ERR("fail to get tdm_display_get_output_count\n");
        return EINA_FALSE;
     }
   e_comp_screen->num_outputs = num_outputs;

   INF("E_COMP_SCREEN: num_outputs = %i", e_comp_screen->num_outputs);

   for (i = 0; i < num_outputs; i++)
     {
        output = e_output_new(e_comp_screen, i);
        if (!output) goto fail;

        if (!e_output_update(output))
          {
            ERR("fail to e_output_update.");
            goto fail;
          }

        e_comp_screen->outputs = eina_list_append(e_comp_screen->outputs, output);

        if (!e_output_connected(output)) continue;

        /* setting with the best mode and enable the output */
        mode = e_output_best_mode_find(output);
        if (!mode)
          {
             ERR("fail to get best mode.");
             goto fail;
          }

        if (!e_output_mode_apply(output, mode))
          {
             ERR("fail to e_output_mode_apply.");
             goto fail;
          }
        if (!e_output_dpms_set(output, E_OUTPUT_DPMS_ON))
          {
             ERR("fail to e_output_dpms.");
             goto fail;
          }

        /* update e_scale with first available output size */
        if ((e_config->scale.for_tdm) && (!scale_updated))
          {
             double target_inch;
             int dpi;

             target_inch = (round((sqrt(output->info.size.w * output->info.size.w + output->info.size.h * output->info.size.h) / 25.4) * 10) / 10);
             dpi = (round((sqrt(mode->w * mode->w + mode->h * mode->h) / target_inch) * 10) / 10);

             e_scale_manual_update(dpi);
             scale_updated = EINA_TRUE;
          }
     }

   //TODO: if there is no output connected, make the fake output which is connected.


   return EINA_TRUE;
fail:
   _e_comp_screen_deinit_outputs(e_comp_screen);

   return EINA_FALSE;
}

E_API void
_e_comp_screen_keymap_set(struct xkb_context **ctx, struct xkb_keymap **map)
{
   char *keymap_path = NULL;
   struct xkb_context *context;
   struct xkb_keymap *keymap;
   struct xkb_rule_names names = {0,};
   const char* default_rules, *default_model, *default_layout, *default_variant, *default_options;

   TRACE_INPUT_BEGIN(_e_comp_screen_keymap_set);

   context = xkb_context_new(0);
   EINA_SAFETY_ON_NULL_RETURN(context);

   /* assemble xkb_rule_names so we can fetch keymap */
   memset(&names, 0, sizeof(names));

   default_rules = e_comp_wl_input_keymap_default_rules_get();
   default_model = e_comp_wl_input_keymap_default_model_get();
   default_layout = e_comp_wl_input_keymap_default_layout_get();
   default_variant = e_comp_wl_input_keymap_default_variant_get();
   default_options = e_comp_wl_input_keymap_default_options_get();

   names.rules = strdup(default_rules);
   names.model = strdup(default_model);
   names.layout = strdup(default_layout);
   if (default_variant) names.variant = strdup(default_variant);
   if (default_options) names.options = strdup(default_options);

   keymap = e_comp_wl_input_keymap_compile(context, names, &keymap_path);
   eina_stringshare_del(keymap_path);
   EINA_SAFETY_ON_NULL_GOTO(keymap, cleanup);

   *ctx = context;
   *map = keymap;

   if (dont_set_e_input_keymap == EINA_FALSE)
     {
        e_input_device_keyboard_cached_context_set(*ctx);
        e_input_device_keyboard_cached_keymap_set(*map);
     }

cleanup:
   free((char *)names.rules);
   free((char *)names.model);
   free((char *)names.layout);
   if (names.variant) free((char *)names.variant);
   if (names.options) free((char *)names.options);

   TRACE_INPUT_END();
}

static int
_e_comp_screen_e_screen_sort_cb(const void *data1, const void *data2)
{
   const E_Output *s1 = data1, *s2 = data2;
   int dif;

   dif = -(s1->config.priority - s2->config.priority);
   if (dif == 0)
     {
        dif = s1->config.geom.x - s2->config.geom.x;
        if (dif == 0)
          dif = s1->config.geom.y - s2->config.geom.y;
     }
   return dif;
}

static void
_e_comp_screen_e_screen_free(E_Screen *scr)
{
   free(scr->id);
   free(scr);
}

static void
_e_comp_screen_e_screens_set(E_Comp_Screen *e_comp_screen, Eina_List *screens)
{
   E_FREE_LIST(e_comp_screen->e_screens, _e_comp_screen_e_screen_free);
   e_comp_screen->e_screens = screens;
}

static void
_e_comp_screen_engine_deinit(void)
{
   if (!e_comp) return;
   if (!e_comp->e_comp_screen) return;

   _e_comp_screen_deinit_outputs(e_comp->e_comp_screen);
   _e_comp_screen_del(e_comp->e_comp_screen);
   tbm_surface_queue_destroy(e_comp->e_comp_screen->tqueue);
   e_comp->e_comp_screen = NULL;
}

static Eina_Bool
_e_comp_screen_engine_init(void)
{
   E_Comp_Screen *e_comp_screen = NULL;
   int w = 0, h = 0, scr_w = 1, scr_h = 1;
   int screen_rotation;
   char buf[1024];
   E_Output *output = NULL;
   tbm_surface_queue_h tqueue = NULL;

   INF("ecore evase engine init with TDM. HWC.");

   /* check the screen rotation */
   screen_rotation = (e_config->screen_rotation_pre + e_config->screen_rotation_setting) % 360;

   INF("E_COMP_SCREEN: screen_rotation_pre %d and screen_rotation_setting %d",
       e_config->screen_rotation_pre, e_config->screen_rotation_setting);

   /* set env for use tbm_surface_queue*/
   setenv("USE_EVAS_SOFTWARE_TBM_ENGINE", "1", 1);
   //setenv("USE_EVAS_GL_TBM_ENGINE", "1", 1);

   /* set gl available if we have ecore_evas support */
   if (ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_DRM) ||
       ecore_evas_engine_type_supported_get(ECORE_EVAS_ENGINE_OPENGL_TBM))
     e_comp_gl_set(EINA_TRUE);

   /* e_comp_screen new */
   e_comp_screen = _e_comp_screen_new(e_comp);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);

   e_comp->e_comp_screen = e_comp_screen;
   e_comp_screen->rotation = screen_rotation;

   e_main_ts("\tE_Outputs Init");
   if (!_e_comp_screen_init_outputs(e_comp_screen))
     {
        e_error_message_show(_("Enlightenment cannot initialize outputs!\n"));
        _e_comp_screen_engine_deinit();
        return EINA_FALSE;
     }
   e_main_ts("\tE_Outputs Init Done");

   /* get the primary output */
   output = e_comp_screen_primary_output_get(e_comp_screen);
   if (!output)
     {
        e_error_message_show(_("Fail to get the primary output!\n"));
        _e_comp_screen_engine_deinit();
        return EINA_FALSE;
     }

   /* get the size of the primary output */
   e_output_size_get(output, &scr_w, &scr_h);

   /* if output is disconnected, set the default width, height */
   if (scr_w == 0 || scr_h == 0)
     {
        scr_w = 1;
        scr_h = 1;

        if (!e_output_fake_config_set(output, w, h))
          {
             e_error_message_show(_("Fail to set the fake output config!\n"));
             _e_comp_screen_engine_deinit();
             return EINA_FALSE;
          }
     }

   INF("GL available:%d config engine:%d screen size:%dx%d",
       e_comp_gl_get(), e_comp_config_get()->engine, scr_w, scr_h);

   tqueue = tbm_surface_queue_create(3, scr_w, scr_h, TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   if (!tqueue)
     {
        e_error_message_show(_("Fail to create tbm_surface_queue!\n"));
       _e_comp_screen_engine_deinit();
       return EINA_FALSE;
     }

   e_comp_screen->tqueue = tqueue;

   if ((e_comp_gl_get()) &&
       (e_comp_config_get()->engine == E_COMP_ENGINE_GL))
     {
        e_main_ts("\tEE_GL_DRM New");
        e_comp->ee = ecore_evas_tbm_ext_new("gl_tbm", tqueue, NULL);
        snprintf(buf, sizeof(buf), "\tEE_GL_DRM New Done %p %dx%d", e_comp->ee, scr_w, scr_h);
        e_main_ts(buf);

        if (!e_comp->ee)
          e_comp_gl_set(EINA_FALSE);
        else
          {
             Evas_GL *evasgl = NULL;
             Evas_GL_API *glapi = NULL;

             e_main_ts("\tEvas_GL New");
             evasgl = evas_gl_new(ecore_evas_get(e_comp->ee));
             if (evasgl)
               {
                  glapi = evas_gl_api_get(evasgl);
                  if (!((glapi) && (glapi->evasglBindWaylandDisplay)))
                    {
                       e_comp_gl_set(EINA_FALSE);
                       ecore_evas_free(e_comp->ee);
                       e_comp->ee = NULL;
                       e_main_ts("\tEvas_GL New Failed 1");
                    }
                  else
                    {
                       e_main_ts("\tEvas_GL New Done");
                    }
               }
             else
               {
                  e_comp_gl_set(EINA_FALSE);
                  ecore_evas_free(e_comp->ee);
                  e_comp->ee = NULL;
                  e_main_ts("\tEvas_GL New Failed 2");
               }
             evas_gl_free(evasgl);
          }
     }

   /* fallback to framebuffer drm (non-accel) */
   if (!e_comp->ee)
     {
        e_main_ts("\tEE_DRM New");
        e_comp->ee = ecore_evas_tbm_ext_new("software_tbm", tqueue, NULL);
        snprintf(buf, sizeof(buf), "\tEE_DRM New Done %p %dx%d", e_comp->ee, scr_w, scr_h);
        e_main_ts(buf);
     }

   if (!e_comp->ee)
     {
        e_error_message_show(_("Enlightenment cannot initialize outputs!\n"));
        _e_comp_screen_engine_deinit();
        return EINA_FALSE;
     }

   ecore_evas_data_set(e_comp->ee, "comp", e_comp);

   ecore_evas_callback_resize_set(e_comp->ee, _e_comp_screen_cb_ee_resize);

   if (screen_rotation)
     {
        /* SHOULD called with resize option after ecore_evas_resize */
        ecore_evas_rotation_with_resize_set(e_comp->ee, screen_rotation);
        ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

        snprintf(buf, sizeof(buf), "\tEE Rotate and Resize %d, %dx%d", screen_rotation, w, h);
        e_main_ts(buf);
     }

   if (!E_EVENT_SCREEN_CHANGE) E_EVENT_SCREEN_CHANGE = ecore_event_type_new();

   ecore_event_add(E_EVENT_SCREEN_CHANGE, NULL, NULL, NULL);

   e_comp_screen_e_screens_setup(e_comp_screen, -1, -1);

   /* TODO: need the output changed handler with TDM */
   //E_LIST_HANDLER_APPEND(event_handlers, ECORE_DRM_EVENT_OUTPUT,        _e_comp_screen_cb_output_drm,       comp);

   /* update the screen, outputs and planes at the idle enterer of the ecore_loop */
   ecore_idle_enterer_add(_e_comp_screen_commit_idle_cb, e_comp);

   return EINA_TRUE;
}

EINTERN void
e_comp_screen_e_screens_setup(E_Comp_Screen *e_comp_screen, int rw, int rh)
{
   int i;
   E_Screen *screen;
   Eina_List *outputs = NULL, *outputs_rem;
   Eina_List *e_screens = NULL;
   Eina_List *l, *ll;
   E_Output *output, *s2, *s_chosen;
   Eina_Bool removed;

   if (!e_comp_screen->outputs) goto out;
   // put screens in tmp list
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if ((output->config.enabled) &&
            (output->config.geom.w > 0) &&
            (output->config.geom.h > 0))
          {
             outputs = eina_list_append(outputs, output);
          }
     }
   // remove overlapping screens - if a set of screens overlap, keep the
   // smallest/lowest res
   do
     {
        removed = EINA_FALSE;

        EINA_LIST_FOREACH(outputs, l, output)
          {
             outputs_rem = NULL;

             EINA_LIST_FOREACH(l->next, ll, s2)
               {
                  if (E_INTERSECTS(output->config.geom.x, output->config.geom.y,
                                   output->config.geom.w, output->config.geom.h,
                                   s2->config.geom.x, s2->config.geom.y,
                                   s2->config.geom.w, s2->config.geom.h))
                    {
                       if (!outputs_rem)
                         outputs_rem = eina_list_append(outputs_rem, output);
                       outputs_rem = eina_list_append(outputs_rem, s2);
                    }
               }
             // we have intersecting screens - choose the lowest res one
             if (outputs_rem)
               {
                  removed = EINA_TRUE;
                  // find the smallest screen (chosen one)
                  s_chosen = NULL;
                  EINA_LIST_FOREACH(outputs_rem, ll, s2)
                    {
                       if (!s_chosen) s_chosen = s2;
                       else
                         {
                            if ((s_chosen->config.geom.w *
                                 s_chosen->config.geom.h) >
                                (s2->config.geom.w *
                                 s2->config.geom.h))
                              s_chosen = s2;
                         }
                    }
                  // remove all from screens but the chosen one
                  EINA_LIST_FREE(outputs_rem, s2)
                    {
                       if (s2 != s_chosen)
                         outputs = eina_list_remove_list(outputs, l);
                    }
                  // break our list walk and try again
                  break;
               }
          }
     }
   while (removed);
   // sort screens by priority etc.
   outputs = eina_list_sort(outputs, 0, _e_comp_screen_e_screen_sort_cb);
   i = 0;
   EINA_LIST_FOREACH(outputs, l, output)
     {
        screen = E_NEW(E_Screen, 1);
        if (!screen) continue;
        screen->escreen = screen->screen = i;
        screen->x = output->config.geom.x;
        screen->y = output->config.geom.y;

        if (output->config.rotation % 180)
          {
             screen->w = output->config.geom.h;
             screen->h = output->config.geom.w;
          }
        else
          {
             screen->w = output->config.geom.w;
             screen->h = output->config.geom.h;
          }

        if (output->id) screen->id = strdup(output->id);

        e_screens = eina_list_append(e_screens, screen);
        INF("E INIT: SCREEN: [%i][%i], %ix%i+%i+%i",
            i, i, screen->w, screen->h, screen->x, screen->y);
        i++;
     }
   eina_list_free(outputs);
   // if we have NO screens at all (above - i will be 0) AND we have no
   // existing screens set up in xinerama - then just say root window size
   // is the entire screen. this should handle the case where you unplug ALL
   // screens from an existing setup (unplug external monitors and/or close
   // laptop lid), in which case as long as at least one screen is configured
   // in xinerama, it will be left-as is until next time we re-eval screen
   // setup and have at least one screen
   printf("e_comp_screen_e_screens_setup............... %i %p\n", i, e_comp_screen->e_screens);
   if ((i == 0) && (!e_comp_screen->e_screens))
     {
out:
        screen = E_NEW(E_Screen, 1);
        if (!screen) return;
        screen->escreen = screen->screen = 0;
        screen->x = 0;
        screen->y = 0;
        if ((rw > 0) && (rh > 0))
          screen->w = rw, screen->h = rh;
        else
          {
             if (e_comp_screen->rotation % 180)
               ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &screen->h, &screen->w);
             else
               ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &screen->w, &screen->h);
          }
        e_screens = eina_list_append(e_screens, screen);
     }
   _e_comp_screen_e_screens_set(e_comp_screen, e_screens);
}

EINTERN const Eina_List *
e_comp_screen_e_screens_get(E_Comp_Screen *e_comp_screen)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, NULL);

   return e_comp_screen->e_screens;
}

E_API Eina_Bool
e_comp_screen_init()
{
   E_Comp *comp;
   int w, h;
   struct xkb_context *ctx = NULL;
   struct xkb_keymap *map = NULL;

   TRACE_DS_BEGIN(E_COMP_SCREEN:INIT);
   if (!(comp = e_comp))
     {
        TRACE_DS_END();
        EINA_SAFETY_ON_NULL_RETURN_VAL(comp, EINA_FALSE);
     }

   /* keymap */
   dont_set_e_input_keymap = getenv("NO_E_INPUT_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;
   dont_use_xkb_cache = getenv("NO_KEYMAP_CACHE") ? EINA_TRUE : EINA_FALSE;

   if (e_config->xkb.use_cache && !dont_use_xkb_cache)
     {
        e_main_ts("\tKeymap Init");
        _e_comp_screen_keymap_set(&ctx, &map);
        e_main_ts("\tKeymap Init Done");
     }

   if (!e_input_init())
     {
        ERR("Could not initialize the e_input.");
        goto failed_comp_screen;
     }

   if (!_e_comp_screen_engine_init())
     {
        ERR("Could not initialize the ecore_evas engine.");
        goto failed_comp_screen;
     }

   e_main_ts("\tE_Comp_Wl Init");
   if (!e_comp_wl_init())
     {
        goto failed_comp_screen;
     }
   e_main_ts("\tE_Comp_Wl Init Done");

   /* get the current screen geometry */
   ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

   /* canvas */
   e_main_ts("\tE_Comp_Canvas Init");
   if (!e_comp_canvas_init(w, h))
     {
        e_error_message_show(_("Enlightenment cannot initialize outputs!\n"));
        goto failed_comp_screen;
     }
   e_main_ts("\tE_Comp_Canvas Init Done");

   /* pointer */
   ecore_evas_pointer_xy_get(e_comp->ee,
                             &e_comp_wl->ptr.x,
                             &e_comp_wl->ptr.y);

   evas_event_feed_mouse_in(e_comp->evas, 0, NULL);

   e_main_ts("\tE_Pointer New");
   if ((comp->pointer = e_pointer_canvas_new(comp->ee, EINA_TRUE)))
     {
        e_pointer_hide(comp->pointer);
     }
   e_main_ts("\tE_Pointer New Done");

   /* FIXME: We need a way to trap for user changing the keymap inside of E
    *        without the event coming from X11 */

   /* FIXME: We should make a decision here ...
    *
    * Fetch the keymap from drm, OR set this to what the E config is....
    */

   /* FIXME: This is just for testing at the moment....
    * happens to jive with what drm does */
   e_main_ts("\tE_Comp_WL Keymap Init");
   e_comp_wl_input_keymap_set(e_comp_wl_input_keymap_default_rules_get(),
                              e_comp_wl_input_keymap_default_model_get(),
                              e_comp_wl_input_keymap_default_layout_get(),
                              e_comp_wl_input_keymap_default_variant_get(),
                              e_comp_wl_input_keymap_default_options_get(),
                              ctx, map);
   e_main_ts("\tE_Comp_WL Keymap Init Done");

   /* try to add tizen_video to wayland globals */
   if (!wl_global_create(e_comp_wl->wl.disp, &tizen_screen_rotation_interface, 1,
                         NULL, _tz_screen_rotation_cb_bind))
     {
        ERR("Could not add tizen_screen_rotation to wayland globals");
        goto failed_comp_screen;
     }

   /* this setup function is called after e_comp_canvas_init */
   if (!e_comp_screen_setup(e_comp->e_comp_screen))
     {
        ERR("fail to e_comp_screen_setup");
        return EINA_FALSE;
     }

   if (eldbus_init() == 0)
     {
        ERR("eldbus_init failed");
        goto failed_comp_screen;
     }

   _e_comp_screen_dbus_init(NULL);

   tzsr_client_hook_del = e_client_hook_add(E_CLIENT_HOOK_DEL, _tz_screen_rotation_cb_client_del, NULL);

   E_LIST_HANDLER_APPEND(event_handlers, E_INPUT_EVENT_INPUT_DEVICE_ADD, _e_comp_screen_cb_input_device_add, comp);
   E_LIST_HANDLER_APPEND(event_handlers, E_INPUT_EVENT_INPUT_DEVICE_DEL, _e_comp_screen_cb_input_device_del, comp);

   TRACE_DS_END();

   return EINA_TRUE;

failed_comp_screen:

   e_input_shutdown();
   _e_comp_screen_engine_deinit();

   TRACE_DS_END();

   return EINA_FALSE;
}

E_API void
e_comp_screen_shutdown()
{
   if (!e_comp) return;
   if (!e_comp->e_comp_screen) return;

   /* shutdown ecore_drm */
   /* ecore_drm_shutdown(); */

   if (e_comp_screen_iface)
     {
        eldbus_service_interface_unregister(e_comp_screen_iface);
        e_comp_screen_iface = NULL;
     }

   if (e_comp_screen_conn)
     {
        eldbus_connection_unref(e_comp_screen_conn);
        e_comp_screen_conn = NULL;
     }

   eldbus_shutdown();

   _e_comp_screen_deinit_outputs(e_comp->e_comp_screen);

   e_client_hook_del(tzsr_client_hook_del);
   tzsr_client_hook_del = NULL;

   dont_set_e_input_keymap = EINA_FALSE;
   dont_use_xkb_cache = EINA_FALSE;
   E_FREE_LIST(event_handlers, ecore_event_handler_del);

   /* delete e_comp_sreen */
   _e_comp_screen_del(e_comp->e_comp_screen);
   e_comp->e_comp_screen = NULL;
}

EINTERN Eina_Bool
e_comp_screen_setup(E_Comp_Screen *e_comp_screen)
{
   Eina_List *l, *ll;
   E_Output *output = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l, ll, output)
     {
        if (!output) continue;
        if (!output->config.enabled) continue;

        if (!e_output_setup(output))
          {
             ERR("fail to e_ouptut_hwc_setup.");
             continue;
          }
     }

   return EINA_TRUE;
}

E_API Eina_Bool
e_comp_screen_rotation_setting_set(E_Comp_Screen *e_comp_screen, int rotation)
{
   E_Output *output = NULL, *o;
   Eina_List *l;
   int w, h;
   int screen_rotation;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(rotation % 90, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(rotation < 0, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(rotation > 270, EINA_FALSE);

   if (e_config->screen_rotation_setting == rotation) return EINA_TRUE;

   EINA_LIST_FOREACH(e_comp_screen->outputs, l, o)
     {
        unsigned int pipe = 0;
        tdm_error error;

        error = tdm_output_get_pipe(o->toutput, &pipe);
        if (error != TDM_ERROR_NONE || pipe != 0)
          continue;

        output = o;
        break;
     }

   if (!output)
     {
        ERR("couldn't find the primary output");
        return EINA_FALSE;
     }

   screen_rotation = (e_config->screen_rotation_pre + rotation) % 360;

   if (!e_output_rotate(output, screen_rotation))
     return EINA_FALSE;

   /* TODO: need to save e_config->screen_rotation_setting to e_config data file */
   e_config->screen_rotation_setting = rotation;
   e_comp_screen->rotation = screen_rotation;

   ecore_evas_rotation_with_resize_set(e_comp->ee, e_comp_screen->rotation);
   ecore_evas_geometry_get(e_comp->ee, NULL, NULL, &w, &h);

   if (e_comp_screen_iface)
     {
        eldbus_service_signal_emit(e_comp_screen_iface, E_COMP_SCREEN_SIGNAL_ROTATION_CHANGED, e_comp_screen->rotation);
        ELOGF("TRANSFORM", "screen-rotation sends signal: %d", NULL, NULL, e_comp_screen->rotation);
     }

   INF("EE Rotated and Resized: %d, %dx%d", e_comp_screen->rotation, w, h);

   return EINA_TRUE;
}

E_API void
e_comp_screen_rotation_ignore_output_transform_send(E_Client *ec, Eina_Bool ignore)
{
   E_Comp_Screen_Tzsr *tzsr = _tz_surface_rotation_find(ec);

   if (!tzsr) return;

   ELOGF("COMP", "|tzsr(%p) ignore(%d)", NULL, ec, tzsr, ignore);

   tizen_screen_rotation_send_ignore_output_transform(tzsr->resource, ec->comp_data->surface, ignore);
}

EINTERN E_Output *
e_comp_screen_primary_output_get(E_Comp_Screen *e_comp_screen)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, NULL);

   E_Output *output = NULL, *o = NULL;
   Eina_List *l = NULL;
   int highest_priority = 0;

   /* find the highest priority of the e_output */
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, o)
     {
        if (highest_priority < o->config.priority)
          {
             highest_priority = o->config.priority;
             output = o;
          }
     }

   return output;
}

EINTERN Eina_Bool
e_comp_screen_pp_support(void)
{
   E_Comp_Screen *e_comp_screen = NULL;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);

   e_comp_screen = e_comp->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen->tdisplay, EINA_FALSE);

   return e_comp_screen->pp_enabled;
}


EINTERN Eina_List *
e_comp_screen_pp_available_formats_get(void)
{
  E_Comp_Screen *e_comp_screen = NULL;
  EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);

  e_comp_screen = e_comp->e_comp_screen;
  EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);
  EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen->tdisplay, EINA_FALSE);

  if (!e_comp_screen->pp_enabled)
    {
       ERR("pp does not support.");
       return NULL;
    }

   return e_comp_screen->available_pp_formats;
}

EINTERN void
e_comp_screen_hwc_info_debug(void)
{
   EINA_SAFETY_ON_NULL_RETURN(e_comp);
   EINA_SAFETY_ON_NULL_RETURN(e_comp->e_comp_screen);

   E_Comp_Screen *e_comp_screen = e_comp->e_comp_screen;
   E_Output *output = NULL;
   E_Plane *plane = NULL;
   Eina_List *l_o, *ll_o;
   Eina_List *l_l, *ll_l;
   tdm_output_conn_status conn_status;
   int output_idx = 0;
   tdm_layer_capability layer_capabilities;
   char layer_cap[4096] = {0, };

   INF("HWC: HWC Information ==========================================================");
   EINA_LIST_FOREACH_SAFE(e_comp_screen->outputs, l_o, ll_o, output)
     {
        if (!output) continue;
        tdm_output_get_conn_status(output->toutput, &conn_status);
        if (conn_status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED) continue;

        INF("HWC: HWC Output(%d):(x, y, w, h)=(%d, %d, %d, %d) Information.",
            ++output_idx,
            output->config.geom.x, output->config.geom.y, output->config.geom.w, output->config.geom.h);
        INF("HWC:  num_layers=%d", output->plane_count);
        EINA_LIST_FOREACH_SAFE(output->planes, l_l, ll_l, plane)
          {
              if (!plane) continue;
              tdm_layer_get_capabilities(plane->tlayer, &layer_capabilities);
              snprintf(layer_cap, sizeof(layer_cap), "%s%s%s%s%s%s%s%s",
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_CURSOR),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_PRIMARY),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_OVERLAY),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_GRAPHIC),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_VIDEO),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_TRANSFORM),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_RESEVED_MEMORY),
                       _layer_cap_to_str(layer_capabilities, TDM_LAYER_CAPABILITY_NO_CROP));
              INF("HWC:  index=%d zpos=%d ec=%p %s",
                  plane->index, plane->zpos,
                  plane->ec?plane->ec:NULL,
                  layer_cap);
          }
     }
   INF("HWC: =========================================================================");
}
