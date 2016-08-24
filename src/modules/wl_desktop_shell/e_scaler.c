#define EXECUTIVE_MODE_ENABLED
#include "e.h"
#include <scaler-server-protocol.h>
#include <transform-server-protocol.h>

static void
_e_viewport_destroy(struct wl_resource *resource)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;
   if (!ec->comp_data->scaler.viewport) return;

   ec->comp_data->scaler.viewport = NULL;
   ec->comp_data->pending.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
   ec->comp_data->pending.buffer_viewport.surface.width = -1;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_viewport_cb_set(struct wl_client *client EINA_UNUSED,
                   struct wl_resource *resource,
                   wl_fixed_t src_x,
                   wl_fixed_t src_y,
                   wl_fixed_t src_width,
                   wl_fixed_t src_height,
                   int32_t dst_width,
                   int32_t dst_height)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->scaler.viewport);

   if (wl_fixed_to_double(src_width) < 0 || wl_fixed_to_double(src_height) < 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "source dimensions must be non-negative (%fx%f)",
                               wl_fixed_to_double(src_width),
                               wl_fixed_to_double(src_height));
        return;
     }

   if (dst_width <= 0 || dst_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "destination dimensions must be positive (%dx%d)",
                               dst_width, dst_height);
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.src_x = src_x;
   ec->comp_data->pending.buffer_viewport.buffer.src_y = src_y;
   ec->comp_data->pending.buffer_viewport.buffer.src_width = src_width;
   ec->comp_data->pending.buffer_viewport.buffer.src_height = src_height;
   ec->comp_data->pending.buffer_viewport.surface.width = dst_width;
   ec->comp_data->pending.buffer_viewport.surface.height = dst_height;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_set_source(struct wl_client *client EINA_UNUSED,
                          struct wl_resource *resource,
                          wl_fixed_t src_x,
                          wl_fixed_t src_y,
                          wl_fixed_t src_width,
                          wl_fixed_t src_height)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->scaler.viewport);

   if (src_width == wl_fixed_from_int(-1) && src_height == wl_fixed_from_int(-1))
     {
        /* unset source size */
        ec->comp_data->pending.buffer_viewport.buffer.src_width = wl_fixed_from_int(-1);
        ec->comp_data->pending.buffer_viewport.changed = 1;
        return;
     }

   if (src_width <= 0 || src_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "source size must be positive (%fx%f)",
                               wl_fixed_to_double(src_width),
                               wl_fixed_to_double(src_height));
        return;
     }

   ec->comp_data->pending.buffer_viewport.buffer.src_x = src_x;
   ec->comp_data->pending.buffer_viewport.buffer.src_y = src_y;
   ec->comp_data->pending.buffer_viewport.buffer.src_width = src_width;
   ec->comp_data->pending.buffer_viewport.buffer.src_height = src_height;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static void
_e_viewport_cb_set_destination(struct wl_client *client EINA_UNUSED,
                               struct wl_resource *resource,
                               int32_t dst_width,
                               int32_t dst_height)
{
   E_Client *ec = wl_resource_get_user_data(resource);

   if (e_object_is_del(E_OBJECT(ec))) return;

   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data);
   EINA_SAFETY_ON_NULL_RETURN(ec->comp_data->scaler.viewport);

   if (dst_width == -1 && dst_height == -1)
     {
        /* unset destination size */
        ec->comp_data->pending.buffer_viewport.surface.width = -1;
        ec->comp_data->pending.buffer_viewport.changed = 1;
        return;
     }

   if (dst_width <= 0 || dst_height <= 0)
     {
        wl_resource_post_error(resource,
                               WL_VIEWPORT_ERROR_BAD_VALUE,
                               "destination size must be positive (%dx%d)",
                               dst_width, dst_height);
        return;
     }

   ec->comp_data->pending.buffer_viewport.surface.width = dst_width;
   ec->comp_data->pending.buffer_viewport.surface.height = dst_height;
   ec->comp_data->pending.buffer_viewport.changed = 1;
}

static const struct wl_viewport_interface _e_viewport_interface = {
   _e_viewport_cb_destroy,
   _e_viewport_cb_set,
   _e_viewport_cb_set_source,
   _e_viewport_cb_set_destination
};

static void
_e_scaler_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_scaler_cb_get_viewport(struct wl_client *client EINA_UNUSED, struct wl_resource *scaler, uint32_t id, struct wl_resource *surface_resource)
{
   int version = wl_resource_get_version(scaler);
   E_Client *ec;
   struct wl_resource *res;

   if (!(ec = wl_resource_get_user_data(surface_resource))) return;
   if (!ec->comp_data) return;

   if (ec->comp_data && ec->comp_data->scaler.viewport)
     {
        wl_resource_post_error(scaler,
                               WL_SCALER_ERROR_VIEWPORT_EXISTS,
                               "a viewport for that surface already exists");
        return;
     }

   res = wl_resource_create(client, &wl_viewport_interface, version, id);
   if (res == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   ec->comp_data->scaler.viewport = res;
   wl_resource_set_implementation(res, &_e_viewport_interface, ec, _e_viewport_destroy);
}

static const struct wl_scaler_interface _e_scaler_interface =
{
   _e_scaler_cb_destroy,
   _e_scaler_cb_get_viewport
};

static void
_e_scaler_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &wl_scaler_interface, MIN(version, 2), id)))
     {
        ERR("Could not create scaler resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_scaler_interface, NULL, NULL);
}

static void
_e_rotator_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   if ((ec = wl_resource_get_user_data(resource)))
     {
        if (!e_object_is_del(E_OBJECT(ec)) && ec->comp_data)
          ec->comp_data->transform.enabled = EINA_FALSE;
     }

     wl_resource_destroy(resource);
}

static void
_e_rotator_cb_set(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   ec->comp_data->transform.enabled = EINA_TRUE;

   DBG("SET ROTATOR");
}

static void
_e_rotator_cb_unset(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   E_Client *ec;

   if (!(ec = wl_resource_get_user_data(resource))) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->comp_data) return;

   ec->comp_data->transform.enabled = EINA_FALSE;
   DBG("UNSET ROTATOR");
}

static const struct wl_rotator_interface _e_rotator_interface =
{
   _e_rotator_cb_destroy,
   _e_rotator_cb_set,
   _e_rotator_cb_unset,
};

static void
_e_transform_cb_destroy(struct wl_client *client EINA_UNUSED, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static void
_e_transform_cb_get_rotator(struct wl_client *client EINA_UNUSED, struct wl_resource *transform, uint32_t id, struct wl_resource *surface_resource)
{
   int version = wl_resource_get_version(transform);
   E_Client *ec;
   struct wl_resource *res;

   if (!(ec = wl_resource_get_user_data(surface_resource))) return;

   res = wl_resource_create(client, &wl_rotator_interface, version, id);
   if (res == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_rotator_interface, ec, NULL);
}

static const struct wl_transform_interface _e_transform_interface =
{
   _e_transform_cb_destroy,
   _e_transform_cb_get_rotator
};

static void
_e_transform_cb_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
   struct wl_resource *res;

   if (!(res = wl_resource_create(client, &wl_transform_interface, version, id)))
     {
        ERR("Could not create transform resource: %m");
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(res, &_e_transform_interface, NULL, NULL);
}

Eina_Bool
e_scaler_init(void)
{
   if (!e_comp) return EINA_FALSE;

   /* try to add scaler to wayland globals */
   if (!wl_global_create(e_comp_wl->wl.disp, &wl_scaler_interface, 2,
                         e_comp->wl_comp_data, _e_scaler_cb_bind))
     {
        ERR("Could not add scaler to wayland globals: %m");
        return EINA_FALSE;
     }

   if (!wl_global_create(e_comp_wl->wl.disp, &wl_transform_interface, 1,
                         e_comp->wl_comp_data, _e_transform_cb_bind))
     {
        ERR("Could not add transform to wayland globals: %m");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}
