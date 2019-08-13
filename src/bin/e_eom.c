#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "e.h"
#include <xdg-shell-unstable-v5-server-protocol.h>
#include <eom-server-protocol.h>
#include <eom.h>
#include <tbm_surface.h>
#include <wayland-tbm-server.h>
#ifdef FRAMES
#include <time.h>
#endif

static int eom_trace_debug = 0;

#define ALEN(array) (sizeof(array) / sizeof(array)[0])

#define EOM_NUM_ATTR 3
#define EOM_CONNECT_CHECK_TIMEOUT 4.0

#define EOERR(f, output, x...)                               \
   do                                                            \
     {                                                           \
        if (!output)                                         \
          ERR("EWL|%20.20s|              |             |%8s|"f,  \
              "EOM", "Unknown", ##x);                            \
        else                                                     \
          ERR("EWL|%20.20s|              |             |%8s|"f,  \
              "EOM", (e_output_output_id_get(output)), ##x);                   \
     }                                                           \
   while (0)

#define EOINF(f, output, x...)                               \
   do                                                            \
     {                                                           \
        if (!output)                                         \
          INF("EWL|%20.20s|              |             |%8s|"f,  \
              "EOM", "Unknown", ##x);                            \
        else                                                     \
          INF("EWL|%20.20s|              |             |%8s|"f,  \
              "EOM", (e_output_output_id_get(output)), ##x);                   \
     }                                                           \
   while (0)

typedef struct _E_Eom        E_Eom,       *E_EomPtr;
typedef struct _E_Eom_Client E_EomClient, *E_EomClientPtr;
typedef struct _E_Eom_Output E_EomOutput, *E_EomOutputPtr;
typedef struct _E_Eom_Comp_Object_Intercept_Hook_Data E_EomCompObjectInterceptHookData;

struct _E_Eom
{
   struct wl_global *global;

   unsigned int eom_output_count;
   Eina_List *eom_outputs;
   Eina_List *clients;
   Eina_List *handlers;
   Eina_List *hooks;
   Eina_List *comp_object_intercept_hooks;
   E_Output_Hook *output_connect_status_hook;
   E_Output_Hook *output_mode_changes_hook;
   E_Output_Hook *output_add_hook;
   E_Output_Hook *output_remove_hook;
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

struct _E_Eom_Output
{
   unsigned int id;
   eom_output_type_e type;
   eom_output_attribute_e attribute;
   enum wl_eom_status connection;

   E_Output *output;
   Eina_Bool added;

   E_EomClientPtr eom_client;
};

struct _E_Eom_Comp_Object_Intercept_Hook_Data
{
   E_Client *ec;
   E_EomOutput *eom_output;
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

   EOINF("Redirect ec:%p, ec->frame:%p", hook_data->eom_output->output, ec, ec->frame);

   e_comp_object_intercept_hook_del(hook_data->hook);
   g_eom->comp_object_intercept_hooks = eina_list_remove(g_eom->comp_object_intercept_hooks, hook_data);

   free(hook_data);

   return EINA_TRUE;
}

static inline eom_output_attribute_e
_e_eom_output_attribute_get(E_EomOutputPtr eom_output)
{
   if (eom_output == NULL) return EOM_OUTPUT_ATTRIBUTE_NONE;

   return eom_output->attribute;
}

static inline void
_e_eom_output_attribute_force_set(E_EomOutputPtr eom_output, eom_output_attribute_e attribute)
{
   if (eom_output == NULL) return;

   eom_output->attribute = attribute;
}

static inline Eina_Bool
_e_eom_output_attribute_set(E_EomOutputPtr eom_output, eom_output_attribute_e attribute)
{
   if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE || eom_output->attribute == EOM_OUTPUT_ATTRIBUTE_NONE)
     {
        eom_output->attribute = attribute;
        return EINA_TRUE;
     }

   if (eom_output_attributes[eom_output->attribute - 1][attribute - 1] == 1)
     {
        eom_output->attribute = attribute;
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static eom_output_mode_e
_e_eom_output_mode_get(E_Output_Display_Mode display_mode)
{
   switch (display_mode)
     {
      case E_OUTPUT_DISPLAY_MODE_NONE:
         return EOM_OUTPUT_MODE_NONE;
      case E_OUTPUT_DISPLAY_MODE_MIRROR:
         return EOM_OUTPUT_MODE_MIRROR;
      case E_OUTPUT_DISPLAY_MODE_PRESENTATION:
         return EOM_OUTPUT_MODE_PRESENTATION;
      default:
         break;
     }

   return EOM_OUTPUT_MODE_NONE;
}

static void
_e_eom_output_info_broadcast(E_EomOutputPtr eom_output, eom_output_attribute_state_e attribute_state)
{
   E_EomClientPtr eom_client = NULL;
   Eina_List *l = NULL;
   int w, h, pw, ph;

   /* get the output size */
   e_output_size_get(eom_output->output, &w, &h);
   e_output_phys_size_get(eom_output->output, &pw, &ph);

   /* If there were previously connected clients to the output - notify them */
   EINA_LIST_FOREACH(g_eom->clients, l, eom_client)
     {
        if (!eom_client) continue;
        if (!eom_client->resource) continue;

        if (attribute_state == EOM_OUTPUT_ATTRIBUTE_STATE_ACTIVE)
          EOINF("Send output connected notification to client: %p", eom_output->output, eom_client);
        if (attribute_state == EOM_OUTPUT_ATTRIBUTE_STATE_INACTIVE)
          EOINF("Send output disconnected notification to client: %p", eom_output->output, eom_client);

        if (eom_client->ec == e_output_presentation_ec_get(eom_output->output))
          wl_eom_send_output_info(eom_client->resource,
                                  eom_output->id,
                                  eom_output->type,
                                  e_output_display_mode_get(eom_output->output),
                                  w, h, pw, ph,
                                  eom_output->connection,
                                  0,
                                  _e_eom_output_attribute_get(eom_output),
                                  attribute_state,
                                  EOM_ERROR_NONE);
        else
          wl_eom_send_output_info(eom_client->resource,
                                  eom_output->id,
                                  eom_output->type,
                                  e_output_display_mode_get(eom_output->output),
                                  w, h, pw, ph,
                                  eom_output->connection,
                                  1, 0, 0, 0);
     }
}

static void
_e_eom_output_status_broadcast(E_EomOutputPtr eom_output, E_EomClientPtr except_client, eom_output_attribute_state_e attribute_state)
{
   E_EomClientPtr eom_client = NULL;
   Eina_List *l = NULL;
   E_Output_Display_Mode display_mode;

   EINA_LIST_FOREACH(g_eom->clients, l, eom_client)
     {
        if (!eom_client) continue;
        if (eom_client->output_id != eom_output->id) continue;
        if (eom_client == except_client) continue;

        wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                     _e_eom_output_attribute_get(eom_output),
                                     attribute_state,
                                     EOM_ERROR_NONE);

        display_mode = e_output_display_mode_get(eom_output->output);
        wl_eom_send_output_mode(eom_client->resource, eom_output->id, _e_eom_output_mode_get(display_mode));
     }
}

static E_EomOutputPtr
_e_eom_output_find(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(g_eom->eom_outputs, l, eom_output)
     {
       if (eom_output->output == output)
         return eom_output;
     }

   return NULL;
}

static Eina_Bool
_e_eom_output_create(E_Output *output, Eina_Bool added)
{
   E_EomOutputPtr eom_output = NULL;

   if (!g_eom) return EINA_TRUE;

   eom_output = E_NEW(E_EomOutput, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, EINA_FALSE);

   eom_output->id = output->index;
   eom_output->connection = WL_EOM_STATUS_NONE;
   eom_output->output = output;
   eom_output->type = (eom_output_type_e)output->toutput_type;
   eom_output->added = added;
   eom_output->eom_client = NULL;

   g_eom->eom_outputs = eina_list_append(g_eom->eom_outputs, eom_output);

   EOINF("create (%d)output, type:%d, added:%d", eom_output->output,
         eom_output->id, eom_output->type, eom_output->added);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_output_destroy(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;

   if (!g_eom) return EINA_TRUE;

   eom_output = _e_eom_output_find(output);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, EINA_FALSE);

   EOINF("destroy (%d)output, type:%d, added:%d", eom_output->output,
         eom_output->id, eom_output->type, eom_output->added);

   g_eom->eom_outputs = eina_list_remove(g_eom->eom_outputs, eom_output);

   E_FREE(eom_output);

   return EINA_TRUE;
}

static E_EomOutputPtr
_e_eom_output_get_by_id(int id)
{
   E_EomOutputPtr eom_output;
   Eina_List *l;

   EINA_LIST_FOREACH(g_eom->eom_outputs, l, eom_output)
     {
        if (eom_output && eom_output->id == id)
          return eom_output;
     }

   return NULL;
}

static E_EomOutputPtr
_e_eom_output_by_ec_child_get(E_Client *ec)
{
   E_EomOutputPtr eom_output = NULL;
   E_Client *parent_ec = NULL, *output_ec = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(g_eom->eom_outputs, l, eom_output)
     {
        output_ec = e_output_presentation_ec_get(eom_output->output);
        if (!output_ec) continue;

        if (output_ec == ec) return eom_output;

        if (!ec->comp_data) continue;
        if (!ec->comp_data->sub.data) continue;

        parent_ec = ec->comp_data->sub.data->parent;
        while (parent_ec)
          {
             if (parent_ec == output_ec) return eom_output;
             if (!parent_ec->comp_data) break;
             if (!parent_ec->comp_data->sub.data) break;

             parent_ec = parent_ec->comp_data->sub.data->parent;
          }
     }

   return NULL;
}

static void
_e_eom_output_send_configure_event(E_EomOutput *eom_output, E_Client *ec)
{
   E_Comp_Client_Data *cdata = NULL;
   E_EomCompObjectInterceptHookData *hook_data = NULL;
   E_Comp_Object_Intercept_Hook *hook = NULL;
   int w, h;

   ec = e_output_presentation_ec_get(eom_output->output);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   cdata = ec->comp_data;
   EINA_SAFETY_ON_NULL_RETURN(cdata);
   EINA_SAFETY_ON_NULL_RETURN(cdata->shell.configure_send);

   hook_data = E_NEW(E_EomCompObjectInterceptHookData, 1);
   EINA_SAFETY_ON_NULL_RETURN(hook_data);

   hook = e_comp_object_intercept_hook_add(E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER,
                                           _e_eom_cb_comp_object_redirected, hook_data);
   EINA_SAFETY_ON_NULL_GOTO(hook, err);

   hook_data->ec = ec;
   hook_data->eom_output = eom_output;
   hook_data->hook = hook;

   g_eom->comp_object_intercept_hooks = eina_list_append(g_eom->comp_object_intercept_hooks, hook_data);

   /* get the output size */
   e_output_size_get(eom_output->output, &w, &h);
   cdata->shell.configure_send(ec->comp_data->shell.surface, 0, w, h);

   EOINF("Send Configure Event for Presentation (%d X %d)", eom_output->output, w, h);
err:
   if (hook_data)
     free(hook_data);
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
   E_EomClientPtr eom_client = NULL, eom_client_itr = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_Event_Client *ev = event;
   E_Client *ec = NULL;
   E_Comp_Wl_Buffer *wl_buffer = NULL;
   tbm_surface_h tbm_buffer = NULL;
   Eina_List *l;
   E_Output_Display_Mode display_mode;
   int width, height;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), ECORE_CALLBACK_PASS_ON);

   eom_client = _e_eom_client_get_current_by_ec(ec);
   if (eom_client == NULL)
     {
        eom_client = _e_eom_client_get_current_by_ec_parrent(ec);
        if (eom_client == NULL)
          return ECORE_CALLBACK_PASS_ON;
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

   if ((width <= 1) || (height <= 1)) return ECORE_CALLBACK_PASS_ON;

   eom_output = _e_eom_output_get_by_id(eom_client->output_id);
   EINA_SAFETY_ON_NULL_RETURN_VAL(eom_output, ECORE_CALLBACK_PASS_ON);

   if (eom_trace_debug)
     EOINF("===============>  EXT START", eom_output->output);

   /* TODO: It works but maybe there is better solution exists ?
    * Also I do not know how it affects on performance */
   if (ec->map_timer)
     {
        if (eom_trace_debug)
          EOINF("delete map_timer", eom_output->output);
        E_FREE_FUNC(ec->map_timer, ecore_timer_del);
     }

   if (eom_trace_debug)
     EOINF("buffer_changed callback ec:%p", eom_output->output, ec);

   /* set the ec to the output for presentation. */
   if (!e_output_presentation_update(eom_output->output, eom_client->ec))
     {
        EOERR("e_output_presentation_update fails.", eom_output->output);
        return ECORE_CALLBACK_PASS_ON;
     }

   EINA_LIST_FOREACH(g_eom->clients, l, eom_client_itr)
     {
        if (eom_client_itr->output_id == eom_output->id)
          {
             display_mode = e_output_display_mode_get(eom_output->output);
             wl_eom_send_output_mode(eom_client->resource, eom_output->id, _e_eom_output_mode_get(display_mode));
          }
     }

   if (eom_trace_debug)
     EOINF("===============<  EXT START", eom_output->output);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_eom_cb_wl_request_set_attribute(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, uint32_t attribute)
{
   E_EomClientPtr eom_client = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_Output *primary_output = NULL;

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   eom_output = _e_eom_output_get_by_id(output_id);
   EINA_SAFETY_ON_NULL_GOTO(eom_output, no_eom_output);

   EOINF("Set attribute:%d, client:%p", eom_output->output, attribute, eom_client);

   /* Bind the client with a concrete output */
   eom_client->output_id = output_id;

   /* Set the attribute to the eom_client */
   if (eom_output->eom_client)
     {
        // if eom_ouitput->eom_client == eom_client
        if (eom_output->eom_client == eom_client)
          {
             /* Current client can set any flag it wants */
             _e_eom_output_attribute_force_set(eom_output, attribute);

             if (attribute == EOM_OUTPUT_ATTRIBUTE_NONE)
               wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                            _e_eom_output_attribute_get(eom_output),
                                            EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                            EOM_ERROR_NONE);
          }
        else
          {
             /* A client is trying to set new attribute */
             if (!_e_eom_output_attribute_set(eom_output, attribute))
               {
                  EOINF("client failed to set attribute", eom_output->output);
                  wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                               _e_eom_output_attribute_get(eom_output),
                                               EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                               EOM_ERROR_INVALID_PARAMETER);
                  return;
               }

             EOINF("Send changes to previous current client", eom_output->output);
             /* eom_output->eom_client is lost the attribute */
             wl_eom_send_output_attribute(eom_output->eom_client->resource, eom_output->id,
                                          _e_eom_output_attribute_get(eom_output),
                                          EOM_OUTPUT_ATTRIBUTE_STATE_LOST,
                                          EOM_ERROR_NONE);

             eom_output->eom_client->current = EINA_FALSE;
             eom_output->eom_client = NULL;
          }
     }
   else
     {
        /* Current client can set any flag it wants */
        _e_eom_output_attribute_force_set(eom_output, attribute);
     }

   /* set the output display_mode */
   if (_e_eom_output_attribute_get(eom_output) == EOM_OUTPUT_ATTRIBUTE_NONE)
     {
        if (e_output_presentation_ec_get(eom_output->output) == eom_client->ec)
          e_output_presentation_unset(eom_output->output);

        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        if (!e_output_mirror_set(eom_output->output, primary_output))
          {
             EOERR("e_output_mirror_set fails", eom_output->output);
             goto no_eom_output;
          }

        /* broadcast the status */
        _e_eom_output_status_broadcast(eom_output, NULL, EOM_OUTPUT_ATTRIBUTE_STATE_NONE);
        return;
     }
   else
     {
        /* Set the client as current client of the eom_output */
        eom_client->current = EINA_TRUE;
        eom_output->eom_client = eom_client;

        if (e_output_presentation_wait_set(eom_output->output, eom_client->ec))
          {
             EOERR("e_output_presentation_wait_set fails\n", NULL);
             return;
          }

        /* Send changes to the caller-client */
        wl_eom_send_output_attribute(eom_client->resource, eom_output->id,
                                     _e_eom_output_attribute_get(eom_output),
                                     EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                                     EOM_ERROR_NONE);
     }

   return;

no_eom_output:
   /* Get here if EOM does not have output referred by output_id */
   wl_eom_send_output_attribute(eom_client->resource, output_id,
                                EOM_OUTPUT_ATTRIBUTE_NONE,
                                EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                                EOM_ERROR_NO_SUCH_DEVICE);

   wl_eom_send_output_mode(eom_client->resource, output_id, EOM_OUTPUT_MODE_NONE);

   wl_eom_send_output_type(eom_client->resource, output_id,
                           EOM_OUTPUT_ATTRIBUTE_STATE_NONE,
                           TDM_OUTPUT_CONN_STATUS_DISCONNECTED);
}

static void
_e_eom_cb_wl_request_set_shell_window(struct wl_client *client, struct wl_resource *resource, uint32_t output_id, struct wl_resource *surface)
{
   E_EomOutputPtr eom_output = NULL;
   E_EomClientPtr eom_client = NULL;
   E_Client *ec = NULL;

   if (!(ec = wl_resource_get_user_data(surface)))
     {
        wl_resource_post_error(surface, WL_DISPLAY_ERROR_INVALID_OBJECT, "No Client For Shell Surface");
        return;
     }

   EOINF("set shell output id:%d resource:%p surface:%p", NULL, output_id, resource, surface);

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   /* ec is used in buffer_change callback for distinguishing external ec and its buffers */
   eom_client->ec = ec;

   eom_output = _e_eom_output_get_by_id(output_id);
   if (eom_output == NULL)
     {
        wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_NO_OUTPUT);
        EOERR("no eom_output error\n", NULL);
        return;
     }

   if (ec == e_output_presentation_ec_get(eom_output->output))
     {
        wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_OUTPUT_OCCUPIED);
        return;
     }

   _e_eom_output_send_configure_event(eom_output, ec);

   wl_eom_send_output_set_window(resource, output_id, WL_EOM_ERROR_NONE);
}

static void
_e_eom_cb_wl_request_get_output_info(struct wl_client *client, struct wl_resource *resource, uint32_t output_id)
{
   E_EomOutputPtr eom_output = NULL;
   int w, h, pw, ph;

   EOINF("get output info:%d", NULL, output_id);

   eom_output = _e_eom_output_get_by_id(output_id);
   EINA_SAFETY_ON_FALSE_RETURN(eom_output);

   /* get the output size */
   e_output_size_get(eom_output->output, &w, &h);
   e_output_phys_size_get(eom_output->output, &pw, &ph);

   wl_eom_send_output_info(resource,
                           eom_output->id,
                           eom_output->type,
                           e_output_display_mode_get(eom_output->output),
                           w, h, pw, ph,
                           eom_output->connection,
                           1, 0, 0, 0);

   EOINF("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d", NULL,
         eom_output->id, eom_output->type, e_output_display_mode_get(eom_output->output),
         w, h, pw, ph, e_output_connected(eom_output->output));
}

static const struct wl_eom_interface _e_eom_wl_implementation =
{
   _e_eom_cb_wl_request_set_attribute,
   _e_eom_cb_wl_request_set_shell_window,
   _e_eom_cb_wl_request_get_output_info
};

static void
_e_eom_cb_wl_eom_client_destroy(struct wl_resource *resource)
{
   E_EomClientPtr eom_client = NULL;
   E_EomOutputPtr eom_output = NULL;
   E_Output *primary_output = NULL;
   E_Client *output_ec = NULL;

   EOINF("=======================>  CLIENT DESTROY", NULL);

   EINA_SAFETY_ON_NULL_RETURN(resource);

   eom_client = _e_eom_client_get_by_resource(resource);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   eom_output = _e_eom_output_get_by_id(eom_client->output_id);
   EINA_SAFETY_ON_NULL_GOTO(eom_output, end);

   if (eom_client == eom_output->eom_client)
     eom_output->eom_client = NULL;

   output_ec = e_output_presentation_ec_get(eom_output->output);
   if (eom_client->ec == output_ec)
     {
        _e_eom_output_attribute_set(eom_output, EOM_OUTPUT_ATTRIBUTE_NONE);
        e_output_presentation_unset(eom_output->output);

        primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
        if (!e_output_mirror_set(eom_output->output, primary_output))
          EOERR("e_output_mirror_set fails", eom_output->output);
     }

end:
   /* Notify eom clients which are binded to a concrete output that the
    * state and mode of the output has been changed */
   if (eom_output)
     _e_eom_output_status_broadcast(eom_output, eom_client, EOM_OUTPUT_ATTRIBUTE_STATE_NONE);

   g_eom->clients = eina_list_remove(g_eom->clients, eom_client);

   free(eom_client);
}

static void
_e_eom_cb_wl_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *resource = NULL;
   E_EomPtr eom = NULL;
   E_EomClientPtr eom_client = NULL;
   E_EomOutputPtr eom_output = NULL;
   Eina_List *l;
   int w, h, pw, ph;

   EINA_SAFETY_ON_NULL_RETURN(data);
   eom = data;

   resource = wl_resource_create(client, &wl_eom_interface, MIN(version, 1), id);
   if (resource == NULL)
     {
        EOERR("resource is null. (version :%d, id:%d)", NULL, version, id);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource, &_e_eom_wl_implementation, eom, _e_eom_cb_wl_eom_client_destroy);

   wl_eom_send_output_count(resource, g_eom->eom_output_count);

   EINA_LIST_FOREACH(g_eom->eom_outputs, l, eom_output)
     {
        if (!eom_output) continue;

        /* get the output size */
        e_output_size_get(eom_output->output, &w, &h);
        e_output_phys_size_get(eom_output->output, &pw, &ph);

        wl_eom_send_output_info(resource,
                                eom_output->id,
                                eom_output->type,
                                e_output_display_mode_get(eom_output->output),
                                w, h, pw, ph,
                                eom_output->connection,
                                1, 0, 0, 0);

        EOINF("send - id : %d, type : %d, mode : %d, w : %d, h : %d, w_mm : %d, h_mm : %d, conn : %d", eom_output->output,
              eom_output->id, eom_output->type, e_output_display_mode_get(eom_output->output),
              w, h, pw, ph, e_output_connected(eom_output->output));
     }

   eom_client = E_NEW(E_EomClient, 1);
   EINA_SAFETY_ON_NULL_RETURN(eom_client);

   eom_client->resource = resource;
   eom_client->current = EINA_FALSE;
   eom_client->output_id = -1;
   eom_client->ec = NULL;

   g_eom->clients = eina_list_append(g_eom->clients, eom_client);

   EOINF("=======================>  BIND CLIENT", NULL);
}

static Eina_Bool
_e_eom_connect(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   E_Client *ec = NULL;
   int w, h;

   if (!g_eom) return EINA_TRUE;

   eom_output = _e_eom_output_find(output);
   if (!eom_output)
     {
        EOERR("cannot find eom_output", NULL);
        return EINA_FALSE;
     }

   if (e_output_display_mode_get(eom_output->output) == E_OUTPUT_DISPLAY_MODE_WAIT_PRESENTATION)
     {
        EOINF("Send Configure Event for Presentation", eom_output->output);

        ec = e_output_presentation_ec_get(eom_output->output);
        EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

        _e_eom_output_send_configure_event(eom_output, ec);
     }

   eom_output->connection = WL_EOM_STATUS_CONNECTION;

   /* If there were previously connected clients to the output - notify them */
   _e_eom_output_info_broadcast(eom_output, EOM_OUTPUT_ATTRIBUTE_STATE_ACTIVE);

   /* get the output size */
   e_output_size_get(eom_output->output, &w, &h);
   EOINF("Setup new eom_output: (%dx%d)", eom_output->output, w, h);

   return EINA_TRUE;
}

static Eina_Bool
_e_eom_disconnect(E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;

   if (!g_eom) return EINA_TRUE;

   eom_output = _e_eom_output_find(output);
   if (!eom_output)
     {
        EOERR("cannot find output", NULL);
        return EINA_FALSE;
     }

   /* update eom_output disconnect */
   eom_output->connection = WL_EOM_STATUS_DISCONNECTION;

   /* If there were previously connected clients to the output - notify them */
   _e_eom_output_info_broadcast(eom_output, EOM_OUTPUT_ATTRIBUTE_STATE_INACTIVE);

   EOINF("Destory output.", eom_output->output);

   return EINA_TRUE;
}

static void
_e_eom_output_cb_output_connect_status_change(void *data, E_Output *output)
{
   if (e_output_connected(output))
     _e_eom_connect(output);
   else
     _e_eom_disconnect(output);
}

static void
_e_eom_output_cb_output_mode_change(void *data, E_Output *output)
{
   E_EomOutputPtr eom_output = NULL;
   E_Client *ec = NULL;

   /* if presentation, send configure notify to all eom_clients */
   if (e_output_display_mode_get(output) == E_OUTPUT_DISPLAY_MODE_PRESENTATION)
     {
       eom_output = _e_eom_output_find(output);
       EINA_SAFETY_ON_NULL_RETURN(eom_output);

       ec = e_output_presentation_ec_get(output);
       EINA_SAFETY_ON_NULL_RETURN(ec);

       _e_eom_output_send_configure_event(eom_output, ec);
     }
}

static void
_e_eom_output_cb_output_add(void *data, E_Output *output)
{
   if (!_e_eom_output_create(output, EINA_TRUE))
     {
        EOERR("_e_eom_output_create fails.", output);
        return;
     }
}

static void
_e_eom_output_cb_output_del(void *data, E_Output *output)
{
   if (!_e_eom_output_destroy(output))
     {
        EOERR("_e_eom_output_destroy fails.", output);
        return;
     }
}

static void
_e_eom_output_deinit(void)
{
   E_EomOutputPtr eom_output;
   Eina_List *l;

   if (!g_eom) return;
   if (!g_eom->eom_outputs) return;

   EINA_LIST_FOREACH(g_eom->eom_outputs, l, eom_output)
     _e_eom_output_destroy(eom_output->output);

   eina_list_free(g_eom->eom_outputs);
   g_eom->eom_outputs = NULL;
}

static Eina_Bool
_e_eom_output_init(void)
{
   E_Comp_Screen *e_comp_screen = NULL;
   E_Output *output = NULL;
   E_Output *primary_output = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp, EINA_FALSE);

   e_comp_screen = e_comp->e_comp_screen;
   EINA_SAFETY_ON_NULL_RETURN_VAL(e_comp_screen, EINA_FALSE);

   primary_output = e_comp_screen_primary_output_get(e_comp->e_comp_screen);
   EINA_SAFETY_ON_NULL_RETURN_VAL(primary_output, EINA_FALSE);

   g_eom->eom_output_count = e_comp_screen->num_outputs - 1;
   EOINF("external output count : %d", NULL, g_eom->eom_output_count);

   /* create the eom_output except for the primary output */
   EINA_LIST_FOREACH(e_comp_screen->outputs, l, output)
     {
        if (!output) continue;
        if (output == primary_output) continue;

        if (!_e_eom_output_create(output, EINA_FALSE))
          {
             EOERR("_e_eom_output_create fails.", output);
             goto err;
          }
     }

   return EINA_TRUE;

err:
   _e_eom_output_deinit();

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_eom_init(void)
{
   EINA_SAFETY_ON_NULL_GOTO(e_comp_wl, err);

   if (e_comp->e_comp_screen->num_outputs < 1)
     return EINA_TRUE;

   g_eom = E_NEW(E_Eom, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_eom, EINA_FALSE);

   g_eom->global = wl_global_create(e_comp_wl->wl.disp, &wl_eom_interface, 1, g_eom, _e_eom_cb_wl_bind);
   EINA_SAFETY_ON_NULL_GOTO(g_eom->global, err);

   if (!_e_eom_output_init())
     {
        EOERR("_e_eom_output_init fail", NULL);
        goto err;
     }

   g_eom->output_connect_status_hook = e_output_hook_add(E_OUTPUT_HOOK_CONNECT_STATUS_CHANGE, _e_eom_output_cb_output_connect_status_change, g_eom);
   g_eom->output_mode_changes_hook = e_output_hook_add(E_OUTPUT_HOOK_MODE_CHANGE, _e_eom_output_cb_output_mode_change, g_eom);
   g_eom->output_add_hook = e_output_hook_add(E_OUTPUT_HOOK_ADD, _e_eom_output_cb_output_add, g_eom);
   g_eom->output_remove_hook = e_output_hook_add(E_OUTPUT_HOOK_REMOVE, _e_eom_output_cb_output_del, g_eom);

   E_LIST_HANDLER_APPEND(g_eom->handlers, E_EVENT_CLIENT_BUFFER_CHANGE, _e_eom_cb_client_buffer_change, g_eom);

   return EINA_TRUE;

err:
   e_eom_shutdown();

   return EINA_FALSE;
}

EINTERN int
e_eom_shutdown(void)
{
   Ecore_Event_Handler *h = NULL;

   if (!g_eom) return 1;

   if (g_eom->handlers)
     {
        EINA_LIST_FREE(g_eom->handlers, h)
          ecore_event_handler_del(h);
        g_eom->handlers = NULL;
     }

   if (g_eom->output_remove_hook)
     {
        e_output_hook_del(g_eom->output_remove_hook);
        g_eom->output_remove_hook = NULL;
     }

   if (g_eom->output_add_hook)
     {
        e_output_hook_del(g_eom->output_add_hook);
        g_eom->output_add_hook = NULL;
     }

   if (g_eom->output_mode_changes_hook)
     {
        e_output_hook_del(g_eom->output_mode_changes_hook);
        g_eom->output_mode_changes_hook = NULL;
     }

   if (g_eom->output_connect_status_hook)
     {
        e_output_hook_del(g_eom->output_connect_status_hook);
        g_eom->output_connect_status_hook = NULL;
     }

   _e_eom_output_deinit();

   if (g_eom->global)
     wl_global_destroy(g_eom->global);
   g_eom->global = NULL;

   E_FREE(g_eom);

   return 1;
}

E_API Eina_Bool
e_eom_is_ec_external(E_Client *ec)
{
   E_EomOutputPtr eom_output;

   if (!g_eom) return EINA_FALSE;

   eom_output = _e_eom_output_by_ec_child_get(ec);
   if (!eom_output) return EINA_FALSE;

   return EINA_TRUE;
}