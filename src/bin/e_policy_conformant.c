#include "e.h"

#include <wayland-server.h>
#include <tizen-extension-server-protocol.h>

#define CFDBG(f, x...)  DBG("Conformant|"f, ##x)
#define CFINF(f, x...)  INF("Conformant|"f, ##x)
#define CFERR(f, x...)  ERR("Conformant|"f, ##x)

#define CONF_DATA_GET(ptr)             \
   Conformant *ptr = _conf_data_get()
#define CONF_DATA_GET_OR_RETURN(ptr)   \
   CONF_DATA_GET(ptr);                 \
   if (!ptr)                           \
   {                                   \
      CFERR("no conformant data");     \
      return;                          \
   }
#define CONF_DATA_GET_OR_RETURN_VAL(ptr, val)   \
   CONF_DATA_GET(ptr);                          \
   if (!ptr)                                    \
   {                                            \
      CFERR("no conformant data");              \
      return val;                               \
   }

typedef enum
{
   CONFORMANT_TYPE_INDICATOR = 0,
   CONFORMANT_TYPE_KEYBOARD,
   CONFORMANT_TYPE_CLIPBOARD,
   CONFORMANT_TYPE_MAX,
} Conformant_Type;

typedef struct
{
   struct
     {
        E_Client *ec;
        E_Client *owner;
        struct
          {
             Eina_Bool restore;
             Eina_Bool visible;
             int x, y, w, h;
          } state;

        Eina_Bool changed : 1;
     } part[CONFORMANT_TYPE_MAX];

   Eina_Hash *client_hash;
   Eina_List *handlers;
   E_Client_Hook *client_del_hook;
   Ecore_Idle_Enterer *idle_enterer;
} Conformant;

typedef struct
{
   E_Client *ec;
   Eina_List *res_list;
} Conformant_Client;

typedef struct
{
   Conformant_Client *cfc;
   struct wl_resource *res;
   struct wl_listener destroy_listener;
} Conformant_Wl_Res;

Conformant *_conf = NULL;

static Conformant *
_conf_data_get()
{
   return _conf;
}

static const char*
_conf_type_to_str(Conformant_Type type)
{
   switch (type)
     {
      case CONFORMANT_TYPE_INDICATOR:
         return "indicator";
      case CONFORMANT_TYPE_KEYBOARD:
         return "keyboard";
      case CONFORMANT_TYPE_CLIPBOARD:
         return "clipboard";
      default:
         return "not supported";
     }
}

static uint32_t
_conf_type_map(Conformant_Type type)
{
   switch (type)
     {
      case CONFORMANT_TYPE_INDICATOR:
         return TIZEN_POLICY_CONFORMANT_PART_INDICATOR;
      case CONFORMANT_TYPE_KEYBOARD:
         return TIZEN_POLICY_CONFORMANT_PART_KEYBOARD;
      case CONFORMANT_TYPE_CLIPBOARD:
         return TIZEN_POLICY_CONFORMANT_PART_CLIPBOARD;
      default:
         return TIZEN_POLICY_CONFORMANT_PART_CLIPBOARD + 1;
     }

   return TIZEN_POLICY_CONFORMANT_PART_CLIPBOARD + 1;
}

static void
_conf_state_update(Conformant *conf, Conformant_Type type, Eina_Bool visible, int x, int y, int w, int h)
{
   Conformant_Client *cfc;
   Conformant_Wl_Res *cres;
   uint32_t conf_type;
   Eina_List *l;

   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   if ((conf->part[type].state.visible == visible) &&
       (conf->part[type].state.x == x) && (conf->part[type].state.x == y) &&
       (conf->part[type].state.x == w) && (conf->part[type].state.x == h))
     return;

   CFDBG("Update Conformant State for %d\n", type);
   CFDBG("\tprev: v %d geom %d %d %d %d\n",
       conf->part[type].state.visible,
       conf->part[type].state.x,
       conf->part[type].state.y,
       conf->part[type].state.w,
       conf->part[type].state.h);
   CFDBG("\tnew : v %d geom %d %d %d %d\n", visible, x, y, w, h);

   conf->part[type].state.visible = visible;
   conf->part[type].state.x = x;
   conf->part[type].state.y = y;
   conf->part[type].state.w = w;
   conf->part[type].state.h = h;

   if (!conf->part[type].owner)
     return;

   conf_type = _conf_type_map(type);

   cfc = eina_hash_find(conf->client_hash, &conf->part[type].owner);
   if (!cfc)
     return;

   CFDBG("\t=> '%s'(%p)", cfc->ec ? (cfc->ec->icccm.name ?:"") : "", cfc->ec);
   EINA_LIST_FOREACH(cfc->res_list, l, cres)
     {
        tizen_policy_send_conformant_area
           (cres->res,
            cfc->ec->comp_data->surface,
            conf_type,
            (unsigned int)visible, x, y, w, h);
     }
}

static Conformant_Type
_conf_client_type_get(E_Client *ec)
{
   Conformant_Type type = CONFORMANT_TYPE_MAX;
   if (!ec) return type;

   if (ec->vkbd.vkbd)
     type = CONFORMANT_TYPE_KEYBOARD;
   else if (e_policy_client_is_cbhm(ec))
     type = CONFORMANT_TYPE_CLIPBOARD;

   return type;
}

static void
_conf_client_del(Conformant_Client *cfc)
{
   Conformant_Wl_Res *cres;

   EINA_LIST_FREE(cfc->res_list, cres)
     {
        wl_list_remove(&cres->destroy_listener.link);
        free(cres);
     }

   free(cfc);
}

static void
_conf_client_resource_destroy(struct wl_listener *listener, void *data)
{
   Conformant_Wl_Res *cres;

   cres = container_of(listener, Conformant_Wl_Res, destroy_listener);
   if (!cres)
     return;

   CFDBG("Destroy Wl Resource res %p owner %s(%p)",
         cres->res, cres->cfc->ec->icccm.name ? cres->cfc->ec->icccm.name : "", cres->cfc->ec);

   cres->cfc->res_list = eina_list_remove(cres->cfc->res_list, cres);

   free(cres);
}

static void
_conf_client_resource_add(Conformant_Client *cfc, struct wl_resource *res)
{
   Conformant_Wl_Res *cres;
   Eina_List *l;

   if (cfc->res_list)
     {
        EINA_LIST_FOREACH(cfc->res_list, l, cres)
          {
             if (cres->res == res)
               {
                  CFERR("Already Added Resource, Nothing to do. res: %p", res);
                  return;
               }
          }
     }

   cres = E_NEW(Conformant_Wl_Res, 1);
   if (!cres)
     return;

   cres->cfc = cfc;
   cres->res = res;
   cres->destroy_listener.notify = _conf_client_resource_destroy;
   wl_resource_add_destroy_listener(res, &cres->destroy_listener);

   cfc->res_list = eina_list_append(cfc->res_list, cres);
}

static Conformant_Client *
_conf_client_add(Conformant *conf, E_Client *ec, struct wl_resource *res)
{
   Conformant_Client *cfc;

   cfc = E_NEW(Conformant_Client, 1);
   if (!cfc)
     return NULL;

   cfc->ec = ec;

   _conf_client_resource_add(cfc, res);

   return cfc;
}

static E_Client *
_conf_part_owner_find(E_Client *part, Conformant_Type type)
{
   if ((type == CONFORMANT_TYPE_KEYBOARD) ||
       (type == CONFORMANT_TYPE_CLIPBOARD))
     {
        return part->parent;
     }

   return NULL;
}

static void
_conf_cb_part_obj_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   CONF_DATA_GET_OR_RETURN(conf);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   CFDBG("PART %s ec(%p) Deleted", _conf_type_to_str(type), conf->part[type].ec);

   conf->part[type].ec = NULL;
}

static void
_conf_cb_part_obj_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;
   E_Client *owner = NULL;

   CONF_DATA_GET_OR_RETURN(conf);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   CFDBG("PART %s ec(%p) Show", _conf_type_to_str(type), conf->part[type].ec);

   owner = _conf_part_owner_find(conf->part[type].ec, type);
   conf->part[type].owner = owner;
   if (!conf->part[type].owner)
     WRN("Not exist %s part(ec(%p)'s parent even if it becomes visible",
         _conf_type_to_str(type), conf->part[type].ec);
   conf->part[type].changed = 1;
}

static void
_conf_cb_part_obj_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   CONF_DATA_GET_OR_RETURN(conf);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   CFDBG("PART %s ec(%p) Hide", _conf_type_to_str(type), conf->part[type].ec);
   _conf_state_update(conf,
                      type,
                      EINA_FALSE,
                      conf->part[type].state.x,
                      conf->part[type].state.y,
                      conf->part[type].state.w,
                      conf->part[type].state.h);
   conf->part[type].owner = NULL;
}

static void
_conf_cb_part_obj_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   CONF_DATA_GET_OR_RETURN(conf);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   CFDBG("PART %s ec(%p) Move", _conf_type_to_str(type), conf->part[type].ec);

   conf->part[type].changed = 1;
}

static void
_conf_cb_part_obj_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   CONF_DATA_GET_OR_RETURN(conf);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   CFDBG("PART %s ec(%p) Resize", _conf_type_to_str(type), conf->part[type].ec);

   conf->part[type].changed = 1;
}

static void
_conf_part_register(Conformant *conf, E_Client *ec, Conformant_Type type)
{
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   if (conf->part[type].ec)
     {
        CFERR("Can't register ec(%p) for %s. ec(%p) was already registered.",
              ec, _conf_type_to_str(type), conf->part[type].ec);
        return;
     }

   CFINF("%s Registered ec:%p", _conf_type_to_str(type), ec);

   conf->part[type].ec = ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_DEL,      _conf_cb_part_obj_del,     (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,     _conf_cb_part_obj_show,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,     _conf_cb_part_obj_hide,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,     _conf_cb_part_obj_move,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,   _conf_cb_part_obj_resize,  (void*)type);
}

static Eina_Bool
_conf_cb_client_add(void *data, int evtype EINA_UNUSED, void *event)
{
   Conformant *conf;
   Conformant_Type type;
   E_Event_Client *ev;

   conf = data;
   ev = event;

   type = _conf_client_type_get(ev->ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, ECORE_CALLBACK_PASS_ON);

   _conf_part_register(conf, ev->ec, type);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_begin(void *data, int evtype EINA_UNUSED, void *event)
{
   Conformant *conf;
   E_Event_Client *ev;
   Conformant_Type type;

   ev = event;
   conf = data;

   type = _conf_client_type_get(ev->ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, ECORE_CALLBACK_PASS_ON);

   /* set conformant area to non-visible state before starting rotation.
    * this is to prevent to apply wrong area of conformant area after rotation.
    * Suppose conformant area will be set later according to changes of vkbd such as resize or move.
    * if there is no being called rot_change_cancel and nothing changes vkbd,
    * that is unexpected case.
    */
   if (conf->part[type].state.visible)
     {
        _conf_state_update(conf,
                           type,
                           EINA_FALSE,
                           conf->part[type].state.x,
                           conf->part[type].state.y,
                           conf->part[type].state.w,
                           conf->part[type].state.h);
        conf->part[type].state.restore = EINA_TRUE;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_cancel(void *data, int evtype EINA_UNUSED, void *event)
{
   Conformant *conf;
   E_Event_Client *ev;
   Conformant_Type type;

   ev = event;
   conf = data;

   type = _conf_client_type_get(ev->ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, ECORE_CALLBACK_PASS_ON);

   if (conf->part[type].state.restore)
     {
        CFDBG("Rotation Cancel %s ec(%p)", _conf_type_to_str(type), ev->ec);
        _conf_state_update(conf,
                           type,
                           EINA_TRUE,
                           conf->part[type].state.x,
                           conf->part[type].state.y,
                           conf->part[type].state.w,
                           conf->part[type].state.h);
        conf->part[type].state.restore = EINA_TRUE;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_end(void *data, int evtype EINA_UNUSED, void *event)
{
   Conformant *conf;
   E_Event_Client *ev;
   Conformant_Type type;

   ev = event;
   conf = data;

   type = _conf_client_type_get(ev->ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, ECORE_CALLBACK_PASS_ON);

   conf->part[type].state.restore = EINA_FALSE;

   return ECORE_CALLBACK_PASS_ON;
}

static void
_conf_cb_client_del(void *data, E_Client *ec)
{
   Conformant *conf;
   Conformant_Client *cfc;

   conf = data;
   if (!conf->client_hash)
     return;

   cfc = eina_hash_find(conf->client_hash, &ec);
   if (!cfc)
     return;

   eina_hash_del(conf->client_hash, &ec, cfc);
   _conf_client_del(cfc);
}

static Eina_Bool
_conf_idle_enter(void *data)
{
   Conformant *conf;
   Eina_Bool visible;
   int x, y, w, h;
   Conformant_Type type;

   conf = data;

   for (type = CONFORMANT_TYPE_INDICATOR; type < CONFORMANT_TYPE_MAX; type ++)
     {
        if (conf->part[type].changed)
          {
             visible = evas_object_visible_get(conf->part[type].ec->frame);
             evas_object_geometry_get(conf->part[type].ec->frame, &x, &y, &w, &h);

             _conf_state_update(conf, type, visible, x, y, w, h);
             conf->part[type].changed = EINA_FALSE;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_conf_event_init(Conformant *conf)
{
   E_LIST_HANDLER_APPEND(conf->handlers, E_EVENT_CLIENT_ADD,                     _conf_cb_client_add,                conf);
   E_LIST_HANDLER_APPEND(conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN,   _conf_cb_client_rot_change_begin,   conf);
   E_LIST_HANDLER_APPEND(conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL,  _conf_cb_client_rot_change_cancel,  conf);
   E_LIST_HANDLER_APPEND(conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_END,     _conf_cb_client_rot_change_end,     conf);

   conf->client_del_hook = e_client_hook_add(E_CLIENT_HOOK_DEL, _conf_cb_client_del, conf);
   conf->idle_enterer = ecore_idle_enterer_add(_conf_idle_enter, conf);
}

static void
_conf_event_shutdown(Conformant *conf)
{
   E_FREE_LIST(conf->handlers, ecore_event_handler_del);
   E_FREE_FUNC(conf->client_del_hook, e_client_hook_del);
   E_FREE_FUNC(conf->idle_enterer, ecore_idle_enterer_del);
}

EINTERN void
e_policy_conformant_part_add(E_Client *ec)
{
   Conformant_Type type = CONFORMANT_TYPE_MAX;

   CONF_DATA_GET(conf);
   type = _conf_client_type_get(ec);
   EINA_SAFETY_ON_TRUE_RETURN(type >= CONFORMANT_TYPE_MAX);

   _conf_part_register(conf, ec, type);
}

EINTERN void
e_policy_conformant_client_add(E_Client *ec, struct wl_resource *res)
{
   Conformant_Client *cfc;

   CONF_DATA_GET_OR_RETURN(conf);

   EINA_SAFETY_ON_NULL_RETURN(ec);

   CFDBG("Client Add '%s'(%p)", ec->icccm.name ? ec->icccm.name : "", ec);

   if (conf->client_hash)
     {
        cfc = eina_hash_find(conf->client_hash, &ec);
        if (cfc)
          {
             CFDBG("Already Added Client, Just Add Resource");
             _conf_client_resource_add(cfc, res);
             return;
          }
     }

   cfc = _conf_client_add(conf, ec, res);

   /* do we need to send conformant state if vkbd is visible ? */

   if (!conf->client_hash)
     conf->client_hash = eina_hash_pointer_new(NULL);

   eina_hash_add(conf->client_hash, &ec, cfc);
}

EINTERN void
e_policy_conformant_client_del(E_Client *ec)
{
   Conformant_Client *cfc;

   CONF_DATA_GET_OR_RETURN(conf);

   EINA_SAFETY_ON_NULL_RETURN(ec);

   CFDBG("Client Del '%s'(%p)", ec->icccm.name ? ec->icccm.name : "", ec);

   cfc = eina_hash_find(conf->client_hash, &ec);
   if (cfc)
     {
        eina_hash_del(conf->client_hash, &ec, cfc);
        _conf_client_del(cfc);
     }
}

EINTERN Eina_Bool
e_policy_conformant_client_check(E_Client *ec)
{
   CONF_DATA_GET_OR_RETURN_VAL(conf, EINA_FALSE);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!conf->client_hash)
     return EINA_FALSE;

   return !!eina_hash_find(conf->client_hash, &ec);
}

EINTERN Eina_Bool
e_policy_conformant_init(void)
{
   Conformant *conf;

   if (_conf)
     return EINA_TRUE;

   CFINF("Conformant Module Init");

   conf = E_NEW(Conformant, 1);
   if (!conf)
     return EINA_FALSE;

   _conf_event_init(conf);

   _conf = conf;

   return EINA_TRUE;
}

EINTERN void
e_policy_conformant_shutdown(void)
{
   Conformant_Client *cfc;
   Eina_Iterator *itr;

   if (!_conf)
     return;

   CFINF("Conformant Module Shutdown");

   _conf_event_shutdown(_conf);

   itr = eina_hash_iterator_data_new(_conf->client_hash);
   EINA_ITERATOR_FOREACH(itr, cfc)
      _conf_client_del(cfc);
   eina_iterator_free(itr);

   E_FREE_FUNC(_conf->client_hash, eina_hash_free);

   E_FREE(_conf);
}