#include "e.h"

/* local subsystem functions */

/* local subsystem globals */

/* externally accessible functions */
E_API void
e_focus_event_mouse_in(E_Client *ec)
{
   
   if ((e_config->focus_policy == E_FOCUS_MOUSE) ||
       (e_config->focus_policy == E_FOCUS_SLOPPY))
     {
        ELOGF("FOCUS", "focus set   | mouse in", ec);
        e_client_frame_focus_set(ec, EINA_TRUE);
     }
   if (e_config->use_auto_raise)
     {
        if (!ec->lock_user_stacking)
          evas_object_raise(ec->frame);
     }
}

E_API void
e_focus_event_mouse_out(E_Client *ec)
{
   if (e_config->focus_policy == E_FOCUS_MOUSE)
     {
        if (!ec->lock_focus_in)
          {
             if (ec->focused)
               {
                  ELOGF("FOCUS", "focus unset | mouse out", ec);
                  e_client_frame_focus_set(ec, EINA_FALSE);
               }
          }
     }
}

E_API void
e_focus_event_mouse_down(E_Client *ec)
{
   if (e_client_focus_policy_click(ec) ||
       e_config->always_click_to_focus)
     {
        ELOGF("FOCUS", "focus set   | mouse down", ec);
        e_client_frame_focus_set(ec, EINA_TRUE);

        if (ec->floating)
          evas_object_raise(ec->frame);
     }
   if (e_config->always_click_to_raise)
     {
        if (!ec->lock_user_stacking)
          evas_object_raise(ec->frame);
     }
}

E_API void
e_focus_event_mouse_up(E_Client *ec EINA_UNUSED)
{
}

E_API void
e_focus_event_focus_in(E_Client *ec EINA_UNUSED)
{
}

E_API void
e_focus_event_focus_out(E_Client *ec EINA_UNUSED)
{
}

/* local subsystem functions */

