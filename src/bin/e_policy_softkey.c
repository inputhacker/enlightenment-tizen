#include "e.h"

static void         _e_policy_cb_softkey(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source EINA_UNUSED);
static void         _e_policy_softkey_iconify(E_Zone *zone, Eina_Bool all);
static Evas_Object *_e_policy_softkey_icon_add(E_Zone *zone, const char *name);
static void         _e_policy_softkey_icon_del(Evas_Object *comp_obj);

static E_Policy_Softkey_Funcs *_e_softkey_funcs = NULL;

static void
_e_policy_cb_softkey(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source EINA_UNUSED)
{
   E_Zone *zone;
   Eina_Bool all;

   zone = data;

   if (!e_util_strcmp(emission, "e,action,softkey,home"))
     all = EINA_TRUE;
   else if (!e_util_strcmp(emission, "e,action,softkey,back"))
     all = EINA_FALSE;
   else
     return;

   _e_policy_softkey_iconify(zone, all);
}

static void
_e_policy_softkey_iconify(E_Zone *zone, Eina_Bool all)
{
   E_Desk *desk;
   E_Client *ec;
   E_Policy_Client *launcher;

   desk = e_desk_current_get(zone);
   launcher = e_policy_client_launcher_get(zone);

   E_CLIENT_REVERSE_FOREACH(ec)
     {
        if (e_client_util_ignored_get(ec)) continue;
        if (!e_client_util_desk_visible(ec, desk)) continue;
        if (!evas_object_visible_get(ec->frame)) continue;

        if ((launcher) && (launcher->ec == ec))
          return;

        if (e_policy_client_is_home_screen(ec))
          {
             evas_object_raise(ec->frame);
             return;
          }
        if (!all)
          {
             evas_object_lower(ec->frame);
             return;
          }
     }
}

static Evas_Object *
_e_policy_softkey_icon_add(E_Zone *zone, const char *name)
{
   Evas_Object *obj, *comp_obj;
   char path[PATH_MAX], group[PATH_MAX];

   obj = edje_object_add(e_comp->evas);

   snprintf(group, sizeof(group), "e/modules/policy-mobile/softkey/%s", name);
   e_prefix_data_snprintf(path, sizeof(path), "data/themes/%s", "e-policy.edj");

   if (!e_theme_edje_object_set(obj, NULL, group))
     edje_object_file_set(obj, path, group);

   edje_object_signal_callback_add(obj, "e,action,softkey,*", "e",
                                   _e_policy_cb_softkey, zone);

   /* use TYPE_NONE to disable shadow for softkey object */
   comp_obj = e_comp_object_util_add(obj, E_COMP_OBJECT_TYPE_NONE);
   evas_object_layer_set(comp_obj, E_LAYER_POPUP);

   evas_object_data_set(comp_obj, "policy_mobile_obj", obj);

   return comp_obj;
}

static void
_e_policy_softkey_icon_del(Evas_Object *comp_obj)
{
   Evas_Object *obj;

   obj = evas_object_data_get(comp_obj, "policy_mobile_obj");

   edje_object_signal_callback_del(obj, "e,action,softkey,*",
                                   "e", _e_policy_cb_softkey);
   evas_object_hide(comp_obj);
   evas_object_del(comp_obj);
}

E_Policy_Softkey *
e_policy_softkey_add(E_Zone *zone)
{
   E_Policy_Softkey *softkey;

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_create)
     {
        softkey = _e_softkey_funcs->softkey_create(zone);
        EINA_SAFETY_ON_NULL_RETURN_VAL(softkey, NULL);
     }
   else
     {
        softkey = E_NEW(E_Policy_Softkey, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(softkey, NULL);

        softkey->zone = zone;
        softkey->home = _e_policy_softkey_icon_add(zone, "home");
        softkey->back = _e_policy_softkey_icon_add(zone, "back");
     }

   e_policy->softkeys = eina_inlist_append(e_policy->softkeys, EINA_INLIST_GET(softkey));

   return softkey;
}

void
e_policy_softkey_del(E_Policy_Softkey *softkey)
{
   if (!softkey) return;

   e_policy->softkeys = eina_inlist_remove(e_policy->softkeys, EINA_INLIST_GET(softkey));

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_destroy)
     return _e_softkey_funcs->softkey_destroy(softkey);
   else
     {
        _e_policy_softkey_icon_del(softkey->home);
        _e_policy_softkey_icon_del(softkey->back);

        E_FREE(softkey);
     }
}

void
e_policy_softkey_show(E_Policy_Softkey *softkey)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_show)
     return _e_softkey_funcs->softkey_show(softkey);

   if (!softkey) return;

   e_policy_softkey_update(softkey);

   evas_object_show(softkey->home);
   evas_object_show(softkey->back);
}

void
e_policy_softkey_hide(E_Policy_Softkey *softkey)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_hide)
     return _e_softkey_funcs->softkey_hide(softkey);

   if (!softkey) return;

   evas_object_hide(softkey->home);
   evas_object_hide(softkey->back);
}

void
e_policy_softkey_update(E_Policy_Softkey *softkey)
{
   int x, y, w, h, ow, oh, space;

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_update)
     return _e_softkey_funcs->softkey_update(softkey);

   if (!softkey) return;

   e_zone_useful_geometry_get(softkey->zone, &x, &y, &w, &h);

   ow = oh = e_config->softkey_size;

   x = x + (w - ow) / 2;
   y = h - oh;
   space = ow * 4;

   evas_object_geometry_set(softkey->home, x - space, y, ow, oh);
   evas_object_geometry_set(softkey->back, x + space, y, ow, oh);
}

E_Policy_Softkey *
e_policy_softkey_get(E_Zone *zone)
{
   E_Policy_Softkey *softkey;

   EINA_INLIST_FOREACH(e_policy->softkeys, softkey)
     {
        if (softkey->zone == zone)
          return softkey;
     }

   return NULL;
}

E_API Eina_Bool
e_policy_softkey_module_func_set(E_Policy_Softkey_Funcs *fp)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL((_e_softkey_funcs == NULL), EINA_FALSE);

   if (!fp) return EINA_FALSE;

   _e_softkey_funcs = E_NEW(E_Policy_Softkey_Funcs, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_e_softkey_funcs, EINA_FALSE);

   _e_softkey_funcs->softkey_create = fp->softkey_create;
   _e_softkey_funcs->softkey_destroy = fp->softkey_destroy;
   _e_softkey_funcs->softkey_show = fp->softkey_show;
   _e_softkey_funcs->softkey_hide = fp->softkey_hide;
   _e_softkey_funcs->softkey_update = fp->softkey_update;

   return EINA_TRUE;
}

E_API void
e_policy_softkey_module_func_unset(void)
{
   if (!_e_softkey_funcs)
     return;

   E_FREE(_e_softkey_funcs);
}
