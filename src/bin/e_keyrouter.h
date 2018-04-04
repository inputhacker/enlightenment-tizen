#ifdef E_TYPEDEFS

typedef struct _E_Keyrouter_Intercept_Hook E_Keyrouter_Intercept_Hook;
typedef struct _E_Keyrouter_Info E_Keyrouter_Info;
typedef struct _E_Keyrouter_Key_List_Node E_Keyrouter_Key_List_Node;
typedef struct _E_Keyrouter_Key_List_Node* E_Keyrouter_Key_List_NodePtr;
typedef struct _E_Keyrouter_Tizen_HWKey E_Keyrouter_Tizen_HWKey;
typedef struct _E_Keyrouter_Grabbed_Key E_Keyrouter_Grabbed_Key;
typedef struct _E_Keyrouter_Registered_Window_Info E_Keyrouter_Registered_Window_Info;
typedef struct _E_Keyrouter_Event_Data E_Keyrouter_Event_Data;

typedef enum _E_Keyrouter_Intercept_Hook_Point
{
   E_KEYROUTER_INTERCEPT_HOOK_BEFORE_KEYROUTING,
   E_KEYROUTER_INTERCEPT_HOOK_DELIVER_FOCUS,
   E_KEYROUTER_INTERCEPT_HOOK_LAST
} E_Keyrouter_Intercept_Hook_Point;

typedef enum _E_Keyrouter_Client_Status
{
   E_KRT_CSTAT_DEAD = 0,
   E_KRT_CSTAT_ALIVE,
   E_KRT_CSTAT_UNGRAB
} E_Keyrouter_Client_Status;

typedef Eina_Bool (*E_Keyrouter_Intercept_Hook_Cb) (void *data, int type, Ecore_Event_Key *event);

#else
#ifndef E_KEYROUTER_H
#define E_KEYROUTER_H

extern E_API E_Keyrouter_Info e_keyrouter;

struct _E_Keyrouter_Intercept_Hook
{
   EINA_INLIST;
   E_Keyrouter_Intercept_Hook_Point hookpoint;
   E_Keyrouter_Intercept_Hook_Cb func;
   void *data;
   unsigned char delete_me : 1;
};

struct _E_Keyrouter_Info
{
   void *(*keygrab_list_get)(void);
   int (*max_keycode_get)(void);
};

struct _E_Keyrouter_Registered_Window_Info
{
   struct wl_resource *surface;
   Eina_List *keys;
};

struct _E_Keyrouter_Key_List_Node
{
   struct wl_resource *surface;
   struct wl_client *wc;
   Eina_Bool focused;
   E_Keyrouter_Client_Status status;
};

struct _E_Keyrouter_Tizen_HWKey
{
   char *name;
   int keycode;
   int no_privcheck;
   int repeat;
};

struct _E_Keyrouter_Grabbed_Key
{
   int keycode;
   char* keyname;
   Eina_Bool no_privcheck;
   Eina_Bool repeat;

   Eina_List *excl_ptr;
   Eina_List *or_excl_ptr;
   Eina_List *top_ptr;
   Eina_List *shared_ptr;
   Eina_List *press_ptr;
   Eina_List *pic_off_ptr;
};

struct _E_Keyrouter_Event_Data
{
   struct wl_client *client;
   struct wl_resource *surface;
};

E_API E_Keyrouter_Intercept_Hook *e_keyrouter_intercept_hook_add(E_Keyrouter_Intercept_Hook_Point hookpoint, E_Keyrouter_Intercept_Hook_Cb func, const void *data);
E_API void e_keyrouter_intercept_hook_del(E_Keyrouter_Intercept_Hook *ch);
E_API Eina_Bool e_keyrouter_intercept_hook_call(E_Keyrouter_Intercept_Hook_Point hookpoint, int type, Ecore_Event_Key *event);

E_API int e_keyrouter_init(void);
E_API int e_keyrouter_shutdown(void);


#endif
#endif

