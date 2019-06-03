#ifndef _E_POLICY_VISIBILITY_INTERNAL_H_
#define _E_POLICY_VISIBILITY_INTERNAL_H_

#define E_VIS_TIMEOUT   2.0
#define E_CLEAR_GRAB_TIMEOUT   0.01

#define NAME(ec)        ec->icccm.name ? ec->icccm.name : ""
#define STATE_STR(vc)                                                                            \
   (vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD ? "WAITING_FOR_CHILD" : \
    vc->state == E_VIS_ICONIFY_STATE_UNICONIC ? "UNICONIC" :                                     \
    vc->state == E_VIS_ICONIFY_STATE_ICONIC ? "ICONIC" :                                         \
    vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_RENDER_DONE ? "RUNNING UNICONIFY RENDER DONE" :  \
    vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY ? "RUNNING UNICONIFY" : "OTHERS")

#define VS_DBG(ec, f, x...) \
      DBG("VISIBILITY | "f" | '%s'(win:0x%08zx, ec:%p) RscID %d", ##x, ec ? NAME(ec) : "", e_client_util_win_get(ec), ec, ec ? e_pixmap_res_id_get(ec->pixmap) : 0)
#define VS_INF(ec, f, x...) \
      INF("VISIBILITY | "f" | '%s'(win:0x%08zx, ec:%p) RscID %d", ##x, ec ? NAME(ec) : "", e_client_util_win_get(ec), ec, ec ? e_pixmap_res_id_get(ec->pixmap) : 0)
#define VS_ERR(ec, f, x...) \
      ERR("VISIBILITY | "f" | '%s'(win:0x%08zx, ec:%p) RscID %d", ##x, ec ? NAME(ec) : "", e_client_util_win_get(ec), ec, ec ? e_pixmap_res_id_get(ec->pixmap) : 0)

#undef E_COMP_OBJECT_INTERCEPT_HOOK_APPEND
#define E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(l, t, cb, d)       \
   do                                                          \
   {                                                           \
      E_Comp_Object_Intercept_Hook *_h;                        \
      _h = e_comp_object_intercept_hook_add(t, cb, d);         \
      assert(_h);                                              \
      l = eina_list_append(l, _h);                             \
   } while (0)

#define E_VIS_ALLOC_RET(ptr, type, size)                       \
   type *ptr;                                                  \
   ptr = E_NEW(type, size);                                    \
   if (EINA_UNLIKELY(!ptr))                                    \
     {                                                         \
        ERR("Failed to alloc 'type'");                         \
        return;                                                \
     }

#define E_VIS_ALLOC_RET_VAL(ptr, type, size, val)              \
   type *ptr;                                                  \
   ptr = E_NEW(type, size);                                    \
   if (EINA_UNLIKELY(!ptr))                                    \
     {                                                         \
        ERR("Failed to alloc 'type'");                         \
        return val;                                            \
     }

#define OBJ_EC_GET(ptr, obj)                                   \
   E_Client *ptr;                                              \
   ptr = evas_object_data_get(obj, "E_Client");

#define E_VIS_CLIENT_GET(ptr, ec)                              \
   E_Vis_Client *ptr = NULL;                                   \
   if (ec) {                                                   \
        if (EINA_LIKELY(pol_vis != NULL)) {                    \
             ptr = eina_hash_find(pol_vis->clients_hash, &ec); \
        }                                                      \
   }

#define E_VIS_CLIENT_GET_OR_RETURN(ptr, ec)                    \
   E_Vis_Client *ptr;                                          \
   EINA_SAFETY_ON_NULL_RETURN(ec);                             \
   if (EINA_UNLIKELY(!pol_vis)) {                              \
        ERR("No Data of Pol_Vis");                             \
        return;                                                \
     }                                                         \
   ptr = eina_hash_find(pol_vis->clients_hash, &ec);           \
   if (!ptr) {                                                 \
        ERR("No Data of E_Vis_Client");                        \
        return;                                                \
     }

#define E_VIS_CLIENT_GET_OR_RETURN_VAL(ptr, ec, val)           \
   E_Vis_Client *ptr;                                          \
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, val);                    \
   if (EINA_UNLIKELY(!pol_vis)) {                              \
        ERR("No Data of Pol_Vis");                             \
        return val;                                            \
     }                                                         \
   ptr = eina_hash_find(pol_vis->clients_hash, &ec);           \
   if (!ptr) {                                                 \
        ERR("No Data of E_Vis_Client");                        \
        return val;                                            \
     }

typedef struct _E_Vis            E_Vis;
typedef struct _E_Vis_Client     E_Vis_Client;
typedef struct _E_Vis_Job_Group  E_Vis_Job_Group;
typedef struct _E_Vis_Job        E_Vis_Job;

typedef enum
{
   E_VIS_ICONIFY_STATE_UNKNOWN,
   E_VIS_ICONIFY_STATE_UNICONIC,
   E_VIS_ICONIFY_STATE_ICONIC,
   E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY,
   E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_RENDER_DONE,
   E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD,
} E_Vis_Iconify_State;

/* external data structure */
struct _E_Vis_Grab
{
   E_Vis_Client      *vc;
   Ecore_Timer       *timer;
   Eina_Stringshare  *name;
   Eina_Bool          deleted;
   E_Vis_Job_Type     type;
};

/* internal data structure */
struct _E_Vis
{
   E_Client             *activity;
   Eina_Hash            *clients_hash;
   Eina_List            *fg_clients;
   Eina_List            *hooks;
   Eina_List            *handlers;
   Eina_List            *interceptors;
   Ecore_Idle_Enterer   *idle_enter;

   struct
   {
      Ecore_Job   *handler;
      Eina_Bool    bg_find;
      Eina_Bool    defer;
   } job;
};

struct _E_Vis_Client
{
   E_Vis_Grab           *grab;
   E_Vis_Iconify_State   state;

   E_Client             *ec;
   E_Client             *wait_for_child; //who ec is waiting for its launching

   Ecore_Event_Handler  *buf_attach;

   struct
   {
      int          count;
      Eina_List   *grab_list;
   } job;
   Eina_Bool prepare_emitted;
   E_Layer layer;

   Eina_Bool skip_below_uniconify;
};

struct _E_Vis_Job_Group
{
   Eina_Clist job_head;
   Eina_Clist entry;
};

struct _E_Vis_Job
{
   Eina_Clist      entry;
   E_Vis_Client   *vc;
   E_Vis_Job_Type  type;
   Ecore_Timer    *timer;
};

#endif
