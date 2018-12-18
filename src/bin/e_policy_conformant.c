#include "e.h"

#include <wayland-server.h>
#include <tizen-extension-server-protocol.h>

#undef E_COMP_OBJECT_INTERCEPT_HOOK_APPEND
#define E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(l, t, cb, d) \
  do                                                     \
    {                                                    \
       E_Comp_Object_Intercept_Hook *_h;                 \
       _h = e_comp_object_intercept_hook_add(t, cb, d);  \
       assert(_h);                                       \
       l = eina_list_append(l, _h);                      \
    }                                                    \
  while (0)

typedef enum
{
   CONFORMANT_TYPE_INDICATOR = 0,
   CONFORMANT_TYPE_KEYBOARD,
   CONFORMANT_TYPE_CLIPBOARD,
   CONFORMANT_TYPE_MAX,
} Conformant_Type;

typedef enum
{
   DEFER_JOB_HIDE = 0,
   DEFER_JOB_RESIZE,
} Defer_Job_Type;

typedef struct
{
   Defer_Job_Type type;
   Conformant_Type conf_type;
   uint32_t serial;
   E_Client *owner;
   E_Client_Hook *owner_del_hook;
} Defer_Job;

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
             Eina_Bool will_hide;
          } state;

        Eina_Bool changed : 1;
        Eina_List *defer_jobs;
        uint32_t last_serial;
     } part[CONFORMANT_TYPE_MAX];

   Eina_Hash *client_hash;
   Eina_List *handlers;
   Eina_List *interceptors;
   E_Client_Hook *client_del_hook;
   Ecore_Idle_Enterer *idle_enterer;
} Conformant;

typedef struct
{
   E_Client *ec;
   Eina_List *res_list;

   Ecore_Timer *timer;
   Eina_Bool wait_ack;
   uint32_t last_ack;
   unsigned int wait_update;
} Conformant_Client;

typedef struct
{
   Conformant_Client *cfc;
   struct wl_resource *res;
   struct wl_listener destroy_listener;

   uint32_t serial;
   Eina_Bool use_ack;
   Eina_Bool ack_done;
} Conformant_Wl_Res;

static Conformant *g_conf = NULL;

static E_Client   *_conf_part_owner_find(E_Client *part, Conformant_Type type);
static void        _conf_client_defer_job_do(Conformant_Client *cfc, uint32_t serial);
static Eina_Bool   _conf_client_ack_timeout(void *data);

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
_conf_state_update(Conformant_Type type, Eina_Bool visible, int x, int y, int w, int h)
{
   Conformant_Client *cfc;
   Conformant_Wl_Res *cres;
   uint32_t conf_type;
   Eina_List *l;
   Eina_Bool wait_ack = EINA_FALSE;

   if (!g_conf)
     return;

   if ((g_conf->part[type].state.visible == visible) &&
       (g_conf->part[type].state.x == x) && (g_conf->part[type].state.x == y) &&
       (g_conf->part[type].state.x == w) && (g_conf->part[type].state.x == h))
     return;

   DBG("Update Conformant State for %d\n", type);
   DBG("\tprev: v %d geom %d %d %d %d\n",
       g_conf->part[type].state.visible,
       g_conf->part[type].state.x,
       g_conf->part[type].state.y,
       g_conf->part[type].state.w,
       g_conf->part[type].state.h);
   DBG("\tnew : v %d geom %d %d %d %d\n", visible, x, y, w, h);

   g_conf->part[type].state.visible = visible;
   g_conf->part[type].state.x = x;
   g_conf->part[type].state.y = y;
   g_conf->part[type].state.w = w;
   g_conf->part[type].state.h = h;

   if (!g_conf->part[type].owner)
     {
        /* WORKAROUND
         * since vkbd's parent can be NULL at the time of vkbd's object is shown,
         * call '_conf_part_owner_find' again.
         * the better way I think is exporting related API so that clipboard and
         * vkbd modules can set its owner directly, or we can use event
         * mechanism, or checking fetch flag every time we enter the idle
         * (but fetch flag can be false, careful), and calling
         * '_conf_state_update' at that time.
         * we need to consider using like this.
         */
        g_conf->part[type].owner = _conf_part_owner_find(g_conf->part[type].ec, type);
        if (!g_conf->part[type].owner)
          {
             DBG("NO Client to send change the conformant area");
             return;
          }
     }

   conf_type = _conf_type_map(type);

   cfc = eina_hash_find(g_conf->client_hash, &g_conf->part[type].owner);
   if (!cfc)
     {
        DBG("NO conformant Client found");
        return;
     }

   DBG("\t=> '%s'(win:%zx, ec:%p)", cfc->ec ? (cfc->ec->icccm.name ?:"") : "", e_client_util_win_get(cfc->ec), cfc->ec);
   EINA_LIST_FOREACH(cfc->res_list, l, cres)
     {
        cres->ack_done = EINA_FALSE;
        if (!cres->use_ack)
          {
             cres->ack_done = EINA_TRUE;
             tizen_policy_send_conformant_area
                (cres->res,
                 cfc->ec->comp_data->surface,
                 conf_type,
                 (unsigned int)visible, x, y, w, h);
          }
        else
          {
             uint32_t serial;
             serial = wl_display_next_serial(e_comp_wl->wl.disp);

             tizen_policy_send_conformant_region
                (cres->res,
                 cfc->ec->comp_data->surface,
                 conf_type,
                 (unsigned int)visible, x, y, w, h, serial);

             wait_ack = EINA_TRUE;
             cres->serial = serial;
             g_conf->part[type].last_serial = serial;
          }
     }

   if (wait_ack)
     {
        if (cfc->timer)
          ecore_timer_del(cfc->timer);

        /* renew timer */
        cfc->timer = ecore_timer_add(e_config->conformant_ack_timeout,
                                     _conf_client_ack_timeout,
                                     cfc);
        cfc->wait_ack = EINA_TRUE;
     }
}

static Eina_Bool
_conf_client_ack_timeout(void *data)
{
   Conformant_Client *cfc = (Conformant_Client *)data;

   if (!cfc) return ECORE_CALLBACK_CANCEL;

   cfc->wait_ack = EINA_FALSE;
   _conf_client_defer_job_do(cfc, 0);
   cfc->wait_update = 0;

   return ECORE_CALLBACK_DONE;
}

static Eina_Bool
_conf_client_ack_check(Conformant_Client *cfc)
{
   Eina_List *l;
   Conformant_Wl_Res *cres;

   EINA_LIST_FOREACH(cfc->res_list, l, cres)
     {
        if (cres->use_ack && !cres->ack_done)
          return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_conf_defer_job_cb_owner_del(void *data, E_Client *ec)
{
   Defer_Job *job;

   if (!g_conf)
     return;

   job = (Defer_Job *)data;
   if (job->owner == ec)
     {
        if (job->type == DEFER_JOB_HIDE)
          {
             if (g_conf->part[job->conf_type].owner == ec)
               {
                  E_Client *part_ec = g_conf->part[job->conf_type].ec;

                  g_conf->part[job->conf_type].owner = NULL;
                  g_conf->part[job->conf_type].state.will_hide = EINA_FALSE;

                  /* checks for dectecting visible state changes */
                  if ((!part_ec->visible) || (part_ec->iconic) || (part_ec->hidden))
                    evas_object_hide(part_ec->frame);
               }
          }
        else if (job->type == DEFER_JOB_RESIZE)
          {
             /* TODO */
          }

        g_conf->part[job->conf_type].defer_jobs =
           eina_list_remove(g_conf->part[job->conf_type].defer_jobs, job);
        free(job);
     }
}

static Defer_Job *
_conf_client_defer_job_create(Defer_Job_Type job_type, Conformant_Type conf_type, uint32_t serial, E_Client *owner)
{
   Defer_Job *job;

   job = E_NEW(Defer_Job, 1);
   if (!job) return NULL;

   job->type = job_type;
   job->conf_type = conf_type;
   job->serial = serial;
   job->owner = owner;
   job->owner_del_hook = e_client_hook_add(E_CLIENT_HOOK_DEL,
                                           _conf_defer_job_cb_owner_del,
                                           (void*)job);

   return job;
}

static void
_conf_client_defer_job_do(Conformant_Client *cfc, uint32_t serial)
{
   E_Client *ec;
   Conformant_Type type;
   Eina_List *l, *ll;
   Defer_Job *job;

   if (!g_conf)
     return;

   ec = cfc->ec;

   for (type = 0; type < CONFORMANT_TYPE_MAX; type++)
     {
        EINA_LIST_FOREACH_SAFE(g_conf->part[type].defer_jobs, l, ll, job)
          {
             /* if serial is lower than job->serial,
              * next commit will not include changes for this job
              */
             if ((serial) && (job->serial > serial)) continue;

             /* it's not from job owner */
             if (job->owner != ec) continue;

             if (job->type == DEFER_JOB_HIDE)
               {
                  if (g_conf->part[type].owner == ec)
                    {
                       E_Client *part_ec = g_conf->part[type].ec;

                       g_conf->part[type].owner = NULL;
                       g_conf->part[type].state.will_hide = EINA_FALSE;

                       /* checks for dectecting visible state changes */
                       if ((!part_ec->visible) || (part_ec->iconic) || (part_ec->hidden))
                         evas_object_hide(part_ec->frame);
                    }
               }
             else if (job->type == DEFER_JOB_RESIZE)
               {
                  /* TODO */
               }

             e_client_hook_del(job->owner_del_hook);
             free(job);
             g_conf->part[type].defer_jobs =
                eina_list_remove_list(g_conf->part[type].defer_jobs, l);
          }
     }
}

static Conformant_Type
_conf_client_type_get(E_Client *ec)
{
   Conformant_Type type = CONFORMANT_TYPE_MAX;
   if (!ec) return type;

   if (ec->vkbd.vkbd && !ec->vkbd.floating)
     type = CONFORMANT_TYPE_KEYBOARD;
   else if (e_policy_client_is_cbhm(ec))
     type = CONFORMANT_TYPE_CLIPBOARD;

   return type;
}

static void
_conf_client_del(Conformant_Client *cfc)
{
   Conformant_Wl_Res *cres;

   if (cfc->timer)
     ecore_timer_del(cfc->timer);

   EINA_LIST_FREE(cfc->res_list, cres)
     {
        wl_list_remove(&cres->destroy_listener.link);
        free(cres);
     }

   _conf_client_defer_job_do(cfc, 0);

   free(cfc);
}

static void
_conf_client_resource_destroy(struct wl_listener *listener, void *data)
{
   Conformant_Wl_Res *cres;

   cres = container_of(listener, Conformant_Wl_Res, destroy_listener);
   if (!cres)
     return;

   if (cres->destroy_listener.notify)
     {
        wl_list_remove(&cres->destroy_listener.link);
        cres->destroy_listener.notify = NULL;
     }
   DBG("Destroy Wl Resource res %p owner %s(%p)",
         cres->res, cres->cfc->ec->icccm.name ? cres->cfc->ec->icccm.name : "", cres->cfc->ec);

   cres->cfc->res_list = eina_list_remove(cres->cfc->res_list, cres);

   /* if we can't expect ack from client anymore, do deferred job now */
   if (!cres->cfc->res_list)
     {
        cres->cfc->wait_ack = EINA_FALSE;
        if (cres->cfc->timer) ecore_timer_del(cres->cfc->timer);
        cres->cfc->timer = NULL;
        _conf_client_defer_job_do(cres->cfc, 0);
     }

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
                  ERR("Already Added Resource, Nothing to do. res: %p", res);
                  return;
               }
          }
     }

   cres = E_NEW(Conformant_Wl_Res, 1);
   if (!cres)
     return;

   cres->cfc = cfc;
   cres->res = res;
   cres->use_ack = ((e_config->enable_conformant_ack) &&
                    ((wl_resource_get_version(res) >= TIZEN_POLICY_CONFORMANT_REGION_SINCE_VERSION)));
   cres->destroy_listener.notify = _conf_client_resource_destroy;
   wl_resource_add_destroy_listener(res, &cres->destroy_listener);

   cfc->res_list = eina_list_append(cfc->res_list, cres);
}

static Conformant_Client *
_conf_client_add(E_Client *ec, struct wl_resource *res)
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
   EINA_SAFETY_ON_NULL_RETURN_VAL(part, NULL);

   if (type == CONFORMANT_TYPE_KEYBOARD)
     {
        return part->parent;
     }
   else if(type == CONFORMANT_TYPE_CLIPBOARD)
     {
        /* FIXME : This transient-for setting procees is for current eldbus based clipboard.
         * It should be removed after clipboard supports tzsh.
         */
        E_Client *focused;

        focused = e_client_focused_get();
        /* e_client_focused_get can return the client of deleted state
         * so it's reasonable to check is_del in here.
         */
        if ((focused) && (e_object_is_del(E_OBJECT(focused))))
          focused = NULL;

        e_policy_stack_transient_for_set(part, focused);
        return part->parent;
     }

   return NULL;
}

static void
_conf_cb_part_obj_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;
   Defer_Job *job;

   if (!g_conf)
     return;

   DBG("PART %s ec(%p) Deleted", _conf_type_to_str(type), g_conf->part[type].ec);

   g_conf->part[type].ec = NULL;
   g_conf->part[type].state.will_hide = EINA_FALSE;
   g_conf->part[type].last_serial = 0;
   EINA_LIST_FREE(g_conf->part[type].defer_jobs, job)
     {
        e_client_hook_del(job->owner_del_hook);
        free(job);
     }
}

static void
_conf_cb_part_obj_show(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;
   E_Client *owner = NULL;

   if (!g_conf)
     return;

   DBG("PART %s win(%zx), ec(%p) Show", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);

   owner = _conf_part_owner_find(g_conf->part[type].ec, type);
   g_conf->part[type].owner = owner;
   g_conf->part[type].state.will_hide = EINA_FALSE;
   if (!g_conf->part[type].owner)
     WRN("Not exist %s part(ec(%p)'s parent even if it becomes visible",
         _conf_type_to_str(type), g_conf->part[type].ec);
   g_conf->part[type].changed = 1;
}

static void
_conf_cb_part_obj_hide(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   if (!g_conf)
     return;

   DBG("PART %s win(%zx), ec(%p) Hide", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);
   _conf_state_update(type,
                      EINA_FALSE,
                      g_conf->part[type].state.x,
                      g_conf->part[type].state.y,
                      g_conf->part[type].state.w,
                      g_conf->part[type].state.h);
   g_conf->part[type].owner = NULL;
   g_conf->part[type].state.will_hide = EINA_FALSE;

   if (type == CONFORMANT_TYPE_CLIPBOARD)
     e_policy_stack_transient_for_set(g_conf->part[type].ec, NULL);
}

static void
_conf_cb_part_obj_hiding(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   if (!g_conf)
     return;

   DBG("PART %s win(%zx), ec(%p) Hiding", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);
   _conf_state_update(type,
                      EINA_FALSE,
                      g_conf->part[type].state.x,
                      g_conf->part[type].state.y,
                      g_conf->part[type].state.w,
                      g_conf->part[type].state.h);
   g_conf->part[type].owner = NULL;
}
static void
_conf_cb_part_obj_move(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   if (!g_conf)
     return;

   DBG("PART %s win(%zx), ec(%p) Move", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);

   g_conf->part[type].changed = 1;
}

static void
_conf_cb_part_obj_resize(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Conformant_Type type = (Conformant_Type)data;

   if (!g_conf)
     return;

   DBG("PART %s win(%zx), ec(%p) Resize", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);

   g_conf->part[type].changed = 1;
}

static void
_conf_part_register(E_Client *ec, Conformant_Type type)
{
   if (!g_conf)
     return;

   if (g_conf->part[type].ec)
     {
        ERR("Can't register ec(%p) for %s. ec(%p) was already registered.",
              ec, _conf_type_to_str(type), g_conf->part[type].ec);
        return;
     }

   INF("%s Registered ec:%p", _conf_type_to_str(type), ec);

   g_conf->part[type].ec = ec;

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_DEL,      _conf_cb_part_obj_del,     (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,     _conf_cb_part_obj_show,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,     _conf_cb_part_obj_hide,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,     _conf_cb_part_obj_move,    (void*)type);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,   _conf_cb_part_obj_resize,  (void*)type);

   evas_object_smart_callback_add(ec->frame, "hiding", _conf_cb_part_obj_hiding, (void*)type);
}

static void
_conf_part_deregister(E_Client *ec, Conformant_Type type)
{
   Defer_Job *job;

   if (!g_conf)
     return;

   if (!g_conf->part[type].ec)
     {
        INF("Can't deregister ec(%p) for %s. no ec has been registered",
            ec, _conf_type_to_str(type));
        return;
     }
   else if (g_conf->part[type].ec != ec)
     {
        INF("Can't deregister ec(%p) for %s. ec(%p) was not registered.",
            ec, _conf_type_to_str(type), g_conf->part[type].ec);
        return;
     }

   // deregister callback
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_DEL,      _conf_cb_part_obj_del,     (void*)type);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_SHOW,     _conf_cb_part_obj_show,    (void*)type);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_HIDE,     _conf_cb_part_obj_hide,    (void*)type);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_MOVE,     _conf_cb_part_obj_move,    (void*)type);
   evas_object_event_callback_del_full(ec->frame, EVAS_CALLBACK_RESIZE,   _conf_cb_part_obj_resize,  (void*)type);

   evas_object_smart_callback_del_full(ec->frame, "hiding", _conf_cb_part_obj_hiding, (void*)type);


   g_conf->part[type].ec = NULL;
   g_conf->part[type].state.will_hide = EINA_FALSE;
   g_conf->part[type].last_serial = 0;
   EINA_LIST_FREE(g_conf->part[type].defer_jobs, job)
     {
        e_client_hook_del(job->owner_del_hook);
        free(job);
     }
}

static Eina_Bool
_conf_cb_client_add(void *data, int evtype EINA_UNUSED, void *event)
{
   Conformant_Type type;
   E_Event_Client *ev;

   ev = event;

   type = _conf_client_type_get(ev->ec);
   if (type >= CONFORMANT_TYPE_MAX)
     goto end;

   _conf_part_register(ev->ec, type);
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_begin(void *data, int evtype EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   Conformant_Type type;
   E_Client *ec;
   int i = -1;
   int angle;

   if (!g_conf)
     goto end;

   ev = event;
   ec = ev->ec;

   type = _conf_client_type_get(ev->ec);
   if (type >= CONFORMANT_TYPE_MAX)
     goto end;
   if (!ec)
     goto end;

   if (g_conf->part[type].state.visible && type == CONFORMANT_TYPE_KEYBOARD)
     {
        angle = ec->e.state.rot.ang.next;
        if ((angle % 90 != 0) || (angle / 90 > 3) || (angle < 0))
          goto end;

        i = angle / 90;
        _conf_state_update(type,
                           EINA_TRUE,
                           ec->e.state.rot.geom[i].x,
                           ec->e.state.rot.geom[i].y,
                           ec->e.state.rot.geom[i].w,
                           ec->e.state.rot.geom[i].h);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_cancel(void *data, int evtype EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   Conformant_Type type;

   if (!g_conf)
     goto end;

   ev = event;

   type = _conf_client_type_get(ev->ec);
   if (type >= CONFORMANT_TYPE_MAX)
     goto end;

   if (g_conf->part[type].state.restore)
     {
        DBG("Rotation Cancel %s win(%zx), ec(%p)", _conf_type_to_str(type), e_client_util_win_get(ev->ec), ev->ec);
        _conf_state_update(type,
                           EINA_TRUE,
                           g_conf->part[type].state.x,
                           g_conf->part[type].state.y,
                           g_conf->part[type].state.w,
                           g_conf->part[type].state.h);
        g_conf->part[type].state.restore = EINA_TRUE;
     }
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_client_rot_change_end(void *data, int evtype EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   Conformant_Type type;

   if (!g_conf)
     goto end;

   ev = event;

   type = _conf_client_type_get(ev->ec);
   if (type >= CONFORMANT_TYPE_MAX)
     goto end;

   g_conf->part[type].state.restore = EINA_FALSE;
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_conf_cb_intercept_hook_hide(void *data EINA_UNUSED, E_Client *ec)
{
   Conformant_Type type;
   Defer_Job *job;
   uint32_t pre_serial, post_serial;

   if (!g_conf)
     return EINA_TRUE;//need to check

   type = _conf_client_type_get(ec);
   if (type >= CONFORMANT_TYPE_MAX)
     return EINA_TRUE;

   /* already have deferred job */
   if (g_conf->part[type].state.will_hide)
     return EINA_FALSE;

   /* already invisible state was sent in any ways */
   if (!g_conf->part[type].state.visible)
     return EINA_TRUE;

   pre_serial = wl_display_next_serial(e_comp_wl->wl.disp);

   DBG("PART %s win(%zx) ec(%p) Intercept Hide", _conf_type_to_str(type), e_client_util_win_get(g_conf->part[type].ec), g_conf->part[type].ec);
   _conf_state_update(type,
                      EINA_FALSE,
                      g_conf->part[type].state.x,
                      g_conf->part[type].state.y,
                      g_conf->part[type].state.w,
                      g_conf->part[type].state.h);

   post_serial = g_conf->part[type].last_serial;

   /* no owner no ack */
   if (!g_conf->part[type].owner) return EINA_TRUE;

   /* if there is no diff it means conformant_region was not sent */
   if ((!post_serial) || (pre_serial == post_serial)) return EINA_TRUE;

   /* do we have to wait for ack from deleted client? */
   if (e_object_is_del(E_OBJECT(g_conf->part[type].owner))) return EINA_TRUE;

   /* we will do job when we receivd ack from first tizen_policy resource */
   job = _conf_client_defer_job_create(DEFER_JOB_HIDE,
                                       type,
                                       pre_serial + 1,
                                       g_conf->part[type].owner);
   if (!job) return EINA_TRUE;

   g_conf->part[type].defer_jobs = eina_list_append(g_conf->part[type].defer_jobs, job);
   g_conf->part[type].state.will_hide = EINA_TRUE;
   return EINA_FALSE;
}

static Eina_Bool
_conf_cb_client_buffer_change(void *data, int type, void *event)
{
   E_Event_Client *ev;
   Conformant_Client *cfc;
   E_Client *ec;

   if (!g_conf)
     return ECORE_CALLBACK_PASS_ON;

   ev = event;
   ec = ev->ec;
   if (!ec) return ECORE_CALLBACK_PASS_ON;

   cfc = eina_hash_find(g_conf->client_hash, &ec);
   if (!cfc) return ECORE_CALLBACK_PASS_ON;
   if (!cfc->wait_update) return ECORE_CALLBACK_PASS_ON;

   cfc->wait_update--;
   if (cfc->wait_update) return ECORE_CALLBACK_PASS_ON;

   _conf_client_defer_job_do(cfc, cfc->last_ack);
   cfc->wait_update = 0;

   if (cfc->timer)
     ecore_timer_del(cfc->timer);
   cfc->timer = NULL;

   return ECORE_CALLBACK_PASS_ON;
}

static void
_conf_cb_client_del(void *data, E_Client *ec)
{
   Conformant_Client *cfc;
   Conformant_Type type;

   if (!g_conf || !g_conf->client_hash)
     return;

   DBG("Client Del '%s'(%p)", ec->icccm.name ? ec->icccm.name : "", ec);

   cfc = eina_hash_find(g_conf->client_hash, &ec);
   if (!cfc)
     return;

   eina_hash_del(g_conf->client_hash, &ec, cfc);
   _conf_client_del(cfc);

   for (type = 0; type < CONFORMANT_TYPE_MAX; type++)
     {
        if (g_conf->part[type].owner == ec)
          {
             g_conf->part[type].owner = NULL;
             g_conf->part[type].state.will_hide = EINA_FALSE;
          }
     }
}

static Eina_Bool
_conf_idle_enter(void *data)
{
   Eina_Bool visible;
   int x, y, w, h;
   Conformant_Type type;

   if (!g_conf)
     return ECORE_CALLBACK_PASS_ON;

   for (type = CONFORMANT_TYPE_INDICATOR; type < CONFORMANT_TYPE_MAX; type ++)
     {
        visible = EINA_FALSE;
        x = y = w = h = 0;

        if (g_conf->part[type].changed)
          {
             if (g_conf->part[type].ec)
               {
                  E_Client *ec = g_conf->part[type].ec;

                  //wait for end of animation
                  if ((e_comp_object_is_animating(ec->frame)) ||
                      (evas_object_data_get(ec->frame, "effect_running")))
                    {
                       DBG("Animation is running, skip and try next ec(%p)", ec);
                       continue;
                    }

                  visible = evas_object_visible_get(g_conf->part[type].ec->frame);
                  evas_object_geometry_get(g_conf->part[type].ec->frame, &x, &y, &w, &h);

               }
             _conf_state_update(type, visible, x, y, w, h);
             g_conf->part[type].changed = EINA_FALSE;
          }
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_conf_event_init()
{
   E_LIST_HANDLER_APPEND(g_conf->handlers, E_EVENT_CLIENT_ADD,                     _conf_cb_client_add,                 NULL);
   E_LIST_HANDLER_APPEND(g_conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN,   _conf_cb_client_rot_change_begin,    NULL);
   E_LIST_HANDLER_APPEND(g_conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL,  _conf_cb_client_rot_change_cancel,   NULL);
   E_LIST_HANDLER_APPEND(g_conf->handlers, E_EVENT_CLIENT_ROTATION_CHANGE_END,     _conf_cb_client_rot_change_end,      NULL);
   E_LIST_HANDLER_APPEND(g_conf->handlers, E_EVENT_CLIENT_BUFFER_CHANGE,           _conf_cb_client_buffer_change,       NULL);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(g_conf->interceptors, E_COMP_OBJECT_INTERCEPT_HOOK_HIDE, _conf_cb_intercept_hook_hide, NULL);

   g_conf->client_del_hook = e_client_hook_add(E_CLIENT_HOOK_DEL, _conf_cb_client_del, g_conf);
   g_conf->idle_enterer = ecore_idle_enterer_add(_conf_idle_enter, NULL);
}

static void
_conf_event_shutdown(void)
{
   E_FREE_LIST(g_conf->handlers, ecore_event_handler_del);
   E_FREE_LIST(g_conf->interceptors, e_comp_object_intercept_hook_del);
   E_FREE_FUNC(g_conf->client_del_hook, e_client_hook_del);
   E_FREE_FUNC(g_conf->idle_enterer, ecore_idle_enterer_del);
}

E_API Eina_Bool
e_policy_conformant_part_add(E_Client *ec)
{
   Conformant_Type type = CONFORMANT_TYPE_MAX;

   if (!g_conf) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   type = _conf_client_type_get(ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, EINA_FALSE);

   _conf_part_register(ec, type);

   g_conf->part[type].changed = 1;

   return EINA_TRUE;
}

E_API Eina_Bool
e_policy_conformant_part_del(E_Client *ec)
{
   Conformant_Type type, t;

   if (!g_conf) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   type = CONFORMANT_TYPE_MAX;

   // find part whether ec has registered
   for (t = 0; t < CONFORMANT_TYPE_MAX; t++)
     {
        if (g_conf->part[t].ec == ec)
          {
             type = t;
             break;
          }
     }

   if (type >= CONFORMANT_TYPE_MAX)
     return EINA_FALSE;

   _conf_state_update(type,
                      EINA_FALSE,
                      g_conf->part[type].state.x,
                      g_conf->part[type].state.y,
                      g_conf->part[type].state.w,
                      g_conf->part[type].state.h);

   g_conf->part[type].owner = NULL;
   g_conf->part[type].state.will_hide = EINA_FALSE;

   if (type == CONFORMANT_TYPE_CLIPBOARD)
     e_policy_stack_transient_for_set(g_conf->part[type].ec, NULL);

   _conf_part_deregister(ec, type);

   return EINA_TRUE;
}

E_API Eina_Bool
e_policy_conformant_part_update(E_Client *ec)
{
   Conformant_Type type;
   E_Client *owner;

   if (!g_conf) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   type = _conf_client_type_get(ec);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= CONFORMANT_TYPE_MAX, EINA_FALSE);

   if (g_conf->part[type].state.visible)
     {
        owner = _conf_part_owner_find(g_conf->part[type].ec, type);
        if (!owner)
          {
             DBG("NO new owner for the conformant area");
             return EINA_FALSE;
          }

        if (owner != g_conf->part[type].owner)
          {
             DBG("Update state %s ec(%p). new_owner(win:%zx, ec:%p)", _conf_type_to_str(type), ec, e_client_util_win_get(owner), owner);
             g_conf->part[type].owner = owner;
             g_conf->part[type].changed = EINA_TRUE;
          }
     }

   return EINA_TRUE;
}

EINTERN void
e_policy_conformant_client_add(E_Client *ec, struct wl_resource *res)
{
   Conformant_Client *cfc;

   EINA_SAFETY_ON_NULL_RETURN(g_conf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   DBG("Client Add '%s'(win:%zx, ec:%p)", ec->icccm.name ? ec->icccm.name : "", e_client_util_win_get(ec), ec);

   if (g_conf->client_hash)
     {
        cfc = eina_hash_find(g_conf->client_hash, &ec);
        if (cfc)
          {
             DBG("Already Added Client, Just Add Resource");
             _conf_client_resource_add(cfc, res);
             return;
          }
     }

   cfc = _conf_client_add(ec, res);

   /* do we need to send conformant state if vkbd is visible ? */

   if (!g_conf->client_hash)
     g_conf->client_hash = eina_hash_pointer_new(NULL);

   eina_hash_add(g_conf->client_hash, &ec, cfc);
}

EINTERN void
e_policy_conformant_client_del(E_Client *ec)
{
   Conformant_Client *cfc;

   EINA_SAFETY_ON_NULL_RETURN(g_conf);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   DBG("Client Del '%s'(win:%zx, ec:%p)", ec->icccm.name ? ec->icccm.name : "", e_client_util_win_get(ec), ec);

   cfc = eina_hash_find(g_conf->client_hash, &ec);
   if (cfc)
     {
        eina_hash_del(g_conf->client_hash, &ec, cfc);
        _conf_client_del(cfc);
     }
}

EINTERN Eina_Bool
e_policy_conformant_client_check(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_conf, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   if (!g_conf->client_hash)
     return EINA_FALSE;

   return !!eina_hash_find(g_conf->client_hash, &ec);
}

EINTERN void
e_policy_conformant_client_ack(E_Client *ec, struct wl_resource *res, uint32_t serial)
{
   Conformant_Client *cfc;
   Conformant_Wl_Res *cres;
   Eina_List *l;

   /* ec is already deleted */
   cfc = eina_hash_find(g_conf->client_hash, &ec);
   if (!cfc)
     return;

   if (!cfc->wait_ack) return;

   EINA_LIST_FOREACH(cfc->res_list, l, cres)
     {
        if (cres->res == res)
          {
             if (serial == cres->serial)
               {
                  DBG("Ack conformant region win(%zx) ec(%p) res(%p) serial(%u)", e_client_util_win_get(ec), ec, res, serial);
                  cres->ack_done = EINA_TRUE;
               }
             break;
          }
     }

   cfc->wait_ack = !_conf_client_ack_check(cfc);

   cfc->last_ack = serial;
   cfc->wait_update ++;
}

EINTERN Eina_Bool
e_policy_conformant_init(void)
{
   if (g_conf)
     return EINA_TRUE;

   INF("Conformant Module Init");

   g_conf = E_NEW(Conformant, 1);
   if (!g_conf)
     return EINA_FALSE;

   _conf_event_init();

   return EINA_TRUE;
}

EINTERN void
e_policy_conformant_shutdown(void)
{
   Conformant_Client *cfc;
   Eina_Iterator *itr;

   EINA_SAFETY_ON_NULL_RETURN(g_conf);

   INF("Conformant Module Shutdown");

   _conf_event_shutdown();

   itr = eina_hash_iterator_data_new(g_conf->client_hash);
   EINA_ITERATOR_FOREACH(itr, cfc)
      _conf_client_del(cfc);
   eina_iterator_free(itr);

   E_FREE_FUNC(g_conf->client_hash, eina_hash_free);

   E_FREE(g_conf);
}
