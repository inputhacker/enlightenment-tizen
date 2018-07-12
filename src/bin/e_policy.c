#include "e.h"
#include "e_policy_conformant_internal.h"
#include "e_policy_wl.h"
#include "e_policy_visibility.h"
#include "e_policy_private_data.h"

E_Policy *e_policy = NULL;
Eina_Hash *hash_policy_desks = NULL;
Eina_Hash *hash_policy_clients = NULL;
E_Policy_System_Info e_policy_system_info =
{
   {NULL, EINA_FALSE},
   { -1, -1, EINA_FALSE}
};

static int _e_policy_interceptors_walking = 0;
static int _e_policy_interceptors_delete = 0;

E_Policy_Interceptor *_e_policy_interceptors[] =
{
   [E_POLICY_INTERCEPT_LAUNCHSCREEN_OBJECT_SETUP] = NULL,
   [E_POLICY_INTERCEPT_STACK_TRANSIENT_FOR] = NULL,
   [E_POLICY_INTERCEPT_ACTIVATE_ABOVE] = NULL,
   [E_POLICY_INTERCEPT_ACTIVATE_BELOW] = NULL,
   [E_POLICY_INTERCEPT_SEND_PRE_VISIBILITY] = NULL,
};

static Eina_List *handlers = NULL;
static Eina_List *hooks_ec = NULL;
static Eina_List *hooks_cp = NULL;
static Ecore_Idle_Enterer *_e_pol_idle_enterer = NULL;
static Eina_Bool _e_pol_changed_vis = EINA_FALSE;
static Eina_List *_e_pol_changed_zone = NULL;
static int _e_policy_hooks_delete = 0;
static int _e_policy_hooks_walking = 0;

static Eina_Inlist *_e_policy_hooks[] =
{
   [E_POLICY_HOOK_CLIENT_POSITION_SET] = NULL,
   [E_POLICY_HOOK_CLIENT_ACTIVE_REQ] = NULL,
   [E_POLICY_HOOK_CLIENT_RAISE_REQ] = NULL,
   [E_POLICY_HOOK_CLIENT_LOWER_REQ] = NULL,
};

static E_Policy_Client *_e_policy_client_add(E_Client *ec);
static void        _e_policy_client_del(E_Policy_Client *pc);
static Eina_Bool   _e_policy_client_normal_check(E_Client *ec);
static Eina_Bool   _e_policy_client_maximize_policy_apply(E_Policy_Client *pc);
static void        _e_policy_client_maximize_policy_cancel(E_Policy_Client *pc);
static void        _e_policy_client_floating_policy_apply(E_Policy_Client *pc);
static void        _e_policy_client_floating_policy_cancel(E_Policy_Client *pc);
static void        _e_policy_client_split_policy_apply(E_Policy_Client *pc);
static void        _e_policy_client_split_policy_cancel(E_Policy_Client *pc);
static void        _e_policy_client_launcher_set(E_Policy_Client *pc);

static void        _e_policy_cb_hook_client_eval_pre_new_client(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_pre_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_pre_post_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_post_fetch(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_eval_post_new_client(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_desk_set(void *d EINA_UNUSED, E_Client *ec);
static void        _e_policy_cb_hook_client_fullscreen_pre(void *data EINA_UNUSED, E_Client *ec);

static void        _e_policy_cb_hook_pixmap_del(void *data EINA_UNUSED, E_Pixmap *cp);
static void        _e_policy_cb_hook_pixmap_unusable(void *data EINA_UNUSED, E_Pixmap *cp);

static void        _e_policy_cb_desk_data_free(void *data);
static void        _e_policy_cb_client_data_free(void *data);
static Eina_Bool   _e_policy_cb_zone_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_del(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_move_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_desk_count_set(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_zone_display_state_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_desk_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_client_add(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_move(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_resize(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_stack(void *data EINA_UNUSED, int type, void *event);
static Eina_Bool   _e_policy_cb_client_property(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_client_vis_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED);
static Eina_Bool   _e_policy_cb_client_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);
static Eina_Bool   _e_policy_cb_client_hide(void *data EINA_UNUSED, int type EINA_UNUSED, void *event);

static Eina_Bool   _e_policy_cb_idle_enterer(void *data EINA_UNUSED);

static void
_e_policy_client_launcher_set(E_Policy_Client *pc)
{
   E_Policy_Client *pc2;

   pc2 = e_policy_client_launcher_get(pc->ec->zone);
   if (pc2) return;

   if (pc->ec->netwm.type != e_config->launcher.type)
     return;

   if (e_util_strcmp(pc->ec->icccm.class,
                     e_config->launcher.clas))
     return;


   if (e_util_strcmp(pc->ec->icccm.title,
                     e_config->launcher.title))
     {
        /* check netwm name instead, because comp_x had ignored
         * icccm name when fetching */
        if (e_util_strcmp(pc->ec->netwm.name,
                          e_config->launcher.title))
          {
             return;
          }
     }

   e_policy->launchers = eina_list_append(e_policy->launchers, pc);
}

static E_Policy_Client *
_e_policy_client_add(E_Client *ec)
{
   E_Policy_Client *pc;

   if (e_object_is_del(E_OBJECT(ec))) return NULL;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (pc) return pc;

   pc = E_NEW(E_Policy_Client, 1);
   if (!pc) return NULL;

   pc->ec = ec;

   eina_hash_add(hash_policy_clients, &ec, pc);

   return pc;
}

static void
_e_policy_client_del(E_Policy_Client *pc)
{
   eina_hash_del_by_key(hash_policy_clients, &pc->ec);
}

static Eina_Bool
_e_policy_client_normal_check(E_Client *ec)
{
   E_Policy_Client *pc;

   if ((e_client_util_ignored_get(ec)) ||
       (!ec->pixmap))
     {
        return EINA_FALSE;
     }

   if (e_policy_client_is_quickpanel(ec))
     {
        return EINA_FALSE;
     }

   if (e_policy_client_is_keyboard(ec) ||
       e_policy_client_is_keyboard_sub(ec))
     {
        e_policy_keyboard_layout_apply(ec);
        goto cancel_max;
     }
   else if (e_policy_client_is_volume_tv(ec))
     goto cancel_max;
   else if (!e_util_strcmp("e_demo", ec->icccm.window_role))
     goto cancel_max;
   else if (e_policy_client_is_floating(ec))
     {
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_floating_policy_apply(pc);
        return EINA_FALSE;
     }
   else if (e_policy_client_is_subsurface(ec))
     goto cancel_max;
   else if (e_policy_client_is_splited(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_split_policy_apply(pc);
        return EINA_FALSE;
     }

   if ((ec->netwm.type == E_WINDOW_TYPE_NORMAL) ||
       (ec->netwm.type == E_WINDOW_TYPE_UNKNOWN) ||
       (ec->netwm.type == E_WINDOW_TYPE_NOTIFICATION))
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;

cancel_max:
   pc = eina_hash_find(hash_policy_clients, &ec);
   _e_policy_client_maximize_policy_cancel(pc);

   return EINA_FALSE;
}

static void
_e_policy_client_maximize_pre(E_Policy_Client *pc)
{
   E_Client *ec;
   int zx, zy, zw, zh;

   ec = pc->ec;

   if (ec->desk->visible)
     e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);
   else
     {
        zx = ec->zone->x;
        zy = ec->zone->y;
        zw = ec->zone->w;
        zh = ec->zone->h;
     }

   ec->x = ec->client.x = zx;
   ec->y = ec->client.y = zy;
   ec->w = ec->client.w = zw;
   ec->h = ec->client.h = zh;

   EC_CHANGED(ec);
}

static Eina_Bool
_e_policy_client_maximize_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;

   if (!pc) return EINA_FALSE;

   if (pc->max_policy_state) return EINA_TRUE;
   if (pc->allow_user_geom) return EINA_FALSE;

   ec = pc->ec;
   if (ec->netwm.type == E_WINDOW_TYPE_UTILITY) return EINA_FALSE;

   pc->max_policy_state = EINA_TRUE;

#undef _SET
# define _SET(a) pc->orig.a = pc->ec->a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   _e_policy_client_launcher_set(pc);

   if (!ec->borderless)
     {
        ec->borderless = 1;
        ec->border.changed = 1;
        EC_CHANGED(pc->ec);
     }

   if (!ec->maximized)
     {
        /* This is added to support e_desk_geometry_set().
         * The geometry of client is calculated based on E_Desk by
         * e_client_maximize() from now.
         * But, if we don't set ec->placed, geometry of new client will be
         * calculated again based on E_Zone by _e_client_eval().
         * FIXME: we can delete it if calculation of placement is based on
         * E_Desk.
         */
        ec->placed = 1;

        e_client_maximize(ec, E_MAXIMIZE_EXPAND | E_MAXIMIZE_BOTH);

        if (ec->changes.need_maximize)
          _e_policy_client_maximize_pre(pc);
     }

   /* do not allow client to change these properties */
   ec->lock_user_location = 1;
   ec->lock_client_location = 1;
   ec->lock_user_size = 1;
   ec->lock_client_size = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;
   ec->skip_fullscreen = 1;

   if (!e_policy_client_is_home_screen(ec))
     ec->lock_client_stacking = 1;

   return EINA_TRUE;
}

static void
_e_policy_client_maximize_policy_cancel(E_Policy_Client *pc)
{
   E_Client *ec;
   Eina_Bool changed = EINA_FALSE;

   if (!pc) return;
   if (!pc->max_policy_state) return;

   pc->max_policy_state = EINA_FALSE;

   ec = pc->ec;

   if (pc->orig.borderless != ec->borderless)
     {
        ec->border.changed = 1;
        changed = EINA_TRUE;
     }

   if ((pc->orig.fullscreen != ec->fullscreen) &&
       (pc->orig.fullscreen))
     {
        ec->need_fullscreen = 1;
        changed = EINA_TRUE;
     }

   if (pc->orig.maximized != ec->maximized)
     {
        if (pc->orig.maximized)
          ec->changes.need_maximize = 1;
        else
          e_client_unmaximize(ec, ec->maximized);

        changed = EINA_TRUE;
     }

   /* floating mode ec which was launched with fake image is not borderless value.
    * thus, we should set borderless value to 1 for this ec to prevent choppy
    * movement of the window when moving the window.
    */
   if (ec->floating)
     {
        pc->orig.borderless = 1;
        changed = EINA_TRUE;
     }

#undef _SET
# define _SET(a) ec->a = pc->orig.a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   ec->skip_fullscreen = 0;

   /* only set it if the border is changed or fullscreen/maximize has changed */
   if (changed)
     EC_CHANGED(pc->ec);

   e_policy->launchers = eina_list_remove(e_policy->launchers, pc);
}

static void
_e_policy_client_dialog_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;
   int x, y, w, h;
   int zx, zy, zw, zh;

   if (!pc) return;
   ec = pc->ec;

   ec->skip_fullscreen = 1;
   ec->lock_client_stacking = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;

   w = ec->w;
   h = ec->h;

   e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

   x = zx + ((zw - w) / 2);
   y = zy + ((zh - h) / 2);

   if ((x != ec->x) || (y != ec->y))
     evas_object_move(ec->frame, x, y);
}

static void
_e_policy_client_floating_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;

   if (!pc) return;
   if (pc->flt_policy_state) return;

   pc->flt_policy_state = EINA_TRUE;
   ec = pc->ec;

#undef _SET
# define _SET(a) pc->orig.a = pc->ec->a
   _SET(fullscreen);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   ec->skip_fullscreen = 1;
   ec->lock_client_stacking = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;
}

static void
_e_policy_client_floating_policy_cancel(E_Policy_Client *pc)
{
   E_Client *ec;
   Eina_Bool changed = EINA_FALSE;

   if (!pc) return;
   if (!pc->flt_policy_state) return;

   pc->flt_policy_state = EINA_FALSE;
   ec = pc->ec;

   if ((pc->orig.fullscreen != ec->fullscreen) &&
       (pc->orig.fullscreen))
     {
        ec->need_fullscreen = 1;
        changed = EINA_TRUE;
     }

   if (pc->orig.maximized != ec->maximized)
     {
        if (pc->orig.maximized)
          ec->changes.need_maximize = 1;
        else
          e_client_unmaximize(ec, ec->maximized);

        changed = EINA_TRUE;
     }

   ec->skip_fullscreen = 0;

#undef _SET
# define _SET(a) ec->a = pc->orig.a
   _SET(fullscreen);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   if (changed)
     EC_CHANGED(pc->ec);
}

static void
_e_policy_client_split_policy_apply(E_Policy_Client *pc)
{
   E_Client *ec;

   if (!pc) return;
   if (pc->split_policy_state) return;

   pc->split_policy_state = EINA_TRUE;
   ec = pc->ec;

#undef _SET
# define _SET(a) pc->orig.a = pc->ec->a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   if (!ec->borderless)
     {
        ec->borderless = 1;
        ec->border.changed = 1;
        EC_CHANGED(pc->ec);
     }

   /* do not allow client to change these properties */
   ec->lock_user_location = 1;
   ec->lock_client_location = 1;
   ec->lock_user_size = 1;
   ec->lock_client_size = 1;
   ec->lock_user_shade = 1;
   ec->lock_client_shade = 1;
   ec->lock_user_maximize = 1;
   ec->lock_client_maximize = 1;
   ec->lock_user_fullscreen = 1;
   ec->lock_client_fullscreen = 1;
   ec->skip_fullscreen = 1;
}

static void
_e_policy_client_split_policy_cancel(E_Policy_Client *pc)
{
   E_Client *ec;
   Eina_Bool changed = EINA_FALSE;

   if (!pc) return;
   if (!pc->split_policy_state) return;

   pc->split_policy_state = EINA_FALSE;
   ec = pc->ec;

   if (pc->orig.borderless != ec->borderless)
     {
        ec->border.changed = 1;
        changed = EINA_TRUE;
     }

   if ((pc->orig.fullscreen != ec->fullscreen) &&
       (pc->orig.fullscreen))
     {
        ec->need_fullscreen = 1;
        changed = EINA_TRUE;
     }

   if (pc->orig.maximized != ec->maximized)
     {
        if (pc->orig.maximized)
          ec->changes.need_maximize = 1;
        else
          e_client_unmaximize(ec, ec->maximized);

        changed = EINA_TRUE;
     }
#undef _SET
# define _SET(a) ec->a = pc->orig.a
   _SET(borderless);
   _SET(fullscreen);
   _SET(maximized);
   _SET(lock_user_location);
   _SET(lock_client_location);
   _SET(lock_user_size);
   _SET(lock_client_size);
   _SET(lock_client_stacking);
   _SET(lock_user_shade);
   _SET(lock_client_shade);
   _SET(lock_user_maximize);
   _SET(lock_client_maximize);
   _SET(lock_user_fullscreen);
   _SET(lock_client_fullscreen);
#undef _SET

   ec->skip_fullscreen = 0;

   /* only set it if the border is changed or fullscreen/maximize has changed */
   if (changed)
     EC_CHANGED(pc->ec);
}

E_Config_Policy_Desk *
_e_policy_desk_get_by_num(unsigned int zone_num, int x, int y)
{
   Eina_List *l;
   E_Config_Policy_Desk *d2;

   EINA_LIST_FOREACH(e_config->policy_desks, l, d2)
     {
        if ((d2->zone_num == zone_num) &&
            (d2->x == x) && (d2->y == y))
          {
             return d2;
          }
     }

   return NULL;
}


static void
_e_policy_cb_hook_client_new(void *d EINA_UNUSED, E_Client *ec)
{
   if (EINA_UNLIKELY(!ec))
     return;

   _e_policy_client_add(ec);
}

static void
_e_policy_cb_hook_client_del(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;

   if (EINA_UNLIKELY(!ec))
     return;

   e_tzsh_indicator_srv_ower_win_update(ec->zone);
   e_policy_wl_win_brightness_apply(ec);
   e_policy_wl_client_del(ec);

   if (e_policy_client_is_lockscreen(ec))
     e_policy_stack_clients_restack_above_lockscreen(ec, EINA_FALSE);

   e_policy_stack_cb_client_remove(ec);
   e_client_visibility_calculate();

   pc = eina_hash_find(hash_policy_clients, &ec);
   _e_policy_client_del(pc);
}

static void
_e_policy_cb_hook_client_eval_pre_new_client(void *d EINA_UNUSED, E_Client *ec)
{
   short ly;

   if (e_object_is_del(E_OBJECT(ec))) return;

   if (e_policy_client_is_keyboard_sub(ec))
     {
        ec->placed = 1;
        ec->exp_iconify.skip_iconify = EINA_TRUE;

        EINA_SAFETY_ON_NULL_RETURN(ec->frame);
        if (ec->layer < E_LAYER_CLIENT_ABOVE)
          evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
     }
   if (e_policy_client_is_noti(ec))
     {
        if (ec->frame)
          {
             ly = evas_object_layer_get(ec->frame);
             ELOGF("NOTI", "         |ec->layer:%d object->layer:%d", ec->pixmap, ec, ec->layer, ly);
             if (ly != ec->layer)
               evas_object_layer_set(ec->frame, ec->layer);
          }
     }

   if (e_policy_client_is_dialog(ec))
     {
        if (ec->frame && !ec->parent)
          {
             if (ec->layer != E_POLICY_DIALOG_LAYER)
               evas_object_layer_set(ec->frame, E_POLICY_DIALOG_LAYER);
          }
     }

   if (e_policy_client_is_floating(ec))
     {
        if (ec->frame)
          {
             if (ec->layer != E_LAYER_CLIENT_ABOVE)
               evas_object_layer_set(ec->frame, E_LAYER_CLIENT_ABOVE);
          }
     }

   if (e_policy_client_is_toast_popup(ec))
     {
        if (ec->frame)
          {
             if (ec->layer != E_POLICY_TOAST_POPUP_LAYER)
               evas_object_layer_set(ec->frame, E_POLICY_TOAST_POPUP_LAYER);
          }
        ec->layer = E_POLICY_TOAST_POPUP_LAYER;
     }
   if (e_policy_client_is_cbhm(ec))
     {
        ec->exp_iconify.skip_iconify = EINA_TRUE;
     }
}

static void
_e_policy_cb_hook_client_eval_pre_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_policy_stack_hook_pre_fetch(ec);
}

static void
_e_policy_cb_hook_client_eval_pre_post_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;

   e_policy_stack_hook_pre_post_fetch(ec);
   e_policy_wl_notification_level_fetch(ec);
   e_policy_wl_eval_pre_post_fetch(ec);
}

static void
_e_policy_cb_hook_client_eval_post_fetch(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;
   E_Policy_Desk *pd;

   if (e_object_is_del(E_OBJECT(ec))) return;
   /* Following E_Clients will be added to module hash and will be managed.
    *
    *  - Not new client: Updating internal info of E_Client has been finished
    *    by e main evaluation, thus module can classify E_Client and manage it.
    *
    *  - New client that has valid buffer: This E_Client has been passed e main
    *    evaluation, and it has handled first wl_surface::commit request.
    */
   if ((ec->new_client) && (!e_pixmap_usable_get(ec->pixmap))) return;

   if (e_policy_client_is_keyboard(ec) ||
       e_policy_client_is_keyboard_sub(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);

        e_policy_keyboard_layout_apply(ec);
     }

   if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        return;
     }

   if (e_policy_client_is_floating(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_floating_policy_apply(pc);
        return;
     }

   if (e_policy_client_is_splited(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_split_policy_apply(pc);
        return;
     }

   if (e_policy_client_is_dialog(ec))
     {
        E_Policy_Client *pc;
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
        _e_policy_client_dialog_policy_apply(pc);
        return;
     }

   if (!_e_policy_client_normal_check(ec)) return;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);
   if (!pd) return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (!pc) return;

   if (pc->flt_policy_state)
     _e_policy_client_floating_policy_cancel(pc);

   if (pc->split_policy_state)
     _e_policy_client_split_policy_cancel(pc);

   _e_policy_client_maximize_policy_apply(pc);
}

static void
_e_policy_cb_hook_client_eval_post_new_client(void *d EINA_UNUSED, E_Client *ec)
{
   int zx, zy, zh, zw;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if ((ec->new_client) && (!e_pixmap_usable_get(ec->pixmap))) return;

   if (e_policy_client_is_lockscreen(ec))
     {
        zx = ec->zone->x;
        zy = ec->zone->y;
        zw = ec->zone->w;
        zh = ec->zone->h;

        if (E_CONTAINS(ec->x, ec->y, ec->w, ec->h, zx, zy, zw, zh))
          e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
     }
}

static void
_e_policy_cb_hook_client_desk_set(void *d EINA_UNUSED, E_Client *ec)
{
   E_Policy_Client *pc;
   E_Policy_Desk *pd;

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!_e_policy_client_normal_check(ec)) return;
   if (ec->internal) return;
   if (ec->new_client) return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (EINA_UNLIKELY(!pc))
     return;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);

   if (pd)
     _e_policy_client_maximize_policy_apply(pc);
   else
     _e_policy_client_maximize_policy_cancel(pc);
}

static void
_e_policy_cb_hook_client_fullscreen_pre(void* data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!_e_policy_client_normal_check(ec)) return;
   if (ec->internal) return;

   ec->skip_fullscreen = 1;
}

static void
_e_policy_cb_hook_client_visibility(void *d EINA_UNUSED, E_Client *ec)
{
   if (ec->visibility.changed)
     {
        if (ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
          {
             e_policy_client_uniconify_by_visibility(ec);
             if (ec->visibility.last_sent_type != E_VISIBILITY_PRE_UNOBSCURED)
               {
                  ELOGF("POL_VIS", "SEND pre-unobscured visibility event", ec->pixmap, ec);
                  e_vis_client_send_pre_visibility_event(ec);
               }
             e_policy_client_visibility_send(ec);
          }
        else
          {
             e_policy_client_visibility_send(ec);
             e_policy_client_iconify_by_visibility(ec);
          }

        e_policy_wl_win_brightness_apply(ec);

        _e_pol_changed_vis = EINA_TRUE;
        if (!eina_list_data_find(_e_pol_changed_zone, ec->zone))
          _e_pol_changed_zone = eina_list_append(_e_pol_changed_zone, ec->zone);
     }
   else
     {
        if (ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
          {
             Eina_Bool obscured_by_alpha_opaque = EINA_FALSE;
             Eina_Bool find_above = EINA_FALSE;
             E_Client *above_ec;
             Evas_Object *o;

             if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_ON)
               {
                  if (!E_CONTAINS(ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h, ec->x, ec->y, ec->w, ec->h))
                    {
                       if (ec->visibility.last_sent_type == E_VISIBILITY_PRE_UNOBSCURED)
                         {
                            ELOGF("POL_VIS", "SEND unobscured/fully-obscured visibility event because iconify visibility", ec->pixmap, ec);
                            e_policy_wl_visibility_send(ec, E_VISIBILITY_UNOBSCURED);
                            e_policy_wl_visibility_send(ec, E_VISIBILITY_FULLY_OBSCURED);
                         }
                       e_policy_client_iconify_by_visibility(ec);
                       return;
                    }

                  for (o = evas_object_above_get(ec->frame); o; o = evas_object_above_get(o))
                    {
                       above_ec = evas_object_data_get(o, "E_Client");
                       if (!above_ec) continue;
                       if (e_client_util_ignored_get(above_ec)) continue;
                       if (!above_ec->visible) continue;

                       if (above_ec->exp_iconify.by_client) continue;
                       if (above_ec->exp_iconify.skip_iconify) continue;
                       if (above_ec->exp_iconify.skip_by_remote) continue;

                       if (above_ec->argb)
                         {
                            if (above_ec->visibility.opaque <= 0)
                              continue;
                            else
                              {
                                 if (!above_ec->iconic)
                                   obscured_by_alpha_opaque = EINA_TRUE;
                              }
                         }

                       find_above = EINA_TRUE;
                       break;
                    }

                  if (!find_above) return;
                  if (obscured_by_alpha_opaque)
                    {
                       e_policy_client_uniconify_by_visibility(ec);
                    }
                  else
                    {
                       if (ec->visibility.last_sent_type == E_VISIBILITY_PRE_UNOBSCURED)
                         {
                            if (!e_policy_visibility_client_is_uniconify_render_running(ec))
                              {
                                 ELOGF("POL_VIS", "SEND unobscured/fully-obscured visibility event because iconify visibility", ec->pixmap, ec);
                                 e_policy_wl_visibility_send(ec, E_VISIBILITY_UNOBSCURED);
                                 e_policy_wl_visibility_send(ec, E_VISIBILITY_FULLY_OBSCURED);
                              }
                         }
                       e_policy_client_iconify_by_visibility(ec);
                    }
               }
             else if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
               {
                  if (e_client_util_ignored_get(ec)) return;
                  if (ec->exp_iconify.by_client) return;
                  if (ec->exp_iconify.skip_iconify) return;
                  if (ec->exp_iconify.skip_by_remote) return;
                  if (!ec->iconic)
                    {
                       e_policy_client_iconify_by_visibility(ec);
                    }
               }
          }
     }
}

static void
_e_policy_cb_hook_client_uniconify(void *d EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!e_policy_wl_iconify_state_supported_get(ec))
     {
        ELOGF("TZPOL", "Force Update the client not supporting iconify state",
              ec->pixmap, ec);

        /* force render for an iconifed e_client having shm buffer not used yet*/
        if ((e_pixmap_image_data_get(ec->pixmap)) &&
            (!e_pixmap_dirty_get(ec->pixmap)))
          {
             e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
             e_comp_object_dirty(ec->frame);
             e_comp_object_render(ec->frame);
          }
     }
}

static void
_e_policy_cb_hook_pixmap_del(void *data EINA_UNUSED, E_Pixmap *cp)
{
   e_policy_wl_pixmap_del(cp);
}

static void
_e_policy_cb_hook_pixmap_unusable(void *data EINA_UNUSED, E_Pixmap *cp)
{
   E_Client *ec = (E_Client *)e_pixmap_client_get(cp);

   if (!ec) return;
   if (!ec->iconic) return;
   if (ec->exp_iconify.by_client) return;
   if (ec->exp_iconify.skip_iconify) return;
   if (ec->exp_iconify.skip_by_remote) return;
   if (ec->remote_surface.bind_ref > 0) return;

   e_policy_client_unmap(ec);
}

static void
_e_policy_cb_desk_data_free(void *data)
{
   free(data);
}

static void
_e_policy_cb_client_data_free(void *data)
{
   free(data);
}

static Eina_Bool
_e_policy_cb_zone_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Add *ev;
   E_Zone *zone;
   E_Config_Policy_Desk *d;
   E_Policy_Softkey *softkey;
   int i, n;

   ev = event;
   zone = ev->zone;
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        d = _e_policy_desk_get_by_num(zone->num,
                                      zone->desks[i]->x,
                                      zone->desks[i]->y);
        if (d)
          e_policy_desk_add(zone->desks[i]);
     }

   /* add and show softkey */
   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(zone);
        if (!softkey)
          softkey = e_policy_softkey_add(zone);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_del(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Del *ev;
   E_Zone *zone;
   E_Policy_Desk *pd;
   E_Policy_Softkey *softkey;

   int i, n;

   ev = event;
   zone = ev->zone;
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        pd = eina_hash_find(hash_policy_desks, &zone->desks[i]);
        if (pd) e_policy_desk_del(pd);
     }

   /* add and show softkey */
   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(zone);
        if (softkey)
          e_policy_softkey_del(softkey);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_move_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Move_Resize *ev;
   E_Policy_Softkey *softkey;

   ev = event;

   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(ev->zone);
        e_policy_softkey_update(softkey);
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_desk_count_set(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Desk_Count_Set *ev;
   E_Zone *zone;
   E_Desk *desk;
   Eina_Iterator *it;
   E_Policy_Desk *pd;
   E_Config_Policy_Desk *d;
   int i, n;
   Eina_Bool found;
   Eina_List *desks_del = NULL;

   ev = event;
   zone = ev->zone;

   /* remove deleted desk from hash */
   it = eina_hash_iterator_data_new(hash_policy_desks);
   while (eina_iterator_next(it, (void **)&pd))
     {
        if (pd->zone != zone) continue;

        found = EINA_FALSE;
        n = zone->desk_y_count * zone->desk_x_count;
        for (i = 0; i < n; i++)
          {
             if (pd->desk == zone->desks[i])
               {
                  found = EINA_TRUE;
                  break;
               }
          }
        if (!found)
          desks_del = eina_list_append(desks_del, pd->desk);
     }
   eina_iterator_free(it);

   EINA_LIST_FREE(desks_del, desk)
     {
        pd = eina_hash_find(hash_policy_desks, &desk);
        if (pd) e_policy_desk_del(pd);
     }

   /* add newly added desk to hash */
   n = zone->desk_y_count * zone->desk_x_count;
   for (i = 0; i < n; i++)
     {
        d = _e_policy_desk_get_by_num(zone->num,
                                      zone->desks[i]->x,
                                      zone->desks[i]->y);
        if (d)
          e_policy_desk_add(zone->desks[i]);
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_zone_display_state_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Zone_Display_State_Change *ev;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   e_client_visibility_calculate();

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_desk_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Desk_Show *ev;
   E_Policy_Softkey *softkey;

   ev = event;

   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(ev->desk->zone);
        if (!softkey)
          softkey = e_policy_softkey_add(ev->desk->zone);

        if (eina_hash_find(hash_policy_desks, &ev->desk))
          e_policy_softkey_show(softkey);
        else
          e_policy_softkey_hide(softkey);
     }
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_add(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   e_policy_wl_client_add(ev->ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_move(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   int zx, zy, zw, zh;

   ev = event;
   if (!ev) goto end;

   ec = ev->ec;
   if (!ec) goto end;

   e_policy_wl_position_send(ec);
   e_client_visibility_calculate();

   if (e_policy_client_is_lockscreen(ec))
     {
        zx = ec->zone->x;
        zy = ec->zone->y;
        zw = ec->zone->w;
        zh = ec->zone->h;

        if (E_CONTAINS(ec->x, ec->y, ec->w, ec->h, zx, zy, zw, zh))
          e_policy_stack_clients_restack_above_lockscreen(ev->ec, EINA_TRUE);
        else
          e_policy_stack_clients_restack_above_lockscreen(ev->ec, EINA_FALSE);
     }

end:
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_resize(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;
   int zh = 0;

   ev = (E_Event_Client *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, ECORE_CALLBACK_PASS_ON);

   /* re-calculate window's position with changed size */
   if (e_policy_client_is_volume_tv(ec))
     {
        e_zone_useful_geometry_get(ec->zone, NULL, NULL, NULL, &zh);
        evas_object_move(ec->frame, 0, (zh / 2) - (ec->h / 2));

        evas_object_pass_events_set(ec->frame, 1);
     }

   /* calculate e_client visibility */
   e_client_visibility_calculate();

   return ECORE_CALLBACK_PASS_ON;
}

static E_Client *
_e_policy_client_find_above(const E_Client *ec)
{
   unsigned int x;
   E_Client *ec2;

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (EINA_INLIST_GET(ec)->next) //check current layer
     {
        EINA_INLIST_FOREACH(EINA_INLIST_GET(ec)->next, ec2)
          {
             if ((!e_object_is_del(E_OBJECT(ec2))) &&
                 (!e_client_util_ignored_get(ec2)) &&
                 (ec2->visible) &&
                 (ec2->frame))
               return ec2;
          }
     }
   if (ec->layer == E_LAYER_CLIENT_ALERT) return NULL;
   if (e_comp_canvas_client_layer_map(ec->layer) == 9999) return NULL;

   /* go up the layers until we find one */
   for (x = e_comp_canvas_layer_map(ec->layer) + 1; x <= e_comp_canvas_layer_map(E_LAYER_CLIENT_ALERT); x++)
     {
        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_FOREACH(e_comp->layers[x].clients, ec2)
          {
             if (ec2 == ec) continue;
             if ((!e_object_is_del(E_OBJECT(ec2))) &&
                 (!e_client_util_ignored_get(ec2)) &&
                 (ec2->visible) &&
                 (ec2->frame))
               return ec2;
          }
     }
   return NULL;
}

static E_Client *
_e_policy_client_find_below(const E_Client *ec)
{
   unsigned int x;
   E_Client *ec2;
   Eina_Inlist *l;

   E_OBJECT_CHECK_RETURN(ec, NULL);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, NULL);

   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   if (EINA_INLIST_GET(ec)->prev) //check current layer
     {
        for (l = EINA_INLIST_GET(ec)->prev; l; l = l->prev)
          {
             ec2 = EINA_INLIST_CONTAINER_GET(l, E_Client);
             if ((!e_object_is_del(E_OBJECT(ec2))) &&
                 (!e_client_util_ignored_get(ec2)) &&
                 (ec2->visible) &&
                 (ec2->frame))
               return ec2;
          }
     }

   /* go down the layers until we find one */
   if (e_comp_canvas_layer_map(ec->layer) > e_comp_canvas_layer_map(E_LAYER_MAX)) return NULL;
   x = e_comp_canvas_layer_map(ec->layer);
   if (x > 0) x--;

   for (; x >= e_comp_canvas_layer_map(E_LAYER_CLIENT_DESKTOP); x--)
     {
        if (!e_comp->layers[x].clients) continue;
        EINA_INLIST_REVERSE_FOREACH(e_comp->layers[x].clients, ec2)
          {
             if (ec2 == ec) continue;
             if ((!e_object_is_del(E_OBJECT(ec2))) &&
                 (!e_client_util_ignored_get(ec2)) &&
                 (ec2->visible) &&
                 (ec2->frame))
               return ec2;
          }
     }
   return NULL;
}

static void
_e_policy_client_stack_change_send(E_Client *ec)
{
   E_Client *above = NULL;
   E_Client *below = NULL;
   int above_pid = -1;
   int below_pid = -1;

   above = _e_policy_client_find_above(ec);
   below = _e_policy_client_find_below(ec);

   if (above) above_pid = above->netwm.pid;
   if (below) below_pid = below->netwm.pid;

   ELOGF("TZPOL", "Send stack change.  above(win:%x, pid:%d), below(win:%x, pid:%d)",
         ec->pixmap, ec, e_client_util_win_get(above), above_pid, e_client_util_win_get(below), below_pid);

   e_policy_aux_message_send_from_int(ec, "stack_changed", "pid", 2, above_pid, below_pid);
}

static Eina_Bool
_e_policy_cb_client_stack(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;
   /* calculate e_client visibility */
   e_client_visibility_calculate();

   // send stack change event
   _e_policy_client_stack_change_send(ev->ec);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_property(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client_Property *ev;

   ev = event;
   if (!ev || (!ev->ec)) return ECORE_CALLBACK_PASS_ON;
   if (ev->property & E_CLIENT_PROPERTY_CLIENT_TYPE)
     {
        if (e_policy_client_is_home_screen(ev->ec))
          {
             ev->ec->lock_client_stacking = 0;
             return ECORE_CALLBACK_PASS_ON;
          }
        else if (e_policy_client_is_lockscreen(ev->ec))
          return ECORE_CALLBACK_PASS_ON;
     }

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_vis_change(void *data EINA_UNUSED, int type EINA_UNUSED, void *event EINA_UNUSED)
{
   e_policy_wl_win_scrmode_apply();
   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_show(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   e_policy_stack_check_above_lockscreen(ec, ec->layer, NULL, EINA_TRUE);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_client_hide(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;
   E_Client *ec;

   ev = event;
   if (!ev) return ECORE_CALLBACK_PASS_ON;

   ec = ev->ec;
   e_tzsh_indicator_srv_ower_win_update(ec->zone);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_policy_cb_idle_enterer(void *data EINA_UNUSED)
{
   E_Zone *zone;

   if (_e_pol_changed_vis)
     {
        EINA_LIST_FREE(_e_pol_changed_zone, zone)
          {
             e_tzsh_indicator_srv_ower_win_update(zone);
          }
        _e_pol_changed_zone = NULL;
     }
   _e_pol_changed_vis = EINA_FALSE;

   return ECORE_CALLBACK_RENEW;
}

void
e_policy_desk_add(E_Desk *desk)
{
   E_Policy_Desk *pd;
   E_Client *ec;
   E_Policy_Client *pc;

   pd = eina_hash_find(hash_policy_desks, &desk);
   if (pd) return;

   pd = E_NEW(E_Policy_Desk, 1);
   if (!pd) return;

   pd->desk = desk;
   pd->zone = desk->zone;

   eina_hash_add(hash_policy_desks, &desk, pd);

   /* add clients */
   E_CLIENT_FOREACH(ec)
     {
       if (pd->desk == ec->desk)
         {
            pc = eina_hash_find(hash_policy_clients, &ec);
            _e_policy_client_maximize_policy_apply(pc);
         }
     }
}

void
e_policy_desk_del(E_Policy_Desk *pd)
{
   Eina_Iterator *it;
   E_Policy_Client *pc;
   E_Client *ec;
   Eina_List *clients_del = NULL;
   E_Policy_Softkey *softkey;

   /* hide and delete softkey */
   if (e_config->use_softkey)
     {
        softkey = e_policy_softkey_get(pd->zone);
        if (e_desk_current_get(pd->zone) == pd->desk)
          e_policy_softkey_hide(softkey);
     }

   /* remove clients */
   it = eina_hash_iterator_data_new(hash_policy_clients);
   while (eina_iterator_next(it, (void **)&pc))
     {
        if (pc->ec->desk == pd->desk)
          clients_del = eina_list_append(clients_del, pc->ec);
     }
   eina_iterator_free(it);

   EINA_LIST_FREE(clients_del, ec)
     {
        pc = eina_hash_find(hash_policy_clients, &ec);
        _e_policy_client_maximize_policy_cancel(pc);
     }

   eina_hash_del_by_key(hash_policy_desks, &pd->desk);
}

E_Policy_Client *
e_policy_client_launcher_get(E_Zone *zone)
{
   E_Policy_Client *pc;
   Eina_List *l;

   EINA_LIST_FOREACH(e_policy->launchers, l, pc)
     {
        if (pc->ec->zone == zone)
          return pc;
     }
   return NULL;
}

void
e_policy_client_unmap(E_Client *ec)
{
   Eina_Bool send_event = EINA_FALSE;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   ELOG("Reset ec information by unmap", ec->pixmap, ec);

   if (ec->iconic)
     send_event = EINA_TRUE;

   ec->deskshow = 0;
   ec->iconic = 0;

   ec->exp_iconify.by_client = 0;
   ec->exp_iconify.not_raise = 0;
   ec->exp_iconify.skip_iconify = 0;

   if (send_event)
     e_policy_wl_iconify_state_change_send(ec, 0);
}

Eina_Bool
e_policy_client_maximize(E_Client *ec)
{
   E_Policy_Desk *pd;
   E_Policy_Client *pc;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;

   if ((e_policy_client_is_keyboard(ec)) ||
       (e_policy_client_is_keyboard_sub(ec)) ||
       (e_policy_client_is_floating(ec)) ||
       (e_policy_client_is_quickpanel(ec)) ||
       (e_policy_client_is_volume(ec)) ||
       (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role)) ||
       (!e_util_strcmp("e_demo", ec->icccm.window_role)))
     return EINA_FALSE;

   if (e_policy_client_is_subsurface(ec)) return EINA_FALSE;

   if (e_policy_client_is_splited(ec))
      return EINA_FALSE;

   if ((ec->netwm.type != E_WINDOW_TYPE_NORMAL) &&
       (ec->netwm.type != E_WINDOW_TYPE_UNKNOWN) &&
       (ec->netwm.type != E_WINDOW_TYPE_NOTIFICATION))
     return EINA_FALSE;

   pd = eina_hash_find(hash_policy_desks, &ec->desk);
   if (!pd) return EINA_FALSE;

   pc = eina_hash_find(hash_policy_clients, &ec);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pc, EINA_FALSE);

   if (pc->flt_policy_state)
     _e_policy_client_floating_policy_cancel(pc);

   return _e_policy_client_maximize_policy_apply(pc);
}

EINTERN void
e_policy_keyboard_layout_apply(E_Client *ec EINA_UNUSED)
{
/* FIXME: do not resize and move client.
 * ec->e.state.rot.geom[].w/h is always 0,
 * then the geometry calculated here is not valid. */
#if 0
   int angle;
   int angle_id = 0;
   int kbd_x, kbd_y, kbd_w, kbd_h;

   if (!e_policy_client_is_keyboard(ec) &&
       !e_policy_client_is_keyboard_sub(ec))
      return;

   angle = e_client_rotation_curr_angle_get(ec);

   switch (angle)
     {
      case 0: angle_id = 0; break;
      case 90: angle_id = 1; break;
      case 180: angle_id = 2; break;
      case 270: angle_id = 3; break;
      default: angle_id = 0; break;
     }

   kbd_w = ec->e.state.rot.geom[angle_id].w;
   kbd_h = ec->e.state.rot.geom[angle_id].h;

   switch (angle)
     {
      case 0:
         kbd_x = ec->zone->w - kbd_w;
         kbd_y = ec->zone->h - kbd_h;
         break;

      case 90:
         kbd_x = ec->zone->w - kbd_w;
         kbd_y = ec->zone->h - kbd_h;
         break;

      case 180:
         kbd_x = 0;
         kbd_y = 0;
         break;

      case 270:
         kbd_x = 0;
         kbd_y = 0;
         break;

      default:
         kbd_x = ec->zone->w - kbd_w;
         kbd_y = ec->zone->h - kbd_h;
         break;
     }

   if ((ec->frame) &&
       ((ec->w != kbd_w) || (ec->h != kbd_h)))
     e_client_util_resize_without_frame(ec, kbd_w, kbd_h);

   if ((e_policy_client_is_keyboard(ec)) &&
       (ec->frame) &&
       ((ec->x != kbd_x) || (ec->y != kbd_y)))
     e_client_util_move_without_frame(ec, kbd_x, kbd_y);
#endif
}

Eina_Bool
e_policy_client_is_lockscreen(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->client_type == 2)
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.title, "LOCKSCREEN"))
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.window_role, "lockscreen"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_home_screen(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->client_type == 1)
     return EINA_TRUE;


   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_quickpanel(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.window_role, "quickpanel"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_conformant(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cdata->conformant == 1)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_volume(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->netwm.name, "volume"))
     return EINA_TRUE;

   if (!e_util_strcmp(ec->icccm.title, "volume"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_volume_tv(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.window_role, "tv-volume-popup"))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_noti(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp(ec->icccm.title, "noti_win"))
     return EINA_TRUE;

   if (ec->netwm.type == E_WINDOW_TYPE_NOTIFICATION)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_subsurface(E_Client *ec)
{
   E_Comp_Wl_Client_Data *cd;

   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   cd = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cd && cd->sub.data)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_floating(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   return ec->floating;
}

Eina_Bool
e_policy_client_is_cursor(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp("wl_pointer-cursor", ec->icccm.window_role))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_cbhm(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp("cbhm", ec->icccm.window_role))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_toast_popup(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (!e_util_strcmp("TOAST_POPUP", ec->icccm.class))
     return EINA_TRUE;

   if (!e_util_strcmp("toast_popup", ec->icccm.window_role))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_dialog(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->netwm.type == E_WINDOW_TYPE_DIALOG)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_splited(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->layout.splited)
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_keyboard(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->vkbd.vkbd) return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_keyboard_sub(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->vkbd.vkbd) return EINA_FALSE;

   if ((ec->icccm.class) &&
       (!strcmp(ec->icccm.class, "ISF")))
     return EINA_TRUE;
   if ((ec->icccm.title) &&
       ((!strcmp(ec->icccm.title, "ISF Popup")) || (!strcmp(ec->icccm.title, "ISF Magnifier"))))
     return EINA_TRUE;

   return EINA_FALSE;
}

Eina_Bool
e_policy_client_is_keyboard_magnifier(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->vkbd.vkbd) return EINA_FALSE;

   if ((ec->icccm.title) && (!strcmp(ec->icccm.title, "ISF Magnifier")))
     return EINA_TRUE;

   return EINA_FALSE;
}

void
e_policy_interceptors_clean(void)
{
   E_Policy_Interceptor *pi;
   unsigned int x;

   for (x = 0; x < E_POLICY_INTERCEPT_LAST; x++)
     {
        pi = _e_policy_interceptors[x];
        if (!pi->delete_me) continue;
        _e_policy_interceptors[x] = NULL;
        free(pi);
     }
}

/*
 * It returns
 * EINA_TRUE,
 *  if interceptor process something successfully at its intercept point,
 * EINA_FALSE,
 *  if interceptor failed or there is no interceptor.
 */
Eina_Bool
e_policy_interceptor_call(E_Policy_Intercept_Point ipoint, E_Client *ec, ...)
{
   va_list list;
   E_Policy_Interceptor *pi;
   Eina_Bool ret = EINA_TRUE;

   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   pi = _e_policy_interceptors[ipoint];
   if (!pi) return EINA_FALSE;

   va_start(list, ec);

   e_object_ref(E_OBJECT(ec));
   _e_policy_interceptors_walking++;
   if (!pi->delete_me)
     {
        if (!(pi->func(pi->data, ec, list)))
          ret = EINA_FALSE;
     }
   _e_policy_interceptors_walking--;
   if ((_e_policy_interceptors_walking == 0) && (_e_policy_interceptors_delete > 0))
     e_policy_interceptors_clean();

   va_end(list);
   e_object_unref(E_OBJECT(ec));
   return ret;
}

static void
_e_policy_event_simple_free(void *d EINA_UNUSED, E_Event_Client *ev)
{
   e_object_unref(E_OBJECT(ev->ec));
   free(ev);
}

void
e_policy_event_simple(E_Client *ec, int type)
{
   E_Event_Client *ev;

   ev = E_NEW(E_Event_Client, 1);
   if (!ev) return;

   ev->ec = ec;
   e_object_ref(E_OBJECT(ec));
   ecore_event_add(type, ev, (Ecore_End_Cb)_e_policy_event_simple_free, NULL);
}

E_API Eina_Bool
e_policy_aux_message_use_get(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec->comp_data, EINA_FALSE);

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cdata->aux_hint.use_msg)
     {
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

E_API void
e_policy_aux_message_send_from_int(E_Client *ec, const char *key, const char *val, int count, ...)
{
   char option[4096];
   char *str_itor;
   Eina_List *options_list = NULL;
   va_list opt_args;
   int opt;
   int itor;

   va_start(opt_args, count);
   for(itor = 0; itor < count; itor ++)
    {
       opt = va_arg(opt_args, int);
       eina_convert_itoa(opt, option);
       options_list = eina_list_append(options_list, eina_stringshare_add(option));
    }
   va_end(opt_args);

   e_policy_aux_message_send(ec, key, val, options_list);

   EINA_LIST_FREE(options_list, str_itor)
    {
       eina_stringshare_del(str_itor);
    }
}

E_API void
e_policy_aux_message_send(E_Client *ec, const char *key, const char *val, Eina_List *options)
{
   E_OBJECT_CHECK(ec);
   E_OBJECT_TYPE_CHECK(ec, E_CLIENT_TYPE);

   e_policy_wl_aux_message_send(ec, key, val, options);
}

E_API E_Policy_Interceptor *
e_policy_interceptor_add(E_Policy_Intercept_Point ipoint, E_Policy_Intercept_Cb func, const void *data)
{
   E_Policy_Interceptor *pi;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(ipoint >= E_POLICY_INTERCEPT_LAST, NULL);
   EINA_SAFETY_ON_TRUE_RETURN_VAL(!!_e_policy_interceptors[ipoint], NULL);
   pi = E_NEW(E_Policy_Interceptor, 1);
   if (!pi) return NULL;
   pi->ipoint = ipoint;
   pi->func = func;
   pi->data = (void*)data;
   _e_policy_interceptors[ipoint] = pi;
   return pi;
}

E_API void
e_policy_interceptor_del(E_Policy_Interceptor *pi)
{
   pi->delete_me = 1;
   if (_e_policy_interceptors_walking == 0)
     {
        _e_policy_interceptors[pi->ipoint] = NULL;
        free(pi);
     }
   else
     _e_policy_interceptors_delete++;
}

E_API E_Policy_Hook *
e_policy_hook_add(E_Policy_Hook_Point hookpoint, E_Policy_Hook_Cb func, const void *data)
{
   E_Policy_Hook *ph;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(hookpoint >= E_POLICY_HOOK_LAST, NULL);
   ph = E_NEW(E_Policy_Hook, 1);
   if (!ph) return NULL;
   ph->hookpoint = hookpoint;
   ph->func = func;
   ph->data = (void*)data;
   _e_policy_hooks[hookpoint] = eina_inlist_append(_e_policy_hooks[hookpoint], EINA_INLIST_GET(ph));
   return ph;
}

E_API void
e_policy_hook_del(E_Policy_Hook *ph)
{
   ph->delete_me = 1;
   if (_e_policy_hooks_walking == 0)
     {
        _e_policy_hooks[ph->hookpoint] = eina_inlist_remove(_e_policy_hooks[ph->hookpoint], EINA_INLIST_GET(ph));
        free(ph);
     }
   else
     _e_policy_hooks_delete++;
}

static void
_e_policy_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Policy_Hook *ph;
   unsigned int x;

   for (x = 0; x < E_POLICY_HOOK_LAST; x++)
     {
        EINA_INLIST_FOREACH_SAFE(_e_policy_hooks[x], l, ph)
          {
             if (!ph->delete_me) continue;
             _e_policy_hooks[x] = eina_inlist_remove(_e_policy_hooks[x], EINA_INLIST_GET(ph));
             free(ph);
          }
     }
}

E_API Eina_Bool
e_policy_hook_call(E_Policy_Hook_Point hookpoint, E_Client *ec)
{
   E_Policy_Hook *ch;

   e_object_ref(E_OBJECT(ec));
   _e_policy_hooks_walking++;

   EINA_INLIST_FOREACH(_e_policy_hooks[hookpoint], ch)
     {
        if (ch->delete_me) continue;
        ch->func(ch->data, ec);
     }
   _e_policy_hooks_walking--;
   if ((_e_policy_hooks_walking == 0) && (_e_policy_hooks_delete > 0))
     _e_policy_hooks_clean();
   return !!e_object_unref(E_OBJECT(ec));
}

E_API void
e_policy_allow_user_geometry_set(E_Client *ec, Eina_Bool set)
{
   E_Policy_Client *pc;

   if (EINA_UNLIKELY(!ec))
     return;

   pc = eina_hash_find(hash_policy_clients, &ec);
   if (EINA_UNLIKELY(!pc))
     return;

   if (set) pc->user_geom_ref++;
   else     pc->user_geom_ref--;

   if (pc->user_geom_ref == 1 && !pc->allow_user_geom)
     {
        pc->allow_user_geom = EINA_TRUE;

        if (!e_policy_client_is_noti(ec))
          {
             ec->netwm.type = E_WINDOW_TYPE_UTILITY;
             ec->lock_client_location = EINA_FALSE;
          }

        ec->lock_client_size = EINA_FALSE;
        ec->placed = 1;

        _e_policy_client_maximize_policy_cancel(pc);
        EC_CHANGED(ec);
     }
   else if (pc->user_geom_ref == 0 && pc->allow_user_geom)
     {
        pc->allow_user_geom = EINA_FALSE;

        ec->lock_client_location = EINA_TRUE;
        ec->lock_client_size = EINA_TRUE;
        ec->placed = 0;
        ec->netwm.type = E_WINDOW_TYPE_NORMAL;
        EC_CHANGED(ec);
     }
}

E_API void
e_policy_deferred_job(void)
{
   if (!e_policy) return;

   e_policy_wl_defer_job();
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

#undef E_PIXMAP_HOOK_APPEND
#define E_PIXMAP_HOOK_APPEND(l, t, cb, d) \
  do                                      \
    {                                     \
       E_Pixmap_Hook *_h;                 \
       _h = e_pixmap_hook_add(t, cb, d);  \
       assert(_h);                        \
       l = eina_list_append(l, _h);       \
    }                                     \
  while (0)

E_API int
e_policy_init(void)
{
   E_Policy *pol;
   E_Zone *zone;
   E_Config_Policy_Desk *d;
   const Eina_List *l;
   int i, n;

   pol = E_NEW(E_Policy, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(pol, EINA_FALSE);

   e_policy = pol;

   hash_policy_clients = eina_hash_pointer_new(_e_policy_cb_client_data_free);
   hash_policy_desks = eina_hash_pointer_new(_e_policy_cb_desk_data_free);

   e_policy_stack_init();
   e_policy_wl_init();
   e_policy_wl_aux_hint_init();

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     {
        n = zone->desk_y_count * zone->desk_x_count;
        for (i = 0; i < n; i++)
          {
             d = _e_policy_desk_get_by_num(zone->num,
                                           zone->desks[i]->x,
                                           zone->desks[i]->y);
             if (d)
               e_policy_desk_add(zone->desks[i]);
          }
     }

   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_ADD,                  _e_policy_cb_zone_add,                        NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DEL,                  _e_policy_cb_zone_del,                        NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_MOVE_RESIZE,          _e_policy_cb_zone_move_resize,                NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DESK_COUNT_SET,       _e_policy_cb_zone_desk_count_set,             NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_ZONE_DISPLAY_STATE_CHANGE, _e_policy_cb_zone_display_state_change,       NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_DESK_SHOW,                 _e_policy_cb_desk_show,                       NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_ADD,                _e_policy_cb_client_add,                      NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_MOVE,               _e_policy_cb_client_move,                     NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_RESIZE,             _e_policy_cb_client_resize,                   NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_STACK,              _e_policy_cb_client_stack,                    NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_PROPERTY,           _e_policy_cb_client_property,                 NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_VISIBILITY_CHANGE,  _e_policy_cb_client_vis_change,               NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_SHOW,               _e_policy_cb_client_show,                     NULL);
   E_LIST_HANDLER_APPEND(handlers, E_EVENT_CLIENT_HIDE,               _e_policy_cb_client_hide,                     NULL);

   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_NEW_CLIENT,          _e_policy_cb_hook_client_new,                 NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_DEL,                 _e_policy_cb_hook_client_del,                 NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_NEW_CLIENT, _e_policy_cb_hook_client_eval_pre_new_client, NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_FETCH,      _e_policy_cb_hook_client_eval_pre_fetch,      NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_PRE_POST_FETCH, _e_policy_cb_hook_client_eval_pre_post_fetch, NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_POST_FETCH,     _e_policy_cb_hook_client_eval_post_fetch,     NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_POST_NEW_CLIENT,_e_policy_cb_hook_client_eval_post_new_client,NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_DESK_SET,            _e_policy_cb_hook_client_desk_set,            NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_FULLSCREEN_PRE,      _e_policy_cb_hook_client_fullscreen_pre,      NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_EVAL_VISIBILITY,     _e_policy_cb_hook_client_visibility,          NULL);
   E_CLIENT_HOOK_APPEND(hooks_ec,  E_CLIENT_HOOK_UNICONIFY,           _e_policy_cb_hook_client_uniconify,           NULL);

   E_PIXMAP_HOOK_APPEND(hooks_cp,  E_PIXMAP_HOOK_DEL,                 _e_policy_cb_hook_pixmap_del,                 NULL);
   E_PIXMAP_HOOK_APPEND(hooks_cp,  E_PIXMAP_HOOK_UNUSABLE,            _e_policy_cb_hook_pixmap_unusable,            NULL);

   _e_pol_idle_enterer = ecore_idle_enterer_add(_e_policy_cb_idle_enterer, NULL);

   e_policy_conformant_init();
   e_policy_visibility_init();

   return EINA_TRUE;
}

E_API int
e_policy_shutdown(void)
{
   E_Policy *pol = e_policy;
   Eina_Inlist *l;
   E_Policy_Softkey *softkey;

   eina_list_free(_e_pol_changed_zone);
   eina_list_free(pol->launchers);
   EINA_INLIST_FOREACH_SAFE(pol->softkeys, l, softkey)
     e_policy_softkey_del(softkey);
   E_FREE_LIST(hooks_cp, e_pixmap_hook_del);
   E_FREE_LIST(hooks_ec, e_client_hook_del);
   E_FREE_LIST(handlers, ecore_event_handler_del);

   E_FREE_FUNC(hash_policy_desks, eina_hash_free);
   E_FREE_FUNC(hash_policy_clients, eina_hash_free);

   e_policy_stack_shutdonw();
   e_policy_wl_shutdown();

   e_policy_conformant_shutdown();
   e_policy_visibility_shutdown();

   E_FREE(pol);

   e_policy = NULL;

   return 1;
}
