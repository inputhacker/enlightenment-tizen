#ifdef E_TYPEDEFS
E_API extern int E_EVENT_DBUS_CONN_INIT_DONE;
#else
#ifndef E_DBUS_CONN_H
#define E_DBUS_CONN_H

#define E_DBUS_CONN_TYPE (int)0xdb00beaf
#define E_DBUS_CONN_DEFAULT_RETRY_COUNT 5

typedef enum _E_DBus_Conn_Init_Status
{
   E_DBUS_CONN_INIT_FAIL = 0,
   E_DBUS_CONN_INIT_SUCCESS,
   E_DBUS_CONN_INIT_YET_STARTED,
   E_DBUS_CONN_INIT_CANCELLED,
   E_DBUS_CONN_INIT_INPROGRESS
} E_DBus_Conn_Init_Status;

struct _E_DBus_Conn_Init_Done_Event
{
   E_DBus_Conn_Init_Status status;
   Eldbus_Connection *conn;
   Eldbus_Connection_Type conn_type;
};

struct _E_DBus_Conn
{
   int retry_cnt;
   Ecore_Thread *init_thread;
   E_DBus_Conn_Init_Status init_status;
   Eina_Bool use_thread : 1;
   Eldbus_Connection *conn;
   Eldbus_Connection_Type conn_type;
   unsigned int retry_intervals; /* suspend re-connection for microsecond intervals */
};

typedef struct _E_DBus_Conn_Init_Done_Event E_DBus_Conn_Init_Done_Event;
typedef struct _E_DBus_Conn E_DBus_Conn;

E_API Eldbus_Connection *e_dbus_conn_connection_get(void);
E_API Eldbus_Connection *e_dbus_conn_connection_ref(Eldbus_Connection_Type type);
E_API void e_dbus_conn_connection_unref(Eldbus_Connection *conn);
E_API Eina_Bool e_dbus_conn_sync_init(E_DBus_Conn *ed);
E_API Eina_Bool e_dbus_conn_dbus_init(Eldbus_Connection_Type type);
E_API int e_dbus_conn_init(void);
E_API int e_dbus_conn_shutdown(void);

#endif
#endif
