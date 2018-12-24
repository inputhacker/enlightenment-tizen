#include "e.h"

#ifdef HAVE_CYNARA
# include <cynara-session.h>
# include <cynara-client.h>
# include <cynara-creds-socket.h>
# include <sys/smack.h>
#endif

#ifdef HAVE_CYNARA
static cynara *g_cynara = NULL;
#endif

E_API Eina_Bool
e_security_privilege_check(pid_t pid, uid_t uid, const char *privilege)
{
#ifdef HAVE_CYNARA
   Eina_Bool res = EINA_FALSE;

   /* Cynara is not initialized. DENY all requests */
   EINA_SAFETY_ON_NULL_RETURN_VAL(g_cynara, EINA_FALSE);
   EINA_SAFETY_ON_NULL_RETURN_VAL(privilege, EINA_FALSE);

   char *client_smack = NULL;
   char *client_session = NULL;
   char uid_str[16] = { 0, };
   int len = -1;
   int ret = -1;

   ret = smack_new_label_from_process((int)pid, &client_smack);
   EINA_SAFETY_ON_FALSE_GOTO((ret > 0), finish);

   snprintf(uid_str, 15, "%d", (int)uid);

   client_session = cynara_session_from_pid(pid);
   EINA_SAFETY_ON_NULL_GOTO(client_session, finish);

   ret = cynara_check(g_cynara,
                      client_smack,
                      client_session,
                      uid_str,
                      privilege);

   if (ret == CYNARA_API_ACCESS_ALLOWED)
     res = EINA_TRUE;

finish:
   ELOGF("E_SECURITY",
         "Privilege Check For '%s' %s pid:%u uid:%u client_smack:%s(len:%d) client_session:%s ret:%d",
         NULL,
         privilege,
         res ? "SUCCESS" : "FAIL",
         pid,
         uid,
         client_smack ? client_smack : "N/A",
         len,
         client_session ? client_session: "N/A",
         ret);

   if (client_session) E_FREE(client_session);
   if (client_smack) E_FREE(client_smack);

   return res;
#else
   return EINA_TRUE;
#endif
}

EINTERN int
e_security_init(void)
{
#ifdef HAVE_CYNARA
   if (cynara_initialize(&g_cynara, NULL) != CYNARA_API_SUCCESS)
     {
        ERR("cynara_initialize failed.");
        g_cynara = NULL;
     }
#endif
   return EINA_TRUE;
}

EINTERN int
e_security_shutdown(void)
{
#ifdef HAVE_CYNARA
   if (g_cynara)
     cynara_finish(g_cynara);
#endif
   g_cynara = NULL;

   return 1;
}
