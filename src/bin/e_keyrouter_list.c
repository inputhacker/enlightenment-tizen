#include "e_keyrouter_private.h"

static int _e_keyrouter_find_duplicated_client(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode);
static Eina_Bool _e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, int key, int mode);
static Eina_List **_e_keyrouter_get_list(int mode, int key);

/* add a new key grab info to the list */
int
e_keyrouter_set_keygrab_in_list(struct wl_resource *surface, struct wl_client *client, uint32_t key, uint32_t mode)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   EINA_SAFETY_ON_FALSE_RETURN_VAL
     (((mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE) ||
       (mode == TIZEN_KEYROUTER_MODE_TOPMOST) ||
       (mode == TIZEN_KEYROUTER_MODE_SHARED)),
      TIZEN_KEYROUTER_ERROR_INVALID_MODE);

   if (mode == TIZEN_KEYROUTER_MODE_EXCLUSIVE)
     {
        EINA_SAFETY_ON_TRUE_RETURN_VAL
          ((krt->HardKeys[key].excl_ptr != NULL),
           TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY);
     }

   if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
     {
        EINA_SAFETY_ON_NULL_RETURN_VAL
          (surface, TIZEN_KEYROUTER_ERROR_INVALID_SURFACE);
     }

   res = e_keyrouter_prepend_to_keylist(surface,
                                   surface ? NULL : client,
                                   key,
                                   mode,
                                   EINA_FALSE);

   EINA_SAFETY_ON_FALSE_RETURN_VAL(res == TIZEN_KEYROUTER_ERROR_NONE, res);

   return res;
}

/* Function for checking whether the key has been grabbed already by the same wl_surface or not */
static int
_e_keyrouter_find_duplicated_client(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List *keylist_ptr = NULL, *l = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         return TIZEN_KEYROUTER_ERROR_NONE;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         keylist_ptr = krt->HardKeys[key].or_excl_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         keylist_ptr = krt->HardKeys[key].top_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         keylist_ptr = krt->HardKeys[key].shared_ptr;
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         keylist_ptr = krt->HardKeys[key].press_ptr;
         break;

     case TIZEN_KEYROUTER_MODE_PICTURE_OFF:
         keylist_ptr = krt->HardKeys[key].pic_off_ptr;
         break;
      default:
         KLWRN("Unknown key(%d) and grab mode(%d)", key, mode);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   EINA_LIST_FOREACH(keylist_ptr, l, key_node_data)
     {
        if (!key_node_data) continue;

        if (surface)
          {
             if (key_node_data->surface == surface)
               {
                  KLDBG("The key(%d) is already grabbed same mode(%s) on the same wl_surface %p",
                        key, e_keyrouter_mode_to_string(mode), surface);
                  return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
               }
          }
        else
          {
             if (key_node_data->wc == wc)
               {
                  KLDBG("The key(%d) is already grabbed same mode(%s) on the same wl_client %p",
                        key, e_keyrouter_mode_to_string(mode), wc);
                  return TIZEN_KEYROUTER_ERROR_GRABBED_ALREADY;
               }
          }
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

static Eina_Bool
_e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, int key, int mode)
{
   Eina_List **list = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   EINA_SAFETY_ON_TRUE_RETURN_VAL(((!surface) && (!wc)), EINA_FALSE);

   list = _e_keyrouter_get_list(mode, key);
   EINA_SAFETY_ON_NULL_RETURN_VAL(list, EINA_FALSE);

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, key_node_data)
     {
        if (!key_node_data) continue;

        if ((surface) && (surface == key_node_data->surface)) return EINA_TRUE;
        else if ((wc == key_node_data->wc)) return EINA_TRUE;
     }

   return EINA_FALSE;
}


/* Function for prepending a new key grab information in the keyrouting list */
int
e_keyrouter_prepend_to_keylist(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode, Eina_Bool focused)
{
   int res = TIZEN_KEYROUTER_ERROR_NONE;

   res = _e_keyrouter_find_duplicated_client(surface, wc, key, mode);
   CHECK_ERR_VAL(res);

   E_Keyrouter_Key_List_NodePtr new_keyptr = E_NEW(E_Keyrouter_Key_List_Node, 1);

   if (!new_keyptr)
     {
        KLERR("Failled to allocate memory for new_keyptr");
        return TIZEN_KEYROUTER_ERROR_NO_SYSTEM_RESOURCES;
     }

   new_keyptr->surface = surface;
   new_keyptr->wc = wc;
   new_keyptr->focused = focused;
   new_keyptr->status = E_KRT_CSTAT_ALIVE;

   switch(mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:
         krt->HardKeys[key].excl_ptr = eina_list_prepend(krt->HardKeys[key].excl_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE:
         krt->HardKeys[key].or_excl_ptr= eina_list_prepend(krt->HardKeys[key].or_excl_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_TOPMOST:
         krt->HardKeys[key].top_ptr = eina_list_prepend(krt->HardKeys[key].top_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_SHARED:
         krt->HardKeys[key].shared_ptr= eina_list_prepend(krt->HardKeys[key].shared_ptr, new_keyptr);
         break;

      case TIZEN_KEYROUTER_MODE_PRESSED:
         krt->HardKeys[key].press_ptr = eina_list_prepend(krt->HardKeys[key].press_ptr, new_keyptr);
         break;

     case TIZEN_KEYROUTER_MODE_PICTURE_OFF:
         krt->HardKeys[key].pic_off_ptr = eina_list_prepend(krt->HardKeys[key].pic_off_ptr, new_keyptr);
         break;

      default:
         KLWRN("Unknown key(%d) and grab mode(%d)", key, mode);
         E_FREE(new_keyptr);
         return TIZEN_KEYROUTER_ERROR_INVALID_MODE;
     }

   if (TIZEN_KEYROUTER_MODE_PRESSED != mode)
     {
        if (surface)
          {
             e_keyrouter_wl_add_surface_destroy_listener(surface);
             /* TODO: if failed add surface_destory_listener, remove keygrabs */
          }
        else if (wc)
          {
             e_keyrouter_wl_add_client_destroy_listener(wc);
             /* TODO: if failed add client_destory_listener, remove keygrabs */
          }
     }

   return TIZEN_KEYROUTER_ERROR_NONE;
}

/* remove key grab info from the list */
void
e_keyrouter_find_and_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key, uint32_t mode)
{
   Eina_List **list = NULL;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   list = _e_keyrouter_get_list(mode, key);
   EINA_SAFETY_ON_NULL_RETURN(list);

   EINA_LIST_FOREACH_SAFE(*list, l, l_next, key_node_data)
     {
        if (!key_node_data) continue;

        if (surface)
          {
             if (surface == key_node_data->surface)
               {
                  if (mode == TIZEN_KEYROUTER_MODE_PRESSED)
                    {
                       key_node_data->status = E_KRT_CSTAT_UNGRAB;
                    }
                  else
                    {
                       *list = eina_list_remove_list(*list, l);
                       E_FREE(key_node_data);
                    }
                  KLDBG("Remove a %s Mode Grabbed key(%d) by surface(%p)", e_keyrouter_mode_to_string(mode), key, surface);
               }
          }
        else if ((wc == key_node_data->wc))
          {
             if (mode == TIZEN_KEYROUTER_MODE_PRESSED)
               {
                  key_node_data->status = E_KRT_CSTAT_UNGRAB;
               }
             else
               {
                  *list = eina_list_remove_list(*list, l);
                  E_FREE(key_node_data);
               }
             KLDBG("Remove a %s Mode Grabbed key(%d) by wc(%p)", e_keyrouter_mode_to_string(mode), key, wc);
          }
     }
}

void
e_keyrouter_remove_client_from_list(struct wl_resource *surface, struct wl_client *wc)
{
   int i = 0;
   Eina_List *l = NULL, *l_next = NULL;
   E_Keyrouter_Key_List_NodePtr key_node_data = NULL;

   EINA_SAFETY_ON_TRUE_RETURN(((!surface) && (!wc)));

   for (i = 0; i < krt->max_tizen_hwkeys; i++)
     {
        if (0 == krt->HardKeys[i].keycode) continue;

        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].excl_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].excl_ptr = eina_list_remove_list(krt->HardKeys[i].excl_ptr, l);
                       E_FREE(key_node_data);
                       KLDBG("Remove a Exclusive Mode Grabbed key(%d) by wl_surface(%p)", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].excl_ptr = eina_list_remove_list(krt->HardKeys[i].excl_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Exclusive Mode Grabbed key(%d) by wl_client(%p)", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].or_excl_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].or_excl_ptr = eina_list_remove_list(krt->HardKeys[i].or_excl_ptr, l);
                       E_FREE(key_node_data);
                       KLDBG("Remove a Overridable_Exclusive Mode Grabbed key(%d) by wl_surface(%p)", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].or_excl_ptr = eina_list_remove_list(krt->HardKeys[i].or_excl_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Overridable_Exclusive Mode Grabbed key(%d) by wl_client(%p)", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].top_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].top_ptr = eina_list_remove_list(krt->HardKeys[i].top_ptr, l);
                       E_FREE(key_node_data);
                       KLDBG("Remove a Topmost Mode Grabbed key(%d) by wl_surface(%p)", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].top_ptr = eina_list_remove_list(krt->HardKeys[i].top_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Topmost Mode Grabbed key(%d) by wl_client(%p)", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].shared_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].shared_ptr = eina_list_remove_list(krt->HardKeys[i].shared_ptr, l);
                       E_FREE(key_node_data);
                       KLDBG("Remove a Shared Mode Grabbed key(%d) by wl_surface(%p)", i, surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  krt->HardKeys[i].shared_ptr = eina_list_remove_list(krt->HardKeys[i].shared_ptr, l);
                  E_FREE(key_node_data);
                  KLDBG("Remove a Shared Mode Grabbed key(%d) by wl_client(%p)", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].press_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;

             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       key_node_data->status = E_KRT_CSTAT_DEAD;
                       KLDBG("Remove a Pressed  key(%d) by wl_surface(%p)", i, surface);
                       key_node_data->wc = wl_resource_get_client(surface);
                    }
               }
             else if ((wc == key_node_data->wc))
               {
                  key_node_data->status = E_KRT_CSTAT_DEAD;
                  KLDBG("Remove a Pressed key(%d) by wl_client(%p)", i, wc);
               }
          }
        EINA_LIST_FOREACH_SAFE(krt->HardKeys[i].pic_off_ptr, l, l_next, key_node_data)
          {
             if (!key_node_data) continue;
             if (surface)
               {
                  if (surface == key_node_data->surface)
                    {
                       krt->HardKeys[i].pic_off_ptr = eina_list_remove_list(krt->HardKeys[i].pic_off_ptr, l);
                       E_FREE(key_node_data);
                    }
               }
             else if ( wc == key_node_data->wc)
               {
                  krt->HardKeys[i].pic_off_ptr = eina_list_remove_list(krt->HardKeys[i].pic_off_ptr, l);
                  E_FREE(key_node_data);
               }
          }
     }
}

int
e_keyrouter_find_key_in_list(struct wl_resource *surface, struct wl_client *wc, uint32_t key)
{
   int mode = TIZEN_KEYROUTER_MODE_NONE;
   Eina_Bool found = EINA_FALSE;

   mode = TIZEN_KEYROUTER_MODE_EXCLUSIVE;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_TOPMOST;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   mode = TIZEN_KEYROUTER_MODE_SHARED;
   found = _e_keyrouter_find_key_in_list(surface, wc, key, mode);
   if (found) goto finish;

   KLDBG("%d key is not grabbed by (wl_surface: %p, wl_client: %p)", key, surface, wc);
   return TIZEN_KEYROUTER_MODE_NONE;

finish:
   KLDBG("Find %d key grabbed by (wl_surface: %p, wl_client: %p) in %s mode",
         key, surface, wc, e_keyrouter_mode_to_string(mode));
   return mode;
}

const char *
e_keyrouter_mode_to_string(uint32_t mode)
{
   const char *str = NULL;

   switch (mode)
     {
      case TIZEN_KEYROUTER_MODE_EXCLUSIVE:             str = "Exclusive";             break;
      case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: str = "Overridable_Exclusive"; break;
      case TIZEN_KEYROUTER_MODE_TOPMOST:               str = "Topmost";               break;
      case TIZEN_KEYROUTER_MODE_SHARED:                str = "Shared";                break;
      case TIZEN_KEYROUTER_MODE_PRESSED:               str = "Pressed";               break;
      default: str = "UnknownMode"; break;
     }

   return str;
}

static Eina_List **
_e_keyrouter_get_list(int mode, int key)
{
   Eina_List **list = NULL;

   switch (mode)
     {
        case TIZEN_KEYROUTER_MODE_EXCLUSIVE:             list = &krt->HardKeys[key].excl_ptr;    break;
        case TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE: list = &krt->HardKeys[key].or_excl_ptr; break;
        case TIZEN_KEYROUTER_MODE_TOPMOST:               list = &krt->HardKeys[key].top_ptr;     break;
        case TIZEN_KEYROUTER_MODE_SHARED:                list = &krt->HardKeys[key].shared_ptr;  break;
        case TIZEN_KEYROUTER_MODE_PRESSED:                list = &krt->HardKeys[key].press_ptr;  break;
        default: break;
     }

   return list;
}


int
e_keyrouter_keygrab_set(struct wl_client *client, struct wl_resource *surface, int key, int mode)
{
   int res=0;

#ifdef HAVE_CYNARA
   if (EINA_FALSE == e_keyrouter_wl_util_do_privilege_check(client, mode, key))
     {
        KLINF("No permission for %d grab mode ! (key=%d)", mode, key);
        return TIZEN_KEYROUTER_ERROR_NO_PERMISSION;
     }
#endif

   if (!surface)
     {
        /* Regarding topmost mode, a client must request to grab a key with a valid surface. */
        if (mode == TIZEN_KEYROUTER_MODE_TOPMOST)
          {
             KLWRN("Invalid surface for %d grab mode ! (key=%d)", mode, key);

             return TIZEN_KEYROUTER_ERROR_INVALID_SURFACE;
          }
     }

   /* Check the given key range */
   if (krt->max_tizen_hwkeys < key)
     {
        KLWRN("Invalid range of key ! (keycode:%d)", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the key can be grabbed or not !
    * Only key listed in Tizen key layout file can be grabbed. */
   if (0 == krt->HardKeys[key].keycode)
     {
        KLWRN("Invalid key ! Disabled to grab ! (keycode:%d)", key);
        return TIZEN_KEYROUTER_ERROR_INVALID_KEY;
     }

   /* Check whether the request key can be grabbed or not */
   res = e_keyrouter_set_keygrab_in_list(surface, client, key, mode);

   return res;
}

int
e_keyrouter_keygrab_unset(struct wl_client *client, struct wl_resource *surface, int key)
{
   /* Ungrab top position grabs first. This grab mode do not need privilege */
   if (!surface)
     e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);
   else
     e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_TOPMOST);

#ifdef HAVE_CYNARA
   if (EINA_FALSE == e_keyrouter_wl_util_do_privilege_check(client, TIZEN_KEYROUTER_MODE_NONE, key))
     {
        goto finish;
     }
#endif

   if (!surface)
     {
        /* EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

        /* OVERRIDABLE_EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

        /* SHARED grab */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_SHARED);

        /* Press List */
        e_keyrouter_find_and_remove_client_from_list(NULL, client, key, TIZEN_KEYROUTER_MODE_PRESSED);
     }
   else
     {
        /* EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_EXCLUSIVE);

        /* OVERRIDABLE_EXCLUSIVE grab */
        e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_OVERRIDABLE_EXCLUSIVE);

        /* SHARED grab */
        e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_SHARED);

        /* Press List */
        e_keyrouter_find_and_remove_client_from_list(surface, client, key, TIZEN_KEYROUTER_MODE_PRESSED);
     }

finish:
   e_keyrouter_keycancel_send(client, surface, key);

   return TIZEN_KEYROUTER_ERROR_NONE;
}
