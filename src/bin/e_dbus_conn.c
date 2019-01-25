#include "e.h"
#include "e_dbus_conn.h"

int _e_dbus_conn_init_count = 0;
int _e_dbus_conn_log_dom = -1;

E_API E_DBus_Conn *e_dbus_conn = NULL;
E_API int E_EVENT_DBUS_CONN_INIT_DONE = 0;

E_API Eldbus_Connection *
e_dbus_conn_connection_get(void)
{
   return e_dbus_conn->conn;
}

E_API Eldbus_Connection *
e_dbus_conn_connection_ref(Eldbus_Connection_Type type)
{
   if (!e_dbus_conn) return NULL;

   /* TODO : support ELDBUS_CONNECTION_TYPE_SESSION */
   /* Currently, ELDBUS_CONNECTION_TYPE_SYSTEM is being supported */

   if (e_dbus_conn->conn && e_dbus_conn->conn_type == type)
     return eldbus_connection_ref(e_dbus_conn->conn);

   return NULL;
}

E_API void
e_dbus_conn_connection_unref(Eldbus_Connection *conn)
{
   if (conn)
     eldbus_connection_unref(conn);
}

static void
_e_dbus_conn_init_done_event_free(void *data EINA_UNUSED, void *event)
{
   E_DBus_Conn_Init_Done_Event *e = event;

   eldbus_connection_unref(e->conn);
   free(e);
}

static void
_e_dbus_conn_init_done_send(E_DBus_Conn *ed)
{
   E_DBus_Conn_Init_Done_Event *e;

   if (!ed) return;

   e = calloc(1, sizeof(E_DBus_Conn_Init_Done_Event));

   if (!e)
     {
        ERR("Failed to allocate memory for E_DBus_Conn_Init_Done_Event !");
        return;
     }

   e->status = ed->init_status;
   e->conn = eldbus_connection_ref(ed->conn);
   e->conn_type = ed->conn_type;

   ecore_event_add(E_EVENT_DBUS_CONN_INIT_DONE, e, _e_dbus_conn_init_done_event_free, NULL);
}

static void
_e_dbus_conn_init_thread_heavy(void *data, Ecore_Thread *th, void *msg_data)
{
   Eina_Bool *res = NULL;
   E_DBus_Conn ed_thread = *(E_DBus_Conn *)data;
   int retry_cnt = ed_thread.retry_cnt;

   res = calloc(1, sizeof(Eina_Bool));

   if (!res)
     {
        ERR("Failed to allocate memory for E_DBus_Conn thread !\n");
        return;
     }

   *res = EINA_FALSE;
   ed_thread.conn = NULL;

   while (retry_cnt--)
     {
        ed_thread.conn = eldbus_connection_get(ed_thread.conn_type);

        if (ed_thread.conn)
          {
             ecore_thread_main_loop_begin();
             eldbus_connection_unref(ed_thread.conn);
             ecore_thread_main_loop_end();
             *res = EINA_TRUE;
             break;
          }

        if (ed_thread.retry_intervals)
          usleep(ed_thread.retry_intervals);
     }

   ecore_thread_feedback(th, res);
   return;
}

static void
_e_dbus_conn_init_thread_notify(void *data, Ecore_Thread *th, void *msg_data)
{
   Eina_Bool *res = (Eina_Bool *)msg_data;

   if (res && *res)
     {
        e_dbus_conn->conn = eldbus_connection_get(e_dbus_conn->conn_type);
        if (e_dbus_conn->conn) e_dbus_conn->init_status = E_DBUS_CONN_INIT_SUCCESS;
        free(res);
     }
}

static void
_e_dbus_conn_init_thread_end(void *data, Ecore_Thread *th, void *msg_data)
{
   _e_dbus_conn_init_done_send(e_dbus_conn);
}

static void
_e_dbus_conn_init_thread_cancel(void *data, Ecore_Thread *th, void *msg_data)
{
   e_dbus_conn->init_thread = NULL;
   e_dbus_conn->init_status = E_DBUS_CONN_INIT_CANCELLED;

   ERR("E_DBus_Conn thread initialization has been cancelled !");
}

static Eina_Bool
_e_dbus_conn_async_init(E_DBus_Conn *ed)
{
   ed->init_thread = ecore_thread_feedback_run((Ecore_Thread_Cb)_e_dbus_conn_init_thread_heavy,
                                             (Ecore_Thread_Notify_Cb)_e_dbus_conn_init_thread_notify,
                                             (Ecore_Thread_Cb)_e_dbus_conn_init_thread_end,
                                             (Ecore_Thread_Cb)_e_dbus_conn_init_thread_cancel, ed, 1);
   return !!(ed->init_thread);
}

E_API Eina_Bool
e_dbus_conn_sync_init(E_DBus_Conn *ed)
{
   Eldbus_Connection_Type type;

   if (!ed) return EINA_FALSE;
   if (ed->conn) return EINA_TRUE;

   type = ed->conn_type;
   ed->init_status = E_DBUS_CONN_INIT_INPROGRESS;

   if (!ed->conn)
     {
        ed->conn = eldbus_connection_get(type);
     }

   if (!ed->conn)
     {
        ed->init_status = E_DBUS_CONN_INIT_FAIL;
        return EINA_FALSE;
     }

   ed->init_status = E_DBUS_CONN_INIT_SUCCESS;

   return EINA_TRUE;
}


E_API Eina_Bool
e_dbus_conn_dbus_init(Eldbus_Connection_Type type)
{
   Eina_Bool res;

   if (!e_dbus_conn) return EINA_FALSE;
   if (e_dbus_conn->conn)
     {
        _e_dbus_conn_init_done_send(e_dbus_conn);
        return EINA_TRUE;
     }

   /* TODO : support ELDBUS_CONNECTION_TYPE_SESSION */
   /* Currently, ELDBUS_CONNECTION_TYPE_SYSTEM is being supported */

   e_dbus_conn->conn_type = type;

   if (e_dbus_conn->use_thread)
     {
        if (e_dbus_conn->init_thread || e_dbus_conn->init_status == E_DBUS_CONN_INIT_INPROGRESS)
          return EINA_TRUE;

        res = _e_dbus_conn_async_init(e_dbus_conn);

        return res;
     }
   else
     {
        res = e_dbus_conn_sync_init(e_dbus_conn);
        _e_dbus_conn_init_done_send(e_dbus_conn);

        return res;
     }
}

E_API int
e_dbus_conn_init(void)
{
   char *env = NULL;
   E_DBus_Conn *ed = NULL;
   unsigned int num;
   Eina_Bool res;

   if (++_e_dbus_conn_init_count != 1) return _e_dbus_conn_init_count;
   if (!eldbus_init()) return --_e_dbus_conn_init_count;

   if (_e_dbus_conn_log_dom < 0)
     _e_dbus_conn_log_dom = eina_log_domain_register("E_DBus_Conn", EINA_COLOR_GREEN);

   if (!_e_dbus_conn_log_dom)
     {
        ERR("Could not create logging domain for E_DBus_Conn");

        return --_e_dbus_conn_log_dom;
     }

   ed = calloc(1, sizeof(E_DBus_Conn));

   if (!ed)
     {
        ERR("Failed to allocate memory for E_DBus_Conn\n");

        return --_e_dbus_conn_init_count;
     }

   ed->conn = NULL;
   ed->init_thread = NULL;
   ed->use_thread = EINA_FALSE;
   ed->conn_type = ELDBUS_CONNECTION_TYPE_UNKNOWN;
   ed->retry_cnt = E_DBUS_CONN_DEFAULT_RETRY_COUNT;
   ed->init_status = E_DBUS_CONN_INIT_YET_STARTED;
   ed->retry_intervals = 0;

   E_EVENT_DBUS_CONN_INIT_DONE = ecore_event_type_new();

   env = e_util_env_get("E_DBUS_CONN_USE_THREAD_INIT");

   if (env)
     {
        ed->use_thread = EINA_TRUE;
        E_FREE(env);

        /* If user wants to connect with D-BUS through another thread,
         * then we also need to check that wants to use time intervals
         * during retrying connection.
         *
         * It is required for some systems which don't support sync call
         * for connecting D-BUS to avoid thread problems.
         */
        env = e_util_env_get("E_DBUS_CONN_INIT_RETRY_INTERVALS");
        if (env)
          {
             res = e_util_string_to_uint(env, &num, 10);
             if (res) ed->retry_intervals = num;
             E_FREE(env);
          }
     }

   env = e_util_env_get("E_DBUS_CONN_INIT_RETRY_COUNT");

   if (env)
     {
        ed->retry_cnt = atoi(env);
        E_FREE(env);
     }

   e_dbus_conn = ed;

   INF("Succeed to init E_DBus_Conn !");

   return _e_dbus_conn_init_count;
}

E_API int
e_dbus_conn_shutdown(void)
{
   if (_e_dbus_conn_init_count < 1) return 0;
   if (--_e_dbus_conn_init_count != 0) return _e_dbus_conn_init_count;

   if (e_dbus_conn && e_dbus_conn->conn)
     eldbus_connection_unref(e_dbus_conn->conn);

   eldbus_shutdown();
   free(e_dbus_conn);
   e_dbus_conn = NULL;

   E_EVENT_DBUS_CONN_INIT_DONE = -1;

   INF("Succeed to shutdown E_DBus_Conn !");

   return _e_dbus_conn_init_count;
}

