#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <xdg-shell-unstable-v5-server-protocol.h>
#include <eom-server-protocol.h>
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
#define EOM_CONNECT_CHECK_TIMEOUT 10.0
#define EOM_DELAY_CHECK_TIMEOUT 1.0
#define EOM_DELAY_CONNECT_CHECK_TIMEOUT 3.0

#define TDM_CONNECTOR_NAME_LEN 32

#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

typedef struct _E_Eom E_Eom, *E_EomPtr;
typedef struct _E_Eom_Out_Mode E_EomOutMode, *E_EomOutModePtr;
typedef struct _E_Eom_Output E_EomOutput, *E_EomOutputPtr;
typedef struct _E_Eom_Virtual_Output E_EomVirtualOutput, *E_EomVirtualOutputPtr;
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
   unsigned int virtual_output_count;
   Eina_List *virtual_outputs;
   Eina_List *added_outputs;

   /* Internal output data */
   Eina_Bool main_output_state;
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
   Eina_Bool connection_status;
   enum wl_eom_status connection;

   /* pp overlay (presentation mode subsurface data) */
   E_EomOutputPpPtr pp_overlay;
   Eina_Bool pp_overlay_converting;
   Eina_Bool pp_overlay_deinit;
   Eina_List *pending_overlay_buff;       /* can be deleted any time */
   E_EomOutputBufferPtr wait_overlay_buff; /* wait end of commit, can't be deleted */
   E_EomOutputBufferPtr show_overlay_buff; /* current showed buffer, can be deleted only after commit event with different buff */
   Eina_List *pending_pp_overlay;
   Eina_List *pp_overlay_data;

   /* If attribute has been set while external output is disconnected
    * then show black screen and wait until EOM client start sending
    * buffers. After expiring of the delay start mirroring */
   Ecore_Timer *delay_timer;

   E_Output *eout;
   E_EomVirtualOutput *voutput;
};

struct _E_Eom_Virtual_Output
{
   unsigned int id;
   eom_output_type_e type;
   eom_output_mode_e mode;
   unsigned int width;
   unsigned int height;
   unsigned int phys_width;
   unsigned int phys_height;

   E_EomOutputState state;
   Eina_Bool connection_status;
   eom_output_attribute_e attribute;
   eom_output_attribute_state_e attribute_state;
   enum wl_eom_status connection;

   E_EomOutput *eom_output;
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

static E_EomPtr g_eom = NULL;
static Eina_Bool eom_trace_debug = 0;

static void _e_eom_presentation_pp_run(E_EomOutputPtr eom_output, tbm_surface_h src_surface, E_EomBufferPtr eom_buff);

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

static inline eom_output_mode_e
_e_eom_virtual_output_state_get_mode(E_EomVirtualOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_MODE_NONE;
   return output->mode;
}

static inline void
_e_eom_virtual_output_state_set_mode(E_EomVirtualOutputPtr output, eom_output_mode_e mode)
{
   if (output == NULL)
     return;
   output->mode = mode;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute_state(E_EomVirtualOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_STATE_NONE;
   return output->attribute_state;
}

static inline void
_e_eom_output_attribute_state_set(E_EomVirtualOutputPtr output, eom_output_attribute_e attribute_state)
{
   if (output == NULL)
     return;
   output->attribute_state = attribute_state;
}

static inline eom_output_attribute_e
_e_eom_output_state_get_attribute(E_EomVirtualOutputPtr output)
{
   if (output == NULL)
     return EOM_OUTPUT_ATTRIBUTE_NONE;
   return output->attribute;
}

static inline void
_e_eom_output_state_set_force_attribute(E_EomVirtualOutputPtr output, eom_output_attribute_e attribute)
{
   if (output == NULL)
     return;
   output->attribute = attribute;
}

static inline Eina_Bool
_e_eom_output_state_set_attribute(E_EomVirtualOutputPtr output, eom_output_attribute_e attribute)
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
_e_eom_pp_init(E_EomOutputPtr eom_output)
{
   tdm_error err = TDM_ERROR_NONE;
   E_EomOutputPpPtr eom_pp = NULL;
   tdm_pp *pp = NULL;
   tbm_surface_queue_h queue = NULL;

   if (eom_output->pp_overlay != NULL)
     return EINA_TRUE;

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
_e_eom_pp_deinit(E_EomOutputPtr eom_output)
{
   E_EomOutputPpPtr eom_pp = NULL;

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

   eom_output->pp_overlay = NULL;
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
   E_EomPpDataPtr pending_pp = NULL;
   tdm_layer *tlayer;
   tbm_surface_h tsurface;
   E_EomBufferPtr eom_buffer;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   outbuff = (E_EomOutputBufferPtr)user_data;

   eom_output = outbuff->eom_output;
   EINA_SAFETY_ON_NULL_RETURN(eom_output);

   if (eom_trace_debug)
     EOMDB("========================>  CM  END     tbm_buff:%p", outbuff->tbm_surface);

   /*it means that eom_output has been canceled*/
   if (eom_output->wait_overlay_buff == NULL)
     {
        _e_eom_output_buff_delete(outbuff);
        return;
     }
   wait_buff = eom_output->wait_overlay_buff;
   show_buff = eom_output->show_overlay_buff;
   tlayer = eom_output->overlay_layer;

   EINA_SAFETY_ON_FALSE_RETURN(wait_buff == outbuff);

   if (eom_trace_debug)
     EOMDB("commit finish tbm_surface_h:%p", outbuff->tbm_surface);

   /* check if show buffer is present */
   if (show_buff != NULL)
     {
        if (eom_trace_debug)
          EOMDB("delete show buffer tbm_surface_h:%p", show_buff->tbm_surface);
        _e_eom_output_buff_delete(show_buff);
        eom_output->show_overlay_buff = NULL;
     }

   /* set wait_buffer as show_buff */
   if (eom_trace_debug)
     EOMDB("set wait_buffer as show_buff tbm_surface_h:%p", outbuff->tbm_surface);

   eom_output->wait_overlay_buff = NULL;
   eom_output->show_overlay_buff = outbuff;

   /* check if pending buffer is present */
   if (eina_list_count(eom_output->pending_overlay_buff) != 0)
     {
        outbuff = eina_list_nth(eom_output->pending_overlay_buff, 0);
        if (outbuff != NULL)
          {
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

             eom_output->wait_overlay_buff = outbuff;
          }
     }

   if (eina_list_count(eom_output->pending_pp_overlay) != 0)
     {
        pending_pp = eina_list_nth(eom_output->pending_pp_overlay, 0);
        if (pending_pp != NULL)
          {
             if (!tbm_surface_queue_can_dequeue(eom_output->pp_overlay->queue, 0))
               return;

             eom_output->pending_pp_overlay = eina_list_remove(eom_output->pending_pp_overlay, pending_pp);

             tsurface = pending_pp->tsurface;
             eom_buffer = pending_pp->eom_buffer;

             E_FREE(pending_pp);

             _e_eom_presentation_pp_run(eom_output, tsurface, eom_buffer);
          }
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
                   E_EomEndShowingEventPtr cb_func, void *cb_user_data)
{
   tdm_error err = TDM_ERROR_NONE;
   tdm_layer *layer;

   /* create new output buffer */
   E_EomOutputBufferPtr outbuff = _e_eom_output_buff_create(eom_output, tbm_srfc, cb_func, cb_user_data);
   EINA_SAFETY_ON_NULL_RETURN_VAL(outbuff, EINA_FALSE);

   /* check if output free to commit */
   if (eina_list_count(eom_output->pending_overlay_buff) != 0)
     {
        eom_output->pending_overlay_buff = eina_list_append(eom_output->pending_overlay_buff, outbuff);

        if (eom_trace_debug)
          EOMDB("add to pending list tdm_output:%p tdm_layer:%p tbm_surface_h:%p",
                eom_output->output, eom_output->overlay_layer, outbuff->tbm_surface);

        return EINA_TRUE;
     }
   else
     {
        layer = eom_output->overlay_layer;
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
_e_eom_pp_info_set(E_EomOutputPtr eom_output, tbm_surface_h src, tbm_surface_h dst)
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

   width = src_aligned_w;
   height = src_info.height;

   _e_eom_util_calculate_fullsize(width, height, eom_output->width, eom_output->height,
                                  &x, &y, &w, &h);

   if (eom_trace_debug)
     {
        EOMDB("PP prentation: src:%dx%d, dst:%dx%d", src_info.width, src_info.height, dst_info.width, dst_info.height);
        EOMDB("PP prentation calculation: x:%d, y:%d, w:%d, h:%d", x, y, w, h);
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

   pp_info.transform = TDM_TRANSFORM_NORMAL;

   pp_info.sync = 0;
   pp_info.flags = 0;

   if (memcmp(&eom_output->pp_overlay->pp_info, &pp_info, sizeof(tdm_info_layer)))
     {
        err = tdm_pp_set_info(eom_output->pp_overlay->pp, &pp_info);
        EINA_SAFETY_ON_FALSE_RETURN_VAL(err == TDM_ERROR_NONE, EINA_FALSE);
        memcpy(&eom_output->pp_overlay->pp_info, &pp_info, sizeof(tdm_info_layer));
     }

   return EINA_TRUE;
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
_e_eom_cb_pp_presentation(E_EomOutputPtr eom_output, E_EomPpDataPtr ppdata, E_EomOutputPpPtr eom_pp)
{
   E_EomVirtualOutputPtr voutput;
   E_EomBufferPtr eom_buff;
   tbm_surface_h tsurface;

   eom_buff = ppdata->eom_buffer;
   tsurface = ppdata->tsurface;

   E_FREE(ppdata);

   voutput = eom_output->voutput;
   if (!voutput)
     {
        tbm_surface_queue_release(eom_pp->queue, tsurface);
        return;
     }

   if (g_eom->main_output_state == EINA_FALSE)
     {
        tbm_surface_queue_release(eom_pp->queue, tsurface);
        return;
     }

   if (voutput->state == MIRROR)
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

   _e_eom_layer_overlay_set(eom_output, tsurface);

   if (!_e_eom_output_show(eom_output, tsurface, _e_eom_tbm_buffer_release_pp_overlay, eom_buff))
     {
        EOMER("pp show fail");
        tbm_surface_queue_release(eom_output->pp_overlay->queue, tsurface);
     }

   if (eom_trace_debug)
     EOMDB("==============================<  presentation PP");
}

static E_EomPpDataPtr
_e_eom_pp_data_get(E_EomOutputPtr eom_output, tbm_surface_h tsurface)
{
   Eina_List *l;
   E_EomPpDataPtr ppdata = NULL;

   EINA_LIST_FOREACH(eom_output->pp_overlay_data, l, ppdata)
     {
        if (!ppdata) continue;

        if (ppdata->tsurface == tsurface)
          return ppdata;
     }

   return NULL;
}

static void
_e_eom_cb_pp_presentation_overlay(tdm_pp *pp, tbm_surface_h tsurface_src, tbm_surface_h tsurface_dst, void *user_data)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomPpDataPtr ppdata = NULL;

   EINA_SAFETY_ON_NULL_RETURN(user_data);
   eom_output = (E_EomOutputPtr)user_data;

   ppdata = _e_eom_pp_data_get(eom_output, tsurface_src);
   EINA_SAFETY_ON_NULL_RETURN(ppdata);

   eom_output->pp_overlay_data = eina_list_remove(eom_output->pp_overlay_data, ppdata);

   tbm_surface_internal_unref(tsurface_src);
   tbm_surface_internal_unref(tsurface_dst);

   eom_output->pp_overlay_converting = EINA_FALSE;

   if (eom_output->pp_overlay_deinit)
     {
        eom_output->pp_overlay_deinit = EINA_FALSE;
        _e_eom_pp_deinit(eom_output);
     }

   if (eom_trace_debug)
     EOMDB("==============================>  presentation PP  END  overlay   tbm_buff:%p  ppdata:%p", tsurface_dst, ppdata);

   if (eom_output->pp_overlay == NULL)
     {
        E_FREE(ppdata);
        return;
     }

   ppdata->tsurface = tsurface_dst;

   _e_eom_cb_pp_presentation(eom_output, ppdata, eom_output->pp_overlay);
}

static void
_e_eom_presentation_pp_run(E_EomOutputPtr eom_output, tbm_surface_h src_surface, E_EomBufferPtr eom_buff)
{
   tdm_error tdm_err = TDM_ERROR_NONE;
   tbm_surface_h dst_surface = NULL;
   Eina_Bool ret = EINA_FALSE;
   E_EomOutputPpPtr eom_pp = NULL;
   E_EomPpDataPtr ppdata = NULL;

   if (g_eom->main_output_state == EINA_FALSE)
     return;

   eom_pp = eom_output->pp_overlay;

   if (!eom_pp || !eom_pp->pp || !eom_pp->queue)
     {
        EOMER("no pp data");
        _e_eom_buffer_destroy(eom_buff);
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

        eom_output->pending_pp_overlay = eina_list_append(eom_output->pending_pp_overlay, ppdata);

        return;
     }

   tdm_err = tbm_surface_queue_dequeue(eom_pp->queue, &dst_surface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

   if (eom_trace_debug)
     EOMDB("============================>  presentation PP  START   tbm_buff:%p ppdata:%p", dst_surface, ppdata);

   ret = _e_eom_pp_info_set(eom_output, src_surface, dst_surface);
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, error);

   eom_output->pp_overlay_data = eina_list_append(eom_output->pp_overlay_data, ppdata);

   tdm_err = tdm_pp_set_done_handler(eom_pp->pp, _e_eom_cb_pp_presentation_overlay, eom_output);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, error);

   tbm_surface_internal_ref(src_surface);
   tbm_surface_internal_ref(dst_surface);

   tdm_err = tdm_pp_attach(eom_pp->pp, src_surface, dst_surface);
   EINA_SAFETY_ON_FALSE_GOTO(tdm_err == TDM_ERROR_NONE, attach_fail);

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
   eom_output->pp_overlay_converting = EINA_FALSE;

attach_fail:
   tbm_surface_internal_unref(dst_surface);
   tbm_surface_internal_unref(src_surface);

error:
   EOMER("failed run pp tdm error: %d", tdm_err);

   eom_output->pp_overlay_data = eina_list_remove(eom_output->pp_overlay_data, ppdata);

   if (dst_surface)
     {
        if (eom_trace_debug)
          EOMDB("============================>  presentation PP  ENDERR  tbm_buff:%p", dst_surface);
        tbm_surface_queue_release(eom_pp->queue, dst_surface);
     }

   _e_eom_buffer_destroy(eom_buff);

   E_FREE(ppdata);
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
   eina_list_free(eom_output->pending_overlay_buff);
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
   eina_list_free(eom_output->pending_pp_overlay);
   eom_output->pending_pp_overlay = NULL;
}

static E_EomOutputPtr
_e_eom_output_find(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL, eom_output_tmp = NULL;
   Eina_List *l;

   if (g_eom->outputs)
     {
        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output_tmp)
          {
             if (eom_output_tmp->output == output->toutput)
               eom_output = eom_output_tmp;
          }
     }

   return eom_output;
}

static E_EomOutputPtr
_e_eom_output_find_added_output(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL, eom_output_tmp = NULL;
   Eina_List *l;

   if (g_eom->added_outputs)
     {
        EINA_LIST_FOREACH(g_eom->added_outputs, l, eom_output_tmp)
          {
             if (eom_output_tmp->output == output->toutput)
               eom_output = eom_output_tmp;
          }
     }

   return eom_output;
}

static void
_e_eom_main_output_info_get()
{
   E_Output *output_primary = NULL;
   int w, h;

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN(output_primary);

   if (e_output_connected(output_primary))
     {
        e_output_size_get(output_primary, &w, &h);
        g_eom->width = w;
        g_eom->height = h;
        g_eom->main_output_state = EINA_TRUE;
     }
   else
     {
        g_eom->width = 0;
        g_eom->height = 0;
        g_eom->main_output_state = EINA_FALSE;
     }
}

static Eina_Bool
_e_eom_output_init(tdm_display *dpy)
{
   E_EomOutputPtr eom_output = NULL;
   int i, count;
   Eina_List *l;

   count = e_comp->e_comp_screen->num_outputs;

   g_eom->output_count = count - 1;
   EOMDB("external output count : %d", g_eom->output_count);

   /* skip main output id:0 */
   /* start from 1 */
   for (i = 1; i < count; i++)
     {
        eom_output = E_NEW(E_EomOutput, 1);
        EINA_SAFETY_ON_NULL_GOTO(eom_output, err);

        eom_output->id = i;
        eom_output->mode = EOM_OUTPUT_MODE_NONE;
        eom_output->connection = WL_EOM_STATUS_NONE;
        eom_output->eout = e_output_find_by_index(i);
        EINA_SAFETY_ON_NULL_GOTO(eom_output->eout, err);

        eom_output->output = eom_output->eout->toutput;
        eom_output->type = (eom_output_type_e)eom_output->eout->toutput_type;

        if (!e_output_connected(eom_output->eout))
          {
             EOMDB("create(%d)output, type:%d, status:%d",
                   eom_output->id, eom_output->type, eom_output->connection_status);
             g_eom->outputs = eina_list_append(g_eom->outputs, eom_output);
             eom_output->connection_status = EINA_FALSE;
             continue;
          }

        eom_output->connection_status = EINA_TRUE;
        eom_output->phys_width = eom_output->eout->info.size.w;
        eom_output->phys_height = eom_output->eout->info.size.h;

        EOMDB("create(%d)output, type:%d, status:%d, w:%d, h:%d, mm_w:%d, mm_h:%d",
              eom_output->id, eom_output->type, eom_output->connection_status,
              eom_output->width, eom_output->height, eom_output->phys_width, eom_output->phys_height);

        g_eom->outputs = eina_list_append(g_eom->outputs, eom_output);
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
_e_eom_boot_connection_check(void *data)
{
   E_EomOutputPtr eom_output;
   E_Output *eout = NULL;
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

             eout = eom_output->eout;

             if (!e_output_connected(eout)) continue;

             e_output_external_update(eout);
          }
     }
   g_eom->timer = NULL;
   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_eom_presentation_check(void *data)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;

   if (!data) return ECORE_CALLBACK_CANCEL;

   eom_output = (E_EomOutputPtr)data;

   eom_output->delay_timer = NULL;

   voutput = eom_output->voutput;
   if (!voutput) return ECORE_CALLBACK_CANCEL;

   if (voutput->state == WAIT_PRESENTATION)
     e_output_external_set(eom_output->eout, E_OUTPUT_EXT_MIRROR);

   return ECORE_CALLBACK_CANCEL;
}

static Eina_Bool
_e_eom_virtual_output_set(E_EomOutput *eom_output)
{
   E_EomVirtualOutputPtr voutput = NULL;
   Eina_List *l;

   if (eom_output->voutput)
     return EINA_TRUE;

   EINA_LIST_FOREACH(g_eom->virtual_outputs, l, voutput)
     {
        if (voutput->eom_output == NULL)
          {
             voutput->eom_output = eom_output;
             voutput->connection = eom_output->connection;

             eom_output->voutput = voutput;
             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static void
_e_eom_virtual_output_unset(E_EomOutput *eom_output)
{
   E_EomVirtualOutputPtr voutput = NULL;

   if (!eom_output->voutput)
     return;

   voutput = eom_output->voutput;
   eom_output->voutput = NULL;

   voutput->eom_output = NULL;
   voutput->connection = eom_output->connection;
}

/* currently use only one virtual output */
static Eina_Bool
_e_eom_virtual_output_init()
{
   E_EomVirtualOutputPtr voutput = NULL;

   voutput = E_NEW(E_EomVirtualOutput, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(voutput, EINA_FALSE);

   voutput->id = 1;
   voutput->mode = EOM_OUTPUT_MODE_NONE;
   voutput->connection = WL_EOM_STATUS_NONE;
   voutput->eom_output = NULL;
   voutput->type = EOM_OUTPUT_TYPE_UNKNOWN;
   voutput->state = NONE;
   voutput->connection_status = EINA_FALSE;

   EOMDB("create(%d) virtual output, type:%d, status:%d",
              voutput->id, voutput->type, voutput->connection_status);
   g_eom->virtual_outputs = eina_list_append(g_eom->virtual_outputs, voutput);

   g_eom->virtual_output_count = 1;

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_init_internal()
{
   g_eom->dpy = e_comp->e_comp_screen->tdisplay;
   EINA_SAFETY_ON_NULL_GOTO(g_eom->dpy, err);

   g_eom->bufmgr = e_comp->e_comp_screen->bufmgr;
   EINA_SAFETY_ON_NULL_GOTO(g_eom->bufmgr, err);

   _e_eom_main_output_info_get();

   if (_e_eom_output_init(g_eom->dpy) != EINA_TRUE)
     {
        EOMER("_e_eom_output_init fail");
        goto err;
     }

   if (!_e_eom_virtual_output_init())
     {
        E_EomOutputPtr eom_output = NULL;
        Eina_List *l;

        EINA_LIST_FOREACH(g_eom->outputs, l, eom_output)
          free(eom_output);

        eina_list_free(g_eom->outputs);

        g_eom->outputs = NULL;

        goto err;
     }

   g_eom->added_outputs = NULL;

   g_eom->timer = ecore_timer_add(EOM_CONNECT_CHECK_TIMEOUT, _e_eom_boot_connection_check, NULL);

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
   E_EomVirtualOutputPtr voutput = NULL;

   if (g_eom == NULL) return;

   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
          ecore_event_handler_del(h);

        g_eom->handlers = NULL;
     }

   if (g_eom->virtual_outputs)
     {
        EINA_LIST_FOREACH(g_eom->virtual_outputs, l, voutput)
          free(voutput);

        eina_list_free(g_eom->virtual_outputs);

        g_eom->virtual_outputs = NULL;
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

static E_EomVirtualOutputPtr
_e_eom_virtual_output_get_by_id(int id)
{
   Eina_List *l;
   E_EomVirtualOutputPtr output;

   EINA_LIST_FOREACH(g_eom->virtual_outputs, l, output)
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
_e_eom_cb_wl_eom_client_destroy(struct wl_resource *resource)
{
   E_EomClientPtr client = NULL, iterator = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_Output *output = NULL;
   E_Plane *ep = NULL;
   Eina_List *l = NULL;

   EOMDB("=======================>  CLIENT DESTROY");

   EINA_SAFETY_ON_NULL_RETURN(resource);

   client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(client);

   g_eom->clients = eina_list_remove(g_eom->clients, client);

   if (client->current == EINA_FALSE)
     goto end2;

   voutput = _e_eom_virtual_output_get_by_id(client->output_id);
   EINA_SAFETY_ON_NULL_GOTO(voutput, end2);

   _e_eom_output_state_set_attribute(voutput, EOM_OUTPUT_ATTRIBUTE_NONE);

   if (voutput->state == NONE)
     goto end;

   if (voutput->state == WAIT_PRESENTATION)
     {
        voutput->state = NONE;
        goto end;
     }

   eom_output = voutput->eom_output;
   EINA_SAFETY_ON_NULL_GOTO(eom_output, end2);

   output = eom_output->eout;

   if (eom_output->overlay_layer)
     {
        tdm_error err = TDM_ERROR_NONE;

        err = tdm_layer_unset_buffer(eom_output->overlay_layer);
        if (err != TDM_ERROR_NONE)
          EOMER("fail unset buffer:%d", err);

        err = tdm_layer_commit(eom_output->overlay_layer, NULL, eom_output);
        if (err != TDM_ERROR_NONE)
          EOMER("fail commit on deleting output err:%d", err);
     }
   _e_eom_output_overlay_buff_release(eom_output);

   if (!eom_output->pp_overlay_converting)
     _e_eom_pp_deinit(eom_output);
   else
     eom_output->pp_overlay_deinit = EINA_TRUE;

   if (e_output_connected(output))
     {
        EOMDB("Start Mirroring");
        e_output_external_set(output, E_OUTPUT_EXT_MIRROR);
        voutput->state = MIRROR;

        ep = e_output_default_fb_target_get(eom_output->eout);
        if (ep->prepare_ec)
          {
             e_plane_ec_prepare_set(ep, NULL);
             e_plane_ec_set(ep, NULL);
          }
     }
end:
   /* Notify eom clients which are binded to a concrete output that the
    * state and mode of the output has been changed */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator != client && iterator->output_id == voutput->id)
          {
             wl_eom_send_output_attribute(iterator->resource, voutput->id,
                                          _e_eom_output_state_get_attribute(voutput),
                                          _e_eom_output_state_get_attribute_state(voutput),
                                          EOM_OUTPUT_MODE_NONE);

             wl_eom_send_output_mode(iterator->resource, voutput->id,
                                     _e_eom_virtual_output_state_get_mode(voutput));
          }
     }

end2:
   free(client);
}

static Eina_Bool
_e_eom_mirror_start(E_EomVirtualOutput *voutput, E_EomClient *eom_client)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomClientPtr iterator = NULL;
   E_Output *output = NULL;
   E_Plane *ep = NULL;
   Eina_List *l;

   eom_output = voutput->eom_output;

   eom_client->current = EINA_FALSE;

   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);
   _e_eom_virtual_output_state_set_mode(voutput, EOM_OUTPUT_MODE_NONE);

   output = eom_output->eout;

   if (e_output_connected(output))
    {
       EOMDB("Start Mirroring");
       e_output_external_set(output, E_OUTPUT_EXT_MIRROR);
       voutput->state = MIRROR;

       ep = e_output_default_fb_target_get(output);

       if (ep->prepare_ec)
         {
            e_plane_ec_prepare_set(ep, NULL);
            e_plane_ec_set(ep, NULL);
         }

       if (eom_output->overlay_layer)
         {
            tdm_error err = TDM_ERROR_NONE;

            err = tdm_layer_unset_buffer(eom_output->overlay_layer);
            if (err != TDM_ERROR_NONE)
              EOMER("fail unset buffer:%d", err);

            err = tdm_layer_commit(eom_output->overlay_layer, NULL, eom_output);
            if (err != TDM_ERROR_NONE)
              EOMER("fail commit on deleting output err:%d", err);
         }
       _e_eom_output_overlay_buff_release(eom_output);

       if (!eom_output->pp_overlay_converting)
         _e_eom_pp_deinit(eom_output);
       else
         eom_output->pp_overlay_deinit = EINA_TRUE;
    }
  /* If mirror mode has been run notify all clients about that */
  if (eom_trace_debug)
    EOMDB("client set NONE attribute, send new info to previous current client");
  EINA_LIST_FOREACH(g_eom->clients, l, iterator)
    {
       if (iterator && iterator->output_id == voutput->id)
         {
            wl_eom_send_output_attribute(iterator->resource, voutput->id,
                                         _e_eom_output_state_get_attribute(voutput),
                                         _e_eom_output_state_get_attribute_state(voutput),
                                         EOM_ERROR_NONE);

            wl_eom_send_output_mode(iterator->resource, voutput->id,
                                    _e_eom_virtual_output_state_get_mode(voutput));
         }
    }

  return EINA_TRUE;
}

static void
_e_eom_cb_wl_request_set_attribute_result_send(E_EomVirtualOutput *voutput, E_EomClient *eom_client)
{
   E_EomClientPtr current_eom_client = NULL;

   /* Send changes to the caller-client */
   wl_eom_send_output_attribute(eom_client->resource, voutput->id,
                                _e_eom_output_state_get_attribute(voutput),
                                _e_eom_output_state_get_attribute_state(voutput),
                                EOM_ERROR_NONE);

   current_eom_client = _e_eom_client_get_current_by_id(voutput->id);
   EOMDB("Substitute current client: new:%p, old:%p", eom_client, current_eom_client);

   /* Send changes to previous current client */
   if (eom_client->current == EINA_FALSE && current_eom_client)
     {
        E_EomOutputPtr eom_output = NULL;
        E_Output *output = NULL;
        E_Plane *ep = NULL;

        EOMDB("Send changes to previous current client");

        wl_eom_send_output_attribute(current_eom_client->resource, voutput->id,
                                     _e_eom_output_state_get_attribute(voutput),
                                     EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                     EOM_ERROR_NONE);

        current_eom_client->current = EINA_FALSE;

        if (voutput->eom_output == NULL) goto end;

        eom_output = voutput->eom_output;
        output = eom_output->eout;

        if (e_output_connected(output))
          {
             EOMDB("Start Mirroring");
             e_output_external_set(output, E_OUTPUT_EXT_MIRROR);
             voutput->state = MIRROR;

             ep = e_output_default_fb_target_get(eom_output->eout);

             if (ep->prepare_ec)
               {
                  e_plane_ec_prepare_set(ep, NULL);
                  e_plane_ec_set(ep, NULL);
               }

             if (eom_output->overlay_layer)
               {
                  tdm_error err = TDM_ERROR_NONE;

                  err = tdm_layer_unset_buffer(eom_output->overlay_layer);
                  if (err != TDM_ERROR_NONE)
                    EOMER("fail unset buffer:%d", err);

                  err = tdm_layer_commit(eom_output->overlay_layer, NULL, eom_output);
                  if (err != TDM_ERROR_NONE)
                    EOMER("fail commit on deleting output err:%d", err);
               }
             _e_eom_output_overlay_buff_release(eom_output);

             if (!eom_output->pp_overlay_converting)
               _e_eom_pp_deinit(eom_output);
             else
               eom_output->pp_overlay_deinit = EINA_TRUE;
          }
     }
end:
   /* Set the client as current client of the eom_output */
   eom_client->current = EINA_TRUE;

   if (voutput->connection_status == EINA_FALSE)
     voutput->state = WAIT_PRESENTATION;
   else
     {
        E_EomOutputPtr eom_output = NULL;

        if (voutput->eom_output)
          {
             eom_output = voutput->eom_output;
             if (eom_output->delay_timer)
               ecore_timer_del(eom_output->delay_timer);
             eom_output->delay_timer = ecore_timer_add(EOM_DELAY_CHECK_TIMEOUT, _e_eom_presentation_check, eom_output);
          }
     }

}

static void
_e_eom_cb_wl_request_set_attribute(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   eom_error_e eom_error = EOM_ERROR_NONE;
   E_EomClientPtr eom_client = NULL;//, current_eom_client = NULL, iterator = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   Eina_Bool ret = EINA_FALSE;

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   /* Bind the client with a concrete output */
   eom_client->output_id = output_id;

   voutput = _e_eom_virtual_output_get_by_id(output_id);
   EINA_SAFETY_ON_NULL_GOTO(voutput, no_output);

   EOMDB("Set attribute:%d, client:%p", attribute, eom_client);

   if (eom_client->current == EINA_TRUE && voutput->id == eom_client->output_id)
     {
        /* Current client can set any flag it wants */
        _e_eom_output_state_set_force_attribute(voutput, attribute);
     }
   else if (voutput->id == eom_client->output_id)
     {
        /* A client is trying to set new attribute */
        ret = _e_eom_output_state_set_attribute(voutput, attribute);
        if (ret == EINA_FALSE)
          {
             EOMDB("client failed to set attribute");
             eom_error = EOM_ERROR_INVALID_PARAMETER;
             wl_eom_send_output_attribute(eom_client->resource, voutput->id,
                                          _e_eom_output_state_get_attribute(voutput),
                                          EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                          eom_error);
             return;
          }
     }
   else
     return;

   eom_output = voutput->eom_output;
   if (eom_output)
     {
        if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE && voutput->state != MIRROR)
          {
             if (!_e_eom_mirror_start(voutput, eom_client))
               {
                  EOMDB("mirror start FAILED");
                  return;
               }

             wl_eom_send_output_attribute(eom_client->resource, voutput->id,
                                          _e_eom_output_state_get_attribute(voutput),
                                          EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                          eom_error);
             return;
          }
     }

   _e_eom_cb_wl_request_set_attribute_result_send(voutput, eom_client);

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
_e_eom_send_configure_event()
{
   E_EomOutput *eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_EomClientPtr eom_client = NULL;
   E_Client *ec = NULL;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;
   E_Comp_Client_Data *cdata = NULL;
   E_Plane *ep = NULL;

   EINA_LIST_FOREACH(g_eom->clients, l, eom_client)
     {
        if (eom_client->current == EINA_TRUE)
          {
             EINA_SAFETY_ON_NULL_RETURN(eom_client->ec);

             ec = eom_client->ec;

             cdata = ec->comp_data;
             EINA_SAFETY_ON_NULL_RETURN(cdata);
             EINA_SAFETY_ON_NULL_RETURN(cdata->shell.configure_send);

             voutput = _e_eom_virtual_output_get_by_id(eom_client->output_id);
             if (voutput == NULL)
               {
                  EOMER("no voutput error\n");
                  return;
               }

             eom_output = voutput->eom_output;
             if (eom_output == NULL)
               {
                  EOMER("no eom_output error\n");
                  return;
               }

             EOMDB("e_comp_object_redirected_set (ec:%p)(ec->frame:%p)\n", ec, ec->frame);
             ret = _e_eom_util_add_comp_object_redirected_hook(eom_client->ec);
             EINA_SAFETY_ON_FALSE_RETURN(ret == EINA_TRUE);

             cdata->shell.configure_send(ec->comp_data->shell.surface, 0, eom_output->width, eom_output->height);

             ep = e_output_default_fb_target_get(eom_output->eout);
             e_plane_ec_prepare_set(ep, ec);

             return;
          }
     }
}

static void
_e_eom_window_set_internal(struct wl_resource *resource, int output_id, E_Client *ec)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_EomClientPtr eom_client = NULL;
   E_Comp_Client_Data *cdata = NULL;
   Eina_Bool ret = EINA_FALSE;
   E_Plane *ep = NULL;

   if (!resource || output_id <= 0 || !ec || !ec->comp_data || e_object_is_del(E_OBJECT(ec)))
     return;

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   voutput = _e_eom_virtual_output_get_by_id(output_id);
   if (voutput == NULL)
     {
        wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_NO_OUTPUT);
        EOMER("no voutput error\n");
        return;
     }

   if (!eom_client->current)
     {
        wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_OUTPUT_OCCUPIED);
        return;
     }

   ret = _e_eom_util_add_comp_object_redirected_hook(ec);
   EINA_SAFETY_ON_FALSE_RETURN(ret == EINA_TRUE);

   EOMDB("e_comp_object_redirected_set (ec:%p)(ec->frame:%p)\n", ec, ec->frame);

   /* ec is used in buffer_change callback for distinguishing external ec and its buffers */
   eom_client->ec = ec;

   eom_output = voutput->eom_output;

   /* Send reconfigure event to a client which will resize its window to
    * external output resolution in respond */
   if (eom_output != NULL)
     {
        cdata = ec->comp_data;
        EINA_SAFETY_ON_NULL_RETURN(cdata);
        EINA_SAFETY_ON_NULL_RETURN(cdata->shell.configure_send);

        cdata->shell.configure_send(ec->comp_data->shell.surface, 0, eom_output->width, eom_output->height);

        ep = e_output_default_fb_target_get(eom_output->eout);
        e_plane_ec_prepare_set(ep, ec);
     }

   wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_NONE);
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

   if (g_eom->virtual_outputs)
     {
        Eina_List *l;
        E_EomOutputPtr output = NULL;
        E_EomVirtualOutputPtr voutput = NULL;

        EINA_LIST_FOREACH(g_eom->virtual_outputs, l, voutput)
          {
             if (voutput->id == output_id)
               {
                  if (voutput->eom_output)
                    {
                       output = voutput->eom_output;

                       EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                              voutput->id, output->type, output->mode, output->width, output->height,
                              output->phys_width, output->phys_height, output->connection_status);

                        wl_eom_send_output_info(resource, voutput->id, output->type, output->mode, output->width, output->height,
                                                output->phys_width, output->phys_height, output->connection,
                                                1, 0, 0, 0);
                    }
                  else
                    {
                        EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                              voutput->id, voutput->type, voutput->mode, voutput->width, voutput->height,
                              voutput->phys_width, voutput->phys_height, voutput->connection_status);

                        wl_eom_send_output_info(resource, voutput->id, voutput->type, voutput->mode, voutput->width, voutput->height,
                                                voutput->phys_width, voutput->phys_height, voutput->connection,
                                                1, 0, 0, 0);
                    }
               }
          }
     }
}

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_cb_wl_request_set_attribute,
   _e_eom_cb_wl_request_set_shell_window,
   _e_eom_cb_wl_request_get_output_info
};

static void
_e_eom_cb_wl_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource = NULL;
   E_EomClientPtr new_client = NULL;
   E_EomPtr eom = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   Eina_List *l;
   E_EomOutputPtr output = NULL;

   EINA_SAFETY_ON_NULL_RETURN(data);
   eom = data;

   resource = wl_resource_create(client, &wl_eom_interface, MIN(version, 1), id);
   if (resource == NULL)
     {
        EOMER("resource is null. (version :%d, id:%d)", version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource, &_e_eom_wl_implementation, eom, _e_eom_cb_wl_eom_client_destroy);

   wl_eom_send_output_count(resource, g_eom->virtual_output_count);

   if (g_eom->virtual_outputs)
     {
        EINA_LIST_FOREACH(g_eom->virtual_outputs, l, voutput)
          {
             if (voutput->eom_output)
               {
                  output = voutput->eom_output;

                  EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                         voutput->id, output->type, output->mode, output->width, output->height,
                         output->phys_width, output->phys_height, output->connection_status);

                   wl_eom_send_output_info(resource, voutput->id, output->type, output->mode, output->width, output->height,
                                           output->phys_width, output->phys_height, output->connection,
                                           1, 0, 0, 0);
               }
             else
               {
                   EOMDB("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d",
                         voutput->id, voutput->type, voutput->mode, voutput->width, voutput->height,
                         voutput->phys_width, voutput->phys_height, voutput->connection_status);

                   wl_eom_send_output_info(resource, voutput->id, voutput->type, voutput->mode, voutput->width, voutput->height,
                                           voutput->phys_width, voutput->phys_height, voutput->connection,
                                           1, 0, 0, 0);
               }
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
   E_EomVirtualOutputPtr voutput = NULL;
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   tbm_surface_h tbm_buffer = NULL;
   Eina_List *l;
   Eina_Bool overlay = EINA_FALSE;
   int width, height;

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

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->pixmap, ECORE_CALLBACK_PASS_ON);

   wl_buffer = e_pixmap_resource_get(ec->pixmap);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_buffer, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(wl_buffer->resource, ECORE_CALLBACK_PASS_ON);

   /* TODO: support different SHMEM buffers etc. */
   tbm_buffer = wayland_tbm_server_get_surface(e_comp->wl_comp_data->tbm.server, wl_buffer->resource);
   EINA_SAFETY_ON_NULL_RETURN_VAL(tbm_buffer, ECORE_CALLBACK_PASS_ON);

   width = tbm_surface_get_width(tbm_buffer);
   height = tbm_surface_get_height(tbm_buffer);

   if ((width <= 1) || (height <= 1))
     return ECORE_CALLBACK_PASS_ON;

   voutput = _e_eom_virtual_output_get_by_id(eom_client->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(voutput, ECORE_CALLBACK_PASS_ON);

   eom_output = voutput->eom_output;
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, ECORE_CALLBACK_PASS_ON);

   if (eom_trace_debug)
     EOMDB("===============>  EXT START");

   if (eom_output->delay_timer)
     ecore_timer_del(eom_output->delay_timer);
   eom_output->delay_timer = NULL;

   e_output_external_set(eom_output->eout, E_OUTPUT_EXT_PRESENTATION);

   /* TODO: It works but maybe there is better solution exists ?
    * Also I do not know how it affects on performance */
   if (ec->map_timer)
     {
        if (eom_trace_debug)
          EOMDB("delete map_timer");
        E_FREE_FUNC(ec->map_timer, ecore_timer_del);
     }

   if (eom_trace_debug)
     EOMDB("buffer_changed callback ec:%p, overlay:%d", ec, overlay);

   if (overlay)
     {
        Eina_Bool video_layer = EINA_FALSE;
        tbm_format format;
        Eina_Bool need_pp = EINA_FALSE;

        E_EomBufferPtr eom_buff = _e_eom_buffer_create(wl_buffer);
        EINA_SAFETY_ON_NULL_RETURN_VAL(eom_buff, ECORE_CALLBACK_PASS_ON);

        format = tbm_surface_get_format(tbm_buffer);
        video_layer = _e_eom_output_video_layer_find(eom_output, format);
        if (!video_layer)
          {
             /* need pp */
             need_pp = EINA_TRUE;
             eom_output->need_overlay_pp = EINA_TRUE;
             if (!_e_eom_pp_init(eom_output))
               {
                  EOMER("pp_init for overlay fail");
                  _e_eom_buffer_destroy(eom_buff);
                  return ECORE_CALLBACK_PASS_ON;
               }
          }

        if (voutput->state != PRESENTATION)
          {
             _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_PRESENTATION);
             _e_eom_virtual_output_state_set_mode(voutput, EOM_OUTPUT_MODE_PRESENTATION);

             EINA_LIST_FOREACH(g_eom->clients, l, eom_client_itr)
               {
                  if (eom_client_itr->output_id == voutput->id)
                    wl_eom_send_output_mode(eom_client_itr->resource, voutput->id,
                                            _e_eom_virtual_output_state_get_mode(voutput));
               }

             voutput->state = PRESENTATION;
          }

        if (need_pp)
          {
             if (eom_trace_debug)
               EOMDB("run _e_eom_presentation_pp_run");
             _e_eom_presentation_pp_run(eom_output, tbm_buffer, eom_buff);
          }
        else
          {
             if (eom_trace_debug)
               EOMDB("run direct show");
             _e_eom_layer_overlay_set(eom_output, tbm_buffer);

             if (!_e_eom_output_show(eom_output, tbm_buffer, _e_eom_tbm_buffer_release_ext_mod, eom_buff))
               {
                  if (eom_trace_debug)
                    {
                       EOMDB("===============>  EXT ENDERR  tbm_buff:%p", tbm_buffer);
                       EOMDB("_e_eom_add_buff_to_show fail tbm_buff:%p", tbm_buffer);
                    }
                  _e_eom_buffer_destroy(eom_buff);
                  return ECORE_CALLBACK_PASS_ON;
               }
          }
     }
   else
     {
        E_Plane *ep = NULL;

        ep = e_output_default_fb_target_get(eom_output->eout);

        if (ep->prepare_ec)
          e_plane_ec_set(ep, ec);

        if (voutput->state != PRESENTATION)
          {
             _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_PRESENTATION);
             _e_eom_virtual_output_state_set_mode(voutput, EOM_OUTPUT_MODE_PRESENTATION);

             EINA_LIST_FOREACH(g_eom->clients, l, eom_client_itr)
               {
                  if (eom_client_itr->output_id == voutput->id)
                    wl_eom_send_output_mode(eom_client_itr->resource, voutput->id,
                                            _e_eom_virtual_output_state_get_mode(voutput));
               }
             voutput->state = PRESENTATION;
          }

        e_comp_object_hwc_update_set(ec->frame, EINA_TRUE);
     }

   if (eom_trace_debug)
        EOMDB("===============<  EXT START");

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_eom_init()
{
   Eina_Bool ret = EINA_FALSE;

   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   if (e_comp->e_comp_screen->num_outputs < 1)
     return EINA_TRUE;

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, EINA_FALSE);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp, &wl_eom_interface, 1, g_eom, _e_eom_cb_wl_bind);
   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   g_eom->angle = 0;
   g_eom->rotate_state = ROTATE_NONE;

   ret = _e_eom_init_internal();
   EINA_SAFETY_ON_FALSE_GOTO(ret == EINA_TRUE, err);

   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_BUFFER_CHANGE, _e_eom_cb_client_buffer_change, NULL);

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

E_API Eina_Bool
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

EINTERN Eina_Bool
e_eom_connect(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_EomClientPtr iterator = NULL;
   Eina_List *l;

   if (!g_eom) return EINA_TRUE;

   g_eom->check_first_boot = 1;

   eom_output = _e_eom_output_find(output);
   if (eom_output == NULL)
     {
        eom_output = _e_eom_output_find_added_output(output);
        if (!eom_output)
          {
             EOMER("cannot find output");
             return EINA_FALSE;
          }
     }

   if (eom_output->connection_status == EINA_TRUE)
     return EINA_TRUE;

   /* update eom_output connect */
   eom_output->width = output->config.mode.w;
   eom_output->height = output->config.mode.h;
   eom_output->phys_width = output->info.size.w;
   eom_output->phys_height = output->info.size.h;
   eom_output->name = eina_stringshare_add(output->id);
   eom_output->connection_status = EINA_TRUE;

   EOMDB("Setup new output: %s (%dx%d)", eom_output->name, eom_output->width, eom_output->height);

   if (!_e_eom_virtual_output_set(eom_output))
     {
        EOMDB("No virtual output.");
        return EINA_TRUE;
     }
   voutput = eom_output->voutput;

   /* TODO: check output mode(presentation set) and HDMI type */
   if (voutput->state == WAIT_PRESENTATION)
     {
        EOMDB("Start wait Presentation");

        _e_eom_send_configure_event();

        if (eom_output->delay_timer)
          ecore_timer_del(eom_output->delay_timer);
        eom_output->delay_timer = ecore_timer_add(EOM_DELAY_CONNECT_CHECK_TIMEOUT, _e_eom_presentation_check, eom_output);
     }
   else
     {
        EOMDB("Start Mirroring");

        _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_MIRROR);
        _e_eom_virtual_output_state_set_mode(voutput, EOM_OUTPUT_MODE_MIRROR);
        voutput->state = MIRROR;

        e_output_external_set(output, E_OUTPUT_EXT_MIRROR);
     }

   eom_output->connection = WL_EOM_STATUS_CONNECTION;

   /* If there were previously connected clients to the output - notify them */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator->resource)
          {
             EOMDB("Send output connected notification to client: %p", iterator);

             if (iterator->current)
               wl_eom_send_output_info(iterator->resource, voutput->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       0,
                                       _e_eom_output_state_get_attribute(voutput),
                                       EOM_OUTPUT_ATTRIBUTE_STATE_ACTIVE,
                                       EOM_ERROR_NONE);
             else
               wl_eom_send_output_info(iterator->resource, voutput->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       1, 0, 0, 0);
          }
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_eom_disconnect(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_EomClientPtr iterator = NULL;
   Eina_List *l;

   if (!g_eom) return EINA_TRUE;

   g_eom->check_first_boot = 1;

   eom_output = _e_eom_output_find(output);
   if (eom_output == NULL)
     {
        eom_output = _e_eom_output_find_added_output(output);
        if (!eom_output)
          {
             EOMER("cannot find output");
             return EINA_FALSE;
          }
     }

   if (eom_output->connection_status == EINA_FALSE)
     return EINA_TRUE;

   if (eom_output->delay_timer)
     ecore_timer_del(eom_output->delay_timer);
   eom_output->delay_timer = NULL;

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

   if (eom_output->voutput == NULL)
     return EINA_TRUE;
   voutput = eom_output->voutput;

   e_output_external_unset(output);

   eom_output->connection_status = EINA_FALSE;

   _e_eom_output_state_set_mode(eom_output, EOM_OUTPUT_MODE_NONE);

   if (_e_eom_client_get_current_by_id(eom_output->id))
     voutput->state = WAIT_PRESENTATION;
   else
     voutput->state = NONE;

   /* If there were previously connected clients to the output - notify them */
   EINA_LIST_FOREACH(g_eom->clients, l, iterator)
     {
        if (iterator && iterator->resource)
          {
             EOMDB("Send output disconnected notification to client: %p", iterator);

             if (iterator->current)
               wl_eom_send_output_info(iterator->resource, voutput->id,
                                       eom_output->type, eom_output->mode,
                                       eom_output->width, eom_output->height,
                                       eom_output->phys_width, eom_output->phys_height,
                                       eom_output->connection,
                                       0,
                                       _e_eom_output_state_get_attribute(voutput),
                                       EOM_OUTPUT_ATTRIBUTE_STATE_INACTIVE,
                                       EOM_ERROR_NONE);
             else
               wl_eom_send_output_info(iterator->resource, voutput->id,
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

   _e_eom_virtual_output_unset(eom_output);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_eom_create(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;

   if (!g_eom) return EINA_TRUE;

   eom_output = E_NEW(E_EomOutput, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, EINA_FALSE);

   eom_output->id = output->index;
   eom_output->mode = EOM_OUTPUT_MODE_NONE;
   eom_output->connection = WL_EOM_STATUS_NONE;
   eom_output->eout = output;
   EINA_SAFETY_ON_NULL_GOTO(eom_output->eout, err);

   eom_output->output = eom_output->eout->toutput;
   eom_output->type = (eom_output_type_e)eom_output->eout->toutput_type;

   eom_output->connection_status = EINA_FALSE;
   eom_output->width = 0;
   eom_output->height = 0;
   eom_output->phys_width = 0;
   eom_output->phys_height = 0;

   EOMDB("create (%d)output, type:%d, name:%s",
               eom_output->id, eom_output->type, eom_output->name);

   g_eom->added_outputs = eina_list_append(g_eom->added_outputs, eom_output);

   return EINA_TRUE;

err:
   E_FREE(eom_output);

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_eom_destroy(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomOutputPtr eom_output_delete = NULL;
   Eina_List *l;

   if (!g_eom) return EINA_TRUE;

   EINA_LIST_FOREACH(g_eom->added_outputs, l, eom_output)
     {
        if (eom_output && eom_output->eout == output)
          {
             eom_output_delete = eom_output;
             break;
          }
     }

   if (!eom_output_delete) return EINA_FALSE;

   EOMDB("destroy (%d)output, type:%d, name:%s",
               eom_output->id, eom_output->type, eom_output->name);

   g_eom->added_outputs = eina_list_remove(g_eom->added_outputs, eom_output_delete);

   E_FREE(eom_output_delete);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_eom_mode_change(E_Output *output, E_Output_Mode *emode)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomVirtualOutputPtr voutput = NULL;
   E_Output *output_primary = NULL;

   if (!g_eom) return EINA_TRUE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(output, EINA_FALSE);

   output_primary = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(output_primary, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(output_primary == output, EINA_FALSE);

   eom_output = _e_eom_output_find(output);
   if (eom_output == NULL)
     {
        eom_output = _e_eom_output_find_added_output(output);
        if (!eom_output)
          {
             EOMER("cannot find output");
             return EINA_FALSE;
          }
     }

   if (eom_output->connection_status == EINA_FALSE)
     return EINA_FALSE;

   if (eom_output->voutput == NULL)
     {
        eom_output->width = output->config.mode.w;
        eom_output->height = output->config.mode.h;

        EOMDB("mode change output: %s (%dx%d)", eom_output->name, eom_output->width, eom_output->height);

        return EINA_TRUE;
     }
   voutput = eom_output->voutput;

   if (eom_output->delay_timer)
     ecore_timer_del(eom_output->delay_timer);
   eom_output->delay_timer = NULL;

   if (g_eom->rotate_output == eom_output)
     {
        if (g_eom->rotate_timer)
          ecore_timer_del(g_eom->rotate_timer);
        g_eom->rotate_timer = NULL;
        g_eom->rotate_output = NULL;
     }

   /* update eom_output connect */
   eom_output->width = output->config.mode.w;
   eom_output->height = output->config.mode.h;
   eom_output->phys_width = output->info.size.w;
   eom_output->phys_height = output->info.size.h;
   eom_output->name = eina_stringshare_add(output->id);
   eom_output->connection_status = EINA_TRUE;

   EOMDB("mode change output: %s (%dx%d)", eom_output->name, eom_output->width, eom_output->height);
   if (voutput->state == PRESENTATION)
     {
        voutput->state = WAIT_PRESENTATION;
        _e_eom_send_configure_event();

        eom_output->delay_timer = ecore_timer_add(EOM_DELAY_CONNECT_CHECK_TIMEOUT, _e_eom_presentation_check, eom_output);
     }

   return EINA_TRUE;
}
