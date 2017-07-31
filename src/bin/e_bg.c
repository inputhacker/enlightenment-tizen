#include "e.h"

/* local subsystem functions */
static void _e_bg_event_bg_update_free(void *data, void *event);

/* local subsystem globals */
E_API int E_EVENT_BG_UPDATE = 0;
//static E_Fm2_Mime_Handler *bg_hdl = NULL;

/* externally accessible functions */
EINTERN int
e_bg_init(void)
{
   E_EVENT_BG_UPDATE = ecore_event_type_new();

   return 1;
}

EINTERN int
e_bg_shutdown(void)
{
   return 1;
}

/**
 * Find the configuration for a given desktop background
 * Use -1 as a wild card for each parameter.
 * The most specific match will be returned
 */
E_API const E_Config_Desktop_Background *
e_bg_config_get(int zone_num, int desk_x, int desk_y)
{
   Eina_List *l, *entries;
   E_Config_Desktop_Background *bg = NULL, *cfbg = NULL;
   const char *bgfile = "";
   char *entry;
   int current_spec = 0; /* how specific the setting is - we want the least general one that applies */

   /* look for desk specific background. */
   if (zone_num >= 0 || desk_x >= 0 || desk_y >= 0)
     {
        EINA_LIST_FOREACH(e_config->desktop_backgrounds, l, cfbg)
          {
             int spec;

             if (!cfbg) continue;
             spec = 0;
             if (cfbg->zone == zone_num) spec++;
             else if (cfbg->zone >= 0)
               continue;
             if (cfbg->desk_x == desk_x) spec++;
             else if (cfbg->desk_x >= 0)
               continue;
             if (cfbg->desk_y == desk_y) spec++;
             else if (cfbg->desk_y >= 0)
               continue;

             if (spec <= current_spec) continue;
             bgfile = cfbg->file;
             if (bgfile)
               {
                  if (bgfile[0] != '/')
                    {
                       const char *bf;

                       bf = e_path_find(path_backgrounds, bgfile);
                       if (bf) bgfile = bf;
                    }
               }
             if (eina_str_has_extension(bgfile, ".edj"))
               {
                  entries = edje_file_collection_list(bgfile);
                  EINA_LIST_FREE(entries, entry)
                    {
                       if (!strcmp(entry, "e/desktop/background"))
                         {
                            bg = cfbg;
                            current_spec = spec;
                            break;
                         }
                       eina_stringshare_del(entry);
                    }
                  E_FREE_LIST(entries, eina_stringshare_del);
               }
             else
               {
                  bg = cfbg;
                  current_spec = spec;
               }
          }
     }
   return bg;
}

E_API Eina_Stringshare *
e_bg_file_get(int zone_num, int desk_x, int desk_y)
{
   const E_Config_Desktop_Background *cfbg;
   const char *bgfile = NULL;
   int ok = 0;

   cfbg = e_bg_config_get(zone_num, desk_x, desk_y);

   /* fall back to default */
   if (cfbg)
     {
        const char *bf;

        bgfile = eina_stringshare_ref(cfbg->file);
        if (!bgfile) return NULL;
        if (bgfile[0] == '/') return bgfile;
        bf = e_path_find(path_backgrounds, bgfile);
        if (!bf) return bgfile;
        eina_stringshare_del(bgfile);
        return bf;
     }
   bgfile = e_config->desktop_default_background;
   if (bgfile)
     {
        if (bgfile[0] != '/')
          {
             const char *bf;

             bf = e_path_find(path_backgrounds, bgfile);
             if (bf) bgfile = bf;
          }
        else
          eina_stringshare_ref(bgfile);
     }
   if (bgfile && eina_str_has_extension(bgfile, ".edj"))
     {
        ok = edje_file_group_exists(bgfile, "e/desktop/background");
     }
   else if ((bgfile) && (bgfile[0]))
     ok = 1;
   if (!ok)
     eina_stringshare_replace(&bgfile, e_theme_edje_file_get("base/theme/background",
                                                             "e/desktop/background"));

   return bgfile;
}

E_API void
e_bg_zone_update(E_Zone *zone, E_Bg_Transition transition)
{
   Evas_Object *o;
   const char *bgfile = "";
   E_Desk *desk;

   desk = e_desk_current_get(zone);
   if (desk)
     bgfile = e_bg_file_get(zone->num, desk->x, desk->y);
   else
     bgfile = e_bg_file_get(zone->num, -1, -1);

   if (zone->bg_object)
     {
        const char *pfile = "";

        edje_object_file_get(zone->bg_object, &pfile, NULL);
        if (!e_util_strcmp(pfile, bgfile)) goto end;
     }

   if (eina_str_has_extension(bgfile, ".edj"))
     {
        o = edje_object_add(e_comp->evas);
        edje_object_file_set(o, bgfile, "e/desktop/background");
        if (edje_object_data_get(o, "noanimation"))
          edje_object_animation_set(o, EINA_FALSE);
     }
   else
     {
        o = e_icon_add(e_comp->evas);
        e_icon_file_key_set(o, bgfile, NULL);
        e_icon_fill_inside_set(o, 0);
     }
   evas_object_data_set(o, "e_zone", zone);
   evas_object_repeat_events_set(o, 1);
   zone->bg_object = o;
   evas_object_name_set(zone->bg_object, "zone->bg_object");
   evas_object_move(o, zone->x, zone->y);
   evas_object_resize(o, zone->w, zone->h);
   evas_object_layer_set(o, E_LAYER_BG);
   evas_object_clip_set(o, zone->bg_clip_object);
   evas_object_show(o);

   if (zone->bg_object) evas_object_name_set(zone->bg_object, "zone->bg_object");
   if (zone->prev_bg_object) evas_object_name_set(zone->prev_bg_object, "zone->prev_bg_object");
   if (zone->transition_object) evas_object_name_set(zone->transition_object, "zone->transition_object");
   evas_object_move(zone->transition_object, zone->x, zone->y);
   evas_object_resize(zone->transition_object, zone->w, zone->h);
   e_comp_canvas_zone_update(zone);
end:
   eina_stringshare_del(bgfile);
}

E_API void
e_bg_default_set(const char *file)
{
   E_Event_Bg_Update *ev;
   Eina_Bool changed;

   file = eina_stringshare_add(file);
   changed = file != e_config->desktop_default_background;

   if (!changed)
     {
        eina_stringshare_del(file);
        return;
     }

   if (e_config->desktop_default_background)
     {
        eina_stringshare_del(e_config->desktop_default_background);
     }

   if (file)
     {
        e_config->desktop_default_background = file;
     }
   else
     e_config->desktop_default_background = NULL;

   ev = E_NEW(E_Event_Bg_Update, 1);
   if (!ev) return;
   ev->zone = -1;
   ev->desk_x = -1;
   ev->desk_y = -1;
   ecore_event_add(E_EVENT_BG_UPDATE, ev, _e_bg_event_bg_update_free, NULL);
}

E_API void
e_bg_add(int zone, int desk_x, int desk_y, const char *file)
{
   const Eina_List *l;
   E_Config_Desktop_Background *cfbg;
   E_Event_Bg_Update *ev;

   file = eina_stringshare_add(file);

   EINA_LIST_FOREACH(e_config->desktop_backgrounds, l, cfbg)
     {
        if ((cfbg) &&
            (cfbg->zone == zone) &&
            (cfbg->desk_x == desk_x) &&
            (cfbg->desk_y == desk_y) &&
            (cfbg->file == file))
          {
             eina_stringshare_del(file);
             return;
          }
     }

   e_bg_del(zone, desk_x, desk_y);
   cfbg = E_NEW(E_Config_Desktop_Background, 1);
   if (!cfbg) return;
   cfbg->zone = zone;
   cfbg->desk_x = desk_x;
   cfbg->desk_y = desk_y;
   cfbg->file = file;
   e_config->desktop_backgrounds = eina_list_append(e_config->desktop_backgrounds, cfbg);

   ev = E_NEW(E_Event_Bg_Update, 1);
   if (!ev) return;
   ev->zone = zone;
   ev->desk_x = desk_x;
   ev->desk_y = desk_y;
   ecore_event_add(E_EVENT_BG_UPDATE, ev, _e_bg_event_bg_update_free, NULL);
}

E_API void
e_bg_del(int zone, int desk_x, int desk_y)
{
   Eina_List *l;
   E_Config_Desktop_Background *cfbg;
   E_Event_Bg_Update *ev;

   EINA_LIST_FOREACH(e_config->desktop_backgrounds, l, cfbg)
     {
        if (!cfbg) continue;
        if ((cfbg->zone == zone) && (cfbg->desk_x == desk_x) && (cfbg->desk_y == desk_y))
          {
             e_config->desktop_backgrounds = eina_list_remove_list(e_config->desktop_backgrounds, l);
             if (cfbg->file) eina_stringshare_del(cfbg->file);
             free(cfbg);
             break;
          }
     }

   ev = E_NEW(E_Event_Bg_Update, 1);
   if (!ev) return;
   ev->zone = zone;
   ev->desk_x = desk_x;
   ev->desk_y = desk_y;
   ecore_event_add(E_EVENT_BG_UPDATE, ev, _e_bg_event_bg_update_free, NULL);
}

E_API void
e_bg_update(void)
{
   const Eina_List *l;
   E_Zone *zone;

   EINA_LIST_FOREACH(e_comp->zones, l, zone)
     e_zone_bg_reconfigure(zone);
}

/* local subsystem functions */
static void
_e_bg_event_bg_update_free(void *data EINA_UNUSED, void *event)
{
   free(event);
}
