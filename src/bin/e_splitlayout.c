#include "e.h"

/* global variables */
#define SMART_NAME            "splitlayout"
#define SLAYOUT_ENTRY                    \
   Splitlayout_Data *sd;                 \
   sd = evas_object_smart_data_get(obj); \
   if (!sd) return;


typedef struct _Splitlayout_Data  Splitlayout_Data;
typedef struct _Splitlayout_Item  Splitlayout_Item;

struct _Splitlayout_Data
{
   E_Desk              *desk;

   Evas_Object         *obj;
   Evas_Object         *clip;
   Evas_Object         *bg;         // background color
   Evas_Object         *fg;         // splitter

   int                  x, y, w, h;
   int                  layer;

   int                  frozen;
   unsigned char        changed : 1;
   Eina_Inlist          *items;
};

struct _Splitlayout_Item
{
   EINA_INLIST;
   Splitlayout_Data    *sd;
   Evas_Coord           x, y, w, h;
   Evas_Object         *obj;         // e_slot smart objs
};

static Evas_Smart     *_splitlayout_smart = NULL;

/* static functions */
static void
_layout_smart_item_del_hook(void *data, Evas *e, Evas_Object *obj, void *event_info)
{
   e_splitlayout_unpack(obj);
}

static Splitlayout_Item *
_layout_smart_adopt(Splitlayout_Data *sd, Evas_Object *obj)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (li) e_splitlayout_unpack(obj);
   li = calloc(1, sizeof(Splitlayout_Item));
   if (!li) return NULL;
   li->sd = sd;
   li->obj = obj;
   /* defaults */
   li->x = 0;
   li->y = 0;
   li->w = 0;
   li->h = 0;
   evas_object_clip_set(obj, sd->clip);
   evas_object_smart_member_add(obj, li->sd->obj);
   evas_object_data_set(obj, "e_slot_data", li);
   evas_object_layer_set(obj, evas_object_layer_get(sd->obj));
   evas_object_event_callback_add(obj, EVAS_CALLBACK_FREE,
                                  _layout_smart_item_del_hook, NULL);
   if ((!evas_object_visible_get(sd->clip)) &&
       (evas_object_visible_get(sd->obj)))
     evas_object_show(sd->clip);
   return li;
}

static void
_layout_smart_disown(Evas_Object *obj)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;
   if (!li->sd->items)
     {
        if (evas_object_visible_get(li->sd->clip))
          evas_object_hide(li->sd->clip);
     }
   evas_object_event_callback_del(obj,
                                  EVAS_CALLBACK_FREE,
                                  _layout_smart_item_del_hook);
   evas_object_smart_member_del(obj);
   evas_object_data_del(obj, "e_slot_data");
   free(li);
}

static void
_layout_smart_move_resize_item(Splitlayout_Item *li)
{
#ifdef SPLIT_DEBUG
   int eo_x, eo_y, eo_w, eo_h;
#endif

   evas_object_move(li->obj, li->sd->x + li->x, li->sd->y + li->y);
   evas_object_resize(li->obj, li->w, li->h);

#ifdef SPLIT_DEBUG
   evas_object_geometry_get(li->obj,&eo_x,&eo_y,&eo_w,&eo_h);
   INF(" %s obj- x(%d) y(%d) w(%d) h(%d)", __FUNCTION__, eo_x, eo_y, eo_w, eo_h);
#endif

}

static void
_layout_smart_reconfigure(Splitlayout_Data *sd)
{
   Splitlayout_Item *li;

   if (!sd->changed) return;

   EINA_INLIST_FOREACH(sd->items, li)
      _layout_smart_move_resize_item(li);
   sd->changed = 0;
}

static void
_layout_intercept_layer_set(void *data, Evas_Object *obj, int layer)
{
   Splitlayout_Data *sd = data;
   Splitlayout_Item *li;

   EINA_INLIST_FOREACH(sd->items, li)
     {
        evas_object_layer_set(li->obj, layer);
     }

   evas_object_layer_set(obj, layer);
}

static void
_layout_smart_add(Evas_Object *obj)
{
   Splitlayout_Data *sd;

   sd = E_NEW(Splitlayout_Data, 1);
   if (EINA_UNLIKELY(!sd)) return;

   sd->obj = obj;
   sd->x = sd->y = sd->w = sd->h = 0;
   sd->clip = evas_object_rectangle_add(evas_object_evas_get(obj));
   evas_object_smart_member_add(sd->clip, obj);
   evas_object_move(sd->clip, 0, 0);
   evas_object_resize(sd->clip, 200002, 200002);
   evas_object_smart_data_set(obj, sd);

   sd->bg = evas_object_rectangle_add(evas_object_evas_get(obj));
   evas_object_clip_set(sd->bg, sd->clip);
#ifdef SPLIT_DEBUG
   evas_object_color_set(sd->bg, 255, 255, 0, 255);  // (yellow) background color can be configed
#else
   evas_object_color_set(sd->bg, 255, 255, 255, 255);
#endif
   evas_object_name_set(sd->bg, "e_splitlayout->bg");
   evas_object_smart_member_add(sd->bg, sd->obj);
   evas_object_show(sd->bg);

   evas_object_intercept_layer_set_callback_add(obj, _layout_intercept_layer_set, sd);
}

static void
_layout_smart_del(Evas_Object *obj)
{
   SLAYOUT_ENTRY;

   while (sd->items)
     {
        Splitlayout_Item *li = (Splitlayout_Item*)sd->items;
        sd->items = eina_inlist_remove(sd->items, EINA_INLIST_GET(li));
        _layout_smart_disown(li->obj);
     }
   if (sd->bg) evas_object_del(sd->bg);
   if (sd->clip) evas_object_del(sd->clip);
   free(sd);
}

static void
_layout_smart_show(Evas_Object *obj)
{
   SLAYOUT_ENTRY;
#ifdef SPLIT_DEBUG
   int eo_x, eo_y, eo_w, eo_h;
#endif

   evas_object_show(sd->bg);
   evas_object_show(sd->clip);

#ifdef SPLIT_DEBUG
   EINA_INLIST_FOREACH(sd->items, li)
   {
   INF("%s child %s", __FUNCTION__, evas_object_name_get(li->obj));
   evas_object_geometry_get(li->obj, &eo_x, &eo_y, &eo_w, &eo_h);
   INF("%s child - x(%d) y(%d) w(%d) h(%d)", __FUNCTION__, eo_x,eo_y,eo_w,eo_h);
   }

   evas_object_geometry_get(sd->clip, &eo_x, &eo_y, &eo_w, &eo_h);
   INF("%s clip- x(%d) y(%d) w(%d) h(%d)", __FUNCTION__, eo_x,eo_y,eo_w,eo_h);
   evas_object_geometry_get(sd->bg,&eo_x,&eo_y,&eo_w,&eo_h);
   INF("%s bg - x(%d) y(%d) w(%d) h(%d)", __FUNCTION__, eo_x,eo_y,eo_w,eo_h);
#endif
}

static void
_layout_smart_hide(Evas_Object *obj)
{
   SLAYOUT_ENTRY;

   evas_object_hide(sd->bg);
   evas_object_hide(sd->clip);
}

static void
_layout_smart_move(Evas_Object *obj, int x, int y)
{
   SLAYOUT_ENTRY;
   Splitlayout_Item *li;
   Evas_Coord dx, dy;

   if ((x == sd->x) && (y == sd->y)) return;
   dx = x - sd->x;
   dy = y - sd->y;

   evas_object_move(sd->clip, x, y);
   evas_object_move(sd->bg, x, y);
   EINA_INLIST_FOREACH(sd->items, li)
     {
        Evas_Coord ox, oy;

        evas_object_geometry_get(li->obj, &ox, &oy, NULL, NULL);
        evas_object_move(li->obj, ox + dx, oy + dy);
     }
   sd->x = x;
   sd->y = y;
}

static void
_layout_smart_resize(Evas_Object *obj, int w, int h)
{
   SLAYOUT_ENTRY;

   if ((w == sd->w) && (h == sd->h)) return;
   sd->w = w;
   sd->h = h;
   sd->changed = 1;

   evas_object_resize(sd->clip, w, h);
   evas_object_resize(sd->bg, w, h);
   if (sd->frozen <= 0) _layout_smart_reconfigure(sd);
}

static void
_layout_smart_init(void)
{
   if (_splitlayout_smart) return;
     {
        static const Evas_Smart_Class sc =
          {
             SMART_NAME,
             EVAS_SMART_CLASS_VERSION,
             _layout_smart_add,
             _layout_smart_del,
             _layout_smart_move,
             _layout_smart_resize,
             _layout_smart_show,
             _layout_smart_hide,
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
        _splitlayout_smart = evas_smart_class_new(&sc);
     }
}

#ifdef SPLIT_DEBUG
static void
_e_splitlayout_cb_slot_add(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}

static void
_e_splitlayout_cb_slot_del(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}

static void
_e_splitlayout_cb_slot_move(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}


static void
_e_splitlayout_cb_slot_resize(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}


static void
_e_splitlayout_cb_slot_lower(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}


static void
_e_splitlayout_cb_slot_raise(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}


static void
_e_splitlayout_cb_slot_update(void *data, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Splitlayout_Data *sd;
   Evas_Object *obj;

   EINA_SAFETY_ON_NULL_RETURN(data);
   sd = data;

   EINA_SAFETY_ON_NULL_RETURN(event_info);
   obj = event_info;
}
#endif

E_API Evas_Object *
e_splitlayout_add(E_Desk *desk)
{
   Splitlayout_Data *sd;
   Evas_Object *o;
   Evas *e;
#ifdef SPLIT_DEBUG
   int eo_x, eo_y, eo_w, eo_h;
#endif
   e = e_comp->evas;

   _layout_smart_init();
   o = evas_object_smart_add(e, _splitlayout_smart);
   sd = evas_object_smart_data_get(o);
   if (!sd) SMARTERR(NULL);
   sd->desk = desk;
   e_object_ref(E_OBJECT(desk));

   evas_object_name_set(sd->obj, "e_splitlayout:obj");

#ifdef SPLIT_DEBUG
   evas_object_smart_callback_add(sd->obj, "slot_add",    (Evas_Smart_Cb)_e_splitlayout_cb_slot_add,    sd);
   evas_object_smart_callback_add(sd->obj, "slot_del",    (Evas_Smart_Cb)_e_splitlayout_cb_slot_del,    sd);
   evas_object_smart_callback_add(sd->obj, "slot_move",   (Evas_Smart_Cb)_e_splitlayout_cb_slot_move,   sd);
   evas_object_smart_callback_add(sd->obj, "slot_resize", (Evas_Smart_Cb)_e_splitlayout_cb_slot_resize, sd);
   evas_object_smart_callback_add(sd->obj, "slot_lower",  (Evas_Smart_Cb)_e_splitlayout_cb_slot_lower,  sd);
   evas_object_smart_callback_add(sd->obj, "slot_raise",  (Evas_Smart_Cb)_e_splitlayout_cb_slot_raise,  sd);
   evas_object_smart_callback_add(sd->obj, "slot_update", (Evas_Smart_Cb)_e_splitlayout_cb_slot_update, sd);

   evas_object_geometry_get(sd->clip, &eo_x, &eo_y, &eo_w, &eo_h);
   INF("%s clip- x(%d) y(%d) w(%d) h(%d)", __FUNCTION__,  eo_x, eo_y, eo_w, eo_h);
   evas_object_geometry_get(sd->bg, &eo_x, &eo_y, &eo_w, &eo_h);
   INF("%s bg - x(%d) y(%d) w(%d) h(%d)", __FUNCTION__ , eo_x, eo_y, eo_w, eo_h);
#endif

   ELOGF("SPLIT", "Added Split layout", NULL);

   return sd->obj;
}

E_API int
e_splitlayout_freeze(Evas_Object *obj)
{
   Splitlayout_Data *sd;

   if (evas_object_smart_smart_get(obj) != _splitlayout_smart) SMARTERR(0);
   sd = evas_object_smart_data_get(obj);
   if (!sd) SMARTERR(0);
   sd->frozen++;
   return sd->frozen;
}

E_API int
e_splitlayout_thaw(Evas_Object *obj)
{
   Splitlayout_Data *sd;

   if (evas_object_smart_smart_get(obj) != _splitlayout_smart) SMARTERR(0);
   sd = evas_object_smart_data_get(obj);
   if (!sd) SMARTERR(0);
   sd->frozen--;
   if (sd->frozen <= 0) _layout_smart_reconfigure(sd);
   return sd->frozen;
}

E_API void
e_splitlayout_pack(Evas_Object *obj, Evas_Object *child)
{
   Splitlayout_Data *sd;
   Splitlayout_Item *li;

   if (evas_object_smart_smart_get(obj) != _splitlayout_smart) SMARTERRNR();
   sd = evas_object_smart_data_get(obj);
   if (!sd) SMARTERRNR();
   li = _layout_smart_adopt(sd, child);
   sd->items = eina_inlist_append(sd->items, EINA_INLIST_GET(li));
   evas_object_lower(child);
   if (sd->frozen <= 0) _layout_smart_move_resize_item(li);
}

E_API void
e_splitlayout_child_move(Evas_Object *obj, Evas_Coord x, Evas_Coord y)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;

   if ((li->x == x) && (li->y == y)) return;
   li->x = x;
   li->y = y;
   if (li->sd->frozen <= 0) _layout_smart_move_resize_item(li);
}

E_API void
e_splitlayout_child_resize(Evas_Object *obj, Evas_Coord w, Evas_Coord h)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;

   if (w < 0) w = 0;
   if (h < 0) h = 0;
   if ((li->w == w) && (li->h == h)) return;
   li->w = w;
   li->h = h;
   if (li->sd->frozen <= 0) _layout_smart_move_resize_item(li);
}

E_API void
e_splitlayout_child_raise(Evas_Object *obj)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;
   if (!li->sd->items) return;
   li->sd->items = eina_inlist_demote(li->sd->items, EINA_INLIST_GET(li));
   evas_object_raise(obj);
}

E_API void
e_splitlayout_child_lower(Evas_Object *obj)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;
   if (!li->sd->items) return;
   li->sd->items = eina_inlist_promote(li->sd->items, EINA_INLIST_GET(li));
   evas_object_lower(obj);
}

E_API void
e_splitlayout_child_raise_above(Evas_Object *obj, Evas_Object *above)
{
   Splitlayout_Item *li, *li2;

   EINA_SAFETY_ON_NULL_RETURN(obj);
   EINA_SAFETY_ON_NULL_RETURN(above);
   if (obj == above) return;
   li = evas_object_data_get(obj, "e_slot_data");
   li2 = evas_object_data_get(above, "e_slot_data");
   if ((!li) || (!li2) || (li->sd != li2->sd)) return;
   li->sd->items = eina_inlist_remove(li->sd->items, EINA_INLIST_GET(li));
   evas_object_stack_above(obj, above);
   li->sd->items = eina_inlist_append_relative(li->sd->items, EINA_INLIST_GET(li), EINA_INLIST_GET(li2));
}

E_API void
e_splitlayout_child_lower_below(Evas_Object *obj, Evas_Object *below)
{
   Splitlayout_Item *li, *li2;

   EINA_SAFETY_ON_NULL_RETURN(obj);
   EINA_SAFETY_ON_NULL_RETURN(below);
   if (obj == below) return;
   li = evas_object_data_get(obj, "e_slot_data");
   li2 = evas_object_data_get(below, "e_slot_data");
   if ((!li) || (!li2) || (li->sd != li2->sd)) return;
   li->sd->items = eina_inlist_remove(li->sd->items, EINA_INLIST_GET(li));
   evas_object_stack_below(obj, below);
   li->sd->items = eina_inlist_prepend_relative(li->sd->items, EINA_INLIST_GET(li), EINA_INLIST_GET(li2));
}

E_API void
e_splitlayout_child_geometry_get(Evas_Object *obj, Evas_Coord *x, Evas_Coord *y, Evas_Coord *w, Evas_Coord *h)
{
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;

   if (x) *x = li->x;
   if (y) *y = li->y;
   if (w) *w = li->w;
   if (h) *h = li->h;
}

E_API void
e_splitlayout_unpack(Evas_Object *obj)
{
   Splitlayout_Data *sd;
   Splitlayout_Item *li;

   li = evas_object_data_get(obj, "e_slot_data");
   if (!li) return;
   sd = li->sd;
   sd->items = eina_inlist_remove(sd->items, EINA_INLIST_GET(li));
   _layout_smart_disown(obj);
}

E_API Eina_List *
e_splitlayout_children_get(Evas_Object *obj)
{
   Splitlayout_Data *sd;
   Eina_List *l = NULL;
   Splitlayout_Item *li;

   if (evas_object_smart_smart_get(obj) != _splitlayout_smart) SMARTERRNR() NULL;
   sd = evas_object_smart_data_get(obj);
   if (!sd) SMARTERR(NULL);
   EINA_INLIST_FOREACH(sd->items, li)
      l = eina_list_append(l, li->obj);
   return l;
}

