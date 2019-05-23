#include "e.h"

typedef struct _E_Policy_Stack E_Policy_Stack;

struct _E_Policy_Stack
{
   E_Client *ec;

   struct
     {
        Ecore_Window win;
        Eina_Bool fetched;
     } transient;
};

static Eina_Hash *hash_pol_stack = NULL;

static void
_e_policy_stack_cb_data_free(void *data)
{
   E_FREE(data);
}

E_Policy_Stack*
_e_policy_stack_data_add(E_Client *ec)
{
   E_Policy_Stack *ps;

   if ((ps = eina_hash_find(hash_pol_stack, &ec)))
     return ps;

   ps = E_NEW(E_Policy_Stack, 1);
   if (!ps) return NULL;

   ps->ec = ec;
   eina_hash_add(hash_pol_stack, &ec, ps);

   return ps;
}

void
_e_policy_stack_data_del(E_Client *ec)
{
   E_Policy_Stack *ps;

   if ((ps = eina_hash_find(hash_pol_stack, &ec)))
     {
        eina_hash_del_by_key(hash_pol_stack, &ec);
     }
}

void
_e_policy_stack_transient_for_apply(E_Client *ec)
{
   int raise;
   E_Client *child, *top;
   Eina_List *l;
   Eina_Bool intercepted = EINA_FALSE;

   intercepted = e_policy_interceptor_call(E_POLICY_INTERCEPT_STACK_TRANSIENT_FOR,
                                           ec);
   if (intercepted)
     {
        ELOGF("TZPOL",
              "Fetch for statck transient_for was successfully intercepted",
              ec);
        return;
     }

   ELOGF("TZPOL", "Fetch for stack transient_for. ec_layer:%d, parent(win:%zx, ec:%p, layer:%d)",
         ec, ec->layer, e_client_util_win_get(ec->parent), ec->parent, ec->parent ? ec->parent->layer:-1);

   if (ec->parent == NULL)
     {
        // restore original layer
        if (ec->layer == ec->saved.layer)
          return;

        ELOGF("TZPOL", "Fetch(restore) for stack transient_for. Restore layer(%d->%d)", ec, ec->layer, ec->saved.layer);
        evas_object_layer_set(ec->frame, ec->saved.layer);

        return;
     }

   if (ec->parent->layer != ec->layer)
     {
        raise = e_config->transient.raise;

        ec->saved.layer = ec->layer;
        if (e_config->transient.layer)
          {
             e_config->transient.raise = 1;
             EINA_LIST_FOREACH(ec->transients, l, child)
               {
                  if (!child) continue;
                  child->saved.layer = child->layer;
               }
          }

        evas_object_layer_set(ec->frame, ec->parent->layer);
        e_config->transient.raise = raise;
     }

   if (ec->transient_policy == E_TRANSIENT_ABOVE)
     {
        top = e_client_top_get();
        while (top)
          {
             if ((!ec->parent->transients) || (top == ec->parent))
               {
                  top = NULL;
                  break;
               }
             if ((top != ec) && (eina_list_data_find(ec->parent->transients, top)))
               break;

             top = e_client_below_get(top);
          }

        if (top)
          evas_object_stack_above(ec->frame, top->frame);
        else
          evas_object_stack_above(ec->frame, ec->parent->frame);
     }
   else if (ec->transient_policy == E_TRANSIENT_BELOW)
     {
        evas_object_stack_below(ec->frame, ec->parent->frame);
     }
}

Eina_Bool
_e_policy_stack_transient_for_tree_check(E_Client *child, E_Client *parent)
{
   E_Client *p;

   p = parent->parent;
   while (p)
     {
        if (e_object_is_del(E_OBJECT(p))) return EINA_FALSE;
        if (p == child) return EINA_TRUE;

        p = p->parent;
     }

   return EINA_FALSE;
}

static void
_e_policy_stack_fetch_transient(E_Client *ec)
{
   E_Client *new_focus = NULL;
   E_Policy_Stack *ps;
   ps = eina_hash_find(hash_pol_stack, &ec);

   if (ps)
     {
        if (ps->transient.fetched)
          {
             if (ps->transient.win)
               {
                  if (ec->icccm.transient_for == ps->transient.win)
                    _e_policy_stack_transient_for_apply(ec);
                  else
                    ps->transient.win = ec->icccm.transient_for;

                  if (ec->parent)
                    {
                       if (ec->parent == e_client_focused_get())
                         {
                            new_focus = e_client_transient_child_top_get(ec->parent, EINA_TRUE);
                            if (new_focus)
                              {
                                 ELOGF("FOCUS", "focus set   | tranient_for fetch", new_focus);
                                 e_client_frame_focus_set(new_focus, EINA_TRUE);
                              }
                         }
                    }
               }
             else
               {
                  _e_policy_stack_transient_for_apply(ec);
               }
             ps->transient.fetched = 0;
          }
     }
}

void
e_policy_stack_hook_pre_post_fetch(E_Client *ec)
{
   _e_policy_stack_fetch_transient(ec);
}

static void
_e_policy_stack_fetch_icccm_transient_for(E_Client *ec)
{
   if (ec->icccm.fetch.transient_for)
     {
        E_Policy_Stack *ps;
        Ecore_Window transient_for_win = 0;
        E_Client *parent = NULL;
        Eina_Bool transient_each_other = EINA_FALSE;

        ps = _e_policy_stack_data_add(ec);
        EINA_SAFETY_ON_NULL_RETURN(ps);

        parent = e_pixmap_find_client(E_PIXMAP_TYPE_WL, ec->icccm.transient_for);

        if (parent)
          {
             ps->transient.win = e_client_util_win_get(parent);
             ps->transient.fetched = 1;

             /* clients transient for each other */
             transient_each_other = _e_policy_stack_transient_for_tree_check(ec, parent);
             if (transient_each_other)
               {
                  ec->icccm.transient_for = transient_for_win;
                  ps->transient.fetched = 0;
                  parent = NULL;
               }
          }
        else
          {
             ps->transient.win = 0;
             ps->transient.fetched = 1;
          }

        ec->icccm.fetch.transient_for = 0;
     }
}

void
e_policy_stack_hook_pre_fetch(E_Client *ec)
{
   _e_policy_stack_fetch_icccm_transient_for(ec);
}

void
e_policy_stack_transient_for_set(E_Client *ec, E_Client *parent)
{
   E_Policy_Stack *ps;
   Ecore_Window pwin = 0;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (!parent)
     {
        ec->icccm.fetch.transient_for = EINA_TRUE;
        ec->icccm.transient_for = 0;
        if (ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
        return;
     }

   ps = _e_policy_stack_data_add(ec);
   EINA_SAFETY_ON_NULL_RETURN(ps);

   pwin = e_client_util_win_get(parent);

   /* If we already have a parent, remove it */
   if (ec->parent)
     {
        if (parent != ec->parent)
          {
             ec->parent->transients =
                eina_list_remove(ec->parent->transients, ec);
             if (ec->parent->modal == ec) ec->parent->modal = NULL;
             ec->parent = NULL;
          }
        else
          parent = NULL;
     }

   if ((parent) && (parent != ec) &&
       (eina_list_data_find(parent->transients, ec) != ec))
     {
        parent->transients = eina_list_append(parent->transients, ec);
        ec->parent = parent;
     }

   ec->icccm.fetch.transient_for = EINA_TRUE;
   ec->icccm.transient_for = pwin;

   if (!ps->transient.win)
     ec->saved.layer = ec->layer;

   EC_CHANGED(ec);
}

void
e_policy_stack_transient_for_apply(E_Client *ec)
{
   if (!ec) return;

   _e_policy_stack_fetch_icccm_transient_for(ec);
   _e_policy_stack_fetch_transient(ec);
}

void
e_policy_stack_cb_client_remove(E_Client *ec)
{
   Eina_List *l;
   E_Client *child = NULL;

   EINA_LIST_FOREACH(ec->transients, l, child)
     {
        if (!child) continue;
        if (e_object_is_del(E_OBJECT(child))) continue;
        // for restore child's original layer
        child->icccm.fetch.transient_for = EINA_TRUE;
        EC_CHANGED(child);
     }

   _e_policy_stack_data_del(ec);
}

void
e_policy_stack_shutdown(void)
{
   eina_hash_free(hash_pol_stack);
   hash_pol_stack = NULL;
}

void
e_policy_stack_init(void)
{
   hash_pol_stack = eina_hash_pointer_new(_e_policy_stack_cb_data_free);
}

void
e_policy_stack_below(E_Client *ec, E_Client *below_ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   EINA_SAFETY_ON_NULL_RETURN(below_ec);
   EINA_SAFETY_ON_NULL_RETURN(below_ec->frame);

   evas_object_stack_below(ec->frame, below_ec->frame);
   e_comp_wl_subsurface_stack_update(below_ec);
   if (e_config->transient.iconify)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             e_policy_stack_below(child, below_ec);
          }
     }
}

void
e_policy_stack_above(E_Client *ec, E_Client *above_ec)
{
   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(ec->frame);

   EINA_SAFETY_ON_NULL_RETURN(above_ec);
   EINA_SAFETY_ON_NULL_RETURN(above_ec->frame);

   evas_object_stack_above(ec->frame, above_ec->frame);
   e_comp_wl_subsurface_stack_update(above_ec);
   if (e_config->transient.iconify)
     {
        E_Client *child;
        Eina_List *list = eina_list_clone(ec->transients);

        EINA_LIST_FREE(list, child)
          {
             e_policy_stack_above(child, ec);
          }
     }
}

static E_Client *
_e_policy_stack_find_top_lockscreen(E_Client *ec_lock, E_Client *ec_except)
{
   E_Client *ec = NULL;
   E_Client *ec_top_lock = NULL;
   int x, y, w, h;

   if (!ec_lock) return NULL;

   E_CLIENT_REVERSE_FOREACH(ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if ((ec != ec_except) &&
            (e_policy_client_is_lockscreen(ec)))
          {
             e_client_geometry_get(ec, &x, &y, &w, &h);
             if (E_CONTAINS(x, y, w, h,
                            ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
               {
                  ec_top_lock = ec;
                  break;
               }
          }
     }

   return ec_top_lock;
}

void
e_policy_stack_clients_restack_above_lockscreen(E_Client *ec_lock, Eina_Bool show)
{
   E_Client *ec = NULL;
   E_Client *new_lock = NULL;
   Eina_Bool restack_above = EINA_FALSE;

   if (!ec_lock) return;

   if (show)
     {
        new_lock = _e_policy_stack_find_top_lockscreen(ec_lock, NULL);
        if (!new_lock)
          new_lock = ec_lock;

        e_policy_system_info.lockscreen.show = show;
        e_policy_system_info.lockscreen.ec = new_lock;

        restack_above = EINA_TRUE;
     }
   else
     {
        if (ec_lock != e_policy_system_info.lockscreen.ec)
          return;

        new_lock = _e_policy_stack_find_top_lockscreen(ec_lock, e_policy_system_info.lockscreen.ec);
        if (new_lock)
          {
             e_policy_system_info.lockscreen.show = EINA_TRUE;
             e_policy_system_info.lockscreen.ec = new_lock;
             restack_above = EINA_TRUE;
          }
        else
          {
             E_Layer org_layer;
             Eina_List *restore_list = NULL;
             Eina_List *l = NULL;

             e_policy_system_info.lockscreen.show = show;
             e_policy_system_info.lockscreen.ec = NULL;

             E_CLIENT_FOREACH(ec)
               {
                  if (e_object_is_del(E_OBJECT(ec))) continue;
                  if (ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set &&
                      ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved)
                    {
                       restore_list = eina_list_append(restore_list, ec);
                    }
               }

             if (restore_list)
               {
                  EINA_LIST_FOREACH(restore_list, l, ec)
                    {
                       org_layer = ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer;
                       ELOGF("CHANGE to Original layer", "AboveLock|layer: %d -> %d", ec, ec->layer, org_layer);
                       evas_object_layer_set(ec->frame, org_layer);
                       ec->layer = org_layer;

                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved = EINA_FALSE;
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer = 0;
                    }
                  eina_list_free(restore_list);
                  restore_list = NULL;
               }
          }
     }

   if (restack_above)
     {
        Eina_List *restack_list = NULL;
        Eina_List *l = NULL;
        E_Layer lock_layer = e_policy_system_info.lockscreen.ec->layer;
        Eina_Bool passed_new_lock = EINA_FALSE;
        int x, y, w, h;

        E_CLIENT_REVERSE_FOREACH(ec)
          {
             if (e_object_is_del(E_OBJECT(ec))) continue;
             if (ec == new_lock)
               {
                  passed_new_lock = EINA_TRUE;
                  continue;
               }
             if (!passed_new_lock) continue;
             if (e_policy_client_is_lockscreen(ec)) continue;
             if (ec->exp_iconify.by_client) continue;
             if (ec->bg_state) continue;

             if (ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set)
               {
                  if (ec->layer <= lock_layer)
                    {
                       restack_list = eina_list_append(restack_list, ec);
                    }
               }

             if ((!ec->argb) ||
                 ((ec->argb) &&
                  (ec->visibility.opaque == 1)))
               {
                  e_client_geometry_get(ec, &x, &y, &w, &h);
                  if (E_CONTAINS(x, y, w, h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
                    {
                       break;
                    }
               }
          }

        if (restack_list)
          {
             EINA_LIST_REVERSE_FOREACH(restack_list, l, ec)
               {
                  if (ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved == EINA_FALSE)
                    {
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved = EINA_TRUE;
                       ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer = ec->layer;
                    }

                  ELOGF("CHANGE to Lockscreen layer", "AboveLock|layer: %d -> %d", ec, ec->layer, lock_layer);
                  if (ec->layer == lock_layer)
                    evas_object_raise(ec->frame);
                  else
                    evas_object_layer_set(ec->frame, lock_layer);

                  ec->layer = lock_layer;
               }
             eina_list_free(restack_list);
             restack_list = NULL;
          }
     }

}

Eina_Bool
e_policy_stack_check_above_lockscreen(E_Client *ec, E_Layer layer, E_Layer *new_layer, Eina_Bool set_layer)
{
   E_Layer lock_layer;

   if (!ec) return EINA_FALSE;
   if (!ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].set)
     return EINA_FALSE;

   if (e_policy_system_info.lockscreen.show &&
       e_policy_system_info.lockscreen.ec)
     {
        lock_layer = e_policy_system_info.lockscreen.ec->layer;
        if (layer <= lock_layer)
          {
             if (ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved == EINA_FALSE)
               {
                  ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved = EINA_TRUE;
                  ec->changable_layer[E_CHANGABLE_LAYER_TYPE_ABOVE_NOTIFICATION].saved_layer = ec->layer;
               }

             if (set_layer)
               {
                  ELOGF("CHANGE to Lockscreen layer", "AboveLock|layer: %d -> %d", ec, ec->layer, lock_layer);
                  if (ec->layer == lock_layer)
                    evas_object_raise(ec->frame);
                  else
                    evas_object_layer_set(ec->frame, lock_layer);
                  ec->layer = lock_layer;
               }

             if (new_layer)
               *new_layer = lock_layer;
          }
        else
          {
             if (set_layer)
               {
                  if (ec->layer != layer)
                    {
                       ELOGF("CHANGE to Lockscreen layer", "AboveLock|layer: %d -> %d", ec, ec->layer, lock_layer);
                       evas_object_layer_set(ec->frame, lock_layer);
                       ec->layer = lock_layer;
                    }
               }

             if (new_layer)
               *new_layer = layer;
          }

        return EINA_TRUE;
     }

   return EINA_FALSE;

}

