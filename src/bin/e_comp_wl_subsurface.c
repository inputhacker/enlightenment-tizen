#include "e.h"

#include <tizen-extension-server-protocol.h>

static struct wl_global *global = NULL;
static Eina_List *hooks = NULL;

static void
_e_comp_wl_subsurface_restack_bg_rectangle(E_Client *ec)
{
   E_Client *bottom = ec;

   if (!ec->comp_data->sub.below_obj)
     return;

   while (bottom)
     {
        short layer = evas_object_layer_get(bottom->frame);

        if (evas_object_layer_get(ec->comp_data->sub.below_obj) != layer)
          evas_object_layer_set(ec->comp_data->sub.below_obj, layer);

        evas_object_stack_below(ec->comp_data->sub.below_obj, bottom->frame);
        bottom = eina_list_nth(bottom->comp_data->sub.below_list, 0);
     }
}

static void
_e_comp_wl_subsurface_restack(E_Client *ec)
{
   E_Client *subc, *temp;
   Eina_List *l;

   if (!ec || !ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   temp = ec;
   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc || !subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        subc->comp_data->sub.restacking = EINA_TRUE;
        evas_object_stack_above(subc->frame, temp->frame);
        subc->comp_data->sub.restacking = EINA_FALSE;
        temp = subc;
     }

   temp = ec;
   EINA_LIST_REVERSE_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc || !subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;
        subc->comp_data->sub.restacking = EINA_TRUE;
        evas_object_stack_below(subc->frame, temp->frame);
        subc->comp_data->sub.restacking = EINA_FALSE;
        temp = subc;
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     _e_comp_wl_subsurface_restack(subc);

   EINA_LIST_REVERSE_FOREACH(ec->comp_data->sub.below_list, l, subc)
     _e_comp_wl_subsurface_restack(subc);
}

static void
_e_comp_wl_subsurface_bg_evas_cb_resize(void *data, Evas *evas EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event EINA_UNUSED)
{
   E_Client *ec;

   if (!(ec = data)) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data->sub.below_obj)
      evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
}

static Eina_Bool
_e_comp_wl_subsurface_video_has(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (!ec->comp_data) return EINA_FALSE;

   if ((ec->comp_data->video_client) ||
       (e_client_video_hw_composition_check(ec)))
     return EINA_TRUE;

   if (ec->comp_data->has_video_client)
     return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
      if (_e_comp_wl_subsurface_video_has(subc))
        return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
      if (_e_comp_wl_subsurface_video_has(subc))
        return EINA_TRUE;

   return EINA_FALSE;
}

static void
_e_comp_wl_subsurface_check_below_bg_rectangle(E_Client *ec)
{
   short layer;

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (ec->comp_data->sub.data)
     {
         E_Client *topmost = e_comp_wl_topmost_parent_get(ec);
         if (!topmost || e_object_is_del(E_OBJECT(topmost)) || !topmost->comp_data) return;
         if (topmost->comp_data->sub.data) return;
         _e_comp_wl_subsurface_check_below_bg_rectangle(topmost);
         return;
     }

   if (ec->argb)
     {
         if (ec->comp_data->sub.below_obj)
           {
               ELOGF("COMP", "         |bg_rectangle(%p) delete", ec, ec->comp_data->sub.below_obj);
               evas_object_del(ec->comp_data->sub.below_obj);
               ec->comp_data->sub.below_obj = NULL;
           }
         return;
     }

   if ((!ec->comp_data->sub.below_obj) &&
       (ec->comp_data->sub.below_list ||
        ec->comp_data->sub.below_list_pending ||
        _e_comp_wl_subsurface_video_has(ec)))
     {
        /* create a bg rectangle if topmost window is 24 depth window */
        ec->comp_data->sub.below_obj = evas_object_rectangle_add(e_comp->evas);
        EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->sub.below_obj);

        ELOGF("COMP", "         |bg_rectangle(%p) created", ec, ec->comp_data->sub.below_obj);

        layer = evas_object_layer_get(ec->frame);
        evas_object_layer_set(ec->comp_data->sub.below_obj, layer);
        evas_object_render_op_set(ec->comp_data->sub.below_obj, EVAS_RENDER_COPY);

        /* It's more reasonable to use the transparent color instead of black because
         * we can show the alpha value of the 24 depth topmost window.
         */
        evas_object_color_set(ec->comp_data->sub.below_obj, 0x00, 0x00, 0x00, 0x00);
        evas_object_move(ec->comp_data->sub.below_obj, ec->x, ec->y);
        evas_object_resize(ec->comp_data->sub.below_obj, ec->w, ec->h);
        evas_object_name_set(ec->comp_data->sub.below_obj, "below_bg_rectangle");

        evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,
                                       _e_comp_wl_subsurface_bg_evas_cb_resize, ec);

        /* set alpha only if SW path */
        e_comp_object_alpha_set(ec->frame, EINA_TRUE);

        /* force update for changing alpha value. if the native surface has been already
         * set before, changing alpha value can't be applied to egl image.
         */
        e_comp_object_native_surface_set(ec->frame, EINA_FALSE);
        e_pixmap_image_refresh(ec->pixmap);
        e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
        e_comp_object_dirty(ec->frame);
        e_comp_object_render(ec->frame);

        _e_comp_wl_subsurface_restack(ec);
        _e_comp_wl_subsurface_restack_bg_rectangle(ec);

        if (evas_object_visible_get(ec->frame))
          evas_object_show(ec->comp_data->sub.below_obj);

        e_client_transform_core_update(ec);
     }
}

static void
_e_comp_wl_subsurface_show(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec))) return;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;

        if (e_pixmap_resource_get(subc->pixmap) && !subc->comp_data->mapped)
          {
             subc->visible = EINA_TRUE;
             evas_object_show(subc->frame);
             subc->comp_data->mapped = 1;
          }
        _e_comp_wl_subsurface_show(subc);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc->comp_data || e_object_is_del(E_OBJECT(subc))) continue;

        if (e_pixmap_resource_get(subc->pixmap) && !subc->comp_data->mapped)
          {
             subc->visible = EINA_TRUE;
             evas_object_show(subc->frame);
             subc->comp_data->mapped = 1;
          }
        _e_comp_wl_subsurface_show(subc);
     }
}

static void
_e_comp_wl_subsurface_hide(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        if (!subc->comp_data->sub.data->stand_alone)
          {
             if (subc->comp_data->mapped)
               {
                  subc->visible = EINA_FALSE;
                  evas_object_hide(subc->frame);
                  subc->comp_data->mapped = 0;
               }
             _e_comp_wl_subsurface_hide(subc);
          }
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        if (!subc->comp_data->sub.data->stand_alone)
          {
             if (subc->comp_data->mapped)
               {
                  subc->visible = EINA_FALSE;
                  evas_object_hide(subc->frame);
                  subc->comp_data->mapped = 0;
               }
             _e_comp_wl_subsurface_hide(subc);
          }
     }
}

static E_Client*
_e_comp_wl_subsurface_invisible_parent_get(E_Client *ec)
{
   E_Client *parent = NULL;

   if (!ec->comp_data || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data->sub.data)
      return NULL;

   parent = ec->comp_data->sub.data->parent;
   while (parent)
     {
        /* in case of topmost */
        if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return NULL;

        if (!parent->comp_data->sub.data)
          return (!evas_object_visible_get(parent->frame)) ? parent : NULL;

        if (!evas_object_visible_get(parent->frame)){
          if (e_pixmap_resource_get(parent->pixmap))
            return parent;

          if (!parent->comp_data->sub.data->parent)
            return parent;
        }

        parent = parent->comp_data->sub.data->parent;
     }

   return NULL;
}

static Eina_Bool
_e_comp_wl_subsurface_order_commit(E_Client *ec)
{
   E_Client *subc, *epc;
   Eina_List *l;
   Eina_Bool need_restack = EINA_FALSE;

   if (!ec->comp_data) return EINA_FALSE;

   if (ec->comp_data->sub.data && (epc = ec->comp_data->sub.data->parent))
     if (epc->comp_data->sub.list_changed)
       need_restack = _e_comp_wl_subsurface_order_commit(epc);

   if (!ec->comp_data->sub.list_changed) return (EINA_FALSE | need_restack);
   ec->comp_data->sub.list_changed = EINA_FALSE;

   /* TODO: need to check more complicated subsurface tree */
   EINA_LIST_FOREACH(ec->comp_data->sub.list_pending, l, subc)
     {
        ec->comp_data->sub.list = eina_list_remove(ec->comp_data->sub.list, subc);
        ec->comp_data->sub.list = eina_list_append(ec->comp_data->sub.list, subc);

        _e_comp_wl_subsurface_order_commit(subc);
     }

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     {
        ec->comp_data->sub.below_list = eina_list_remove(ec->comp_data->sub.below_list, subc);
        ec->comp_data->sub.below_list = eina_list_append(ec->comp_data->sub.below_list, subc);

        _e_comp_wl_subsurface_order_commit(subc);
     }

   return EINA_TRUE;
}

static void
_e_comp_wl_subsurface_commit_to_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;
   struct wl_resource *cb;
   Eina_List *l, *ll;
   Eina_Iterator *itr;
   Eina_Rectangle *rect;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit to Cache");

   /* move pending damage to cached */
   EINA_LIST_FOREACH_SAFE(cdata->pending.damages, l, ll, rect)
     eina_list_move_list(&sdata->cached.damages, &cdata->pending.damages, l);

   EINA_LIST_FOREACH_SAFE(cdata->pending.buffer_damages, l, ll, rect)
     eina_list_move_list(&sdata->cached.buffer_damages, &cdata->pending.buffer_damages, l);

   if (cdata->pending.new_attach)
     {
        sdata->cached.new_attach = EINA_TRUE;
        e_comp_wl_surface_state_buffer_set(&sdata->cached,
                                            cdata->pending.buffer);
        e_comp_wl_buffer_reference(&sdata->cached_buffer_ref,
                                   cdata->pending.buffer);
     }

   sdata->cached.sx = cdata->pending.sx;
   sdata->cached.sy = cdata->pending.sy;
   /* sdata->cached.buffer = cdata->pending.buffer; */

   /* When subsurface is sync mode, the commit of subsurface can happen before
    * a parent surface is committed. In this case, we can't show a attached
    * buffer on screen.
    */
   //sdata->cached.new_attach = cdata->pending.new_attach;

   sdata->cached.buffer_viewport.changed |= cdata->pending.buffer_viewport.changed;
   sdata->cached.buffer_viewport.buffer =cdata->pending.buffer_viewport.buffer;
   sdata->cached.buffer_viewport.surface = cdata->pending.buffer_viewport.surface;

   e_comp_wl_surface_state_buffer_set(&cdata->pending, NULL);
   cdata->pending.sx = 0;
   cdata->pending.sy = 0;
   cdata->pending.new_attach = EINA_FALSE;
   cdata->pending.buffer_viewport.changed = 0;

   /* copy cdata->pending.opaque into sdata->cached.opaque */
   itr = eina_tiler_iterator_new(cdata->pending.opaque);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.opaque, rect);
   eina_iterator_free(itr);

   /* repeat for input */
   itr = eina_tiler_iterator_new(cdata->pending.input);
   EINA_ITERATOR_FOREACH(itr, rect)
     eina_tiler_rect_add(sdata->cached.input, rect);
   eina_iterator_free(itr);

   EINA_LIST_FOREACH_SAFE(cdata->pending.frames, l, ll, cb)
     {
        if (cb)
          eina_list_move_list(&sdata->cached.frames,
                              &cdata->pending.frames,
                              l);
     }

   sdata->cached.has_data = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_commit_from_cache(E_Client *ec)
{
   E_Comp_Client_Data *cdata;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!(cdata = ec->comp_data)) return;
   if (!(sdata = cdata->sub.data)) return;

   DBG("Subsurface Commit from Cache");

   e_comp_wl_surface_state_commit(ec, &sdata->cached);

   if (!e_comp_object_damage_exists(ec->frame))
     {
        if ((ec->comp_data->video_client) ||
            (!e_client_video_hw_composition_check(ec)))
          e_pixmap_image_clear(ec->pixmap, 1);
     }

   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   if (_e_comp_wl_subsurface_order_commit(ec))
     {
        E_Client *topmost = e_comp_wl_topmost_parent_get(ec);
        _e_comp_wl_subsurface_restack(topmost);
        _e_comp_wl_subsurface_restack_bg_rectangle(topmost);
     }
}

static void
_e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized)
{
   E_Client *parent;
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;
   if (!(parent = sdata->parent)) return;

   if (sdata->position.set)
     {
        evas_object_move(ec->frame, parent->x + sdata->position.x,
                         parent->y + sdata->position.y);
        sdata->position.set = EINA_FALSE;
     }

   if ((parent_synchronized) || (sdata->synchronized))
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->cached.has_data)
          _e_comp_wl_subsurface_commit_from_cache(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_TRUE);
          }
     }
}

static void
_e_comp_wl_subsurface_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subsurface_cb_position_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, int32_t x, int32_t y)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Position Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->position.x = x;
   sdata->position.y = y;
   sdata->position.set = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_above(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Above: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;
   if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_append_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_place_below(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, struct wl_resource *sibling_resource)
{
   E_Client *ec, *ecs;
   E_Client *parent;

   DBG("Subsurface Cb Place Below: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!ec->comp_data->sub.data) return;

   /* try to get the client from the sibling resource */
   if (!(ecs = wl_resource_get_user_data(sibling_resource))) return;

   if (!ecs->comp_data->sub.data) return;

   if (!(parent = ec->comp_data->sub.data->parent)) return;
   if (e_object_is_del(E_OBJECT(parent)) || !parent->comp_data) return;

   parent->comp_data->sub.list_pending =
     eina_list_remove(parent->comp_data->sub.list_pending, ec);

   parent->comp_data->sub.list_pending =
     eina_list_prepend_relative(parent->comp_data->sub.list_pending, ec, ecs);

   parent->comp_data->sub.list_changed = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_sync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Sync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_TRUE;
}

static void
_e_comp_wl_subsurface_cb_desync_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;
   E_Comp_Wl_Subsurf_Data *sdata;

   DBG("Subsurface Cb Desync Set: %d", wl_resource_get_id(resource));

   /* try to get the client from resource data */
   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return;
   if (!(sdata = ec->comp_data->sub.data)) return;

   sdata->synchronized = EINA_FALSE;
}

static const struct wl_subsurface_interface _e_subsurface_interface =
{
   _e_comp_wl_subsurface_cb_destroy,
   _e_comp_wl_subsurface_cb_position_set,
   _e_comp_wl_subsurface_cb_place_above,
   _e_comp_wl_subsurface_cb_place_below,
   _e_comp_wl_subsurface_cb_sync_set,
   _e_comp_wl_subsurface_cb_desync_set
};

static void
_e_comp_wl_subcompositor_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_comp_wl_subcompositor_cb_subsurface_get(struct wl_client *client EINA_UNUSED, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *parent_resource)
{
   E_Client *ec, *epc = NULL;
   static const char where[] = "get_subsurface: wl_subsurface@";

   if (!(ec = wl_resource_get_user_data(surface_resource))) return;
   if (!(epc = wl_resource_get_user_data(parent_resource))) return;

   if (ec == epc)
     {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d cannot be its own parent",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_object_is_del(E_OBJECT(epc))) return;

   /* check if this surface is already a sub-surface */
   if ((ec->comp_data) && (ec->comp_data->sub.data))
     {
        wl_resource_post_error(resource,
                               WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "%s%d: wl_surface@%d is already a sub-surface",
                               where, id, wl_resource_get_id(surface_resource));
        return;
     }

   /* try to create a new subsurface */
   if (!e_comp_wl_subsurface_create(ec, epc, id, surface_resource))
     ERR("Failed to create subsurface for surface %d",
         wl_resource_get_id(surface_resource));
}

static const struct wl_subcompositor_interface _e_subcomp_interface =
{
   _e_comp_wl_subcompositor_cb_destroy,
   _e_comp_wl_subcompositor_cb_subsurface_get
};

static void
_e_comp_wl_subcompositor_cb_bind(struct wl_client *client, void *data EINA_UNUSED, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   res = wl_resource_create(client, &wl_subcompositor_interface, version, id);
   if (!res)
     {
        ERR("Could not create subcompositor resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_subcomp_interface, NULL, NULL);

   /* TODO: add handlers for client iconify/uniconify */
}

static void
_e_comp_wl_subsurface_cb_ec_iconify(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   /* DON'T iconify subsurface. When iconfied, buffer will be released. */
   _e_comp_wl_subsurface_hide(ec);
}

static void
_e_comp_wl_subsurface_cb_ec_uniconify(void *data EINA_UNUSED, E_Client *ec)
{
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   _e_comp_wl_subsurface_show(ec);
}

static void
_e_comp_wl_subsurface_destroy_sdata(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   if (!ec || !ec->comp_data || !ec->comp_data->sub.data) return;

   sdata = ec->comp_data->sub.data;
   if (sdata->parent)
     {
        /* remove this client from parents sub list */
        sdata->parent->comp_data->sub.list =
          eina_list_remove(sdata->parent->comp_data->sub.list, ec);
        sdata->parent->comp_data->sub.list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.list_pending, ec);
        sdata->parent->comp_data->sub.below_list =
          eina_list_remove(sdata->parent->comp_data->sub.below_list, ec);
        sdata->parent->comp_data->sub.below_list_pending =
          eina_list_remove(sdata->parent->comp_data->sub.below_list_pending, ec);
     }

   e_comp_wl_surface_state_finish(&sdata->cached);
   e_comp_wl_buffer_reference(&sdata->cached_buffer_ref, NULL);

   /* the client is getting deleted, which means the pixmap will be getting
    * freed. We need to unset the surface user data */
   /* wl_resource_set_user_data(ec->comp_data->surface, NULL); */

   E_FREE(sdata);

   ec->comp_data->sub.data = NULL;
}

static void
_e_comp_wl_surface_sub_list_free(Eina_List *sub_list)
{
   E_Client *subc;

   EINA_LIST_FREE(sub_list, subc)
     {
        if (!subc->comp_data || !subc->comp_data->sub.data) continue;

        subc->comp_data->sub.data->parent = NULL;

        if (subc->comp_data->sub.watcher)
          tizen_subsurface_watcher_send_message(subc->comp_data->sub.watcher, TIZEN_SUBSURFACE_WATCHER_MSG_PARENT_ID_DESTROYED);
     }
}

static void
_e_comp_wl_subsurface_cb_ec_del(void *data EINA_UNUSED, E_Client *ec)
{
   /* make sure this is a wayland client */
   if (e_pixmap_type_get(ec->pixmap) != E_PIXMAP_TYPE_WL) return;

   if (ec->comp_data->sub.data)
     _e_comp_wl_subsurface_destroy_sdata(ec);

   if (ec->comp_data->sub.below_obj)
     evas_object_del(ec->comp_data->sub.below_obj);

   /* remove sub list */
   /* TODO: if parent is set by onscreen_parent of remote surface? */
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.list);
   ec->comp_data->sub.list = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.list_pending);
   ec->comp_data->sub.list_pending = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.below_list);
   ec->comp_data->sub.below_list = NULL;
   _e_comp_wl_surface_sub_list_free(ec->comp_data->sub.below_list_pending);
   ec->comp_data->sub.below_list_pending = NULL;
}

static void
_e_comp_wl_subsurface_destroy(struct wl_resource *resource)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (!e_object_unref(E_OBJECT(ec))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;

   _e_comp_wl_subsurface_destroy_sdata(ec);
}

static Eina_Bool
_e_comp_wl_subsurface_synchronized_get(E_Comp_Wl_Subsurf_Data *sdata)
{
   while (sdata)
     {
        if (sdata->synchronized) return EINA_TRUE;
        if (!sdata->parent) return EINA_FALSE;
        sdata = sdata->parent->comp_data->sub.data;
     }

   return EINA_FALSE;
}

EINTERN Eina_Bool
e_comp_wl_subsurfaces_init(E_Comp_Wl_Data *wl_comp)
{
   global = wl_global_create(wl_comp->wl.disp,
                             &wl_subcompositor_interface,
                             1, NULL,
                             _e_comp_wl_subcompositor_cb_bind);
   if (!global)
     {
        ERR("Could not add subcompositor to wayland globals: %m");
        return EINA_FALSE;
     }

   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_DEL,       _e_comp_wl_subsurface_cb_ec_del,         NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_ICONIFY,   _e_comp_wl_subsurface_cb_ec_iconify,     NULL);
   E_LIST_HOOK_APPEND(hooks, E_CLIENT_HOOK_UNICONIFY, _e_comp_wl_subsurface_cb_ec_uniconify,   NULL);

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_subsurfaces_shutdown(void)
{
   E_FREE_FUNC(global, wl_global_destroy);
   E_FREE_LIST(hooks, e_client_hook_del);
}

E_API Eina_Bool
e_comp_wl_subsurface_create(E_Client *ec, E_Client *epc, uint32_t id, struct wl_resource *surface_resource)
{
   struct wl_client *client;
   struct wl_resource *res;
   E_Comp_Wl_Subsurf_Data *sdata;
   E_Client *offscreen_parent = NULL;

   /* try to get the wayland client from the surface resource */
   if (!(client = wl_resource_get_client(surface_resource)))
     {
        ERR("Could not get client from resource %d",
            wl_resource_get_id(surface_resource));
        return EINA_FALSE;
     }

   if (!ec || e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return EINA_FALSE;

   if (!epc || e_object_is_del(E_OBJECT(epc)))
     {
        if (ec->comp_data->sub.watcher)
          tizen_subsurface_watcher_send_message(ec->comp_data->sub.watcher, TIZEN_SUBSURFACE_WATCHER_MSG_PARENT_ID_INVALID);

        /* We have to create a subsurface resource here even though it's error case
         * because server will send the fatal error when a client destroy a subsurface object.
         * Otherwise, server will kill a client by the fatal error.
         */
        if (!(res = wl_resource_create(client, &wl_subsurface_interface, 1, id)))
          {
             ERR("Failed to create subsurface resource");
             wl_resource_post_no_memory(surface_resource);
             return EINA_FALSE;
          }

        wl_resource_set_implementation(res, &_e_subsurface_interface, NULL, NULL);

        ERR("tizen_policy failed: invalid parent");
        return EINA_FALSE;
     }

   // reparent remote surface provider's subsurfaces
   if ((epc->comp_data) && (epc->comp_data->remote_surface.onscreen_parent))
     {
        offscreen_parent = epc;
        epc = epc->comp_data->remote_surface.onscreen_parent;
     }

   // check parent relationship is a cycle
     {
        E_Client *parent = epc;

        while(parent)
          {
             if (ec == parent)
               {
                  ERR("Subsurface parent relationship is a cycle : [child win : %zx, %s], [parent win : %zx, %s]",
                      e_client_util_win_get(ec), e_client_util_name_get(ec),
                      e_client_util_win_get(epc), e_client_util_name_get(epc));

                  return EINA_FALSE;
               }

             if ((parent->comp_data) && (parent->comp_data->sub.data))
               parent = parent->comp_data->sub.data->parent;
             else
               break;
          }
     }

   /* try to allocate subsurface data */
   if (!(sdata = E_NEW(E_Comp_Wl_Subsurf_Data, 1)))
     {
        ERR("Could not allocate space for subsurface data");
        goto dat_err;
     }

   /* try to create the subsurface resource */
   if (!(res = wl_resource_create(client, &wl_subsurface_interface, 1, id)))
     {
        ERR("Failed to create subsurface resource");
        wl_resource_post_no_memory(surface_resource);
        goto res_err;
     }

   /* set resource implementation */
   wl_resource_set_implementation(res, &_e_subsurface_interface, ec,
                                  _e_comp_wl_subsurface_destroy);
   e_object_ref(E_OBJECT(ec));

   e_comp_wl_surface_state_init(&sdata->cached, ec->w, ec->h);

   /* set subsurface data properties */
   sdata->cached_buffer_ref.buffer = NULL;
   sdata->resource = res;
   sdata->synchronized = EINA_TRUE;
   sdata->parent = epc;
   sdata->remote_surface.offscreen_parent = offscreen_parent;

   /* set subsurface client properties */
   ec->borderless = EINA_TRUE;
   ec->argb = EINA_TRUE;
   ec->lock_border = EINA_TRUE;
   ec->lock_focus_in = ec->lock_focus_out = EINA_TRUE;
   ec->netwm.state.skip_taskbar = EINA_TRUE;
   ec->netwm.state.skip_pager = EINA_TRUE;
   ec->no_shape_cut = EINA_TRUE;
   ec->border_size = 0;

   ELOGF("COMP", "         |subsurface_parent:%p", ec, epc);
   if (offscreen_parent)
     ELOGF("COMP", "         |offscreen_parent:%p", ec, offscreen_parent);

   if (epc)
     {
        if (epc->frame)
          {
             short layer = evas_object_layer_get(epc->frame);
             evas_object_layer_set(ec->frame, layer);
          }

        if (epc->comp_data)
          {
             /* append this client to the parents subsurface list */
             epc->comp_data->sub.list =
                eina_list_append(epc->comp_data->sub.list, ec);
             epc->comp_data->sub.list_changed = EINA_TRUE;
          }

        /* TODO: add callbacks ?? */
     }

   ec->comp_data->surface = surface_resource;
   ec->comp_data->sub.data = sdata;

   ec->lock_user_location = 0;
   ec->lock_client_location = 0;
   ec->lock_user_size = 0;
   ec->lock_client_size = 0;
   ec->lock_client_stacking = 0;
   ec->lock_user_shade = 0;
   ec->lock_client_shade = 0;
   ec->lock_user_maximize = 0;
   ec->lock_client_maximize = 0;
   ec->changes.need_maximize = 0;
   ec->maximized = E_MAXIMIZE_NONE;
   EC_CHANGED(ec);

   ec->new_client = ec->netwm.ping = EINA_TRUE;
   e_comp->new_clients++;
   e_client_unignore(ec);

   /* Delete 'below_obj' if it was created before 'E_Client' becomes subsurface.
    * It's not for subsurface. */
   E_FREE_FUNC(ec->comp_data->sub.below_obj, evas_object_del);

   e_comp_wl_hook_call(E_COMP_WL_HOOK_SUBSURFACE_CREATE, ec);
   return EINA_TRUE;

res_err:
   free(sdata);
dat_err:
   return EINA_FALSE;
}

EINTERN void
e_comp_wl_subsurface_parent_commit(E_Client *ec, Eina_Bool parent_synchronized)
{
   _e_comp_wl_subsurface_parent_commit(ec, parent_synchronized);
}

EINTERN Eina_Bool
e_comp_wl_subsurface_order_commit(E_Client *ec)
{
   return _e_comp_wl_subsurface_order_commit(ec);
}

EINTERN Eina_Bool
e_comp_wl_subsurface_commit(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata;

   /* check for valid subcompositor data */
   if (e_object_is_del(E_OBJECT(ec)) || !ec->comp_data) return EINA_FALSE;
   if (!(sdata = ec->comp_data->sub.data)) return EINA_FALSE;

   if (_e_comp_wl_subsurface_synchronized_get(sdata))
     _e_comp_wl_subsurface_commit_to_cache(ec);
   else
     {
        E_Client *subc;
        Eina_List *l;

        if (sdata->position.set)
          {
             E_Client *parent = sdata->parent;
             if (parent)
               {
                  evas_object_move(ec->frame, parent->x + sdata->position.x,
                                   parent->y + sdata->position.y);
                  sdata->position.set = EINA_FALSE;
               }
          }

        if (sdata->cached.has_data)
          {
             _e_comp_wl_subsurface_commit_to_cache(ec);
             _e_comp_wl_subsurface_commit_from_cache(ec);
          }
        else
          e_comp_wl_surface_commit(ec);

        EINA_LIST_FOREACH(ec->comp_data->sub.list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
        EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
          {
             if (ec != subc)
               _e_comp_wl_subsurface_parent_commit(subc, EINA_FALSE);
          }
     }

   return EINA_TRUE;
}

EINTERN Eina_Bool
e_comp_wl_subsurface_can_show(E_Client *ec)
{
   E_Comp_Wl_Subsurf_Data *sdata = ec->comp_data->sub.data;
   E_Client *invisible_parent;
   E_Client *topmost;

   /* if it's not subsurface */
   if (!sdata)
     return EINA_FALSE;

   invisible_parent = _e_comp_wl_subsurface_invisible_parent_get(ec);
   topmost = e_comp_wl_topmost_parent_get(ec);
   if (!topmost)
     return EINA_FALSE;

   /* if topmost is composited by compositor && if there is a invisible parent */
   if (topmost->redirected && invisible_parent)
     return EINA_FALSE;

   return EINA_TRUE;
}

EINTERN void
e_comp_wl_subsurface_show(E_Client *ec)
{
   _e_comp_wl_subsurface_show(ec);
}

EINTERN void
e_comp_wl_subsurface_hide(E_Client *ec)
{
   _e_comp_wl_subsurface_hide(ec);
}

EINTERN void
e_comp_wl_subsurface_restack_bg_rectangle(E_Client *ec)
{
   _e_comp_wl_subsurface_restack_bg_rectangle(ec);
}

EINTERN void
e_comp_wl_subsurface_restack(E_Client *ec)
{
   _e_comp_wl_subsurface_restack(ec);
}

E_API void
e_comp_wl_subsurface_stack_update(E_Client *ec)
{
   E_Client *topmost;

   if (!(ec) || !ec->comp_data) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (ec->comp_data->sub.restacking) return;

   /* return if ec isn't both a parent of a subsurface and a subsurface itself */
   if (!ec->comp_data->sub.list && !ec->comp_data->sub.below_list && !ec->comp_data->sub.data)
     {
        if (ec->comp_data->sub.below_obj)
          _e_comp_wl_subsurface_restack_bg_rectangle(ec);
        return;
     }

   topmost = e_comp_wl_topmost_parent_get(ec);

   _e_comp_wl_subsurface_restack(topmost);
   _e_comp_wl_subsurface_restack_bg_rectangle(topmost);

   //To update client stack list
   if ((ec->comp_data->sub.data) &&
       (ec->comp_data->sub.data->parent))
     {
        E_Client *parent;
        Evas_Object *o;

        parent = ec->comp_data->sub.data->parent;

        if ((parent->comp_data->sub.list) &&
            (eina_list_data_find(parent->comp_data->sub.list, ec)))
          {
             //stack above done
             o = evas_object_below_get(ec->frame);
             e_comp_object_layer_update(ec->frame, o, NULL);
          }
        else if ((parent->comp_data->sub.below_list) &&
                 (eina_list_data_find(parent->comp_data->sub.below_list, ec)))
          {
             //stack below done
             o = evas_object_above_get(ec->frame);
             e_comp_object_layer_update(ec->frame, NULL, o);
          }
     }
}

EINTERN Eina_Bool
e_comp_wl_video_subsurface_has(E_Client *ec)
{
   return _e_comp_wl_subsurface_video_has(ec);
}

EINTERN Eina_Bool
e_comp_wl_normal_subsurface_has(E_Client *ec)
{
   E_Client *subc;
   Eina_List *l;

   if (!ec) return EINA_FALSE;
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   if (!ec->comp_data) return EINA_FALSE;

   /* if a leaf client is not video cliet */
   if (ec->comp_data->sub.data && !ec->comp_data->sub.below_list &&
       !ec->comp_data->sub.below_list_pending &&
       ((!ec->comp_data->video_client) && (!e_client_video_hw_composition_check(ec))))
     return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list_pending, l, subc)
     if (e_comp_wl_normal_subsurface_has(subc))
        return EINA_TRUE;

   EINA_LIST_FOREACH(ec->comp_data->sub.below_list, l, subc)
     if (e_comp_wl_normal_subsurface_has(subc))
        return EINA_TRUE;

   return EINA_FALSE;
}

EINTERN void
e_comp_wl_subsurface_check_below_bg_rectangle(E_Client *ec)
{
   _e_comp_wl_subsurface_check_below_bg_rectangle(ec);
}
