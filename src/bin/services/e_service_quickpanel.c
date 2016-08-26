#include "e.h"
#include "services/e_service_quickpanel.h"
#include "services/e_service_gesture.h"
#include "services/e_service_region.h"
#include "e_policy_wl.h"

#define SMART_NAME            "quickpanel_object"
#define INTERNAL_ENTRY                    \
   Mover_Data *md;                        \
   md = evas_object_smart_data_get(obj)

#define QP_SHOW(EC)              \
do                               \
{                                \
   EC->visible = EINA_TRUE;      \
   evas_object_show(EC->frame);  \
} while (0)

#define QP_HIDE(EC)              \
do                               \
{                                \
   EC->visible = EINA_FALSE;     \
   evas_object_hide(EC->frame);  \
} while (0)

#define QP_VISIBLE_SET(EC, VIS)  \
do                               \
{                                \
   if (VIS) QP_SHOW(EC);         \
   else     QP_HIDE(EC);         \
} while(0)

typedef struct _E_Policy_Quickpanel E_Policy_Quickpanel;
typedef struct _Mover_Data Mover_Data;
typedef struct _Mover_Effect_Data Mover_Effect_Data;

typedef struct _E_QP_Client E_QP_Client;

struct _E_Policy_Quickpanel
{
   E_Client *ec;
   E_Client *below;
   E_Client *stacking;
   Evas_Object *mover;
   Evas_Object *indi_obj;
   Evas_Object *handler_obj;

   Eina_List *intercept_hooks;
   Eina_List *hooks;
   Eina_List *events;
   Ecore_Idle_Enterer *idle_enterer;
   Ecore_Event_Handler *buf_change_hdlr;

   struct
   {
      Eina_Bool below;
   } changes;

   E_Policy_Angle_Map rotation;

   Eina_Bool show_block;

   Eina_List *clients; /* list of E_QP_Client */
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
   E_Policy_Angle_Map rotation;

   struct
   {
      Ecore_Animator *animator;
      Mover_Effect_Data *data;
      int x, y;
      unsigned int timestamp;
      float accel;
      Eina_Bool visible;
   } effect_info;
};

struct _Mover_Effect_Data
{
   Ecore_Animator *animator;
   Evas_Object *mover;
   int from;
   int to;
   Eina_Bool visible : 1;
};

struct _E_QP_Client
{
   E_Client *ec;
   struct
   {
      Eina_Bool vis;
      Eina_Bool scrollable;
   } hint;
};

static E_Policy_Quickpanel *_pol_quickpanel = NULL;
static Evas_Smart *_mover_smart = NULL;
static Eina_Bool _changed = EINA_FALSE;

static E_QP_Client * _e_qp_client_ec_get(E_Client *ec);
static Eina_Bool     _e_qp_client_scrollable_update(void);

static E_Policy_Quickpanel *
_quickpanel_get()
{
   return _pol_quickpanel;
}

static void
_mover_intercept_show(void *data, Evas_Object *obj)
{
   Mover_Data *md;
   E_Client *ec;
   Evas *e;

   md = data;
   md->qp->show_block = EINA_FALSE;

   ec = md->ec;
   QP_SHOW(ec);

   /* force update */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

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
   e_layout_child_move(md->handler_mirror_obj, ec->x, ec->y);
   e_layout_child_resize(md->handler_mirror_obj, ec->w, ec->h);
   evas_object_show(md->handler_mirror_obj);

   // create handler_clip
   md->handler_clip = evas_object_rectangle_add(e);
   e_layout_pack(md->qp_layout_obj, md->handler_clip);
   e_layout_child_move(md->handler_clip, md->handler_rect.x, md->handler_rect.y);
   e_layout_child_resize(md->handler_clip, md->handler_rect.w, md->handler_rect.h);
   evas_object_color_set(md->handler_clip, 255, 255, 255, 255);
   evas_object_show(md->handler_clip);
   evas_object_clip_set(md->handler_mirror_obj, md->handler_clip);

   evas_object_show(obj);
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
   evas_object_layer_set(obj, EVAS_LAYER_MAX - 1); // EVAS_LAYER_MAX :L cursor layer
   evas_object_intercept_show_callback_add(obj, _mover_intercept_show, md);
}

static void
_mover_smart_del(Evas_Object *obj)
{
   E_Client *ec;

   INTERNAL_ENTRY;

   ec = md->qp->ec;
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

   md->qp->mover = NULL;

   e_zone_orientation_block_set(md->qp->ec->zone, "quickpanel-mover", EINA_FALSE);
   e_comp_override_del();

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

static Eina_Bool
_mover_obj_handler_move(Mover_Data *md, int x, int y)
{
   E_Zone *zone;
   E_Client *ec;

   ec = md->ec;
   zone = ec->zone;
   switch (md->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         if ((x + md->handler_rect.w) > zone->w) return EINA_FALSE;

         md->handler_rect.x = x;
         e_layout_child_resize(md->base_clip, md->handler_rect.x, ec->h);
         e_layout_child_move(md->handler_mirror_obj, md->handler_rect.x - ec->w + md->handler_rect.w, md->handler_rect.y);
         e_layout_child_move(md->handler_clip, md->handler_rect.x, md->handler_rect.y);
         break;
      case E_POLICY_ANGLE_MAP_180:
         if ((y - md->handler_rect.h) < 0) return EINA_FALSE;

         md->handler_rect.y = y;
         e_layout_child_move(md->base_clip, md->handler_rect.x, md->handler_rect.y);
         e_layout_child_resize(md->base_clip, ec->w, ec->h - md->handler_rect.y);
         e_layout_child_move(md->handler_mirror_obj, md->handler_rect.x, md->handler_rect.y - md->handler_rect.h);
         e_layout_child_move(md->handler_clip, md->handler_rect.x, md->handler_rect.y - md->handler_rect.h);
         break;
      case E_POLICY_ANGLE_MAP_270:
         if ((x - md->handler_rect.w) < 0) return EINA_FALSE;

         md->handler_rect.x = x;
         e_layout_child_move(md->base_clip, md->handler_rect.x, md->handler_rect.y);
         e_layout_child_resize(md->base_clip, ec->w - md->handler_rect.x, ec->h);
         e_layout_child_move(md->handler_mirror_obj, md->handler_rect.x - md->handler_rect.w, md->handler_rect.y);
         e_layout_child_move(md->handler_clip, md->handler_rect.x - md->handler_rect.w, md->handler_rect.y);
         break;
      default:
        if ((y + md->handler_rect.h) > zone->h) return EINA_FALSE;

        md->handler_rect.y = y;
        e_layout_child_resize(md->base_clip, ec->w, md->handler_rect.y);
        e_layout_child_move(md->handler_mirror_obj, md->handler_rect.x, md->handler_rect.y - ec->h + md->handler_rect.h);
        e_layout_child_move(md->handler_clip, md->handler_rect.x, md->handler_rect.y);
     }

   return EINA_TRUE;
}

static Evas_Object *
_mover_obj_new(E_Policy_Quickpanel *qp)
{
   Evas_Object *mover;
   Mover_Data *md;
   int x, y, w, h;

   /* Pause changing zone orientation during mover object is working. */
   e_zone_orientation_block_set(qp->ec->zone, "quickpanel-mover", EINA_TRUE);
   e_comp_override_add();

   _mover_smart_init();
   mover = evas_object_smart_add(evas_object_evas_get(qp->ec->frame), _mover_smart);

   /* Should setup 'md' before call evas_object_show() */
   md = evas_object_smart_data_get(mover);
   md->qp = qp;
   md->ec = qp->ec;
   md->rotation = qp->rotation;

   e_service_region_rectangle_get(qp->handler_obj, qp->rotation, &x, &y, &w, &h);
   EINA_RECTANGLE_SET(&md->handler_rect, x, y, w, h);

   evas_object_move(mover, 0, 0);
   evas_object_resize(mover, qp->ec->w, qp->ec->h);
   evas_object_show(mover);

   qp->mover = mover;
   qp->show_block = EINA_FALSE;

   return mover;
}

static Evas_Object *
_mover_obj_new_with_move(E_Policy_Quickpanel *qp, int x, int y, unsigned int timestamp)
{
   Evas_Object *mover;
   Mover_Data *md;

   mover = _mover_obj_new(qp);
   if (!mover)
     return NULL;

   md = evas_object_smart_data_get(mover);
   md->effect_info.x = x;
   md->effect_info.y = y;
   md->effect_info.timestamp = timestamp;

   _mover_obj_handler_move(md, x, y);

   return mover;
}

static void
_mover_obj_visible_set(Evas_Object *mover, Eina_Bool visible)
{
   Mover_Data *md;
   E_Client *ec;
   int x = 0, y = 0;

   md = evas_object_smart_data_get(mover);
   ec = md->ec;

   switch (md->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         x = visible ? ec->zone->w : 0;
         break;
      case E_POLICY_ANGLE_MAP_180:
         y = visible ? 0 : ec->zone->h;
         break;
      case E_POLICY_ANGLE_MAP_270:
         x = visible ? 0 : ec->zone->w;
         break;
      default:
         y = visible ? ec->zone->h : 0;
         break;
     }

   _mover_obj_handler_move(md, x, y);
}

static Eina_Bool
_mover_obj_move(Evas_Object *mover, int x, int y, unsigned int timestamp)
{
   Mover_Data *md;
   int dp;
   unsigned int dt;

   if (!mover) return EINA_FALSE;

   md = evas_object_smart_data_get(mover);
   if (!_mover_obj_handler_move(md, x, y)) return EINA_FALSE;

   /* Calculate the acceleration of movement,
    * determine the visibility of quickpanel based on the result. */
   dt = timestamp - md->effect_info.timestamp;
   switch (md->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         dp = x - md->effect_info.x;
         break;
      case E_POLICY_ANGLE_MAP_180:
         dp = md->effect_info.y - y;
         break;
      case E_POLICY_ANGLE_MAP_270:
         dp = md->effect_info.x - x;
         break;
      default:
         dp = y - md->effect_info.y;
         break;
     }
   if (dt) md->effect_info.accel = (float)dp / (float)dt;

   /* Store current information to next calculation */
   md->effect_info.x = x;
   md->effect_info.y = y;
   md->effect_info.timestamp = timestamp;

   return EINA_TRUE;
}

static Mover_Effect_Data *
_mover_obj_effect_data_new(Evas_Object *mover, int from, int to, Eina_Bool visible)
{
   Mover_Effect_Data *ed;

   ed = E_NEW(Mover_Effect_Data, 1);
   if (!ed) return NULL;

   ed->mover = mover;
   ed->visible = visible;
   ed->from = from;
   ed->to = to;

   return ed;
}

static void
_mover_obj_effect_cb_mover_obj_del(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Mover_Effect_Data *ed = data;
   Mover_Data *md;

   ed = data;
   md = evas_object_smart_data_get(ed->mover);
   QP_VISIBLE_SET(md->qp->ec, ed->visible);

   /* make sure NULL before calling ecore_animator_del() */
   ed->mover = NULL;

   ecore_animator_del(ed->animator);
   ed->animator = NULL;
}

static void
_mover_obj_effect_data_free(Mover_Effect_Data *ed)
{
   E_Policy_Quickpanel *qp;
   Mover_Data *md;
   E_QP_Client *qp_client;
   Eina_List *l;

   if (ed->mover)
     {
        md = evas_object_smart_data_get(ed->mover);
        QP_VISIBLE_SET(md->qp->ec, ed->visible);

        evas_object_event_callback_del(ed->mover, EVAS_CALLBACK_DEL, _mover_obj_effect_cb_mover_obj_del);
        evas_object_del(ed->mover);
     }

   qp = _quickpanel_get();
   if (qp)
     {
        EINA_LIST_FOREACH(qp->clients, l, qp_client)
          e_tzsh_qp_state_visible_update(qp_client->ec,
                                         ed->visible);
     }

   free(ed);
}

static Eina_Bool
_mover_obj_effect_update(void *data, double pos)
{
   Mover_Effect_Data *ed = data;
   Mover_Data *md;
   int new_x = 0, new_y = 0;
   double progress = 0;

   progress = ecore_animator_pos_map(pos, ECORE_POS_MAP_DECELERATE, 0, 0);

   md = evas_object_smart_data_get(ed->mover);

   switch (md->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
      case E_POLICY_ANGLE_MAP_270:
         new_x = ed->from + (ed->to * progress);
         break;
      default:
      case E_POLICY_ANGLE_MAP_180:
         new_y = ed->from + (ed->to * progress);
         break;
     }
   _mover_obj_handler_move(md, new_x, new_y);

   if (pos == 1.0)
     {
        ecore_animator_del(ed->animator);
        ed->animator = NULL;

        _mover_obj_effect_data_free(ed);

        return ECORE_CALLBACK_CANCEL;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static void
_mover_obj_effect_start(Evas_Object *mover, Eina_Bool visible)
{
   Mover_Data *md;
   E_Client *ec;
   Mover_Effect_Data *ed;
   int from;
   int to;
   double duration;
   const double ref = 0.1;

   md = evas_object_smart_data_get(mover);
   ec = md->qp->ec;

   switch (md->rotation)
     {
      case E_POLICY_ANGLE_MAP_90:
         from = md->handler_rect.x;
         to = (visible) ? (ec->zone->w - from) : (-from);
         duration = ((double)abs(to) / (ec->zone->w / 2)) * ref;
         break;
      case E_POLICY_ANGLE_MAP_180:
         from = md->handler_rect.y;
         to = (visible) ? (-from) : (ec->zone->h - from);
         duration = ((double)abs(to) / (ec->zone->h / 2)) * ref;
         break;
      case E_POLICY_ANGLE_MAP_270:
         from = md->handler_rect.x;
         to = (visible) ? (-from) : (ec->zone->w - from);
         duration = ((double)abs(to) / (ec->zone->w / 2)) * ref;
         break;
      default:
         from = md->handler_rect.y;
         to = (visible) ? (ec->zone->h - from) : (-from);
         duration = ((double)abs(to) / (ec->zone->h / 2)) * ref;
         break;
     }

   /* create effect data */
   ed = _mover_obj_effect_data_new(mover, from, to, visible);

   /* start move effect */
   ed->animator = ecore_animator_timeline_add(duration,
                                              _mover_obj_effect_update,
                                              ed);

   evas_object_event_callback_add(mover, EVAS_CALLBACK_DEL, _mover_obj_effect_cb_mover_obj_del, ed);

   md->effect_info.animator = ed->animator;
   md->effect_info.visible = visible;
   md->effect_info.data = ed;
}

static void
_mover_obj_effect_stop(Evas_Object *mover)
{
   Mover_Data *md;

   md = evas_object_smart_data_get(mover);
   md->effect_info.data->mover = NULL;

   evas_object_event_callback_del(mover, EVAS_CALLBACK_DEL, _mover_obj_effect_cb_mover_obj_del);

   E_FREE_FUNC(md->effect_info.animator, ecore_animator_del);
}

static Eina_Bool
_mover_obj_visibility_eval(Evas_Object *mover)
{
   E_Client *ec;
   Mover_Data *md;
   Eina_Bool threshold;
   const float sensitivity = 1.5; /* hard coded. (arbitrary) */

   md = evas_object_smart_data_get(mover);
   ec = md->ec;

   switch (md->rotation)
     {
        case E_POLICY_ANGLE_MAP_90:
           threshold = (md->handler_rect.x > (ec->zone->w / 2));
           break;
        case E_POLICY_ANGLE_MAP_180:
           threshold = (md->handler_rect.y < (ec->zone->h / 2));
           break;
        case E_POLICY_ANGLE_MAP_270:
           threshold = (md->handler_rect.x < (ec->zone->w / 2));
           break;
        default:
           threshold = (md->handler_rect.y > (ec->zone->h / 2));
           break;
     }

   if ((md->effect_info.accel > sensitivity) ||
       ((md->effect_info.accel > -sensitivity) && threshold))
     return EINA_TRUE;

   return EINA_FALSE;
}

static Eina_Bool
_mover_obj_is_animating(Evas_Object *mover)
{
   Mover_Data *md;

   md = evas_object_smart_data_get(mover);

   return !!md->effect_info.animator;
}

static Eina_Bool
_mover_obj_effect_visible_get(Evas_Object *mover)
{
   Mover_Data *md;

   md = evas_object_smart_data_get(mover);

   return md->effect_info.visible;
}

static Eina_Bool
_quickpanel_send_gesture_to_indicator(void)
{
   E_Client *focused;
   focused = e_client_focused_get();
   if (focused)
     {
        ELOGF("TZ_IND", "INDICATOR state:%d, opacity:%d, vtype:%d",
              focused->pixmap, focused, focused->indicator.state, focused->indicator.opacity_mode, focused->indicator.visible_type);

        if (focused->indicator.state == 2) // state: on
          {
             if (focused->indicator.visible_type == 0) // visible: hidden
               {
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

static void
_region_obj_cb_gesture_start(void *data, Evas_Object *handler, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   if (EINA_UNLIKELY(!qp))
     return;

   if (EINA_UNLIKELY(!qp->ec))
     return;

   if (e_object_is_del(E_OBJECT(qp->ec)))
     return;

   if (_quickpanel_send_gesture_to_indicator())
     return;

   if (qp->mover)
     {
        if (_mover_obj_is_animating(qp->mover))
          return;

        DBG("Mover object already existed");
        evas_object_del(qp->mover);
     }

   _mover_obj_new_with_move(qp, x, y, timestamp);
}

static void
_region_obj_cb_gesture_move(void *data, Evas_Object *handler, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;

   qp = data;
   if (!qp->mover)
     return;

   if (_mover_obj_is_animating(qp->mover))
     return;

   _mover_obj_move(qp->mover, x, y, timestamp);
}

static void
_region_obj_cb_gesture_end(void *data EINA_UNUSED, Evas_Object *handler, int x, int y, unsigned int timestamp)
{
   E_Policy_Quickpanel *qp;
   Eina_Bool v;

   qp = data;
   if (!qp->mover)
     {
        DBG("Could not find quickpanel mover object");
        return;
     }

   if (_mover_obj_is_animating(qp->mover))
     return;

   v = _mover_obj_visibility_eval(qp->mover);
   _mover_obj_effect_start(qp->mover, v);
}

static void
_quickpanel_free(E_Policy_Quickpanel *qp)
{
   E_FREE_LIST(qp->clients, free);
   E_FREE_FUNC(qp->mover, evas_object_del);
   E_FREE_FUNC(qp->indi_obj, evas_object_del);
   E_FREE_FUNC(qp->handler_obj, evas_object_del);
   E_FREE_FUNC(qp->idle_enterer, ecore_idle_enterer_del);
   E_FREE_LIST(qp->events, ecore_event_handler_del);
   E_FREE_LIST(qp->hooks, e_client_hook_del);
   E_FREE_LIST(qp->intercept_hooks, e_comp_object_intercept_hook_del);
   E_FREE(_pol_quickpanel);
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
_quickpanel_handler_rect_add(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, int x, int y, int w, int h)
{
   E_Client *ec;
   Evas_Object *obj;

   ec = qp->ec;

   ELOGF("QUICKPANEL", "Handler Geo Set | x %d, y %d, w %d, h %d",
         NULL, NULL, x, y, w, h);

   if (qp->handler_obj)
     goto end;

   obj = e_service_region_object_new();
   evas_object_name_set(obj, "qp::handler_obj");
   if (!obj)
     return;

   e_service_region_cb_set(obj,
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
   e_service_region_rectangle_set(qp->handler_obj, ridx, x, y, w, h);
}

static void
_quickpanel_handler_region_set(E_Policy_Quickpanel *qp, E_Policy_Angle_Map ridx, Eina_Tiler *tiler)
{
   Eina_Iterator *it;
   Eina_Rectangle *r;
   int x = 0, y = 0;

   /* FIXME supported single rectangle, not tiler */

   it = eina_tiler_iterator_new(tiler);
   EINA_ITERATOR_FOREACH(it, r)
     {
        _quickpanel_handler_rect_add(qp, ridx, r->x, r->y, r->w, r->h);

        /* FIXME: this should be set by another way like indicator */
        if (ridx == E_POLICY_ANGLE_MAP_180)
          {
             x = 0;
             y = qp->ec->zone->h - r->h;
          }
        else if (ridx == E_POLICY_ANGLE_MAP_270)
          {
             x = qp->ec->zone->w - r->w;
             y = 0;
          }
        e_service_region_rectangle_set(qp->indi_obj, ridx, x, y, r->w, r->h);

        break;
     }
   eina_iterator_free(it);
}

static void
_e_qp_vis_change(E_Policy_Quickpanel *qp, Eina_Bool vis, Eina_Bool with_effect)
{
   E_Client *ec;
   Evas_Object *mover;
   Eina_Bool res, cur_vis = EINA_FALSE;
   int x, y, w, h;

   res = _e_qp_client_scrollable_update();
   if (!res) return;

   ec = qp->ec;

   evas_object_geometry_get(ec->frame, &x, &y, &w, &h);

   if (E_INTERSECTS(x, y, w, h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
     cur_vis = evas_object_visible_get(ec->frame);

   if (cur_vis == vis)
     return;

   mover = qp->mover;

   if (with_effect)
     {
        if (mover)
          {
             if (_mover_obj_is_animating(mover))
               {
                  if (_mover_obj_effect_visible_get(mover) == vis)
                    return;

                  _mover_obj_effect_stop(mover);
               }
          }
        else
          {
             mover = _mover_obj_new(qp);
             _mover_obj_visible_set(mover, !vis);
          }

        _mover_obj_effect_start(mover, vis);
     }
   else
     {
        if (mover)
          {
             if (_mover_obj_is_animating(mover))
               _mover_obj_effect_stop(mover);
             evas_object_del(mover);
          }

        QP_VISIBLE_SET(ec, vis);
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

   evas_object_hide(qp->indi_obj);
   evas_object_hide(qp->handler_obj);

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

   if (evas_object_visible_get(ec->frame))
     evas_object_show(qp->handler_obj);
   else
     evas_object_show(qp->indi_obj);

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

   qp = data;
   if (EINA_UNLIKELY(!qp))
     goto end;

   ec = ev->ec;
   if (EINA_UNLIKELY(!ec))
     goto end;

   if (qp->ec != ec)
     goto end;

   qp->rotation = e_policy_angle_map(ec->e.state.rot.ang.curr);

   if (evas_object_visible_get(ec->frame))
     evas_object_show(qp->handler_obj);
   else
     evas_object_show(qp->indi_obj);

   EINA_LIST_FOREACH(qp->clients, l, qp_client)
     e_tzsh_qp_state_orientation_update(qp_client->ec,
                                        qp->rotation);

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
 *       _quickpanel_cb_client_stack()
 *       _quickpanel_cb_client_remove()
 *       _quickpanel_idle_enter()
 */
static E_Client *
_quickpanel_below_visible_client_get(E_Policy_Quickpanel *qp)
{
   E_Client *ec;

   for (ec = e_client_below_get(qp->ec); ec; ec = e_client_below_get(ec))
     {
        if (!ec->visible) continue;
        if (!ec->icccm.accepts_focus) continue;

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

   _quickpanel_below_change_eval(data, event);
end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_quickpanel_cb_client_remove(void *data, int type, void *event)
{
   _quickpanel_below_change_eval(data, event);
   return ECORE_CALLBACK_PASS_ON;
}

static Evas_Object *
_quickpanel_indicator_object_new(E_Policy_Quickpanel *qp)
{
   Evas_Object *indi_obj;

   indi_obj = e_service_region_object_new();
   evas_object_name_set(indi_obj, "qp::indicator_obj");
   if (!indi_obj)
     return NULL;

   evas_object_repeat_events_set(indi_obj, EINA_FALSE);
   /* FIXME: make me move to explicit layer something like POL_LAYER */
   evas_object_layer_set(indi_obj, EVAS_LAYER_MAX - 1);

   e_service_region_cb_set(indi_obj,
                       _region_obj_cb_gesture_start,
                       _region_obj_cb_gesture_move,
                       _region_obj_cb_gesture_end, qp);

   evas_object_show(indi_obj);

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

        below = _quickpanel_below_visible_client_get(qp);
        if (qp->below != below)
          {
             DBG("qp->below '%s'(%p) new_below '%s'(%p)\n",
                 qp->below ? (qp->below->icccm.name ? qp->below->icccm.name : "") : "",
                 qp->below,
                 below ? (below->icccm.name ? below->icccm.name : "") : "",
                 below);

             qp->below = below;

             /* QUICKFIX
              * hide the quickpanel, if below client is the stacking client.
              * it means to find out whether or not it was launched.
              */
             if ((qp->stacking == below) &&
                 (qp->ec->visible))
               e_service_quickpanel_hide();

             _e_qp_client_scrollable_update();
          }

        qp->stacking = NULL;
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
_e_qp_client_ec_get(E_Client *ec)
{
   E_Policy_Quickpanel *qp = _quickpanel_get();
   E_QP_Client *qp_client = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, qp_client);

   EINA_LIST_FOREACH(qp->clients, l, qp_client)
     {
        if (qp_client->ec == ec)
          return qp_client;
     }

   return qp_client;
}

/* return value
 *  EINA_TRUE : user can scrool the QP.
 *  EINA_FALSE: user can't scroll QP since below window doesn't want.
 */
static Eina_Bool
_e_qp_client_scrollable_update(void)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;
   Eina_Bool res = EINA_TRUE;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   if (!qp->below)
     {
        evas_object_pass_events_set(qp->handler_obj, EINA_FALSE);
        evas_object_pass_events_set(qp->indi_obj, EINA_FALSE);
        return EINA_TRUE;
     }

   /* Do not show and scroll the quickpanel window if the qp_client winodw
    * which is placed at the below of the quickpanel window doesn't want
    * to show and scroll the quickpanel window.
    */
   qp_client = _e_qp_client_ec_get(qp->below);
   if ((qp_client) && (!qp_client->hint.scrollable))
     {
        evas_object_pass_events_set(qp->handler_obj, EINA_TRUE);
        evas_object_pass_events_set(qp->indi_obj, EINA_TRUE);
        res = EINA_FALSE;
     }
   else
     {
        evas_object_pass_events_set(qp->handler_obj, EINA_FALSE);
        evas_object_pass_events_set(qp->indi_obj, EINA_FALSE);
        res = EINA_TRUE;
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

/* NOTE: supported single client for quickpanel for now. */
EINTERN void
e_service_quickpanel_client_set(E_Client *ec)
{
   E_Policy_Quickpanel *qp;

   if (EINA_UNLIKELY(!ec))
     {
        qp = _quickpanel_get();
        if (qp)
          _quickpanel_free(qp);
        return;
     }

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* if we have not setup evas callbacks for this client, do it */
   if (_pol_quickpanel) return;

   ELOGF("QUICKPANEL", "Set Client | ec %p", NULL, NULL, ec);

   qp = calloc(1, sizeof(*qp));
   if (!qp)
     return;

   _pol_quickpanel = qp;

   qp->ec = ec;
   qp->show_block = EINA_TRUE;
   qp->below = _quickpanel_below_visible_client_get(qp);
   qp->indi_obj = _quickpanel_indicator_object_new(qp);
   if (!qp->indi_obj)
     {
        free(qp);
        return;
     }

   e_client_window_role_set(ec, "quickpanel");

   // set quickpanel layer
   if (E_POLICY_QUICKPANEL_LAYER != evas_object_layer_get(ec->frame))
     {
        evas_object_layer_set(ec->frame, E_POLICY_QUICKPANEL_LAYER);
     }
   ec->layer = E_POLICY_QUICKPANEL_LAYER;

   // set skip iconify
   ec->exp_iconify.skip_iconify = 1;
   ec->e.state.rot.type = E_CLIENT_ROTATION_TYPE_DEPENDENT;

   /* add quickpanel to force update list of zone */
   e_zone_orientation_force_update_add(ec->zone, ec);

   QP_HIDE(ec);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW, _quickpanel_client_evas_cb_show, qp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE, _quickpanel_client_evas_cb_hide, qp);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE, _quickpanel_client_evas_cb_move, qp);

   E_CLIENT_HOOK_APPEND(qp->hooks,   E_CLIENT_HOOK_DEL,                       _quickpanel_hook_client_del,     qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_ROTATION_CHANGE_BEGIN,    _quickpanel_cb_rotation_begin,   qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_ROTATION_CHANGE_CANCEL,   _quickpanel_cb_rotation_cancel,  qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_ROTATION_CHANGE_END,      _quickpanel_cb_rotation_done,    qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_SHOW,                     _quickpanel_cb_client_show,      qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_HIDE,                     _quickpanel_cb_client_hide,      qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_STACK,                    _quickpanel_cb_client_stack,     qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_REMOVE,                   _quickpanel_cb_client_remove,    qp);
   E_LIST_HANDLER_APPEND(qp->events, E_EVENT_CLIENT_BUFFER_CHANGE,            _quickpanel_cb_buffer_change,    qp);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(qp->intercept_hooks, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _quickpanel_intercept_hook_show, qp);


   qp->idle_enterer = ecore_idle_enterer_add(_quickpanel_idle_enter, qp);
}

EINTERN E_Client *
e_service_quickpanel_client_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_pol_quickpanel, NULL);

   return _pol_quickpanel->ec;
}

EINTERN Eina_Bool
e_service_quickpanel_region_set(int type, int angle, Eina_Tiler *tiler)
{
   E_Policy_Quickpanel *qp;
   E_Policy_Angle_Map ridx;

   qp = _quickpanel_get();
   if (EINA_UNLIKELY(!qp))
     return EINA_FALSE;

   if (EINA_UNLIKELY(!qp->ec))
     return EINA_FALSE;

   if (e_object_is_del(E_OBJECT(qp->ec)))
     return EINA_FALSE;

   // FIXME: region type
   if (type != 0)
     return EINA_FALSE;

   ridx = e_policy_angle_map(angle);
   _quickpanel_handler_region_set(qp, ridx, tiler);

   return EINA_TRUE;
}

EINTERN void
e_service_quickpanel_show(void)
{
   E_Policy_Quickpanel *qp;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_vis_change(qp, EINA_TRUE, EINA_TRUE);
}

EINTERN void
e_service_quickpanel_hide(void)
{
   E_Policy_Quickpanel *qp;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   _e_qp_vis_change(qp, EINA_FALSE, EINA_TRUE);
}

EINTERN Eina_Bool
e_qp_visible_get(void)
{
   E_Policy_Quickpanel *qp;
   E_Client *ec;
   Eina_Bool vis = EINA_FALSE;
   int x, y, w, h;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   ec = qp->ec;
   evas_object_geometry_get(ec->frame, &x, &y, &w, &h);

   if (E_INTERSECTS(x, y, w, h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
     vis = evas_object_visible_get(ec->frame);

   return vis;
}

EINTERN int
e_qp_orientation_get(void)
{
   E_Policy_Quickpanel *qp;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, E_POLICY_ANGLE_MAP_0);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, E_POLICY_ANGLE_MAP_0);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), E_POLICY_ANGLE_MAP_0);

   return qp->rotation;
}

EINTERN void
e_qp_client_add(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(ec)));

   qp_client = E_NEW(E_QP_Client, 1);
   qp_client->ec = ec;
   qp_client->hint.vis = EINA_TRUE;
   qp_client->hint.scrollable = EINA_TRUE;

   qp->clients = eina_list_append(qp->clients, qp_client);
}

EINTERN void
e_qp_client_del(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   qp_client = _e_qp_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);

   qp->clients = eina_list_remove(qp->clients, qp_client);

   E_FREE(qp_client);
}

EINTERN void
e_qp_client_show(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   qp_client = _e_qp_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);
   EINA_SAFETY_ON_FALSE_RETURN(qp_client->hint.scrollable);

   _e_qp_vis_change(qp, EINA_TRUE, EINA_TRUE);
}

EINTERN void
e_qp_client_hide(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN(qp);
   EINA_SAFETY_ON_NULL_RETURN(qp->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(qp->ec)));

   qp_client = _e_qp_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(qp_client);
   EINA_SAFETY_ON_FALSE_RETURN(qp_client->hint.scrollable);

   _e_qp_vis_change(qp, EINA_FALSE, EINA_TRUE);
}

EINTERN Eina_Bool
e_qp_client_scrollable_set(E_Client *ec, Eina_Bool set)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   qp_client = _e_qp_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   if (qp_client->hint.scrollable != set)
     qp_client->hint.scrollable = set;

   _e_qp_client_scrollable_update();

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_qp_client_scrollable_get(E_Client *ec)
{
   E_Policy_Quickpanel *qp;
   E_QP_Client *qp_client;

   qp = _quickpanel_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp->ec, EINA_FALSE);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(e_object_is_del(E_OBJECT(qp->ec)), EINA_FALSE);

   qp_client = _e_qp_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(qp_client, EINA_FALSE);

   return qp_client->hint.scrollable;
}
