#include "e.h"

typedef struct _E_Msg_Event E_Msg_Event;
typedef struct _E_Event_Type E_Event_Type;

struct _E_Msg_Handler
{
   void          (*func)(void *data, const char *name, const char *info, int val, E_Object *obj, void *msgdata);
   void         *data;
   unsigned char delete_me : 1;
};

struct _E_Event_Type
{
   int         type_id; // ecore event type
   const char *message;
};

struct _E_Msg_Event
{
   char     *name;
   char     *info;
   int       val;
   E_Object *obj;
   void     *msgdata;
   void      (*afterfunc)(void *data, E_Object *obj, void *msgdata);
   void     *afterdata;
};

/* local subsystem functions */
static Eina_Bool _e_msg_event_cb(void *data, int ev_type, void *ev);
static void      _e_msg_event_free(void *data, void *ev);

/* local subsystem globals */
static Eina_List *handlers = NULL;
static Eina_List *del_handlers = NULL;
static int processing_handlers = 0;
static int E_EVENT_MSG = 0;
static Ecore_Event_Handler *hand = NULL;

static Eina_Hash *msg_hash = NULL;

/* externally accessible functions */
EINTERN int
e_msg_init(void)
{
   E_EVENT_MSG = ecore_event_type_new();
   hand = ecore_event_handler_add(E_EVENT_MSG, _e_msg_event_cb, NULL);
   msg_hash = eina_hash_string_superfast_new(NULL);
   return 1;
}

EINTERN int
e_msg_shutdown(void)
{
   E_Event_Type *ev_type;
   Eina_Iterator *it;

   while (handlers)
     e_msg_handler_del(eina_list_data_get(handlers));
   E_EVENT_MSG = 0;
   if (hand) ecore_event_handler_del(hand);
   hand = NULL;

   it = eina_hash_iterator_data_new(msg_hash);
   EINA_ITERATOR_FOREACH(it, ev_type)
     {
        eina_stringshare_del(ev_type->message);
        E_FREE(ev_type);
     }
   eina_iterator_free(it);
   E_FREE_FUNC(msg_hash, eina_hash_free);

   return 1;
}

E_API void
e_msg_send(const char *name, const char *info, int val, E_Object *obj, void *msgdata, void (*afterfunc)(void *data, E_Object *obj, void *msgdata), void *afterdata)
{
   unsigned int size, pos, name_len, info_len;
   E_Msg_Event *ev;

   name_len = info_len = 0;
   size = sizeof(E_Msg_Event);
   if (name) name_len = strlen(name) + 1;
   if (info) info_len = strlen(info) + 1;
   ev = malloc(size + name_len + info_len);
   if (!ev) return;
   pos = size;
   if (name)
     {
        ev->name = ((char *)ev) + pos;
        pos += name_len;
        strncpy(ev->name, name, name_len - 1);
        ev->name[name_len - 1] = '\0';
     }
   if (info)
     {
        ev->info = ((char *)ev) + pos;
        strncpy(ev->info, info, info_len - 1);
        ev->info[info_len - 1] = '\0';
     }
   ev->val = val;
   ev->obj = obj;
   ev->msgdata = msgdata;
   ev->afterfunc = afterfunc;
   ev->afterdata = afterdata;
   if (ev->obj) e_object_ref(ev->obj);
   ecore_event_add(E_EVENT_MSG, ev, _e_msg_event_free, NULL);
}

E_API E_Msg_Handler *
e_msg_handler_add(void (*func)(void *data, const char *name, const char *info, int val, E_Object *obj, void *msgdata), void *data)
{
   E_Msg_Handler *emsgh;

   emsgh = calloc(1, sizeof(E_Msg_Handler));
   if (!emsgh) return NULL;
   emsgh->func = func;
   emsgh->data = data;
   handlers = eina_list_append(handlers, emsgh);
   return emsgh;
}

E_API void
e_msg_handler_del(E_Msg_Handler *emsgh)
{
   if (processing_handlers > 0)
     {
        emsgh->delete_me = 1;
        del_handlers = eina_list_append(del_handlers, emsgh);
     }
   else
     {
        handlers = eina_list_remove(handlers, emsgh);
        free(emsgh);
     }
}

/* local subsystem functions */

static Eina_Bool
_e_msg_event_cb(void *data EINA_UNUSED, int ev_type EINA_UNUSED, void *ev)
{
   E_Msg_Event *e;
   Eina_List *l;
   E_Msg_Handler *emsgh;

   processing_handlers++;
   e = ev;
   EINA_LIST_FOREACH(handlers, l, emsgh)
     {
        if (!emsgh->delete_me)
          emsgh->func(emsgh->data, e->name, e->info, e->val, e->obj, e->msgdata);
     }
   if (e->afterfunc) e->afterfunc(e->afterdata, e->obj, e->msgdata);
   processing_handlers--;
   if ((processing_handlers == 0) && (del_handlers))
     {
        E_FREE_LIST(del_handlers, e_msg_handler_del);
     }
   return 1;
}

static void
_e_msg_event_free(void *data EINA_UNUSED, void *ev)
{
   E_Msg_Event *e;

   e = ev;
   if (e->obj) e_object_unref(e->obj);

   E_FREE(ev);
}

static E_Event_Type *
_e_msg_event_type_new(const char *type)
{
   E_Event_Type *ev_type;
   ev_type = calloc(1, sizeof(E_Event_Type));
   if (!ev_type) return NULL;

   ev_type->message = eina_stringshare_add(type);
   ev_type->type_id = ecore_event_type_new();
   return ev_type;
}

E_API int
e_msg_event_type_get(const char *msg)
{
   E_Event_Type *ev_type = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(msg_hash, -1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(msg, -1);

   if ((ev_type = eina_hash_find(msg_hash, msg)))
     {
        return ev_type->type_id;
     }
   else
     {
        ev_type = _e_msg_event_type_new(msg);
        EINA_SAFETY_ON_NULL_RETURN_VAL(ev_type, -1);

        eina_hash_add(msg_hash, msg, ev_type);
        return ev_type->type_id;
     }
}
