#include "e.h"
#include <tzsh_server.h>
#include "services/e_service_launcher.h"

typedef struct _E_Service_Launcher         E_Service_Launcher;
typedef struct _E_Service_Launcher_Handler E_Service_Launcher_Handler;

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
   Launcher_State                       state;          //current state of launcher

   struct wl_resource                  *res;            //tizen_window_transition_launcher resource
   E_Client                            *ec;             //launcher E_Client

   E_Vis_Grab                          *vis_grab;       //grab of launcher visibility
   uint32_t                             serial;         //identifier
   enum tws_service_launcher_direction  direction;      //direction of transition

   struct
     {
        E_Client                       *ec;             //target E_Client
        pid_t                           pid;            //pid
        E_Vis_Grab                     *vis_grab;       //grab of target client's visibility

        Eina_Bool                       delay_del;      //refered delay_del
        E_Object_Delfn                 *delfn;          //del callback of target E_Client
     } target; //target window information for transition

   E_Client                            *launched_ec;    //E_Client was launched by launcher
   E_Object_Delfn                      *launched_delfn;  //del callback of launched_ec

   Ecore_Event_Handler                 *buff_attach;    //event handler for BUFFER_CHANGE
};

struct _E_Service_Launcher_Handler
{
   Eina_Hash           *launcher_hash;  //hash key:launcher_ec, data:E_Service_Launcher
   unsigned int         launcher_count; //count of launcher object

   Eina_List           *hooks_ec;       //hook list for E_CLIENT_HOOK_*
   Eina_List           *hooks_vis;      //hook list for E_POL_VIS_HOOK_TYPE_*
   Eina_List           *hooks_co;       //hook list for E_COMP_OBJECT_INTERCEPT_HOOK_*
   Eina_List           *hdlrs_ev;       //handler list for ecore events

   E_Service_Launcher  *runner;         //current runner(running launcher)
   E_Service_Launcher  *pre_runner;     //previous runner
};

////////////////////////////////////////////////////////////////////
static E_Service_Launcher_Handler *_laundler = NULL;

static void                _launcher_launched_ec_set(E_Service_Launcher *lc, E_Client *launched_ec);
static void                _launcher_target_ec_set(E_Service_Launcher *lc, E_Client *target_ec);

static E_Service_Launcher *_launcher_handler_launcher_find(E_Client *ec);
static Eina_Bool           _launcher_handler_launcher_add(E_Service_Launcher *lc);
static Eina_Bool           _launcher_handler_launcher_del(E_Service_Launcher *lc);

static E_Service_Launcher *_launcher_handler_launcher_runner_get(void);
static void                _launcher_handler_launcher_runner_set(E_Service_Launcher *lc);
static void                _launcher_handler_launcher_runner_unset(E_Service_Launcher *lc);
static E_Service_Launcher *_launcher_handler_launcher_pre_runner_get(void);
static void                _launcher_handler_launcher_pre_runner_set(E_Service_Launcher *lc);
static void                _launcher_handler_launcher_pre_runner_unset(E_Service_Launcher *lc);

////////////////////////////////////////////////////////////////////
static Eina_List *
_launcher_clients_find_by_pid(pid_t pid)
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
_launcher_state_to_str(Launcher_State state)
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
_launcher_state_set(E_Service_Launcher *lc,
                          Launcher_State state)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   if (state == lc->state) return;

   ELOGF("LAUNCHER_SRV", "Set state  %s --> %s",
         lc->ec,
         _launcher_state_to_str(lc->state),
         _launcher_state_to_str(state));

   lc->state = state;
}

static void
_launcher_post_forward(E_Service_Launcher *lc, Eina_Bool success)
{
   E_Client *target_ec = NULL;

   if ((lc->target.ec) && (!e_object_is_del(E_OBJECT(lc->target.ec))))
     target_ec = lc->target.ec;

   _launcher_target_ec_set(lc, NULL);

   lc->serial = 0;
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
     _launcher_launched_ec_set(lc, target_ec);

   //show target_ec
   e_comp_object_damage(target_ec->frame, 0, 0, target_ec->w, target_ec->h);
   e_comp_object_dirty(target_ec->frame);
   e_comp_object_render(target_ec->frame);
   evas_object_show(target_ec->frame);

   e_comp_client_override_del(target_ec);
}

static void
_launcher_post_backward(E_Service_Launcher *lc, Eina_Bool success)
{
   E_Client *target_ec = NULL;

   target_ec = lc->target.ec;
   _launcher_target_ec_set(lc, NULL);

   lc->serial = 0;
   lc->target.pid = -1;
   lc->direction = 0;

   E_FREE_FUNC(lc->buff_attach, ecore_event_handler_del);

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

static void
_launcher_stop_send(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "Send STOP event(%d) target(ec:%p)",
         lc->ec, lc->serial, lc->target.ec);

   tws_service_launcher_send_stop(lc->res, lc->serial);
}

static Eina_Bool
_launcher_prepare_send(E_Service_Launcher *lc,
                             E_Client *target_ec,
                             int x, int y)
{
   uint32_t res_id = 0;

   E_Comp_Object_Content_Type content_type = 0;
   enum tws_service_launcher_target_type target_type = 0;
   const char *target_path = NULL, *target_group = NULL;
   Evas_Object *content = NULL;
   struct wl_array info_array;

   int len;
   char *p_char;
   uint32_t *p_u32;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(target_ec, EINA_FALSE);

   wl_array_init(&info_array);
   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     {
        content_type = e_comp_object_content_type_get(target_ec->frame);
        switch (content_type)
          {
           case E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE:
              content = e_comp_object_content_get(target_ec->frame);
              EINA_SAFETY_ON_NULL_GOTO(content, fail);

              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_IMAGE;
              evas_object_image_file_get(content, &target_path, NULL);
              EINA_SAFETY_ON_NULL_GOTO(target_path, fail);

              len = strlen(target_path) + 1;
              p_char = wl_array_add(&info_array, len);
              EINA_SAFETY_ON_NULL_GOTO(p_char, fail);

              strncpy(p_char, target_path, len);
              break;
           case E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE:
              content = e_comp_object_content_get(target_ec->frame);
              EINA_SAFETY_ON_NULL_GOTO(content, fail);

              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_EDJE;
              edje_object_file_get(content, &target_path, &target_group);
              EINA_SAFETY_ON_NULL_GOTO(target_path, fail);
              EINA_SAFETY_ON_NULL_GOTO(target_group, fail);

              len = strlen(target_path) + 1;
              p_char = wl_array_add(&info_array, len);
              EINA_SAFETY_ON_NULL_GOTO(p_char, fail);

              strncpy(p_char, target_path, len);

              len = strlen(target_group) + 1;
              p_char = wl_array_add(&info_array, len);
              EINA_SAFETY_ON_NULL_GOTO(p_char, fail);

              strncpy(p_char, target_group, len);

              break;
           default:
              target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_REMOTE_SURFACE;
              res_id = e_pixmap_res_id_get(target_ec->pixmap);

              p_u32 = wl_array_add(&info_array, sizeof(uint32_t));
              EINA_SAFETY_ON_NULL_GOTO(p_u32, fail);

              *p_u32 = res_id;
          }
     }
   else
     {
        target_type = TWS_SERVICE_LAUNCHER_TARGET_TYPE_REMOTE_SURFACE;
        res_id = e_pixmap_res_id_get(target_ec->pixmap);

        p_u32 = wl_array_add(&info_array, sizeof(uint32_t));
        EINA_SAFETY_ON_NULL_GOTO(p_u32, fail);

        *p_u32 = res_id;
     }


   ELOGF("LAUNCHER_SRV", "Send PREPARE event(%d) direction:%s target(ec:%p type:%d res:%d path:%s pos(%d,%d))",
         lc->ec, lc->serial, lc->direction?"BACKWARD":"FORWARD", target_ec, target_type, res_id, target_path?:"N/A", x, y);

   tws_service_launcher_send_prepare(lc->res,
                                     target_type,
                                     &info_array,
                                     lc->direction,
                                     x, y, lc->serial);

   wl_array_release(&info_array);
   return EINA_TRUE;
fail:
   wl_array_release(&info_array);
   return EINA_FALSE;
}

static Eina_Bool
_launcher_prepare_forward_send(E_Service_Launcher *lc,
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

   _launcher_launched_ec_set(lc, NULL);
   _launcher_target_ec_set(lc, target_ec);

   lc->direction = TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD;
   lc->serial = wl_display_next_serial(e_comp_wl->wl.disp);
   e_client_pos_get(target_ec, &x, &y);

   sent = _launcher_prepare_send(lc, target_ec, x, y);

   //fail to send protocol event
   if (!sent)
     {
        ELOGF("LAUNCHER_SRV", "Failed to send event(PREPARE:FORWARD)", lc->ec);
        _launcher_post_forward(lc, EINA_FALSE);
     }

   return sent;
}

static Eina_Bool
_launcher_prepare_backward_send(E_Service_Launcher *lc,
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

   _launcher_launched_ec_set(lc, NULL);
   _launcher_target_ec_set(lc, target_ec);

   lc->serial = wl_display_next_serial(e_comp_wl->wl.disp);
   lc->direction = TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD;
   e_client_pos_get(target_ec, &x, &y);

   sent = _launcher_prepare_send(lc, target_ec, x, y);

   //fail to send protocol event
   if (!sent)
     {
        ELOGF("LAUNCHER_SRV", "Failed to send event(PREPARE:BACKWARD)", lc->ec);
        _launcher_post_backward(lc, EINA_FALSE);
     }

   return sent;
}

/* Reset lc data */
static void
_launcher_data_reset(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "Reset Launcher Data", lc->ec);

   //clear resource and send 'DISQUALIFIED' msg
   if (lc->res)
     {
        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_DISQUALIFIED, lc->serial);
        wl_resource_set_user_data(lc->res, NULL);
        lc->res = NULL;
     }

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _launcher_post_forward(lc, EINA_FALSE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _launcher_post_backward(lc, EINA_FALSE);

   _launcher_state_set(lc, LAUNCHER_STATE_IDLE);
   _launcher_launched_ec_set(lc, NULL);

   _launcher_handler_launcher_runner_unset(lc);
   _launcher_handler_launcher_pre_runner_unset(lc);

   lc->direction = 0;
}


static Eina_Bool
_launcher_cb_event_buff_attach(void *data, int type EINA_UNUSED, void *event)
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
     _launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING);

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

   E_FREE_FUNC(lc->buff_attach, ecore_event_handler_del);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_launcher_cb_launched_ec_del(void *data, void *obj)
{
   E_Service_Launcher *lc = (E_Service_Launcher *)data;
   E_Client *launched_ec = (E_Client *)obj;

   EINA_SAFETY_ON_NULL_RETURN(launched_ec);
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_TRUE_RETURN(lc->launched_ec != launched_ec);

   lc->launched_ec = NULL;
   lc->launched_delfn = NULL;
}

static void
_launcher_cb_target_ec_del(void *data, void *obj)
{
   E_Service_Launcher *lc = (E_Service_Launcher *)data;
   E_Client *target_ec = (E_Client *)obj;

   EINA_SAFETY_ON_NULL_RETURN(target_ec);
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_TRUE_RETURN(lc->target.ec != target_ec);

   lc->target.ec = NULL;
   lc->target.delfn = NULL;

   switch (lc->state)
     {
      case LAUNCHER_STATE_PREPARING:
      case LAUNCHER_STATE_LAUNCHING:
      case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:
         _launcher_stop_send(lc);
         if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
           _launcher_post_forward(lc, EINA_FALSE);
         else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
           _launcher_post_backward(lc, EINA_FALSE);

         _launcher_handler_launcher_runner_unset(lc);
         _launcher_handler_launcher_pre_runner_unset(lc);

         _launcher_state_set(lc, LAUNCHER_STATE_IDLE);
         break;
      default:
         break;
     }
}

static void
_launcher_launched_ec_set(E_Service_Launcher *lc, E_Client *launched_ec)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);
   if (lc->launched_ec == launched_ec) return;

   if (lc->launched_ec)
     {
        e_object_delfn_del(E_OBJECT(lc->launched_ec), lc->launched_delfn);
        lc->launched_ec = NULL;
        lc->launched_delfn = NULL;
     }

   if (launched_ec)
     {
        lc->launched_ec = launched_ec;
        lc->launched_delfn = e_object_delfn_add(E_OBJECT(launched_ec),
                                                _launcher_cb_launched_ec_del, lc);
     }
}

static void
_launcher_target_ec_set(E_Service_Launcher *lc, E_Client *target_ec)
{
   EINA_SAFETY_ON_NULL_RETURN(lc);
   if (lc->target.ec == target_ec) return;

   if (lc->target.ec)
     {
        e_object_delfn_del(E_OBJECT(lc->target.ec), lc->target.delfn);
        lc->target.ec = NULL;
        lc->target.delfn = NULL;
     }

   if (target_ec)
     {
        lc->target.ec = target_ec;
        lc->target.delfn = e_object_delfn_add(E_OBJECT(target_ec),
                                              _launcher_cb_target_ec_del, lc);
     }
}

static void
_launcher_cb_resource_destroy(struct wl_resource *res_tws_lc)
{
   E_Service_Launcher *lc;

   lc = wl_resource_get_user_data(res_tws_lc);
   if (!lc) return;

   ELOGF("LAUNCHER_SRV", "Start Resource Destroy tws_service_launcher", lc->ec);

   _launcher_handler_launcher_del(lc);

   lc->res = NULL;
   _launcher_data_reset(lc);

   E_FREE(lc);

   ELOGF("LAUNCHER_SRV", "End Resource Destroy tws_service_launcher", NULL);
}

static void
_launcher_cb_destroy(struct wl_client *client EINA_UNUSED,
                           struct wl_resource *res_tws_lc)
{
   ELOGF("LAUNCHER_SRV", "Received request(launcher_destroy)", NULL);
   wl_resource_destroy(res_tws_lc);
}

static void
_launcher_cb_launch(struct wl_client *client EINA_UNUSED,
                          struct wl_resource *res_tws_lc,
                          const char *app_id,
                          const char *instance_id,
                          int32_t pid)
{
   E_Service_Launcher *lc;
   E_Service_Launcher *runner, *pre_runner;
   E_Client *target_ec;
   Eina_List *ecs, *l;
   Eina_Bool sent = EINA_FALSE;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_NULL_RETURN(lc->ec);

   ELOGF("LAUNCHER_SRV",
         "Recieved request(launcher_launch) appid:%s instance id:%s pid:%d",
         lc->ec, app_id?:"NONE", instance_id?:"NONE", pid);

   EINA_SAFETY_ON_TRUE_GOTO(lc->ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED, send_stop);
   EINA_SAFETY_ON_TRUE_GOTO(pid < 0, send_stop);

   //check current state of lc
   runner =  _launcher_handler_launcher_runner_get();
   if (runner == lc)
     {
        ELOGF("LAUNCHER_SRV",
              "Launcher(%s) requests LAUNCH again without cancel, ignore this.",
              lc->ec, _launcher_state_to_str(lc->state));

        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_WRONG_REQUEST, lc->serial);
        return;
     }

   pre_runner = _launcher_handler_launcher_pre_runner_get();
   if (pre_runner == lc)
     {
        _launcher_handler_launcher_pre_runner_set(NULL);
        _launcher_launched_ec_set(lc, NULL);
     }

   lc->target.pid = pid;

   ecs = _launcher_clients_find_by_pid(pid);
   EINA_LIST_FOREACH(ecs, l, target_ec)
     {
        if (e_object_is_del(E_OBJECT(target_ec))) continue;
        if (e_client_util_ignored_get(target_ec)) continue;

        ELOGF("LAUNCHER_SRV", "Found target_ec:%p", lc->ec, target_ec);

        sent = _launcher_prepare_forward_send(lc, target_ec);
        EINA_SAFETY_ON_FALSE_GOTO(sent, send_stop);

        _launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
        break;
     }
   eina_list_free(ecs);

   if (!lc->target.ec)
     {
        ELOGF("LAUNCHER_SRV", "Can't find target_ec, Start Monitoring", lc->ec);
        _launcher_state_set(lc, LAUNCHER_STATE_MONITORING);
     }

   _launcher_handler_launcher_runner_set(lc);

   return;

send_stop:
   ELOGF("LAUNCHER_SRV", "can't process request(launcher_launch)", lc->ec);
   _launcher_stop_send(lc);
}

static void
_launcher_cb_launching(struct wl_client *client EINA_UNUSED,
                             struct wl_resource *res_tws_lc,
                             uint32_t serial)
{
   E_Service_Launcher *lc;
   E_Service_Launcher *runner;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCHING(%d) %s",
         lc->ec, serial, lc->direction?"backward":"forward");

   //check current state of lc
   runner = _launcher_handler_launcher_runner_get();
   if (runner != lc)
     {
        ELOGF("LAUNCHER_SRV", "lc(%p) runner(%p), lc is not runner, ignore LAUNCHING",
              lc->ec, lc, runner);
        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_WRONG_REQUEST, lc->serial);
        return;
     }

   _launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING);

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     {
        _launcher_state_set(lc, LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER);
        lc->buff_attach = ecore_event_handler_add(E_EVENT_CLIENT_BUFFER_CHANGE,
                                                 _launcher_cb_event_buff_attach, lc);
     }
}

static void
_launcher_cb_launch_done(struct wl_client *client EINA_UNUSED,
                               struct wl_resource *res_tws_lc,
                               uint32_t serial)
{
   E_Service_Launcher *lc;
   E_Service_Launcher *runner;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCH_DONE(%d) target:%p", lc->ec, serial, lc->target.ec);

   //check current state of lc
   runner = _launcher_handler_launcher_runner_get();
   if (runner != lc)
     {
        ELOGF("LAUNCHER_SRV", "lc(%p) runner(%p), lc is not runner, ignore LAUNCH_DONE",
              lc->ec, lc, runner);
        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_WRONG_REQUEST, lc->serial);
        return;
     }

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _launcher_post_forward(lc, EINA_TRUE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _launcher_post_backward(lc, EINA_TRUE);

   _launcher_handler_launcher_runner_unset(lc);
   _launcher_handler_launcher_pre_runner_set(lc);
   _launcher_state_set(lc, LAUNCHER_STATE_DONE);
}

static void
_launcher_cb_launch_cancel(struct wl_client *client EINA_UNUSED,
                                 struct wl_resource *res_tws_lc,
                                 uint32_t serial)
{
   E_Service_Launcher *lc;
   E_Service_Launcher *runner;

   lc = wl_resource_get_user_data(res_tws_lc);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   ELOGF("LAUNCHER_SRV", "LAUNCH_CANCEL(%d) target_ec:%p",
         lc->ec, serial, lc->target.ec);

   //check state of lc
   runner = _launcher_handler_launcher_runner_get();
   if (runner != lc)
     {
        ELOGF("LAUNCHER_SRV", "lc(%p) runner(%p), lc is not runner, ignore LAUNCH_CANCEL",
              lc->ec, lc, runner);
        tws_service_launcher_send_error(lc->res, TWS_SERVICE_LAUNCHER_ERROR_WRONG_REQUEST, lc->serial);
        return;
     }

   if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _launcher_post_forward(lc, EINA_FALSE);
   else if (lc->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _launcher_post_backward(lc, EINA_FALSE);

   _launcher_handler_launcher_runner_unset(lc);
   _launcher_handler_launcher_pre_runner_set(lc);
   _launcher_state_set(lc, LAUNCHER_STATE_CANCELED);
}

static const struct tws_service_launcher_interface _launcher_iface =
{
   _launcher_cb_destroy,
   _launcher_cb_launch,
   _launcher_cb_launching,
   _launcher_cb_launch_done,
   _launcher_cb_launch_cancel,
};

static E_Service_Launcher *
_launcher_handler_launcher_find(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler, NULL);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler->launcher_hash, NULL);

   return eina_hash_find(_laundler->launcher_hash, &ec);
}

static Eina_Bool
_launcher_handler_launcher_add(E_Service_Launcher *lc)
{
   Eina_Bool ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc->ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler->launcher_hash, EINA_FALSE);

   ret = eina_hash_add(_laundler->launcher_hash, &lc->ec, lc);
   if (ret)
     _laundler->launcher_count++;

   return ret;
}

static Eina_Bool
_launcher_handler_launcher_del(E_Service_Launcher *lc)
{
   Eina_Bool ret;

   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc->ec, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler->launcher_hash, EINA_FALSE);

   ret = eina_hash_del(_laundler->launcher_hash, &lc->ec, lc);
   if (ret) _laundler->launcher_count--;

   return ret;
}

static void
_launcher_handler_launcher_runner_set(E_Service_Launcher *lc)
{
   E_Service_Launcher *runner = NULL;

   EINA_SAFETY_ON_NULL_RETURN(_laundler);
   if (_laundler->runner == lc) return;

   //reset previous runner
   runner = _laundler->runner;
   if (runner)
     {
        switch (runner->state)
          {
           case LAUNCHER_STATE_PREPARING:
           case LAUNCHER_STATE_LAUNCHING:
           case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:
              _launcher_state_set(runner, LAUNCHER_STATE_CANCELED);
              _launcher_stop_send(runner);
              if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
                _launcher_post_forward(runner, EINA_FALSE);
              else if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
                _launcher_post_backward(runner, EINA_FALSE);
              break;
           case LAUNCHER_STATE_MONITORING:
              _launcher_state_set(runner, LAUNCHER_STATE_CANCELED);
              _launcher_stop_send(runner);
              runner->target.pid = -1;
              break;
           default:
              break;
          }
        _launcher_handler_launcher_pre_runner_set(runner);
     }

   ELOGF("LAUNCHER_SRV", "runner change %p(ec:%p) to %p(ec:%p)",
         NULL, runner, runner?runner->ec:NULL, lc, lc?lc->ec:NULL);

   _laundler->runner = lc;
}

static void
_launcher_handler_launcher_runner_unset(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(_laundler);
   if (_laundler->runner != lc) return;

   _laundler->runner = NULL;

   ELOGF("LAUNCHER_SRV", "runner unset %p(ec:%p)",
         NULL, lc, lc?lc->ec:NULL);
}

static E_Service_Launcher *
_launcher_handler_launcher_runner_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler, NULL);
   return _laundler->runner;
}

static void
_launcher_handler_launcher_pre_runner_set(E_Service_Launcher *lc)
{
   E_Service_Launcher *pre_runner = NULL;

   EINA_SAFETY_ON_NULL_RETURN(_laundler);

   pre_runner = _laundler->pre_runner;
   if (pre_runner == lc) return;
   if (pre_runner)
     {
        if (pre_runner->launched_ec)
          e_policy_animatable_lock(pre_runner->launched_ec, E_POLICY_ANIMATABLE_CUSTOMIZED, 0);
        _launcher_state_set(pre_runner, LAUNCHER_STATE_IDLE);
     }

   _laundler->pre_runner = lc;

   ELOGF("LAUNCHER_SRV", "pre_runner change %p(ec:%p) to %p(ec:%p)",
         NULL, pre_runner, pre_runner?pre_runner->ec:NULL, lc, lc?lc->ec:NULL);
}

static void
_launcher_handler_launcher_pre_runner_unset(E_Service_Launcher *lc)
{
   EINA_SAFETY_ON_NULL_RETURN(_laundler);
   if (_laundler->pre_runner != lc) return;

   _laundler->pre_runner = NULL;

   ELOGF("LAUNCHER_SRV", "pre_runner unset %p(ec:%p)",
         NULL, lc, lc?lc->ec:NULL);
}

static E_Service_Launcher *
_launcher_handler_launcher_pre_runner_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(_laundler, NULL);
   return _laundler->pre_runner;
}

static Eina_Bool
_launcher_handler_cb_hook_vis_uniconify_render_running(void *data EINA_UNUSED, E_Client *ec)
{
   E_Service_Launcher *lc = NULL;
   E_Service_Launcher *runner, *pre_runner = NULL;
   E_Client *activity = NULL;

   lc = _launcher_handler_launcher_find(ec);
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

             //check state of lc
             runner =  _launcher_handler_launcher_runner_get();
             if (runner == lc)
               {
                  ELOGF("LAUNCHER_SRV",
                        "Launcher(%s) is already runner, do nothing",
                        lc->ec, _launcher_state_to_str(lc->state));
                  return EINA_TRUE;
               }

             pre_runner = _launcher_handler_launcher_pre_runner_get();
             if (pre_runner ==  lc)
               {
                  _launcher_handler_launcher_pre_runner_set(NULL);
               }

             sent = _launcher_prepare_backward_send(lc, activity, activity,
                                                          (E_VIS_JOB_TYPE_UNICONIFY |
                                                           E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY));
             if (!sent) return EINA_FALSE;

             _launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
             _launcher_handler_launcher_runner_set(lc);
          }
     }

   return EINA_TRUE;
}

static Eina_Bool
_launcher_handler_cb_hook_vis_lower(void *data EINA_UNUSED, E_Client *ec)
{
   E_Service_Launcher *lc = NULL;
   E_Service_Launcher *runner, *pre_runner;
   E_Client *activity = NULL;

   activity = e_policy_visibility_main_activity_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_FALSE);

   lc = _launcher_handler_launcher_find(activity);
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);

   if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) return EINA_FALSE;

   if (ec == lc->launched_ec)
     {
        Eina_Bool sent = EINA_FALSE;
        ELOGF("LAUNCHER_SRV", "Lower hook of launched_ec(%p)", lc->ec, ec);

        //check state of lc
        runner =  _launcher_handler_launcher_runner_get();
        if (runner == lc)
          {
             ELOGF("LAUNCHER_SRV",
                   "Launcher(%s) is already runner, do nothing",
                   lc->ec, _launcher_state_to_str(lc->state));
             return EINA_FALSE;
          }

        pre_runner = _launcher_handler_launcher_pre_runner_get();
        if (pre_runner ==  lc)
          {
             _launcher_handler_launcher_pre_runner_set(NULL);
          }

        sent = _launcher_prepare_backward_send(lc, activity, ec, E_VIS_JOB_TYPE_LOWER);
        if (!sent) return EINA_FALSE;

        _launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
        _launcher_handler_launcher_runner_set(lc);
     }

   return EINA_TRUE;
}

static Eina_Bool
_launcher_handler_cb_hook_vis_hide(void *data EINA_UNUSED, E_Client *ec)
{
   E_Service_Launcher *lc = NULL;
   E_Service_Launcher *runner, *pre_runner;
   E_Client *activity = NULL;

   activity = e_policy_visibility_main_activity_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(activity, EINA_FALSE);

   lc = _launcher_handler_launcher_find(activity);
   EINA_SAFETY_ON_NULL_RETURN_VAL(lc, EINA_FALSE);

   if (ec->visibility.obscured != E_VISIBILITY_UNOBSCURED) return EINA_FALSE;

   if (ec == lc->launched_ec)
     {
        Eina_Bool sent = EINA_FALSE;
        ELOGF("LAUNCHER_SRV", "Hide hook of launched_ec(%p)", lc->ec, ec);

        //check state of lc
        runner =  _launcher_handler_launcher_runner_get();
        if (runner == lc)
          {
             ELOGF("LAUNCHER_SRV",
                   "Launcher(%s) is already runner, do nothing",
                   lc->ec, _launcher_state_to_str(lc->state));
             return EINA_FALSE;
          }

        pre_runner = _launcher_handler_launcher_pre_runner_get();
        if (pre_runner ==  lc)
          {
             _launcher_handler_launcher_pre_runner_set(NULL);
          }

        sent = _launcher_prepare_backward_send(lc, activity, ec, E_VIS_JOB_TYPE_HIDE);
        if (!sent) return EINA_FALSE;

        _launcher_state_set(lc, LAUNCHER_STATE_PREPARING);
        _launcher_handler_launcher_runner_set(lc);
     }

   return EINA_TRUE;
}

static void
_launcher_handler_cb_hook_client_del(void *data EINA_UNUSED, E_Client *ec)
{
   E_Service_Launcher *lc;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   lc = _launcher_handler_launcher_find(ec);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   if (lc->ec == ec) //launcher surface is gone.
     {
        _launcher_handler_launcher_del(lc);
        _launcher_data_reset(lc);
        E_FREE(lc);
     }
}

static Eina_Bool
_launcher_handler_cb_hook_intercept_show_helper(void *data, E_Client *ec)
{
   E_Service_Launcher *runner;
   Eina_Bool sent = EINA_FALSE;

   runner = _launcher_handler_launcher_runner_get();

   EINA_SAFETY_ON_NULL_GOTO(runner, show_allow);
   EINA_SAFETY_ON_NULL_GOTO(ec, show_allow);

   if (ec->new_client) goto show_allow;

   switch (runner->state)
     {
      case LAUNCHER_STATE_LAUNCHING_WAIT_BUFFER:  //waiting buffer change
         if (ec == runner->ec)
           goto show_deny;
         break;
      case LAUNCHER_STATE_PREPARING:              //waiting launcher client's preparation
         if (ec  == runner->target.ec) goto show_deny;
         break;
      case LAUNCHER_STATE_LAUNCHING:              //doing animation
         if (ec == runner->target.ec) goto show_deny; //don't show launched app window
         else if (ec == runner->ec) goto show_allow;  //show launcher
         break;
      case LAUNCHER_STATE_MONITORING:             //waiting creation of target window
         if (ec->netwm.pid != runner->target.pid) goto show_allow;
         if (e_object_is_del(E_OBJECT(ec))) goto show_allow;

         sent = _launcher_prepare_forward_send(runner, ec);
         EINA_SAFETY_ON_FALSE_GOTO(sent, send_stop);

         _launcher_state_set(runner, LAUNCHER_STATE_PREPARING);
         goto show_deny;
      default:
         goto show_allow;
     }

show_allow:
   return EINA_TRUE;
show_deny:
   return EINA_FALSE;
send_stop:
   runner->target.pid = -1;
   _launcher_stop_send(runner);
   _launcher_state_set(runner, LAUNCHER_STATE_IDLE);
   _launcher_handler_launcher_runner_unset(runner);
   return EINA_TRUE;
}

static Eina_Bool
_launcher_handler_cb_event_client_show(void *data, int type EINA_UNUSED, void *event)
{
   E_Service_Launcher *runner, *pre_runner;
   E_Client *ec;
   E_Event_Client *ev;

   runner = _launcher_handler_launcher_runner_get();
   EINA_SAFETY_ON_NULL_RETURN_VAL(runner, ECORE_CALLBACK_PASS_ON);

   ev = (E_Event_Client *)event;
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev, ECORE_CALLBACK_PASS_ON);
   EINA_SAFETY_ON_NULL_RETURN_VAL(ev->ec, ECORE_CALLBACK_PASS_ON);

   ec = ev->ec;

   switch (runner->state)
     {
      case LAUNCHER_STATE_MONITORING:
         if (ec->netwm.pid == runner->target.pid)
           goto send_stop;
         break;
      case LAUNCHER_STATE_PREPARING:
         if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
           {
              if (ec == runner->target.ec)  goto send_stop;
           }
         else if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
           {
              if (ec == runner->ec) goto send_stop;
           }
         break;
      case LAUNCHER_STATE_LAUNCHING:
         if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
           {
              if (ec == runner->target.ec) goto send_stop;
           }
         break;
      default:
         break;
     }

   pre_runner = _launcher_handler_launcher_pre_runner_get();
   if (pre_runner)
     {
        switch (pre_runner->state)
          {
           case LAUNCHER_STATE_DONE:
              if (ec == pre_runner->launched_ec)
                _launcher_handler_launcher_pre_runner_set(NULL);
              break;
           case LAUNCHER_STATE_CANCELED:
              _launcher_handler_launcher_pre_runner_set(NULL);
              break;
           default:
              break;
          }
     }

   return ECORE_CALLBACK_PASS_ON;

send_stop:
   ELOGF("LAUNCHER_SRV", "CLIENT SHOW: Failure Case runner:%p", ec, runner);

   _launcher_stop_send(runner);

   if (runner->state == LAUNCHER_STATE_MONITORING)
     runner->target.pid = -1;
   else if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_FORWARD)
     _launcher_post_forward(runner, EINA_FALSE);
   else if (runner->direction == TWS_SERVICE_LAUNCHER_DIRECTION_BACKWARD)
     _launcher_post_backward(runner, EINA_FALSE);

   _launcher_state_set(runner, LAUNCHER_STATE_IDLE);
   _launcher_handler_launcher_runner_unset(runner);

   return ECORE_CALLBACK_PASS_ON;
}

#undef  LAUNCHER_HANDLER_CB_ADD
#define LAUNCHER_HANDLER_CB_ADD(l, appender, event_type, cb, data)  \
  do                                                                \
    {                                                               \
       void *_h;                                                    \
       _h = appender(event_type, cb, data);                         \
       assert(_h);                                                  \
       l = eina_list_append(l, _h);                                 \
    }                                                               \
  while (0)


static E_Service_Launcher_Handler *
_launcher_handler_create(void)
{
   E_Service_Launcher_Handler *laundler = NULL;

   laundler = E_NEW(E_Service_Launcher_Handler, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(laundler, NULL);

   laundler->launcher_count = 0;
   laundler->launcher_hash = eina_hash_pointer_new(NULL);

   LAUNCHER_HANDLER_CB_ADD(laundler->hooks_vis,
                           e_policy_visibility_hook_add,
                           E_POL_VIS_HOOK_TYPE_UNICONIFY_RENDER_RUNNING,
                           _launcher_handler_cb_hook_vis_uniconify_render_running, NULL);

   LAUNCHER_HANDLER_CB_ADD(laundler->hooks_vis,
                           e_policy_visibility_hook_add,
                           E_POL_VIS_HOOK_TYPE_LOWER,
                           _launcher_handler_cb_hook_vis_lower, NULL);

   LAUNCHER_HANDLER_CB_ADD(laundler->hooks_vis,
                           e_policy_visibility_hook_add,
                           E_POL_VIS_HOOK_TYPE_HIDE,
                           _launcher_handler_cb_hook_vis_hide, NULL);

   LAUNCHER_HANDLER_CB_ADD(laundler->hooks_ec,
                           e_client_hook_add,
                           E_CLIENT_HOOK_DEL,
                           _launcher_handler_cb_hook_client_del, NULL);

   LAUNCHER_HANDLER_CB_ADD(laundler->hooks_co,
                           e_comp_object_intercept_hook_add,
                           E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER,
                           _launcher_handler_cb_hook_intercept_show_helper, NULL);


   LAUNCHER_HANDLER_CB_ADD(laundler->hdlrs_ev,
                           ecore_event_handler_add,
                           E_EVENT_CLIENT_SHOW,
                           _launcher_handler_cb_event_client_show, NULL);

   ELOGF("LAUNCHER_SRV", "new launcher handler(%p) created", NULL, laundler);

   return laundler;
}

static void
_launcher_handler_destroy(E_Service_Launcher_Handler *laundler)
{
   EINA_SAFETY_ON_NULL_RETURN(laundler);

   E_FREE_LIST(laundler->hdlrs_ev, ecore_event_handler_del);
   E_FREE_LIST(laundler->hooks_co, e_comp_object_intercept_hook_del);
   E_FREE_LIST(laundler->hooks_ec, e_client_hook_del);
   E_FREE_LIST(laundler->hooks_vis, e_policy_visibility_hook_del);

   E_FREE_FUNC(laundler->launcher_hash, eina_hash_free);
   E_FREE(laundler);
}

EINTERN void
e_service_launcher_resource_set(E_Client *ec, struct wl_resource *res_tws_lc)
{
   E_Service_Launcher *lc;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_NULL_RETURN(res_tws_lc);

   lc = _launcher_handler_launcher_find(ec);
   EINA_SAFETY_ON_NULL_RETURN(lc);
   EINA_SAFETY_ON_TRUE_RETURN(lc->ec != ec);

   lc->res = res_tws_lc;
   wl_resource_set_implementation(res_tws_lc,
                                  &_launcher_iface, lc,
                                  _launcher_cb_resource_destroy);
}

EINTERN void
e_service_launcher_client_set(E_Client *ec)
{
   E_Service_Launcher *lc = NULL;

   EINA_SAFETY_ON_NULL_RETURN(ec);
   EINA_SAFETY_ON_TRUE_RETURN(e_object_is_del(E_OBJECT(ec)));

   if ((lc = _launcher_handler_launcher_find(ec)))
     {
        ELOGF("LAUNCHER_SRV", "ec(%p) is already set as launcher(%p)",
              ec, ec, lc);
        return;
     }

   if (!_laundler)
     {
        _laundler = _launcher_handler_create();
        EINA_SAFETY_ON_NULL_RETURN(_laundler);
     }

   lc = E_NEW(E_Service_Launcher, 1);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   lc->ec = ec;
   if (!_launcher_handler_launcher_add(lc))
     {
        E_FREE(lc);
        ELOGF("LAUNCHER_SRV", "Fail to add launcher", ec);
        return;
     }

   ELOGF("LAUNCHER_SRV", "client set|Created New Launcher(%p)", ec, lc);
   _launcher_state_set(lc, LAUNCHER_STATE_IDLE);

   return;
}

EINTERN void
e_service_launcher_client_unset(E_Client *ec)
{
   E_Service_Launcher *lc;

   EINA_SAFETY_ON_NULL_RETURN(ec);

   lc = _launcher_handler_launcher_find(ec);
   EINA_SAFETY_ON_NULL_RETURN(lc);

   _launcher_handler_launcher_del(lc);
   _launcher_data_reset(lc);
   E_FREE(lc);

   ELOGF("LAUNCHER_SRV", "client unset", ec);

   if ((_laundler) && (_laundler->launcher_count == 0))
     {
        ELOGF("LAUNCHER_SRV", "No launcher available, Destroy Launcher Handler(%p)",
              NULL, _laundler);

        _launcher_handler_destroy(_laundler);
        _laundler = NULL;
     }
}
