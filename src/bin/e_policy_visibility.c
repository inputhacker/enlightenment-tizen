/*
 * activity    : The client which has full screen size and normal type.
 * foreground  : The client which is unobscured.
 * background  : The client which is fully obscured by others.
 */

#include "e.h"
#include "e_comp_wl.h"
#include "e_policy_wl.h"
#include "e_policy_visibility.h"
#include "e_policy_visibility_internal.h"

#ifdef ENABLE_TTRACE
# include <ttrace.h>
# undef TRACE_DS_BEGIN
# undef TRACE_DS_END

# define TRACE_DS_BEGIN(NAME) traceBegin(TTRACE_TAG_WINDOW_MANAGER, "DS:POL:"#NAME)
# define TRACE_DS_END() traceEnd(TTRACE_TAG_WINDOW_MANAGER)
#else
# define TRACE_DS_BEGIN(NAME)
# define TRACE_DS_END()
#endif

typedef enum _E_Pol_Vis_Type
{
   E_POL_VIS_TYPE_NON_ALPHA,
   E_POL_VIS_TYPE_ALPHA,
   E_POL_VIS_TYPE_ALPHA_OPAQUE,
} E_Pol_Vis_Type;

struct _E_Pol_Vis_Hook
{
   EINA_INLIST;
   E_Pol_Vis_Hook_Type type;
   E_Pol_Vis_Hook_Cb   cb;
   void               *data;
   Eina_Bool           delete_me;
};

static Eina_Bool _e_policy_check_transient_child_visible(E_Client *ancestor_ec, E_Client *ec);
static Eina_Bool _e_policy_check_above_alpha_opaque(E_Client *ec);
static void      _e_policy_client_iconify_by_visibility(E_Client *ec);
static void      _e_policy_client_ancestor_uniconify(E_Client *ec);
static void      _e_policy_client_below_uniconify(E_Client *ec);
static void      _e_policy_client_uniconify_by_visibility(E_Client *ec);

static inline Eina_Bool  _e_vis_client_is_grabbed(E_Vis_Client *vc);
static void              _e_vis_client_grab_remove(E_Vis_Client *vc, E_Vis_Grab *grab);
static Eina_Bool         _e_vis_client_grab_cancel(E_Vis_Client *vc);
static void              _e_vis_client_job_exec(E_Vis_Client *vc, E_Vis_Job_Type type);
static Eina_Bool         _e_vis_ec_activity_check(E_Client *ec, Eina_Bool check_alpha);
static void              _e_vis_ec_job_exec(E_Client *ec, E_Vis_Job_Type type);
static void              _e_vis_ec_setup(E_Client *ec);
static void              _e_vis_ec_reset(E_Client *ec);
static E_Pol_Vis_Type    _e_vis_ec_above_visible_type(E_Client *ec, Eina_Bool check_child);
static Eina_Bool         _e_vis_ec_below_uniconify(E_Client *ec, E_Pol_Vis_Type above_vis_type);
static void              _e_vis_cb_child_launch_done(void *data, Evas_Object *obj, const char *signal, const char *source);
static void              _e_vis_update_foreground_job_queue(void);
static void              _e_vis_update_forground_list(void);
static Eina_Bool         _e_vis_client_check_send_pre_visibility(E_Vis_Client *vc, Eina_Bool raise);

static E_Vis            *pol_vis = NULL;
/* the list for E_Vis_Job */
static E_Vis_Job_Group  *pol_job_group = NULL;
/* the head of list for E_Vis_Job_Group */
static Eina_Clist        pol_job_group_head = EINA_CLIST_INIT(pol_job_group_head);

static Eina_Inlist *_e_pol_vis_hooks[] =
{
   [E_POL_VIS_HOOK_TYPE_FG_SET] = NULL,
};

static int _e_pol_vis_hooks_delete = 0;
static int _e_pol_vis_hooks_walking = 0;

static inline Eina_Bool
_e_vis_client_is_grabbed(E_Vis_Client *vc)
{
   return !!vc->job.grab_list;
}

static inline Eina_Bool
_e_vis_client_is_iconic(E_Vis_Client *vc)
{
   return (vc->state == E_VIS_ICONIFY_STATE_ICONIC);
}

static inline Eina_Bool
_e_vis_client_is_uniconic(E_Vis_Client *vc)
{
   return (vc->state == E_VIS_ICONIFY_STATE_UNICONIC);
}

static inline Eina_Bool
_e_vis_client_is_uniconify_render_running(E_Vis_Client *vc)
{
   return (vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY);
}

static inline Eina_Bool
_e_vis_client_is_uniconify_render_running_done(E_Vis_Client *vc)
{
   return (vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_RENDER_DONE);
}

static inline void
_e_vis_client_below_uniconify_skip_set(E_Vis_Client *vc, Eina_Bool skip)
{
   vc->skip_below_uniconify = skip;
}

static inline Eina_Bool
_e_vis_client_is_below_uniconify_skip(E_Vis_Client *vc)
{
   return (vc->skip_below_uniconify == EINA_TRUE);
}

static void
_e_pol_vis_hooks_clean(void)
{
   Eina_Inlist *l;
   E_Pol_Vis_Hook *h;
   unsigned int x;

   for (x = 0; x < E_POL_VIS_HOOK_TYPE_LAST; x++)
     EINA_INLIST_FOREACH_SAFE(_e_pol_vis_hooks[x], l, h)
       {
          if (!h->delete_me) continue;
          _e_pol_vis_hooks[x] = eina_inlist_remove(_e_pol_vis_hooks[x],
                                                   EINA_INLIST_GET(h));
          E_FREE(h);
       }
}

static Eina_Bool
_e_pol_vis_hook_call(E_Pol_Vis_Hook_Type type, E_Client *ec)
{
   E_Pol_Vis_Hook *h;

   e_object_ref(E_OBJECT(ec));

   _e_pol_vis_hooks_walking++;
   EINA_INLIST_FOREACH(_e_pol_vis_hooks[type], h)
     {
        if (h->delete_me) continue;
        h->cb(h->data, ec);
     }
   _e_pol_vis_hooks_walking--;

   if ((_e_pol_vis_hooks_walking == 0) &&
       (_e_pol_vis_hooks_delete > 0))
     _e_pol_vis_hooks_clean();

   return !!e_object_unref(E_OBJECT(ec));
}

static Eina_Bool
_e_policy_check_transient_child_visible(E_Client *ancestor_ec, E_Client *ec)
{
   Eina_Bool visible = EINA_FALSE;
   Eina_List *list = NULL;
   E_Client *child_ec = NULL;
   int anc_x, anc_y, anc_w, anc_h;
   int child_x, child_y, child_w, child_h;

   if (!ancestor_ec) return EINA_FALSE;

   e_client_geometry_get(ancestor_ec, &anc_x, &anc_y, &anc_w, &anc_h);

   list = eina_list_clone(ec->transients);
   EINA_LIST_FREE(list, child_ec)
     {
        if (visible == EINA_TRUE) continue;

        if (!child_ec->comp_data) continue;
        if (!child_ec->comp_data->mapped && !child_ec->use_splash) continue;
        if (child_ec->transient_policy == E_TRANSIENT_BELOW) continue;

        if ((child_ec->exp_iconify.skip_iconify == EINA_TRUE) ||
            (child_ec->exp_iconify.skip_by_remote == EINA_TRUE))
          {
             if (child_ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
               {
                  ELOGF("Find visible child", "ancestor(win:0x%08zx, ec:%p), child(win:0x%08zx, ec:%p)",
                        ec->pixmap, ec,
                        e_client_util_win_get(ancestor_ec), ancestor_ec,
                        e_client_util_win_get(child_ec), child_ec);
                  return EINA_TRUE;
               }
             else
               {
                  if (!child_ec->iconic)
                    {
                       e_client_geometry_get(child_ec, &child_x, &child_y, &child_w, &child_h);
                       if (E_CONTAINS(child_x, child_y, child_w, child_h, anc_x, anc_y, anc_w, anc_h))
                         {
                            ELOGF("Find visible child", "ancestor(win:0x%08zx, ec:%p), child(win:0x%08zx, ec:%p)",
                                  ec->pixmap, ec,
                                  e_client_util_win_get(ancestor_ec), ancestor_ec,
                                  e_client_util_win_get(child_ec), child_ec);
                            return EINA_TRUE;
                         }
                    }
               }
          }
        else
          {
             if ((!child_ec->iconic) ||
                 (child_ec->visibility.obscured == E_VISIBILITY_UNOBSCURED))
               {
                  ELOGF("Find visible child", "ancestor(win:0x%08zx, ec:%p), child(win:0x%08zx, ec:%p)",
                        ec->pixmap, ec,
                        e_client_util_win_get(ancestor_ec), ancestor_ec,
                        e_client_util_win_get(child_ec), child_ec);
                  return EINA_TRUE;
               }
          }

        visible = _e_policy_check_transient_child_visible(ancestor_ec, child_ec);
     }

   return visible;
}

static Eina_Bool
_e_policy_check_above_alpha_opaque(E_Client *ec)
{
   E_Client *above_ec;
   Evas_Object *o;
   Eina_Bool alpha_opaque = EINA_FALSE;

   for (o = evas_object_above_get(ec->frame); o; o = evas_object_above_get(o))
     {
        above_ec = evas_object_data_get(o, "E_Client");
        if (!above_ec) continue;
        if (e_client_util_ignored_get(above_ec)) continue;
        if (!E_CONTAINS(above_ec->x, above_ec->y, above_ec->w, above_ec->h, ec->x, ec->y, ec->w, ec->h)) continue;
        if (above_ec->comp_data && !above_ec->comp_data->mapped) continue;

        if (above_ec->argb)
          {
             if (above_ec->visibility.opaque <= 0)
               continue;
             else
               {
                  if (above_ec->visibility.obscured == E_VISIBILITY_UNOBSCURED)
                    {
                       alpha_opaque = EINA_TRUE;
                    }
                  else
                    {
                       if (!above_ec->iconic)
                         {
                            if (above_ec->comp_data && above_ec->comp_data->mapped)
                              alpha_opaque = EINA_TRUE;
                         }
                    }
               }
          }
        break;
     }

   return alpha_opaque;
}

static void
_e_policy_client_iconify_by_visibility(E_Client *ec)
{
   int skip_iconify = 0;

   if (!ec) return;
   if (ec->iconic) return;
   if (ec->exp_iconify.by_client) return;
   if (ec->bg_state) return;
   if (ec->exp_iconify.skip_iconify) return;
   if (ec->exp_iconify.skip_by_remote) return;

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cdata && !cdata->mapped) return;

   if (e_config->transient.iconify)
     {
        if (_e_policy_check_transient_child_visible(ec, ec))
          {
             skip_iconify |= 1;
          }
     }

   if (ec->zone->display_state != E_ZONE_DISPLAY_STATE_OFF)
     {
        // check above window is alpha opaque or not
        if (_e_policy_check_above_alpha_opaque(ec))
          {
             skip_iconify |= 2;
          }
     }

   if (skip_iconify)
     {
        ELOGF("SKIP.. ICONIFY_BY_WM", "win:0x%08zx cause_type:%d", ec->pixmap, ec, e_client_util_win_get(ec), skip_iconify);
        if (pol_vis)
          _e_vis_update_forground_list();
        return;
     }

   ELOGF("ICONIFY_BY_WM", "win:0x%08zx", ec->pixmap, ec, e_client_util_win_get(ec));
   e_policy_wl_iconify_state_change_send(ec, 1);
   e_client_iconify(ec);

   /* if client has obscured parent, try to iconify the parent also */
   if (ec->parent)
     {
        if (ec->parent->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
          {
             _e_policy_client_iconify_by_visibility(ec->parent);

             E_VIS_CLIENT_GET(vc, ec->parent);
             if (vc)
               {
                  if (_e_vis_client_is_uniconify_render_running(vc))
                    {
                       VS_INF(ec, "Uniconify render because parent(win:%zx, ec:%p)", e_client_util_win_get(ec->parent), ec->parent);
                       e_policy_visibility_client_uniconify(ec, !ec->parent->exp_iconify.not_raise);
                    }
               }
          }
     }
}

static void
_e_policy_client_ancestor_uniconify(E_Client *ec)
{
   Eina_List *list = NULL;
   Eina_List *l = NULL;
   E_Client *parent = NULL;
   int transient_iconify = 0;
   int count = 0;
   Eina_Bool ret = EINA_FALSE;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->iconic) return;
   if (ec->exp_iconify.by_client) return;
   if (ec->bg_state) return;
   if (ec->exp_iconify.skip_iconify) return;
   if (ec->exp_iconify.skip_by_remote) return;

   parent = ec->parent;
   while (parent)
     {
        if (count > 10)
          {
             // something strange state.
             ELOGF("CHECK transient_for tree", "win:0x%08zx, parent:0x%08zx", NULL, NULL, e_client_util_win_get(ec), e_client_util_win_get(parent));
             break;
          }

        if (e_object_is_del(E_OBJECT(parent))) break;
        if (!parent->iconic) break;
        if (parent->exp_iconify.by_client) break;
        if (parent->bg_state) break;
        if (parent->exp_iconify.skip_iconify) break;
        if (parent->exp_iconify.skip_by_remote) break;

        if (eina_list_data_find(list, parent))
          {
             // very bad. there are loop for parenting
             ELOGF("Very BAD. Circling transient_for window", "win:0x%08zx, parent:0x%08zx", NULL, NULL, e_client_util_win_get(ec), e_client_util_win_get(parent));
             break;
          }

        list = eina_list_prepend(list, parent);
        parent = parent->parent;

        // for preventing infiniting loop
        count++;
     }

   transient_iconify = e_config->transient.iconify;
   e_config->transient.iconify = 0;

   parent = NULL;
   EINA_LIST_FOREACH(list, l, parent)
     {
        ELOGF("UNICONIFY_BY_WM", "parent_win:0x%08zx", parent->pixmap, parent, e_client_util_win_get(parent));
        ret = e_policy_visibility_client_uniconify(parent, 0);
        if (!ret)
          {
             parent->exp_iconify.not_raise = 1;
             e_client_uniconify(parent);
             e_policy_wl_iconify_state_change_send(parent, 0);
          }
     }
   eina_list_free(list);

   e_config->transient.iconify = transient_iconify;
}

static void
_e_policy_client_below_uniconify(E_Client *ec)
{
   E_Client *below_ec;
   Evas_Object *o;

   for (o = evas_object_below_get(ec->frame); o; o = evas_object_below_get(o))
     {
        below_ec = evas_object_data_get(o, "E_Client");
        if (!below_ec) continue;
        if (e_client_util_ignored_get(below_ec)) continue;

        if (ec->parent == below_ec) break;
        if (!below_ec->iconic) break;

        if (below_ec->visibility.obscured == E_VISIBILITY_FULLY_OBSCURED)
          {
             _e_policy_client_uniconify_by_visibility(below_ec);
          }

        break;
     }
}

static void
_e_policy_client_uniconify_by_visibility(E_Client *ec)
{
   Eina_Bool ret = EINA_FALSE;

   if (!ec) return;
   if (e_object_is_del(E_OBJECT(ec))) return;
   if (!ec->iconic) return;
   if (ec->exp_iconify.by_client) return;
   if (ec->bg_state) return;
   if (ec->exp_iconify.skip_iconify) return;
   if (ec->exp_iconify.skip_by_remote) return;

   E_Comp_Wl_Client_Data *cdata = (E_Comp_Wl_Client_Data *)ec->comp_data;
   if (cdata && !cdata->mapped) return;

   _e_policy_client_ancestor_uniconify(ec);

   ELOGF("UNICONIFY_BY_WM", "win:0x%08zx", ec->pixmap, ec, e_client_util_win_get(ec));
   ret = e_policy_visibility_client_uniconify(ec, 0);
   if (!ret)
     {
        ec->exp_iconify.not_raise = 1;
        e_client_uniconify(ec);
        e_policy_wl_iconify_state_change_send(ec, 0);
     }

   if ((ec->visibility.opaque > 0) && (ec->argb))
     {
        _e_policy_client_below_uniconify(ec);
     }
}

void
e_policy_client_visibility_send(E_Client *ec)
{
   e_policy_wl_visibility_send(ec, ec->visibility.obscured);
}

void
e_policy_client_iconify_by_visibility(E_Client *ec)
{
   if (!ec) return;
   _e_policy_client_iconify_by_visibility(ec);
}

void
e_policy_client_uniconify_by_visibility(E_Client *ec)
{
   if (!ec) return;
   _e_policy_client_uniconify_by_visibility(ec);
}

static inline void
_e_vis_clist_unlink(Eina_Clist *elem)
{
   if (eina_clist_element_is_linked(elem))
     eina_clist_remove(elem);
}

static void
_e_vis_clist_clean(Eina_Clist *list, void (*free_func)(Eina_Clist *elem))
{
   Eina_Clist *elem;

   while ((elem = eina_clist_head(list)) != NULL)
     {
        eina_clist_remove(elem);
        if (free_func) free_func(elem);
     }
}

static void
_e_vis_update_forground_list(void)
{
   E_Client *ec, *fg_activity = NULL;

   DBG("VISIBILITY | Update Foreground Client List");

   /* clear list of fg_clients before updating */
   E_FREE_FUNC(pol_vis->fg_clients, eina_list_free);

   /* update foreground client list and find activity client */
   E_CLIENT_REVERSE_FOREACH(ec)
     {
        /* TODO: check if client is included to zone of mobile */
        if (!evas_object_visible_get(ec->frame)) continue;

        pol_vis->fg_clients = eina_list_append(pol_vis->fg_clients, ec);
        if (_e_vis_ec_activity_check(ec, EINA_TRUE))
          {
             fg_activity = ec;
             break;
          }
     }

   if (pol_vis->activity != fg_activity)
     {
        DBG("VISIBILITY | \tPrev: %s(%p)", pol_vis->activity ? NAME(pol_vis->activity) : "", pol_vis->activity);
        DBG("VISIBILITY | \tNew : %s(%p)", fg_activity ? NAME(fg_activity) : "", fg_activity);
        pol_vis->activity = fg_activity;
        /* TODO do we need to raise event like E_EVENT_VISIBILITY_ACTIVITY_CHANGE? */
        _e_pol_vis_hook_call(E_POL_VIS_HOOK_TYPE_FG_SET, fg_activity);
     }
}

static void
_e_vis_update_cb_job(void *data EINA_UNUSED)
{
   if (pol_vis->job.bg_find)
     {
        _e_vis_update_forground_list();
        pol_vis->job.bg_find = EINA_FALSE;
     }
   pol_vis->job.handler = NULL;
}

static void
_e_vis_update_job_queue(void)
{
   if (pol_vis->job.handler)
     ecore_job_del(pol_vis->job.handler);
   pol_vis->job.handler = ecore_job_add(_e_vis_update_cb_job, NULL);
}

static void
_e_vis_update_foreground_job_queue(void)
{
   pol_vis->job.bg_find = EINA_TRUE;
   _e_vis_update_job_queue();
}

static Eina_Bool
_e_vis_job_push(E_Vis_Job *job)
{
   if (!pol_job_group)
     {
        pol_job_group = E_NEW(E_Vis_Job_Group, 1);
        if (!pol_job_group)
          return EINA_FALSE;
        eina_clist_init(&pol_job_group->job_head);
     }
   eina_clist_add_tail(&pol_job_group->job_head, &job->entry);
   return EINA_TRUE;
}

static Eina_Bool
_e_vis_job_find(E_Vis_Client *vc, E_Vis_Job_Type type)
{
   E_Vis_Job_Group *group, *tmp_group;
   E_Vis_Job *job, *tmp_job;

   EINA_CLIST_FOR_EACH_ENTRY_SAFE(group, tmp_group,
                                  &pol_job_group_head, E_Vis_Job_Group, entry)
     {
        EINA_CLIST_FOR_EACH_ENTRY_SAFE(job, tmp_job,
                                       &group->job_head, E_Vis_Job, entry)
          {
             if (job->vc != vc) continue;
             if (job->type == type)
               return EINA_TRUE;
          }
     }

   return EINA_FALSE;
}

static Eina_Bool
_e_vis_job_add(E_Vis_Client *vc, E_Vis_Job_Type type, Ecore_Task_Cb timeout_func)
{
   E_VIS_ALLOC_RET_VAL(job, E_Vis_Job, 1, EINA_FALSE);
   if (!_e_vis_job_push(job))
     {
        free(job);
        return EINA_FALSE;
     }
   job->vc = vc;
   job->type = type;
   job->timer = ecore_timer_add(E_VIS_TIMEOUT, timeout_func, job);
   VS_INF(vc->ec, "NEW JOB:%p, type:%d", job, type);

   if ((job->type == E_VIS_JOB_TYPE_LOWER) ||
       (job->type == E_VIS_JOB_TYPE_HIDE) ||
       (job->type == E_VIS_JOB_TYPE_ICONIFY) ||
       (job->type == E_VIS_JOB_TYPE_LAYER_LOWER))
     e_comp_canvas_norender_push();

   return EINA_TRUE;
}

static void
_e_vis_job_del(Eina_Clist *elem)
{
   E_Vis_Job *job;

   _e_vis_clist_unlink(elem);
   job = EINA_CLIST_ENTRY(elem, E_Vis_Job, entry);
   if ((job->type == E_VIS_JOB_TYPE_LOWER) ||
       (job->type == E_VIS_JOB_TYPE_HIDE) ||
       (job->type == E_VIS_JOB_TYPE_ICONIFY) ||
       (job->type == E_VIS_JOB_TYPE_LAYER_LOWER))
     e_comp_canvas_norender_pop();

   VS_INF(job->vc->ec, "FREE JOB:%p, type:%d", job, job->type);
   E_FREE_FUNC(job->timer, ecore_timer_del);
   free(job);
}

static void
_e_vis_job_group_del(Eina_Clist *elem)
{
   E_Vis_Job_Group *group;

   _e_vis_clist_unlink(elem);
   group = EINA_CLIST_ENTRY(elem, E_Vis_Job_Group, entry);
   _e_vis_clist_clean(&group->job_head, _e_vis_job_del);
   free(group);
}

static void
_e_vis_job_exec(Eina_Clist *elem)
{
   E_Vis_Job *job;

   _e_vis_clist_unlink(elem);
   job = EINA_CLIST_ENTRY(elem, E_Vis_Job, entry);
   _e_vis_client_job_exec(job->vc, job->type);
   VS_INF(job->vc->ec, "FREE JOB:%p, type:%d", job, job->type);
   E_FREE_FUNC(job->timer, ecore_timer_del);
   free(job);
}

static void
_e_vis_job_group_exec(E_Vis_Job_Group *group)
{
   _e_vis_clist_unlink(&group->entry);
   _e_vis_clist_clean(&group->job_head, _e_vis_job_exec);
   free(group);
}

static Eina_Bool
_e_vis_job_group_eval(E_Vis_Job_Group *group)
{
   E_Vis_Job *job, *tmp;

   EINA_CLIST_FOR_EACH_ENTRY_SAFE(job, tmp,
                                  &group->job_head, E_Vis_Job, entry)
     {
        if (_e_vis_client_is_grabbed(job->vc))
          return EINA_FALSE;
     }
   return EINA_TRUE;
}

static void
_e_vis_job_group_state_update(E_Vis_Job_Group *group)
{
   E_Vis_Job *job, *tmp;
   EINA_CLIST_FOR_EACH_ENTRY_SAFE(job, tmp,
                                  &group->job_head, E_Vis_Job, entry)
     {
        if (_e_vis_client_is_uniconify_render_running_done(job->vc))
          {
             job->vc->state = E_VIS_ICONIFY_STATE_UNICONIC;
             VS_DBG(job->vc->ec, "\tUPDATE ICONIC STATE: %s", "UNICONIC");
          }
     }
}

static void
_e_vis_job_queue_update(void)
{
   if (!pol_job_group) return;
   eina_clist_add_tail(&pol_job_group_head, &pol_job_group->entry);
   pol_job_group = NULL;
}

/* FIXME just add a ecore job, and do real evaluate in the job. */
static void
_e_vis_job_eval(void)
{
   E_Vis_Job_Group *group, *tmp;

   INF("VISIBILITY | Job Eval Begin");

   _e_vis_job_queue_update();

   EINA_CLIST_FOR_EACH_ENTRY_SAFE(group, tmp, &pol_job_group_head,
                                  E_Vis_Job_Group, entry)
     {
        /* if all of job in the group is ready */
        if (!_e_vis_job_group_eval(group))
          break;

        /* updates state to uniconic from render_done */
        _e_vis_job_group_state_update(group);

        /* execute all of job in the group */
        _e_vis_job_group_exec(group);
     }
   INF("VISIBILITY | Job Eval End");
}

static Eina_Bool
_e_vis_job_cancel(E_Vis_Client *vc, E_Vis_Job_Type type)
{
   Eina_Bool ret = EINA_FALSE;
   E_Vis_Job_Group *group, *tmp_group;
   E_Vis_Job *job, *tmp_job;

   /* update queue before deleting */
   _e_vis_job_queue_update();

   EINA_CLIST_FOR_EACH_ENTRY_SAFE(group, tmp_group,
                                  &pol_job_group_head, E_Vis_Job_Group, entry)
     {
        EINA_CLIST_FOR_EACH_ENTRY_SAFE(job, tmp_job,
                                       &group->job_head, E_Vis_Job, entry)
          {
             if (job->vc != vc) continue;
             if (job->type != type) continue;

             VS_INF(vc->ec, "Find Job:%p, type:%d", job, type);
             _e_vis_job_del(&job->entry);
             vc->job.count--;
             VS_INF(vc->ec, "Decrease VC JOB count:%d by cancel", vc->job.count);
             ret = EINA_TRUE;
          }
        if (!eina_clist_empty(&group->job_head)) continue;
        _e_vis_job_group_del(&group->entry);
     }

   /* evaluate job list after deleting an element */
   _e_vis_job_eval();

   return ret;
}

static void
_e_vis_job_del_by_client(E_Vis_Client *vc)
{
   E_Vis_Job_Group *group, *tmp_group;
   E_Vis_Job *job, *tmp_job;

   /* update queue before deleting */
   _e_vis_job_queue_update();

   EINA_CLIST_FOR_EACH_ENTRY_SAFE(group, tmp_group,
                                  &pol_job_group_head, E_Vis_Job_Group, entry)
     {
        EINA_CLIST_FOR_EACH_ENTRY_SAFE(job, tmp_job,
                                       &group->job_head, E_Vis_Job, entry)
          {
             if (job->vc != vc) continue;
             _e_vis_job_del(&job->entry);
          }
        if (!eina_clist_empty(&group->job_head)) continue;
        _e_vis_job_group_del(&group->entry);
     }

   /* evaluate job list after deleting an element */
   _e_vis_job_eval();
}

static E_Vis_Grab *
_e_vis_grab_new(E_Vis_Client *vc, const char *name, Ecore_Task_Cb timeout_func)
{
   E_VIS_ALLOC_RET_VAL(grab, E_Vis_Grab, 1, NULL);
   grab->vc = vc;
   grab->name = eina_stringshare_add(name);
   grab->timer = ecore_timer_add(E_VIS_TIMEOUT, timeout_func, grab);

   return grab;
}

static void
_e_vis_grab_del(E_Vis_Grab *grab)
{
   E_FREE_FUNC(grab->timer, ecore_timer_del);
   E_FREE_FUNC(grab->name, eina_stringshare_del);
   free(grab);
}

static void
_e_vis_grab_release(E_Vis_Grab *grab)
{
   if (!grab->deleted)
     _e_vis_client_grab_remove(grab->vc, grab);

   _e_vis_grab_del(grab);
}

static Eina_Bool
_e_vis_client_cb_buffer_attach(void *data, int type EINA_UNUSED, void *event)
{
   E_Vis_Client *vc;
   E_Vis_Grab *grab;
   E_Client *ec;
   E_Event_Client *ev;
   E_Client *provider_ec;

   ev = event;
   vc = data;
   provider_ec = e_comp_wl_remote_surface_bound_provider_ec_get(vc->ec);
   if (vc->ec != ev->ec && provider_ec != ev->ec)
     goto renew;

   ec = vc->ec;
   grab = vc->grab;

   VS_INF(ec, "FINISH Uniconify render(ev:%p, vc:%p, provider:%p)", ev->ec, vc->ec, provider_ec);

   /* force update
    * NOTE: this update can invoke some functions related to visibility grab */
   e_comp_object_damage(ec->frame, 0, 0, ec->w, ec->h);
   e_comp_object_dirty(ec->frame);
   e_comp_object_render(ec->frame);

   if (vc->grab == grab)
     vc->grab = NULL;

   E_FREE_FUNC(grab, _e_vis_grab_release);
   E_FREE_FUNC(vc->buf_attach, ecore_event_handler_del);
renew:
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_vis_client_buffer_attach_handler_add(E_Vis_Client *vc)
{
   if (vc->buf_attach)
     return;

   vc->buf_attach =
      ecore_event_handler_add(E_EVENT_CLIENT_BUFFER_CHANGE,
                              _e_vis_client_cb_buffer_attach, vc);
}

static void
_e_vis_client_job_exec(E_Vis_Client *vc, E_Vis_Job_Type type)
{
   switch (type)
     {
      case E_VIS_JOB_TYPE_ACTIVATE:
      case E_VIS_JOB_TYPE_UNICONIFY:
      case E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY:
         /* check previous state first */
         if (vc->state != E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD)
           vc->state = E_VIS_ICONIFY_STATE_UNICONIC;
         break;
      case E_VIS_JOB_TYPE_SHOW:
         vc->state = E_VIS_ICONIFY_STATE_UNICONIC;
         break;
      default:
         break;
     }

   VS_DBG(vc->ec, "\tUPDATE ICONIC STATE: %s", STATE_STR(vc));

   _e_vis_ec_job_exec(vc->ec, type);

   vc->job.count--;
   VS_INF(vc->ec, "Decrease VC JOB count:%d", vc->job.count);

   if (vc->job.count == 0)
     {
        if (e_object_is_del(E_OBJECT(vc->ec)))
          {
             /* all of enqueued job is executed */
             VS_INF(vc->ec, "Deleted Client: UNREF Delay Del");
             e_pixmap_free(vc->ec->pixmap);
             e_object_delay_del_unref(E_OBJECT(vc->ec));
          }
        E_FREE_FUNC(vc->buf_attach, ecore_event_handler_del);
     }
}

static void
_e_vis_client_grab_add(E_Vis_Client *vc, E_Vis_Grab *grab)
{
   VS_INF(vc->ec, "Add Client Visibility Grab: %s", grab->name);

   vc->job.grab_list = eina_list_append(vc->job.grab_list, grab);
}

static void
_e_vis_client_grab_remove(E_Vis_Client *vc, E_Vis_Grab *grab)
{
   VS_INF(vc->ec, "Remove Client Visibility Grab: %s", grab->name);

   if (!vc->job.grab_list)
     {
        VS_ERR(vc->ec, "The list of grab is empty");
        return;
     }

   vc->job.grab_list = eina_list_remove(vc->job.grab_list, grab);
   if (!vc->job.grab_list)
     {
        if (vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY)
          {
             vc->state = E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_RENDER_DONE;
             VS_DBG(vc->ec, "\tUPDATE ICONIC STATE: %s", STATE_STR(vc));
          }

        _e_vis_job_eval();
     }
}

static Eina_Bool
_e_vis_client_grab_cb_timeout(void *data)
{
   E_Vis_Grab *grab = data;
   VS_INF(grab->vc->ec, "TIMEOUT(%f) Grab %s", E_VIS_TIMEOUT, grab->name);
   grab->deleted = 1;
   _e_vis_client_grab_remove(grab->vc, grab);
   return ECORE_CALLBACK_DONE;
}

static E_Vis_Grab *
_e_vis_client_grab_get(E_Vis_Client *vc, const char *name)
{
   E_Vis_Grab *grab;

   VS_INF(vc->ec, "Get job grab: '%s'", name);

   grab = _e_vis_grab_new(vc, name, _e_vis_client_grab_cb_timeout);
   if (!grab)
     return NULL;

   _e_vis_client_grab_add(vc, grab);

   return grab;
}

static Eina_Bool
_e_vis_client_grab_cancel(E_Vis_Client *vc)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(vc, EINA_FALSE);

   if (_e_vis_client_is_uniconify_render_running(vc))
     {
        VS_INF(vc->ec, "Visibility changed while waiting Uniconify. Release grab.");
        E_FREE_FUNC(vc->grab, _e_vis_grab_release);
        return EINA_TRUE;
     }

   return EINA_FALSE;
}

static void
_e_vis_client_cb_evas_show(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   OBJ_EC_GET(ec, obj);
   VS_DBG(ec, "CALLBACK 'SHOW'...");
   _e_vis_update_foreground_job_queue();
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);
   if (vc->state != E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY)
     {
        vc->state = E_VIS_ICONIFY_STATE_UNICONIC;
        VS_DBG(ec, "\tUPDATE ICONIC STATE: %s", "UNICONIC");
     }
}

static void
_e_vis_client_cb_evas_hide(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   OBJ_EC_GET(ec, obj);
   VS_DBG(ec, "CALLBACK 'HIDE'...");
   _e_vis_update_foreground_job_queue();
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);
   vc->state = ec->iconic ? E_VIS_ICONIFY_STATE_ICONIC : E_VIS_ICONIFY_STATE_UNICONIC;
   VS_DBG(ec, "\tUPDATE ICONIC STATE: %s", STATE_STR(vc));
   vc->prepare_emitted = 0;

   if (ec->iconic && ec->exp_iconify.buffer_flush)
     e_pixmap_buffer_clear(ec->pixmap, EINA_FALSE);
}

static void
_e_vis_client_cb_evas_move(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Evas_Coord x, y;
   Eina_Bool visible;

   OBJ_EC_GET(ec, obj);
   evas_object_geometry_get(obj, &x, &y, NULL, NULL);
   visible = evas_object_visible_get(obj);
   VS_DBG(ec, "CALLBACK 'MOVE'... %d %d (v %d)", x, y, visible);
   if (visible) _e_vis_update_foreground_job_queue();
}

static void
_e_vis_client_cb_evas_resize(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Evas_Coord w, h;
   Eina_Bool visible;

   OBJ_EC_GET(ec, obj);
   evas_object_geometry_get(obj, NULL, NULL, &w, &h);
   visible = evas_object_visible_get(obj);
   VS_DBG(ec, "CALLBACK 'RESIZE'... %d %d (v %d)", w, h, visible);
   if (visible) _e_vis_update_foreground_job_queue();
}

static void
_e_vis_client_cb_evas_restack(void *data EINA_UNUSED, Evas *e EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Eina_Bool visible;

   OBJ_EC_GET(ec, obj);
   visible = evas_object_visible_get(obj);
   VS_DBG(ec, "CALLBACK 'RESTACK'... v %d", visible);
   if (visible) _e_vis_update_foreground_job_queue();
}

static Eina_Bool
_e_vis_client_grab_clear_cb(void *data)
{
   E_Vis_Grab *grab = data;
   E_Pol_Vis_Type above_vis_type;

   VS_INF(grab->vc->ec, "FORCE CLEAR! Grab %s, cur state:%d", grab->name, grab->vc->state);

   if (grab->vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY)
     {
        above_vis_type = _e_vis_ec_above_visible_type(grab->vc->ec, EINA_FALSE);
        if (above_vis_type != E_POL_VIS_TYPE_NON_ALPHA)
          _e_vis_ec_below_uniconify(grab->vc->ec, above_vis_type);
     }

   grab->deleted = 1;
   _e_vis_client_grab_remove(grab->vc, grab);
   return ECORE_CALLBACK_DONE;
}

void
_e_vis_client_delay_del(E_Object *obj)
{
   E_Client *ec;

   ec = (E_Client *)obj;
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);
   if (vc->job.count)
     {
        VS_DBG(ec, "REF Delay Del");
        e_pixmap_ref(ec->pixmap);
        e_object_delay_del_ref(obj);
        if (vc->grab)
          {
             E_FREE_FUNC(vc->grab->timer, ecore_timer_del);
             vc->grab->timer = ecore_timer_add(E_CLEAR_GRAB_TIMEOUT, _e_vis_client_grab_clear_cb, vc->grab);
          }
     }
}

static Eina_Bool
_e_vis_client_job_timeout(void *data)
{
   E_Vis_Job *job = data;

   VS_INF(job->vc->ec, "TIMEOUT(%f) JOB %d", E_VIS_TIMEOUT, job->type);
   /* FIXME delete all grab and evaluate it instead of exec */
   _e_vis_job_exec(&job->entry);
   _e_vis_job_eval();
   return ECORE_CALLBACK_DONE;
}

static void
_e_vis_client_job_add(E_Vis_Client *vc, E_Vis_Job_Type type)
{
   VS_DBG(vc->ec, "Add Job: type %d", type);

   if (!_e_vis_job_add(vc, type, _e_vis_client_job_timeout))
     return;

   vc->job.count++;
   VS_INF(vc->ec, "Increase VC JOB count:%d", vc->job.count);
}

static void
_e_vis_client_del(E_Vis_Client *vc)
{
   E_Vis_Grab *grab;

   VS_DBG(vc->ec, "CLIENT DEL");

   E_FREE_FUNC(vc->grab, _e_vis_grab_release);
   E_LIST_REVERSE_FREE(vc->job.grab_list, grab)
      grab->deleted = 1;

   /* if it's intended normal operation, there is no job to delete. */
   _e_vis_job_del_by_client(vc);
   /* clear event handler of E_Client */
   _e_vis_ec_reset(vc->ec);
   /* delete buffer attach handler for client */
   E_FREE_FUNC(vc->buf_attach, ecore_event_handler_del);

   free(vc);
}

static void
_e_vis_client_add(E_Client *ec)
{
   VS_DBG(ec, "CLIENT ADD");

   if (e_policy_client_is_subsurface(ec))
     return;

   E_VIS_ALLOC_RET(vc, E_Vis_Client, 1);
   vc->ec = ec;

   _e_vis_ec_setup(ec);
   eina_hash_add(pol_vis->clients_hash, &ec, vc);
}

static void
_e_vis_client_prepare_foreground_signal_emit(E_Vis_Client *vc)
{
   /* TODO should emit signal only if it's real foreground. */
   if (vc->prepare_emitted)
     return;
   vc->prepare_emitted = 1;
   evas_object_smart_callback_call(vc->ec->frame, "e,visibility,prepare,foreground", vc->ec);
}

EINTERN void
e_vis_client_send_pre_visibility_event(E_Client *ec)
{
   Eina_Bool intercepted;

   if (!ec) return;
   if ((ec->visibility.last_sent_type == E_VISIBILITY_PRE_UNOBSCURED) ||
       (ec->visibility.last_sent_type == E_VISIBILITY_UNOBSCURED))
     return;

   intercepted = e_policy_interceptor_call(E_POLICY_INTERCEPT_SEND_PRE_VISIBILITY, ec);
   if (intercepted)
     {
        ELOGF("POL_VIS", "Handled by Intercept function", ec->pixmap, ec);
        return;
     }

   e_policy_wl_visibility_send(ec, E_VISIBILITY_PRE_UNOBSCURED);
}

EINTERN void
e_vis_client_check_send_pre_visibility_event(E_Client *ec, Eina_Bool raise)
{
   if (!ec) return;

   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);

   if (_e_vis_client_check_send_pre_visibility(vc, raise))
     {
        ELOGF("POL_VIS", "SEND pre-unobscured visibility event", ec->pixmap, ec);
        e_vis_client_send_pre_visibility_event(ec);
     }
}

static Eina_Bool
_e_vis_client_is_uniconify_render_necessary(E_Vis_Client *vc)
{
   E_Client *ec = NULL;
   if (vc) ec = vc->ec;
   if (!ec) return EINA_FALSE;
   if (!ec->exp_iconify.buffer_flush)
     {
        if (ec && ec->exp_iconify.deiconify_update) // hint or conf->deiconify_approve set deiconify update 1
          goto need_deiconify_render;

        VS_INF(ec, "Not necessary deiconify rendering");
        return EINA_FALSE;
     }

need_deiconify_render:
   if (_e_vis_client_is_uniconic(vc))
     {
        VS_INF(vc->ec, "Already uniconic state");
        return EINA_FALSE;
     }

   return EINA_TRUE;
}

static Eina_Bool
_e_vis_client_check_obscured_by_children(E_Client *ec)
{
   Eina_Bool obscured = EINA_FALSE;
   E_Client *child = NULL;
   const Eina_List *l = NULL;

   EINA_LIST_FOREACH(ec->transients, l, child)
     {
        if (child->transient_policy == E_TRANSIENT_BELOW)
          continue;

        if (!child->argb)
          {
             ELOGF("POL_VIS", "Fully Obscured by child (win:%zx, child:%p)",
                   ec->pixmap, ec, e_client_util_win_get(child), child);
             obscured = EINA_TRUE;
             break;
          }
        else
          {
             if (child->visibility.opaque > 0)
               {
                  ELOGF("POL_VIS", "Fully Obscured by alpha opaque child (win:%zx, child:%p)",
                        ec->pixmap, ec, e_client_util_win_get(child), child);
                  obscured = EINA_TRUE;
                  break;
               }
          }
     }

   return obscured;
}

static Eina_Bool
_e_vis_client_check_obscured_by_same_layer(E_Client *ec)
{
   Eina_Bool obscured = EINA_FALSE;
   E_Client *above = NULL;

   for (above = e_client_above_get(ec); above; above = e_client_above_get(above))
     {
        if (above->layer > ec->layer) break;
        if (e_client_util_ignored_get(above)) continue;
        if (e_object_is_del(E_OBJECT(above))) continue;
        if (above->iconic && above->exp_iconify.by_client) continue;
        if (above->bg_state) continue;
        if (above->comp_data && !above->comp_data->mapped) continue;
        if (!E_CONTAINS(above->x, above->y, above->w, above->h, ec->x, ec->y, ec->w, ec->h)) continue;

        if (!above->argb)
          {
             ELOGF("POL_VIS", "Fully Obscured by above (win:%zx, ec:%p, layer:%d)",
                   ec->pixmap, ec, e_client_util_win_get(above), above, above->layer);
             obscured = EINA_TRUE;
             break;
          }
        else
          {
             if (above->visibility.opaque <= 0)
               continue;
             else
               {
                  ELOGF("POL_VIS", "Fully Obscured by alpha opaque above (win:%zx, ec:%p, layer:%d)",
                        ec->pixmap, ec, e_client_util_win_get(above), above, above->layer);
                  obscured = EINA_TRUE;
                  break;
               }
          }
     }

   return obscured;
}

static Eina_Bool
_e_vis_client_check_obscured_by_above_layers(E_Client *ec)
{
   Eina_Bool obscured = EINA_FALSE;
   E_Client *above = NULL;

   for (above = e_client_above_get(ec); above; above = e_client_above_get(above))
     {
        if (above->layer <= ec->layer) continue;
        if (e_client_util_ignored_get(above)) continue;
        if (e_object_is_del(E_OBJECT(above))) continue;
        if (above->iconic && above->exp_iconify.by_client) continue;
        if (above->bg_state) continue;
        if (above->comp_data && !above->comp_data->mapped) continue;
        if (!E_CONTAINS(above->x, above->y, above->w, above->h, ec->x, ec->y, ec->w, ec->h)) continue;

        if (!above->argb)
          {
             ELOGF("POL_VIS", "Fully Obscured by above (win:%zx, ec:%p, layer:%d)",
                   ec->pixmap, ec, e_client_util_win_get(above), above, above->layer);
             obscured = EINA_TRUE;
             break;
          }
        else
          {
             if (above->visibility.opaque <= 0)
               continue;
             else
               {
                  ELOGF("POL_VIS", "Fully Obscured by alpha opaque above (win:%zx, ec:%p, layer:%d)",
                        ec->pixmap, ec, e_client_util_win_get(above), above, above->layer);
                  obscured = EINA_TRUE;
                  break;
               }
          }
     }

   return obscured;
}

static Eina_Bool
_e_vis_client_check_send_pre_visibility(E_Vis_Client *vc, Eina_Bool raise)
{
   Eina_Bool send_vis_event = EINA_TRUE;
   E_Client *ec;

   ec = vc->ec;

   if (_e_vis_client_is_uniconic(vc))
     return EINA_FALSE;

   if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
     return EINA_FALSE;

   // check all windows on above layers, if obscured by above then return FALSE
   if (_e_vis_client_check_obscured_by_above_layers(ec))
     {
        ELOGF("POL_VIS", "DO NOT Need to SEND pre-visibility.. obscured by above (above layer)", ec->pixmap, ec);
        return EINA_FALSE;
     }

   if (!raise)
     {
        // check above windows in same layer
        if (_e_vis_client_check_obscured_by_same_layer(ec))
          {
             ELOGF("POL_VIS", "DO NOT Need to SEND pre-visibility.. obscured by above (same layer)", ec->pixmap, ec);
             send_vis_event = EINA_FALSE;
          }
     }
   else
     {
        if (ec->transients)
          {
             // check children windows
             if (_e_vis_client_check_obscured_by_children(ec))
               {
                  ELOGF("POL_VIS", "DO NOT Need to SEND pre-visibility.. obscured by child", ec->pixmap, ec);
                  send_vis_event = EINA_FALSE;
               }
          }
     }

   return send_vis_event;
}

static Eina_Bool
_e_vis_client_add_uniconify_render_pending(E_Vis_Client *vc, E_Vis_Job_Type type, Eina_Bool raise, Eina_Bool force_send)
{
   E_Client *ec;
   Eina_Bool send_vis_event = EINA_TRUE;

   ec = vc->ec;

   if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
     {
        VS_DBG(ec, "Display off. no need to uniconify render.");
        return EINA_FALSE;
     }

   if (ec->comp_data && !ec->comp_data->mapped)
     {
        ELOGF("POL_VIS", "Not mapped. no need to uniconify render.", ec->pixmap, ec);
        return EINA_FALSE;
     }

   /* do not add child window which is obscured by 24 bit parent window */
   if ((ec->parent) &&
       !(ec->parent->argb) &&
       (ec->transient_policy == E_TRANSIENT_BELOW))
     {
        VS_DBG(ec, "Transient below. no need to uniconify render.");
        return EINA_FALSE;
     }

   if (_e_vis_client_is_uniconify_render_running(vc))
     goto end;

   if (!force_send)
     send_vis_event = _e_vis_client_check_send_pre_visibility(vc, raise);

   if (send_vis_event)
     {
        ELOGF("POL_VIS", "SEND pre-unobscured visibility event", ec->pixmap, ec);
        e_vis_client_send_pre_visibility_event(ec);
     }

   if (!_e_vis_client_is_uniconify_render_necessary(vc))
     return EINA_FALSE;

   if (_e_vis_client_is_uniconify_render_running_done(vc))
     goto end;

   ec->exp_iconify.not_raise = !raise;

   VS_DBG(ec, "BEGIN Uniconify render: raise %d", raise);

   _e_vis_client_prepare_foreground_signal_emit(vc);
   vc->state = E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY;
   VS_DBG(vc->ec, "\tUPDATE ICONIC STATE: %s", STATE_STR(vc));
   vc->grab = _e_vis_client_grab_get(vc, __func__);
   _e_vis_client_buffer_attach_handler_add(vc);

   e_policy_wl_iconify_state_change_send(ec, 0);

end:
   _e_vis_client_job_add(vc, type);

  return EINA_TRUE;
}

static Eina_Bool
_e_vis_client_defer_move(E_Vis_Client *vc, E_Vis_Job_Type type)
{
   if (!vc) return EINA_FALSE;

   VS_DBG(vc->ec, "\tDEFER MOVE: %s", STATE_STR(vc));
   vc->grab = _e_vis_client_grab_get(vc, __func__);
   _e_vis_client_buffer_attach_handler_add(vc);

   _e_vis_client_job_add(vc, type);

   return EINA_TRUE;
}

static inline Eina_Bool
_e_vis_ec_special_check(E_Client *ec)
{
   return (e_policy_client_is_quickpanel(ec) ||
           e_policy_client_is_subsurface(ec) ||
           e_policy_client_is_keyboard(ec) ||
           e_policy_client_is_keyboard_sub(ec) ||
           e_policy_client_is_floating(ec));
}

static inline Eina_Bool
_e_vis_ec_size_is_full(E_Client *ec)
{
   return ((ec->x == ec->zone->x) && (ec->y == ec->zone->y) &&
           (ec->w == ec->zone->w) && (ec->h == ec->zone->h));
}

static Eina_Bool
_e_vis_ec_activity_check(E_Client *ec, Eina_Bool check_alpha)
{
   int x, y, w, h;
   E_Comp_Object_Content_Type type = E_COMP_OBJECT_CONTENT_TYPE_NONE;
   int check_mapped = 1;

   /* check if ignored */
   if (e_client_util_ignored_get(ec)) return EINA_FALSE;
   /* check transparent */
   if (check_alpha && ((ec->argb) && (ec->visibility.opaque <= 0))) return EINA_FALSE;
   /* check deleted client */
   if (e_object_is_del(E_OBJECT(ec))) return EINA_FALSE;
   /* check launchscreen */
   if (ec->frame) type = e_comp_object_content_type_get(ec->frame);
   if ((type == E_COMP_OBJECT_CONTENT_TYPE_EXT_IMAGE) ||
       (type == E_COMP_OBJECT_CONTENT_TYPE_EXT_EDJE))
     check_mapped = 0;
   /* check unmapped client */
   if (check_mapped && ec->comp_data && !ec->comp_data->mapped) return EINA_FALSE;
   /* check iconify window by client */
   if ((ec->iconic) && (ec->exp_iconify.by_client)) return EINA_FALSE;
   /* check background state */
   if (ec->bg_state) return EINA_FALSE;
   /* check special client */
   if (_e_vis_ec_special_check(ec)) return EINA_FALSE;
   /* check if full screen */
   e_client_geometry_get(ec, &x, &y, &w, &h);
   if (!E_CONTAINS(x, y, w, h, ec->desk->geom.x, ec->desk->geom.y, ec->desk->geom.w, ec->desk->geom.h))
     return EINA_FALSE;
   /* check if obscured by force */
   if (ec->visibility.force_obscured)
     return EINA_FALSE;

   return EINA_TRUE;
}

static void
_e_vis_ec_job_exec(E_Client *ec, E_Vis_Job_Type type)
{
   VS_INF(ec, "Job Run: type %d", type);

   E_Vis_Client *vc = NULL;
   if (ec)
     {
        if (EINA_LIKELY(pol_vis != NULL))
          {
             vc = eina_hash_find(pol_vis->clients_hash, &ec);
          }
     }

   switch (type)
     {
      case E_VIS_JOB_TYPE_ACTIVATE:
         if (!ec) break;
         e_client_activate(ec, 1);
         if (e_policy_client_is_lockscreen(ec))
           {
              if (E_CONTAINS(ec->x, ec->y, ec->w, ec->h, ec->zone->x, ec->zone->y, ec->zone->w, ec->zone->h))
                e_policy_stack_clients_restack_above_lockscreen(ec, EINA_TRUE);
           }
         else
           e_policy_stack_check_above_lockscreen(ec, ec->layer, NULL, EINA_TRUE);
         break;
      case E_VIS_JOB_TYPE_UNICONIFY:
         e_client_uniconify(ec);
         break;
      case E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY:
         _e_policy_client_uniconify_by_visibility(ec);
         break;
      case E_VIS_JOB_TYPE_LOWER:
         e_comp_canvas_norender_pop();
         if (ec)
           {
              evas_object_lower(ec->frame);
              if (ec->focused)
                e_client_revert_focus(ec);
           }
         break;
      case E_VIS_JOB_TYPE_HIDE:
         e_comp_canvas_norender_pop();
         if (ec) evas_object_hide(ec->frame);
         break;
      case E_VIS_JOB_TYPE_LAYER_LOWER:
         e_comp_canvas_norender_pop();
         if (vc) evas_object_layer_set(ec->frame, vc->layer);
         break;
      case E_VIS_JOB_TYPE_ICONIFY:
         e_comp_canvas_norender_pop();
         e_client_iconify(ec);
         break;
      case E_VIS_JOB_TYPE_SHOW:
         /* checks for dectecting hide request after show request */
         if ((ec) && (!e_object_is_del(E_OBJECT(ec))) &&
             (ec->visible) && (!ec->hidden) &&
             (!ec->iconic) && (!ec->ignored))
           evas_object_show(ec->frame);

         if ((vc) && (vc->wait_for_child))
           {
              e_comp_object_signal_callback_del_full(vc->wait_for_child->frame,
                                                     "e,action,launch,done", "e",
                                                     _e_vis_cb_child_launch_done, vc);
              e_pixmap_free(vc->wait_for_child->pixmap);
              e_object_delay_del_unref(E_OBJECT(vc->wait_for_child));
              vc->wait_for_child = NULL;
           }
         break;
      case E_VIS_JOB_TYPE_DEFER_MOVE:
         /* handle defered job regarding move */
         if (vc &&(!e_object_is_del(E_OBJECT(ec))) &&
             (ec->visible) && (!ec->hidden) &&
             (!ec->iconic) && (!ec->ignored))
           evas_object_move(ec->frame, ec->x, ec->y);
         break;

      default:
         VS_ERR(ec, "Unkown job type: %d", type);
         break;
     }
}

static E_Pol_Vis_Type
_e_vis_ec_above_visible_type(E_Client *ec, Eina_Bool check_child)
{
   E_Client *above;
   E_Pol_Vis_Type above_vis_type = E_POL_VIS_TYPE_ALPHA;

   for (above = e_client_above_get(ec); above; above = e_client_above_get(above))
     {
        if (e_client_util_ignored_get(above)) continue;
        if (!E_CONTAINS(above->x, above->y, above->w, above->h, ec->x, ec->y, ec->w, ec->h)) continue;
        if (check_child && (above->parent == ec)) continue;
        if (above->first_mapped)
          {
             if (above->comp_data && !above->comp_data->mapped)
               continue;
          }

        if (above->iconic && above->exp_iconify.by_client)
          continue;

        if (above->bg_state)
          continue;

        if (above->visibility.obscured == E_VISIBILITY_UNOBSCURED)
          {
             if (!above->argb)
               above_vis_type = E_POL_VIS_TYPE_NON_ALPHA;
             else
               {
                  if (above->visibility.opaque > 0)
                    above_vis_type = E_POL_VIS_TYPE_ALPHA_OPAQUE;
               }
          }
        else
          {
             if (!above->visible)
               continue;

             above_vis_type = E_POL_VIS_TYPE_NON_ALPHA;
          }

        break;
     }

   return above_vis_type;
}

static void
_e_vis_ec_below_activity_clients_get(E_Client *ec, Eina_List **below_list)
{
   E_Client *below;

   for (below = e_client_below_get(ec); below; below = e_client_below_get(below))
     {
        if (!_e_vis_ec_activity_check(below, EINA_FALSE)) continue;

        E_VIS_CLIENT_GET(vc, below);
        if (!vc) continue;

        if ((below->transient_policy == E_TRANSIENT_BELOW) &&
            (below->parent == ec))
          continue;

        *below_list = eina_list_prepend(*below_list, vc);
        if ((below->argb) && (below->visibility.opaque <= 0))
          continue;
        else
          break;
     }
}

static Eina_Bool
_e_vis_ec_below_uniconify(E_Client *ec, E_Pol_Vis_Type above_vis_type)
{
   Eina_List *below_list = NULL;
   Eina_Bool ret = EINA_FALSE;
   E_Vis_Client *below;
   Eina_Bool send_vis;

   if (!ec) return EINA_FALSE;

   if (ec->zone)
     {
        if (ec->zone->display_state == E_ZONE_DISPLAY_STATE_OFF)
          return EINA_FALSE;
     }

   /* find below activity clients */
   _e_vis_ec_below_activity_clients_get(ec, &below_list);
   if (!below_list)
     {
        VS_INF(ec, "There is NO below activity");
        return EINA_FALSE;
     }
   else
     {
        if (above_vis_type == E_POL_VIS_TYPE_ALPHA_OPAQUE)
          send_vis = EINA_FALSE;
        else
          send_vis = EINA_TRUE;

        EINA_LIST_FREE(below_list, below)
          {
             Eina_Bool job_added = EINA_FALSE;
             E_Client *below_ec = below->ec;

             if (below_ec == ec->parent)
               {
                  /* Check if its parent is waiting for a child's uniconify.
                   * if so cancel the waiting now.
                   */
                  if (below->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD)
                    {
                       E_FREE_FUNC(below->grab, _e_vis_grab_release);
                       continue;
                    }

                  ELOGF("POL_VIS", "SEND pre-unobscured visibility event", below_ec->pixmap, below_ec);
                  e_vis_client_send_pre_visibility_event(below_ec);
               }

             job_added = _e_vis_client_add_uniconify_render_pending(below, E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY, 0, send_vis);

             if (!job_added)
               {
                  if ((below_ec->iconic) && (!below_ec->exp_iconify.by_client))
                    {
                       // show evas obj if uniconify pending is not necessary
                       VS_DBG(below_ec, "Show below iconic client in advance");
                       evas_object_show(below_ec->frame);
                    }
               }

             ret |= job_added;
          }
     }

   return ret;
}

static void
_e_vis_ec_setup(E_Client *ec)
{
   e_object_delay_del_set(E_OBJECT(ec), _e_vis_client_delay_del);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_SHOW,     _e_vis_client_cb_evas_show,     NULL);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_HIDE,     _e_vis_client_cb_evas_hide,     NULL);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_MOVE,     _e_vis_client_cb_evas_move,     NULL);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESIZE,   _e_vis_client_cb_evas_resize,   NULL);
   evas_object_event_callback_add(ec->frame, EVAS_CALLBACK_RESTACK,  _e_vis_client_cb_evas_restack,  NULL);
}

static void
_e_vis_ec_reset(E_Client *ec)
{
   e_object_delay_del_set(E_OBJECT(ec), NULL);
   evas_object_event_callback_del(ec->frame, EVAS_CALLBACK_SHOW,     _e_vis_client_cb_evas_show);
   evas_object_event_callback_del(ec->frame, EVAS_CALLBACK_HIDE,     _e_vis_client_cb_evas_hide);
   evas_object_event_callback_del(ec->frame, EVAS_CALLBACK_MOVE,     _e_vis_client_cb_evas_move);
   evas_object_event_callback_del(ec->frame, EVAS_CALLBACK_RESIZE,   _e_vis_client_cb_evas_resize);
   evas_object_event_callback_del(ec->frame, EVAS_CALLBACK_RESTACK,  _e_vis_client_cb_evas_restack);
}

static void
_e_vis_hook_new_client_post(void *data EINA_UNUSED, E_Client *ec)
{
   _e_vis_client_add(ec);
}

static void
_e_vis_client_remove(E_Client *ec)
{
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);
   eina_hash_del_by_key(pol_vis->clients_hash, &ec);

   if (pol_vis->activity == ec)
     pol_vis->activity = NULL;

   if (!stopping)
     _e_vis_update_foreground_job_queue();
}

static void
_e_vis_hook_client_del(void *data EINA_UNUSED, E_Client *ec)
{
   if (!ec->new_client) return;
   _e_vis_client_remove(ec);
}

static Eina_Bool
_e_vis_cb_client_remove(void *data EINA_UNUSED, int type EINA_UNUSED, void *event)
{
   E_Event_Client *ev;

   ev = event;
   _e_vis_client_remove(ev->ec);

   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_vis_cb_child_launch_done(void *data, Evas_Object *obj, const char *signal, const char *source)
{
   E_Vis_Client *vc;
   Evas_Object *comp_obj;

   vc = data;
   comp_obj = evas_object_data_get(obj, "comp_smart_obj");

   E_FREE_FUNC(vc->grab, _e_vis_grab_release);
   if (comp_obj)
     e_comp_object_signal_callback_del_full(comp_obj, "e,action,launch,done", "e", _e_vis_cb_child_launch_done, vc);
}

static void
_e_vis_transient_group_make(E_Client *ec, Eina_List **list)
{
   E_Client *child;
   Eina_List *l;

   if (!ec) return;

   if (e_config->transient.raise)
     {
        EINA_LIST_FOREACH(ec->transients, l, child)
          {
             if (!child) continue;
             if (child->iconic && ec->exp_iconify.by_client)
               continue;
             if (child->bg_state)
               continue;

             if (child->transient_policy == E_TRANSIENT_ABOVE)
               {
                  *list = eina_list_prepend(*list, child);
                  _e_vis_transient_group_make(child, list);
               }
          }
     }
}

static E_Client *
_e_vis_transient_top_get(E_Client *ec)
{
   E_Client *topmost = NULL;
   Eina_List *transient_list = NULL;

   _e_vis_transient_group_make(ec, &transient_list);

   if (transient_list)
     {
        topmost = eina_list_data_get(transient_list);
        eina_list_free(transient_list);
     }
   return topmost;
}

static Eina_Bool
_e_vis_intercept_show(void *data EINA_UNUSED, E_Client *ec)
{
   VS_DBG(ec, "INTERCEPT SHOW: new_client %d size %d %d",
          ec->new_client, ec->w, ec->h);

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_TRUE);

   if (vc->state == E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD)
     return EINA_FALSE;

   if (ec->transients)
     {
        E_Client *topmost;

        topmost = _e_vis_transient_top_get(ec);
        if (!topmost) return EINA_TRUE;

        if (topmost->transient_policy == E_TRANSIENT_BELOW)
          return EINA_TRUE;

        /* allow show if topmost child is alpha window or not fully cover region of ec */
        if ((topmost->argb) || !(E_CONTAINS(topmost->x, topmost->y, topmost->w, topmost->h, ec->x, ec->y, ec->w, ec->h)))
          return EINA_TRUE;

        /* do not show until child is shown */
        if (e_config->transient.iconify)
          {
             /* wait for child's uniconify */
             if (ec->iconic && topmost->iconic)
               {
                  if (topmost->pixmap && e_pixmap_usable_get(topmost->pixmap))
                    {
                       if (e_policy_visibility_client_is_uniconic(topmost))
                         {
                            ELOGF("COMP", "Already child(win:0x%08zx ec:%p) uniconify_render done..", ec->pixmap, ec, e_client_util_win_get(topmost), topmost);
                            return EINA_TRUE;
                         }

                       ELOGF("COMP", "Set launching flag..", topmost->pixmap, topmost);
                       topmost->launching = EINA_TRUE;

                       vc->state = E_VIS_ICONIFY_STATE_RUNNING_UNICONIFY_WAITING_FOR_CHILD;
                       VS_DBG(vc->ec, "\tUPDATE ICONIC STATE: %s", STATE_STR(vc));
                       vc->grab = _e_vis_client_grab_get(vc, __func__);
                       e_comp_object_signal_callback_add(topmost->frame,
                                                         "e,action,launch,done",
                                                         "e",
                                                         _e_vis_cb_child_launch_done,
                                                         vc);

                       /* do not allow child's removal */
                       vc->wait_for_child = topmost;
                       e_pixmap_ref(topmost->pixmap);
                       e_object_delay_del_ref(E_OBJECT(topmost));
                       _e_vis_client_job_add(vc, E_VIS_JOB_TYPE_SHOW);
                       return EINA_FALSE;
                    }
               }
          }

     }
   return EINA_TRUE;
}

static Eina_Bool
_e_vis_intercept_hide(void *data EINA_UNUSED, E_Client *ec)
{
   E_Pol_Vis_Type above_vis_type;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_TRUE);

   VS_DBG(ec, "INTERCEPTOR HIDE");

   if (_e_vis_job_find(vc, E_VIS_JOB_TYPE_HIDE))
     {
        VS_INF(ec, "Already Pending HIDE...");
        return EINA_FALSE;
     }

   if (_e_vis_client_is_below_uniconify_skip(vc))
     {
        VS_DBG(ec, "Skip to uniconify below client");
        return EINA_TRUE;
     }

   above_vis_type = _e_vis_ec_above_visible_type(ec, EINA_FALSE);
   if (above_vis_type == E_POL_VIS_TYPE_NON_ALPHA)
     {
        VS_DBG(ec, "Obscured by above window.");
        return EINA_TRUE;
     }

   if (!_e_vis_ec_below_uniconify(ec, above_vis_type))
     {
        VS_DBG(ec, "Failed to uniconify below client");
        return EINA_TRUE;
     }

   /* add lower job, it will be executed after below activity client finishs updating */
   _e_vis_client_job_add(vc, E_VIS_JOB_TYPE_HIDE);

   return EINA_FALSE;
}

static Eina_Bool
_e_vis_idle_enter(void *data)
{
   _e_vis_job_queue_update();
   return ECORE_CALLBACK_PASS_ON;
}

static void
_e_vis_event_init(void)
{
   E_LIST_HOOK_APPEND(pol_vis->hooks, E_CLIENT_HOOK_NEW_CLIENT_POST, _e_vis_hook_new_client_post, NULL);
   E_LIST_HOOK_APPEND(pol_vis->hooks, E_CLIENT_HOOK_DEL, _e_vis_hook_client_del, NULL);

   E_LIST_HANDLER_APPEND(pol_vis->handlers, E_EVENT_CLIENT_REMOVE,   _e_vis_cb_client_remove, NULL);

   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(pol_vis->interceptors, E_COMP_OBJECT_INTERCEPT_HOOK_SHOW_HELPER,  _e_vis_intercept_show, NULL);
   E_COMP_OBJECT_INTERCEPT_HOOK_APPEND(pol_vis->interceptors, E_COMP_OBJECT_INTERCEPT_HOOK_HIDE,         _e_vis_intercept_hide, NULL);

   pol_vis->idle_enter = ecore_idle_enterer_add(_e_vis_idle_enter, NULL);
}

E_API E_Client*
e_policy_visibility_fg_activity_get(void)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(pol_vis, NULL);
   return pol_vis->activity;
}

E_API Eina_List*
e_policy_visibility_foreground_clients_get(void)
{
   if (!pol_vis->fg_clients) return NULL;
   return eina_list_clone(pol_vis->fg_clients);
}

E_API Eina_Bool
e_policy_visibility_client_is_activity(E_Client *ec)
{
   EINA_SAFETY_ON_NULL_RETURN_VAL(ec, EINA_FALSE);
   return _e_vis_ec_activity_check(ec, EINA_TRUE);
}

E_API E_Vis_Grab *
e_policy_visibility_client_grab_get(E_Client *ec, const char *name)
{
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, NULL);
   if (!name)
     {
        VS_ERR(ec, "Trying to get the grab without name used hint");
        return NULL;
     }
   return _e_vis_client_grab_get(vc, name);
}

E_API void
e_policy_visibility_client_grab_release(E_Vis_Grab *grab)
{
   EINA_SAFETY_ON_NULL_RETURN(grab);
   _e_vis_grab_release(grab);
}

E_API Eina_Bool
e_policy_visibility_client_grab_cancel(E_Client *ec)
{
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);
   return _e_vis_client_grab_cancel(vc);
}

E_API Eina_Bool
e_policy_visibility_client_hide_job_cancel(E_Client *ec)
{
   if (!ec) return EINA_FALSE;
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_INF(ec, "Find and Cancel HIDE job...");
   return _e_vis_job_cancel(vc, E_VIS_JOB_TYPE_HIDE);
}

E_API Eina_Bool
e_policy_visibility_client_raise(E_Client *ec)
{
   E_Client *child;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;


   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_DBG(ec, "API ENTRY | RAISE");

   if (!ec->iconic && !ec->exp_iconify.deiconify_update)
     return EINA_FALSE;

   if (ec->exp_iconify.by_client)
     return EINA_FALSE;

   ret = _e_vis_client_add_uniconify_render_pending(vc, E_VIS_JOB_TYPE_UNICONIFY, 1, EINA_FALSE);

   /* uniconify its transients recursively */
   if (e_config->transient.raise)
     {
        l = eina_list_clone(ec->transients);

        EINA_LIST_FREE(l, child)
           ret |= e_policy_visibility_client_raise(child);
     }

   /* TODO find topmost activity client and emit signal */

   return ret;
}

E_API Eina_Bool
e_policy_visibility_client_lower(E_Client *ec)
{
   E_Pol_Vis_Type above_vis_type;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_DBG(ec, "API ENTRY | LOWER");

   /* if vc has job grab, release them */
   _e_vis_client_grab_cancel(vc);

   above_vis_type = _e_vis_ec_above_visible_type(ec, EINA_TRUE);
   if (above_vis_type == E_POL_VIS_TYPE_NON_ALPHA)
     {
        VS_DBG(ec, "Obscured by above window.");
        return EINA_FALSE;
     }

   if (!_e_vis_ec_below_uniconify(ec, above_vis_type))
     {
        VS_DBG(ec, "Failed to uniconify below client");
        return EINA_FALSE;
     }

   /* add lower job, it will be executed after below activity client finishs updating */
   _e_vis_client_job_add(vc, E_VIS_JOB_TYPE_LOWER);

   return EINA_TRUE;
}

E_API Eina_Bool
e_policy_visibility_client_iconify(E_Client *ec)
{
   E_Pol_Vis_Type above_vis_type;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_DBG(ec, "API ENTRY | ICONIFY");

   /* if vc has job grab, release them */
   _e_vis_client_grab_cancel(vc);

   if (ec->iconic) return EINA_FALSE;

   above_vis_type = _e_vis_ec_above_visible_type(ec, EINA_TRUE);
   if (above_vis_type == E_POL_VIS_TYPE_NON_ALPHA)
     {
        VS_DBG(ec, "Obscured by above window.");
        return EINA_FALSE;
     }

   if (!_e_vis_ec_below_uniconify(ec, above_vis_type))
     {
        VS_DBG(ec, "Failed to uniconify below client");
        return EINA_FALSE;
     }

   /* add lower job, it will be executed after below activity client finishs updating */
   _e_vis_client_job_add(vc, E_VIS_JOB_TYPE_ICONIFY);

   return EINA_TRUE;
}

E_API Eina_Bool
e_policy_visibility_client_uniconify(E_Client *ec, Eina_Bool raise)
{
   E_Client *child;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);
   if (!ec->iconic && !ec->exp_iconify.deiconify_update)
     return EINA_FALSE;

   if (!ec->visible)
     {
        ELOGF("POL", "UNICONIFY. but NOT MAPPED. So skip...", ec->pixmap, ec);
        return EINA_FALSE;
     }

   VS_DBG(ec, "API ENTRY | UNICONIFY");

   /* TODO search clients to be really foreground and uniconify it.
    * suppose that transients will be above on the parent. */

   if (raise)
     ret = _e_vis_client_add_uniconify_render_pending(vc, E_VIS_JOB_TYPE_UNICONIFY, raise, EINA_FALSE);
   else
     ret = _e_vis_client_add_uniconify_render_pending(vc, E_VIS_JOB_TYPE_UNICONIFY_BY_VISIBILITY, raise, EINA_FALSE);

   /* uniconify its transients recursively */
   if (e_config->transient.iconify)
     {
        l = eina_list_clone(ec->transients);

        EINA_LIST_FREE(l, child)
           ret |= e_policy_visibility_client_uniconify(child, raise);
     }

   /* TODO find topmost activity client and emit signal */

   return ret;
}

E_API Eina_Bool
e_policy_visibility_client_activate(E_Client *ec)
{
   E_Client *child;
   Eina_List *l;
   Eina_Bool ret = EINA_FALSE;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_DBG(ec, "API ENTRY | ACTIVATE");

   ret = _e_vis_client_add_uniconify_render_pending(vc, E_VIS_JOB_TYPE_ACTIVATE, 1, EINA_FALSE);

   /* TODO search clients to be foreground
    * suppose that transients will be above on the parent. */

   /* uniconify its transients recursively */
   if (e_config->transient.iconify)
     {
        l = eina_list_clone(ec->transients);

        EINA_LIST_FREE(l, child)
           ret |= e_policy_visibility_client_uniconify(child, EINA_TRUE);
     }

   /* TODO find topmost activity client and emit signal */

   return ret;
}

E_API Eina_Bool
e_policy_visibility_client_layer_lower(E_Client *ec, E_Layer layer)
{
   E_Pol_Vis_Type above_vis_type;

   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   VS_DBG(ec, "API ENTRY | LAYER LOWER (layer:%d)", layer);

   /* if vc has job grab, release them */
   _e_vis_client_grab_cancel(vc);

   above_vis_type = _e_vis_ec_above_visible_type(ec, EINA_TRUE);
   if (above_vis_type == E_POL_VIS_TYPE_NON_ALPHA)
     {
        VS_DBG(ec, "Obscured by above window.");
        return EINA_FALSE;
     }

   if (!_e_vis_ec_below_uniconify(ec, above_vis_type))
     {
        VS_DBG(ec, "Failed to uniconify below client");
        return EINA_FALSE;
     }

   /* add lower lower job, it will be executed after below activity client finishs updating */
   vc->layer = layer;
   _e_vis_client_job_add(vc, E_VIS_JOB_TYPE_LAYER_LOWER);

   return EINA_TRUE;
}

EINTERN void
e_policy_visibility_client_defer_move(E_Client *ec)
{
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);
   VS_DBG(ec, "API ENTRY | Defered Move");

   _e_vis_client_defer_move(vc, E_VIS_JOB_TYPE_DEFER_MOVE);
}

E_API Eina_Bool
e_policy_visibility_client_is_iconic(E_Client *ec)
{
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   return _e_vis_client_is_iconic(vc);
}

EINTERN Eina_Bool
e_policy_visibility_client_is_uniconic(E_Client *ec)
{
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   return _e_vis_client_is_uniconic(vc);
}

E_API Eina_Bool
e_policy_visibility_client_is_uniconify_render_running(E_Client *ec)
{
   Eina_Bool running = EINA_FALSE;
   E_VIS_CLIENT_GET_OR_RETURN_VAL(vc, ec, EINA_FALSE);

   if ((_e_vis_client_is_uniconify_render_running(vc)) ||
       (_e_vis_client_is_uniconify_render_running_done(vc)))
     running = EINA_TRUE;

   return running;
}

E_API void
e_policy_visibility_client_below_uniconify_skip_set(E_Client *ec, Eina_Bool skip)
{
   E_VIS_CLIENT_GET_OR_RETURN(vc, ec);

   _e_vis_client_below_uniconify_skip_set(vc, skip);
}

E_API E_Pol_Vis_Hook *
e_policy_visibility_hook_add(E_Pol_Vis_Hook_Type type, E_Pol_Vis_Hook_Cb cb, const void *data)
{
   E_Pol_Vis_Hook *h;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(type >= E_POL_VIS_HOOK_TYPE_LAST, NULL);

   h = E_NEW(E_Pol_Vis_Hook, 1);
   EINA_SAFETY_ON_NULL_RETURN_VAL(h, NULL);

   h->type = type;
   h->cb = cb;
   h->data = (void *)data;

   _e_pol_vis_hooks[type] = eina_inlist_append(_e_pol_vis_hooks[type],
                                               EINA_INLIST_GET(h));

   return h;
}

E_API void
e_policy_visibility_hook_del(E_Pol_Vis_Hook *h)
{
   h->delete_me = EINA_TRUE;
   if (_e_pol_vis_hooks_walking == 0)
     {
        _e_pol_vis_hooks[h->type] = eina_inlist_remove(_e_pol_vis_hooks[h->type],
                                                       EINA_INLIST_GET(h));
        E_FREE(h);
     }
   else
     _e_pol_vis_hooks_delete++;
}

E_API Eina_Bool
e_policy_visibility_init(void)
{
   E_Client *ec;

   INF("Init Visibility Module");
   if (pol_vis)
     return EINA_TRUE;

   pol_vis = E_NEW(E_Vis, 1);
   if (!pol_vis)
     {
        ERR("Failed to allocate 'E_Vis'");
        return EINA_FALSE;
     }

   pol_vis->clients_hash = eina_hash_pointer_new((Eina_Free_Cb)_e_vis_client_del);

   E_CLIENT_REVERSE_FOREACH(ec)
      _e_vis_client_add(ec);

   _e_vis_event_init();
   _e_vis_update_forground_list();

   return EINA_TRUE;
}

E_API void
e_policy_visibility_shutdown(void)
{
   INF("Shutdown Visibility Module");

   if (!pol_vis)
     return;

   _e_vis_clist_clean(&pol_job_group_head, _e_vis_job_group_del);

   E_FREE_FUNC(pol_vis->clients_hash, eina_hash_free);
   E_FREE_FUNC(pol_vis->idle_enter, ecore_idle_enterer_del);
   E_FREE_LIST(pol_vis->hooks, e_client_hook_del);
   E_FREE_LIST(pol_vis->handlers, ecore_event_handler_del);
   E_FREE_LIST(pol_vis->interceptors, e_comp_object_intercept_hook_del);
   E_FREE(pol_vis);
}
