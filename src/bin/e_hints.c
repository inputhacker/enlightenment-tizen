#include "e.h"

static Eina_List *aux_hints_supported = NULL;

E_API void
e_hints_active_window_set(E_Client *ec)
{
   (void)ec;
}

EINTERN void
e_hints_window_init(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_state_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_allowed_action_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_type_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_type_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_state_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_allowed_action_update(E_Client *ec, int action)
{
   (void)ec;
   (void)action;
}

E_API void
e_hints_allowed_action_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_visible_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_iconic_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_hidden_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_shaded_set(E_Client *ec, int on)
{
   (void)ec;
   (void)on;
}

E_API void
e_hints_window_shade_direction_set(E_Client *ec, E_Direction dir)
{
   (void)ec;
   (void)dir;
}

E_API E_Direction
e_hints_window_shade_direction_get(E_Client *ec)
{
   (void)ec;
   return E_DIRECTION_UP;
}

E_API void
e_hints_window_size_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_size_unset(E_Client *ec)
{
   (void)ec;
}

E_API int
e_hints_window_size_get(E_Client *ec)
{
   (void)ec;
   return 1;
}

E_API void
e_hints_window_maximized_set(E_Client *ec, int horizontal, int vertical)
{
   (void)ec;
   (void)horizontal;
   (void)vertical;
}

E_API void
e_hints_window_fullscreen_set(E_Client *ec, int on)
{
   (void)ec;
   (void)on;
}

E_API void
e_hints_window_sticky_set(E_Client *ec, int on)
{
   (void)ec;
   (void)on;
}

E_API void
e_hints_window_stacking_set(E_Client *ec, E_Stacking stacking)
{
   (void)ec;
   (void)stacking;
}

E_API void
e_hints_window_desktop_set(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_e_state_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_e_state_set(E_Client *ec EINA_UNUSED)
{
   /* TODO */
}

E_API void
e_hints_window_e_opaque_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_virtual_keyboard_state_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_window_virtual_keyboard_get(E_Client *ec)
{
   (void)ec;
}

E_API void
e_hints_scale_update(void)
{
   Eina_List *l;
   E_Comp_Wl_Output *output;

   EINA_LIST_FOREACH(e_comp_wl->outputs, l, output)
     output->scale = e_scale;
}

E_API const Eina_List *
e_hints_aux_hint_supported_add(const char *hint)
{
   Eina_List *l;
   const char *supported;

   EINA_LIST_FOREACH(aux_hints_supported, l, supported)
     {
        if (!strcmp(supported, hint))
          return aux_hints_supported;
     }

   aux_hints_supported = eina_list_append(aux_hints_supported, hint);

   return aux_hints_supported;
}

E_API const Eina_List *
e_hints_aux_hint_supported_del(const char *hint)
{
   Eina_List *l;
   const char *supported;

   EINA_LIST_FOREACH(aux_hints_supported, l, supported)
     {
        if (!strcmp(supported, hint))
          {
             aux_hints_supported = eina_list_remove(aux_hints_supported, hint);
             break;
          }
     }

   return aux_hints_supported;
}

E_API const Eina_List *
e_hints_aux_hint_supported_get(void)
{
   return aux_hints_supported;
}

E_API Eina_Bool
e_hints_aux_hint_add(E_Client *ec, int32_t id, const char *name, const char *val)
{
   if (!ec) return EINA_FALSE;
   return e_hints_aux_hint_add_with_pixmap(ec->pixmap, id, name, val);
}

E_API Eina_Bool
e_hints_aux_hint_change(E_Client *ec, int32_t id, const char *val)
{
   if (!ec) return EINA_FALSE;
   return e_hints_aux_hint_change_with_pixmap(ec->pixmap, id, val);
}

E_API Eina_Bool
e_hints_aux_hint_del(E_Client *ec, int32_t id)
{
   if (!ec) return EINA_FALSE;
   return e_hints_aux_hint_del_with_pixmap(ec->pixmap, id);
}

E_API const char *
e_hints_aux_hint_value_get(E_Client *ec, const char *name)
{
   if (!ec) return NULL;
   return e_hints_aux_hint_value_get_with_pixmap(ec->pixmap, name);
}

E_API Eina_Bool
e_hints_aux_hint_add_with_pixmap(E_Pixmap *cp, int32_t id, const char *name, const char *val)
{
   E_Comp_Wl_Client_Data *cdata;
   Eina_Bool found = EINA_FALSE;
   E_Comp_Wl_Aux_Hint *hint;
   Eina_List *l;

   if (!cp) return EINA_FALSE;
   cdata = (E_Comp_Wl_Client_Data*)e_pixmap_cdata_get(cp);
   if (!cdata) return EINA_FALSE;

   EINA_LIST_FOREACH(cdata->aux_hint.hints, l, hint)
     {
        if (hint->id == id)
          {
             if (strcmp(hint->val, val) != 0)
               {
                  ELOGF("COMP", "AUX_HINT |Change [pixmap] [%d:%s:%s -> %s]",
                        e_pixmap_client_get(cp),
                        id, hint->hint, hint->val, val);
                  eina_stringshare_del(hint->val);

                  hint->val = eina_stringshare_add(val);
                  hint->changed = EINA_TRUE;
                  hint->deleted = EINA_FALSE;
                  cdata->aux_hint.changed = 1;
               }
             found = EINA_TRUE;
             break;
          }
     }

   if (!found)
     {
        hint = E_NEW(E_Comp_Wl_Aux_Hint, 1);
        EINA_SAFETY_ON_NULL_RETURN_VAL(hint, EINA_FALSE);

        memset(hint, 0, sizeof(E_Comp_Wl_Aux_Hint));

        hint->id = id;
        hint->hint = eina_stringshare_add(name);
        hint->val = eina_stringshare_add(val);
        hint->changed = EINA_TRUE;
        hint->deleted = EINA_FALSE;
        cdata->aux_hint.hints = eina_list_append(cdata->aux_hint.hints, hint);
        cdata->aux_hint.changed = 1;
        ELOGF("COMP", "AUX_HINT |Add [%d:%s:%s]", e_pixmap_client_get(cp),
              id, hint->hint, hint->val);
     }

   return EINA_TRUE;
}

E_API Eina_Bool
e_hints_aux_hint_change_with_pixmap(E_Pixmap *cp, int32_t id, const char *val)
{
   E_Comp_Wl_Client_Data *cdata;
   Eina_List *l;
   E_Comp_Wl_Aux_Hint *hint;
   Eina_Bool found = EINA_FALSE;

   if (!cp) return EINA_FALSE;
   cdata = (E_Comp_Wl_Client_Data*)e_pixmap_cdata_get(cp);
   if (!cdata) return EINA_FALSE;

   EINA_LIST_FOREACH(cdata->aux_hint.hints, l, hint)
     {
        if (hint->id == id)
          {
             if ((hint->val) && (strcmp(hint->val, val) != 0))
               {
                  ELOGF("COMP", "AUX_HINT |Change [%d:%s:%s -> %s]", e_pixmap_client_get(cp),
                        id, hint->hint, hint->val, val);
                  eina_stringshare_del(hint->val);
                  hint->val = eina_stringshare_add(val);
                  hint->changed = EINA_TRUE;
                  cdata->aux_hint.changed = 1;
               }

             if (hint->deleted)
               hint->deleted = EINA_FALSE;

             found = EINA_TRUE;
             break;
          }
     }

   if (found)
     return EINA_TRUE;

   return EINA_FALSE;
}

E_API Eina_Bool
e_hints_aux_hint_del_with_pixmap(E_Pixmap *cp, int32_t id)
{
   E_Comp_Wl_Client_Data *cdata;
   Eina_List *l, *ll;
   E_Comp_Wl_Aux_Hint *hint;
   int res = -1;

   if (!cp) return EINA_FALSE;
   cdata = (E_Comp_Wl_Client_Data*)e_pixmap_cdata_get(cp);
   if (!cdata) return EINA_FALSE;

   EINA_LIST_FOREACH_SAFE(cdata->aux_hint.hints, l, ll, hint)
     {
        if (hint->id == id)
          {
             ELOGF("COMP", "AUX_HINT |Del (pending) [%d:%s:%s]", e_pixmap_client_get(cp), id, hint->hint, hint->val);
             hint->changed = EINA_TRUE;
             hint->deleted = EINA_TRUE;
             cdata->aux_hint.changed = 1;
             res = hint->id;
             break;
          }
     }

   if (res == -1)
     return EINA_FALSE;

   return EINA_TRUE;
}

E_API const char *
e_hints_aux_hint_value_get_with_pixmap(E_Pixmap *cp, const char *name)
{
   E_Comp_Wl_Client_Data *cdata;
   Eina_List *l;
   E_Comp_Wl_Aux_Hint *hint;
   const char *res = NULL;

   if (!cp) return NULL;
   cdata = (E_Comp_Wl_Client_Data*)e_pixmap_cdata_get(cp);
   if (!cdata) return NULL;

   EINA_LIST_REVERSE_FOREACH(cdata->aux_hint.hints, l, hint)
     {
        if ((!hint->deleted) &&
            (!strcmp(hint->hint, name)))
          {
             res =  hint->val;
             break;
          }
     }

   return res;
}
