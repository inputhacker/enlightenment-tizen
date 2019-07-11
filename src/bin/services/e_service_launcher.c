#include "e.h"
#include <tzsh_server.h>
#include "services/e_service_launcher.h"

typedef struct _E_Service_Launcher E_Service_Launcher;

typedef enum
{
   LAUNCHER_STATE_IDLE,
   LAUNCHER_STATE_MONITORING,
   LAUNCHER_STATE_PREPARING,
   LAUNCHER_STATE_LAUNCHING,
   LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER,
   LAUNCHER_STATE_DONE,
   LAUNCHER_STATE_CANCELED,
} Launcher_State;

struct _E_Service_Launcher
{
   struct wl_resource *res;      //tizen_window_transition_launcher resource
   Launcher_State      state;    //current state of launcher

   E_Client           *ec;       //launcher E_Client
   E_Vis_Grab         *vis_grab; //grab of launcher visibility
   uint32_t            serial;   //identifier
   enum tws_service_launcher_direction direction; //direction of transition

   struct
     {
        E_Client   *ec;        //target E_Client
        pid_t       pid;       //pid
        E_Vis_Grab *vis_grab;  //grab of target client's visibility
        Eina_Bool   delay_del; //refered delay_del
     } target; //target window information for transition

   E_Client            *launched_ec;     //E_Client was launched by launcher

   Ecore_Event_Handler *buf_attach;      //event handler for BUFFER_CHANGE
   Eina_List           *hooks_ec;        //hook list for E_CLIENT_HOOK_*
   Eina_List           *hooks_co;        //hook list for E_COMP_OBJECT_INTERCEPT_*
   Eina_List           *hooks_vis;       //hook list for E_POL_VIS_HOOK_TYPE_*
   Eina_List           *handlers;        //ecore event handlers
};
////////////////////////////////////////////////////////////////////
static E_Service_Launcher  *_e_srv_launcher = NULL; //only one instance is allowed
////////////////////////////////////////////////////////////////////
static Eina_List *
_e_srv_launcher_clients_find_by_pid(pid_t pid)
{
   E_Client *ec;
   Eina_List *clients = NULL, *l;

   EINA_LIST_FOREACH(e_comp->clients, l, ec)
     {
        if (e_object_is_del(E_OBJECT(ec))) continue;
        if (ec->netwm.pid != pid) continue;
        clients = eina_list_append(clients, ec);
     }

   return clients;
}

static const char*
_e_srv_launcher_state_to_str(Launcher_State state)
{
   switch (state)
     {
      case LAUNCHER_STATE_IDLE:
         return "IDLE";
      case LAUNCHER_STATE_MONITORING:
         return "PID_MONITORING";
      case LAUNCHER_STATE_PREPARING:
         return "PREPARING";
      case LAUNCHER_STATE_LAUNCHING:
         return "LAUNCHING";
      case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:
         return "LAUNCHING_BUT_WATING_BUFFER";
      case LAUNCHER_STATE_DONE:
         return "LAUNCH_DONE";
      case LAUNCHER_STATE_CANCELED:
         return "LAUNCH_CANCELED";
     }
   return "UNKNOWN";
}

static void
_e_srv_launcher_state_set(E_Service_Launcher *lc,
                          Launcher_State state)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   if (state == lc->state) return;

   ELOGF("LAUNCHER_SRV", "Set state  %s --> %s",
         lc->ec,
         _e_srv_launcher_state_to_str(lc->state),
         _e_srv_launcher_state_to_str(state));

   lc->state = state;
}

static void
_e_srv_launcher_stop_send(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "Send STOP event(%d) target(ec:%p)",
         lc->ec, lc->serial, lc->target.ec);

   tws_service_launcher_send_stop(lc->res, lc->serial);
}

static Eina_Bool
_e_srv_launcher_prepare_send(E_Service_Launcher *lc,
                             E_Client *target_ec,
                             int x, int y)
{
   uint32_t res_id = 0;

   E_Comp_Object_Content_Type content_type = 0;
   enum tws_service_launcher_target_type target_type = 0;
   const char *target_path = NULL;
   Evas_Object *content = NULL;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_ec, EINA_FALSE);

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     {
        content_type = e_comp_object_content_type_get(target_ec->frame);
        switch (content_type)
          {
           case E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE:
              content = e_comp_object_content_get(target_ec->frame);
              EINA_SAFETY_ON_NULL_RETURN_VAL(content, EINA_FALSE);

              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_IMAGE;
              evas_object_image_file_get(content, &target_path, NULL);
              EINA_SAFETY_ON_NULL_RETURN_VAL(target_path, EINA_FALSE);

              break;
           case E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE:
              content = e_comp_object_content_get(target_ec->frame);
              EINA_SAFETY_ON_NULL_RETURN_VAL(content, EINA_FALSE);

              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_EDJE;
              edje_object_file_get(content, &target_path, NULL);
              EINA_SAFETY_ON_NULL_RETURN_VAL(target_path, EINA_FALSE);

              break;
           default:
              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_REMOTE_SURFACE;
              res_id = e_pixmap_res_id_get(target_ec->pixmap);
          }
     }
   else
     {
        target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_REMOTE_SURFACE;
        res_id = e_pixmap_res_id_get(target_ec->pixmap);
     }


   ELOGF("LAUNCHER_SRV", "Send PREPARE event(%d) direction:%s target(ec:%p type:%d res:%d path:%s pos(%d,%d))",
         lc->ec, lc->serial, lc->direction?"BACKWARD":"FORWARD", target_ec, target_type, res_id, target_path?:"N/A", x, y);

   tws_service_launcher_send_prepare(lc->res,
                                     target_type,
                                     res_id, target_path,
                                     lc->direction,
                                     x, y, lc->serial);

   return EINA_TRUE;
}

static Eina_Bool
_e_srv_launcher_prepare_forward_send(E_Service_Launcher *lc,
                                     E_Client *target_ec)
{
   Eina_Bool sent = EINA_FALSE;
   int x, y;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_ec, EINA_FALSE);

   if(e_object_is_del(E_OBJECT(target_ec))) return EINA_FALSE;

   e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 1);
   e_comp_client_override_add(target_ec);

   //grab uniconify job of target_ec
   if (target_ec->iconic)
     lc->target.vis_grab = e_policy_visibility_client_filtered_grab_get(target_ec,
                                                                        (E_VIS_JOB_TYPE_UNICONIFY |
                                                                         E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY),
                                                                        __func__);

   lc->launched_ec = NULL;
   lc->target.ec = target_ec;
   lc->direction = TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD;
   lc->serial = wl_display_next_serial(e_comp_wl->wl.disp);

   e_client_pos_get(target_ec, &x, &y);

   sent = _e_srv_launcher_prepare_send(lc, target_ec, x, y);

   if (!sent)
     {
        ELOGF("LAUNCHER_SRV", "Failed to send event(PREPARE:FORWARD)", lc->ec);

        lc->launched_ec = NULL;
        lc->target.ec = NULL;
        lc->direction = 0;
        lc->serial = 0;

        if (lc->target.vis_grab)
          e_policy_visibility_client_grab_release(lc->target.vis_grab);
        lc->target.vis_grab = NULL;

        e_comp_client_override_del(target_ec);
        e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);
     }

   return sent;
}

static Eina_Bool
_e_srv_launcher_prepare_backward_send(E_Service_Launcher *lc,
                                      E_Client *activity,
                                      E_Client *target_ec,
                                      E_Vis_Job_Type job_type)
{
   int x, y;
   Eina_Bool sent = EINA_FALSE;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_ec, EINA_FALSE);

   if (e_object_is_del(E_OBJECT(target_ec)))
     {
        //can't do nothing if ec is deleted and there's no delay_del_ref as well.
        if (!e_object_delay_del_ref_get(E_OBJECT(target_ec)))
          return EINA_FALSE;
     }

   e_object_delay_del_ref(E_OBJECT(target_ec));
   lc->target.delay_del = EINA_TRUE;

   e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 1);
   e_comp_client_override_add(target_ec);

   if (activity == target_ec)
     {
        lc->vis_grab = e_policy_visibility_client_filtered_grab_get(lc->ec, job_type, __func__);
        lc->target.vis_grab = e_policy_visibility_client_filtered_grab_get(target_ec, E_VIS_JOB_TYPE_ALL, __func__);
     }
   else
     {
        lc->target.vis_grab = e_policy_visibility_client_filtered_grab_get(target_ec, job_type, __func__);
     }

   lc->launched_ec = NULL;
   lc->target.ec = target_ec;
   lc->serial = wl_display_next_serial(e_comp_wl->wl.disp);
   lc->direction = TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD;
   e_client_pos_get(target_ec, &x, &y);

   sent = _e_srv_launcher_prepare_send(lc, target_ec, x, y);

   if (!sent)
     {
        ELOGF("LAUNCHER_SRV", "Failed to send event(PREPARE:BACKWARD)", lc->ec);

        lc->launched_ec = NULL;
        lc->target.ec = NULL;
        lc->serial = 0;
        lc->direction = 0;

        if (lc->vis_grab)
          e_policy_visibility_client_grab_release(lc->vis_grab);
        if (lc->target.vis_grab)
          e_policy_visibility_client_grab_release(lc->target.vis_grab);

        e_comp_client_override_del(target_ec);
        e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);

        e_object_delay_del_unref(E_OBJECT(target_ec));
        lc->target.delay_del = EINA_FALSE;
     }

   return sent;
}

static void
_e_srv_launcher_post_forward(E_Service_Launcher *lc,
                             Eina_Bool success)
{
   E_Client *target_ec = NULL;

   if ((lc->target.ec) && (!e_object_is_del(E_OBJECT(lc->target.ec))))
     target_ec = lc->target.ec;

   lc->serial = 0;
   lc->target.ec = NULL;
   lc->target.pid = -1;

   //if forward animation is failed, enlightenment can run animation instead.
   if ((!success) && (target_ec))
     e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);

   if (lc->vis_grab)
     e_policy_visibility_client_grab_release(lc->vis_grab);
   if (lc->target.vis_grab)
     e_policy_visibility_client_grab_release(lc->target.vis_grab);

   lc->vis_grab = NULL;
   lc->target.vis_grab = NULL;

   if (!target_ec) return;

   if (success)
     lc->launched_ec = target_ec;

   //show target_ec
   e_comp_object_damage(target_ec->frame, 0, 0, target_ec->w, target_ec->h);
   e_comp_object_dirty(target_ec->frame);
   e_comp_object_render(target_ec->frame);
   evas_object_show(target_ec->frame);

   e_comp_client_override_del(target_ec);
}

static void
_e_srv_launcher_post_backward(E_Service_Launcher *lc,
                              Eina_Bool success)
{
   E_Client *target_ec = NULL;

   target_ec = lc->target.ec;

   lc->serial = 0;
   lc->target.ec = NULL;
   lc->target.pid = -1;

   E_FREE_FUNC(lc->buf_attach, ecore_event_handler_del);

   if (target_ec)
     {
        Eina_Bool is_del;
        is_del = e_object_is_del(E_OBJECT(target_ec));
        if (lc->target.delay_del)
          e_object_delay_del_unref(E_OBJECT(target_ec));

        if (is_del)
          target_ec = NULL;
     }
   lc->target.delay_del = EINA_FALSE;

   //if forward animation is failed, enlightenment can run animation instead.
   if ((!success) && (target_ec))
     e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);

   if (lc->vis_grab)
     e_policy_visibility_client_grab_release(lc->vis_grab);
   if (lc->target.vis_grab)
     e_policy_visibility_client_grab_release(lc->target.vis_grab);

   lc->vis_grab = NULL;
   lc->target.vis_grab = NULL;

   if (!target_ec) return;

   if (success)
     e_policy_animatable_lock(target_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);

   e_comp_client_override_del(target_ec);
}

/* Reset lc data except for reusable hooks and handlers. */
static void
_e_srv_launcher_data_reset(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "Reset Launcher Data", lc->ec);

   //clear resource and send 'DISQUALIFIED' msg
   if (lc->res)
     {
        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_DISQUALIFIED);
        wl_resource_set_user_data(lc->res, NULL);
        lc->res = NULL;
     }

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _e_srv_launcher_post_forward(lc, EINA_FALSE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _e_srv_launcher_post_backward(lc, EINA_FALSE);

   _e_srv_launcher_state_set(lc, LAUNCHER_STATE_IDLE);
   lc->direction = 0;
   lc->launched_ec = NULL;
}

static void
_e_srv_launcher_data_free(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   E_FREE_FUNC(lc->buf_attach, ecore_event_handler_del);
   E_FREE_LIST(lc->handlers,   ecore_event_handler_del);
   E_FREE_LIST(lc->hooks_ec,   e_client_hook_del);
   E_FREE_LIST(lc->hooks_co,   e_comp_object_intercept_hook_del);
   E_FREE_LIST(lc->hooks_vis,  e_policy_visibility_hook_del);

   _e_srv_launcher_data_reset(lc);

   E_FREE(lc);
}

static Eina_Bool
_e_srv_launcher_cb_hook_intercept_show_helper(void *data, E_Client *ec)
{
   E_Service_Launcher *lc;
   Eina_Bool sent = EINA_FALSE;

   lc = (E_Service_Launcher*)data;

   EINA_SAFETY_ON_NULL_GOTO(lc, show_allow);
   EINA_SAFETY_ON_NULL_GOTO(ec, show_allow);

   if (ec->new_client) goto show_allow;

   switch (lc->state)
     {
      case LAUNCHER_STATE_IDLE:
      case LAUNCHER_STATE_DONE:
      case LAUNCHER_STATE_CANCELED:               //animation ended or didn't start
         goto show_allow;
      case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:  //waiting buffer change
         goto show_deny;
      case LAUNCHER_STATE_PREPARING:              //waiting launcher client's preparation
         if (ec == lc->target.ec) goto show_deny;
         break;
      case LAUNCHER_STATE_LAUNCHING:              //doing animation
         if (ec == lc->target.ec) goto show_deny; //don't show launched app window
         else if (ec == lc->ec) goto show_allow;  //show launcher
         break;
      case LAUNCHER_STATE_MONITORING:             //waiting creation of target window
         if (lc->target.pid != ec->netwm.pid) goto show_allow;
         if (e_object_is_del(E_OBJECT(ec))) goto show_allow;

         sent = _e_srv_launcher_prepare_forward_send(lc, ec);
         EINA_SAFETY_ON_FALSE_GOTO(sent, send_stop);

         _e_srv_launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
         goto show_deny;
      default:
         goto show_allow;
     }

show_allow:
   return EINA_TRUE;
show_deny:
   return EINA_FALSE;
send_stop:
   lc->target.pid = -1;
   _e_srv_launcher_stop_send(lc);
   _e_srv_launcher_state_set(lc, LAUNCHER_STATE_IDLE);
   return EINA_TRUE;
}

static Eina_Bool
_e_srv_launcher_cb_hook_vis_uniconify_render_running(void *data EINA_UNUSED, E_Client *ec)
{
   E_Service_Launcher *lc;
   E_Client *activity = NULL;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_TRUE);

   activity = e_policy_visibility_main_activity_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_TRUE);

   if (ec == lc->ec)
     {
        ELOGF("LAUNCHER_SRV", "Hook uniconify render begin target.ec:%p activity:%p launched_ec:%p",
              ec, lc->target.ec, activity, lc->launched_ec);

        if (activity == lc->launched_ec)
          {
             int sent = EINA_FALSE;

             ELOGF("LAUNCHER_SRV", "Current activity(%p, is_del:%d) was launched by launcher.",
                   ec, activity, e_object_is_del(E_OBJECT(activity)));

             sent = _e_srv_launcher_prepare_backward_send(lc, activity, activity,
                                                          (E_VIS_JOB_TYPE_UNICONIFY |
                                                           E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY));
             if (!sent) return EINA_FALSE;
             _e_srv_launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_srv_launcher_cb_hook_vis_lower(void *data, E_Client *ec)
{
   E_Service_Launcher *lc;
   E_Client *activity = NULL;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   activity = e_policy_visibility_main_activity_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_FALSE);

   if (activity != lc->ec) return EINA_FALSE;
   if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) return EINA_FALSE;

   if (ec == lc->launched_ec)
     {
        Eina_Bool sent = EINA_FALSE;
        ELOGF("LAUNCHER_SRV", "Lower hook of launched_ec(%p)", lc->ec, ec);

        sent = _e_srv_launcher_prepare_backward_send(lc, activity, ec, E_VIS_JOB_TYPE_LOWER);
        if (!sent) return EINA_FALSE;

        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_srv_launcher_cb_hook_vis_hide(void *data, E_Client *ec)
{
   E_Service_Launcher *lc;
   E_Client *activity = NULL;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);

   activity = e_policy_visibility_main_activity_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_FALSE);

   if (activity != lc->ec) return EINA_FALSE;
   if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) return EINA_FALSE;

   if (ec == lc->launched_ec)
     {
        Eina_Bool sent = EINA_FALSE;
        ELOGF("LAUNCHER_SRV", "Hide hook of launched_ec(%p)", lc->ec, ec);

        sent = _e_srv_launcher_prepare_backward_send(lc, activity, ec, E_VIS_JOB_TYPE_HIDE);
        if (!sent) return EINA_FALSE;

        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
     }

   return EINA_TRUE;
}


static Eina_Bool
_e_srv_launcher_cb_event_buff_attach(void *data, int type EINA_UNUSED, void *event)
{
   E_Service_Launcher *lc;
   E_Client *ec;
   E_Event_Client *ev;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, ECORE_CALLBACK_PASS_ON);

   ev = (E_Event_Client *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if (ec != lc->ec) return ECORE_CALLBACK_PASS_ON;

   ELOGF("LAUNCHER_SRV", "Event cb(BUFFER_CHANGE)", ec);

   if (lc->state == LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER)
     _e_srv_launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING);

   if (lc->vis_grab)
     {
        e_policy_visibility_client_grab_release(lc->vis_grab);
        lc->vis_grab = NULL;
     }

   if (lc->target.vis_grab)
     {
        e_policy_visibility_client_grab_release(lc->target.vis_grab);
        lc->target.vis_grab = NULL;
     }

   E_FREE_FUNC(lc->buf_attach, ecore_event_handler_del);

   return ECORE_CALLBACK_PASS_ON;
}

static Eina_Bool
_e_srv_launcher_cb_event_client_show(void *data, int type EINA_UNUSED, void *event)
{
   E_Service_Launcher *lc;
   E_Client *ec;
   E_Event_Client *ev;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, ECORE_CALLBACK_PASS_ON);

   if ((lc->state == LAUNCHER_STATE_IDLE) ||
       (lc->state == LAUNCHER_STATE_MONITORING) ||
       (lc->state == LAUNCHER_STATE_LAUNCHING) ||
       (lc->state == LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER))
     return ECORE_CALLBACK_PASS_ON;

   ev = (E_Event_Client *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;
   if ((ec != lc->launched_ec) && (ec != lc->ec)) return ECORE_CALLBACK_PASS_ON;

   ELOGF("LAUNCHER_SRV", "Event cb(CLIENT_SHOW)", ec);

   if (ec == lc->launched_ec)
     e_policy_animatable_lock(ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);

   //failure case
   if (lc->state == LAUNCHER_STATE_PREPARING)
     {
        _e_srv_launcher_stop_send(lc);
        if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
          _e_srv_launcher_post_backward(lc, EINA_FALSE);
     }

   _e_srv_launcher_state_set(lc, LAUNCHER_STATE_IDLE);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_srv_launcher_cb_hook_client_del(void *data, E_Client *ec)
{
   E_Service_Launcher *lc;

   lc = (E_Service_Launcher*)data;
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_NULL_RETURN(ec);

   if (lc->launched_ec == ec)
     lc->launched_ec = NULL;
   else if (lc->ec == ec) //launcher surface is gone.
     {
        if (_e_srv_launcher == lc)
          _e_srv_launcher = NULL;

        _e_srv_launcher_data_free(lc);
     }
   else if (lc->target.ec == ec) //target surface is gone.
     {
        switch (lc->state)
          {
           case LAUNCHER_STATE_PREPARING:
           case LAUNCHER_STATE_LAUNCHING:
           case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:
              _e_srv_launcher_stop_send(lc);
              if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
                _e_srv_launcher_post_forward(lc, EINA_FALSE);
              else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
                _e_srv_launcher_post_backward(lc, EINA_FALSE);
              break;
           default:
              break;
          }
     }
}

static void
_e_srv_launcher_cb_resource_destroy(struct wl_resource *res_tws_lc)
{
   E_Service_Launcher *lc;

   lc = wl_resource_get_user_data(res_tws_lc);
   if (!lc) return;

   ELOGF("LAUNCHER_SRV", "Start Resource Destroy tws_service_launcher", lc->ec);

   if (_e_srv_launcher == lc)
     _e_srv_launcher = NULL;

   lc->res = NULL;
   _e_srv_launcher_data_free(lc);

   ELOGF("LAUNCHER_SRV", "End Resource Destroy tws_service_launcher", NULL);
}

static void
_e_srv_launcher_cb_destroy(struct wl_client *client EINA_UNUSED,
                           struct wl_resource *res_tws_lc)
{
   ELOGF("LAUNCHER_SRV", "Received request(launcher_destroy)", NULL);
   wl_resource_destroy(res_tws_lc);
}

static void
_e_srv_launcher_cb_launch(struct wl_client *client EINA_UNUSED,
                          struct wl_resource *res_tws_lc,
                          const char *app_id,
                          int32_t pid)
{
   E_Service_Launcher *lc;
   E_Client *target_ec;
   Eina_List *ecs, *l;
   Eina_Bool sent = EINA_FALSE;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_NULL_RETURN(lc->ec);

   ELOGF("LAUNCHER_SRV",
         "Recieved request(launcher_launch) appid:%s pid:%d",
         lc->ec, app_id?:"NONE", pid);

   EINA_SAFETY_ON_TRUE_GOTO(lc->ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED, send_stop);
   EINA_SAFETY_ON_TRUE_GOTO(pid < 0, send_stop);

   lc->target.pid = pid;

   //if we received launch without idle state
   if ((lc->state == LAUNCHER_STATE_DONE) ||
       (lc->state == LAUNCHER_STATE_CANCELED))
     {
        if (lc->launched_ec)
          {
             e_policy_animatable_lock(lc->launched_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);
             lc->launched_ec = NULL;
          }

        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_IDLE);
     }

   ecs = _e_srv_launcher_clients_find_by_pid(pid);
   EINA_LIST_FOREACH(ecs, l, target_ec)
     {
        if (e_object_is_del(E_OBJECT(target_ec))) continue;
        if (e_client_util_ignored_get(target_ec)) continue;

        ELOGF("LAUNCHER_SRV", "Found target_ec:%p", lc->ec, target_ec);

        sent = _e_srv_launcher_prepare_forward_send(lc, target_ec);
        EINA_SAFETY_ON_FALSE_GOTO(sent, send_stop);

        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
        break;
     }
   eina_list_free(ecs);

   if (!lc->target.ec)
     {
        ELOGF("LAUNCHER_SRV", "Can't find target_ec, Start Monitoring", lc->ec);
        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_MONITORING);
     }

   return;

send_stop:
   ELOGF("LAUNCHER_SRV", "can't process request(launcher_launch)", lc->ec);
   _e_srv_launcher_stop_send(lc);
}

static void
_e_srv_launcher_cb_launching(struct wl_client *client EINA_UNUSED,
                             struct wl_resource *res_tws_lc,
                             uint32_t serial)
{
   E_Service_Launcher *lc;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCHING(%d) %s",
         lc->ec, serial, lc->direction?"backward":"forward");

   _e_srv_launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING);

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     {
        _e_srv_launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER);
        lc->buf_attach = ecore_event_handler_add(E_EVENT_CLIENT_BUFFER_CHANGE,
                                                 _e_srv_launcher_cb_event_buff_attach, lc);
     }
}

static void
_e_srv_launcher_cb_launch_done(struct wl_client *client EINA_UNUSED,
                               struct wl_resource *res_tws_lc,
                               uint32_t serial)
{
   E_Service_Launcher *lc;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCH_DONE(%d) target:%p", lc->ec, serial, lc->target.ec);

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _e_srv_launcher_post_forward(lc, EINA_TRUE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _e_srv_launcher_post_backward(lc, EINA_TRUE);

   if ((lc->state == LAUNCHER_STATE_LAUNCHING) ||
       (lc->state == LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER))
     _e_srv_launcher_state_set(lc, LAUNCHER_STATE_DONE);
}

static void
_e_srv_launcher_cb_launch_cancel(struct wl_client *client EINA_UNUSED,
                                 struct wl_resource *res_tws_lc,
                                 uint32_t serial)
{
   E_Service_Launcher *lc;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCH_CANCEL(%d) target_ec:%p",
         lc->ec, serial, lc->target.ec);

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _e_srv_launcher_post_forward(lc, EINA_FALSE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _e_srv_launcher_post_backward(lc, EINA_FALSE);

   if ((lc->state == LAUNCHER_STATE_PREPARING) ||
       (lc->state == LAUNCHER_STATE_LAUNCHING) ||
       (lc->state == LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER))
     _e_srv_launcher_state_set(lc, LAUNCHER_STATE_CANCELED);
}

static const struct tws_service_launcher_interface _e_srv_launcher_iface =
{
   _e_srv_launcher_cb_destroy,
   _e_srv_launcher_cb_launch,
   _e_srv_launcher_cb_launching,
   _e_srv_launcher_cb_launch_done,
   _e_srv_launcher_cb_launch_cancel,
};

EINTERN void
e_service_launcher_resource_set(E_Client *ec, struct wl_resource *res_tws_lc)
{
   E_Service_Launcher *lc;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(res_tws_lc);

   lc = _e_srv_launcher;
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_TRUE_RETURN(lc->ec != ec);

   lc->res = res_tws_lc;
   wl_resource_set_implementation(res_tws_lc,
                                  &_e_srv_launcher_iface, lc,
                                  _e_srv_launcher_cb_resource_destroy);
}

#undef  LAUNCHER_CB_ADD
#define LAUNCHER_CB_ADD(l, appender, event_type, cb, data)  \
  do                                                        \
    {                                                       \
       void *_h;                                            \
       _h = appender(event_type, cb, data);                 \
       assert(_h);                                          \
       l = eina_list_append(l, _h);                         \
    }                                                       \
  while (0)

EINTERN void
e_service_launcher_client_set(E_Client *ec)
{
   E_Service_Launcher *lc = NULL;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(ec)));

   if (_e_srv_launcher)
     {
        lc = _e_srv_launcher;
        _e_srv_launcher_data_reset(lc);
     }

   if (!lc)
     {

        lc = E_NEW(E_Service_Launcher, 1);
        EINA_SAFETY_ON_NULL_RETURN(lc);

        /* hook, event handler add */
        LAUNCHER_CB_ADD(lc->hooks_co,  e_comp_object_intercept_hook_add, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER, _e_srv_launcher_cb_hook_intercept_show_helper, lc);
        LAUNCHER_CB_ADD(lc->hooks_vis, e_policy_visibility_hook_add, E_POL_VIS_HOOK_TYPE_UNICONIFY_RENDER_RUNNING, _e_srv_launcher_cb_hook_vis_uniconify_render_running, lc);
        LAUNCHER_CB_ADD(lc->hooks_vis, e_policy_visibility_hook_add, E_POL_VIS_HOOK_TYPE_LOWER, _e_srv_launcher_cb_hook_vis_lower, lc);
        LAUNCHER_CB_ADD(lc->hooks_vis, e_policy_visibility_hook_add, E_POL_VIS_HOOK_TYPE_HIDE, _e_srv_launcher_cb_hook_vis_hide, lc);
        LAUNCHER_CB_ADD(lc->hooks_ec,  e_client_hook_add, E_CLIENT_HOOK_DEL, _e_srv_launcher_cb_hook_client_del, lc);
        LAUNCHER_CB_ADD(lc->handlers,  ecore_event_handler_add, E_EVENT_CLIENT_SHOW, _e_srv_launcher_cb_event_client_show, lc);

        _e_srv_launcher = lc;
     }

   lc->ec = ec;

   ELOGF("LAUNCHER_SRV", "client set|Created New Launcher(%p)", ec, lc);

   return;
}

EINTERN void
e_service_launcher_client_unset(E_Client *ec)
{
   E_Service_Launcher *lc;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(_e_srv_launcher);
   EINA_SAFETY_ON_TRUE_RETURN(_e_srv_launcher->ec != ec);

   lc = _e_srv_launcher;
   _e_srv_launcher_data_reset(lc);

   ELOGF("LAUNCHER_SRV", "client unset", ec);
}
