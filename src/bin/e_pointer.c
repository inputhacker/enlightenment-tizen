#include "e.h"
#include <Ecore_Drm.h>

/* local variables */
static Eina_List *_ptrs = NULL;
static Eina_Bool _initted = EINA_FALSE;

/* temp variable */
static Eina_Bool override = EINA_FALSE;

/* move the cursor image with the calcaultion of the hot spot */
static void
_e_pointer_position_update(E_Pointer *ptr)
{
   int nx, ny;

   nx = ptr->x - ptr->hot.x;
   ny = ptr->y - ptr->hot.y;

   if (ptr->hwc)
      e_comp_object_hwc_update_set(ptr->o_ptr, EINA_TRUE);
   else
      evas_object_move(ptr->o_ptr, nx, ny);
}

static void
_e_pointer_map_transform(int width, int height, uint32_t transform,
                         int sx, int sy, int *dx, int *dy)
{
   switch (transform)
     {
      case WL_OUTPUT_TRANSFORM_NORMAL:
      default:
        *dx = sx, *dy = sy;
        break;
      case WL_OUTPUT_TRANSFORM_90:
        *dx = height - sy, *dy = sx;
        break;
      case WL_OUTPUT_TRANSFORM_180:
        *dx = width - sx, *dy = height - sy;
        break;
      case WL_OUTPUT_TRANSFORM_270:
        *dx = sy, *dy = width - sx;
        break;
     }
}

// TODO: transform the cursor position with hot spot...!!!!!!
static void
_e_pointer_object_rotation(E_Pointer *ptr)
{
   Evas_Map *map;
   int x1, y1, x2, y2, dx, dy;
   int32_t width, height;
   int cursor_w, cursor_h;
   uint32_t transform;
   E_Client *ec;
   int rotation;

   EINA_SAFETY_ON_NULL_RETURN(ptr);
   if (!ptr->o_ptr) return;

   ec = e_comp_object_client_get(ptr->o_ptr);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   rotation = ptr->rotation;

   evas_object_geometry_get(ec->frame, NULL, NULL, &cursor_w, &cursor_h);

   if ((rotation == 0) || (rotation % 90 != 0) || (rotation / 90 > 3))
     {
        evas_object_map_set(ec->frame, NULL);
        evas_object_map_enable_set(ec->frame, EINA_FALSE);
        return;
     }

   width = cursor_w;
   height = cursor_h;

   switch(rotation)
     {
      case 90:
         transform = WL_OUTPUT_TRANSFORM_90;
         width = cursor_h;
         height = cursor_w;
         break;
      case 180:
         transform = WL_OUTPUT_TRANSFORM_180;
         break;
      case 270:
         transform = WL_OUTPUT_TRANSFORM_270;
         width = cursor_h;
         height = cursor_w;
         break;
      default:
         transform = WL_OUTPUT_TRANSFORM_NORMAL;
         break;
     }

   map = evas_map_new(4);
   evas_map_util_points_populate_from_geometry(map,
                                               ec->x, ec->y,
                                               width, height, 0);

   x1 = 0.0;
   y1 = 0.0;
   x2 = width;
   y2 = height;

   _e_pointer_map_transform(width, height, transform,
                            x1, y1, &dx, &dy);
   evas_map_point_image_uv_set(map, 0, dx, dy);

   _e_pointer_map_transform(width, height, transform,
                            x2, y1, &dx, &dy);
   evas_map_point_image_uv_set(map, 1, dx, dy);

   _e_pointer_map_transform(width, height, transform,
                            x2, y2, &dx, &dy);
   evas_map_point_image_uv_set(map, 2, dx, dy);

   _e_pointer_map_transform(width, height, transform,
                            x1, y2, &dx, &dy);
   evas_map_point_image_uv_set(map, 3, dx, dy);

   evas_object_map_set(ec->frame, map);
   evas_object_map_enable_set(ec->frame, map ? EINA_TRUE : EINA_FALSE);

   evas_map_free(map);
}

static void
_e_pointer_hwc_rotation(E_Pointer *ptr)
{
   E_Client *ec;

   if (!ptr->o_ptr) return;

   ec = e_comp_object_client_get(ptr->o_ptr);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   // TODO: roatation cursor buffer with pixman
}

// TODO: transform the cursor position with hot spot...!!!!!!
static void
_e_pointer_rotation_apply(E_Pointer *ptr)
{
   EINA_SAFETY_ON_NULL_RETURN(ptr);

   if (ptr->hwc)
      _e_pointer_hwc_rotation(ptr);
   else
      _e_pointer_object_rotation(ptr);
}

static void
_e_pointer_cb_free(E_Pointer *ptr)
{
   _ptrs = eina_list_remove(_ptrs, ptr);

   free(ptr);
}

EINTERN int
e_pointer_init(void)
{
   _initted = EINA_TRUE;
   return 1;
}

EINTERN int
e_pointer_shutdown(void)
{
   _initted = EINA_FALSE;
   return 1;
}

EINTERN E_Pointer *
e_pointer_canvas_new(Ecore_Evas *ee, Eina_Bool filled)
{
   E_Pointer *ptr = NULL;

   EINA_SAFETY_ON_FALSE_RETURN_VAL(ee, NULL);
   if (!_initted) return NULL;

   /* allocate space for new pointer */
   if (!(ptr = E_OBJECT_ALLOC(E_Pointer, E_POINTER_TYPE, _e_pointer_cb_free)))
     return NULL;

   /* set default pointer properties */
   ptr->canvas = EINA_TRUE;
   ptr->w = ptr->h = e_config->cursor_size;
   ptr->e_cursor = e_config->use_e_cursor;

   ptr->ee = ee;
   ptr->evas = ecore_evas_get(ee);

   /* append this pointer to the list */
   _ptrs = eina_list_append(_ptrs, ptr);

   return ptr;
}

EINTERN void
e_pointer_object_set(E_Pointer *ptr, Evas_Object *obj, int x, int y)
{
   E_Client *ec;

   EINA_SAFETY_ON_NULL_RETURN(ptr);

   /* don't show cursor if in hidden mode */
   if ((!e_config->show_cursor) || (!e_comp_wl->ptr.enabled))
     {
        e_pointer_hide(ptr);
        return;
     }

   /* hide and unset the existed ptr->o_ptr */
   if (ptr->o_ptr)
     {
        ec = e_comp_object_client_get(ptr->o_ptr);
        if ((ec) && (!e_object_is_del(E_OBJECT(ec))))
          {
             ec->hidden = 1;
             ec->visible = EINA_FALSE;
             ec->comp_data->mapped = EINA_FALSE;
             ec->override = 1; /* ignore the previous cursor_ec */
          }

        if (ptr->o_ptr != obj)
           ptr->hwc = 0;

        /* hide cursor object */
        evas_object_hide(ptr->o_ptr);
        ptr->o_ptr = NULL;
        ptr->device = E_POINTER_NONE;

        /* Current if e_pointer set rotation, it can't' use hwc.
            if it can use hwc, comp override will be removed. */
        if (ptr->rotation !=0 && override)
          {
             e_comp_override_del();
             override = EINA_FALSE;
          }
     }

   /* update the hot spot of the cursor */
   ptr->hot.x = x;
   ptr->hot.y = y;

   /* if obj is not null, set the obj to ptr->o_ptr */
   if (obj)
     {
        ec = e_comp_object_client_get(obj);
        if (ec && e_pixmap_usable_get(ec->pixmap))
          {
             ec->hidden = 0;
             ec->visible = EINA_TRUE;
             evas_object_geometry_set(ec->frame, ec->x, ec->y, ec->w, ec->h);
             ec->comp_data->mapped = EINA_TRUE;
             ec->override = 0; /* do not ignore the cursor_ec to set the image object */
          }

        ptr->o_ptr = obj;

        /* apply the cursor obj rotation */
        _e_pointer_rotation_apply(ptr);

        /* move the pointer to the current position */
        _e_pointer_position_update(ptr);

        /* Current if e_pointer set rotation, it can't' use hwc.
           if it can use hwc, comp override will be removed. */
        switch(ptr->rotation)
          {
            case 90:
            case 180:
            case 270:
              if (!override)
                {
                   e_comp_override_add();
                   override = EINA_TRUE;
                }
               break;
          }

        /* show cursor object */
        evas_object_show(obj);
     }
}

EINTERN void
e_pointer_touch_move(E_Pointer *ptr, int x, int y)
{
   EINA_SAFETY_ON_NULL_RETURN(ptr);

   if (!e_config->show_cursor) return;
   if (!ptr->o_ptr) return;

   /* save the current position */
   ptr->x = x;
   ptr->y = y;

   if (ptr->device != E_POINTER_TOUCH) ptr->device = E_POINTER_TOUCH;

   _e_pointer_rotation_apply(ptr);
   _e_pointer_position_update(ptr);
}

EINTERN void
e_pointer_mouse_move(E_Pointer *ptr, int x, int y)
{
   EINA_SAFETY_ON_NULL_RETURN(ptr);

   if (!e_config->show_cursor) return;
   if (!ptr->o_ptr) return;

   /* save the current position */
   ptr->x = x;
   ptr->y = y;

   if (ptr->device != E_POINTER_MOUSE) ptr->device = E_POINTER_MOUSE;

   _e_pointer_rotation_apply(ptr);
   _e_pointer_position_update(ptr);
}

E_API void
e_pointer_hide(E_Pointer *ptr)
{
   EINA_SAFETY_ON_NULL_RETURN(ptr);
   if (!ptr->o_ptr) return;
   if (!evas_object_visible_get(ptr->o_ptr)) return;

   evas_object_hide(ptr->o_ptr);

   /* Current if e_pointer set rotation, it can't' use hwc.
      if it can use hwc, comp override will be removed. */
   if (ptr->rotation !=0 && override)
     {
        e_comp_override_del();
        override = EINA_FALSE;
     }
}

E_API Eina_Bool
e_pointer_is_hidden(E_Pointer *ptr)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ptr, EINA_TRUE);

   if (!e_config->show_cursor) return EINA_TRUE;
   if (ptr->o_ptr && evas_object_visible_get(ptr->o_ptr)) return EINA_FALSE;

   return EINA_TRUE;
}

E_API void
e_pointer_rotation_set(E_Pointer *ptr, int rotation)
{
   const Eina_List *l;
   Ecore_Drm_Device *dev;

   if (ptr->rotation == rotation) return;

   ptr->rotation = rotation;

   _e_pointer_rotation_apply(ptr);
   _e_pointer_position_update(ptr);

   EINA_LIST_FOREACH(ecore_drm_devices_get(), l, dev)
     ecore_drm_device_pointer_rotation_set(dev, rotation);

   /* Current if e_pointer set rotation, it can't' use hwc.
      if it can use hwc, comp override will be removed. */
   if ((!ptr->o_ptr) || (ptr->o_ptr && !evas_object_visible_get(ptr->o_ptr))) return;

   switch(rotation)
     {
       case 0:
          if (override)
            {
               e_comp_override_del();
               override = EINA_FALSE;
            }
          break;
       case 90:
       case 180:
       case 270:
          if (!override)
            {
               e_comp_override_add();
               override = EINA_TRUE;
            }
          break;
     }
}

E_API void
e_pointer_position_get(E_Pointer *ptr, int *x, int *y)
{
   EINA_SAFETY_ON_NULL_RETURN(ptr);

   if (!e_config->show_cursor) return;
   if (!ptr->o_ptr) return;
   if (!evas_object_visible_get(ptr->o_ptr)) return;

   *x = ptr->x;
   *y = ptr->y;
}

EINTERN Eina_Bool
e_pointer_hwc_set(E_Pointer *ptr, Eina_Bool set)
{
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ptr, EINA_FALSE);
   EINA_SAFETY_ON_FALSE_RETURN_VAL(ptr->o_ptr, EINA_FALSE);

   if (ptr->hwc == set) return EINA_TRUE;

   ptr->hwc = set;

   if (set)
    {
       e_comp_object_hwc_update_set(ptr->o_ptr, EINA_TRUE);
    }
   else
    {
       _e_pointer_rotation_apply(ptr);
       _e_pointer_position_update(ptr);
    }

   return EINA_TRUE;
}

E_API E_Pointer *
e_pointer_get(E_Client *ec)
{
   const Eina_List *l;
   E_Pointer *ptr;
   E_Client *ptr_ec = NULL;

   if ((!ec) || (e_object_is_del(E_OBJECT(ec)))) return NULL;

   EINA_LIST_FOREACH(_ptrs, l, ptr)
     {
        if (ptr->o_ptr)
          {
             ptr_ec = e_comp_object_client_get(ptr->o_ptr);
             if (ptr_ec == ec)
                return ptr;
          }
     }

  return NULL;
}
