#include "e.h"
#include "services/e_service_quickpanel.h"
#include "services/e_service_gesture.h"
#include "services/e_service_region.h"
#include "e_policy_wl.h"

#define SMART_NAME            "quickpanel_object"
#define INTERNAL_ENTRY                       \
   Mover_Data *md;                           \
   md = evas_object_smart_data_get(obj);     \
   if (!md) return

#define QP_SHOW(qp)                          \
do                                           \
{                                            \
   if (qp->ec && !qp->ec->visible)           \
     {                                       \
        qp->show_block = EINA_FALSE;         \
        qp->ec->visible = EINA_TRUE;         \
        evas_object_show(qp->ec->frame);     \
                                             \
        if (qp->bg_rect)                     \
          {                                  \
             ELOGF("QUICKPANEL", "SHOW BG_RECT...", qp->ec);      \
             evas_object_stack_below(qp->bg_rect, qp->ec->frame); \
             evas_object_show(qp->bg_rect);  \
          }                                  \
     }                                       \
} while (0)

#define QP_HIDE(qp)                          \
do                                           \
{                                            \
   if (qp->ec && qp->ec->visible)            \
     {                                       \
        qp->show_block = EINA_TRUE;          \
        qp->ec->visible = EINA_FALSE;        \
        evas_object_hide(qp->ec->frame);     \
                                             \
        if (qp->bg_rect)                     \
          {                                  \
             ELOGF("QUICKPANEL", "HIDE BG_RECT...", qp->ec);  \
             evas_object_hide(qp->bg_rect);  \
          }                                  \
     }                                       \
} while (0)

#define QP_VISIBLE_SET(qp, vis)              \
do                                           \
{                                            \
   if (vis) QP_SHOW(qp);                     \
   else     QP_HIDE(qp);                     \
} while(0)

#define BACKEND_FUNC_CALL(f, ...)            \
do                                           \
{                                            \
   if ((qp_mgr_funcs) &&                     \
       (qp_mgr_funcs->f))                    \
     {                                       \
        qp_mgr_funcs->f(__VA_ARGS__);        \
        return;                              \
     }                                       \
} while(0)

#define BACKEND_FUNC_CALL_RET(f, ...)        \
do                                           \
{                                            \
   if ((qp_mgr_funcs) &&                     \
       (qp_mgr_funcs->f))                    \
     {                                       \
        return qp_mgr_funcs->f(__VA_ARGS__); \
     }                                       \
} while(0)

typedef struct _E_Policy_Quickpanel E_Policy_Quickpanel;
typedef struct _Mover_Data Mover_Data;

typedef struct _E_QP_Client E_QP_Client;

struct _E_Policy_Quickpanel
{
   E_Client *ec;
   E_Service_Quickpanel_Type type;

   E_Client *below;
   E_Client *stacking;
   Evas_Object *mover;
   Evas_Object *indi_obj;
   Evas_Object *handler_obj;
   Evas_Object *bg_rect;

   Eina_List *intercept_hooks;
   Eina_List *hooks;
   Eina_List *events;
   Ecore_Idle_Enterer *idle_enterer;
   Ecore_Event_Handler *buf_change_hdlr;

   struct
   {
      int x, y;
      unsigned int timestamp;
      float accel;
   } mouse_info;

   struct
   {
      Ecore_Animator *animator;
      E_Service_Quickpanel_Effect_Type type;
      int x, y, from, to;
      int disable_ref;
      Eina_Bool final_visible_state;
      Eina_Bool active;
   } effect;

   struct
   {
      Eina_Bool below;
   } changes;

   E_Policy_Angle_Map rotation;
   E_Maximize saved_maximize;

   Eina_Bool show_block;
   Eina_Bool need_scroll_update;
   Eina_Bool scroll_lock;

   struct
   {
      int x, y, w, h;
   } geom;
};

struct _Mover_Data
{
   E_Policy_Quickpanel *qp;
   E_Client *ec;

   Evas_Object *smart_obj; //smart object
   Evas_Object *qp_layout_obj; // quickpanel's e_layout_object
   Evas_Object *handler_mirror_obj; // quickpanel handler mirror object
   Evas_Object *base_clip; // clipper for quickapnel base object
   Evas_Object *handler_clip; // clipper for quickpanel handler object

   Eina_Rectangle handler_rect;
};

struct _E_QP_Client
{
   E_Client *ec;
   E_Quickpanel_Type type;
   int ref;
   struct
   {
      Eina_Bool vis;
      Eina_Bool scrollable;
   } hint;
};

static Eina_List *qp_services = NULL; /* list of E_Policy_Quickpanel for quickpanel services */
static Evas_Smart *_mover_smart = NULL;
static Eina_Bool _changed = EINA_FALSE;
static E_QP_Mgr_Funcs *qp_mgr_funcs = NULL;

Eina_List *qp_clients = NULL; /* list of E_QP_Client */

static void          _e_qp_srv_effect_update(E_Policy_Quickpanel *qp, int x, int y);
static E_QP_Client * _e_qp_client_ec_get(E_Client *ec, E_Quickpanel_Type type);
static Eina_Bool     _e_qp_client_scrollable_update(E_Policy_Quickpanel *qp);

static void          _quickpanel_client_evas_cb_show(void *data, Evas *evas, Evas_Object *obj, void *event);
static void          _quickpanel_client_evas_cb_hide(void *data, Evas *evas, Evas_Object *obj, void *event);
static void          _quickpanel_client_evas_cb_move(void *data, Evas *evas, Evas_Object *obj, void *event);

inline static Eina_Bool
_e_qp_srv_is_effect_finish_job_started(E_Policy_Quickpanel *qp)
{
   return !!qp->effect.animator;
}

inline static Eina_Bool
_e_qp_srv_effect_final_visible_state_get(E_Policy_Quickpanel *qp)
{
   return !!qp->effect.final_visible_state;
}

inline static Eina_Bool
_e_qp_srv_is_effect_running(E_Policy_Quickpanel *qp)
{
   return !!qp->effect.active;
}

/* look for E_Policy_Quickpanel instance of qp service
 * which is same as given ec
 *
 * @param ec window of qp service
 * @return E_Policy_Quickpanel instance of qp service
 */
static E_Policy_Quickpanel *
_quickpanel_service_get(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   Eina_List *l;

   EINA_LIST_FOREACH(qp_services, l, qp)
     if (qp->ec == ec)
       return qp;

   return NULL;
}

/* look for E_Policy_Quickpanel instance of qp service
 * which has same type as given qp_client
 *
 * @param qp_client instance of qp client
 * @return instance of qp service
 */
static E_Policy_Quickpanel *
_quickpanel_get_with_client_type(E_QP_Client *qp_client)
{
   E_Policy_Quickpanel *qp;
   Eina_List *l;

   /* look for a qp service which has same type as given qp client */
   EINA_LIST_FOREACH(qp_services, l, qp)
     if (qp->type == (E_Service_Quickpanel_Type)qp_client->type)
       return qp;

   return NULL;
}

static void
_mover_intercept_show(void *data, Evas_Object *obj)
{
   Mover_Data *md;
   E_Client *ec;
   Evas *e;

   md = data;

   ec = md->ec;

   /* force update */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   // desk-zoom-set apply map on all e_desk's smart_data(clients)
   // to properly pack a quickpanel window on the mover's e_layout_object
   // (to became a member of mover) it shouldn't be in e_desk's clists.
   // because mover (also smart obj) is a member of e_desk
   // otherwize, desk-zoom will mutiplied on a ec again.
   if (e_config->qp_add_on_desk_smart)
     e_desk_client_del(ec->desk, ec);

   e_layout_pack(md->qp_layout_obj, ec->frame);

  // create base_clip
   e = evas_object_evas_get(obj);
   md->base_clip = evas_object_rectangle_add(e);
   e_layout_pack(md->qp_layout_obj, md->base_clip);
   e_layout_child_move(md->base_clip, 0, 0);
   e_layout_child_resize(md->base_clip, ec->w, ec->h);
   evas_object_color_set(md->base_clip, 255, 255, 255, 255);
   evas_object_show(md->base_clip);
   evas_object_clip_set(ec->frame, md->base_clip);

   // create handler_mirror_obj
   md->handler_mirror_obj =  e_comp_object_util_mirror_add(ec->frame);
   e_layout_pack(md->qp_layout_obj, md->handler_mirror_obj);
   e_layout_child_move(md->handler_mirror_obj, 0, 0);
   e_layout_child_resize(md->handler_mirror_obj, ec->w, ec->h);
   evas_object_show(md->handler_mirror_obj);

   // create handler_clip
   md->handler_clip = evas_object_rectangle_add(e);
   e_layout_pack(md->qp_layout_obj, md->handler_clip);
   e_layout_child_move(md->handler_clip, md->handler_rect.x, md->handler_rect.y);
   e_layout_child_resize(md->handler_clip, md->handler_rect.w, md->handler_rect.h);
   if (e_config->qp_handler.use_alpha)
     evas_object_color_set(md->handler_clip, 255, 255, 255, e_config->qp_handler.alpha);
   else
     evas_object_color_set(md->handler_clip, 255, 255, 255, 255);
   evas_object_show(md->handler_clip);
   evas_object_clip_set(md->handler_mirror_obj, md->handler_clip);

   evas_object_show(obj);

   if (e_config->qp_add_on_desk_smart)
     e_desk_smart_member_add(ec->desk, obj);
}

static void
_mover_smart_add(Evas_Object *obj)
{
   Mover_Data *md;

   md = E_NEW(Mover_Data, 1);
   if (EINA_UNLIKELY(!md))
     return;

   md->smart_obj = obj;
   md->qp_layout_obj = e_layout_add(evas_object_evas_get(obj));
   evas_object_color_set(md->qp_layout_obj, 255, 255, 255, 255);
   evas_object_smart_member_add(md->qp_layout_obj, md->smart_obj);

   evas_object_smart_data_set(obj, md);

   evas_object_move(obj, -1 , -1);
   evas_object_intercept_show_callback_add(obj, _mover_intercept_show, md);
}

static void
_mover_smart_del(Evas_Object *obj)
{
   E_Client *ec;

   INTERNAL_ENTRY;

   ec = md->ec;
   if (md->base_clip)
     {
        evas_object_clip_unset(md->base_clip);
        e_layout_unpack(md->base_clip);
        evas_object_del(md->base_clip);
     }
   if (md->handler_clip)
     {
        evas_object_clip_unset(md->handler_clip);
        e_layout_unpack(md->handler_clip);
        evas_object_del(md->handler_clip);
     }
   if (md->handler_mirror_obj)
     {
        e_layout_unpack(md->handler_mirror_obj);
        evas_object_del(md->handler_mirror_obj);
     }

   if (md->qp_layout_obj) evas_object_del(md->qp_layout_obj);

   evas_object_color_set(ec->frame, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity, ec->netwm.opacity);

   e_comp_client_override_del(ec);

   /* force update
    * we need to force update 'E_Client' here even if update only evas_object,
    * because our render loop would not be started by chaning evas object,
    * we need to make a change on the 'E_Client'. */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   e_layout_unpack(ec->frame);

   if (e_config->qp_add_on_desk_smart)
     e_desk_smart_member_add(ec->desk, ec->frame);

   free(md);
}

static void
_mover_smart_show(Evas_Object *obj)
{
   INTERNAL_ENTRY;

   evas_object_show(md->qp_layout_obj);
}

static void
_mover_smart_hide(Evas_Object *obj)
{
   INTERNAL_ENTRY;

   evas_object_hide(md->qp_layout_obj);
}

static void
_mover_smart_move(Evas_Object *obj, int x, int y)
{
   INTERNAL_ENTRY;

   evas_object_move(md->qp_layout_obj, x, y);
}

static void
_mover_smart_resize(Evas_Object *obj, int w, int h)
{
   INTERNAL_ENTRY;

   e_layout_virtual_size_set(md->qp_layout_obj, w, h);
   evas_object_resize(md->qp_layout_obj, w, h);
}

static void
_mover_smart_init(void)
{
   if (_mover_smart) return;
   {
      static const Evas_Smart_Class sc =
      {
         SMART_NAME,
         EVAS_SMART_CLASS_VERSION,
         _mover_smart_add,
         _mover_smart_del,
         _mover_smart_move,
         _mover_smart_resize,
         _mover_smart_show,
         _mover_smart_hide,
         NULL, /* color_set */
         NULL, /* clip_set */
         NULL, /* clip_unset */
         NULL, /* calculate */
         NULL, /* member_add */
         NULL, /* member_del */

         NULL, /* parent */
         NULL, /* callbacks */
         NULL, /* interfaces */
         NULL  /* data */
      };
      _mover_smart = evas_smart_class_new(&sc);
   }
}

static Evas_Object *
_e_qp_srv_mover_new(E_Policy_Quickpanel *qp)
{
   Evas_Object *mover;
   Mover_Data *md;
   int x, y, w, h;
   E_Desk *desk;
   int tx, ty;

   e_comp_client_override_add(qp->ec);

   _mover_smart_init();
   mover = evas_object_smart_add(evas_object_evas_get(qp->ec->frame), _mover_smart);

   /* Should setup 'md' before call evas_object_show() */
   md = evas_object_smart_data_get(mover);
   EINA_SAFETY_ON_NULL_RETURN_VAL(md, NULL);

   md->ec = qp->ec;

   evas_object_layer_set(md->smart_obj, qp->ec->layer);

   e_service_region_rectangle_get(qp->handler_obj, qp->rotation, &x, &y, &w, &h);
   EINA_RECTANGLE_SET(&md->handler_rect, x, y, w, h);

   tx = qp->ec->zone->x;
   ty = qp->ec->zone->y;
   if (e_config->use_desk_smart_obj)
     {
        desk = e_desk_current_get(qp->ec->zone);
        if (desk)
          {
             tx = desk->geom.x;
             ty = desk->geom.y;
          }
     }

   evas_object_move(mover, tx, ty);
   evas_object_resize(mover, qp->ec->w, qp->ec->h);
   evas_object_show(mover);

   qp->mover = mover;

   return mover;
}

static Eina_Bool
_e_qp_srv_mover_object_relocate(E_Policy_Quickpanel *qp, int x, int y)
{
   E_Client *ec;
   Mover_Data *md;
   int tx, ty, tw, th;

   ec = qp->ec;

   md = evas_object_smart_data_get(qp->mover);
   EINA_SAFETY_ON_NULL_RETURN_VAL(md, EINA_FALSE);

   evas_object_geometry_get(qp->mover, &tx, &ty, &tw, &th);

   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         if (x < tx) return EINA_FALSE;
         if ((x + md->handler_rect.w) > (tx+tw)) return EINA_FALSE;

         e_layout_child_move(md->base_clip, 0, 0);
         e_layout_child_resize(md->base_clip, x - tx, ec->h);

         e_layout_child_move(md->handler_mirror_obj, x - tx - ec->w + md->handler_rect.w, 0);
         e_layout_child_move(md->handler_clip, x - tx, 0);
         break;

      case E_POLICY_ANGLE_MAP_180:
         if (y > (ty+th)) return EINA_FALSE;
         if ((y - md->handler_rect.h) < ty) return EINA_FALSE;

         e_layout_child_move(md->base_clip, 0, y - ty);
         e_layout_child_resize(md->base_clip, ec->w, ty + ec->h - y);

         e_layout_child_move(md->handler_mirror_obj, 0, y - ty - md->handler_rect.h);
         e_layout_child_move(md->handler_clip, 0, y - ty - md->handler_rect.h);
         break;

      case E_POLICY_ANGLE_MAP_270:
         if ((x + md->handler_rect.w) > (tx+tw)) return EINA_FALSE;
         if ((x - md->handler_rect.w) < tx) return EINA_FALSE;

         e_layout_child_move(md->base_clip, x - tx, 0);
         e_layout_child_resize(md->base_clip, tx + ec->w - x, ec->h);

         e_layout_child_move(md->handler_mirror_obj, x - tx - md->handler_rect.w, 0);
         e_layout_child_move(md->handler_clip, x - tx - md->handler_rect.w, 0);
         break;

      default:
         if (y < ty) return EINA_FALSE;
         if ((y + md->handler_rect.h) > (ty+th)) return EINA_FALSE;

         e_layout_child_move(md->base_clip, 0, 0);
         e_layout_child_resize(md->base_clip, ec->w, y - ty);

         e_layout_child_move(md->handler_mirror_obj, 0, y - ty - ec->h + md->handler_rect.h);
         e_layout_child_move(md->handler_clip, 0, y - ty);
     }

   return EINA_TRUE;
}

static void
_e_qp_srv_mover_cb_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   QP_VISIBLE_SET(qp, qp->effect.final_visible_state);
   E_FREE_FUNC(qp->effect.animator, ecore_animator_del);
}

static void
_e_qp_srv_effect_finish_job_end(E_Policy_Quickpanel *qp)
{
   E_QP_Client *qp_client;
   E_Client *focused;
   Eina_List *l;

   if (qp->mover)
     {
        evas_object_event_callback_del(qp->mover, EVAS_CALLBACK_DEL, _e_qp_srv_mover_cb_del);
        E_FREE_FUNC(qp->mover, evas_object_del);
     }

   QP_VISIBLE_SET(qp, qp->effect.final_visible_state);
   qp->effect.active = EINA_FALSE;

   e_zone_orientation_block_set(qp->ec->zone, "quickpanel-mover", EINA_FALSE);

   /* send visible event to only client with the same type of quickpanel service */
   EINA_LIST_FOREACH(qp_clients, l, qp_client)
     {
        if (qp->type == (E_Service_Quickpanel_Type)qp_client->type)
          e_tzsh_qp_state_visible_update(qp_client->ec,
                                         qp->effect.final_visible_state,
                                         qp_client->type);
     }

   focused = e_client_focused_get();
   if (focused)
     {
        if (qp->effect.final_visible_state)
          e_policy_aux_message_send(focused, "quickpanel_state", "shown", NULL);
        else
          e_policy_aux_message_send(focused, "quickpanel_state", "hidden", NULL);
     }

   if ((qp->below) &&
       (qp->below != focused))
     {
        if (qp->effect.final_visible_state)
          e_policy_aux_message_send(qp->below, "quickpanel_state", "shown", NULL);
        else
          e_policy_aux_message_send(qp->below, "quickpanel_state", "hidden", NULL);
     }

   EC_CHANGED(qp->ec);
}

static Eina_Bool
_e_qp_srv_effect_finish_job_op(void *data, double pos)
{
   E_Policy_Quickpanel *qp;
   int new_x = 0, new_y = 0;
   double progress = 0;

   qp = data;
   progress = ecore_animator_pos_map(pos, ECORE_POS_MAP_DECELERATE, 0, 0);
   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
      case E_POLICY_ANGLE_MAP_270:
         new_x = qp->effect.from + (qp->effect.to * progress);
         break;
      default:
      case E_POLICY_ANGLE_MAP_180:
         new_y = qp->effect.from + (qp->effect.to * progress);
         break;
     }
   _e_qp_srv_effect_update(qp, new_x, new_y);

   if (pos == 1.0)
     {
        E_FREE_FUNC(qp->effect.animator, ecore_animator_del);

        _e_qp_srv_effect_finish_job_end(qp);

        return ECORE_CALLBACK_CANCEL;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_qp_srv_effect_finish_job_start(E_Policy_Quickpanel *qp, Eina_Bool visible)
{
   E_Client *ec;
   int from;
   int to;
   double duration;
   const double ref = 0.1;

   ec = qp->ec;

   int tw, th;
   if (qp->effect.type == E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE)
     {
        tw = qp->geom.w;
        th = qp->geom.h;
     }
   else
     {
        tw = ec->zone->w;
        th = ec->zone->h;
     }

   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         from = qp->effect.x;
         to = (visible) ? (tw - from) : (-from);
         duration = ((double)abs(to) / ((double)tw / 2)) * ref;
         break;
      case E_POLICY_ANGLE_MAP_180:
         from = qp->effect.y;
         to = (visible) ? (-from) : (th - from);
         duration = ((double)abs(to) / ((double)th / 2)) * ref;
         break;
      case E_POLICY_ANGLE_MAP_270:
         from = qp->effect.x;
         to = (visible) ? (-from) : (tw - from);
         duration = ((double)abs(to) / ((double)tw / 2)) * ref;
         break;
      default:
         from = qp->effect.y;
         to = (visible) ? (th - from) : (-from);
         duration = ((double)abs(to) / ((double)th / 2)) * ref;
         break;
     }

   if (duration == 0.0)
     {
        if (visible != qp->effect.final_visible_state)
          qp->effect.final_visible_state = visible;

        _e_qp_srv_effect_finish_job_end(qp);
        return;
     }

   /* start move effect */
   qp->effect.from = from;
   qp->effect.to = to;
   qp->effect.final_visible_state = visible;
   qp->effect.animator =
      ecore_animator_timeline_add(duration, _e_qp_srv_effect_finish_job_op, qp);

   if (qp->mover)
     evas_object_event_callback_add(qp->mover, EVAS_CALLBACK_DEL, _e_qp_srv_mover_cb_del, qp);
}

static void
_e_qp_srv_effect_finish_job_stop(E_Policy_Quickpanel *qp)
{
   if (qp->mover)
     evas_object_event_callback_del(qp->mover, EVAS_CALLBACK_DEL, _e_qp_srv_mover_cb_del);
   E_FREE_FUNC(qp->effect.animator, ecore_animator_del);
}


static Eina_Bool
_quickpanel_send_gesture_to_indicator(void)
{
   E_Client *focused;
   focused = e_client_focused_get();
   if (focused)
     {
        ELOGF("TZ_IND", "INDICATOR state:%d, opacity:%d, vtype:%d",
              focused, focused->indicator.state, focused->indicator.opacity_mode, focused->indicator.visible_type);

        if (focused->indicator.state == 2) // state: on
          {
             if (focused->indicator.visible_type == 0) // visible: hidden
               {
                  /* cancel touch events sended up to now */
                  e_comp_wl_touch_cancel();
                  e_policy_wl_indicator_flick_send(focused);
                  return EINA_TRUE;
               }
          }
        else // state: off, unknown
          {
             return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_qp_srv_visibility_eval_by_mouse_info(E_Policy_Quickpanel *qp)
{
   E_Client *ec;
   Eina_Bool is_half;
   int tw, th;
   const float sensitivity = 1.5; /* hard coded. (arbitary) */

   ec = qp->ec;

   if (qp->effect.type == E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE)
     {
        tw = qp->geom.w;
        th = qp->geom.h;
     }
   else
     {
        tw = ec->zone->w;
        th = ec->zone->h;
     }

   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         is_half = (qp->mouse_info.x > (tw / 2));
         break;
      case E_POLICY_ANGLE_MAP_180:
         is_half = (qp->mouse_info.y < (th / 2));
         break;
      case E_POLICY_ANGLE_MAP_270:
         is_half = (qp->mouse_info.x < (tw / 2));
         break;
      case E_POLICY_ANGLE_MAP_0:
      default:
         is_half = (qp->mouse_info.y > (th / 2));
         break;
     }

   if ((qp->mouse_info.accel > sensitivity) ||
       ((qp->mouse_info.accel > -sensitivity) && is_half))
     return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_qp_srv_mouse_info_update(E_Policy_Quickpanel *qp, int x, int y, unsigned int timestamp)
{
   int dp;
   unsigned int dt;

   /* Calculate the acceleration of movement,
    * determine the visibility of quickpanel based on the result. */
   dt = timestamp - qp->mouse_info.timestamp;
   if (dt)
     {
        switch (qp->rotation)
          {
           case E_POLICY_ANGLE_MAP_90:
              dp = x - qp->mouse_info.x;
              break;
           case E_POLICY_ANGLE_MAP_180:
              dp = qp->mouse_info.y - y;
              break;
           case E_POLICY_ANGLE_MAP_270:
              dp = qp->mouse_info.x - x;
              break;
           default:
              dp = y - qp->mouse_info.y;
              break;
          }
        qp->mouse_info.accel = (float)dp / (float)dt;
     }

   qp->mouse_info.x = x;
   qp->mouse_info.y = y;
   qp->mouse_info.timestamp = timestamp;
}

static void
_e_qp_srv_effect_start(E_Policy_Quickpanel *qp)
{
   if (qp->effect.disable_ref)
     return;

   /* Pause changing zone orientation during mover object is working. */
   e_zone_orientation_block_set(qp->ec->zone, "quickpanel-mover", EINA_TRUE);

   qp->effect.active = EINA_TRUE;
   QP_SHOW(qp);
   if (qp->effect.type == E_SERVICE_QUICKPANEL_EFFECT_TYPE_SWIPE)
     _e_qp_srv_mover_new(qp);
}

static void
_e_qp_srv_qp_move(E_Policy_Quickpanel *qp, int x, int y)
{
   E_Client *ec;
   int new_x, new_y;
   int dim;
   double weight;

   if (!qp) return;
   if (!qp->ec) return;

   ec = qp->ec;

   new_x = 0;
   new_y = 0;

   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         new_x = x - qp->geom.w;
         break;
      case E_POLICY_ANGLE_MAP_180:
         new_y = y;
         break;
      case E_POLICY_ANGLE_MAP_270:
         new_x = x;
         break;
      default:
         new_y = y - qp->geom.h;
         if (new_y > 0) new_y = 0;
         break;
     }

   if (qp->bg_rect)
     {
        weight = y / (double)qp->geom.h;
        if (weight > 1) weight = 1;
        else if (weight < 0) weight = 0;

        dim = (int)(178 * weight);
        evas_object_color_set(qp->bg_rect, 0, 0, 0, dim);
     }

   e_client_util_move_without_frame(ec, new_x, new_y);
}

static void
_e_qp_srv_effect_update(E_Policy_Quickpanel *qp, int x, int y)
{
   Eina_Bool res;

   res = _e_qp_srv_is_effect_running(qp);
   if (!res)
     return;

   qp->effect.x = x;
   qp->effect.y = y;

   switch (qp->effect.type)
     {
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_SWIPE:
         _e_qp_srv_mover_object_relocate(qp, x, y);
         break;
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE:
         _e_qp_srv_qp_move(qp, x, y);
         break;
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_APP_CUSTOM:
         ERR("Undefine behavior for APP_CUSTOM type");
         break;
      default:
         ERR("Unknown effect type");
         break;
     }
}

static void
_e_qp_srv_effect_finish(E_Policy_Quickpanel *qp, Eina_Bool final_visible_state)
{
   Eina_Bool vis;
   Eina_Bool res;

   res = _e_qp_srv_is_effect_running(qp);
   if (!res)
     return;

   res = _e_qp_srv_is_effect_finish_job_started(qp);
   if (res)
     {
        vis = _e_qp_srv_effect_final_visible_state_get(qp);
        if (vis == final_visible_state)
          return;

        _e_qp_srv_effect_finish_job_stop(qp);
     }
   _e_qp_srv_effect_finish_job_start(qp, final_visible_state);
}

static void
_e_qp_srv_effect_stop(E_Policy_Quickpanel *qp)
{
   Eina_Bool res;

   res = _e_qp_srv_is_effect_running(qp);
   if (!res)
     return;

   qp->effect.active = EINA_FALSE;

   res = _e_qp_srv_is_effect_finish_job_started(qp);
   if (res)
     _e_qp_srv_effect_finish_job_end(qp);
   else
     {
        e_zone_orientation_block_set(qp->ec->zone, "quickpanel-mover", EINA_FALSE);
        QP_VISIBLE_SET(qp, EINA_FALSE);
        E_FREE_FUNC(qp->mover, evas_object_del);
     }
}

inline static void
_e_qp_srv_effect_disable_unref(E_Policy_Quickpanel *qp)
{
   if (qp->effect.disable_ref > 0)
     qp->effect.disable_ref--;
}

static void
_e_qp_srv_effect_disable_ref(E_Policy_Quickpanel *qp)
{
   qp->effect.disable_ref++;
   if (qp->effect.disable_ref == 1)
     _e_qp_srv_effect_stop(qp);
}

static void
_region_obj_cb_gesture_start(void *data, Evas_Object *handler, int nfingers, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;
   E_Client *focused;
   E_Client *pos_ec = NULL;
   E_Desk *desk = NULL;
   int indi_x, indi_y, indi_w, indi_h;
   const int sensitivity = 50;
   Eina_Bool res;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   if (EINA_UNLIKELY(!qp->ec))
     return;

   if (e_object_is_del(E_OBJECT(qp->ec)))
     return;

   desk = e_desk_current_get(qp->ec->zone);
   if (desk)
     pos_ec = e_client_under_position_get(desk, x, y, NULL);

   if (!pos_ec)
     {
        ELOGF("QUICKPANEL", "NO visible client under pos(%d,%d)", NULL, x, y);
        return;
     }

   if (handler == qp->indi_obj)
     {
        int ix, iy, iw, ih;

        e_service_region_rectangle_get(qp->indi_obj, qp->rotation, &indi_x, &indi_y, &indi_w, &indi_h);
        switch (qp->rotation)
          {
           case E_POLICY_ANGLE_MAP_90:
              ix = pos_ec->x;
              iy = pos_ec->y;
              iw = indi_w + sensitivity;
              ih = pos_ec->h;
              break;
           case E_POLICY_ANGLE_MAP_180:
              ix = pos_ec->x;
              iy = pos_ec->y + pos_ec->h - (indi_h + sensitivity);
              iw = pos_ec->w;
              ih = indi_h + sensitivity;
              break;
           case E_POLICY_ANGLE_MAP_270:
              ix = pos_ec->x + pos_ec->w - (indi_w + sensitivity);
              iy = pos_ec->y;
              iw = indi_w + sensitivity;
              ih = pos_ec->h;
              break;
           case E_POLICY_ANGLE_MAP_0:
           default:
              ix = pos_ec->x;
              iy = pos_ec->y;
              iw = pos_ec->w;
              ih = indi_h + sensitivity;
              break;
          }

        if (!E_INSIDE(x, y, ix, iy, iw, ih))
          {
             ELOGF("QUICKPANEL", "NOT in indicator area", NULL);
             return;
          }

        if (_quickpanel_send_gesture_to_indicator())
          {
             ELOGF("QUICKPANEL", "SEND to change indicator state", NULL);
             return;
          }
     }

   // check quickpanel service window's scroll lock state
   if (qp->scroll_lock)
     return;

   /* Do not show and scroll the quickpanel window if the qp_client winodw
    * which is placed at the below of the quickpanel window doesn't want
    * to show and scroll the quickpanel window.
    */
   qp_client = _e_qp_client_ec_get(qp->below, (E_Quickpanel_Type)qp->type);
   if ((qp_client) && (!qp_client->hint.scrollable))
     return;

   res = _e_qp_srv_is_effect_running(qp);
   if (res)
     {
        INF("Already animated");
        return;
     }

   focused = e_client_focused_get();
   if (focused)
     e_policy_aux_message_send(focused, "quickpanel_state", "moving", NULL);

   if ((qp->below) &&
       (qp->below != focused))
     e_policy_aux_message_send(qp->below, "quickpanel_state", "moving", NULL);

   /* cancel touch events sended up to now */
   e_comp_wl_touch_cancel();

   _e_qp_srv_mouse_info_update(qp, x, y, timestamp);
   _e_qp_srv_effect_start(qp);
   _e_qp_srv_effect_update(qp, x, y);
}

static void
_region_obj_cb_gesture_move(void *data, Evas_Object *handler, int nfingers, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;
   Eina_Bool res;

   qp = data;
   res = _e_qp_srv_is_effect_running(qp);
   if (!res)
     return;

   _e_qp_srv_mouse_info_update(qp, x, y, timestamp);
   _e_qp_srv_effect_update(qp, x, y);
}

static void
_region_obj_cb_gesture_end(void *data EINA_UNUSED, Evas_Object *handler, int nfingers, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;
   Eina_Bool res, v;

   qp = data;
   res = _e_qp_srv_is_effect_running(qp);
   if (!res)
     return;

   v = _e_qp_srv_visibility_eval_by_mouse_info(qp);
   _e_qp_srv_effect_finish(qp, v);
}

static void
_quickpanel_free(E_Policy_Quickpanel *qp)
{
   ELOGF("QUICKPANEL", "Remove Client | qp %p", qp->ec, qp);

   evas_object_event_callback_del(qp->ec->frame, EVAS_CALLBACK_SHOW, _quickpanel_client_evas_cb_show);
   evas_object_event_callback_del(qp->ec->frame, EVAS_CALLBACK_HIDE, _quickpanel_client_evas_cb_hide);
   evas_object_event_callback_del(qp->ec->frame, EVAS_CALLBACK_MOVE, _quickpanel_client_evas_cb_move);

   if (qp->bg_rect)
     evas_object_del(qp->bg_rect);

   E_FREE_LIST(qp_clients, free);
   E_FREE_FUNC(qp->mover, evas_object_del);
   E_FREE_FUNC(qp->indi_obj, evas_object_del);
   E_FREE_FUNC(qp->handler_obj, evas_object_del);
   E_FREE_FUNC(qp->effect.animator, ecore_animator_del);
   E_FREE_FUNC(qp->idle_enterer, ecore_idle_enterer_del);
   E_FREE_LIST(qp->events, ecore_event_handler_del);
   E_FREE_LIST(qp->hooks, e_client_hook_del);
   E_FREE_LIST(qp->intercept_hooks, e_comp_object_intercept_hook_del);

   qp_services = eina_list_remove(qp_services, qp);
   E_FREE(qp);
}

static void
_quickpanel_hook_client_del(void *d, E_Client *ec)
{
   E_Policy_Quickpanel *qp;

   qp = d;
   if (EINA_UNLIKELY(!qp))
     return;

   if (!ec) return;

   if (qp->ec != ec)
     return;

   _quickpanel_free(qp);

   e_zone_orientation_force_update_del(ec->zone, ec);
}

static void
_quickpanel_client_evas_cb_show(void *data, Evas *evas, Evas_Object *obj, void *event)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   evas_object_show(qp->handler_obj);
   evas_object_raise(qp->handler_obj);
   evas_object_hide(qp->indi_obj);

   E_FREE_FUNC(qp->buf_change_hdlr, ecore_event_handler_del);
}

static Eina_Bool
_quickpanel_cb_buffer_change(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev;
   E_Client *ec;
   int x, y, w, h;
   Eina_Bool vis = EINA_FALSE;

   qp = data;
   if (!qp->ec)
     goto end;

   ev = event;
   ec = ev->ec;
   if (qp->ec != ec)
     goto end;

   /* render forcibly */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   /* make frame event */
   e_pixmap_image_clear(ec->pixmap, EINA_TRUE);

   /* use single buffer if qp service's ec is invisible */
   evas_object_geometry_get(ec->frame, &x, &y, &w, &h);

   if (E_INTERSECTS(x, y, w, h,
                    ec->zone->x,
                    ec->zone->y,
                    ec->zone->w,
                    ec->zone->h))
     vis = evas_object_visible_get(ec->frame);

   if (!vis)
     e_pixmap_buffer_clear(ec->pixmap, EINA_TRUE);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_quickpanel_client_evas_cb_hide(void *data, Evas *evas, Evas_Object *obj, void *event)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   evas_object_hide(qp->handler_obj);
   evas_object_show(qp->indi_obj);

   if (qp->need_scroll_update)
     _e_qp_client_scrollable_update(qp);

   e_pixmap_buffer_clear(qp->ec->pixmap, EINA_TRUE);
}

static void
_quickpanel_client_evas_cb_move(void *data, Evas *evas, Evas_Object *obj, void *event)
{
   E_Policy_Quickpanel *qp;
   int x, y, hx, hy;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   e_service_region_rectangle_get(qp->handler_obj, qp->rotation, &hx, &hy, NULL, NULL);
   evas_object_geometry_get(obj, &x, &y, NULL, NULL);
   evas_object_move(qp->handler_obj, x + hx, y + hy);
}

static void
_quickpanel_handler_obj_region_convert_set(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, int x, int y, int w, int h, int tx, int ty, int tw, int th)
{
   int nx, ny, nw, nh;

   switch (ridx)
     {
      case E_POLICY_ANGLE_MAP_0:
         nx = tx;
         ny = ty + th - h;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_90:
         nx = tx + tw - w;
         ny = ty;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_180:
         nx = tx;
         ny = ty;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_270:
         nx = tx;
         ny = ty;
         nw = w;
         nh = h;
         break;

      default:
         nx = tx;
         ny = ty + th - h;
         nw = w;
         nh = h;
         break;
     }

   e_service_region_rectangle_set(qp->handler_obj, ridx, nx, ny, nw, nh);
   ELOGF("QUICKPANEL", "handler obj:%p, angle:%d, geo(%d,%d,%d,%d)", NULL, qp->handler_obj, ridx, nx, ny, nw, nh);
}

static void
_quickpanel_handler_rect_add(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, int x, int y, int w, int h, int tx, int ty, int tw, int th)
{
   E_Client *ec;
   Evas_Object *obj;

   ec = qp->ec;

   ELOGF("QUICKPANEL", "Handler Geo Set | x %d, y %d, w %d, h %d",
         NULL, x, y, w, h);

   if (qp->handler_obj)
     goto end;

   obj = e_service_region_object_new(ec);
   evas_object_name_set(obj, "qp::handler_obj");
   if (!obj)
     return;

   e_service_region_gesture_set(obj,
                                POL_GESTURE_TYPE_NONE,
                                1,
                                _region_obj_cb_gesture_start,
                                _region_obj_cb_gesture_move,
                                _region_obj_cb_gesture_end, qp);

   /* Add handler object to smart member to follow the client's stack */
   evas_object_smart_member_add(obj, ec->frame);
   evas_object_propagate_events_set(obj, 0);
   if (evas_object_visible_get(ec->frame))
     evas_object_show(obj);

   qp->handler_obj = obj;

end:
   _quickpanel_handler_obj_region_convert_set(qp, ridx, x, y, w, h, tx, ty, tw, th);
}

void _quickpanel_indi_obj_region_convert_set(E_Policy_Quickpanel *qp, int angle, int x, int y, int w, int h, int tx, int ty, int tw, int th)
{
   int nx, ny, nw, nh;

   if ((w <= 0) || (h <= 0)) return;

   switch (angle)
     {
      case E_POLICY_ANGLE_MAP_0:
         nx = tx;
         ny = ty;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_90:
         nx = tx;
         ny = ty;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_180:
         nx = tx;
         ny = ty + th - h;
         nw = w;
         nh = h;
         break;

      case E_POLICY_ANGLE_MAP_270:
         nx = tx + tw - w;
         ny = ty;
         nw = w;
         nh = h;
         break;

      default:
         nx = tx;
         ny = ty;
         nw = w;
         nh = h;
         break;
     }

   e_service_region_rectangle_set(qp->indi_obj, angle, nx, ny, nw, nh);
   ELOGF("QUICKPANEL", "indicator obj:%p, angle:%d, geo(%d,%d,%d,%d)", NULL, qp->indi_obj, angle, nx, ny, nw, nh);
}

static void
_quickpanel_handler_region_set(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, Eina_Tiler *tiler)
{
   Eina_Iterator *it;
   Eina_Rectangle *r;
   E_Desk *desk = NULL;
   int tx, ty, tw, th;

   /* FIXME supported single rectangle, not tiler */

   tx = qp->ec->zone->x;
   ty = qp->ec->zone->y;
   tw = qp->ec->zone->w;
   th = qp->ec->zone->h;

   if (e_config->use_desk_smart_obj)
     {
        desk = e_desk_current_get(qp->ec->zone);
        if (desk)
          {
             tx = desk->geom.x;
             ty = desk->geom.y;
             tw = desk->geom.w;
             th = desk->geom.h;
          }
     }

   it = eina_tiler_iterator_new(tiler);
   EINA_ITERATOR_FOREACH(it, r)
     {
        _quickpanel_handler_rect_add(qp, ridx, r->x, r->y, r->w, r->h, tx, ty, tw, th);
        _quickpanel_indi_obj_region_convert_set(qp, ridx, r->x, r->y, r->w, r->h, tx, ty, tw, th);
        break;
     }
   eina_iterator_free(it);
}

static void
_quickpanel_contents_region_set(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, Eina_Tiler *tiler)
{
   // Do Something
   Eina_Iterator *it;
   Eina_Rectangle *r;

   it = eina_tiler_iterator_new(tiler);
   EINA_ITERATOR_FOREACH(it, r)
     {
        qp->geom.x = r->x;
        qp->geom.y = r->y;
        qp->geom.w = r->w;
        qp->geom.h = r->h;
     }
   eina_iterator_free(it);
}

static void
_e_qp_srv_visible_set(E_Policy_Quickpanel *qp, Eina_Bool vis)
{
   Eina_Bool res;

   res = _e_qp_client_scrollable_update(qp);
   if (!res) return;

   if (qp->effect.disable_ref)
     {
        QP_VISIBLE_SET(qp, vis);
        return;
     }

   res = _e_qp_srv_is_effect_running(qp);
   if (res)
     _e_qp_srv_effect_finish(qp, vis);
   else if ((qp->ec) && ((qp->ec->visible) || (vis)))
     {
        _e_qp_srv_effect_start(qp);
        _e_qp_srv_effect_update(qp, qp->effect.x, qp->effect.y);
        _e_qp_srv_effect_finish(qp, vis);
     }
}

static Eina_Bool
_quickpanel_cb_rotation_begin(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev = event;
   E_Client *ec;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   ec = ev->ec;
   if (EINA_UNLIKELY(!ec))
     goto end;

   if (qp->ec != ec)
     goto end;

   E_FREE_FUNC(qp->mover, evas_object_del);

   _e_qp_srv_effect_disable_ref(qp);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_rotation_cancel(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev = event;
   E_Client *ec;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   ec = ev->ec;
   if (EINA_UNLIKELY(!ec))
     goto end;

   if (qp->ec != ec)
     goto end;

   _e_qp_srv_effect_disable_unref(qp);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_rotation_done(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev = event;
   E_Client *ec;
   E_QP_Client *qp_client;
   Eina_List *l;
   Eina_Bool vis;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   ec = ev->ec;
   if (EINA_UNLIKELY(!ec))
     goto end;

   if (qp->ec != ec)
     goto end;

   qp->rotation = e_policy_angle_map(ec->e.state.rot.ang.curr);

   vis = evas_object_visible_get(ec->frame);
   switch (qp->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         qp->effect.x = vis ? ec->zone->w : 0;
         break;
      case E_POLICY_ANGLE_MAP_180:
         qp->effect.y = vis ? 0 : ec->zone->h;
         break;
      case E_POLICY_ANGLE_MAP_270:
         qp->effect.x = vis ? 0 : ec->zone->w;
         break;
      default:
         qp->effect.y = vis ? ec->zone->h : 0;
         break;
     }

   _e_qp_srv_effect_disable_unref(qp);

   /* send orientation event to only client with the same type of quickpanel service */
   EINA_LIST_FOREACH(qp_clients, l, qp_client)
     {
        if (qp->type == (E_Service_Quickpanel_Type)qp_client->type)
          e_tzsh_qp_state_orientation_update(qp_client->ec,
                                             qp->rotation,
                                             qp_client->type);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

/* NOTE: if the state(show/hide/stack) of windows which are stacked below
 * quickpanel is changed, we close the quickpanel.
 * the most major senario is that quickpanel should be closed when WiFi popup to
 * show the available connection list is shown by click the button on
 * the quickpanel to turn on the WiFi.
 * @see  _quickpanel_cb_client_show(),
 *       _quickpanel_cb_client_hide()
 *       _quickpanel_cb_client_move()
 *       _quickpanel_cb_client_stack()
 *       _quickpanel_cb_client_remove()
 *       _quickpanel_idle_enter()
 */
static E_Client *
_quickpanel_below_visible_client_get(E_Policy_Quickpanel *qp)
{
   E_Client *ec;
   int zx, zy, zw, zh;

   zx = qp->ec->zone->x;
   zy = qp->ec->zone->y;
   zw = qp->ec->zone->w;
   zh = qp->ec->zone->h;

   for (ec = e_client_below_get(qp->ec); ec; ec = e_client_below_get(ec))
     {
        if (!ec->visible) continue;
        if (e_policy_client_is_keyboard(ec)) continue;
        if (!E_INTERSECTS(ec->x, ec->y, ec->w, ec->h, zx, zy, zw, zh)) continue;
        return ec;
     }

   return NULL;
}

static void
_quickpanel_below_change_eval(void *data, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   ev = event;
   if (EINA_UNLIKELY((!ev) || (!ev->ec)))
     return;

   if (e_policy_client_is_cursor(ev->ec))
     return;

   qp->changes.below = EINA_TRUE;
   _changed = EINA_TRUE;
}

static Eina_Bool
_quickpanel_cb_client_show(void *data, int type, void *event)
{
   _quickpanel_below_change_eval(data, event);
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_hide(void *data, int type, void *event)
{
   _quickpanel_below_change_eval(data, event);
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_move(void *data, int type, void *event)
{
   _quickpanel_below_change_eval(data, event);
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_stack(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev;

   qp = data;
   EINA_SAFETY_ON_NULL_GOTO(qp, end);

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   qp->stacking = ev->ec;

   DBG("Stacking Client '%s'(%p)",
       ev->ec->icccm.name ? ev->ec->icccm.name : "", ev->ec);

   if (evas_object_visible_get(ev->ec->frame))
     _quickpanel_below_change_eval(data, event);
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_remove(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev;

   qp = data;
   EINA_SAFETY_ON_NULL_GOTO(qp, end);

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   if (qp->stacking == ev->ec)
     qp->stacking = NULL;

   if (qp->below == ev->ec)
     qp->below = NULL;

   if (!stopping)
     _quickpanel_below_change_eval(data, event);
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_focus_in(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Client *ev;
   E_Client *ec;

   qp = data;
   EINA_SAFETY_ON_NULL_GOTO(qp, end);

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_GOTO(ec, end);

   if (ec == qp->ec)
     goto end;

   if (ec->visible)
     {
        DBG("Focus changed to '%s'(%zx), Hide QP",
            ec->icccm.name ? ec->icccm.name : "", e_client_util_win_get(ec));
        e_service_quickpanel_hide(qp->ec);
     }
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_desk_geometry_change(void *data, int type, void *event)
{
   E_Policy_Quickpanel *qp;
   E_Event_Desk_Geometry_Change *ev;
   E_Desk *desk = NULL;
   int x, y, w, h;
   int angle;

   qp = data;
   EINA_SAFETY_ON_NULL_GOTO(qp, end);

   ev = event;
   EINA_SAFETY_ON_NULL_GOTO(ev, end);

   desk = e_desk_current_get(qp->ec->zone);
   EINA_SAFETY_ON_NULL_GOTO(desk, end);

   for (angle = E_POLICY_ANGLE_MAP_0; angle < E_POLICY_ANGLE_MAP_NUM; angle++)
     {
        e_service_region_rectangle_get(qp->indi_obj, angle, &x, &y, &w, &h);
        if ((w > 0) && (h > 0))
          _quickpanel_indi_obj_region_convert_set(qp, angle, x, y, w, h, ev->desk->geom.x, ev->desk->geom.y, ev->desk->geom.w, ev->desk->geom.h);

        e_service_region_rectangle_get(qp->handler_obj, angle, &x, &y, &w, &h);
        if ((w > 0) && (h > 0))
          _quickpanel_handler_obj_region_convert_set(qp, angle, x, y, w, h, ev->desk->geom.x, ev->desk->geom.y, ev->desk->geom.w, ev->desk->geom.h);
     }

   evas_object_move(qp->ec->frame, ev->desk->geom.x, ev->desk->geom.y);

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Evas_Object *
_quickpanel_indicator_object_new(E_Policy_Quickpanel *qp)
{
   Evas_Object *indi_obj;

   indi_obj = e_service_region_object_new(qp->ec);
   evas_object_name_set(indi_obj, "qp::indicator_obj");
   if (!indi_obj)
     return NULL;

   evas_object_repeat_events_set(indi_obj, EINA_FALSE);
   /* FIXME: make me move to explicit layer something like POL_LAYER */
   evas_object_layer_set(indi_obj, EVAS_LAYER_MAX - 1);

   e_service_region_gesture_set(indi_obj,
                                POL_GESTURE_TYPE_LINE,
                                1,
                                _region_obj_cb_gesture_start,
                                _region_obj_cb_gesture_move,
                                _region_obj_cb_gesture_end, qp);

   evas_object_show(indi_obj);

   if (e_config->qp_add_on_desk_smart)
     e_desk_smart_member_add(qp->ec->desk, indi_obj);

   return indi_obj;
}

static Eina_Bool
_quickpanel_idle_enter(void *data)
{
   E_Policy_Quickpanel *qp;

   if (!_changed)
     goto end;
   _changed = EINA_FALSE;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   if (qp->changes.below)
     {
        E_Client *below;
        E_Client *below_old;

        below = _quickpanel_below_visible_client_get(qp);
        if (qp->below != below)
          {
             DBG("qp->below '%s'(%p) new_below '%s'(%p)\n",
                 qp->below ? (qp->below->icccm.name ? qp->below->icccm.name : "") : "",
                 qp->below,
                 below ? (below->icccm.name ? below->icccm.name : "") : "",
                 below);

             below_old = qp->below;
             qp->below = below;

             /* QUICKFIX
              * hide the quickpanel, if below client is the stacking client.
              * it means to find out whether or not it was launched.
              */
             if ((qp->below) &&
                 (qp->below->icccm.accepts_focus))
               {
                  if ((qp->stacking == below) &&
                      (qp->ec->visible))
                    {
                       if (below_old)
                         e_policy_aux_message_send(below_old, "quickpanel_state", "hidden", NULL);

                       e_service_quickpanel_hide(qp->ec);
                    }
               }

             _e_qp_client_scrollable_update(qp);
          }

        qp->changes.below = EINA_FALSE;
     }

end:
   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_quickpanel_intercept_hook_show(void *data, E_Client *ec)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   if (qp->ec != ec)
     goto end;

   if (qp->show_block)
     {
        ec->visible = EINA_FALSE;
        return EINA_FALSE;
     }

end:
   return EINA_TRUE;
}

static E_QP_Client *
_e_qp_client_ec_get(E_Client *ec, E_Quickpanel_Type type)
{
   E_QP_Client *qp_client = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(qp_clients, l, qp_client)
     {
        if ((qp_client->ec == ec) && (qp_client->type == type))
          return qp_client;
     }

   return qp_client;
}

/* return value
 *  EINA_TRUE : user can scroll the QP.
 *  EINA_FALSE: user can't scroll QP since below window doesn't want.
 */
static Eina_Bool
_e_qp_client_scrollable_update(E_Policy_Quickpanel *qp)
{
   Eina_Bool res = EINA_TRUE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   if (qp->ec->visible)
     {
        qp->need_scroll_update = EINA_TRUE;
        return EINA_TRUE;
     }
   qp->need_scroll_update = EINA_FALSE;

   if (!qp->below)
     {
        evas_object_pass_events_set(qp->handler_obj, EINA_FALSE);
        evas_object_pass_events_set(qp->indi_obj, EINA_FALSE);
        return EINA_TRUE;
     }

   return res;
}


#undef E_CLIENT_HOOK_APPEND
#define E_CLIENT_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Client_Hook *_h;                 \
       _h = e_client_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

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

/* window for quickpanel service */
EINTERN void
e_service_quickpanel_client_add(E_Client *ec, E_Service_Quickpanel_Type type)
{
   E_Policy_Quickpanel *qp = NULL;

   BACKEND_FUNC_CALL(quickpanel_client_add, ec, type);

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   qp = E_NEW(E_Policy_Quickpanel, 1);
   EINA_SAFETY_ON_NULL_RETURN(qp);

   ELOGF("QUICKPANEL", "Set Client | qp %p", ec, qp);

   qp->ec = ec;
   qp->show_block = EINA_TRUE;
   qp->effect.type = E_SERVICE_QUICKPANEL_EFFECT_TYPE_SWIPE; /* default effect type */
   qp->below = _quickpanel_below_visible_client_get(qp);
   qp->type = type;

   if (type == E_SERVICE_QUICKPANEL_TYPE_SYSTEM_DEFAULT)
     {
        qp->indi_obj = _quickpanel_indicator_object_new(qp);
        EINA_SAFETY_ON_NULL_GOTO(qp->indi_obj, indi_err);

        e_client_window_role_set(ec, "quickpanel_system_default");
     }
   else if (type == E_SERVICE_QUICKPANEL_TYPE_CONTEXT_MENU)
     {
        /* don't support swipe type of effect for the qp context menu in public
         * you have to make your own qp module and provide backend functions
         * if you want to change type of effect of qp context menu
         */
        qp->effect.type = E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE;
        e_client_window_role_set(ec, "quickpanel_context_menu");
     }

   e_comp_screen_rotation_ignore_output_transform_send(qp->ec, EINA_TRUE);

   // set quickpanel layer
   if (E_POLICY_QUICKPANEL_LAYER != evas_object_layer_get(ec->frame))
     evas_object_layer_set(ec->frame, E_POLICY_QUICKPANEL_LAYER);
   ec->layer = E_POLICY_QUICKPANEL_LAYER;

   // set skip iconify
   ec->exp_iconify.skip_iconify = 1;

   // disable effect
   e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, EINA_TRUE);

   qp->geom.x = ec->x;
   qp->geom.y = ec->y;
   qp->geom.w = ec->w;
   qp->geom.h = ec->h;

   // bg rect
   if (e_config->qp_use_bg_rect)
     {
        Evas_Object *o;
        o = evas_object_rectangle_add(e_comp->evas);

        qp->bg_rect = o;
        evas_object_layer_set(o, E_POLICY_QUICKPANEL_LAYER);
        evas_object_name_set(o, "qp::bg_rect");
        evas_object_move(o, 0, 0);
        evas_object_resize(o, ec->zone->w, ec->zone->h);
        evas_object_color_set(o, 0, 0, 0, 0);
        evas_object_lower(o);
     }
   else
     qp->bg_rect = NULL;

   /* add quickpanel to force update list of zone */
   e_zone_orientation_force_update_add(ec->zone, ec);

   QP_HIDE(qp);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,                    _quickpanel_client_evas_cb_show,     qp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,                    _quickpanel_client_evas_cb_hide,     qp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,                    _quickpanel_client_evas_cb_move,     qp);
   E_CLIENT_HOOK_APPEND(qp->hooks,           E_CLIENT_HOOK_DEL,                     _quickpanel_hook_client_del,         qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN,  _quickpanel_cb_rotation_begin,       qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL, _quickpanel_cb_rotation_cancel,      qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_ROTATION_CHANGE_END,    _quickpanel_cb_rotation_done,        qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_SHOW,                   _quickpanel_cb_client_show,          qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_HIDE,                   _quickpanel_cb_client_hide,          qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_MOVE,                   _quickpanel_cb_client_move,          qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_STACK,                  _quickpanel_cb_client_stack,         qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_REMOVE,                 _quickpanel_cb_client_remove,        qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_BUFFER_CHANGE,          _quickpanel_cb_buffer_change,        qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_CLIENT_FOCUS_IN,               _quickpanel_cb_client_focus_in,      qp);
   E_LIST_HANDLER_APPEND(qp->events,         E_EVENT_DESK_GEOMETRY_CHANGE,          _quickpanel_cb_desk_geometry_change, qp);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(qp->intercept_hooks, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _quickpanel_intercept_hook_show, qp);

   qp->idle_enterer = ecore_idle_enterer_add(_quickpanel_idle_enter, qp);

   qp_services = eina_list_append(qp_services, qp);

   return;

indi_err:
   free(qp);
}

EINTERN void
e_service_quickpanel_client_del(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   Eina_List *l, *ll;

   BACKEND_FUNC_CALL(quickpanel_client_del, ec);

   EINA_LIST_FOREACH_SAFE(qp_services, l, ll, qp)
     {
        if (qp->ec == ec)
          {
             _quickpanel_free(qp);
             break;
          }
     }
}

EINTERN void
e_service_quickpanel_effect_type_set(E_Client *ec, E_Service_Quickpanel_Effect_Type type)
{
   E_Policy_Quickpanel *qp;

   BACKEND_FUNC_CALL(quickpanel_effect_type_set, ec, type);

   qp = _quickpanel_service_get(ec);
   if (!qp)
     return;

   if ((qp->ec != ec))
     return;

   if (qp->effect.type == type)
     return;

   qp->effect.type = type;
   switch (type)
     {
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_SWIPE:
         ec->lock_client_location = 1;
         e_policy_allow_user_geometry_set(ec, EINA_FALSE);
         if ((ec->maximized == E_MAXIMIZE_NONE) &&
             (qp->saved_maximize != E_MAXIMIZE_NONE))
           e_client_maximize(ec, qp->saved_maximize);
         break;
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_MOVE:
         ec->lock_client_location = 0;
         e_policy_allow_user_geometry_set(ec, EINA_TRUE);
         if (ec->maximized != E_MAXIMIZE_NONE)
           {
              qp->saved_maximize = ec->maximized;
              e_client_unmaximize(ec, ec->maximized);
           }
         e_client_util_move_resize_without_frame(ec, 0, 0, ec->zone->w, ec->zone->h);
         break;
      case E_SERVICE_QUICKPANEL_EFFECT_TYPE_APP_CUSTOM:
         WRN("APP_CUSTOM effect type is not supported yet");
         /* TODO */
         break;
      default:
         ERR("Unkown effect type");
         break;
     }
}

EINTERN void
e_service_quickpanel_scroll_lock_set(E_Client *ec, Eina_Bool lock)
{
   E_Policy_Quickpanel *qp = NULL;

   BACKEND_FUNC_CALL(quickpanel_scroll_lock_set, ec, lock);

   qp = _quickpanel_service_get(ec);
   if (!qp) return;

   if (qp->scroll_lock == lock)
     return;

   ELOGF("QUICKPANEL", "Scroll lock is set to %d", NULL, lock);
   qp->scroll_lock = lock;
}

EINTERN Eina_Bool
e_service_quickpanel_region_set(E_Client *ec, int type, int angle, Eina_Tiler *tiler)
{
   E_Policy_Quickpanel *qp;
   E_Policy_Angle_Map ridx;

   BACKEND_FUNC_CALL_RET(quickpanel_region_set, ec, type, angle, tiler);

   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), EINA_FALSE);

   qp = _quickpanel_service_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);

   ridx = e_policy_angle_map(angle);
   if (type == E_QUICKPANEL_REGION_TYPE_HANDLER)
     _quickpanel_handler_region_set(qp, ridx, tiler);
   else if (type == E_QUICKPANEL_REGION_TYPE_CONTENTS)
     _quickpanel_contents_region_set(qp, ridx, tiler);

   return EINA_TRUE;
}

EINTERN void
e_service_quickpanel_show(E_Client *ec)
{
   E_Policy_Quickpanel *qp;

   BACKEND_FUNC_CALL(quickpanel_show, ec);

   qp = _quickpanel_service_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_srv_visible_set(qp, EINA_TRUE);
}

EINTERN void
e_service_quickpanel_hide(E_Client *ec)
{
   E_Policy_Quickpanel *qp;

   BACKEND_FUNC_CALL(quickpanel_hide, ec);

   qp = _quickpanel_service_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_srv_visible_set(qp, EINA_FALSE);
}

/* check if at least one quickpanel is visible */
EINTERN Eina_Bool
e_qps_visible_get(void)
{
   E_Policy_Quickpanel *qp;
   E_Client *ec;
   Eina_Bool vis;
   int x, y, w, h;
   Eina_List *l;

   BACKEND_FUNC_CALL_RET(qps_visible_get);

   EINA_LIST_FOREACH(qp_services, l, qp)
     {
        ec = qp->ec;
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;

        evas_object_geometry_get(ec->frame,
                                 &x, &y, &w, &h);

        if (E_INTERSECTS(x, y, w, h,
                         ec->zone->x,
                         ec->zone->y,
                         ec->zone->w,
                         ec->zone->h))
          {
             vis = evas_object_visible_get(ec->frame);
             if (vis) return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_qp_visible_get(E_Client *ec, E_Quickpanel_Type type)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;
   Eina_Bool vis = EINA_FALSE;
   int x, y, w, h;

   BACKEND_FUNC_CALL_RET(qp_visible_get, ec, type);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(ec)), EINA_FALSE);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   qp = _quickpanel_get_with_client_type(qp_client);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);

   evas_object_geometry_get(qp->ec->frame, &x, &y, &w, &h);

   if (E_INTERSECTS(x, y, w, h, qp->ec->zone->x, qp->ec->zone->y, qp->ec->zone->w, qp->ec->zone->h))
     vis = evas_object_visible_get(qp->ec->frame);

   return vis;
}

EINTERN int
e_qp_orientation_get(E_Client *ec, E_Quickpanel_Type type)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL_RET(qp_orientation_get, ec, type);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   qp = _quickpanel_get_with_client_type(qp_client);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, E_POLICY_ANGLE_MAP_0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, E_POLICY_ANGLE_MAP_0);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), E_POLICY_ANGLE_MAP_0);

   return qp->rotation;
}

EINTERN void
e_qp_client_add(E_Client *ec, E_Quickpanel_Type type)
{
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL(qp_client_add, ec, type);

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(ec)));

   qp_client = _e_qp_client_ec_get(ec, type);
   if (qp_client)
     {
        qp_client->ref++;
        return;
     }

   qp_client = E_NEW(E_QP_Client, 1);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);

   qp_client->ec = ec;
   qp_client->ref = 1;
   qp_client->hint.vis = EINA_TRUE;
   qp_client->hint.scrollable = EINA_TRUE;
   qp_client->type = type;

   qp_clients = eina_list_append(qp_clients, qp_client);
}

EINTERN void
e_qp_client_del(E_Client *ec, E_Quickpanel_Type type)
{
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL(qp_client_del, ec, type);

   EINA_SAFETY_ON_NULL_RETURN(ec);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);

   qp_client->ref--;
   if (qp_client->ref != 0) return;

   qp_clients = eina_list_remove(qp_clients, qp_client);

   E_FREE(qp_client);
}

EINTERN void
e_qp_client_show(E_Client *ec, E_Quickpanel_Type type)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL(qp_client_show, ec, type);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);
   EINA_SAFETY_ON_FALSE_RETURN(qp_client->hint.scrollable);

   qp = _quickpanel_get_with_client_type(qp_client);
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_srv_visible_set(qp, EINA_TRUE);
}

EINTERN void
e_qp_client_hide(E_Client *ec, E_Quickpanel_Type type)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL(qp_client_hide, ec, type);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);
   EINA_SAFETY_ON_FALSE_RETURN(qp_client->hint.scrollable);

   qp = _quickpanel_get_with_client_type(qp_client);
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_srv_visible_set(qp, EINA_FALSE);
}

EINTERN Eina_Bool
e_qp_client_scrollable_set(E_Client *ec, E_Quickpanel_Type type, Eina_Bool set)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL_RET(qp_client_scrollable_set, ec, type, set);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   if (qp_client->hint.scrollable != set)
     qp_client->hint.scrollable = set;

   qp = _quickpanel_get_with_client_type(qp_client);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   _e_qp_client_scrollable_update(qp);

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_qp_client_scrollable_get(E_Client *ec, E_Quickpanel_Type type)
{
   E_QP_Client *qp_client;

   BACKEND_FUNC_CALL_RET(qp_client_scrollable_get, ec, type);

   qp_client = _e_qp_client_ec_get(ec, type);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   return qp_client->hint.scrollable;
}

E_API Eina_Bool
e_service_quickpanel_module_func_set(E_QP_Mgr_Funcs *fp)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_config->use_module_srv.qp, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL((qp_mgr_funcs == NULL), EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fp, EINA_FALSE);

   qp_mgr_funcs = E_NEW(E_QP_Mgr_Funcs, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_mgr_funcs, EINA_FALSE);

   qp_mgr_funcs->quickpanel_client_add = fp->quickpanel_client_add;
   qp_mgr_funcs->quickpanel_client_del = fp->quickpanel_client_del;
   qp_mgr_funcs->quickpanel_show = fp->quickpanel_show;
   qp_mgr_funcs->quickpanel_hide = fp->quickpanel_hide;
   qp_mgr_funcs->quickpanel_region_set = fp->quickpanel_region_set;
   qp_mgr_funcs->quickpanel_effect_type_set = fp->quickpanel_effect_type_set;
   qp_mgr_funcs->quickpanel_scroll_lock_set = fp->quickpanel_scroll_lock_set;
   qp_mgr_funcs->qps_visible_get = fp->qps_visible_get;
   qp_mgr_funcs->qp_visible_get = fp->qp_visible_get;
   qp_mgr_funcs->qp_orientation_get = fp->qp_orientation_get;
   qp_mgr_funcs->qp_client_add = fp->qp_client_add;
   qp_mgr_funcs->qp_client_del = fp->qp_client_del;
   qp_mgr_funcs->qp_client_show = fp->qp_client_show;
   qp_mgr_funcs->qp_client_hide = fp->qp_client_hide;
   qp_mgr_funcs->qp_client_scrollable_set = fp->qp_client_scrollable_set;
   qp_mgr_funcs->qp_client_scrollable_get = fp->qp_client_scrollable_get;

   return EINA_TRUE;
}

E_API Eina_Bool
e_service_quickpanel_module_func_unset(void)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_config->use_module_srv.qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_mgr_funcs, EINA_FALSE);

   E_FREE(qp_mgr_funcs);

   return EINA_TRUE;
}

E_API Eina_List *
e_service_quickpanels_get(void)
{
   Eina_List *l, *list = NULL;
   E_Policy_Quickpanel *qp;

   EINA_LIST_FOREACH(qp_services, l, qp)
     {
        list = eina_list_append(list, qp->ec);
     }

   return list;
}
