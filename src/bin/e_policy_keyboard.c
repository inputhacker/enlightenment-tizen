#include "e_policy_keyboard.h"

E_API Eina_Bool
e_policy_client_is_keyboard(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->vkbd.vkbd) return EINA_TRUE;

   return EINA_FALSE;
}

E_API Eina_Bool
e_policy_client_is_keyboard_sub(E_Client *ec)
{
   E_OBJECT_CHECK_RETURN(ec, EINA_FALSE);
   E_OBJECT_TYPE_CHECK_RETURN(ec, E_CLIENT_TYPE, EINA_FALSE);

   if (ec->vkbd.vkbd) return EINA_FALSE;

   if ((ec->icccm.class) &&
       (!strcmp(ec->icccm.class, "ISF")))
     return EINA_TRUE;
   if ((ec->icccm.title) &&
       (!strcmp(ec->icccm.title, "ISF Popup")))
     return EINA_TRUE;

   return EINA_FALSE;
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
