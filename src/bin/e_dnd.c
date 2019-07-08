#include "e.h"

/* local subsystem functions */

static void           _e_drag_coords_update(const E_Drop_Handler *h, int *dx, int *dy);
static Ecore_Window _e_drag_win_get(const E_Drop_Handler *h, int xdnd);
static int            _e_drag_win_matches(E_Drop_Handler *h, Ecore_Window win);
static void           _e_drag_end(int x, int y);
static void           _e_drag_free(E_Drag *drag);

static Eina_Bool      _e_dnd_cb_key_down(void *data, int type, void *event);
static Eina_Bool      _e_dnd_cb_key_up(void *data, int type, void *event);
static Eina_Bool      _e_dnd_cb_mouse_up(void *data, int type, void *event);
static Eina_Bool      _e_dnd_cb_mouse_move(void *data, int type, void *event);

/* local subsystem globals */
typedef struct _XDnd XDnd;

struct _XDnd
{
   int         x, y;
   const char *type;
   void       *data;
};

static Eina_List *_event_handlers = NULL;
static Eina_List *_drop_handlers = NULL;
static Eina_List *_active_handlers = NULL;
static Eina_Hash *_drop_win_hash = NULL;

static Ecore_Window _drag_win = 0;
static Ecore_Window _drag_win_root = 0;

static Eina_List *_drag_list = NULL;
static E_Drag *_drag_current = NULL;

static Eina_Stringshare *_type_text_uri_list = NULL;

static Eina_Hash *_drop_handlers_responsives;

static void
_e_drop_handler_active_check(E_Drop_Handler *h, const E_Drag *drag, Eina_Stringshare *type)
{
   unsigned int i, j;

   if (h->hidden) return;
   for (i = 0; i < h->num_types; i++)
     {
        if (drag)
          {
             for (j = 0; j < drag->num_types; j++)
               {
                  if (h->types[i] != drag->types[j]) continue;
                  h->active = 1;
                  h->active_type = eina_stringshare_ref(h->types[i]);
                  return;
               }
          }
        else
          {
             if (h->types[i] != type) continue;
             h->active = 1;
             h->active_type = eina_stringshare_ref(h->types[i]);
             return;
          }
     }
}

static int
_e_drag_finalize(E_Drag *drag, E_Drag_Type type, int x, int y)
{
   const Eina_List *l;
   E_Drop_Handler *h;

   if (_drag_win) return 0;
     {
        _drag_win = _drag_win_root = e_comp->ee_win;
        if (!e_comp_grab_input(1, 1))
          {
             _drag_win = _drag_win_root = 0;
             return 0;
          }
     }

   if (!drag->object)
     {
        e_drag_object_set(drag, evas_object_rectangle_add(drag->evas));
        evas_object_color_set(drag->object, 0, 0, 0, 0);
     }
   evas_object_move(drag->comp_object, drag->x, drag->y);
   evas_object_resize(drag->comp_object, drag->w, drag->h);
   drag->visible = 1;
   evas_object_show(drag->comp_object);
   drag->type = type;

   drag->dx = x - drag->x;
   drag->dy = y - drag->y;

   _active_handlers = eina_list_free(_active_handlers);
   EINA_LIST_FOREACH(_drop_handlers, l, h)
     {
        Eina_Bool active = h->active;

        h->active = 0;
        eina_stringshare_replace(&h->active_type, NULL);
        _e_drop_handler_active_check(h, drag, NULL);
        if (h->active != active)
          {
             if (h->active)
               _active_handlers = eina_list_append(_active_handlers, h);
             else
               _active_handlers = eina_list_remove(_active_handlers, h);
          }
        h->entered = 0;
     }

   _drag_current = drag;
   return 1;
}

/* externally accessible functions */

EINTERN int
e_dnd_init(void)
{
   if (!_event_handlers)
     {
        _type_text_uri_list = eina_stringshare_add("text/uri-list");

        _drop_win_hash = eina_hash_int32_new(NULL);
        _drop_handlers_responsives = eina_hash_int32_new(NULL);

        E_LIST_HANDLER_APPEND(_event_handlers, ECORE_EVENT_MOUSE_BUTTON_UP, _e_dnd_cb_mouse_up, NULL);
        E_LIST_HANDLER_APPEND(_event_handlers, ECORE_EVENT_MOUSE_MOVE, _e_dnd_cb_mouse_move, NULL);
        E_LIST_HANDLER_APPEND(_event_handlers, ECORE_EVENT_KEY_DOWN, _e_dnd_cb_key_down, NULL);
        E_LIST_HANDLER_APPEND(_event_handlers, ECORE_EVENT_KEY_UP, _e_dnd_cb_key_up, NULL);
     }

   return 1;
}

EINTERN int
e_dnd_shutdown(void)
{
   E_FREE_LIST(_drag_list, e_object_del);

   _active_handlers = eina_list_free(_active_handlers);
   E_FREE_LIST(_drop_handlers, e_drop_handler_del);

   E_FREE_LIST(_event_handlers, ecore_event_handler_del);

   eina_hash_free(_drop_win_hash);

   eina_hash_free(_drop_handlers_responsives);

   eina_stringshare_del(_type_text_uri_list);
   _type_text_uri_list = NULL;

   return 1;
}

E_API E_Drag *
e_drag_current_get(void)
{
   return _drag_current;
}

E_API E_Drag *
e_drag_new(int x, int y,
           const char **types, unsigned int num_types,
           void *data, int size,
           void *(*convert_cb)(E_Drag * drag, const char *type),
           void (*finished_cb)(E_Drag *drag, int dropped))
{
   E_Drag *drag;
   unsigned int i;

   drag = e_object_alloc(sizeof(E_Drag) + num_types * sizeof(char *),
                         E_DRAG_TYPE, E_OBJECT_CLEANUP_FUNC(_e_drag_free));
   if (!drag) return NULL;

   drag->x = x;
   drag->y = y;
   drag->w = 24;
   drag->h = 24;
   drag->layer = E_LAYER_CLIENT_DRAG;

   drag->evas = e_comp->evas;

   drag->type = E_DRAG_NONE;

   for (i = 0; i < num_types; i++)
     drag->types[i] = eina_stringshare_add(types[i]);
   drag->num_types = num_types;
   drag->data = data;
   drag->data_size = size;
   drag->cb.convert = convert_cb;
   drag->cb.finished = finished_cb;

   _drag_list = eina_list_append(_drag_list, drag);

   _drag_win_root = e_comp->root;

   drag->cb.key_down = NULL;
   drag->cb.key_up = NULL;

   return drag;
}

E_API Evas *
e_drag_evas_get(const E_Drag *drag)
{
   return drag->evas;
}

E_API void
e_drag_object_set(E_Drag *drag, Evas_Object *object)
{
   EINA_SAFETY_ON_NULL_RETURN(object);
   EINA_SAFETY_ON_TRUE_RETURN(!!drag->object);
   if (drag->visible)
     evas_object_show(object);
   else
     evas_object_hide(object);
   drag->object = object;
   drag->comp_object = e_comp_object_util_add(object, E_COMP_OBJECT_TYPE_NONE);
   evas_object_layer_set(drag->comp_object, drag->layer);
   evas_object_name_set(drag->comp_object, "E Drag");
   evas_object_pass_events_set(drag->comp_object, 1);
}

E_API void
e_drag_move(E_Drag *drag, int x, int y)
{
   if ((drag->x == x) && (drag->y == y)) return;
   drag->x = x;
   drag->y = y;
   if (_drag_current == drag)
     evas_object_move(drag->comp_object, x, y);
}

E_API void
e_drag_resize(E_Drag *drag, int w, int h)
{
   if ((drag->w == w) && (drag->h == h)) return;
   drag->h = h;
   drag->w = w;
   if (_drag_current == drag)
     evas_object_resize(drag->comp_object, w, h);
}

E_API int
e_dnd_active(void)
{
   return _drag_win != 0;
}

E_API int
e_drag_start(E_Drag *drag, int x, int y)
{
   return _e_drag_finalize(drag, E_DRAG_INTERNAL, x, y);
}

E_API void
e_drop_handler_xds_set(E_Drop_Handler *handler, Eina_Bool (*cb)(void *data, const char *type))
{
   handler->cb.xds = cb;
}

/* should only be used for windows */
E_API E_Drop_Handler *
e_drop_handler_add(E_Object *obj, Evas_Object *win,
                   void *data,
                   void (*enter_cb)(void *data, const char *type, void *event),
                   void (*move_cb)(void *data, const char *type, void *event),
                   void (*leave_cb)(void *data, const char *type, void *event),
                   void (*drop_cb)(void *data, const char *type, void *event),
                   const char **types, unsigned int num_types, int x, int y, int w, int h)
{
   E_Drop_Handler *handler;
   unsigned int i;

   handler = calloc(1, sizeof(E_Drop_Handler) + num_types * sizeof(char *));
   if (!handler) return NULL;

   handler->cb.data = data;
   handler->cb.enter = enter_cb;
   handler->cb.move = move_cb;
   handler->cb.leave = leave_cb;
   handler->cb.drop = drop_cb;
   handler->num_types = num_types;
   for (i = 0; i < num_types; i++)
     handler->types[i] = eina_stringshare_add(types[i]);
   handler->x = x;
   handler->y = y;
   handler->w = w;
   handler->h = h;

   handler->obj = obj;
   handler->win = win;
   handler->entered = 0;

   _drop_handlers = eina_list_append(_drop_handlers, handler);

   return handler;
}

E_API void
e_drop_handler_geometry_set(E_Drop_Handler *handler, int x, int y, int w, int h)
{
   handler->x = x;
   handler->y = y;
   handler->w = w;
   handler->h = h;
}

E_API int
e_drop_inside(const E_Drop_Handler *handler, int x, int y)
{
   int dx, dy;

   _e_drag_coords_update(handler, &dx, &dy);
   x -= dx;
   y -= dy;
   return E_INSIDE(x, y, handler->x, handler->y, handler->w, handler->h);
}

E_API void
e_drop_handler_del(E_Drop_Handler *handler)
{
   unsigned int i;
   Eina_List *l;
   Ecore_Window hwin;

   if (!handler)
     return;

   hwin = _e_drag_win_get(handler, 1);
   if (hwin)
     {
        l = eina_hash_find(_drop_handlers_responsives, &hwin);
        if (l)
          eina_hash_set(_drop_handlers_responsives, &hwin, eina_list_remove(l, handler));
     }
   _drop_handlers = eina_list_remove(_drop_handlers, handler);
   if (handler->active)
     _active_handlers = eina_list_remove(_active_handlers, handler);
   for (i = 0; i < handler->num_types; i++)
     eina_stringshare_del(handler->types[i]);
   eina_stringshare_del(handler->active_type);
   free(handler);
}

E_API void
e_drop_handler_responsive_set(E_Drop_Handler *handler)
{
   Ecore_Window hwin = _e_drag_win_get(handler, 1);
   Eina_List *l;

   l = eina_hash_find(_drop_handlers_responsives, &hwin);
   eina_hash_set(_drop_handlers_responsives, &hwin, eina_list_append(l, handler));
}

E_API int
e_drop_handler_responsive_get(const E_Drop_Handler *handler)
{
   Ecore_Window hwin = _e_drag_win_get(handler, 1);
   Eina_List *l;

   l = eina_hash_find(_drop_handlers_responsives, &hwin);
   return l && eina_list_data_find(l, handler);
}

E_API void
e_drag_key_down_cb_set(E_Drag *drag, void (*func)(E_Drag *drag, Ecore_Event_Key *e))
{
   drag->cb.key_down = func;
}

E_API void
e_drag_key_up_cb_set(E_Drag *drag, void (*func)(E_Drag *drag, Ecore_Event_Key *e))
{
   drag->cb.key_up = func;
}

/* from ecore_x_selection.c */
E_API Eina_List *
e_dnd_util_text_uri_list_convert(char *data, int size)
{
   char *tmp;
   int i, is;
   Eina_List *ret = NULL;

   if ((!data) || (!size)) return NULL;
   tmp = malloc(size);
   if (!tmp) return NULL;

   is = i = 0;
   while ((is < size) && (data[is]))
     {
        if ((i == 0) && (data[is] == '#'))
          for (; ((data[is]) && (data[is] != '\n')); is++) ;
        else
          {
             if ((data[is] != '\r') &&
                 (data[is] != '\n'))
               tmp[i++] = data[is++];
             else
               {
                  while ((data[is] == '\r') || (data[is] == '\n'))
                    is++;
                  tmp[i] = 0;
                  ret = eina_list_append(ret, strdup(tmp));
                  tmp[0] = 0;
                  i = 0;
               }
          }
     }
   if (i > 0)
     {
        tmp[i] = 0;
        ret = eina_list_append(ret, strdup(tmp));
     }

   free(tmp);

   return ret;
}

/* local subsystem functions */
static void
_e_drag_coords_update(const E_Drop_Handler *h, int *dx, int *dy)
{
   int px = 0, py = 0;

   *dx = 0;
   *dy = 0;
   if (h->win)
     {
        E_Client *ec;

        ec = e_win_client_get(h->win);
        px = ec->x;
        py = ec->y;
     }
   else if (h->obj)
     {
        switch (h->obj->type)
          {
           case E_ZONE_TYPE:
// zone based drag targets are in a comp thus their coords should be
// screen-relative as containers just cover the screen
//	     px = ((E_Zone *)(h->obj))->x;
//	     py = ((E_Zone *)(h->obj))->y;
             break;

           case E_CLIENT_TYPE:
             px = ((E_Client *)(h->obj))->x;
             py = ((E_Client *)(h->obj))->y;
             break;

           /* FIXME: add more types as needed */
           default:
             break;
          }
     }
   *dx += px;
   *dy += py;
}

static Ecore_Window
_e_drag_win_get(const E_Drop_Handler *h, int xdnd)
{
   Ecore_Window hwin = 0;

   if (h->obj)
     {
        switch (h->obj->type)
          {
           case E_CLIENT_TYPE:
           case E_ZONE_TYPE:
             hwin = e_comp->ee_win;
             break;

           /* FIXME: add more types as needed */
           default:
             break;
          }
     }

   return hwin;
}

static int
_e_drag_win_matches(E_Drop_Handler *h, Ecore_Window win)
{
   Ecore_Window hwin = _e_drag_win_get(h, 0);

   if (win == hwin) return 1;
   return 0;
}

static void
_e_drag_end(int x, int y)
{
   E_Zone *zone;
   const Eina_List *l;
   E_Event_Dnd_Drop ev;
   int dx, dy;
   Ecore_Window win;
   E_Drop_Handler *h;
   int dropped = 0;

   if (!_drag_current) return;
   win = e_comp_top_window_at_xy_get(x, y);
   zone = e_comp_zone_xy_get(x, y);
   /* Pass -1, -1, so that it is possible to drop at the edge. */
   if (zone) e_zone_flip_coords_handle(zone, -1, -1);

   evas_object_hide(_drag_current->comp_object);

   dropped = 0;
   if (!_drag_current->data)
     {
        /* Just leave */
        E_Event_Dnd_Leave leave_ev;

        leave_ev.x = x;
        leave_ev.y = y;

        EINA_LIST_FOREACH(_active_handlers, l, h)
          {
             if (h->entered)
               {
                  if (h->cb.leave)
                    h->cb.leave(h->cb.data, h->active_type, &leave_ev);
                  h->entered = 0;
               }
          }
     }

   EINA_LIST_FOREACH(_active_handlers, l, h)
     {
        if (!h->entered) continue;
        _e_drag_coords_update(h, &dx, &dy);
        ev.x = x - dx;
        ev.y = y - dy;
        if ((_e_drag_win_matches(h, win)) &&
            ((h->cb.drop) && (E_INSIDE(ev.x, ev.y, h->x, h->y, h->w, h->h))))
          {
             Eina_Bool need_free = EINA_FALSE;

             if (_drag_current->cb.convert)
               {
                  ev.data = _drag_current->cb.convert(_drag_current,
                                                      h->active_type);
               }
             else
               {
                  unsigned int i;

                  for (i = 0; i < _drag_current->num_types; i++)
                    if (_drag_current->types[i] == _type_text_uri_list)
                      {
                         char *data = _drag_current->data;
                         int size = _drag_current->data_size;

                         if (data && data[size - 1])
                           {
                              /* Isn't nul terminated */
                              size++;
                              data = realloc(data, size);
                              if (!data) break;
                              data[size - 1] = 0;
                           }
                         _drag_current->data = data;
                         _drag_current->data_size = size;
                         ev.data = e_dnd_util_text_uri_list_convert(_drag_current->data, _drag_current->data_size);
                         need_free = EINA_TRUE;
                         break;
                      }
                  if (!need_free)
                    ev.data = _drag_current->data;
               }
             h->cb.drop(h->cb.data, h->active_type, &ev);
             if (need_free) E_FREE_LIST(ev.data, free);
             dropped = 1;
          }
        h->entered = 0;
        if (dropped) break;
     }
   if (_drag_current->cb.finished)
     _drag_current->cb.finished(_drag_current, dropped);
   _drag_current->cb.finished = NULL;

   e_object_del(E_OBJECT(_drag_current));

   e_comp_ungrab_input(1, 1);
}

static void
_e_drag_free(E_Drag *drag)
{
   unsigned int i;

   if (drag == _drag_current)
     {
        E_Event_Dnd_Leave leave_ev;
        E_Drop_Handler *h;

        e_grabinput_release(_drag_win, _drag_win);
        _drag_win_root = 0;

        leave_ev.x = 0;
        leave_ev.y = 0;
        EINA_LIST_FREE(_active_handlers, h)
          {
             if (h->entered)
               {
                  if (h->cb.leave)
                    h->cb.leave(h->cb.data, h->active_type, &leave_ev);
               }
             h->active = 0;
          }
        if (drag->cb.finished)
          drag->cb.finished(drag, 0);
        drag->cb.finished = NULL;
     }

   _drag_current = NULL;

   _drag_list = eina_list_remove(_drag_list, drag);

   evas_object_hide(drag->comp_object);
   E_FREE_FUNC(drag->comp_object, evas_object_del);
   for (i = 0; i < drag->num_types; i++)
     eina_stringshare_del(drag->types[i]);
   free(drag);
   e_comp_ungrab_input(1, 1);
   _drag_win = 0;
}

static Eina_Bool
_e_dnd_cb_key_down(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;

   if (ev->window != _drag_win) return ECORE_CALLBACK_PASS_ON;

   if (!_drag_current) return ECORE_CALLBACK_PASS_ON;

   if (_drag_current->cb.key_down)
     _drag_current->cb.key_down(_drag_current, ev);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_dnd_cb_key_up(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Key *ev = event;

   if (ev->window != _drag_win) return ECORE_CALLBACK_PASS_ON;

   if (!_drag_current) return ECORE_CALLBACK_PASS_ON;

   if (_drag_current->cb.key_up)
     _drag_current->cb.key_up(_drag_current, ev);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_dnd_cb_mouse_up(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Mouse_Button *ev = event;

   if (ev->window != _drag_win) return ECORE_CALLBACK_PASS_ON;

   if (_drag_current && _drag_current->button_mask)
     {
        _drag_current->button_mask &= ~(1 << (ev->buttons - 1));
        if (_drag_current->button_mask) return ECORE_CALLBACK_RENEW;
     }
   _e_drag_end(ev->x, ev->y);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_dnd_cb_mouse_move(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   Ecore_Event_Mouse_Move *ev = event;

   if (ev->window != _drag_win) return ECORE_CALLBACK_PASS_ON;

   return ECORE_CALLBACK_PASS_ON;
}
