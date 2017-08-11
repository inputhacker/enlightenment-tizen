#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <xdg-shell-server-protocol.h>
#include <eom-server-protocol.h>
#include <Ecore_Drm.h>
#include <tdm.h>
#include <eom.h>
#include <tbm_bufmgr.h>
#include <tbm_surface.h>
#include <wayland-tbm-server.h>
#ifdef FRAMES
#include <time.h>
#endif

/*
#define EOM_DUMP_MIRROR_BUFFERS
#define EOM_DUMP_PRESENTATION_BUFFERS
*/

#define ALEN(array) (sizeof(array) / sizeof(array)[0])

#define EOMER(msg, ARG...) ERR("[EOM][ERR] " msg, ##ARG)
#define EOMWR(msg, ARG...) WRN("[EOM][WRN] " msg, ##ARG)
#define EOMIN(msg, ARG...) INF("[EOM][INF] " msg, ##ARG)
#define EOMDB(msg, ARG...) DBG("[EOM][DBG] " msg, ##ARG)

#define EOM_NUM_ATTR 3
#define EOM_CONNECT_CHECK_TIMEOUT 7.0
#define EOM_DELAY_CHECK_TIMEOUT 4.0
#define EOM_ROTATE_DELAY_TIMEOUT 0.4

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

typedef struct _E_Eom E_Eom, *E_EomPtr;
typedef struct _E_Eom_Out_Mode E_EomOutMode, *E_EomOutModePtr;
typedef struct _E_Eom_Output E_EomOutput, *E_EomOutputPtr;
typedef struct _E_Eom_Client E_EomClient, *E_EomClientPtr;
typedef struct _E_Eom_Comp_Object_Intercept_Hook_Data E_EomCompObjectInterceptHookData;
typedef struct _E_Eom_Output_Buffer E_EomOutputBuffer, *E_EomOutputBufferPtr;
typedef struct _E_Eom_Buffer E_EomBuffer, *E_EomBufferPtr;
typedef struct _E_Eom_Output_Pp E_EomOutputPp, *E_EomOutputPpPtr;
typedef struct _E_Eom_Pp_Data E_EomPpData, *E_EomPpDataPtr;
typedef void(*E_EomEndShowingEventPtr)(E_EomOutputPtr eom_output, tbm_surface_h srfc, void * user_data);

typedef enum
{
   NONE,
   MIRROR,
   PRESENTATION,
   WAIT_PRESENTATION,    /* It is used for delayed runnig of Presentation mode */
} E_EomOutputState;

typedef enum
{
   ROTATE_NONE,
   ROTATE_INIT,
   ROTATE_PENDING,
   ROTATE_CANCEL,
   ROTATE_DONE
} E_EomOutputRotate;

struct _E_Eom
{
   struct wl_global *global;

   tdm_display *dpy;
   tbm_bufmgr bufmgr;
   int fd;

   unsigned int output_count;
   Eina_List *outputs;
   Eina_List *clients;
   Eina_List *handlers;
   Eina_List *hooks;
   Eina_List *comp_object_intercept_hooks;

   /* Internal output data */
   int main_output_state;
   char *main_output_name;
   int width;
   int height;
   char check_first_boot;
   Ecore_Timer *timer;

   /* Rotation data */
   int angle; /* 0, 90, 180, 270 */
   E_EomOutputRotate rotate_state;
   E_EomOutput *rotate_output;
   Ecore_Timer *rotate_timer;
};

struct _E_Eom_Output
{
   unsigned int id;
   eom_output_type_e type;
   eom_output_mode_e mode;
   unsigned int width;
   unsigned int height;
   unsigned int phys_width;
   unsigned int phys_height;

   const char *name;

   tdm_output *output;
   tdm_layer *primary_layer;
   tdm_layer *overlay_layer;
   Eina_Bool need_overlay_pp;

   E_EomOutputState state;
   tdm_output_conn_status status;
   eom_output_attribute_e attribute;
   eom_output_attribute_state_e attribute_state;
   enum wl_eom_status connection;

   /* pp primary (mirror mode data) */
   E_EomOutputPpPtr pp_primary;
   Eina_Bool pp_primary_converting;
   Eina_Bool pp_primary_deinit;
   Eina_List *pending_buff;       /* can be deleted any time */
   E_EomOutputBufferPtr wait_buff; /* wait end of commit, can't be deleted */
   E_EomOutputBufferPtr show_buff; /* current showed buffer, can be deleted only after commit event with different buff */
   Eina_List *pending_pp_data;

   /* pp overlay (presentation mode subsurface data) */
   E_EomOutputPpPtr pp_overlay;
   Eina_Bool pp_overlay_converting;
   Eina_Bool pp_overlay_deinit;
   Eina_List *pending_overlay_buff;       /* can be deleted any time */
   E_EomOutputBufferPtr wait_overlay_buff; /* wait end of commit, can't be deleted */
   E_EomOutputBufferPtr show_overlay_buff; /* current showed buffer, can be deleted only after commit event with different buff */
   Eina_List *pending_pp_overlay;

   /* If attribute has been set while external output is disconnected
    * then show black screen and wait until EOM client start sending
    * buffers. After expiring of the delay start mirroring */
   Ecore_Timer *delay;
   Ecore_Timer *watchdog;
};

struct _E_Eom_Client
{
   struct wl_resource *resource;
   Eina_Bool current;

   /* EOM output the client related to */
   int output_id;
   /* E_Client the client related to */
   E_Client *ec;
};

struct _E_Eom_Output_Buffer
{
   E_EomOutputPtr eom_output;
   tbm_surface_h tbm_surface;
   E_EomEndShowingEventPtr cb_func;
   void *cb_user_data;
   Eina_Bool primary;
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
   E_EomOutputPtr eom_output;
   E_EomBufferPtr eom_buffer;
   tbm_surface_h tsurface;
};

struct _E_Eom_Comp_Object_Intercept_Hook_Data
{
   E_Client *ec;
   E_Comp_Object_Intercept_Hook *hook;
};

/*
 * EOM Output Attributes
 * +-----------------+------------+-----------------+------------+
 * |                 |   normal   | exclusive_share | exclusive  |
 * +-----------------+------------+-----------------+------------+
 * | normal          |  possible  |    possible     |  possible  |
 * +-----------------+------------+-----------------+------------+
 * | exclusive_share | impossible |    possible     |  possible  |
 * +-----------------+------------+-----------------+------------+
 * | exclusive       | impossible |   impossible    | impossible |
 * +-----------------+------------+-----------------+------------+
 *
 * possible   = 1
 * impossible = 0
 */
static int eom_output_attributes[EOM_NUM_ATTR][EOM_NUM_ATTR] =
{
   {1, 1, 1},
   {0, 1, 1},
   {0, 0, 0},
};

static const char *eom_conn_types[] =
{
   "None", "VGA", "DVI-I", "DVI-D", "DVI-A",
   "Composite", "S-Video", "LVDS", "Component", "DIN",
   "DisplayPort", "HDMI-A", "HDMI-B", "TV", "eDP", "Virtual",
   "DSI",
};

static E_EomPtr g_eom = NULL;
static Eina_Bool eom_trace_debug = 0;

static void _e_eom_cb_dequeuable(tbm_surface_queue_h queue, void *user_data);
static void _e_eom_cb_pp(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data);
static void _e_eom_presentation_pp_run(E_EomOutputPtr eom_output, tbm_surface_h src_surface, E_EomBufferPtr eom_buff, Eina_Bool primary);
static E_EomOutputPtr _e_eom_output_get_by_id(int id);

static E_EomOutputBufferPtr
_e_eom_output_buff_create(E_EomOutputPtr eom_output, tbm_surface_h tbm_surface, E_EomEndShowingEventPtr cb_func, void *cb_user_data)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_surface, NULL);
   E_EomOutputBufferPtr outbuff = E_NEW(E_EomOutputBuffer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(outbuff, NULL);

   if (eom_trace_debug)
     EOMDB("Allocate output buffer:%p", outbuff);

   outbuff->eom_output = eom_output;

   tbm_surface_internal_ref(tbm_surface);
   outbuff->tbm_surface = tbm_surface;

   outbuff->cb_func = cb_func;
   outbuff->cb_user_data = cb_user_data;

   return outbuff;
}

static void
_e_eom_output_buff_delete(E_EomOutputBufferPtr buff)
{
   if (buff)
     {
        tbm_surface_internal_unref(buff->tbm_surface);
        if (buff->cb_func)
          buff->cb_func(buff->eom_output, buff->tbm_surface, buff->cb_user_data);
        E_FREE(buff);
     }
}

static E_EomBuffer *
_e_eom_buffer_create(E_Comp_Wl_Buffer *wl_buffer)
{
   E_EomBuffer * eom_buffer = E_NEW(E_EomBuffer, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_buffer, NULL);

   eom_buffer->wl_buffer = wl_buffer;

   /* Forbid E sending 'wl_buffer_send_release' event to external clients */
   e_comp_wl_buffer_reference(&eom_buffer->comp_wl_buffer_ref, wl_buffer);

   /* double reference to avoid sigterm crash */
   e_comp_wl_buffer_reference(&eom_buffer->comp_wl_buffer_ref_2, wl_buffer);

   if (eom_trace_debug)
     EOMDB("E_EomBuffer:%p wl_buffer:%p busy:%d", eom_buffer, wl_buffer, wl_buffer->busy);

   return eom_buffer;
}

static void
_e_eom_buffer_destroy(E_EomBuffer *eom_buffer)
{
   EINA_SAFETY_ON_NULL_RETURN(eom_buffer);

   if (eom_trace_debug)
     EOMDB("wl_buffer:%p busy:%d", eom_buffer->wl_buffer, eom_buffer->wl_buffer->busy);

   eom_buffer->wl_buffer = NULL;

   e_comp_wl_buffer_reference(&eom_buffer->comp_wl_buffer_ref, NULL);

   /* double reference to avoid sigterm crash */
   e_comp_wl_buffer_reference(&eom_buffer->comp_wl_buffer_ref_2, NULL);

   E_FREE(eom_buffer);
}

static inline eom_output_mode_e
_e_eom_output_state_get_mode(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_MODE_NONE;
   return output->mode;
}

static inline void
_e_eom_output_state_set_mode(E_EomOutputPtr output, eom_output_mode_e mode)
{
   if (output == NULL)
     return;
   output->mode = mode;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute_state(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_STATE_NONE;
   return output->attribute_state;
}

static inline void
_e_eom_output_attribute_state_set(E_EomOutputPtr output, eom_output_attribute_e attribute_state)
{
   if (output == NULL)
     return;
   output->attribute_state = attribute_state;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute(E_EomOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_NONE;
   return output->attribute;
}

static inline void
_e_eom_output_state_set_force_attribute(E_EomOutputPtr output, eom_output_attribute_e attribute)
{
   if (output == NULL)
     return;
   output->attribute = attribute;
}

static inline Eina_Bool
_e_eom_output_state_set_attribute(E_EomOutputPtr output, eom_output_attribute_e attribute)
{
   if (output == NULL)
     return EINA_FALSE;

   if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE || output->attribute == EOM_OUTPUT_ATTRIBUTE_NONE)
     {
        output->attribute = attribute;
        return EINA_TRUE;
     }

   if (eom_output_attributes[output->attribute - 1][attribute - 1] == 1)
     {
        output->attribute = attribute;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static inline tdm_output_conn_status
_e_eom_output_state_get_status(E_EomOutputPtr output)
{
   if (output == NULL)
     return TDM_OUTPUT_CONN_STATUS_DISCONNECTED;
   return output->status;
}

static inline void
_e_eom_output_state_set_status(E_EomOutputPtr output, tdm_output_conn_status status)
{
   if (output == NULL)
     return;
   output->status = status;
}

static Eina_Bool
_e_eom_output_video_layer_find(E_EomOutputPtr eom_output, tbm_format format)
{
   tdm_layer *layer = NULL;
   tdm_layer *tmp_overlay_layer = NULL;
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer_capability capa;
   int i, count, format_count;
   int primary_index = 0;
   const tbm_format *formats;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output->output, EINA_FALSE);

   if (eom_output->overlay_layer)
     {
       if (eom_output->need_overlay_pp)
         return EINA_FALSE;
       else
         return EINA_TRUE;
     }

   err = tdm_output_get_layer_count(eom_output->output, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOMER("tdm_output_get_layer_count fail(%d)", err);
        return EINA_FALSE;
     }

   for (i = 0; i < count; i++)
     {
        layer = (tdm_layer *)tdm_output_get_layer(eom_output->output, i, &err);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        err = tdm_layer_get_capabilities(layer, &capa);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        if (capa & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             primary_index = i + 1;
             continue;
          }

        if (primary_index == i)
          tmp_overlay_layer = layer;

        if (capa & TDM_LAYER_CAPABILITY_VIDEO)
          {
             eom_output->overlay_layer = layer;
             break;
          }
     }

   if (eom_output->overlay_layer)
     return EINA_TRUE;

   for (i = 0; i < count; i++)
     {
        layer = (tdm_layer *)tdm_output_get_layer(eom_output->output, i, &err);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        err = tdm_layer_get_capabilities(layer, &capa);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        if ((capa & TDM_LAYER_CAPABILITY_PRIMARY) || (capa & TDM_LAYER_CAPABILITY_CURSOR))
          continue;

        if (capa & TDM_LAYER_CAPABILITY_SCALE)
          {
             format_count = 0;
             err = tdm_layer_get_available_formats(layer, &formats, &format_count);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

             for (i = 0; i < count; i++)
               {
                  if (formats[i] == format)
                    {
                       eom_output->overlay_layer = layer;
                       break;
                    }
               }
          }
     }
   if (eom_output->overlay_layer)
     return EINA_TRUE;

   eom_output->overlay_layer = tmp_overlay_layer;

   return EINA_FALSE;
}

static Eina_Bool
_e_eom_output_primary_layer_setup(E_EomOutputPtr eom_output)
{
   tdm_layer *layer = NULL;
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer_capability capa;
   tdm_info_layer layer_info;
   int i, count;

   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output->output, EINA_FALSE);

   err = tdm_output_get_layer_count(eom_output->output, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOMER("tdm_output_get_layer_count fail(%d)", err);
        return EINA_FALSE;
     }

   for (i = 0; i < count; i++)
     {
        layer = (tdm_layer *)tdm_output_get_layer(eom_output->output, i, &err);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        err = tdm_layer_get_capabilities(layer, &capa);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

        if (capa & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             if (eom_trace_debug)
               EOMDB("TDM_LAYER_CAPABILITY_PRIMARY layer found : %d", i);
             break;
          }
     }

   memset(&layer_info, 0x0, sizeof(tdm_info_layer));
   layer_info.src_config.size.h = eom_output->width;
   layer_info.src_config.size.v = eom_output->height;
   layer_info.src_config.pos.x = 0;
   layer_info.src_config.pos.y = 0;
   layer_info.src_config.pos.w = eom_output->width;
   layer_info.src_config.pos.h = eom_output->height;
   layer_info.src_config.format = TBM_FORMAT_ARGB8888;
   layer_info.dst_pos.x = 0;
   layer_info.dst_pos.y = 0;
   layer_info.dst_pos.w = eom_output->width;
   layer_info.dst_pos.h = eom_output->height;
   layer_info.transform = TDM_TRANSFORM_NORMAL;

   err = tdm_layer_set_info(layer, &layer_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);

   eom_output->primary_layer = layer;

   return EINA_TRUE;
}

static tbm_surface_h
_e_eom_util_get_output_surface(const char *name)
{
   Ecore_Drm_Output *primary_output = NULL;
   Ecore_Drm_Device *dev;
   const Eina_List *l;
   tbm_surface_h tbm = NULL;
   tdm_output *tdm_output_obj = NULL;
   tdm_layer *layer = NULL;
   tdm_layer_capability capabilities = 0;
   tdm_error err = TDM_ERROR_NONE;
   int count = 0, i = 0;

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     {
        primary_output = ecore_drm_device_output_name_find(dev, name);
        if (primary_output != NULL)
          break;
     }

   if (primary_output == NULL)
     {
        EOMER("Get primary output fail.(%s)", name);
        EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
          {
             primary_output = ecore_drm_output_primary_get(dev);
             if (primary_output != NULL)
               break;
          }

        if (primary_output == NULL)
          {
             EOMER("Get primary output fail.(%s)", name);
             return NULL;
          }
     }

   tdm_output_obj = tdm_display_get_output(g_eom->dpy, 0, &err);
   if (tdm_output_obj == NULL || err != TDM_ERROR_NONE)
     {
        EOMER("tdm_display_get_output 0 fail");
        return NULL;
     }
   err = tdm_output_get_layer_count(tdm_output_obj, &count);
   if (err != TDM_ERROR_NONE)
     {
        EOMER("tdm_output_get_layer_count fail");
        return NULL;
     }

   for (i = 0; i < count; i++)
     {
        layer = tdm_output_get_layer(tdm_output_obj, i, NULL);
        tdm_layer_get_capabilities(layer, &capabilities);
        if (capabilities & TDM_LAYER_CAPABILITY_PRIMARY)
          {
             tbm = tdm_layer_get_displaying_buffer(layer, &err);
             if (err != TDM_ERROR_NONE)
               {
                  EOMER("tdm_layer_get_displaying_buffer fail");
                  return NULL;
               }
             break;
          }
     }

   return tbm;
}

static Eina_Bool
_e_eom_pp_init(E_EomOutputPtr eom_output, Eina_Bool primary)
{
   tdm_error err = TDM_ERROR_NONE;
   E_EomOutputPpPtr eom_pp = NULL;
   tdm_pp *pp = NULL;
   tbm_surface_queue_h queue = NULL;

   if (primary)
     {
        if (eom_output->pp_primary != NULL)
          return EINA_TRUE;
     }
   else
     {
        if (eom_output->pp_overlay != NULL)
          return EINA_TRUE;
     }

   eom_pp = E_NEW(E_EomOutputPp, 1);
   if (!eom_pp)
     return EINA_FALSE;

   pp = tdm_display_create_pp(g_eom->dpy, &err);
   EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, error);
   /* TODO: Add support for other formats */
   queue = tbm_surface_queue_create(3, eom_output->width,eom_output->height,
                                    TBM_FORMAT_ARGB8888, TBM_BO_SCANOUT);
   EINA_SAFETY_ON_NULL_GOTO(queue, error);

   eom_pp->pp = pp;
   eom_pp->queue = queue;

   if (primary)
     eom_output->pp_primary = eom_pp;
   else
     eom_output->pp_overlay = eom_pp;

   return EINA_TRUE;

error:
   if (pp)
     tdm_pp_destroy(pp);

   if (eom_pp)
     E_FREE(eom_pp);

   return EINA_FALSE;
}

static void
_e_eom_pp_deinit(E_EomOutputPtr eom_output, Eina_Bool primary)
{
   E_EomOutputPpPtr eom_pp = NULL;

   if (primary)
      eom_pp = eom_output->pp_primary;
   else
      eom_pp = eom_output->pp_overlay;

   if (!eom_pp)
     return;

   if (eom_pp->queue)
     {
        if (eom_trace_debug)
          EOMDB("flush and destroy queue");
        tbm_surface_queue_flush(eom_pp->queue);
        tbm_surface_queue_destroy(eom_pp->queue);
     }

   if (eom_pp->pp)
     tdm_pp_destroy(eom_pp->pp);

   E_FREE(eom_pp);

   if (primary)
     eom_output->pp_primary = NULL;
   else
     eom_output->pp_overlay = NULL;
}

static Eina_Bool
_e_eom_pp_is_needed(int src_w, int src_h, int dst_w, int dst_h)
{
   if (src_w != dst_w)
     return EINA_TRUE;

   if (src_h != dst_h)
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_eom_util_calculate_fullsize(int src_h, int src_v, int dst_size_h, int dst_size_v,
                               int *dst_x, int *dst_y, int *dst_w, int *dst_h)
{
   double h_ratio, v_ratio;

   h_ratio = (double)src_h / (double)dst_size_h;
   v_ratio = (double)src_v / (double)dst_size_v;

   if (h_ratio == v_ratio)
     {
        *dst_x = 0;
        *dst_y = 0;
        *dst_w = dst_size_h;
        *dst_h = dst_size_v;
     }
   else if (h_ratio < v_ratio)
     {
        *dst_y = 0;
        *dst_h = dst_size_v;
        *dst_w = dst_size_v * src_h / src_v;
        *dst_x = (dst_size_h - *dst_w) / 2;
     }
   else /* (h_ratio > v_ratio) */
     {
        *dst_x = 0;
        *dst_w = dst_size_h;
        *dst_h = dst_size_h * src_h / src_v;
        *dst_y = (dst_size_v - *dst_h) / 2;
     }
}

static void
_e_eom_tbm_buffer_release_pp_primary(E_EomOutputPtr eom_output, tbm_surface_h surface, void *eom_buff)
{
   if (eom_trace_debug)
     EOMDB("release pp_primary eom_output:%p, tbm_surface_h:%p data:%p", eom_output, surface, eom_buff);

   if (!eom_output->pp_primary || !eom_output->pp_primary->queue)
     return;

   tbm_surface_queue_release(eom_output->pp_primary->queue, surface);

   if (eom_buff)
     _e_eom_buffer_destroy(eom_buff);
}

static void
_e_eom_tbm_buffer_release_pp_overlay(E_EomOutputPtr eom_output, tbm_surface_h surface, void *eom_buff)
{
   if (eom_trace_debug)
     EOMDB("release pp_overlay eom_output:%p, tbm_surface_h:%p data:%p", eom_output, surface, eom_buff);

   if (!eom_output->pp_overlay || !eom_output->pp_overlay->queue)
     return;

   tbm_surface_queue_release(eom_output->pp_overlay->queue, surface);

   _e_eom_buffer_destroy(eom_buff);
}

static void
_e_eom_cb_layer_commit(tdm_layer *layer EINA_UNUSED, unsigned int sequence EINA_UNUSED,
                       unsigned int tv_sec EINA_UNUSED, unsigned int tv_usec EINA_UNUSED,
                       void *user_data)
{
   E_EomOutputBufferPtr outbuff = NULL;
   E_EomOutputPtr eom_output = NULL;
   tdm_error err = TDM_ERROR_NONE;
   E_EomOutputBufferPtr wait_buff;
   E_EomOutputBufferPtr show_buff;
   Eina_List *pending_buff;
   Eina_List *pending_pp_list;
   E_EomPpDataPtr pending_pp = NULL;
   Eina_Bool primary = EINA_FALSE;
   tdm_layer *tlayer;
   tbm_surface_h tsurface;
   E_EomBufferPtr eom_buffer;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   outbuff = (E_EomOutputBufferPtr)user_data;

   eom_output = outbuff->eom_output;
   EINA_SAFETY_ON_NULL_RETURN(eom_output);

   primary = outbuff->primary;

   if (eom_trace_debug)
     EOMDB("========================>  CM  END     tbm_buff:%p", outbuff->tbm_surface);

   /*it means that eom_output has been canceled*/
   if (primary)
     {
        if (eom_output->wait_buff == NULL)
          {
             _e_eom_output_buff_delete(outbuff);
             return;
          }

        wait_buff = eom_output->wait_buff;
        show_buff = eom_output->show_buff;
        pending_buff = eom_output->pending_buff;
        pending_pp_list = eom_output->pending_pp_data;
        tlayer = eom_output->primary_layer;
     }
   else
     {
        if (eom_output->wait_overlay_buff == NULL)
          {
             _e_eom_output_buff_delete(outbuff);
             return;
          }

        wait_buff = eom_output->wait_overlay_buff;
        show_buff = eom_output->show_overlay_buff;
        pending_buff = eom_output->pending_overlay_buff;
        pending_pp_list = eom_output->pending_pp_overlay;
        tlayer = eom_output->overlay_layer;
     }

   EINA_SAFETY_ON_FALSE_RETURN(wait_buff == outbuff);

   if (eom_trace_debug)
     EOMDB("commit finish tbm_surface_h:%p", outbuff->tbm_surface);

   /* check if show buffer is present */
   if (show_buff != NULL)
     {
        if (eom_trace_debug)
          EOMDB("delete show buffer tbm_surface_h:%p", show_buff->tbm_surface);
        _e_eom_output_buff_delete(show_buff);
        if (primary)
          eom_output->show_buff = NULL;
        else
          eom_output->show_overlay_buff = NULL;
     }

   /* set wait_buffer as show_buff */
   if (eom_trace_debug)
     EOMDB("set wait_buffer as show_buff tbm_surface_h:%p", outbuff->tbm_surface);

   if (primary)
     {
        eom_output->wait_buff = NULL;
        eom_output->show_buff = outbuff;
     }
   else
     {
        eom_output->wait_overlay_buff = NULL;
        eom_output->show_overlay_buff = outbuff;
     }

   /* check if pending buffer is present */
   outbuff = eina_list_nth(pending_buff, 0);
   if (outbuff != NULL)
     {
        if (primary)
          eom_output->pending_buff = eina_list_remove(eom_output->pending_buff, outbuff);
        else
          eom_output->pending_overlay_buff = eina_list_remove(eom_output->pending_overlay_buff, outbuff);

        if (eom_trace_debug)
          {
             EOMDB("========================>  CM- START   tbm_buff:%p", outbuff->tbm_surface);
             EOMDB("do commit tdm_output:%p tdm_layer:%p tbm_surface_h:%p",
                   eom_output->output, tlayer, outbuff->tbm_surface);
          }
        err = tdm_layer_set_buffer(tlayer, outbuff->tbm_surface);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, error);

        err = tdm_layer_commit(tlayer, _e_eom_cb_layer_commit, outbuff);
        EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, error2);

        eom_output->wait_buff = outbuff;
     }

   pending_pp = eina_list_nth(pending_pp_list, 0);
   if (pending_pp != NULL)
     {
        if (primary)
          {
             if (!tbm_surface_queue_can_dequeue(eom_output->pp_primary->queue, 0))
               return;
          }
        else
          {
             if (!tbm_surface_queue_can_dequeue(eom_output->pp_overlay->queue, 0))
               return;
          }
        pending_pp_list = eina_list_remove(pending_pp_list, pending_pp);

        tsurface = pending_pp->tsurface;
        eom_buffer = pending_pp->eom_buffer;

        E_FREE(pending_pp);

        _e_eom_presentation_pp_run(eom_output, tsurface, eom_buffer, primary);
     }

   return;

error2:
   tdm_layer_unset_buffer(tlayer);

error:
   EOMDB("========================>  CM- ENDERR  tbm_buff:%p", outbuff->tbm_surface);

   _e_eom_output_buff_delete(outbuff);
}

static Eina_Bool
_e_eom_output_show(E_EomOutputPtr eom_output, tbm_surface_h tbm_srfc,
                   E_EomEndShowingEventPtr cb_func, void *cb_user_data, Eina_Bool primary)
{
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer *layer;

   /* create new output buffer */
   E_EomOutputBufferPtr outbuff = _e_eom_output_buff_create(eom_output, tbm_srfc, cb_func, cb_user_data);
   EINA_SAFETY_ON_NULL_RETURN_VAL(outbuff, EINA_FALSE);

   if (primary)
     {
        outbuff->primary = EINA_TRUE;

        /* check if output free to commit */
        if (eom_output->wait_buff != NULL)
          {
             eom_output->pending_buff = eina_list_append(eom_output->pending_buff , outbuff);

             if (eom_trace_debug)
               EOMDB("add to pending list tdm_output:%p tdm_layer:%p tbm_surface_h:%p",
                     eom_output->output, eom_output->primary_layer, outbuff->tbm_surface);

             return EINA_TRUE;
          }
        else
          {
             layer = eom_output->primary_layer;
          }
     }
   else
     {
        /* check if output free to commit */
        if (eom_output->wait_overlay_buff != NULL)
          {
             eom_output->pending_overlay_buff = eina_list_append(eom_output->pending_overlay_buff , outbuff);

             if (eom_trace_debug)
               EOMDB("add to pending list tdm_output:%p tdm_layer:%p tbm_surface_h:%p",
                     eom_output->output, eom_output->overlay_layer, outbuff->tbm_surface);

             return EINA_TRUE;
          }
        else
          {
             layer = eom_output->overlay_layer;
          }
     }

   if (eom_trace_debug)
     {
        EOMDB("========================>  CM  START   tbm_buff:%p", tbm_srfc);
        EOMDB("do commit tdm_output:%p tdm_layer:%p tbm_surface_h:%p",
              eom_output->output, layer, outbuff->tbm_surface);
     }
     err = tdm_layer_set_buffer(layer, outbuff->tbm_surface);
     EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, error);

     err = tdm_layer_commit(layer, _e_eom_cb_layer_commit, outbuff);
     EINA_SAFETY_ON_FALSE_GOTO(err == TDM_ERROR_NONE, error2);

     if (primary)
       eom_output->wait_buff = outbuff;
     else
       eom_output->wait_overlay_buff = outbuff;

   return EINA_TRUE;

error2:
   tdm_layer_unset_buffer(layer);

error:
   if (outbuff)
     _e_eom_output_buff_delete(outbuff);

   if (eom_trace_debug)
     EOMDB("========================>  CM  ENDERR  tbm_buff:%p", tbm_srfc);

   return EINA_FALSE;
}

static unsigned int
_e_eom_aligned_width_get(tbm_surface_info_s *surf_info)
{
   unsigned int aligned_width = 0;

   switch (surf_info->format)
     {
      case TBM_FORMAT_YUV420:
      case TBM_FORMAT_YVU420:
      case TBM_FORMAT_YUV422:
      case TBM_FORMAT_YVU422:
      case TBM_FORMAT_NV12:
      case TBM_FORMAT_NV21:
        aligned_width = surf_info->planes[0].stride;
        break;
      case TBM_FORMAT_YUYV:
      case TBM_FORMAT_UYVY:
        aligned_width = surf_info->planes[0].stride >> 1;
        break;
      case TBM_FORMAT_ARGB8888:
      case TBM_FORMAT_XRGB8888:
        aligned_width = surf_info->planes[0].stride >> 2;
        break;
      default:
        EOMER("not supported format: %x", surf_info->format);
     }

   return aligned_width;
}

static Eina_Bool
_e_eom_pp_info_set(E_EomOutputPtr eom_output, tbm_surface_h src, tbm_surface_h dst, Eina_Bool primary, Eina_Bool mirror)
{
   tdm_error err = TDM_ERROR_NONE;
   tdm_info_pp pp_info;
   int x = 0, y = 0, w = 0, h = 0;
   int width = 0, height = 0;
   tbm_surface_info_s src_info;
   tbm_surface_info_s dst_info;
   unsigned int src_aligned_w;
   unsigned int dst_aligned_w;

   memset(&pp_info, 0, sizeof(tdm_info_pp));

   tbm_surface_get_info(src, &src_info);
   tbm_surface_get_info(dst, &dst_info);
   src_aligned_w = _e_eom_aligned_width_get(&src_info);
   dst_aligned_w = _e_eom_aligned_width_get(&dst_info);

   if (mirror)
     {
        if (g_eom->angle == 90 || g_eom->angle == 270)
         {
            width = g_eom->height;
            height = g_eom->width;
         }
       else
         {
            width = g_eom->width;
            height = g_eom->height;
         }
     }
   else
     {
        width = src_aligned_w;
        height = src_info.height;
     }

   _e_eom_util_calculate_fullsize(width, height, eom_output->width, eom_output->height,
                                  &x, &y, &w, &h);

   if (eom_trace_debug)
     {
        if (mirror)
          {
             EOMDB("PP mirror: angle:%d", g_eom->angle);
             EOMDB("PP mirror: src:%dx%d, dst:%dx%d", g_eom->width, g_eom->height, eom_output->width, eom_output->height);
             EOMDB("PP calculation: x:%d, y:%d, w:%d, h:%d", x, y, w, h);
          }
        else
          {
             EOMDB("PP prentation: src:%dx%d, dst:%dx%d", src_info.width, src_info.height, dst_info.width, dst_info.height);
             EOMDB("PP prentation calculation: x:%d, y:%d, w:%d, h:%d", x, y, w, h);
          }
     }

   pp_info.src_config.size.h = src_aligned_w;
   pp_info.src_config.size.v = src_info.height;
   pp_info.src_config.pos.x = 0;
   pp_info.src_config.pos.y = 0;
   pp_info.src_config.pos.w = src_info.width;
   pp_info.src_config.pos.h = src_info.height;
   pp_info.src_config.format = src_info.format;

   pp_info.dst_config.size.h = dst_aligned_w;
   pp_info.dst_config.size.v = dst_info.height;
   pp_info.dst_config.pos.x = x;
   pp_info.dst_config.pos.y = y;
   pp_info.dst_config.pos.w = w;
   pp_info.dst_config.pos.h = h;
   pp_info.dst_config.format = dst_info.format;

   if (mirror)
     {
        switch (g_eom->angle)
          {
           case 0:
              pp_info.transform = TDM_TRANSFORM_NORMAL;
              break;
           case 90:
              pp_info.transform = TDM_TRANSFORM_90;
              break;
           case 180:
              pp_info.transform = TDM_TRANSFORM_180;
              break;
           case 270:
              pp_info.transform = TDM_TRANSFORM_270;
              break;
           default:
              EOMIN("Never get here");
              break;
          }
     }
   else
     pp_info.transform = TDM_TRANSFORM_NORMAL;

   pp_info.sync = 0;
   pp_info.flags = 0;

   if (primary)
     {
        if (memcmp(&eom_output->pp_primary->pp_info, &pp_info, sizeof(tdm_info_layer)))
          {
             err = tdm_pp_set_info(eom_output->pp_primary->pp, &pp_info);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
             memcpy(&eom_output->pp_primary->pp_info, &pp_info, sizeof(tdm_info_layer));
          }
     }
   else
     {
        if (memcmp(&eom_output->pp_overlay->pp_info, &pp_info, sizeof(tdm_info_layer)))
          {
             err = tdm_pp_set_info(eom_output->pp_overlay->pp, &pp_info);
             EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
             memcpy(&eom_output->pp_overlay->pp_info, &pp_info, sizeof(tdm_info_layer));
          }
     }

   return EINA_TRUE;
}

void _e_eom_clear_surfaces(E_EomOutputPtr eom_output, tbm_surface_queue_h queue)
{
   int num, pitch;
   int i = 0;
   tbm_bo bo;
   tbm_bo_handle hndl;
   tbm_surface_h surface[3];
   tbm_surface_queue_error_e err = TBM_SURFACE_QUEUE_ERROR_NONE;

   err = tbm_surface_queue_get_surfaces(queue, surface, &num);
   if (err != TBM_SURFACE_QUEUE_ERROR_NONE)
     {
        EOMER("surface get fail");
        return;
     }

   for (i = 0; i < num; i++)
     {
        /* XXX: should be cleared all the bos of the surface? */
        bo = tbm_surface_internal_get_bo(surface[i], 0);
        if (!bo)
          {
             EOMER("bo get fail");
             return;
          }

        hndl = tbm_bo_map(bo, TBM_DEVICE_CPU, TBM_OPTION_READ | TBM_OPTION_WRITE);
        if (!hndl.ptr)
          {
             EOMER("handle get fail");
             return;
          }

        /* TODO: take correct picth */
        pitch = eom_output->width * 4;
        memset(hndl.ptr, 0x00, pitch*eom_output->height);

        tbm_bo_unmap(bo);
     }
}

#ifdef SUPPORT_ROTATE
static E_Client *
_e_eom_top_visible_ec_get()
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
#endif

static Eina_Bool
_e_eom_pp_rotate_check()
{
#ifdef SUPPORT_ROTATE
   E_Client *ec;

   ec = _e_eom_top_visible_ec_get();
   if (ec == NULL)
     return EINA_FALSE;

   if (g_eom->angle != ec->e.state.rot.ang.curr)
     {
        g_eom->angle = ec->e.state.rot.ang.curr;
        if (eom_trace_debug)
          EOMDB("rotate check: rotate angle:%d", g_eom->angle);

        return EINA_TRUE;
     }
#endif
   return EINA_FALSE;
}

static void
_e_eom_pp_run(E_EomOutputPtr eom_output, Eina_Bool first_run)
{
   tdm_error tdm_err = TDM_ERROR_NONE;
   tbm_surface_h dst_surface = NULL;
   tbm_surface_h src_surface = NULL;
   E_EomOutputPpPtr pp_primary = NULL;
   Eina_Bool ret = EINA_FALSE;

   if (g_eom->main_output_state == 0)
     return;

   /* If a client has committed its buffer stop mirror mode */
   if (eom_output->state != MIRROR)
     {
        g_eom->rotate_state = ROTATE_NONE;
        g_eom->rotate_output = NULL;
        return;
     }

   if (eom_output->pp_primary_deinit)
     return;

   if (!eom_output->pp_primary)
     return;

   pp_primary = eom_output->pp_primary;

   if (!pp_primary->pp || !pp_primary->queue)
     return;

   if (g_eom->rotate_state == ROTATE_INIT || g_eom->rotate_state == ROTATE_PENDING)
     {
        g_eom->rotate_output = eom_output;
        if (!first_run)
          return;
     }

   if (g_eom->rotate_state == ROTATE_CANCEL)
     g_eom->rotate_state = ROTATE_NONE;

   if (tbm_surface_queue_can_dequeue(pp_primary->queue, 0) )
     {
        tdm_err = tbm_surface_queue_dequeue(pp_primary->queue, &dst_surface);
        EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

        if (eom_trace_debug)
          EOMDB("============================>  PP  START   tbm_buff:%p", dst_surface);

        src_surface = _e_eom_util_get_output_surface(g_eom->main_output_name);
        tdm_err = TDM_ERROR_OPERATION_FAILED;
        EINA_SAFETY_ON_NULL_GOTO(src_surface, error);

        /* Is set to TRUE if device has been recently rotated */
        if (g_eom->rotate_state == ROTATE_DONE || first_run || _e_eom_pp_rotate_check())
          {
             g_eom->rotate_state = ROTATE_NONE;
             g_eom->rotate_output = NULL;

             /* TODO: it has to be implemented in better way */
             _e_eom_clear_surfaces(eom_output, pp_primary->queue);

             ret = _e_eom_pp_info_set(eom_output, src_surface, dst_surface, EINA_TRUE, EINA_TRUE);
             EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, error);
          }

        tdm_err = tdm_pp_set_done_handler(pp_primary->pp, _e_eom_cb_pp, eom_output);
        EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

        tbm_surface_internal_ref(dst_surface);
        tbm_surface_internal_ref(src_surface);

        tdm_err = tdm_pp_attach(pp_primary->pp, src_surface, dst_surface);
        EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

        eom_output->pp_primary_converting = EINA_TRUE;

        tdm_err = tdm_pp_commit(pp_primary->pp);
        EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, commit_fail);

        if (eom_trace_debug)
          EOMDB("do pp commit tdm_output:%p tbm_surface_h(src:%p dst:%p)", eom_output->output, src_surface, dst_surface);
     }
   else
     {
        if (eom_trace_debug)
          EOMDB("all pp buffers are busy, wait release queue");
        tbm_surface_queue_add_dequeuable_cb(pp_primary->queue, _e_eom_cb_dequeuable, eom_output);
     }

   return;

commit_fail:
attach_fail:
   tbm_surface_internal_unref(dst_surface);
   tbm_surface_internal_unref(src_surface);
error:
   EOMER("failed run pp tdm error: %d", tdm_err);

   if (dst_surface)
     {
        if (eom_trace_debug)
          EOMDB("============================>  PP  ENDERR  tbm_buff:%p", dst_surface);
        tbm_surface_queue_release(pp_primary->queue, dst_surface);
     }
}

static void
_e_eom_cb_pp(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomOutputPpPtr pp_primary = NULL;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   eom_output = (E_EomOutputPtr)user_data;

   eom_output->pp_primary_converting = EINA_FALSE;

   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);

   if (!eom_output->pp_primary)
     return;

   pp_primary = eom_output->pp_primary;

   if (eom_trace_debug)
     EOMDB("==============================>  PP  END     tbm_buff:%p", tsurface_dst);

   if (eom_output->pp_primary_deinit)
     {
        _e_eom_pp_deinit(eom_output, EINA_TRUE);
        eom_output->pp_primary_deinit = EINA_FALSE;
        return;
     }

   if (pp_primary->queue == NULL)
     return;

   if (g_eom->main_output_state == 0)
     {
        tbm_surface_queue_release(pp_primary->queue, tsurface_dst);
        return;
     }

   /* If a client has committed its buffer stop mirror mode */
   if (eom_output->state != MIRROR)
     {
        tbm_surface_queue_release(pp_primary->queue, tsurface_dst);
        return;
     }

#ifdef EOM_DUMP_MIRROR_BUFFERS
   char file[256];
   static int i;
   snprintf(file, sizeof file, "%s_%d", "eom_mirror", i++);
   tbm_surface_internal_dump_buffer(surface, file, i++, 0);
#endif

   if (!_e_eom_output_show(eom_output, tsurface_dst, _e_eom_tbm_buffer_release_pp_primary, NULL, EINA_TRUE))
     {
        EOMER("_e_eom_add_buff_to_show fail");
        tbm_surface_queue_release(pp_primary->queue, tsurface_dst);
     }

   _e_eom_pp_run(eom_output, EINA_FALSE);

   if (eom_trace_debug)
     EOMDB("==============================<  PP");
}

static void
_e_eom_layer_overlay_set(E_EomOutputPtr eom_output, tbm_surface_h tsurface)
{
   tdm_info_layer layer_info, old_info;
   tdm_error err = TDM_ERROR_NONE;
   tbm_surface_info_s dst_info;
   unsigned int width;

   CLEAR(old_info);
   err = tdm_layer_get_info(eom_output->overlay_layer, &old_info);
   EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);

   tbm_surface_get_info(tsurface, &dst_info);
   width = _e_eom_aligned_width_get(&dst_info);

   memset(&layer_info, 0x0, sizeof(tdm_info_layer));
   layer_info.src_config.size.h = width;
   layer_info.src_config.size.v = dst_info.height;
   layer_info.src_config.pos.x = 0;
   layer_info.src_config.pos.y = 0;
   layer_info.src_config.pos.w = dst_info.width;
   layer_info.src_config.pos.h = dst_info.height;
   layer_info.src_config.format = dst_info.format;
   layer_info.dst_pos.x = 0;
   layer_info.dst_pos.y = 0;
   layer_info.dst_pos.w = eom_output->width;
   layer_info.dst_pos.h = eom_output->height;
   layer_info.transform = TDM_TRANSFORM_NORMAL;

   if (memcmp(&old_info, &layer_info, sizeof(tdm_info_layer)))
     {
        err = tdm_layer_set_info(eom_output->overlay_layer, &layer_info);
        EINA_SAFETY_ON_FALSE_RETURN(err == TDM_ERROR_NONE);
     }
}

static void
_e_eom_cb_pp_presentation(E_EomOutputPtr eom_output, E_EomPpDataPtr ppdata, E_EomOutputPpPtr eom_pp, Eina_Bool primary)
{
   E_EomEndShowingEventPtr cb_func;
   E_EomBufferPtr eom_buff;
   tbm_surface_h tsurface;

   eom_buff = ppdata->eom_buffer;
   tsurface = ppdata->tsurface;

   E_FREE(ppdata);

   if (g_eom->main_output_state == 0)
     {
        tbm_surface_queue_release(eom_pp->queue, tsurface);
        return;
     }

   if (eom_output->state == MIRROR)
     {
        tbm_surface_queue_release(eom_pp->queue, tsurface);
        return;
     }

#ifdef EOM_DUMP_PRESENTATION_BUFFERS
   char file[256];
   static int i;
   snprintf(file, sizeof file, "%s_%d", "eom_external", i++);
   tbm_surface_internal_dump_buffer(tsurface, file, i++, 0);
#endif

   if (primary)
     {
        cb_func = _e_eom_tbm_buffer_release_pp_primary;
     }
   else
     {
        _e_eom_layer_overlay_set(eom_output, tsurface);

        cb_func = _e_eom_tbm_buffer_release_pp_overlay;
     }

   if (!_e_eom_output_show(eom_output, tsurface, cb_func, eom_buff, primary))
     {
        EOMER("pp show fail");
        if (primary)
          tbm_surface_queue_release(eom_output->pp_primary->queue, tsurface);
        else
          tbm_surface_queue_release(eom_output->pp_overlay->queue, tsurface);
     }

   if (eom_trace_debug)
     EOMDB("==============================<  presentation PP");
}


static void
_e_eom_cb_pp_presentation_primary(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomPpDataPtr ppdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   ppdata = (E_EomPpDataPtr)user_data;
   eom_output = ppdata->eom_output;

   eom_output->pp_primary_converting = EINA_FALSE;

   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);

   if (eom_output->pp_primary_deinit)
     {
        eom_output->pp_primary_deinit = EINA_FALSE;
        _e_eom_pp_deinit(eom_output, EINA_FALSE);
     }

   if (eom_trace_debug)
     EOMDB("==============================>  presentation PP  END  primary   tbm_buff:%p", tsurface_dst);

   if (eom_output->pp_primary == NULL)
     {
        E_FREE(ppdata);
        return;
     }

   ppdata->tsurface = tsurface_dst;

   _e_eom_cb_pp_presentation(eom_output, ppdata, eom_output->pp_primary, EINA_TRUE);
}

static void
_e_eom_cb_pp_presentation_overlay(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomPpDataPtr ppdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   ppdata = (E_EomPpDataPtr)user_data;
   eom_output = ppdata->eom_output;

   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);

   eom_output->pp_overlay_converting = EINA_FALSE;

   if (eom_output->pp_overlay_deinit)
     {
        eom_output->pp_overlay_deinit = EINA_FALSE;
        _e_eom_pp_deinit(eom_output, EINA_FALSE);
     }

   if (eom_trace_debug)
     EOMDB("==============================>  presentation PP  END  overlay   tbm_buff:%p", tsurface_dst);

   if (eom_output->pp_overlay == NULL)
     {
        E_FREE(ppdata);
        return;
     }

   ppdata->tsurface = tsurface_dst;

   _e_eom_cb_pp_presentation(eom_output, ppdata, eom_output->pp_overlay, EINA_FALSE);
}

static void
_e_eom_presentation_pp_run(E_EomOutputPtr eom_output, tbm_surface_h src_surface, E_EomBufferPtr eom_buff, Eina_Bool primary)
{
   tdm_error tdm_err = TDM_ERROR_NONE;
   tbm_surface_h dst_surface = NULL;
   Eina_Bool ret = EINA_FALSE;
   E_EomClientPtr eom_client_itr = NULL;
   Eina_List *l;
   E_EomOutputPpPtr eom_pp = NULL;
   E_EomPpDataPtr ppdata = NULL;

   if (g_eom->main_output_state == 0)
     return;

   if (eom_output->state == WAIT_PRESENTATION)
     {
        EOMDB("remove delayed presentation timer");
        if (eom_output->delay)
          ecore_timer_del(eom_output->delay);
     }

   if (eom_output->state != PRESENTATION)
     {
        _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_PRESENTATION);

        EINA_LIST_FOREACH(g_eom->clients, l, eom_client_itr)
          {
             if (eom_client_itr->output_id == eom_output->id)
               wl_eom_send_output_mode(eom_client_itr->resource, eom_output->id,
                                       _e_eom_output_state_get_mode(eom_output));
          }

        eom_output->state = PRESENTATION;
     }

   if (primary)
     eom_pp = eom_output->pp_primary;
   else
     eom_pp = eom_output->pp_overlay;

   if (!eom_pp || !eom_pp->pp || !eom_pp->queue)
     {
        EOMER("no pp data");
        return;
     }

   ppdata = E_NEW(E_EomPpData, 1);
   if (!ppdata)
     {
        EOMER("make pp data fail");
        _e_eom_buffer_destroy(eom_buff);
        return;
     }
   ppdata->eom_output = eom_output;
   ppdata->eom_buffer = eom_buff;
   ppdata->tsurface = src_surface;

   if (!tbm_surface_queue_can_dequeue(eom_pp->queue, 0))
     {
        if (eom_trace_debug)
          EOMDB("all pp buffers are busy, wait release queue(%p)", eom_pp->queue);

        if (primary)
          eom_output->pending_pp_data = eina_list_append(eom_output->pending_pp_data, ppdata);
        else
          eom_output->pending_pp_overlay = eina_list_append(eom_output->pending_pp_overlay, ppdata);

        return;
     }

   tdm_err = tbm_surface_queue_dequeue(eom_pp->queue, &dst_surface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

   if (eom_trace_debug)
     EOMDB("============================>  presentation PP  START   tbm_buff:%p", dst_surface);

   ret = _e_eom_pp_info_set(eom_output, src_surface, dst_surface, primary, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, error);

   if (primary)
     tdm_err = tdm_pp_set_done_handler(eom_pp->pp, _e_eom_cb_pp_presentation_primary, ppdata);
   else
     tdm_err = tdm_pp_set_done_handler(eom_pp->pp, _e_eom_cb_pp_presentation_overlay, ppdata);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

   tbm_surface_internal_ref(src_surface);
   tbm_surface_internal_ref(dst_surface);

   tdm_err = tdm_pp_attach(eom_pp->pp, src_surface, dst_surface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

   if (primary)
     eom_output->pp_primary_converting = EINA_TRUE;
   else
     eom_output->pp_overlay_converting = EINA_TRUE;

   tdm_err = tdm_pp_commit(eom_pp->pp);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, commit_fail);

   if (eom_trace_debug)
     {
        EOMDB("do presentation pp commit tdm_output:%p tbm_surface_h(src:%p dst:%p)",
              eom_output->output, src_surface, dst_surface);
        EOMER("============================<  presentation PP  done   tbm_buff:%p", dst_surface);
     }

   return;

commit_fail:
attach_fail:
   tbm_surface_internal_unref(dst_surface);
   tbm_surface_internal_unref(src_surface);

error:
   EOMER("failed run pp tdm error: %d", tdm_err);

   if (dst_surface)
     {
        if (eom_trace_debug)
          EOMDB("============================>  presentation PP  ENDERR  tbm_buff:%p", dst_surface);
        tbm_surface_queue_release(eom_pp->queue, dst_surface);
     }
}

static void
_e_eom_cb_dequeuable(tbm_surface_queue_h queue, void *user_data)
{
   E_EomOutputPtr eom_output = (E_EomOutputPtr)user_data;
   EINA_SAFETY_ON_NULL_RETURN(user_data);

   if (eom_trace_debug)
     EOMDB("release before in queue");

   tbm_surface_queue_remove_dequeuable_cb(eom_output->pp_primary->queue, _e_eom_cb_dequeuable, eom_output);

   _e_eom_pp_run(eom_output, EINA_FALSE);
}

static E_EomClientPtr
_e_eom_client_get_current_by_id(int id)
{
   Eina_List *l;
   E_EomClientPtr client;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        if (client &&
            client->current == EINA_TRUE &&
            client->output_id == id)
          return client;
     }

   return NULL;
}

static Eina_Bool
_e_eom_output_connected_setup(E_EomOutputPtr eom_output)
{
   tdm_error tdm_err = TDM_ERROR_NONE;
   tdm_info_layer layer_info;

   if (!_e_eom_output_primary_layer_setup(eom_output))
     {
        EOMER("layer setup fail");
        return EINA_FALSE;
     }

   tdm_err = tdm_layer_get_info(eom_output->primary_layer, &layer_info);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tdm_err == TDM_ERROR_NONE, EINA_FALSE);

   if (eom_trace_debug)
     EOMDB("layer info: %dx%d, pos (x:%d, y:%d, w:%d, h:%d,  dpos (x:%d, y:%d, w:%d, h:%d))",
           layer_info.src_config.size.h,  layer_info.src_config.size.v,
           layer_info.src_config.pos.x, layer_info.src_config.pos.y,
           layer_info.src_config.pos.w, layer_info.src_config.pos.h,
           layer_info.dst_pos.x, layer_info.dst_pos.y,
           layer_info.dst_pos.w, layer_info.dst_pos.h);

   tdm_err = tdm_output_set_dpms(eom_output->output, TDM_OUTPUT_DPMS_ON);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(tdm_err == TDM_ERROR_NONE, EINA_FALSE);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_output_start_mirror(E_EomOutputPtr eom_output)
{
   Eina_Bool ret = EINA_FALSE;

   if (eom_output->state == MIRROR)
     return EINA_TRUE;

   ret = _e_eom_output_connected_setup(eom_output);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == EINA_TRUE, EINA_FALSE);

   if (!_e_eom_pp_is_needed(g_eom->width, g_eom->height, eom_output->width, eom_output->height))
     {
        /* TODO: Internal and external outputs are equal */
        if (eom_trace_debug)
          EOMDB("internal and external outputs are equal");
     }

   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_MIRROR);
   eom_output->state = MIRROR;

   ret = _e_eom_pp_init(eom_output, EINA_TRUE);
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, err);

   _e_eom_pp_run(eom_output, EINA_TRUE);

   return EINA_TRUE;

err:
   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);
   eom_output->state = NONE;

   return EINA_FALSE;
}

static void
_e_eom_output_start_presentation(E_EomOutputPtr eom_output)
{
   Eina_Bool ret = EINA_FALSE;

   ret = _e_eom_output_connected_setup(eom_output);
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, err);

   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_PRESENTATION);

   return;

err:
   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);
   eom_output->state = NONE;

   return;
}

static void
_e_eom_output_overlay_buff_release(E_EomOutputPtr eom_output)
{
   Eina_List *l, *ll;
   E_EomOutputBufferPtr buff = NULL;
   E_EomPpDataPtr ppdata = NULL;

   EINA_LIST_FOREACH_SAFE(eom_output->pending_overlay_buff, l, ll, buff)
     {
        if (eom_trace_debug)
          EOMDB("delete pending overlay tbm_buff:%p", buff->tbm_surface);
        eom_output->pending_overlay_buff = eina_list_remove_list(eom_output->pending_overlay_buff, l);
        _e_eom_output_buff_delete(buff);
     }
   eom_output->wait_overlay_buff = NULL;

   if (eom_trace_debug)
     EOMDB("delete show overlay tbm_buff:%p", eom_output->show_overlay_buff ? eom_output->show_overlay_buff->tbm_surface : NULL);
   _e_eom_output_buff_delete(eom_output->show_overlay_buff);
   eom_output->show_overlay_buff = NULL;

   EINA_LIST_FOREACH_SAFE(eom_output->pending_pp_overlay, l, ll, ppdata)
     {
        if (eom_trace_debug)
          EOMDB("delete pending overlay pp data:%p", ppdata);
        eom_output->pending_pp_overlay = eina_list_remove_list(eom_output->pending_pp_overlay, l);
        _e_eom_buffer_destroy(ppdata->eom_buffer);
        E_FREE(ppdata);
     }
   eom_output->pending_pp_overlay = NULL;
}

static void
_e_eom_output_all_buff_release(E_EomOutputPtr eom_output)
{
   Eina_List *l, *ll;
   E_EomOutputBufferPtr  buff = NULL;
   E_EomPpDataPtr ppdata = NULL;

   EINA_LIST_FOREACH_SAFE(eom_output->pending_buff, l, ll, buff)
     {
        if (eom_trace_debug)
          EOMDB("delete pending tbm_buff:%p", buff->tbm_surface);
        eom_output->pending_buff = eina_list_remove_list(eom_output->pending_buff, l);
        _e_eom_output_buff_delete(buff);
     }

   eom_output->wait_buff = NULL;

   if (eom_trace_debug)
     EOMDB("delete show tbm_buff:%p", eom_output->show_buff ? eom_output->show_buff->tbm_surface : NULL);
   _e_eom_output_buff_delete(eom_output->show_buff);
   eom_output->show_buff = NULL;

   EINA_LIST_FOREACH_SAFE(eom_output->pending_pp_data, l, ll, ppdata)
     {
        if (eom_trace_debug)
          EOMDB("delete pending pp data:%p", ppdata);
        eom_output->pending_pp_data = eina_list_remove_list(eom_output->pending_pp_data, l);
        _e_eom_buffer_destroy(ppdata->eom_buffer);
        E_FREE(ppdata);
     }
   eom_output->pending_pp_data = NULL;
}

static void
_e_eom_output_deinit(E_EomOutputPtr eom_output)
{
   tdm_error err = TDM_ERROR_NONE;

   if (eom_output->state == NONE)
     return;

   _e_eom_output_state_set_status(eom_output, TDM_OUTPUT_CONN_STATUS_DISCONNECTED);
   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);

   if (_e_eom_client_get_current_by_id(eom_output->id))
     eom_output->state = WAIT_PRESENTATION;
   else
     eom_output->state = NONE;

   if (eom_output->overlay_layer)
     {
        err = tdm_layer_unset_buffer(eom_output->overlay_layer);
        if (err != TDM_ERROR_NONE)
          EOMER("fail unset buffer:%d", err);

        err = tdm_layer_commit(eom_output->overlay_layer, NULL, eom_output);
        if (err != TDM_ERROR_NONE)
          EOMER("fail commit on deleting output err:%d", err);
     }
   _e_eom_output_overlay_buff_release(eom_output);

   if (eom_output->primary_layer)
     {
        err = tdm_layer_unset_buffer(eom_output->primary_layer);
        if (err != TDM_ERROR_NONE)
          EOMER("fail unset buffer:%d", err);

        err = tdm_layer_commit(eom_output->primary_layer, NULL, eom_output);
        if (err != TDM_ERROR_NONE)
          EOMER("fail commit on deleting output err:%d", err);
     }
   _e_eom_output_all_buff_release(eom_output);

   if (!eom_output->pp_primary_converting)
     _e_eom_pp_deinit(eom_output, EINA_TRUE);
   else
     eom_output->pp_primary_deinit = EINA_TRUE;

   if (!eom_output->pp_overlay_converting)
     _e_eom_pp_deinit(eom_output, EINA_FALSE);
   else
     eom_output->pp_overlay_deinit = EINA_TRUE;

   err = tdm_output_set_dpms(eom_output->output, TDM_OUTPUT_DPMS_OFF);
   if (err != TDM_ERROR_NONE)
     EOMER("set DPMS off:%d", err);
}

static const tdm_output_mode *
_e_eom_output_get_best_mode(tdm_output *output)
{
   tdm_error ret = TDM_ERROR_NONE;
   const tdm_output_mode *modes;
   const tdm_output_mode *mode = NULL;
   const tdm_output_mode *preferred_mode = NULL;
   const tdm_output_mode *best_mode = NULL;
   unsigned int best_value = 0;
   unsigned int best_refresh = 0;
   unsigned int value;
   int i, count = 0;

   ret = tdm_output_get_available_modes(output, &modes, &count);
   if (ret != TDM_ERROR_NONE)
     {
        EOMER("tdm_output_get_available_modes fail(%d)", ret);
        return NULL;
     }

   for (i = 0; i < count; i++)
     {
        if (modes[i].type & TDM_OUTPUT_MODE_TYPE_PREFERRED)
          preferred_mode = &modes[i];

        value = modes[i].vdisplay + modes[i].hdisplay;
        if (value > best_value)
          {
             best_value = value;
             best_refresh = modes[i].vrefresh;
             best_mode = &modes[i];
          }
        else if (value == best_value)
          {
             if (modes[i].vrefresh > best_refresh)
               {
                  best_value = value;
                  best_refresh = modes[i].vrefresh;
                  best_mode = &modes[i];
               }
          }
     }

   if (preferred_mode)
     mode = preferred_mode;
   else if (best_mode)
     mode = best_mode;

   if (eom_trace_debug)
     {
        if (mode)
          EOMDB("bestmode : %s, (%dx%d) r(%d), f(%d), t(%d)",
                mode->name, mode->hdisplay, mode->vdisplay,
                mode->vrefresh, mode->flags, mode->type);
     }

   return mode;
}

static Eina_Bool
_e_eom_timer_delayed_presentation_mode(void *data)
{
   E_EomOutputPtr eom_output = NULL;

   if (eom_trace_debug)
     EOMDB("timer called %s", __FUNCTION__);

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, ECORE_CALLBACK_CANCEL);

   eom_output = (E_EomOutputPtr )data;
   eom_output->delay = NULL;

   _e_eom_output_start_mirror(eom_output);

   return ECORE_CALLBACK_CANCEL;
}

static int
_e_eom_output_connected(E_EomOutputPtr eom_output)
{
   tdm_output *output;
   tdm_error ret = TDM_ERROR_NONE;
   E_EomClientPtr iterator = NULL;
   Eina_List *l;
   const tdm_output_mode *mode;
   unsigned int mmWidth, mmHeight;

   output = eom_output->output;

   ret = tdm_output_get_physical_size(output, &mmWidth, &mmHeight);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, -1);

   /* XXX: TMD returns not correct Primary mode for external output,
    * therefore we have to find it by ourself */
   mode = _e_eom_output_get_best_mode(output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(mode, -1);

   ret = tdm_output_set_mode(output, mode);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, -1);

   /* update eom_output connect */
   eom_output->width = mode->hdisplay;
   eom_output->height = mode->vdisplay;
   eom_output->phys_width = mmWidth;
   eom_output->phys_height = mmHeight;

   EOMDB("Setup new output: %s (%dx%d)", eom_output->name, eom_output->width, eom_output->height);

   /* TODO: check output mode(presentation set) and HDMI type */

   if (eom_output->state == WAIT_PRESENTATION)
     {
        EOMDB("Start Presentation");

        if (eom_output->delay)
          ecore_timer_del(eom_output->delay);
        eom_output->delay = ecore_timer_add(EOM_DELAY_CHECK_TIMEOUT, _e_eom_timer_delayed_presentation_mode, eom_output);

        _e_eom_output_start_presentation(eom_output);
     }
   else
     {
        EOMDB("Start Mirroring");
        _e_eom_output_start_mirror(eom_output);
     }

   eom_output->connection = WL_EOM_STATUS_CONNECTION;

   /* If there were previously connected clients to the output - notify them */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator->resource)
          {
             EOMDB("Send output connected notification to client: %p", iterator);

             if (iterator->current)
               wl_eom_send_output_info(iterator->resource, eom_output->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       0,
                                       _e_eom_output_state_get_attribute(eom_output),
                                       EOM_OUTPUT_ATTRIBUTE_STATE_ACTIVE,
                                       EOM_ERROR_NONE);
             else
               wl_eom_send_output_info(iterator->resource, eom_output->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       1, 0, 0, 0);
          }
     }

   return 0;
}

static void
_e_eom_output_disconnected(E_EomOutputPtr eom_output)
{
   E_EomClientPtr iterator = NULL;
   Eina_List *l;

   if (eom_output->delay)
     ecore_timer_del(eom_output->delay);

   if (eom_output->watchdog)
     ecore_timer_del(eom_output->watchdog);

   if (g_eom->rotate_output == eom_output)
     {
        if (g_eom->rotate_timer)
          ecore_timer_del(g_eom->rotate_timer);
        g_eom->rotate_timer = NULL;
        g_eom->rotate_output = NULL;
     }

   /* update eom_output disconnect */
   eom_output->width = 0;
   eom_output->height = 0;
   eom_output->phys_width = 0;
   eom_output->phys_height = 0;
   eom_output->connection = WL_EOM_STATUS_DISCONNECTION;

   _e_eom_output_deinit(eom_output);

   /* If there were previously connected clients to the output - notify them */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator->resource)
          {
             EOMDB("Send output disconnected notification to client: %p", iterator);

             if (iterator->current)
               wl_eom_send_output_info(iterator->resource, eom_output->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       0,
                                       _e_eom_output_state_get_attribute(eom_output),
                                       EOM_OUTPUT_ATTRIBUTE_STATE_INACTIVE,
                                       EOM_ERROR_NONE);
             else
               wl_eom_send_output_info(iterator->resource, eom_output->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       1, 0, 0, 0);
          }
     }

   EOMDB("Destory output: %s", eom_output->name);
   eina_stringshare_del(eom_output->name);
   eom_output->name = NULL;
}

static void
_e_eom_cb_tdm_output_status_change(tdm_output *output, tdm_output_change_type type, tdm_value value, void *user_data)
{
   tdm_output_type tdm_type;
   tdm_output_conn_status status, status_check;
   tdm_error ret = TDM_ERROR_NONE;
   const char *tmp_name;
   char new_name[DRM_CONNECTOR_NAME_LEN];
   E_EomOutputPtr eom_output = NULL, eom_output_tmp = NULL;
   Eina_List *l;

   g_eom->check_first_boot = 1;

   if (type == TDM_OUTPUT_CHANGE_DPMS || g_eom->main_output_state == 0)
     return;

   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output_tmp)
          {
             if (eom_output_tmp->output == output)
               eom_output = eom_output_tmp;
          }
     }

   EINA_SAFETY_ON_NULL_RETURN(eom_output);

   ret = tdm_output_get_output_type(output, &tdm_type);
   EINA_SAFETY_ON_FALSE_RETURN(ret == TDM_ERROR_NONE);

   ret = tdm_output_get_conn_status(output, &status_check);
   EINA_SAFETY_ON_FALSE_RETURN(ret == TDM_ERROR_NONE);

   status = value.u32;

   EOMDB("outupt id(%d), type(%d, %d), status(%d, %d)", eom_output->id, type, tdm_type, status_check, status);

   eom_output->type = (eom_output_type_e)tdm_type;
   eom_output->status = status;

   if (status == TDM_OUTPUT_CONN_STATUS_CONNECTED)
     {
        if (tdm_type < ALEN(eom_conn_types))
          tmp_name = eom_conn_types[tdm_type];
        else
          tmp_name = "unknown";

        /* TODO: What if there will more then one output of same type.
         * e.g. "HDMI and HDMI" "LVDS and LVDS"*/
        snprintf(new_name, sizeof(new_name), "%s-%d", tmp_name, 0);

        eom_output->name = eina_stringshare_add(new_name);

#ifdef ENABLE_HWC_MULTI
        e_comp_hwc_multi_plane_set(EINA_FALSE);
#else
        e_comp_override_add();
#endif
        _e_eom_output_connected(eom_output);
     }
   else if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
     {
        _e_eom_output_disconnected(eom_output);
#ifdef ENABLE_HWC_MULTI
        e_comp_hwc_multi_plane_set(EINA_TRUE);
#else
        e_comp_override_del();
#endif
     }
}

static Eina_Bool
_e_eom_output_init(tdm_display *dpy)
{
   E_EomOutputPtr new_output = NULL;
   tdm_output *output = NULL;
   tdm_output_type type;
   tdm_output_conn_status status;
   const tdm_output_mode *mode = NULL;
   tdm_error ret = TDM_ERROR_NONE;
   unsigned int mmWidth, mmHeight;
   int i, count;
   Eina_List *l;
   E_EomOutputPtr eom_output = NULL;

   ret = tdm_display_get_output_count(dpy, &count);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(count > 1, EINA_FALSE);

   g_eom->output_count = count - 1;
   EOMDB("external output count : %d", g_eom->output_count);

   /* skip main output id:0 */
   /* start from 1 */
   for (i = 1; i < count; i++)
     {
        output = tdm_display_get_output(dpy, i, &ret);
        EINA_SAFETY_ON_FALSE_GOTO(ret == TDM_ERROR_NONE, err);
        EINA_SAFETY_ON_NULL_GOTO(output, err);

        ret = tdm_output_get_output_type(output, &type);
        EINA_SAFETY_ON_FALSE_GOTO(ret == TDM_ERROR_NONE, err);

        new_output = E_NEW(E_EomOutput, 1);
        EINA_SAFETY_ON_NULL_GOTO(new_output, err);

        ret = tdm_output_get_conn_status(output, &status);
        if (ret != TDM_ERROR_NONE)
          {
             EOMER("tdm_output_get_conn_status fail(%d)", ret);
             free(new_output);
             goto err;
          }

        new_output->id = i;
        new_output->type = type;
        new_output->status = status;
        new_output->mode = EOM_OUTPUT_MODE_NONE;
        new_output->connection = WL_EOM_STATUS_NONE;
        new_output->output = output;

        ret = tdm_output_add_change_handler(output, _e_eom_cb_tdm_output_status_change, NULL);
        if (ret != TDM_ERROR_NONE)
          {
             EOMER("tdm_output_add_change_handler fail(%d)", ret);
             free(new_output);
             goto err;
          }

        if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
          {
             EOMDB("create(%d)output, type:%d, status:%d",
                   new_output->id, new_output->type, new_output->status);
             g_eom->outputs = eina_list_append(g_eom->outputs, new_output);
             continue;
          }

        new_output->status = TDM_OUTPUT_CONN_STATUS_CONNECTED;

        ret = tdm_output_get_mode(output, &mode);
        if (ret != TDM_ERROR_NONE)
          {
             EOMER("tdm_output_get_mode fail(%d)", ret);
             free(new_output);
             goto err;
          }

        if (mode == NULL)
          {
             new_output->width = 0;
             new_output->height = 0;
          }
        else
          {
             new_output->width = mode->hdisplay;
             new_output->height = mode->vdisplay;
          }

        ret = tdm_output_get_physical_size(output, &mmWidth, &mmHeight);
        if (ret != TDM_ERROR_NONE)
          {
             EOMER("tdm_output_get_conn_status fail(%d)", ret);
             free(new_output);
             goto err;
          }

        new_output->phys_width = mmWidth;
        new_output->phys_height = mmHeight;

        EOMDB("create(%d)output, type:%d, status:%d, w:%d, h:%d, mm_w:%d, mm_h:%d",
              new_output->id, new_output->type, new_output->status,
              new_output->width, new_output->height, new_output->phys_width, new_output->phys_height);

        g_eom->outputs = eina_list_append(g_eom->outputs, new_output);
     }

   return EINA_TRUE;

err:
   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output)
          free(eom_output);

        eina_list_free(g_eom->outputs);

        g_eom->outputs = NULL;
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_eom_init_internal()
{
   g_eom->dpy = e_comp->e_comp_screen->tdisplay;
   EINA_SAFETY_ON_NULL_GOTO(g_eom->dpy, err);

   g_eom->bufmgr = e_comp->e_comp_screen->bufmgr;
   EINA_SAFETY_ON_NULL_GOTO(g_eom->bufmgr, err);

   if (_e_eom_output_init(g_eom->dpy) != EINA_TRUE)
     {
        EOMER("_e_eom_output_init fail");
        goto err;
     }

   return EINA_TRUE;

err:
   if (g_eom->bufmgr)
     g_eom->bufmgr = NULL;

   if (g_eom->dpy)
     g_eom->dpy = NULL;

   return EINA_FALSE;
}

static void
_e_eom_deinit()
{
   Ecore_Event_Handler *h = NULL;
   Eina_List *l;
   E_EomOutputPtr output;

   if (g_eom == NULL) return;

   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
          ecore_event_handler_del(h);

        g_eom->handlers = NULL;
     }

   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          free(output);

        eina_list_free(g_eom->outputs);

        g_eom->outputs = NULL;
     }

   if (g_eom->dpy)
     g_eom->dpy = NULL;

   if (g_eom->bufmgr)
     g_eom->bufmgr = NULL;

   if (g_eom->global)
     wl_global_destroy(g_eom->global);
   g_eom->global = NULL;

   E_FREE(g_eom);
}

static E_EomClientPtr
_e_eom_client_get_by_resource(struct wl_resource *resource)
{
   Eina_List *l;
   E_EomClientPtr client;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        if (client && client->resource == resource)
          return client;
     }

   return NULL;
}

static E_EomOutputPtr
_e_eom_output_get_by_id(int id)
{
   Eina_List *l;
   E_EomOutputPtr output;

   EINA_LIST_FOREACH(g_eom->outputs, l, output)
     {
        if (output && output->id == id)
          return output;
     }

   return NULL;
}

static E_EomOutputPtr
_e_eom_output_by_ec_child_get(E_Client *ec)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomClientPtr eom_client = NULL;
   E_Client *parent = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(g_eom->outputs, l, eom_output)
     {
        eom_client = _e_eom_client_get_current_by_id(eom_output->id);
        if (!eom_client)
          continue;

        if (eom_client->ec == ec)
          return eom_output;

        if (!ec->comp_data || !ec->comp_data->sub.data)
          continue;

        parent = ec->comp_data->sub.data->parent;
        while (parent)
          {
             if (parent == eom_client->ec)
               return eom_output;

             if (!parent->comp_data || !parent->comp_data->sub.data)
               break;

             parent = parent->comp_data->sub.data->parent;
          }
     }

   return NULL;
}

static void
_e_eom_output_hide_layers(E_EomOutputPtr eom_output)
{
   tdm_layer * layer = NULL;

   if (!eom_output || eom_output->state == NONE)
     return;

   layer = e_comp_wl_video_layer_get(eom_output->output);
   if (!layer)
     return;

   /* XXX: sometimes video buffers are keep showing on a layer, therefore
    * we have to clear those stuck buffers from a layer */
   tdm_layer_unset_buffer(layer);
}

static void
_e_eom_top_ec_angle_get(void)
{
#ifdef SUPPORT_ROTATE
   E_Client *ec;

   ec = _e_eom_top_visible_ec_get();
   if (ec)
     {
        g_eom->angle = ec->e.state.rot.ang.curr;
        if (eom_trace_debug)
          EOMDB("top ec rotate angle:%d", g_eom->angle);
     }
#else
   g_eom->angle = 0;
#endif
}

static void
_e_eom_cb_wl_eom_client_destory(struct wl_resource *resource)
{
   E_EomClientPtr client = NULL, iterator = NULL;
   E_EomOutputPtr output = NULL;
   Eina_List *l = NULL;
   tdm_error err = TDM_ERROR_NONE;
   Eina_Bool ret;

   EOMDB("=======================>  CLIENT DESTROY");

   EINA_SAFETY_ON_NULL_RETURN(resource);

   client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(client);

   g_eom->clients = eina_list_remove(g_eom->clients, client);

   if (client->current == EINA_FALSE)
     goto end2;

   output = _e_eom_output_get_by_id(client->output_id);
   EINA_SAFETY_ON_NULL_GOTO(output, end2);

   ret = _e_eom_output_state_set_attribute(output, EOM_OUTPUT_ATTRIBUTE_NONE);
   (void)ret;

   if (output->state == NONE)
     goto end;

   if (output->state == WAIT_PRESENTATION)
     {
        output->state = NONE;
        goto end;
     }

   if (output->show_overlay_buff)
     {
        if (output->overlay_layer)
          {
             err = tdm_layer_unset_buffer(output->overlay_layer);
             if (err != TDM_ERROR_NONE)
               EOMER("fail unset buffer:%d", err);

             err = tdm_layer_commit(output->overlay_layer, NULL, output);
             if (err != TDM_ERROR_NONE)
               EOMER("fail commit on deleting output err:%d", err);
          }

        _e_eom_output_overlay_buff_release(output);
     }
   output->overlay_layer = NULL;
   output->need_overlay_pp = EINA_FALSE;

   _e_eom_output_all_buff_release(output);

   /* If a client has been disconnected and mirror mode has not
    * been restored, start mirror mode
    */
   _e_eom_top_ec_angle_get();
   _e_eom_output_start_mirror(output);

end:
   /* Notify eom clients which are binded to a concrete output that the
    * state and mode of the output has been changed */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator != client && iterator->output_id == output->id)
          {
             wl_eom_send_output_attribute(iterator->resource, output->id,
                                          _e_eom_output_state_get_attribute(output),
                                          _e_eom_output_state_get_attribute_state(output),
                                          EOM_OUTPUT_MODE_NONE);

             wl_eom_send_output_mode(iterator->resource, output->id,
                                     _e_eom_output_state_get_mode(output));
          }
     }

end2:
   free(client);
}

static void
_e_eom_cb_wl_request_set_attribute(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   eom_error_e eom_error = EOM_ERROR_NONE;
   E_EomClientPtr eom_client = NULL, current_eom_client = NULL, iterator = NULL;
   E_EomOutputPtr eom_output = NULL;
   Eina_Bool ret = EINA_FALSE;
   Eina_List *l;

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   /* Bind the client with a concrete output */
   eom_client->output_id = output_id;

   eom_output = _e_eom_output_get_by_id(output_id);
   EINA_SAFETY_ON_NULL_GOTO(eom_output, no_output);

   EOMDB("Set attribute:%d, client:%p", attribute, eom_client);

   if (eom_client->current == EINA_TRUE && eom_output->id == eom_client->output_id)
     {
        /* Current client can set any flag it wants */
        _e_eom_output_state_set_force_attribute(eom_output, attribute);
     }
   else if (eom_output->id == eom_client->output_id)
     {
        /* A client is trying to set new attribute */
        ret = _e_eom_output_state_set_attribute(eom_output, attribute);
        if (ret == EINA_FALSE)
          {
             EOMDB("set attribute FAILED");

             eom_error = EOM_ERROR_INVALID_PARAMETER;
             goto end;
          }
     }
   else
     return;

   /* If client has set EOM_OUTPUT_ATTRIBUTE_NONE switching to mirror mode */
   if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE && eom_output->state != MIRROR)
     {
        eom_client->current = EINA_FALSE;

        _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);
        _e_eom_output_state_set_attribute(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);

        if (eom_output->status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
          {
             if (eom_trace_debug)
               EOMDB("output:%d is disconnected", output_id);
             goto end;
          }

        ret = _e_eom_output_start_mirror(eom_output);
        EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, end);

        /* If mirror mode has been run notify all clients about that */
        if (eom_trace_debug)
          EOMDB("client set NONE attribute, send new info to previous current client");
        EINA_LIST_FOREACH(g_eom->clients, l, iterator)
          {
             if (iterator && iterator->output_id == output_id)
               {
                  wl_eom_send_output_attribute(iterator->resource, eom_output->id,
                                               _e_eom_output_state_get_attribute(eom_output),
                                               _e_eom_output_state_get_attribute_state(eom_output),
                                               EOM_ERROR_NONE);

                  wl_eom_send_output_mode(iterator->resource, eom_output->id,
                                          _e_eom_output_state_get_mode(eom_output));
               }
          }

        return;
     }

end:
   /* If client was not able to set attribute send LOST event to it */
   if (eom_error == EOM_ERROR_INVALID_PARAMETER)
     {
        EOMDB("client failed to set attribute");

        wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                     _e_eom_output_state_get_attribute(eom_output),
                                     EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                     eom_error);
        return;
     }

   /* Send changes to the caller-client */
   wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                _e_eom_output_state_get_attribute(eom_output),
                                _e_eom_output_state_get_attribute_state(eom_output),
                                eom_error);

   current_eom_client = _e_eom_client_get_current_by_id(eom_output->id);
   EOMDB("Substitute current client: new:%p, old:%p", eom_client, current_eom_client);

   /* Send changes to previous current client */
   if (eom_client->current == EINA_FALSE && current_eom_client)
     {
        current_eom_client->current = EINA_FALSE;

        /* Actually deleting of buffers right here is a hack intended to
         * send release events of buffers to current client, since it could
         * be locked until it get 'release' event */
        EOMDB("Send changes to previous current client, and delete buffers");
        _e_eom_output_all_buff_release(eom_output);

        wl_eom_send_output_attribute(current_eom_client->resource, eom_output->id,
                                     _e_eom_output_state_get_attribute(eom_output),
                                     EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                     EOM_ERROR_NONE);
     }

   /* Set the client as current client of the eom_output */
   eom_client->current= EINA_TRUE;

   _e_eom_output_hide_layers(eom_output);

   if (eom_output->status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
     eom_output->state = WAIT_PRESENTATION;

   return;

   /* Get here if EOM does not have output referred by output_id */
no_output:
   wl_eom_send_output_attribute(eom_client->resource, output_id,
                                EOM_OUTPUT_ATTRIBUTE_NONE,
                                EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                                EOM_ERROR_NO_SUCH_DEVICE);

   wl_eom_send_output_mode(eom_client->resource, output_id,
                           EOM_OUTPUT_MODE_NONE);

   wl_eom_send_output_type(eom_client->resource, output_id,
                           EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                           TDM_OUTPUT_CONN_STATUS_DISCONNECTED);
}

static Eina_Bool
_e_eom_cb_comp_object_redirected(void *data, E_Client *ec)
{
   E_EomCompObjectInterceptHookData *hook_data;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, EINA_TRUE);

   hook_data = (E_EomCompObjectInterceptHookData* )data;

   if (!hook_data->ec || !hook_data->hook)
     return EINA_TRUE;

   if (hook_data->ec != ec)
     return EINA_TRUE;

   /* Hide the window from Enlightenment main screen */
   e_client_redirected_set(ec, EINA_FALSE);

   e_comp_object_intercept_hook_del(hook_data->hook);

   g_eom->comp_object_intercept_hooks = eina_list_remove(g_eom->comp_object_intercept_hooks, hook_data);

   free(hook_data);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_util_add_comp_object_redirected_hook(E_Client *ec)
{
   E_EomCompObjectInterceptHookData *hook_data = NULL;
   E_Comp_Object_Intercept_Hook *hook = NULL;

   hook_data = E_NEW(E_EomCompObjectInterceptHookData, 1);
   EINA_SAFETY_ON_NULL_GOTO(hook_data, err);

   hook_data->ec = ec;

   hook = e_comp_object_intercept_hook_add(E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER,
                                           _e_eom_cb_comp_object_redirected, hook_data);
   EINA_SAFETY_ON_NULL_GOTO(hook, err);

   hook_data->hook = hook;

   g_eom->comp_object_intercept_hooks = eina_list_append(g_eom->comp_object_intercept_hooks, hook_data);

   return EINA_TRUE;

err:
   if (hook_data)
     free(hook_data);

   return EINA_FALSE;
}

static void
_e_eom_window_set_internal(struct wl_resource *resource, int output_id, E_Client *ec)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomClientPtr eom_client = NULL;
   E_Comp_Client_Data *cdata = NULL;
   Eina_Bool ret = EINA_FALSE;

   if (resource == NULL || output_id <= 0 || ec == NULL)
     return;

   cdata = ec->comp_data;
   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->shell.configure_send);

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   eom_output = _e_eom_output_get_by_id(output_id);
   if (eom_output == NULL)
     {
        wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_NO_OUTPUT);
        return;
     }

   ret = _e_eom_util_add_comp_object_redirected_hook(ec);
   EINA_SAFETY_ON_FALSE_RETURN(ret == EINA_TRUE);

   EOMDB("e_comp_object_redirected_set (ec:%p)(ec->frame:%p)\n", ec, ec->frame);

   /* Send reconfigure event to a client which will resize its window to
    * external output resolution in respond */
   cdata->shell.configure_send(ec->comp_data->shell.surface, 0, eom_output->width, eom_output->height);

   /* ec is used in buffer_change callback for distinguishing external ec and its buffers */
   eom_client->ec = ec;

   if (eom_client->current == EINA_TRUE)
     wl_eom_send_output_set_window(resource, eom_output->id, WL_EOM_ERROR_NONE);
   else
     wl_eom_send_output_set_window(resource, eom_output->id, WL_EOM_ERROR_OUTPUT_OCCUPIED);
}

static void
_e_eom_cb_wl_request_set_xdg_window(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, struct wl_resource *surface)
{
   E_Client *ec = NULL;

   if (resource == NULL || output_id <= 0 || surface == NULL)
     return;

   EOMDB("set xdg output id:%d resource:%p surface:%p", output_id, resource, surface);

   if (!(ec = wl_resource_get_user_data(surface)))
     {
        wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT, "No Client For Shell Surface");
        return;
     }

   _e_eom_window_set_internal(resource, output_id, ec);
}

static void
_e_eom_cb_wl_request_set_shell_window(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, struct wl_resource *surface)
{
   E_Client *ec = NULL;

   if (resource == NULL || output_id <= 0 || surface == NULL)
     return;

   EOMDB("set shell output id:%d resource:%p surface:%p", output_id, resource, surface);

   if (!(ec = wl_resource_get_user_data(surface)))
     {
        wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT, "No Client For Shell Surface");
        return;
     }

   _e_eom_window_set_internal(resource, output_id, ec);
}

static void
_e_eom_cb_wl_request_get_output_info(struct wl_client *client, struct wl_resource *resource, uint32_t output_id)
{
   EOMDB("get output info:%d", output_id);

   if (g_eom->outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output = NULL;

        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             if (output->id == output_id)
               {
                  EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                        output->id, output->type, output->mode, output->width, output->height,
                        output->phys_width, output->phys_height, output->status);

                  wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->width, output->height,
                                          output->phys_width, output->phys_height, output->connection,
                                          1, 0, 0, 0);
               }
          }
     }
}

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_cb_wl_request_set_attribute,
   _e_eom_cb_wl_request_set_xdg_window,
   _e_eom_cb_wl_request_set_shell_window,
   _e_eom_cb_wl_request_get_output_info
};

static void
_e_eom_cb_wl_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource = NULL;
   E_EomClientPtr new_client = NULL;
   E_EomPtr eom = NULL;
   E_EomOutputPtr output = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN(data);
   eom = data;

   resource = wl_resource_create(client, &wl_eom_interface, MIN(version, 1), id);
   if (resource == NULL)
     {
        EOMER("resource is null. (version :%d, id:%d)", version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource, &_e_eom_wl_implementation, eom, _e_eom_cb_wl_eom_client_destory);

   wl_eom_send_output_count(resource, g_eom->output_count);

   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, output)
          {
             if (eom_trace_debug)
               EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                     output->id, output->type, output->mode, output->width, output->height,
                     output->phys_width, output->phys_height, output->status);
             wl_eom_send_output_info(resource, output->id, output->type, output->mode, output->width, output->height,
                                     output->phys_width, output->phys_height, output->connection,
                                     1, 0, 0, 0);
          }
     }

   new_client = E_NEW(E_EomClient, 1);
   EINA_SAFETY_ON_NULL_RETURN(new_client);

   new_client->resource = resource;
   new_client->current = EINA_FALSE;
   new_client->output_id = -1;
   new_client->ec = NULL;

   g_eom->clients = eina_list_append(g_eom->clients, new_client);

   EOMDB("=======================>  BIND CLIENT");
}

static Eina_Bool
_e_eom_cb_ecore_drm_activate(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{

   Ecore_Drm_Event_Activate *e = NULL;

   if ((!event) || (!data))
     return ECORE_CALLBACK_PASS_ON;

   e = event;
   (void) e;

   EOMDB("e->active:%d", e->active);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_boot_connection_check(void *data)
{
   E_EomOutputPtr eom_output;
   tdm_output *output = NULL;
   tdm_output_type tdm_type = TDM_OUTPUT_TYPE_Unknown;
   tdm_output_conn_status status = TDM_OUTPUT_CONN_STATUS_DISCONNECTED;
   tdm_error ret = TDM_ERROR_NONE;
   const char *tmp_name;
   char new_name[DRM_CONNECTOR_NAME_LEN];
   Eina_List *l;

   if (g_eom->check_first_boot != 0)
     {
        g_eom->timer = NULL;
        return ECORE_CALLBACK_CANCEL;
     }

   g_eom->check_first_boot = 1;

   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output)
          {
             if (eom_output->id == 0)
               continue;

             output = eom_output->output;
             if (output == NULL)
               {
                  EOMER("output is null fail");
                  continue;
               }

             ret = tdm_output_get_conn_status(output, &status);
             if (ret != TDM_ERROR_NONE)
               {
                  EOMER("tdm_output_get_conn_status fail(%d)", ret);
                  continue;
               }

             if (status == TDM_OUTPUT_CONN_STATUS_DISCONNECTED)
               continue;

             ret = tdm_output_get_output_type(output, &tdm_type);
             if (ret != TDM_ERROR_NONE)
               {
                  EOMER("tdm_output_get_output_type fail(%d)", ret);
                  continue;
               }

             if (tdm_type < ALEN(eom_conn_types))
               tmp_name = eom_conn_types[tdm_type];
             else
               tmp_name = "unknown";
             /* TODO: What if there will more then one output of same type.
              * e.g. "HDMI and HDMI" "LVDS and LVDS"*/
             snprintf(new_name, sizeof(new_name), "%s-%d", tmp_name, 0);

             eom_output->type = (eom_output_type_e)tdm_type;
             eom_output->name = eina_stringshare_add(new_name);
             eom_output->status = status;

             _e_eom_output_connected(eom_output);
          }
     }
   g_eom->timer = NULL;
   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_eom_cb_ecore_drm_output(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Drm_Event_Output *e = NULL;
   char buff[PATH_MAX];

   if (!(e = event)) return ECORE_CALLBACK_PASS_ON;

   EOMDB("id:%d (x,y,w,h):(%d,%d,%d,%d) (w_mm,h_mm):(%d,%d) refresh:%d subpixel_order:%d transform:%d make:%s model:%s name:%s plug:%d",
         e->id, e->x, e->y, e->w, e->h, e->phys_width, e->phys_height, e->refresh, e->subpixel_order, e->transform, e->make, e->model, e->name, e->plug);

   snprintf(buff, sizeof(buff), "%s", e->name);

   /* main output */
   if (e->id == 0)
     {
        if (e->plug == 1)
          {
             g_eom->width = e->w;
             g_eom->height = e->h;
             if (g_eom->main_output_name == NULL)
               g_eom->main_output_name = strdup(buff);

             g_eom->main_output_state = 1;

             if (g_eom->check_first_boot == 0)
               {
                  if (g_eom->timer)
                    ecore_timer_del(g_eom->timer);
                  g_eom->timer = ecore_timer_add(EOM_CONNECT_CHECK_TIMEOUT, _e_eom_boot_connection_check, NULL);
               }
          }
        else
          {
             g_eom->width = -1;
             g_eom->height = -1;
             if (g_eom->main_output_name)
               free(g_eom->main_output_name);

             g_eom->main_output_state = 0;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static E_EomClientPtr
_e_eom_client_get_current_by_ec(E_Client *ec)
{
   Eina_List *l;
   E_EomClientPtr client;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        if (client && client->current == EINA_TRUE && client->ec == ec)
          return client;
     }

   return NULL;
}

static void
_e_eom_tbm_buffer_release_ext_mod(E_EomOutputPtr eom_output, tbm_surface_h srfc, void *eom_buff)
{
   if (eom_trace_debug)
     EOMDB("============>  EXT END     tbm_buff:%p E_EomBuffer:%p", srfc, eom_buff);
   _e_eom_buffer_destroy(eom_buff);
}

static E_EomClientPtr
_e_eom_client_get_current_by_ec_parrent(E_Client *ec)
{
   Eina_List *l;
   E_EomClientPtr client;
   E_Client *parent = NULL;

   if (!ec->comp_data || !ec->comp_data->sub.data)
     return NULL;

   EINA_LIST_FOREACH(g_eom->clients, l, client)
     {
        parent = ec->comp_data->sub.data->parent;
        while (parent)
          {
             if (client->ec == parent)
               return client;

             if (!parent->comp_data || !parent->comp_data->sub.data)
               break;

             parent = parent->comp_data->sub.data->parent;
          }
     }

   return NULL;
}

static Eina_Bool
_e_eom_cb_client_buffer_change(void *data, int type, void *event)
{
   E_Comp_Wl_Buffer *wl_buffer = NULL;
   E_EomClientPtr eom_client = NULL, eom_client_itr = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   tbm_surface_h tbm_buffer = NULL;
   Eina_List *l;
   Eina_Bool overlay = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)),
                                  ECORE_CALLBACK_PASS_ON);

   eom_client = _e_eom_client_get_current_by_ec(ec);
   if (eom_client == NULL)
     {
        eom_client = _e_eom_client_get_current_by_ec_parrent(ec);
        if (eom_client == NULL)
          return ECORE_CALLBACK_PASS_ON;

        overlay = EINA_TRUE;
     }

   eom_output = _e_eom_output_get_by_id(eom_client->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, ECORE_CALLBACK_PASS_ON);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->pixmap, ECORE_CALLBACK_PASS_ON);

   wl_buffer = e_pixmap_resource_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_buffer, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_buffer->resource, ECORE_CALLBACK_PASS_ON);

   /* Since Enlightenment client has reconfigured its window to fit
    * external output resolution and Enlightenment no nothing about
    * external outputs Enlightenment sees that client's resolution
    * differs form main screen resolution. Therefore, Enlightenment
    * is trying to resize it back to main screen resolution. It uses
    * timer for that purpose. To forbid it just delete the timer */

   /* TODO: It works but maybe there is better solution exists ?
    * Also I do not know how it affects on performance */
   if (ec->map_timer)
     {
        if (eom_trace_debug)
          EOMDB("delete map_timer");
        E_FREE_FUNC(ec->map_timer, ecore_timer_del);
     }

   /* TODO: support different SHMEM buffers etc. */
   tbm_buffer = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, wl_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_buffer, ECORE_CALLBACK_PASS_ON);

   if ((tbm_surface_get_width(tbm_buffer) <= 1) || (tbm_surface_get_height(tbm_buffer) <= 1))
     return ECORE_CALLBACK_PASS_ON;

   E_EomBufferPtr eom_buff = _e_eom_buffer_create(wl_buffer);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_buff, ECORE_CALLBACK_PASS_ON);

   if (wl_buffer->w != eom_output->width || wl_buffer->h != eom_output->height )
     {
        Eina_Bool need_pp = EINA_FALSE;

        if (eom_trace_debug)
          EOMDB("tbm_buffer does not fit output's resolution. wl_buffer(%dx%d), eom_output(%dx%d), tbm_buff(%dx%d)",
                wl_buffer->w, wl_buffer->h, eom_output->width, eom_output->height,
                tbm_surface_get_width(tbm_buffer), tbm_surface_get_height(tbm_buffer));

        if (overlay)
          {
             Eina_Bool video_layer = EINA_FALSE;
             tbm_format format = tbm_surface_get_format(tbm_buffer);

             video_layer = _e_eom_output_video_layer_find(eom_output, format);
             if (!video_layer)
               {
                  /* need pp */
                  need_pp = EINA_TRUE;
                  eom_output->need_overlay_pp = EINA_TRUE;
                  if (!_e_eom_pp_init(eom_output, EINA_FALSE))
                    {
                       EOMER("pp_init for overlay fail");
                       return ECORE_CALLBACK_PASS_ON;
                    }
               }
             /* else - possible direct buffer set */
          }
        else
          {
             need_pp = EINA_TRUE;
             if (!_e_eom_pp_init(eom_output, EINA_TRUE))
               {
                  EOMER("pp_init for primary fail");
                  return ECORE_CALLBACK_PASS_ON;
               }
          }

        if (need_pp)
          {
             EOMDB("run _e_eom_presentation_pp_run");
             _e_eom_presentation_pp_run(eom_output, tbm_buffer, eom_buff, !overlay);
             return ECORE_CALLBACK_PASS_ON;
          }
     }

   if (eom_trace_debug)
     EOMDB("===============>  EXT START   tbm_buff:%p", tbm_buffer);

#ifdef EOM_DUMP_PRESENTATION_BUFFERS
   char file[256];
   static int i;
   snprintf(file, sizeof file, "%s_%d", "eom_external", i++);
   tbm_surface_internal_dump_buffer(tbm_buffer, file, i++, 0);
#endif

   if (overlay)
     _e_eom_layer_overlay_set(eom_output, tbm_buffer);

   if (!_e_eom_output_show(eom_output, tbm_buffer, _e_eom_tbm_buffer_release_ext_mod, eom_buff, !overlay))
     {
        EOMDB("===============>  EXT ENDERR  tbm_buff:%p", tbm_buffer);
        EOMDB("_e_eom_add_buff_to_show fail");
        _e_eom_buffer_destroy(eom_buff);
        return ECORE_CALLBACK_PASS_ON;
     }

   if (eom_output->state == WAIT_PRESENTATION)
     {
        if (eom_trace_debug)
          EOMDB("remove delayed presentation timer");
        if (eom_output->delay)
          ecore_timer_del(eom_output->delay);
     }

   if (eom_output->state != PRESENTATION)
     {
        _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_PRESENTATION);

        EINA_LIST_FOREACH(g_eom->clients, l, eom_client_itr)
          {
             if (eom_client_itr->output_id == eom_output->id)
               wl_eom_send_output_mode(eom_client_itr->resource, eom_output->id,
                                       _e_eom_output_state_get_mode(eom_output));
          }

        eom_output->state = PRESENTATION;
     }

   if (eom_trace_debug)
     EOMDB("===============<  EXT START");

   return ECORE_CALLBACK_PASS_ON;
}

#ifdef SUPPORT_ROTATE
static Eina_Bool
_e_eom_cb_rotation_effect_ready(void *data, int type, void *event)
{
   E_EomPtr eom = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   eom = data;

   eom->rotate_state = ROTATE_INIT;

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_rotate(void *data)
{
   g_eom->rotate_state = ROTATE_DONE;

   _e_eom_top_ec_angle_get();

   if (g_eom->rotate_output)
     _e_eom_pp_run(g_eom->rotate_output, EINA_FALSE);

   g_eom->rotate_timer = NULL;

   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_eom_cb_rotation_effect_cancel(void *data, int type, void *event)
{
   E_EomPtr eom = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   eom = data;

   eom->rotate_state = ROTATE_CANCEL;

   if (eom->rotate_timer)
     ecore_timer_del(eom->rotate_timer);

   if (g_eom->rotate_output)
     eom->rotate_timer = ecore_timer_add(EOM_ROTATE_DELAY_TIMEOUT, _e_eom_rotate, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_cb_rotation_effect_done(void *data, int type, void *event)
{
   E_Event_Zone_Rotation_Effect_Done *ev;
   E_EomPtr eom = NULL;
   E_Zone *zone;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   ev = event;
   eom = data;

   zone = ev->zone;
   EINA_SAFETY_ON_NULL_RETURN_VAL(zone, ECORE_CALLBACK_PASS_ON);

   if (eom_trace_debug)
     {
        EOMDB("-----------------------------------------------------");
        EOMDB("effect END: angles: prev:%d  curr:%d  next:%d  sub:%d",
              zone->rot.prev, zone->rot.curr,
              zone->rot.next, zone->rot.sub);
        EOMDB("effect END: rotate angle:%d", eom->angle);
        EOMDB("-----------------------------------------------------");
     }
   eom->angle = zone->rot.curr;

   if (eom->rotate_timer)
     ecore_timer_del(eom->rotate_timer);

   if (g_eom->rotate_output)
     eom->rotate_timer = ecore_timer_add(EOM_ROTATE_DELAY_TIMEOUT, _e_eom_rotate, NULL);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_cb_rotation_end(void *data, int evtype EINA_UNUSED, void *event)
{
   E_Client *ec = NULL;
   E_Event_Client *ev = NULL;
   E_EomPtr eom = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(data, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(event, ECORE_CALLBACK_PASS_ON);

   ev = event;
   eom = data;
   ec = ev->ec;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, ECORE_CALLBACK_PASS_ON);

   /* As I understand E sends rotate events to all visible apps, thus EOM's
    * "_e_eom_cb_rotation_end" will be called for each visible E_Client.
    * Therefore we are interested only in the first change of angle, other
    * events with the same angle value will be ignored. */
   if (eom->angle == ec->e.state.rot.ang.curr)
     return ECORE_CALLBACK_PASS_ON;

   if (eom->rotate_state == ROTATE_NONE)
     {
        eom->angle = ec->e.state.rot.ang.curr;
        eom->rotate_state = ROTATE_DONE;

        if (eom_trace_debug)
          {
             EOMDB("-----------------------------------------------------");
             EOMDB("END: ec:%p (%dx%d)", ec, ec->w, ec->h);
             EOMDB("END: angles: prev:%d  curr:%d  next:%d  res:%d",
                   ec->e.state.rot.ang.prev, ec->e.state.rot.ang.curr,
                   ec->e.state.rot.ang.next, ec->e.state.rot.ang.reserve);
             EOMDB("END: rotate angle:%d", eom->angle);
             EOMDB("-----------------------------------------------------");
          }
     }
   else if (eom->rotate_state == ROTATE_INIT)
     {
        eom->angle = ec->e.state.rot.ang.curr;
        eom->rotate_state = ROTATE_PENDING;
     }

   return ECORE_CALLBACK_PASS_ON;
}
#endif

static Eina_Bool
_e_eom_external_output_check()
{
   tdm_error ret = TDM_ERROR_NONE;
   tdm_display *dpy = NULL;
   int count;

   dpy = e_comp->e_comp_screen->tdisplay;

   ret = tdm_display_get_output_count(dpy, &count);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ret == TDM_ERROR_NONE, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(count > 1, EINA_FALSE);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_init()
{
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   if (!_e_eom_external_output_check())
     return EINA_TRUE;

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, EINA_FALSE);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp, &wl_eom_interface, 1, g_eom, _e_eom_cb_wl_bind);
   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   g_eom->angle = 0;
   g_eom->rotate_state = ROTATE_NONE;
   g_eom->main_output_name = NULL;

   ret = _e_eom_init_internal();
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, err);

   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_ACTIVATE, _e_eom_cb_ecore_drm_activate, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, ECORE_DRM_EVENT_OUTPUT, _e_eom_cb_ecore_drm_output, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_BUFFER_CHANGE, _e_eom_cb_client_buffer_change, NULL);
#ifdef SUPPORT_ROTATE
   /* TODO: add if def _F_ZONE_WINDOW_ROTATION_ */
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_ZONE_ROTATION_EFFECT_READY, _e_eom_cb_rotation_effect_ready, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_ZONE_ROTATION_EFFECT_CANCEL, _e_eom_cb_rotation_effect_cancel, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_ZONE_ROTATION_EFFECT_DONE, _e_eom_cb_rotation_effect_done, g_eom);
   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_END, _e_eom_cb_rotation_end, g_eom);
#endif
   return EINA_TRUE;

err:
   _e_eom_deinit();

   return EINA_FALSE;
}

EINTERN int
e_eom_init(void)
{
   Eina_Bool ret = EINA_FALSE;

   ret = _e_eom_init();

   if (ret == EINA_FALSE)
     return 0;

   return 1;
}

EINTERN int
e_eom_shutdown(void)
{
   if (!g_eom) return 1;

   _e_eom_deinit();

   return 1;
}

EINTERN Eina_Bool
e_eom_is_ec_external(E_Client *ec)
{
   E_EomOutputPtr eom_output;

   if (!g_eom) return EINA_FALSE;

   eom_output = _e_eom_output_by_ec_child_get(ec);
   if (!eom_output)
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN tdm_output*
e_eom_tdm_output_by_ec_get(E_Client *ec)
{
   E_EomOutputPtr eom_output;

   if (!g_eom) return NULL;

   eom_output = _e_eom_output_by_ec_child_get(ec);
   if (!eom_output)
     return NULL;

   return eom_output->output;
}
