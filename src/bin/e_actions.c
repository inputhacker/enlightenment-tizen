#include "e.h"

#define INITS
#define ACT_GO(name)                                      \
  {                                                       \
     act = e_action_add(#name);                           \
     if (act) act->func.go = _e_actions_act_##name##_go;  \
  }
#define ACT_FN_GO(act, use) \
  static void _e_actions_act_##act##_go(E_Object * obj EINA_UNUSED, const char *params use)

#define ACT_GO_MOUSE(name)                                            \
  {                                                                   \
     act = e_action_add(#name);                                       \
     if (act) act->func.go_mouse = _e_actions_act_##name##_go_mouse;  \
  }
#define ACT_FN_GO_MOUSE(act, use) \
  static void _e_actions_act_##act##_go_mouse(E_Object * obj EINA_UNUSED, const char *params use, E_Binding_Event_Mouse_Button * ev EINA_UNUSED)

#define ACT_GO_WHEEL(name)                                            \
  {                                                                   \
     act = e_action_add(#name);                                       \
     if (act) act->func.go_wheel = _e_actions_act_##name##_go_wheel;  \
  }
#define ACT_FN_GO_WHEEL(act, use) \
  static void _e_actions_act_##act##_go_wheel(E_Object * obj EINA_UNUSED, const char *params use, E_Binding_Event_Wheel * ev EINA_UNUSED)

#define ACT_GO_EDGE(name)                                           \
  {                                                                 \
     act = e_action_add(#name);                                     \
     if (act) act->func.go_edge = _e_actions_act_##name##_go_edge;  \
  }
#define ACT_FN_GO_EDGE(act, use) \
  static void _e_actions_act_##act##_go_edge(E_Object * obj EINA_UNUSED, const char *params use, E_Event_Zone_Edge * ev EINA_UNUSED)

#define ACT_GO_SIGNAL(name)                                             \
  {                                                                     \
     act = e_action_add(#name);                                         \
     if (act) act->func.go_signal = _e_actions_act_##name##_go_signal;  \
  }
#define ACT_FN_GO_SIGNAL(act, use) \
  static void _e_actions_act_##act##_go_signal(E_Object * obj EINA_UNUSED, const char *params use, const char *sig, const char *src)

#define ACT_GO_KEY(name)                                          \
  {                                                               \
     act = e_action_add(#name);                                   \
     if (act) act->func.go_key = _e_actions_act_##name##_go_key;  \
  }
#define ACT_FN_GO_KEY(act, use1, use2) \
  static void _e_actions_act_##act##_go_key(E_Object * obj EINA_UNUSED, const char *params use1, Ecore_Event_Key * ev use2)

#define ACT_END(name)                                       \
  {                                                         \
     act = e_action_add(#name);                             \
     if (act) act->func.end = _e_actions_act_##name##_end;  \
  }
#define ACT_FN_END(act, use) \
  static void _e_actions_act_##act##_end(E_Object * obj EINA_UNUSED, const char *params use)

#define ACT_END_MOUSE(name)                                             \
  {                                                                     \
     act = e_action_add(#name);                                         \
     if (act) act->func.end_mouse = _e_actions_act_##name##_end_mouse;  \
  }
#define ACT_FN_END_MOUSE(act, use) \
  static void _e_actions_act_##act##_end_mouse(E_Object * obj EINA_UNUSED, const char *params use, E_Binding_Event_Mouse_Button * ev EINA_UNUSED)

#define ACT_END_KEY(name)                                           \
  {                                                                 \
     act = e_action_add(#name);                                     \
     if (act) act->func.end_key = _e_actions_act_##name##_end_key;  \
  }
#define ACT_FN_END_KEY(act, use) \
  static void _e_actions_act_##act##_end_key(E_Object * obj EINA_UNUSED, const char *params use, Ecore_Event_Key * ev EINA_UNUSED)

/* local subsystem functions */
static void       _e_action_free(E_Action *act);
static E_Maximize _e_actions_maximize_parse(const char *maximize);
static int        _action_groups_sort_cb(const void *d1, const void *d2);

/* to save writing this in N places - the sections are defined here */
/***************************************************************************/
ACT_FN_GO(window_move, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   if (!((E_Client *)obj)->lock_user_location)
     e_client_act_move_begin((E_Client *)obj, NULL);
}

ACT_FN_GO_MOUSE(window_move, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   e_client_act_move_begin((E_Client *)obj, ev);
}

ACT_FN_GO_SIGNAL(window_move, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   if (!((E_Client *)obj)->lock_user_location)
     {
        if ((params) && (!strcmp(params, "end")))
          e_client_signal_move_end((E_Client *)obj, sig, src);
        else
          {
             if (!((E_Client *)obj)->moving)
               e_client_signal_move_begin((E_Client *)obj, sig, src);
          }
     }
}

ACT_FN_END(window_move, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   e_client_act_move_end((E_Client *)obj, NULL);
}

ACT_FN_END_MOUSE(window_move, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   e_client_act_move_end((E_Client *)obj, ev);
}

ACT_FN_GO_KEY(window_move, EINA_UNUSED, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_location)
     e_client_act_move_keyboard((E_Client *)obj);
}

/***************************************************************************/
ACT_FN_GO(window_resize, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   if (!((E_Client *)obj)->lock_user_size)
     e_client_act_resize_begin((E_Client *)obj, NULL, E_POINTER_RESIZE_NONE);
}

ACT_FN_GO_MOUSE(window_resize, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   if (!((E_Client *)obj)->lock_user_size)
     e_client_act_resize_begin((E_Client *)obj, ev, E_POINTER_RESIZE_NONE);
}

ACT_FN_GO_SIGNAL(window_resize, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   if (!((E_Client *)obj)->lock_user_size)
     {
        if ((params) && (!strcmp(params, "end")))
          e_client_signal_resize_end((E_Client *)obj, params, sig, src);
        else
          {
             if (!params) params = "";
             if (e_client_util_resizing_get((E_Client *)obj))
               e_client_signal_resize_end((E_Client *)obj, params, sig, src);
             else
               e_client_signal_resize_begin((E_Client *)obj, params, sig, src);
          }
     }
}

ACT_FN_END(window_resize, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   e_client_act_resize_end((E_Client *)obj, NULL);
}

ACT_FN_END_MOUSE(window_resize, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   e_client_act_resize_end((E_Client *)obj, ev);
}

ACT_FN_GO_KEY(window_resize, EINA_UNUSED, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_size)
     e_client_act_resize_keyboard((E_Client *)obj);
}

/***************************************************************************/
ACT_FN_GO(window_menu, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   e_client_act_menu_begin((E_Client *)obj, NULL, 0);
}

ACT_FN_GO_MOUSE(window_menu, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   e_client_act_menu_begin((E_Client *)obj, ev, 0);
}

ACT_FN_GO_KEY(window_menu, EINA_UNUSED, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   e_client_act_menu_begin((E_Client *)obj, NULL, 1);
}

/***************************************************************************/
ACT_FN_GO(window_raise, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_stacking)
     evas_object_raise(((E_Client *)obj)->frame);
}

/***************************************************************************/
ACT_FN_GO(window_lower, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_stacking)
     evas_object_lower(((E_Client *)obj)->frame);
}

/***************************************************************************/
ACT_FN_GO(window_close, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_close)
     e_client_act_close_begin((E_Client *)obj);
}

/***************************************************************************/
static E_Dialog *kill_dialog = NULL;

static void
_e_actions_cb_kill_dialog_ok(void *data, E_Dialog *dia)
{
   E_Object *obj;

   obj = data;
   if (dia)
     {
        e_object_del(E_OBJECT(kill_dialog));
        kill_dialog = NULL;
     }
   if ((!((E_Client *)obj)->lock_close) && (!((E_Client *)obj)->internal))
     e_client_act_kill_begin((E_Client *)obj);
}

static void
_e_actions_cb_kill_dialog_cancel(void *data EINA_UNUSED, E_Dialog *dia EINA_UNUSED)
{
   e_object_del(E_OBJECT(kill_dialog));
   kill_dialog = NULL;
}

static void
_e_actions_cb_kill_dialog_delete(void *data, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   _e_actions_cb_kill_dialog_cancel(NULL, data);
}

ACT_FN_GO(window_kill, EINA_UNUSED)
{
   E_Client *ec;
   char dialog_text[1024];

   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE) return;
   ec = (E_Client *)obj;
   if ((ec->lock_close) || (ec->internal)) return;

   if (kill_dialog) e_object_del(E_OBJECT(kill_dialog));

   snprintf(dialog_text, sizeof(dialog_text),
            _("You are about to kill %s.<br><br>"
              "Please keep in mind that all data from this window<br>"
              "which has not yet been saved will be lost!<br><br>"
              "Are you sure you want to kill this window?"),
            ec->icccm.name);

   kill_dialog = e_dialog_new(NULL,
                              "E", "_kill_dialog");
   if (!kill_dialog) return;
   evas_object_event_callback_add(kill_dialog->win, EVAS_CALLBACK_DEL,
                             _e_actions_cb_kill_dialog_delete, kill_dialog);
   e_dialog_title_set(kill_dialog,
                      _("Are you sure you want to kill this window?"));
   e_dialog_text_set(kill_dialog, _(dialog_text));
   e_dialog_icon_set(kill_dialog, "application-exit", 64);
   e_dialog_button_add(kill_dialog, _("Kill"), NULL,
                       _e_actions_cb_kill_dialog_ok, obj);
   e_dialog_button_add(kill_dialog, _("No"), NULL,
                       _e_actions_cb_kill_dialog_cancel, NULL);
   e_dialog_button_focus_num(kill_dialog, 1);
   //elm_win_center(kill_dialog->win, 1, 1);
   e_dialog_show(kill_dialog);
}

/***************************************************************************/
ACT_FN_GO(window_sticky_toggle, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_sticky)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (ec->sticky) e_client_unstick(ec);
        else e_client_stick(ec);
     }
}

/***************************************************************************/
ACT_FN_GO(window_sticky, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_sticky)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (params)
          {
             if (atoi(params) == 1)
               e_client_stick(ec);
             else if (atoi(params) == 0)
               e_client_unstick(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_iconic_toggle, EINA_UNUSED)
{
   E_Client *ec;

   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   ec = (E_Client *)obj;

   if ((!ec->lock_user_iconify) && (!ec->fullscreen) &&
       ((ec->netwm.type == E_WINDOW_TYPE_NORMAL) ||
        (ec->netwm.type == E_WINDOW_TYPE_UNKNOWN)))
     {
        if (ec->iconic) e_client_uniconify(ec);
        else e_client_iconify(ec);
     }
}

/***************************************************************************/
ACT_FN_GO(window_iconic, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_iconify)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (params)
          {
             if (atoi(params) == 1)
               e_client_iconify(ec);
             else if (atoi(params) == 0)
               e_client_uniconify(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_fullscreen_toggle, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_fullscreen)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (ec->fullscreen)
          e_client_unfullscreen(ec);
        else if (!params || *params == '\0')
          e_client_fullscreen(ec, e_config->fullscreen_policy);
        else if (!strcmp(params, "resize"))
          e_client_fullscreen(ec, E_FULLSCREEN_RESIZE);
        else if (!strcmp(params, "zoom"))
          e_client_fullscreen(ec, E_FULLSCREEN_ZOOM);
     }
}

/***************************************************************************/
ACT_FN_GO(window_fullscreen, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_fullscreen)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (params)
          {
             int v;
             char buf[32];

             buf[0] = 0;
             if (sscanf(params, "%i %20s", &v, buf) == 2)
               {
                  if (v == 1)
                    {
                       if (*buf == '\0')
                         e_client_fullscreen(ec, e_config->fullscreen_policy);
                       else if (!strcmp(buf, "resize"))
                         e_client_fullscreen(ec, E_FULLSCREEN_RESIZE);
                       else if (!strcmp(buf, "zoom"))
                         e_client_fullscreen(ec, E_FULLSCREEN_ZOOM);
                    }
                  else if (v == 0)
                    e_client_unfullscreen(ec);
               }
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_maximized_toggle, )
{
   E_Client *ec;
   Eina_Bool resize = EINA_FALSE;

   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   ec = (E_Client *)obj;

   /* internal dialog which is resizable */
   if (ec->internal && (ec->netwm.type == E_WINDOW_TYPE_DIALOG))
     resize = (ec->icccm.max_w != ec->icccm.min_w);
   if ((!ec->lock_user_maximize) && (!ec->fullscreen) &&
       (resize || (ec->netwm.type == E_WINDOW_TYPE_NORMAL) ||
        (ec->netwm.type == E_WINDOW_TYPE_UNKNOWN)))
     {
        if ((ec->maximized & E_MAXIMIZE_TYPE) != E_MAXIMIZE_NONE)
          {
             if (!params)
               e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
             else
               {
                  E_Maximize max;

                  max = _e_actions_maximize_parse(params);
                  max &= E_MAXIMIZE_DIRECTION;
                  if (max == E_MAXIMIZE_VERTICAL)
                    {
                       if (ec->maximized & E_MAXIMIZE_VERTICAL)
                         e_client_unmaximize(ec, E_MAXIMIZE_VERTICAL);
                       else
                         goto maximize;
                    }
                  else if (max == E_MAXIMIZE_HORIZONTAL)
                    {
                       if (ec->maximized & E_MAXIMIZE_HORIZONTAL)
                         e_client_unmaximize(ec, E_MAXIMIZE_HORIZONTAL);
                       else
                         goto maximize;
                    }
                  else if (max == E_MAXIMIZE_LEFT)
                    {
                       if (ec->maximized & E_MAXIMIZE_LEFT)
                         e_client_unmaximize(ec, E_MAXIMIZE_LEFT);
                       else
                         goto maximize;
                    }
                  else if (max == E_MAXIMIZE_RIGHT)
                    {
                       if (ec->maximized & E_MAXIMIZE_RIGHT)
                         e_client_unmaximize(ec, E_MAXIMIZE_RIGHT);
                       else
                         goto maximize;
                    }
                  else
                    e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
               }
          }
        else
          {
maximize:
             e_client_maximize(ec, _e_actions_maximize_parse(params));
          }
     }
}
/***************************************************************************/
ACT_FN_GO(window_maximized, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_maximize)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (params)
          {
             E_Maximize max;
             int v, ret;
             char s1[32], s2[32];

             max = (e_config->maximize_policy & E_MAXIMIZE_DIRECTION);
             ret = sscanf(params, "%i %20s %20s", &v, s1, s2);
             if (ret == 3)
               {
                  if (!strcmp(s2, "horizontal"))
                    max = E_MAXIMIZE_HORIZONTAL;
                  else if (!strcmp(s2, "vertical"))
                    max = E_MAXIMIZE_VERTICAL;
                  else if (!strcmp(s2, "left"))
                    max = E_MAXIMIZE_LEFT;
                  else if (!strcmp(s2, "right"))
                    max = E_MAXIMIZE_RIGHT;
                  else
                    max = E_MAXIMIZE_BOTH;
               }
             if (ret > 1)
               {
                  if (v == 1)
                    {
                       if (!strcmp(s1, "fullscreen"))
                         e_client_maximize(ec, E_MAXIMIZE_FULLSCREEN | max);
                       else if (!strcmp(s1, "smart"))
                         e_client_maximize(ec, E_MAXIMIZE_SMART | max);
                       else if (!strcmp(s1, "expand"))
                         e_client_maximize(ec, E_MAXIMIZE_EXPAND | max);
                       else if (!strcmp(s1, "fill"))
                         e_client_maximize(ec, E_MAXIMIZE_FILL | max);
                       else
                         e_client_maximize(ec, (e_config->maximize_policy & E_MAXIMIZE_TYPE) | max);
                    }
                  else if (v == 0)
                    e_client_unmaximize(ec, max);
               }
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_shaded_toggle, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_shade)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (ec->shaded)
          {
             if (!params)
               e_client_unshade(ec, E_DIRECTION_UP);
             else
               {
                  if (!strcmp(params, "up"))
                    e_client_unshade(ec, E_DIRECTION_UP);
                  else if (!strcmp(params, "down"))
                    e_client_unshade(ec, E_DIRECTION_DOWN);
                  else if (!strcmp(params, "left"))
                    e_client_unshade(ec, E_DIRECTION_LEFT);
                  else if (!strcmp(params, "right"))
                    e_client_unshade(ec, E_DIRECTION_RIGHT);
                  else
                    e_client_unshade(ec, E_DIRECTION_UP);
               }
          }
        else
          {
             if (!params)
               e_client_shade(ec, E_DIRECTION_UP);
             else
               {
                  if (!strcmp(params, "up"))
                    e_client_shade(ec, E_DIRECTION_UP);
                  else if (!strcmp(params, "down"))
                    e_client_shade(ec, E_DIRECTION_DOWN);
                  else if (!strcmp(params, "left"))
                    e_client_shade(ec, E_DIRECTION_LEFT);
                  else if (!strcmp(params, "right"))
                    e_client_shade(ec, E_DIRECTION_RIGHT);
                  else
                    e_client_shade(ec, E_DIRECTION_UP);
               }
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_shaded, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (!((E_Client *)obj)->lock_user_shade)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (!e_comp_object_frame_allowed(ec->frame)) return;
        if (params)
          {
             int v;
             char buf[32];

             if (sscanf(params, "%i %20s", &v, buf) == 2)
               {
                  if (v == 1)
                    {
                       if (!strcmp(buf, "up"))
                         e_client_shade(ec, E_DIRECTION_UP);
                       else if (!strcmp(buf, "down"))
                         e_client_shade(ec, E_DIRECTION_DOWN);
                       else if (!strcmp(buf, "left"))
                         e_client_shade(ec, E_DIRECTION_LEFT);
                       else if (!strcmp(buf, "right"))
                         e_client_shade(ec, E_DIRECTION_RIGHT);
                    }
                  else if (v == 0)
                    {
                       if (!strcmp(buf, "up"))
                         e_client_unshade(ec, E_DIRECTION_UP);
                       else if (!strcmp(buf, "down"))
                         e_client_unshade(ec, E_DIRECTION_DOWN);
                       else if (!strcmp(buf, "left"))
                         e_client_unshade(ec, E_DIRECTION_LEFT);
                       else if (!strcmp(buf, "right"))
                         e_client_unshade(ec, E_DIRECTION_RIGHT);
                    }
               }
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_borderless_toggle, EINA_UNUSED)
{
   if ((!obj) || (obj->type != E_CLIENT_TYPE))
     obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (!((E_Client *)obj)->lock_border)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (!e_comp_object_frame_allowed(ec->frame)) return;
        ec->borderless = !ec->borderless;

        ec->border.changed = 1;
        EC_CHANGED(ec);
     }
}

/***************************************************************************/
ACT_FN_GO(window_border_set, EINA_UNUSED)
{
   if ((!obj) || (obj->type != E_CLIENT_TYPE))
     obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (!((E_Client *)obj)->lock_border)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (!e_comp_object_frame_allowed(ec->frame)) return;
        if (ec && params)
          {
             eina_stringshare_replace(&ec->bordername, params);
             ec->border.changed = 1;
             EC_CHANGED(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_border_cycle, EINA_UNUSED)
{
   if ((!obj) || (obj->type != E_CLIENT_TYPE))
     obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (!((E_Client *)obj)->lock_border)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if (!e_comp_object_frame_allowed(ec->frame)) return;
        if (ec && params)
          {
             const char *space;

             while (*params == ' ')
               params++;

             if (ec->bordername)
               {
                  const char *bdname = params;

                  while ((space = strchr(bdname, ' ')))
                    {
                       if (strncmp(ec->bordername, bdname, space - bdname) == 0)
                         {
                            bdname = space + 1;
                            while (*bdname == ' ')
                              bdname++;
                            space = strchr(bdname, ' ');
                            if (space)
                              eina_stringshare_replace_length(
                                &ec->bordername,
                                bdname, space - bdname);
                            else
                              eina_stringshare_replace(&ec->bordername, bdname);
                            ec->border.changed = 1;
                            EC_CHANGED(ec);
                            return;
                         }
                       bdname = space + 1;
                       while (*bdname == ' ')
                         bdname++;
                    }
               }

             space = strchr(params, ' ');
             if (space)
               eina_stringshare_replace_length(&ec->bordername,
                                               params, space - params);
             else
               eina_stringshare_replace(&ec->bordername, params);
             ec->border.changed = 1;
             EC_CHANGED(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_pinned_toggle, EINA_UNUSED)
{
   if ((!obj) || (obj->type != E_CLIENT_TYPE))
     obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (!((E_Client *)obj)->lock_border)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if ((ec->netwm.state.stacking == E_STACKING_BELOW) &&
            (ec->user_skip_winlist) && (ec->borderless))
          e_client_pinned_set(ec, 0);
        else
          e_client_pinned_set(ec, 1);
     }
}

/***************************************************************************/
ACT_FN_GO(window_move_by, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (params)
     {
        int dx, dy;

        if (sscanf(params, "%i %i", &dx, &dy) == 2)
          {
             E_Client *ec;

             ec = (E_Client *)obj;
             evas_object_move(ec->frame, ec->x + dx, ec->y + dy);

             e_util_pointer_center(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_move_to, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   if (params)
     {
        E_Client *ec;
        int x, y, zx, zy, zw, zh;
        char cx, cy;

        ec = (E_Client *)obj;
        e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

        if (sscanf(params, "%c%i %c%i", &cx, &x, &cy, &y) == 4)
          {
             x += zx;
             y += zy;
          }
        else if (sscanf(params, "* %c%i", &cy, &y) == 2)
          {
             /* Updated y, keep x. */
             y += zy;
             x = ec->x;
             cx = 0;
          }
        else if (sscanf(params, "%c%i *", &cx, &x) == 2)
          {
             /* Updated x, keep y. */
             x += zx;
             y = ec->y;
             cy = 0;
          }
        else return;

        if (cx == '-') x = zw - ec->w - x + 2 * zx;  /* compensate x with zx */
        if (cy == '-') y = zh - ec->h - y + 2 * zy;  /* compensate y with zy */

        if ((x != ec->x) || (y != ec->y))
          {
             evas_object_move(ec->frame, x, y);

             e_util_pointer_center(ec);
          }
     }
}


/***************************************************************************/
ACT_FN_GO(window_quick_tile_to_quadrant, )
{
   E_Client *ec;
   int x, y, zx, zy, zw, zh;
   if ((!obj) || (obj->type != E_CLIENT_TYPE))
      obj = E_OBJECT(e_client_focused_get());

   if (!obj) return;

   ec = (E_Client *)obj;
   if((ec->maximized & E_MAXIMIZE_TYPE) != E_MAXIMIZE_NONE)
      e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

   if (params)
     {
        if (strcmp(params, "upper_left") == 0)
          {
             x = zx;
             y = zy;
          }
        else if (strcmp(params, "upper_right") == 0)
          {
             x = zx + (zw / 2);
             y = zy;
          }
        else if (strcmp(params, "lower_left") == 0)
          {
             x = zx;
             y = zy + (zh / 2);
          }
        else if (strcmp(params, "lower_right") == 0)
          {
             x = zx + (zw / 2);
             y = zy + (zh / 2);
          }
        else
          return;

        evas_object_geometry_set(ec->frame, x, y, zw / 2, zh / 2);
     }
}


/***************************************************************************/
ACT_FN_GO(window_move_to_center, EINA_UNUSED)
{
   E_Client *ec;

   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   ec = (E_Client *)obj;
   e_comp_object_util_center(ec->frame);

   e_util_pointer_center(ec);
}

/***************************************************************************/
ACT_FN_GO(window_resize_by, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   if (params)
     {
        int dw, dh;

        if (sscanf(params, "%i %i", &dw, &dh) == 2)
          {
             E_Client *ec;

             ec = (E_Client *)obj;

             dw += ec->w;
             dh += ec->h;
             e_client_resize_limit(ec, &dw, &dh);
             evas_object_resize(ec->frame, dw, dh);

             e_util_pointer_center(ec);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_push, )
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   if (params)
     {
        E_Client *ec, *cur;
        E_Desk *desk_current;
        int hdir, vdir;
        int x, y, zx, zy, zw, zh;

        if (strcmp(params, "left") == 0)
          {
             hdir = -1;
             vdir = 0;
          }
        else if (strcmp(params, "right") == 0)
          {
             hdir = +1;
             vdir = 0;
          }
        else if (strcmp(params, "up") == 0)
          {
             hdir = 0;
             vdir = -1;
          }
        else if (strcmp(params, "down") == 0)
          {
             hdir = 0;
             vdir = +1;
          }
        else if (strcmp(params, "up-left") == 0)
          {
             hdir = -1;
             vdir = -1;
          }
        else if (strcmp(params, "up-right") == 0)
          {
             hdir = +1;
             vdir = -1;
          }
        else if (strcmp(params, "down-left") == 0)
          {
             hdir = -1;
             vdir = +1;
          }
        else if (strcmp(params, "down-right") == 0)
          {
             hdir = +1;
             vdir = +1;
          }
        else
          return;

        ec = (E_Client *)obj;
        e_zone_useful_geometry_get(ec->zone, &zx, &zy, &zw, &zh);

        if (hdir < 0) x = zx;
        else if (hdir > 0)
          x = zx + zw - ec->w;
        else x = ec->x;

        if (vdir < 0) y = zy;
        else if (vdir > 0)
          y = zy + zh - ec->h;
        else y = ec->y;

        desk_current = e_desk_current_get(ec->zone);

        E_CLIENT_FOREACH(cur)
          {
             if (((cur->desk == desk_current) || (cur->sticky)) && (ec != cur) && (!cur->iconic))
               {
                  if ((hdir < 0)
                      && (cur->x + cur->w < ec->x)
                      && (E_SPANS_COMMON(ec->y, ec->h, cur->y, cur->h)))
                    x = MAX(x, cur->x + cur->w);
                  else if ((hdir > 0)
                           && (cur->x > ec->x + ec->w)
                           && (E_SPANS_COMMON(ec->y, ec->h, cur->y, cur->h)))
                    x = MIN(x, zx + cur->x - ec->w);

                  if ((vdir < 0)
                      && (cur->y + cur->h < ec->y)
                      && (E_SPANS_COMMON(ec->x, ec->w, cur->x, cur->w)))
                    y = MAX(y, cur->y + cur->h);
                  else if ((vdir > 0)
                           && (cur->y > ec->y + ec->h)
                           && (E_SPANS_COMMON(ec->x, ec->w, cur->x, cur->w)))
                    y = MIN(y, zy + cur->y - ec->h);
               }
          }

        if ((x != ec->x) || (y != ec->y))
          {
             evas_object_move(ec->frame, x, y);
             e_util_pointer_center(ec);
          }
     }
}

/*
 * These actions jump to a window with the given name. It uses the last focused
 * window it finds (going through e_client_focus_stack_get), so the name should
 * be unique, if you want to jump to a specific window.
 *
 */
static int
window_jump_to(const char *params)
{
   Eina_List *l, *ll;
   E_Zone *current_zone;
   E_Client *ec;

   if (!params) return 0;

   /* Go through the list of all windows (that's what the focus stack is used for) */
   l = e_client_focus_stack_get();
   /* If we can start at the second window, we start there in order to cycle.
    * If not, there is only one window, so let's use it */
   if (eina_list_next(l))
     l = l->next;
   EINA_LIST_FOREACH(l, ll, ec)
     {
        if (!ec->icccm.name) continue;
        if (strcasecmp(ec->icccm.name, params)) continue;
        /* Jump to the screen the window is on if it isn't on the current screen but
         * only if we don't have to warp the pointer anyway */
        current_zone = e_zone_current_get();

        /* Change the virtual desktop if the window isn't on the current virtual desktop */
        e_desk_show(ec->desk);

        /* A minimized window wont be focusable for key input, un-minimize it */
        if (!ec->lock_user_iconify)
          e_client_uniconify(ec);


        evas_object_raise(ec->frame);
        if (ec->zone != current_zone)
          e_util_pointer_center(ec);
        if (e_config->focus_policy_ext != E_FOCUS_EXT_TOP_STACK)
          {
             ELOGF("FOCUS", "focus set   | window_jump_to", ec);
             e_client_frame_focus_set(ec, EINA_TRUE);
          }
        return 1;
     }
   return 0;
}

ACT_FN_GO(window_jump_to, )
{
   window_jump_to(params);
}

/***************************************************************************/
ACT_FN_GO(window_drag_icon, EINA_UNUSED)
{
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }
   {
      E_Client *ec;

      ec = (E_Client *)obj;
      ec->drag.start = 1;
      ec->drag.x = -1;
      ec->drag.y = -1;
   }
}

/***************************************************************************/
ACT_FN_GO(window_desk_move_by, )
{
   E_Client *ec;
   int x, y;

   if (!params) return;
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   ec = (E_Client *)obj;
   if ((!ec->zone) || (!ec->desk)) return;
   if (sscanf(params, "%d %d", &x, &y) == 2)
     {
        E_Desk *desk;
        int dx, dy;
        int to_x = 0, to_y = 0;

        e_desk_xy_get(ec->desk, &dx, &dy);

        to_x = dx + x;
        to_y = dy + y;
        while (!(desk = e_desk_at_xy_get(ec->zone, to_x, to_y)))
          {
             /* here we are out of our desktop range */
             while (to_x >= ec->zone->desk_x_count)
               {
                  to_x -= ec->zone->desk_x_count;
                  to_y++;
               }
             while (to_x < 0)
               {
                  to_x += ec->zone->desk_x_count;
                  to_y--;
               }

             while (to_y >= ec->zone->desk_y_count)
               to_y -= ec->zone->desk_y_count;
             while (to_y < 0)
               to_y += ec->zone->desk_y_count;
          }

        if (desk)
          {
             /* send the border to the required desktop. */
             e_client_desk_set(ec, desk);
             /* switch desktop. Quite useful from the interface point of view. */
             e_zone_desk_flip_by(ec->zone, to_x - dx, to_y - dy);
             if (!ec->lock_user_stacking)
               evas_object_raise(ec->frame);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(window_zone_move_by, )
{
   E_Client *ec;
   E_Zone *zone;
   E_Maximize max;
   int move;
   char *end;
   E_Fullscreen fs;
   Eina_Bool fullscreen;

   if (!params) return;
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   ec = (E_Client *)obj;
   /* bad */
   if (!ec->zone) return;
   /* only one screen */
   if (eina_list_count(e_comp->zones) < 2) return;
   errno = 0;
   move = strtol(params, &end, 10);
   if ((!end) || end[0] || errno) return;
   move += ec->zone->num;
   if (move < 0)
     move = eina_list_count(e_comp->zones) - 1;
   else if ((unsigned int)move >= eina_list_count(e_comp->zones))
     move = 0;
   zone = e_comp_zone_number_get(move);
   if (!zone) return;
   max = ec->maximized;
   fs = ec->fullscreen_policy;
   fullscreen = ec->fullscreen;
   if (ec->maximized) e_client_unmaximize(ec, E_MAXIMIZE_BOTH);
   if (fullscreen) e_client_unfullscreen(ec);
   e_client_zone_set(ec, zone);
//   e_client_desk_set(ec, e_desk_current_get(zone));
   if (max) e_client_maximize(ec, max);
   if (fullscreen) e_client_fullscreen(ec, fs);
   if (e_config->focus_policy_ext != E_FOCUS_EXT_TOP_STACK)
     {
        ELOGF("FOCUS", "focus set   | window_zone_move_by_action", ec);
     }
   e_client_frame_focus_set(ec, EINA_TRUE);
}

/***************************************************************************/
ACT_FN_GO(window_desk_move_to, )
{
   E_Client *ec;
   int x, y;

   if (!params) return;
   if (!obj) obj = E_OBJECT(e_client_focused_get());
   if (!obj) return;
   if (obj->type != E_CLIENT_TYPE)
     {
        obj = E_OBJECT(e_client_focused_get());
        if (!obj) return;
     }

   ec = (E_Client *)obj;
   if ((!ec->zone) || (!ec->desk)) return;
   if (sscanf(params, "%d %d", &x, &y) == 2)
     {
        E_Desk *desk;

        desk = e_desk_at_xy_get(ec->zone, x, y);
        if (desk) e_client_desk_set(ec, desk);
     }
}

/***************************************************************************/
static E_Zone *
_e_actions_zone_get(E_Object *obj)
{
   if (obj)
     {
        if (obj->type == (int)E_COMP_TYPE)
          return e_zone_current_get();
        else if (obj->type == (int)E_ZONE_TYPE)
          return (E_Zone *)obj;
        else if (obj->type == (int)E_CLIENT_TYPE)
          return ((E_Client *)obj)->zone;
        else if (e_obj_is_win(obj))
          return e_win_client_get((void*)obj)->zone;
     }
   return e_zone_current_get();
}

ACT_FN_GO(desk_flip_by, )
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (zone)
     {
        if (params)
          {
             int dx = 0, dy = 0;

             if (sscanf(params, "%i %i", &dx, &dy) == 2)
               e_zone_desk_flip_by(zone, dx, dy);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_flip_to, )
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (zone)
     {
        if (params)
          {
             int dx = 0, dy = 0;

             if (sscanf(params, "%i %i", &dx, &dy) == 2)
               e_zone_desk_flip_to(zone, dx, dy);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_flip_prev, EINA_UNUSED)
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (!zone) return;
   e_zone_desk_flip_to(zone, zone->desk_x_prev, zone->desk_y_prev);
}

/***************************************************************************/
ACT_FN_GO(desk_linear_flip_by, )
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (zone)
     {
        if (params)
          {
             int dx = 0;

             if (sscanf(params, "%i", &dx) == 1)
               e_zone_desk_linear_flip_by(zone, dx);
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_linear_flip_to, )
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (zone)
     {
        if (params)
          {
             int dx = 0;

             if (sscanf(params, "%i", &dx) == 1)
               e_zone_desk_linear_flip_to(zone, dx);
          }
     }
}

#define DESK_ACTION_ALL(zone, act) \
   E_Zone *zone; \
   const Eina_List *lz; \
   \
   EINA_LIST_FOREACH(e_comp->zones, lz, zone) { \
      act; \
   }

/***************************************************************************/
ACT_FN_GO(desk_linear_flip_to_screen, )
{
   if (params)
     {
        int dx = 0, ds = 0;

        if (sscanf(params, "%i %i", &dx, &ds) == 2)
          {
             int current_screen = 0;

             DESK_ACTION_ALL(zone,
                             if (current_screen == ds)
                               {
                                  e_zone_desk_linear_flip_to(zone, dx);
                                  break;
                               }
                             else current_screen++;
                            );
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_flip_by_all, )
{
   if (params)
     {
        int dx = 0, dy = 0;

        if (sscanf(params, "%i %i", &dx, &dy) == 2)
          {
             DESK_ACTION_ALL(zone, e_zone_desk_flip_by(zone, dx, dy));
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_flip_to_all, )
{
   if (params)
     {
        int dx = 0, dy = 0;

        if (sscanf(params, "%i %i", &dx, &dy) == 2)
          {
             DESK_ACTION_ALL(zone, e_zone_desk_flip_to(zone, dx, dy));
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_linear_flip_by_all, )
{
   if (params)
     {
        int dx = 0;

        if (sscanf(params, "%i", &dx) == 1)
          {
             DESK_ACTION_ALL(zone, e_zone_desk_linear_flip_by(zone, dx));
          }
     }
}

/***************************************************************************/
ACT_FN_GO(desk_linear_flip_to_all, )
{
   if (params)
     {
        int dx = 0;

        if (sscanf(params, "%i", &dx) == 1)
          {
             DESK_ACTION_ALL(zone, e_zone_desk_linear_flip_to(zone, dx));
          }
     }
}

#define ZONE_DESK_ACTION(con_num, zone_num, zone, act)                  \
  E_Zone * zone;                                                        \
  if ((con_num < 0) || (zone_num < 0)) {                                \
       Eina_List *l;                                                    \
       if ((con_num >= 0) && (zone_num < 0)) /* con=1 zone=all */ {     \
            EINA_LIST_FOREACH(e_comp->zones, l, zone) {                 \
                 act;                                                   \
              } }                                                       \
       else if ((con_num < 0) && (zone_num >= 0)) /* c=all zone=1 */ {  \
              zone = e_comp_zone_number_get(zone_num);                  \
              if (zone)                                                 \
                act;                                                    \
                    }                                                   \
       else if ((con_num < 0) && (zone_num < 0)) /* c=all zone=all */ { \
              EINA_LIST_FOREACH(e_comp->zones, lll, zone) {             \
                   act;                                                 \
                } } }                                                   \
  else {                                                                \
       zone = e_comp_zone_number_get(zone_num);                         \
       if (zone) act;                                                   \
    }

/***************************************************************************/
ACT_FN_GO(desk_deskshow_toggle, EINA_UNUSED)
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (!zone) zone = e_zone_current_get();
   if (zone) e_desk_deskshow(zone);
}

ACT_FN_GO(cleanup_windows, EINA_UNUSED)
{
   E_Zone *zone;

   zone = _e_actions_zone_get(obj);
   if (!zone) zone = e_zone_current_get();
   if (zone) e_place_zone_region_smart_cleanup(zone);
}

/***************************************************************************/
ACT_FN_GO(pointer_resize_push, )
{
   if (!obj) return;
   if (obj->type == E_CLIENT_TYPE)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if ((ec->lock_user_size) || (ec->shaded) || (ec->shading) ||
            (ec->fullscreen) || ((ec->maximized) && (!e_config->allow_manip)))
          return;
        ERR("pointer_resize_push Not Supported.");
     }
}

/***************************************************************************/
ACT_FN_GO(pointer_resize_pop, )
{
   if (!obj) return;
   if (obj->type == E_CLIENT_TYPE)
     {
        E_Client *ec;

        ec = (E_Client *)obj;
        if ((ec->lock_user_size) || (ec->shaded) || (ec->shading) ||
            (ec->fullscreen) || ((ec->maximized) && (!e_config->allow_manip)))
          return;
        ERR("pointer_resize_push Not Supported.");
     }
}

/***************************************************************************/
typedef struct _Delayed_Action Delayed_Action;

struct _Delayed_Action
{
   int          mouse, button;
   const char  *key;
   E_Object    *obj;
   Ecore_Timer *timer;
   struct
   {
      const char *action, *params;
   } def, delayed;
};

static Eina_List *_delayed_actions = NULL;

static void
_delayed_action_free(Delayed_Action *da)
{
   if (da->obj) e_object_unref(da->obj);
   if (da->key) eina_stringshare_del(da->key);
   if (da->timer) ecore_timer_del(da->timer);
   if (da->def.action) eina_stringshare_del(da->def.action);
   if (da->def.params) eina_stringshare_del(da->def.params);
   if (da->delayed.action) eina_stringshare_del(da->delayed.action);
   if (da->delayed.params) eina_stringshare_del(da->delayed.params);
   free(da);
}

static void
_delayed_action_do(Delayed_Action *da)
{
   E_Action *act;

   act = e_action_find(da->def.action);
   if (act)
     {
        if (act->func.go) act->func.go(da->obj, da->def.params);
     }
}

static void
_delayed_action_key_add(E_Object *obj, const char *params EINA_UNUSED, Ecore_Event_Key *ev)
{
   Delayed_Action *da;

   da = E_NEW(Delayed_Action, 1);
   if (!da) return;
   if (obj)
     {
        da->obj = obj;
        e_object_ref(da->obj);
     }
   da->mouse = 0;
   da->key = eina_stringshare_add(ev->key);
   _delayed_actions = eina_list_append(_delayed_actions, da);
}

static void
_delayed_action_key_del(E_Object *obj, const char *params EINA_UNUSED, Ecore_Event_Key *ev)
{
   Eina_List *l;
   Delayed_Action *da;

   EINA_LIST_FOREACH(_delayed_actions, l, da)
     {
        if ((da->obj == obj) && (!da->mouse) &&
            (!strcmp(da->key, ev->key)))
          {
             _delayed_action_do(da);
             _delayed_action_free(da);
             _delayed_actions = eina_list_remove_list(_delayed_actions, l);
             return;
          }
     }
}

static void
_delayed_action_mouse_add(E_Object *obj, const char *params EINA_UNUSED, E_Binding_Event_Mouse_Button *ev)
{
   Delayed_Action *da;

   da = E_NEW(Delayed_Action, 1);
   if (!da) return;
   if (obj)
     {
        da->obj = obj;
        e_object_ref(da->obj);
     }
   da->mouse = 1;
   da->button = ev->button;
   _delayed_actions = eina_list_append(_delayed_actions, da);
}

static void
_delayed_action_mouse_del(E_Object *obj, const char *params EINA_UNUSED, E_Binding_Event_Mouse_Button *ev)
{
   Eina_List *l;
   Delayed_Action *da;

   EINA_LIST_FOREACH(_delayed_actions, l, da)
     {
        if ((da->obj == obj) && (da->mouse) &&
            ((int)ev->button == da->button))
          {
             _delayed_action_do(da);
             _delayed_action_free(da);
             _delayed_actions = eina_list_remove_list(_delayed_actions, l);
             return;
          }
     }
}

// obj , params  , ev
ACT_FN_GO_KEY(delayed_action, , )
{
   _delayed_action_key_add(obj, params, ev);
}

ACT_FN_GO_MOUSE(delayed_action, )
{
   _delayed_action_mouse_add(obj, params, ev);
}

ACT_FN_END_KEY(delayed_action, )
{
   _delayed_action_key_del(obj, params, ev);
}

ACT_FN_END_MOUSE(delayed_action, )
{
   _delayed_action_mouse_del(obj, params, ev);
}

ACT_FN_GO(module_enable, )
{
   E_Module *m;

   if (!params) return;
   m = e_module_find(params);
   if (!m)
     {
        m = e_module_new(params);
        if (!m) return;
     }
   e_module_enable(m);
}

ACT_FN_GO(module_disable, )
{
   E_Module *m;

   if (!params) return;
   m = e_module_find(params);
   if (!m) return;
   e_module_disable(m);
}

ACT_FN_GO(module_toggle, )
{
   E_Module *m;

   fprintf(stderr, "toggle\n");
   if (!params) return;
   fprintf(stderr, "'%s'\n", params);
   m = e_module_find(params);
   fprintf(stderr, "m = %p\n", m);
   if (!m)
     {
        m = e_module_new(params);
        if (!m) return;
     }
   fprintf(stderr, "currently %i\n", e_module_enabled_get(m));
   if (e_module_enabled_get(m)) e_module_disable(m);
   else e_module_enable(m);
}

/* local subsystem globals */
static Eina_Hash *actions = NULL;
static Eina_List *action_list = NULL;
static Eina_List *action_names = NULL;
static Eina_List *action_groups = NULL;

/* externally accessible functions */

EINTERN int
e_actions_init(void)
{
   E_Action *act;

   actions = eina_hash_string_superfast_new(NULL);
   ACT_GO(window_move);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move"),
                            "window_move", NULL, NULL, 0);

   ACT_GO_MOUSE(window_move);
   ACT_GO_SIGNAL(window_move);
   ACT_END(window_move);
   ACT_END_MOUSE(window_move);
   ACT_GO_KEY(window_move);

   /* window_resize */
   ACT_GO(window_resize);
   e_action_predef_name_set(N_("Window : Actions"), N_("Resize"),
                            "window_resize", NULL, NULL, 0);

   ACT_GO_MOUSE(window_resize);
   ACT_GO_SIGNAL(window_resize);
   ACT_END(window_resize);
   ACT_END_MOUSE(window_resize);
   ACT_GO_KEY(window_resize);

   /* window_menu */
   ACT_GO(window_menu);
   e_action_predef_name_set(N_("Menu"), N_("Window Menu"),
                            "window_menu", NULL, NULL, 0);

   ACT_GO_MOUSE(window_menu);
   ACT_GO_KEY(window_menu);

   /* window_raise */
   ACT_GO(window_raise);
   e_action_predef_name_set(N_("Window : Actions"), N_("Raise"),
                            "window_raise", NULL, NULL, 0);

   /* window_lower */
   ACT_GO(window_lower);
   e_action_predef_name_set(N_("Window : Actions"), N_("Lower"),
                            "window_lower", NULL, NULL, 0);

   /* window_close */
   ACT_GO(window_close);
   e_action_predef_name_set(N_("Window : Actions"), N_("Close"),
                            "window_close", NULL, NULL, 0);

   /* window_kill */
   ACT_GO(window_kill);
   e_action_predef_name_set(N_("Window : Actions"), N_("Kill"),
                            "window_kill", NULL, NULL, 0);

   /* window_sticky_toggle */
   ACT_GO(window_sticky_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Sticky Mode Toggle"),
                            "window_sticky_toggle", NULL, NULL, 0);

   ACT_GO(window_sticky);
   e_action_predef_name_set(N_("Window : State"), N_("Sticky Mode Enable"),
                            "window_sticky", NULL, NULL, 0);

   /* window_iconic_toggle */
   ACT_GO(window_iconic_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Iconic Mode Toggle"),
                            "window_iconic_toggle", NULL, NULL, 0);

   ACT_GO(window_iconic);
   e_action_predef_name_set(N_("Window : State"), N_("Iconic Mode Enable"),
                            "window_iconic", NULL, NULL, 0);

   /* window_fullscreen_toggle */
   ACT_GO(window_fullscreen_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Fullscreen Mode Toggle"),
                            "window_fullscreen_toggle", NULL, NULL, 0);

   ACT_GO(window_fullscreen);
   e_action_predef_name_set(N_("Window : State"), N_("Fullscreen Mode Enable"),
                            "window_fullscreen", NULL, NULL, 0);

   /* window_maximized_toggle */
   ACT_GO(window_maximized_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize"),
                            "window_maximized_toggle", NULL, NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Vertically"),
                            "window_maximized_toggle", "default vertical",
                            NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Horizontally"),
                            "window_maximized_toggle", "default horizontal",
                            NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Left"),
                            "window_maximized_toggle", "default left",
                            NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Right"),
                            "window_maximized_toggle", "default right",
                            NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Fullscreen"),
                            "window_maximized_toggle", "fullscreen", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Mode \"Smart\""),
                            "window_maximized_toggle", "smart", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Mode \"Expand\""),
                            "window_maximized_toggle", "expand", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Maximize Mode \"Fill\""),
                            "window_maximized_toggle", "fill", NULL, 0);

   ACT_GO(window_maximized);

   /* window_shaded_toggle */
   ACT_GO(window_shaded_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Shade Up Mode Toggle"),
                            "window_shaded_toggle", "up", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Shade Down Mode Toggle"),
                            "window_shaded_toggle", "down", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Shade Left Mode Toggle"),
                            "window_shaded_toggle", "left", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Shade Right Mode Toggle"),
                            "window_shaded_toggle", "right", NULL, 0);
   e_action_predef_name_set(N_("Window : State"), N_("Shade Mode Toggle"),
                            "window_shaded_toggle", NULL, NULL, 0);

   ACT_GO(window_shaded);
   e_action_predef_name_set(N_("Window : State"), N_("Set Shaded State"),
                            "window_shaded", NULL, _("syntax: \"(0|1) (up|down|left|right)\""), 1);

   /* window_borderless_toggle */
   ACT_GO(window_borderless_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Toggle Borderless State"),
                            "window_borderless_toggle", NULL, NULL, 0);

   /* window_border_set */
   ACT_GO(window_border_set);
   e_action_predef_name_set(N_("Window : State"), N_("Set Border"),
                            "window_border_set", NULL,
                            "syntax: BorderName, example: pixel", 1);

   /* window_border_cycle */
   ACT_GO(window_border_cycle);
   e_action_predef_name_set(N_("Window : State"), N_("Cycle between Borders"),
                            "window_border_cycle", NULL,
                            "syntax: BorderNames, example: default pixel", 1);

   /* window_pinned_toggle */
   ACT_GO(window_pinned_toggle);
   e_action_predef_name_set(N_("Window : State"), N_("Toggle Pinned State"),
                            "window_pinned_toggle", NULL, NULL, 0);

   /* desk_flip_by */
   ACT_GO(desk_flip_by);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Left"),
                            "desk_flip_by", "-1 0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Right"),
                            "desk_flip_by", "1 0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Up"),
                            "desk_flip_by", "0 -1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Down"),
                            "desk_flip_by", "0 1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop By..."),
                            "desk_flip_by", NULL,
                            "syntax: X-offset Y-offset, example: -1 0", 1);

   /* desk_flip_prev */
   ACT_GO(desk_flip_prev);
   e_action_predef_name_set(N_("Desktop"), N_("Flip To Previous Desktop"),
                            "desk_flip_prev", NULL, NULL, 0);

   /* desk_deskshow_toggle */
   ACT_GO(desk_deskshow_toggle);
   e_action_predef_name_set(N_("Desktop"), N_("Show The Desktop"),
                            "desk_deskshow_toggle", NULL, NULL, 0);

   /* desk_linear_flip_to */
   ACT_GO(desk_flip_to);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop To..."),
                            "desk_flip_to", NULL,
                            "syntax: X Y, example: 1 2", 1);
   /* desk_linear_flip_to_screen */
   ACT_GO(desk_linear_flip_to_screen);
   e_action_predef_name_set(N_("Desktop"), N_("Switch Desktop To... On Screen..."),
                            "desk_linear_flip_to_screen", NULL,
                            "syntax: D S, example: 0 1", 1);
   
   /* desk_linear_flip_by */
   ACT_GO(desk_linear_flip_by);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Linearly..."),
                            "desk_linear_flip_by",
                            NULL, "syntax: N-offset, example: -2", 1);

   /* desk_linear_flip_to */
   ACT_GO(desk_linear_flip_to);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 0"),
                            "desk_linear_flip_to", "0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 1"),
                            "desk_linear_flip_to", "1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 2"),
                            "desk_linear_flip_to", "2", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 3"),
                            "desk_linear_flip_to", "3", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 4"),
                            "desk_linear_flip_to", "4", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 5"),
                            "desk_linear_flip_to", "5", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 6"),
                            "desk_linear_flip_to", "6", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 7"),
                            "desk_linear_flip_to", "7", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 8"),
                            "desk_linear_flip_to", "8", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 9"),
                            "desk_linear_flip_to", "9", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 10"),
                            "desk_linear_flip_to", "10", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 11"),
                            "desk_linear_flip_to", "11", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop..."),
                            "desk_linear_flip_to", NULL,
                            "syntax: N, example: 1", 1);

   /* desk_flip_by_all */
   ACT_GO(desk_flip_by_all);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Left (All Screens)"),
                            "desk_flip_by_all", "-1 0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Right (All Screens)"),
                            "desk_flip_by_all", "1 0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Up (All Screens)"),
                            "desk_flip_by_all", "0 -1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Down (All Screens)"),
                            "desk_flip_by_all", "0 1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop By... (All Screens)"),
                            "desk_flip_by_all", NULL,
                            "syntax: X-offset Y-offset, example: -1 0", 1);

   /* desk_flip_to_all */
   ACT_GO(desk_flip_to_all);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop To... (All Screens)"),
                            "desk_flip_to_all", NULL,
                            "syntax: X Y, example: 1 2", 1);

   /* desk_linear_flip_by_all */
   ACT_GO(desk_linear_flip_by_all);
   e_action_predef_name_set(N_("Desktop"), N_("Flip Desktop Linearly... (All Screens)"),
                            "desk_linear_flip_by_all",
                            NULL, "syntax: N-offset, example: -2", 1);

   /* desk_linear_flip_to_all */
   ACT_GO(desk_linear_flip_to_all);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 0 (All Screens)"),
                            "desk_linear_flip_to_all", "0", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 1 (All Screens)"),
                            "desk_linear_flip_to_all", "1", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 2 (All Screens)"),
                            "desk_linear_flip_to_all", "2", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 3 (All Screens)"),
                            "desk_linear_flip_to_all", "3", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 4 (All Screens)"),
                            "desk_linear_flip_to_all", "4", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 5 (All Screens)"),
                            "desk_linear_flip_to_all", "5", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 6 (All Screens)"),
                            "desk_linear_flip_to_all", "6", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 7 (All Screens)"),
                            "desk_linear_flip_to_all", "7", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 8 (All Screens)"),
                            "desk_linear_flip_to_all", "8", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 9 (All Screens)"),
                            "desk_linear_flip_to_all", "9", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 10 (All Screens)"),
                            "desk_linear_flip_to_all", "10", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop 11 (All Screens)"),
                            "desk_linear_flip_to_all", "11", NULL, 0);
   e_action_predef_name_set(N_("Desktop"), N_("Switch To Desktop... (All Screens)"),
                            "desk_linear_flip_to_all", NULL,
                            "syntax: N, example: 1", 1);

   /* window_jump_to */
   ACT_GO(window_jump_to);
   e_action_predef_name_set(N_("Window : List"), N_("Jump to window..."),
                            "window_jump_to", NULL, "syntax: icccm window name, example: urxvt-mutt", 1);

   /* window_move_to_center */
   ACT_GO(window_move_to_center);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move To Center"),
                            "window_move_to_center", NULL, NULL, 0);

   /* window_quick_tile_upper_left */
   ACT_GO(window_quick_tile_to_quadrant);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move/resize to upper-left quadrant"),
                            "window_quick_tile_to_quadrant", "upper_left", NULL, 0);

   /* window_quick_tile_upper_right */
   ACT_GO(window_quick_tile_to_quadrant);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move/resize to upper-right quadrant"),
                            "window_quick_tile_to_quadrant", "upper_right", NULL, 0);

   /* window_quick_tile_lower_left */
   ACT_GO(window_quick_tile_to_quadrant);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move/resize to lower-left quadrant"),
                            "window_quick_tile_to_quadrant", "lower_left", NULL, 0);

   /* window_quick_tile_lower_right */
   ACT_GO(window_quick_tile_to_quadrant);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move/resize to lower-right quadrant"),
                            "window_quick_tile_to_quadrant", "lower_right", NULL, 0);

   /* window_move_to */
   ACT_GO(window_move_to);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move To Coordinates..."),
                            "window_move_to", NULL,
                            "syntax: [+,-]X [+,-]Y or * [+,-]Y or [+,-]X *, example: -1 +1", 1);
   /* window_move_by */
   ACT_GO(window_move_by);
   e_action_predef_name_set(N_("Window : Actions"), N_("Move By Coordinate Offset..."),
                            "window_move_by", NULL,
                            "syntax: X-offset Y-offset, example: -1 0", 1);

   /* window_resize_by */
   ACT_GO(window_resize_by);
   e_action_predef_name_set(N_("Window : Actions"), N_("Resize By..."),
                            "window_resize_by", NULL,
                            "syntax: W H, example: 100 150", 1);

   /* window_push */
   ACT_GO(window_push);
   e_action_predef_name_set(N_("Window : Actions"), N_("Push in Direction..."),
                            "window_push", NULL,
                            "syntax: direction, example: up, down, left, right, up-left, up-right, down-left, down-right", 1);

   /* window_drag_icon */
   ACT_GO(window_drag_icon);
   e_action_predef_name_set(N_("Window : Actions"), N_("Drag Icon..."),
                            "window_drag_icon", NULL, NULL, 0);

   /* window_desk_move_by */
   ACT_GO(window_desk_move_by);
   e_action_predef_name_set(N_("Window : Moving"), N_("To Next Desktop"),
                            "window_desk_move_by", "1 0", NULL, 0);
   e_action_predef_name_set(N_("Window : Moving"), N_("To Previous Desktop"),
                            "window_desk_move_by", "-1 0", NULL, 0);
   e_action_predef_name_set(N_("Window : Moving"), N_("By Desktop #..."),
                            "window_desk_move_by", NULL,
                            "syntax: X-offset Y-offset, example: -2 2", 1);

   /* window_desk_move_to */
   ACT_GO(window_desk_move_to);
   e_action_predef_name_set(N_("Window : Moving"), N_("To Desktop..."),
                            "window_desk_move_to", NULL,
                            "syntax: X Y, example: 0 1", 1);

   /* window_zone_move_by */
   ACT_GO(window_zone_move_by);
   e_action_predef_name_set(N_("Window : Moving"), N_("To Next Screen"),
                            "window_zone_move_by", "1", NULL, 0);
   e_action_predef_name_set(N_("Window : Moving"), N_("To Previous Screen"),
                            "window_zone_move_by", "-1", NULL, 0);

   /* modules */
   ACT_GO(module_enable);
   e_action_predef_name_set(N_("Enlightenment : Module"),
                            N_("Enable the named module"),
                            "module_enable", NULL, NULL, 1);
   ACT_GO(module_disable);
   e_action_predef_name_set(N_("Enlightenment : Module"),
                            N_("Disable the named module"),
                            "module_disable", NULL, NULL, 1);
   ACT_GO(module_toggle);
   e_action_predef_name_set(N_("Enlightenment : Module"),
                            N_("Toggle the named module"),
                            "module_toggle", NULL, NULL, 1);

   ACT_GO(pointer_resize_push);
   ACT_GO(pointer_resize_pop);

   /* cleanup_windows */
   ACT_GO(cleanup_windows);
   e_action_predef_name_set(N_("Desktop"), N_("Cleanup Windows"),
                            "cleanup_windows", NULL, NULL, 0);

   /* delayed_action */
   ACT_GO_KEY(delayed_action);
   e_action_predef_name_set(N_("Generic : Actions"), N_("Delayed Action"),
                            "delayed_action", NULL, "[0.0 exec xterm] [0.3 exec xev]", 1);
   ACT_GO_MOUSE(delayed_action);
   ACT_END_KEY(delayed_action);
   ACT_END_MOUSE(delayed_action);

   return 1;
}

EINTERN int
e_actions_shutdown(void)
{
   Eina_List *tlist = NULL, *l;
   E_Action *act;

   e_action_predef_name_all_del();

   EINA_LIST_FOREACH(action_list, l, act)
     tlist = eina_list_append(tlist, act);
   EINA_LIST_FREE(tlist, act)
     e_object_del(E_OBJECT(act));

   action_names = eina_list_free(action_names);
   eina_hash_free(actions);
   actions = NULL;

   return 1;
}

E_API Eina_List *
e_action_name_list(void)
{
   return action_names;
}

E_API E_Action *
e_action_add(const char *name)
{
   E_Action *act;

   act = e_action_find(name);
   if (!act)
     {
        act = E_OBJECT_ALLOC(E_Action, E_ACTION_TYPE, _e_action_free);
        if (!act) return NULL;
        act->name = name;
        eina_hash_direct_add(actions, act->name, act);
        action_names = eina_list_append(action_names, name);
        action_list = eina_list_append(action_list, act);
     }
   return act;
}

E_API void
e_action_del(const char *name)
{
   E_Action *act;

   act = eina_hash_find(actions, name);
   if (act) _e_action_free(act);
}

E_API E_Action *
e_action_find(const char *name)
{
   E_Action *act;

   act = eina_hash_find(actions, name);
   return act;
}

E_API const char *
e_action_predef_label_get(const char *action, const char *params)
{
   E_Action_Group *actg = NULL;
   E_Action_Description *actd = NULL;
   Eina_List *l, *l2;

   EINA_LIST_FOREACH(action_groups, l, actg)
     {
        EINA_LIST_FOREACH(actg->acts, l2, actd)
          {
             if (!strcmp(actd->act_cmd, action))
               {
                  if ((params) && (actd->act_params))
                    {
                       if (!strcmp(params, actd->act_params))
                         return actd->act_name;
                    }
                  else return actd->act_name;
               }
          }
     }
   if (params) return e_action_predef_label_get(action, NULL);
   return NULL;
}

E_API void
e_action_predef_name_set(const char *act_grp, const char *act_name, const char *act_cmd, const char *act_params, const char *param_example, int editable)
{
   E_Action_Group *actg = NULL;
   E_Action_Description *actd = NULL;
   Eina_List *l;

   if ((!act_grp) || (!act_name)) return;

   EINA_LIST_FOREACH(action_groups, l, actg)
     {
        if (!strcmp(actg->act_grp, act_grp)) break;
        actg = NULL;
     }

   if (!actg)
     {
        actg = E_NEW(E_Action_Group, 1);
        if (!actg) return;

        actg->act_grp = eina_stringshare_add(act_grp);
        action_groups = eina_list_append(action_groups, actg);
        action_groups =
          eina_list_sort(action_groups, -1, _action_groups_sort_cb);
     }

   EINA_LIST_FOREACH(actg->acts, l, actd)
     {
        if (!strcmp(actd->act_name, act_name)) break;
        actd = NULL;
     }

   if (actd) return;

   actd = E_NEW(E_Action_Description, 1);
   if (!actd) return;

   actd->act_name = eina_stringshare_add(act_name);
   actd->act_cmd = !act_cmd ? NULL : eina_stringshare_add(act_cmd);
   actd->act_params = !act_params ? NULL : eina_stringshare_add(act_params);
   actd->param_example = !param_example ? NULL : eina_stringshare_add(param_example);
   actd->editable = editable;

   actg->acts = eina_list_append(actg->acts, actd);
}

E_API void
e_action_predef_name_del(const char *act_grp, const char *act_name)
{
   E_Action_Group *actg = NULL;
   E_Action_Description *actd = NULL;
   Eina_List *l;

   EINA_LIST_FOREACH(action_groups, l, actg)
     {
        if (!strcmp(actg->act_grp, act_grp)) break;
        actg = NULL;
     }

   if (!actg) return;

   EINA_LIST_FOREACH(actg->acts, l, actd)
     {
        if ((actd->act_name) && (!strcmp(actd->act_name, act_name)))
          {
             actg->acts = eina_list_remove(actg->acts, actd);

             if (actd->act_name) eina_stringshare_del(actd->act_name);
             if (actd->act_cmd) eina_stringshare_del(actd->act_cmd);
             if (actd->act_params) eina_stringshare_del(actd->act_params);
             if (actd->param_example) eina_stringshare_del(actd->param_example);

             E_FREE(actd);

             if (!eina_list_count(actg->acts))
               {
                  action_groups = eina_list_remove(action_groups, actg);
                  if (actg->act_grp) eina_stringshare_del(actg->act_grp);
                  E_FREE(actg);
               }
             break;
          }
     }
}

E_API void
e_action_predef_name_all_del(void)
{
   E_Action_Group *actg = NULL;
   E_Action_Description *actd = NULL;

   EINA_LIST_FREE(action_groups, actg)
     {
        EINA_LIST_FREE(actg->acts, actd)
          {
             if (actd->act_name) eina_stringshare_del(actd->act_name);
             if (actd->act_cmd) eina_stringshare_del(actd->act_cmd);
             if (actd->act_params) eina_stringshare_del(actd->act_params);
             if (actd->param_example) eina_stringshare_del(actd->param_example);

             E_FREE(actd);
          }
        if (actg->act_grp) eina_stringshare_del(actg->act_grp);
        E_FREE(actg);
     }
   action_groups = NULL;
}

E_API Eina_List *
e_action_groups_get(void)
{
   return action_groups;
}

/* local subsystem functions */

static void
_e_action_free(E_Action *act)
{
   eina_hash_del(actions, act->name, act);
   action_names = eina_list_remove(action_names, act->name);
   action_list = eina_list_remove(action_list, act);
   free(act);
}

static E_Maximize
_e_actions_maximize_parse(const char *params)
{
   E_Maximize max = 0;
   int ret;
   char s1[32], s2[32];

   if (!params) return e_config->maximize_policy;
   ret = sscanf(params, "%20s %20s", s1, s2);
   if (ret == 2)
     {
        if (!strcmp(s2, "horizontal"))
          max = E_MAXIMIZE_HORIZONTAL;
        else if (!strcmp(s2, "vertical"))
          max = E_MAXIMIZE_VERTICAL;
        else if (!strcmp(s2, "left"))
          max = E_MAXIMIZE_LEFT;
        else if (!strcmp(s2, "right"))
          max = E_MAXIMIZE_RIGHT;
        else
          max = E_MAXIMIZE_BOTH;
     }
   if (ret >= 1)
     {
        if (!strcmp(s1, "fullscreen"))
          max |= E_MAXIMIZE_FULLSCREEN;
        else if (!strcmp(s1, "smart"))
          max |= E_MAXIMIZE_SMART;
        else if (!strcmp(s1, "expand"))
          max |= E_MAXIMIZE_EXPAND;
        else if (!strcmp(s1, "fill"))
          max |= E_MAXIMIZE_FILL;
        else
          max |= (e_config->maximize_policy & E_MAXIMIZE_TYPE);
     }
   else
     max = e_config->maximize_policy;
   return max;
}

static int
_action_groups_sort_cb(const void *d1, const void *d2)
{
   const E_Action_Group *g1, *g2;

   if (!(g1 = d1)) return 1;
   if (!(g2 = d2)) return -1;
   return strcmp(g1->act_grp, g2->act_grp);
}
