#ifdef E_TYPEDEFS
typedef void (*E_Info_Hook_Cb)(void *data, const char *log_path);
typedef struct _E_Info_Hook E_Info_Hook;
#else
#ifndef E_INFO_SERVER_H
#define E_INFO_SERVER_H

#include <e_info_shared_types.h>

typedef struct E_Event_Info_Rotation_Message E_Event_Info_Rotation_Message;

struct E_Event_Info_Rotation_Message
{
   E_Zone *zone;
   E_Info_Rotation_Message message;
   int rotation;
};

E_API extern int E_EVENT_INFO_ROTATION_MESSAGE;

E_API void e_info_server_hook_set(const char *module_name, E_Info_Hook_Cb func, void *data);

EINTERN int e_info_server_init(void);
EINTERN int e_info_server_shutdown(void);

EINTERN void e_info_server_dump_client(E_Client *ec, char *fname);

#define WL_HIDE_DEPRECATED
#include <wayland-server.h>

/* wayland private MACRO and structure definitions */
#ifndef WL_CLOSURE_MAX_ARGS
#define WL_CLOSURE_MAX_ARGS 20
#endif

struct wl_closure
{
   int count;
   const struct wl_message *message;
   uint32_t opcode;
   uint32_t sender_id;
   union wl_argument args[WL_CLOSURE_MAX_ARGS];
   struct wl_list link;
   struct wl_proxy *proxy;
   struct wl_array extra[0];
};

struct argument_details {
   char type;
   int nullable;
};

struct _E_Info_Hook
{
   const char *module_name;
   E_Info_Hook_Cb func;
   void *data;
};

typedef struct _E_Info_Server_Hook E_Info_Server_Hook;

typedef enum _E_Info_Server_Hook_Point
{
   E_INFO_SERVER_HOOK_BUFFER_DUMP_BEGIN,
   E_INFO_SERVER_HOOK_BUFFER_DUMP_END,
   E_INFO_SERVER_HOOK_LAST
} E_Info_Server_Hook_Point;

typedef void (*E_Info_Server_Hook_Cb)(void *data);

struct _E_Info_Server_Hook
{
   EINA_INLIST;
   E_Info_Server_Hook_Point hookpoint;
   E_Info_Server_Hook_Cb    func;
   void             *data;
   unsigned char     delete_me : 1;
};

E_API E_Info_Server_Hook *e_info_server_hook_add(E_Info_Server_Hook_Point hookpoint, E_Info_Server_Hook_Cb func, const void *data);

E_API void e_info_server_hook_del(E_Info_Server_Hook *ph);

E_API void e_info_server_hook_call(E_Info_Server_Hook_Point hookpoint);

#endif
#endif
