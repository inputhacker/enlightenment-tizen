#include "e.h"

/* global variables */
#define SLOT_SMART_ENTRY Slot_Smart_Data *sd; sd = evas_object_smart_data_get(obj); if (!sd) return;
#define SPLIT_DEBUG 0

typedef enum _Slot_Client_Type
{
   E_SLOT_CLIENT_TYPE_NORMAL = 0,
   E_SLOT_CLIENT_TYPE_TRANSFORM = 1
}E_Slot_Client_Type;

typedef struct _Slot_Smart_Data
{
   Evas_Object               *parent_obj;       // smart_obj belong to

   Evas_Object               *clip;
   Evas_Object               *slot_layout;
   Evas_Object               *bg_object;       // for test

   Eina_List                 *clist;           // E_Client list added on slot

   int                        x, y, w, h;
   int                        id;
   Eina_Bool                  changed;
}Slot_Smart_Data;

typedef struct _E_Slot_Client
{
   E_Client                  *ec;
   Evas_Object               *slot;

   struct
     {
        int                   x, y, w, h;
     } orig;
   int                        type;
   E_Util_Transform          *transform;
}E_Slot_Client;

typedef struct _E_Slot_G
{
   Eina_List           *slot_objs;
   Eina_Hash           *hash_slot_objs;        // data : smart object , key: slot id
   Eina_Hash           *hash_slot_clients;     // data : E_Slot_Client, key: ec
}E_Slot_G;

static E_Slot_G    *_e_slot_g = NULL;

static int          id = 0;

static Evas_Smart  *_e_slot_smart = NULL;


/* static functions */
static int _e_slot_generate_id(void);
static E_Slot_Client* _e_slot_client_new(Evas_Object *obj, E_Client* ec, E_Slot_Client_Type type);
static void _e_slot_client_del(E_Slot_Client* slot_client);
static void _e_slot_client_restore_all(Evas_Object *obj);
static Eina_Bool _e_slot_client_type_restore(E_Slot_Client* slot_client);

static void
_slot_smart_add(Evas_Object *obj)
{
   Slot_Smart_Data *sd;
   Evas *e = evas_object_evas_get(obj);

   sd = E_NEW(Slot_Smart_Data, 1);
   if (EINA_UNLIKELY(!sd)) return;

   sd->clip = evas_object_rectangle_add(e);
   evas_object_move(sd->clip, 0, 0);
   evas_object_resize(sd->clip, 200002, 200002);
   evas_object_smart_member_add(sd->clip, obj);

   sd->slot_layout = e_layout_add(e);
   evas_object_clip_set(sd->slot_layout, sd->clip);
   e_layout_virtual_size_set(sd->slot_layout, e_comp->w, e_comp->h); // FIXME:
   evas_object_smart_member_add(sd->slot_layout, obj);
   evas_object_name_set(sd->slot_layout, "e_slot->slot_layout");
   evas_object_show(sd->slot_layout);

   sd->bg_object = evas_object_rectangle_add(e);
   evas_object_name_set(sd->bg_object, "e_slot->bg_object");
   evas_object_color_set(sd->bg_object, 0, 0, 0, 255);

   e_layout_pack(sd->slot_layout, sd->bg_object);
   e_layout_child_move(sd->bg_object, 0, 0);

   evas_object_show(sd->bg_object);
   evas_object_smart_data_set(obj, sd);
}

static void
_slot_smart_del(Evas_Object *obj)
{
   SLOT_SMART_ENTRY

   _e_slot_g->slot_objs = eina_list_remove(_e_slot_g->slot_objs, obj);

   _e_slot_client_restore_all(obj);

   eina_list_free(sd->clist);
   sd->clist = NULL;

   evas_object_del(sd->bg_object);
   evas_object_del(sd->slot_layout);
   if (sd->clip)
     {
        evas_object_clip_unset(sd->clip);
        evas_object_del(sd->clip);
     }

   free(sd);
}

static void
_slot_smart_move(Evas_Object *obj, Evas_Coord x, Evas_Coord y)
{
   SLOT_SMART_ENTRY

   if ((sd->x == x) && (sd->y == y)) return;

   sd->x = x;
   sd->y = y;

   evas_object_move(sd->clip, x, y);
   evas_object_move(sd->slot_layout,  x, y);
   evas_object_move(sd->bg_object, x, y);
}

static void
_slot_smart_resize(Evas_Object *obj, Evas_Coord w, Evas_Coord h)
{
   SLOT_SMART_ENTRY

   sd->w = w;
   sd->h = h;

   evas_object_resize(sd->clip, w, h);
   evas_object_resize(sd->slot_layout, w, h);
   evas_object_resize(sd->bg_object, w, h);
}

static void
_slot_smart_show(Evas_Object *obj)
{
   SLOT_SMART_ENTRY

   evas_object_show(sd->bg_object);
   evas_object_show(sd->slot_layout);
   evas_object_show(sd->clip);
}

static void
_slot_smart_hide(Evas_Object *obj)
{
   SLOT_SMART_ENTRY

   evas_object_hide(sd->bg_object);
   evas_object_hide(sd->slot_layout);
   evas_object_hide(sd->clip);
}

static void
_slot_smart_init(void)
{
   if (_e_slot_smart) return;
     {
        static const Evas_Smart_Class sc =
          {
             "slot_smart",
             EVAS_SMART_CLASS_VERSION,
             _slot_smart_add,
             _slot_smart_del,
             _slot_smart_move,
             _slot_smart_resize,
             _slot_smart_show,
             _slot_smart_hide,
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
        _e_slot_smart = evas_smart_class_new(&sc);
     }
}

static void
e_slot_layout_cb(void)
{
   Eina_List *l;
   Evas_Object *obj = NULL;
   Slot_Smart_Data *sd = NULL;

   EINA_LIST_FOREACH(_e_slot_g->slot_objs, l, obj)
     {
        sd = evas_object_smart_data_get(obj);
        if (sd && sd->changed) e_slot_update(obj);
     }
}

EINTERN void
e_slot_init(void)
{
   ELOGF("SLOT", "e_slot_init", NULL, NULL);
   E_Slot_G *g = NULL;
   g = E_NEW(E_Slot_G, 1);

   EINA_SAFETY_ON_NULL_RETURN(g);

   g->hash_slot_objs = eina_hash_pointer_new(NULL);
   g->hash_slot_clients = eina_hash_pointer_new(NULL);
   e_client_layout_cb_set(e_slot_layout_cb);

   _e_slot_g = g;
}

EINTERN int
e_slot_shutdown(void)
{
   ELOGF("SLOT", "e_slot_shutdown", NULL, NULL);

   E_FREE_FUNC(_e_slot_g->slot_objs, eina_list_free);

   E_FREE_FUNC(_e_slot_g->hash_slot_objs, eina_hash_free);
   E_FREE_FUNC(_e_slot_g->hash_slot_clients, eina_hash_free);
   E_FREE(_e_slot_g);

   return 1;
}

E_API Evas_Object *
e_slot_new(Evas_Object *parent)
{
   Evas_Object *o;
   Slot_Smart_Data *sd;

   _slot_smart_init();
   o = evas_object_smart_add(e_comp->evas, _e_slot_smart);
   sd = evas_object_smart_data_get(o);
   EINA_SAFETY_ON_NULL_RETURN_VAL(sd, NULL);
   sd->id = _e_slot_generate_id();
   sd->parent_obj = parent;

#if SPLIT_DEBUG
   int r =100, b = 100, g = 100;
   if (sd->id % 2) {
        r = 255;
        evas_object_name_set(o, "red_slot->smart_obj");
        INF("------------------------------- RED RED RED %s (%d)", __FUNCTION__, __LINE__);
   }
   else {
        b = 255;
        INF("------------------------------- BLUE BLUE BLUE %s (%d)", __FUNCTION__, __LINE__);
        evas_object_name_set(o, "blue_slot->smart_obj");
   }
   evas_object_color_set(sd->bg_object, r, g, b, 255);
   evas_object_show(sd->bg_object);
   e_layout_child_move(sd->bg_object, 0, 0);
#endif

   _e_slot_g->slot_objs = eina_list_append(_e_slot_g->slot_objs, o);

   eina_hash_add(_e_slot_g->hash_slot_objs, &sd->id, o);

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_add", o);

   evas_object_show(o);

   ELOGF("SLOT", "|Create new slot - id:%d ", NULL, NULL, sd->id);
   evas_object_data_set(o, "e_slot_object", (void*)1);

   return o;
}

E_API void
e_slot_del(Evas_Object *obj)
{
   SLOT_SMART_ENTRY
   ELOGF("SLOT", "|Remove slot - id:%d ", NULL, NULL, sd->id);

   eina_hash_del_by_key(_e_slot_g->hash_slot_objs, &sd->id);
   evas_object_del(obj);
}

E_API void
e_slot_move(Evas_Object *obj, int x, int y)
{
   SLOT_SMART_ENTRY

   if (sd->x == x && sd->y == y) return;

   ELOGF("SLOT", "|Move slot - id:%d  (%d, %d) -> (%d, %d)", NULL, NULL,
         sd->id, sd->x, sd->y, x, y);

   evas_object_move(obj, x, y);
   sd->changed = 1;

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_move", obj);
}

E_API void
e_slot_resize(Evas_Object *obj, int w, int h)
{
   SLOT_SMART_ENTRY

   if (sd->w == w && sd->h == h) return;

   ELOGF("SLOT", "|Resize slot - id:%d  (%dx%d) -> (%dx%d)", NULL, NULL,
         sd->id, sd->w, sd->h, w, h);

   evas_object_resize(obj, w, h);
   sd->changed = 1;

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_resize", obj);
}

E_API void
e_slot_raise(Evas_Object *obj)
//e_slot_client_raise
{
   SLOT_SMART_ENTRY

   Eina_List *l = NULL;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   E_Client *ec2 = NULL;
   E_Client *top_ec = NULL;
   E_Client *below_ec = NULL;
   int cnt = 0;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->layout.s_id != sd->id) continue;
        ELOGF("SLOT", "|raise ec[list add] - id:%d [cnt:%d]pid:%d", ec->pixmap, ec, sd->id, cnt++, ec->netwm.pid);
        if (!top_ec) top_ec = ec;
        l = eina_list_append(l, ec);
     }

   if (!top_ec) return;

   below_ec = top_ec;
   cnt = 0;
   EINA_LIST_FREE(l, ec2)
     {
        ELOGF("SLOT", "e_slot_raise |raise ec - id:%d [cnt:%d]pid:%d", ec2->pixmap, ec2, sd->id, cnt++, ec2->netwm.pid);
        if (top_ec == ec2) evas_object_raise(ec2->frame);
        else evas_object_stack_below(ec2->frame, below_ec->frame);
        below_ec = ec2;
     }

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_raise", obj);
}

E_API void
e_slot_lower(Evas_Object *obj)
//e_slot_client_lower
{
   SLOT_SMART_ENTRY

   Eina_List *l = NULL;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   E_Client *ec2 = NULL;
   int cnt = 0;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->layout.s_id != sd->id) continue;
        ELOGF("SLOT", "e_slot_lower |lower ec[list add] - id:%d [cnt:%d]", ec->pixmap, ec, sd->id, cnt++);
        l = eina_list_append(l, ec);
     }

   cnt = 0;
   EINA_LIST_FREE(l, ec2)
     {
        ELOGF("SLOT", "e_slot_lower |lower ec - id:%d [cnt:%d]", ec2->pixmap, ec2, sd->id, cnt++);
        evas_object_lower(ec2->frame);
     }

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_lower", obj);
}

E_API void
e_slot_focus_set(Evas_Object *obj)
{
   SLOT_SMART_ENTRY

   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   int cnt = 0;

   if (sd->parent_obj)
     evas_object_smart_callback_call(sd->parent_obj, "slot_focus", obj);

   for(o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->layout.s_id != sd->id) continue;
        ELOGF("SLOT", "e_slot_focus_set |ec foucsed set - id:%d [cnt:%d]", ec->pixmap, ec, sd->id, cnt++);
        if (ec->frame) evas_object_raise(ec->frame);
        break;
     }
}

E_API void
e_slot_update(Evas_Object *obj)
{
   SLOT_SMART_ENTRY

   Eina_List *l = NULL;
   E_Slot_Client *slot_client = NULL;
   E_Client *ec;
   int x, y, w, h;
   int cnt = 0;

   if (sd->clist)
     {
        Eina_List *clist = NULL;
        Evas_Object *o = NULL;

        o = evas_object_top_get(e_comp->evas);
        for (; o; o = evas_object_below_get(o))
          {
             ec = evas_object_data_get(o, "E_Client");
             if (!ec) continue;
             if (e_object_is_del(E_OBJECT(ec))) continue;
             if (e_client_util_ignored_get(ec)) continue;
             if (ec->layout.s_id != sd->id) continue;
             clist = eina_list_append(clist, ec);
          }

        EINA_LIST_FOREACH(clist, l, ec)
          {
             if (e_object_is_del(E_OBJECT(ec))) continue;
             if (e_client_util_ignored_get(ec)) continue;
             if (!ec->comp_data) continue;

             slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
             if (!slot_client) continue;
             if (e_object_is_del(E_OBJECT(slot_client->ec))) continue;

             e_client_geometry_get(slot_client->ec, &x, &y, &w, &h);

             if (slot_client->type == E_SLOT_CLIENT_TYPE_TRANSFORM)
               {
                  if (slot_client->transform)
                    {
                       e_util_transform_move(slot_client->transform, (double)sd->x, (double)sd->y, 0);
                       e_util_transform_scale(slot_client->transform,  (double)sd->w /(double)slot_client->ec->w, (double)sd->h /(double)slot_client->ec->h, 1.0);
                       e_client_transform_core_update(slot_client->ec);

                       ELOGF("SLOT", "e_slot_update |transform update - id:%d (%d,%d) (%lf x %lf) [cnt:%d] [pid:%d]", slot_client->ec->pixmap, slot_client->ec,
                             sd->id, sd->x, sd->y, (double)sd->w/(double)slot_client->ec->w, (double)sd->h /(double)slot_client->ec->h, cnt++, ec->netwm.pid);
                    }
               }
             else
               {
                  if (slot_client->ec->frame)
                    {
                       evas_object_move(slot_client->ec->frame, sd->x, sd->y);
                       evas_object_resize(slot_client->ec->frame, sd->w, sd->h);
                       ELOGF("SLOT", "e_slot_update |resize update - id:%d (%d,%d,%dx%d) [cnt:%d] [pid:%d]", slot_client->ec->pixmap, slot_client->ec,
                             sd->id, sd->x, sd->y, sd->w, sd->h, cnt++, ec->netwm.pid);
                    }
               }
          }
     }

   sd->changed = 0;
   e_client_visibility_calculate();
}

E_API Eina_List*
e_slot_list_get(void)
{
   return _e_slot_g->slot_objs;
}

E_API Evas_Object *
e_slot_find_by_id(int slot_id)
{
   Evas_Object *obj = NULL;

   obj = eina_hash_find(_e_slot_g->hash_slot_objs, &slot_id);

   return obj;
}

E_API E_Client *
e_slot_client_top_find(Evas_Object *obj)
{
   Slot_Smart_Data *sd;
   Evas_Object *o = NULL;
   E_Client *ec = NULL;
   sd = evas_object_smart_data_get(obj);
   if (!sd) return NULL;

   for (o = evas_object_top_get(e_comp->evas); o; o = evas_object_below_get(o))
     {
        ec = evas_object_data_get(o, "E_Client");
        if (!ec) continue;
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (e_client_util_ignored_get(ec)) continue;
        if (ec->layout.s_id != sd->id) continue;
        if ((!ec->visible) || (ec->hidden) || (!evas_object_visible_get(ec->frame))) continue;
        return ec;
     }

   return NULL;
}

E_API Eina_Bool
e_slot_client_add(Evas_Object *obj, E_Client *ec, Eina_Bool resizable)
{
   Slot_Smart_Data *sd;
   E_Slot_Client* slot_client = NULL;
   int type = resizable ? E_SLOT_CLIENT_TYPE_NORMAL : E_SLOT_CLIENT_TYPE_TRANSFORM;

   if (!obj) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   sd = evas_object_smart_data_get(obj);
   if (!sd) return EINA_FALSE;

   slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
   if (slot_client)
     {
        ELOGF("SLOT", "%s | Remove already allocated in other slot", slot_client->ec->pixmap, slot_client->ec, __FUNCTION__);
        e_slot_client_remove(slot_client->slot, ec);
     }

   slot_client = _e_slot_client_new(obj, ec, type);
   if (!slot_client) return EINA_FALSE;

   sd->clist = eina_list_append(sd->clist, ec);
   eina_hash_add(_e_slot_g->hash_slot_clients, &ec, slot_client);

   ec->layout.s_id = sd->id;
   ec->layout.splited = EINA_TRUE;
   sd->changed = EINA_TRUE;
   ELOGF("SLOT", "%s | Add - id:%d type:%d", ec->pixmap, ec, __FUNCTION__, sd->id, type);

   return EINA_TRUE;
}

E_API Eina_Bool
e_slot_client_remove(Evas_Object *obj, E_Client *ec)
{
   Slot_Smart_Data *sd;
   E_Slot_Client* slot_client = NULL;

   if (!obj) return EINA_FALSE;
   if (!ec) return EINA_FALSE;

   sd = evas_object_smart_data_get(obj);
   if (!sd) return EINA_FALSE;

   slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
   if (!slot_client) return EINA_FALSE;

   _e_slot_client_type_restore(slot_client);

   eina_hash_del_by_key(_e_slot_g->hash_slot_clients, &ec);

   _e_slot_client_del(slot_client);

   sd->clist = eina_list_remove(sd->clist, ec);

   ec->layout.s_id = 0;
   ec->layout.splited = EINA_FALSE;
   sd->changed = EINA_TRUE;

   return EINA_TRUE;
}

E_API int
e_slot_client_type_get(E_Client *ec)
{
   E_Slot_Client *slot_client = NULL;
   int type = -1;

   if (!ec) return type;

   slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
   if (slot_client) type = slot_client->type;

   return type;
}

E_API void
e_slot_client_update(E_Client *ec)
{
   Slot_Smart_Data *sd;
   E_Slot_Client *slot_client = NULL;
   int x, y, w, h;

   if (!ec) return;

   slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
   if (!slot_client) return;

   sd = evas_object_smart_data_get(slot_client->slot);
   if (!sd) return;

   if (slot_client)
     {
        if (e_object_is_del(E_OBJECT(slot_client->ec))) return;
        if (e_client_util_ignored_get(slot_client->ec)) return;
        if (!slot_client->ec->comp_data) return;

        e_client_geometry_get(slot_client->ec, &x, &y, &w, &h);

        if (slot_client->type == E_SLOT_CLIENT_TYPE_TRANSFORM)
          {
             if (slot_client->transform)
               {
                  e_util_transform_move(slot_client->transform, (double)sd->x, (double)sd->y, 0);
                  e_util_transform_scale(slot_client->transform,  (double)sd->w/(double)slot_client->ec->w, (double)sd->h /(double)slot_client->ec->h, 1.0);
                  e_client_transform_core_update(slot_client->ec);

                  ELOGF("SLOT", "e_slot_client_update |transform update - id:%d (%d,%d) (%lf x %lf) ", slot_client->ec->pixmap, slot_client->ec,
                        ec->layout.s_id, sd->x, sd->y, (double)sd->w/(double)slot_client->ec->w, (double)sd->h /(double)slot_client->ec->h);
               }
          }
        else
          {
             if (slot_client->ec->frame)
               {
                  evas_object_move(slot_client->ec->frame, sd->x, sd->y);
                  evas_object_resize(slot_client->ec->frame, sd->w, sd->h);
                  ELOGF("SLOT", "e_slot_client_update |resize update - id:%d (%d,%d,%dx%d)", slot_client->ec->pixmap, slot_client->ec,
                        ec->layout.s_id, sd->x, sd->y, sd->w, sd->h);
               }
          }
     }
}

E_API int
e_slot_find_id(Evas_Object *obj)
{
   Slot_Smart_Data *sd;

   if (!obj) return -1;

   sd = evas_object_smart_data_get(obj);
   if (!sd) return -1;

   return sd->id;
}

/* static functions */
static int
_e_slot_generate_id(void)
{
   /* FIXME: */
   return ++id;
}

static E_Slot_Client*
_e_slot_client_new(Evas_Object *obj, E_Client* ec, E_Slot_Client_Type type)
{
   E_Slot_Client *slot_client = NULL;

   if (!ec) return NULL;
   slot_client = E_NEW(E_Slot_Client, 1);
   if (!slot_client) return NULL;

   slot_client->ec = ec;
   slot_client->slot = obj;
   slot_client->orig.x = ec->x;
   slot_client->orig.y = ec->y;
   slot_client->orig.w = ec->w;
   slot_client->orig.h = ec->h;

   if (slot_client->orig.x == 0 &&
       slot_client->orig.y == 0 &&
       slot_client->orig.w == 1 &&
       slot_client->orig.h == 1 &&
       ec->zone)
     {
        slot_client->orig.x = ec->zone->x;
        slot_client->orig.y = ec->zone->y;
        slot_client->orig.w = ec->zone->w;
        slot_client->orig.h = ec->zone->h;
     }

   slot_client->transform = NULL;
   slot_client->type = type;
   if (type == E_SLOT_CLIENT_TYPE_TRANSFORM)
     {
        slot_client->transform = e_util_transform_new();
        e_client_transform_core_add(slot_client->ec, slot_client->transform);
     }

   ELOGF("SLOT", "|Create slot client - type:%d orig: %d,%d,%dx%d", ec->pixmap, ec,
         type, ec->x, ec->y, ec->w, ec->h);

   return slot_client;
}

static void
_e_slot_client_del(E_Slot_Client* slot_client)
{
   if (!slot_client) return;
   E_FREE(slot_client);
}

static Eina_Bool
_e_slot_client_type_restore(E_Slot_Client* slot_client)
{
   Eina_Bool ret = EINA_FALSE;

   if (!slot_client) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(slot_client->ec))) return EINA_FALSE;

   switch (slot_client->type)
     {
      case E_SLOT_CLIENT_TYPE_TRANSFORM:
         if (slot_client->transform)
           {
              e_client_transform_core_remove(slot_client->ec, slot_client->transform);
              e_util_transform_del(slot_client->transform);
              slot_client->transform = NULL;

              ELOGF("SLOT", "Restore client |unset transform ", slot_client->ec->pixmap, slot_client->ec);

              ret = EINA_TRUE;
           }
         break;

      default:
      case E_SLOT_CLIENT_TYPE_NORMAL:
         if (slot_client->ec->frame)
           {
              /* restore to its origin size when remove from slot */
              evas_object_move(slot_client->ec->frame, slot_client->orig.x, slot_client->orig.y);
              evas_object_resize(slot_client->ec->frame, slot_client->orig.w, slot_client->orig.h);

              ELOGF("SLOT", "Restore client |restore its origin size: %d,%d,%dx%d", slot_client->ec->pixmap, slot_client->ec,
                    slot_client->orig.x, slot_client->orig.y, slot_client->orig.w, slot_client->orig.h);
              ret = EINA_TRUE;
           }
         break;
     }

   if (!ret) ELOGF("SLOT", "Restore client |failed", slot_client->ec->pixmap, slot_client->ec);
   return ret;
}

static void
_e_slot_client_restore_all(Evas_Object *obj)
{
   SLOT_SMART_ENTRY
   E_Slot_Client* slot_client = NULL;
   E_Client *ec;
   Eina_List *l;

   EINA_LIST_FOREACH(sd->clist, l, ec)
     {
        slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
        if (slot_client)
          {
             _e_slot_client_type_restore(slot_client);
             eina_hash_del_by_key(_e_slot_g->hash_slot_clients, &ec);
             _e_slot_client_del(slot_client);
          }
     }
}

E_API Eina_List*
e_slot_client_list_get(Evas_Object *obj)
{
   Slot_Smart_Data *sd;

   sd = evas_object_smart_data_get(obj);
   if (!sd) return NULL;

   return sd->clist;
}

E_API int
e_slot_client_count_get(Evas_Object *obj)
{
   Slot_Smart_Data *sd;

   sd = evas_object_smart_data_get(obj);
   if (!sd) return 0;

   return eina_list_count(sd->clist);
}

E_API int
e_slot_client_slot_id_get(E_Client *ec)
{
   E_Slot_Client *slot_client = NULL;
   Slot_Smart_Data *sd;

   if (!ec) return -1;

   slot_client = eina_hash_find(_e_slot_g->hash_slot_clients, &ec);
   if (!slot_client) return -1;

   sd = evas_object_smart_data_get(slot_client->slot);
   if (!sd) return -1;

   return sd->id;
}
