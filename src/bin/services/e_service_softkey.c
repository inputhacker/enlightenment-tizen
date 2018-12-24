#include "e.h"
#include <tzsh_server.h>
#include "services/e_service_softkey.h"
#include "services/e_service_gesture.h"
#include "services/e_service_region.h"
#include "e_policy_wl.h"

#define SOFTKEY_SHOW(softkey)                 \
do                                            \
{                                             \
   if (softkey->ec && !softkey->ec->visible)  \
     {                                        \
        softkey->show_block = EINA_FALSE;     \
        softkey->ec->visible = EINA_TRUE;     \
        evas_object_show(softkey->ec->frame); \
     }                                        \
} while (0)

#define SOFTKEY_HIDE(softkey)                 \
do                                            \
{                                             \
   if (softkey->ec && softkey->ec->visible)   \
     {                                        \
        softkey->show_block = EINA_TRUE;      \
        softkey->ec->visible = EINA_FALSE;    \
        evas_object_hide(softkey->ec->frame); \
     }                                        \
} while (0)

static Eina_List *_e_softkey_list;
static E_Service_Softkey_Funcs *_e_softkey_funcs = NULL;

E_API Eina_Bool
e_service_softkey_module_func_set(E_Service_Softkey_Funcs *fp)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(e_config->use_softkey_service, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL((_e_softkey_funcs == NULL), EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(fp, EINA_FALSE);

   _e_softkey_funcs = E_NEW(E_Service_Softkey_Funcs, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_e_softkey_funcs, EINA_FALSE);

   _e_softkey_funcs->softkey_service_add = fp->softkey_service_add;
   _e_softkey_funcs->softkey_service_del = fp->softkey_service_del;
   _e_softkey_funcs->softkey_service_wl_resource_set = fp->softkey_service_wl_resource_set;
   _e_softkey_funcs->softkey_service_wl_resource_get = fp->softkey_service_wl_resource_get;
   _e_softkey_funcs->softkey_service_client_set = fp->softkey_service_client_set;
   _e_softkey_funcs->softkey_service_client_unset = fp->softkey_service_client_unset;
   _e_softkey_funcs->softkey_service_show = fp->softkey_service_show;
   _e_softkey_funcs->softkey_service_hide = fp->softkey_service_hide;
   _e_softkey_funcs->softkey_service_visible_set = fp->softkey_service_visible_set;
   _e_softkey_funcs->softkey_service_visible_get = fp->softkey_service_visible_get;
   _e_softkey_funcs->softkey_service_expand_set = fp->softkey_service_expand_set;
   _e_softkey_funcs->softkey_service_expand_get = fp->softkey_service_expand_get;
   _e_softkey_funcs->softkey_service_opacity_set = fp->softkey_service_opacity_set;
   _e_softkey_funcs->softkey_service_opacity_get = fp->softkey_service_opacity_get;
   _e_softkey_funcs->softkey_service_get = fp->softkey_service_get;

   return EINA_TRUE;
}

E_API Eina_Bool
e_service_softkey_module_func_unset(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_e_softkey_funcs, EINA_FALSE);

   E_FREE(_e_softkey_funcs);

   return EINA_TRUE;
}

E_API E_Service_Softkey *
e_service_softkey_add(E_Zone *zone, E_Client *ec)
{
   ELOGF("SOFTKEY_SRV", "%s (zone:%p)", ec, __func__, zone);

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_add)
     {
        return _e_softkey_funcs->softkey_service_add(zone, ec);
     }

   if (!zone || !ec) return NULL;

   E_Service_Softkey *softkey;

   ELOGF("SOFTKEY_SRV", "Softkey service window add. zone:%p (id:%d)", ec, zone, zone->id);
   softkey = E_NEW(E_Service_Softkey, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(softkey, NULL);

   _e_softkey_list = eina_list_append(_e_softkey_list, softkey);

   softkey->zone = zone;
   softkey->ec = ec;

   return softkey;
}

E_API void
e_service_softkey_del(E_Service_Softkey *softkey)
{
   ELOGF("SOFTKEY_SRV", "%s (softkey:%p)", NULL, __func__, softkey);

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_del)
     {
        _e_softkey_funcs->softkey_service_del(softkey);
        return;
     }

   if (!softkey) return;

   _e_softkey_list = eina_list_remove(_e_softkey_list , softkey);

   E_FREE_LIST(softkey->intercept_hooks, e_comp_object_intercept_hook_del);

   E_FREE(softkey);

}

E_API Eina_Bool
e_service_softkey_wl_resource_set(E_Service_Softkey *softkey, struct wl_resource *wl_res)
{
   ELOGF("SOFTKEY_SRV", "%s (softkey:%p, res:%p)", NULL, __func__, softkey, wl_res);

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_wl_resource_set)
     {
        return _e_softkey_funcs->softkey_service_wl_resource_set(softkey, wl_res);
     }

   if (!softkey)
     return EINA_FALSE;

   ELOGF("SOFTKEY_SRV", "SET Softkey service (softkey:%p, res:%p)", softkey->ec, softkey, wl_res);
   softkey->wl_res = wl_res;

   return EINA_TRUE;
}

E_API struct wl_resource *
e_service_softkey_wl_resource_get(E_Service_Softkey *softkey)
{
   ELOGF("SOFTKEY_SRV", "%s (softkey:%p)", NULL, __func__, softkey);

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_wl_resource_get)
     {
        return _e_softkey_funcs->softkey_service_wl_resource_get(softkey);
     }

   if (!softkey)
     return NULL;

   return softkey->wl_res;
}

static Eina_Bool
_softkey_intercept_hook_show(void *data, E_Client *ec)
{
   E_Service_Softkey *softkey;

   softkey = data;
   if (EINA_UNLIKELY(!softkey))
     goto end;

   if (softkey->ec != ec)
     goto end;

   if (softkey->show_block)
     {
        ec->visible = EINA_FALSE;
        return EINA_FALSE;
     }

   ec->visible = EINA_TRUE;

end:
   return EINA_TRUE;
}

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

EINTERN void
e_service_softkey_client_set(E_Client *ec)
{
   ELOGF("SOFTKEY_SRV", "%s", ec, __func__);

   E_Service_Softkey *softkey;

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_client_set)
     {
        _e_softkey_funcs->softkey_service_client_set(ec);
        return;
     }

   if (!ec) return;

   /* check for client being deleted */
   if (e_object_is_del(E_OBJECT(ec))) return;

   /* check for wayland pixmap */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   ELOGF("SOFTKEY_SRV", "SET Softkey service", ec);

   softkey = e_service_softkey_get(ec->zone);
   if (!softkey)
     softkey = e_service_softkey_add(ec->zone, ec);
   else
     {
        if (softkey->ec != ec)
          {
             e_service_softkey_del(softkey);
             softkey = e_service_softkey_add(ec->zone, ec);
          }
     }

   if (!softkey)
     return;

   ELOGF("SOFTKEY", "Set Client | softkey %p", ec, softkey);

   softkey->ec = ec;
   e_client_window_role_set(ec, "softkey");

   // set softkey layer
   if (E_POLICY_SOFTKEY_LAYER != evas_object_layer_get(ec->frame))
     {
        evas_object_layer_set(ec->frame, E_POLICY_SOFTKEY_LAYER);
     }
   ec->layer = E_POLICY_SOFTKEY_LAYER;

   // set skip iconify
   ec->exp_iconify.skip_iconify = 1;

   // set skip focus
   ec->icccm.accepts_focus = ec->icccm.take_focus = 0;

   // disable effect
   e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_NEVER, EINA_TRUE);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(softkey->intercept_hooks, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _softkey_intercept_hook_show, softkey);
}

EINTERN void
e_service_softkey_client_unset(E_Client *ec)
{
   ELOGF("SOFTKEY_SRV", "%s", ec, __func__);

   E_Service_Softkey *softkey;

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_client_unset)
     {
        _e_softkey_funcs->softkey_service_client_unset(ec);
        return;
     }

   softkey = e_service_softkey_get(ec->zone);
   if (!softkey) return;

   e_service_softkey_del(softkey);
}

EINTERN void
e_service_softkey_show(E_Service_Softkey *softkey)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_show)
     {
        _e_softkey_funcs->softkey_service_show(softkey);
        return;
     }

   if (!softkey) return;
   if (!softkey->ec) return;

   SOFTKEY_SHOW(softkey);
}

EINTERN void
e_service_softkey_hide(E_Service_Softkey *softkey)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_hide)
     {
        _e_softkey_funcs->softkey_service_hide(softkey);
        return;
     }

   if (!softkey) return;
   if (!softkey->ec) return;

   SOFTKEY_HIDE(softkey);
}


EINTERN void
e_service_softkey_visible_set(E_Service_Softkey *softkey, int visible)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_visible_set)
     {
        _e_softkey_funcs->softkey_service_visible_set(softkey, visible);
        return;
     }

   if (!softkey) return;
   if (!softkey->wl_res)
     {
        ELOGF("SOFTKEY_SRV", "Error. No wl_resource of Softkey Service", NULL);
        return;
     }

   tws_service_softkey_send_visible_change_request(softkey->wl_res, visible);
}


EINTERN int
e_service_softkey_visible_get(E_Service_Softkey *softkey)
{
   int visible;

   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_visible_get)
     {
        return _e_softkey_funcs->softkey_service_visible_get(softkey);
     }

   if (!softkey) return 0;
   if (!softkey->ec) return 0;

   visible = evas_object_visible_get(softkey->ec->frame);
   return visible;
}

EINTERN void
e_service_softkey_expand_set(E_Service_Softkey *softkey, E_Policy_Softkey_Expand expand)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_expand_set)
     {
        _e_softkey_funcs->softkey_service_expand_set(softkey, expand);
        return;
     }

   if (!softkey) return;
   if (!softkey->wl_res)
     {
        ELOGF("SOFTKEY_SRV", "Error. No wl_resource of Softkey Service", NULL);
        return;
     }

   tws_service_softkey_send_expand_change_request(softkey->wl_res, expand);
   softkey->expand = expand;
}

EINTERN Eina_Bool
e_service_softkey_expand_get(E_Service_Softkey *softkey, E_Policy_Softkey_Expand *expand)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_expand_get)
     {
        return _e_softkey_funcs->softkey_service_expand_get(softkey, expand);
     }

   if (!softkey)
     {
        if (expand)
          *expand = E_POLICY_SOFTKEY_EXPAND_OFF;

        return EINA_FALSE;
     }
   else
     {
        if (expand)
          *expand = softkey->expand;

        return EINA_TRUE;
     }
}

EINTERN void
e_service_softkey_opacity_set(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity opacity)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_opacity_set)
     {
        _e_softkey_funcs->softkey_service_opacity_set(softkey, opacity);
        return;
     }

   if (!softkey) return;
   if (!softkey->wl_res)
     {
        ELOGF("SOFTKEY_SRV", "Error. No wl_resource of Softkey Service", NULL);
        return;
     }

   tws_service_softkey_send_opacity_change_request(softkey->wl_res, opacity);
   softkey->opacity = opacity;
}

EINTERN Eina_Bool
e_service_softkey_opacity_get(E_Service_Softkey *softkey, E_Policy_Softkey_Opacity *opacity)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_opacity_get)
     {
        return _e_softkey_funcs->softkey_service_opacity_get(softkey, opacity);
     }

   if (!softkey)
     {
        if (opacity)
          *opacity = E_POLICY_SOFTKEY_OPACITY_OPAQUE;

        return EINA_FALSE;
     }
   else
     {
        if (opacity)
          *opacity = softkey->opacity;

        return EINA_TRUE;
     }
}

E_API E_Service_Softkey *
e_service_softkey_get(E_Zone *zone)
{
   if (_e_softkey_funcs && _e_softkey_funcs->softkey_service_get)
     {
        return _e_softkey_funcs->softkey_service_get(zone);
     }

   Eina_List *l;
   E_Service_Softkey *softkey = NULL;

   EINA_LIST_FOREACH(_e_softkey_list, l, softkey)
     {
        if (softkey->zone == zone)
          return softkey;
     }

   return softkey;
}
