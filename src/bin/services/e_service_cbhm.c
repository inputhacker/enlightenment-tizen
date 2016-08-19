#include "e.h"
#include "services/e_service_cbhm.h"
#include "e_policy_wl.h"
#include "e_policy_conformant.h"

typedef struct _E_Policy_Cbhm E_Policy_Cbhm;
typedef struct _E_CBHM_Client E_CBHM_Client;

struct _E_Policy_Cbhm
{
   E_Client *ec; /* cbhm service client */

   Eina_Bool show_block;
   Eina_List *hooks;
   Eina_List *intercept_hooks;
   Eina_List *clients; /* list of E_CBHM_Client */
};

struct _E_CBHM_Client
{
   E_Client *ec;
};

static E_Policy_Cbhm *_pol_cbhm = NULL;
static E_CBHM_Client * _e_cbhm_client_ec_get(E_Client *ec);

static E_Policy_Cbhm *
_cbhm_get()
{
   return _pol_cbhm;
}

static void
_cbhm_free(E_Policy_Cbhm *cbhm)
{
   E_FREE_LIST(cbhm->clients, free);
   E_FREE_LIST(cbhm->hooks, e_client_hook_del);
   E_FREE_LIST(cbhm->intercept_hooks, e_client_hook_del);
   E_FREE(_pol_cbhm);
}

static void
_cbhm_cb_evas_show(void *d, Evas *evas EINA_UNUSED, Evas_Object *obj, void *event)
{
   E_Policy_Cbhm *cbhm;
   E_Client *ec;

   cbhm = d;
   if (EINA_UNLIKELY(!cbhm))
     return;

   ec = cbhm->ec;
   if (EINA_UNLIKELY(!ec))
     return;

   if (ec->frame != obj)
     return;

   if ((!cbhm->show_block) &&
       (ec->comp_data->mapped))
     cbhm->show_block = EINA_TRUE;
}

static void
_cbhm_hook_client_del(void *d, E_Client *ec)
{
   E_Policy_Cbhm *cbhm;

   cbhm = d;
   if (EINA_UNLIKELY(!cbhm))
     return;

   if (!ec) return;

   if (cbhm->ec != ec)
     return;

   _cbhm_free(cbhm);
}

static Eina_Bool
_cbhm_intercept_hook_show(void *d, E_Client *ec)
{
   E_Policy_Cbhm *cbhm;

   cbhm = d;
   if (EINA_UNLIKELY(!cbhm))
     return EINA_TRUE;

   if (cbhm->ec != ec)
     return EINA_TRUE;

   if (cbhm->show_block)
     {
        ec->visible = EINA_FALSE;
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static void
_e_cbhm_vis_change(E_Policy_Cbhm *cbhm, Eina_Bool vis)
{
   E_Client *ec;
   Eina_Bool cur_vis = EINA_FALSE;
   int x, y, w, h;

   ec = cbhm->ec;

   evas_object_geometry_get(ec->frame, &x, &y, &w, &h);

   if (E_INTERSECTS(x, y, w, h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
     cur_vis = evas_object_visible_get(ec->frame);

   if (cur_vis == vis)
     return;

   if (vis)
     {
        cbhm->show_block = EINA_FALSE;
        if (ec->iconic)
          {
             ELOGF("CBHM", "Un-set ICONIFY BY CBHM", ec->pixmap, ec);
             if ((ec->iconic) && (!ec->exp_iconify.by_client))
               e_policy_wl_iconify_state_change_send(ec, 0);
             ec->exp_iconify.not_raise = 0;
             e_client_uniconify(ec);
          }
        else
          {
             ec->visible = EINA_TRUE;
             evas_object_show(ec->frame);
             evas_object_raise(ec->frame);
          }

        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
        e_comp_object_dirty(ec->frame);
        e_comp_object_render(ec->frame);
     }
   else
     {
        if (!ec->iconic)
          {
             ELOGF("CBHM", "Set ICONIFY BY CBHM", ec->pixmap, ec);
             e_policy_wl_iconify_state_change_send(ec, 1);
             ec->exp_iconify.by_client = 0;
             e_client_iconify(ec);
             evas_object_lower(ec->frame);
          }
     }

   EC_CHANGED(ec);
}

static E_CBHM_Client *
_e_cbhm_client_ec_get(E_Client *ec)
{
   E_Policy_Cbhm *cbhm = _cbhm_get();
   E_CBHM_Client *cbhm_client = NULL;
   Eina_List *l;

   EINA_SAFETY_ON_NULL_RETURN_VAL(cbhm, cbhm_client);

   EINA_LIST_FOREACH(cbhm->clients, l, cbhm_client)
     {
        if (cbhm_client->ec == ec)
          return cbhm_client;
     }

   return cbhm_client;
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

/* NOTE: supported single client for cbhm for now. */
EINTERN void
e_service_cbhm_client_set(E_Client *ec)
{
   E_Policy_Cbhm *cbhm;

   if (EINA_UNLIKELY(!ec))
     {
        cbhm = _cbhm_get();
        if (cbhm)
          _cbhm_free(cbhm);
        return;
     }

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* if we have not setup evas callbacks for this client, do it */
   if (_pol_cbhm)
     {
        ERR("A CBHM Client already exists ec(%p)", _pol_cbhm->ec);
        return;
     }

   ELOGF("CBHM", "Set Client | ec %p", NULL, NULL, ec);

   cbhm = calloc(1, sizeof(*cbhm));
   if (!cbhm)
     return;

   _pol_cbhm = cbhm;

   cbhm->ec = ec;
   cbhm->show_block = EINA_TRUE;

   e_comp_wl->selection.cbhm = ec->comp_data->surface;
   eina_stringshare_replace(&ec->icccm.window_role, "cbhm");
   e_policy_conformant_part_add(ec);

   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW, _cbhm_cb_evas_show, cbhm);
   E_CLIENT_HOOK_APPEND(cbhm->hooks, E_CLIENT_HOOK_DEL, _cbhm_hook_client_del, cbhm);
   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(cbhm->intercept_hooks, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _cbhm_intercept_hook_show, cbhm);
}

EINTERN E_Client *
e_service_cbhm_client_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_pol_cbhm, NULL);

   return _pol_cbhm->ec;
}

EINTERN void
e_service_cbhm_show(void)
{
   E_Policy_Cbhm *cbhm;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(cbhm->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(cbhm->ec)));

   _e_cbhm_vis_change(cbhm, EINA_TRUE);
}

EINTERN void
e_service_cbhm_hide(void)
{
   E_Policy_Cbhm *cbhm;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(cbhm->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(cbhm->ec)));

   _e_cbhm_vis_change(cbhm, EINA_FALSE);
}

EINTERN void
e_cbhm_client_add(E_Client *ec)
{
   E_Policy_Cbhm *cbhm;
   E_CBHM_Client *cbhm_client;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(cbhm->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(cbhm->ec)));
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(ec)));

   cbhm_client = E_NEW(E_CBHM_Client, 1);
   cbhm_client->ec = ec;

   cbhm->clients = eina_list_append(cbhm->clients, cbhm_client);
}

EINTERN void
e_cbhm_client_del(E_Client *ec)
{
   E_Policy_Cbhm *cbhm;
   E_CBHM_Client *cbhm_client;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   cbhm_client = _e_cbhm_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cbhm_client);

   cbhm->clients = eina_list_remove(cbhm->clients, cbhm_client);

   E_FREE(cbhm_client);
}

EINTERN void
e_cbhm_client_show(E_Client *ec)
{
   E_Policy_Cbhm *cbhm;
   E_CBHM_Client *cbhm_client;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(cbhm->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(cbhm->ec)));

   cbhm_client = _e_cbhm_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cbhm_client);

   _e_cbhm_vis_change(cbhm, EINA_TRUE);
}

EINTERN void
e_cbhm_client_hide(E_Client *ec)
{
   E_Policy_Cbhm *cbhm;
   E_CBHM_Client *cbhm_client;

   cbhm = _cbhm_get();
   EINA_SAFETY_ON_NULL_RETURN(cbhm);
   EINA_SAFETY_ON_NULL_RETURN(cbhm->ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(cbhm->ec)));

   cbhm_client = _e_cbhm_client_ec_get(ec);
   EINA_SAFETY_ON_NULL_RETURN(cbhm_client);

   _e_cbhm_vis_change(cbhm, EINA_FALSE);
}

