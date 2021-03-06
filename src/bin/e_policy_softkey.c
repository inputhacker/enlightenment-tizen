#include "e.h"

static void         _e_policy_cb_softkey(void *data, Evas_Object *obj EINA_UNUSED, const char *emission, const char *source EINA_UNUSED);
static void         _e_policy_softkey_iconify(E_Zone *zone, Eina_Bool all);
static Evas_Object *_e_policy_softkey_icon_add(E_Zone *zone, const char *name);
static void         _e_policy_softkey_icon_del(Evas_Object *comp_obj);

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

   softkey = E_NEW(E_Policy_Softkey, 1);

   softkey->zone = zone;
   softkey->home = _e_policy_softkey_icon_add(zone, "home");
   softkey->back = _e_policy_softkey_icon_add(zone, "back");

   e_policy->softkeys = eina_inlist_append(e_policy->softkeys, EINA_INLIST_GET(softkey));

   return softkey;
}

void
e_policy_softkey_del(E_Policy_Softkey *softkey)
{
   if (!softkey) return;

   _e_policy_softkey_icon_del(softkey->home);
   _e_policy_softkey_icon_del(softkey->back);

   e_policy->softkeys = eina_inlist_remove(e_policy->softkeys, EINA_INLIST_GET(softkey));

   free(softkey);
}

void
e_policy_softkey_show(E_Policy_Softkey *softkey)
{
   if (!softkey) return;

   e_policy_softkey_update(softkey);

   evas_object_show(softkey->home);
   evas_object_show(softkey->back);
}

void
e_policy_softkey_hide(E_Policy_Softkey *softkey)
{
   if (!softkey) return;

   evas_object_hide(softkey->home);
   evas_object_hide(softkey->back);
}

void
e_policy_softkey_update(E_Policy_Softkey *softkey)
{
   int x, y, w, h, ow, oh, space;

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
