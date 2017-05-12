#ifdef E_TYPEDEFS
#else
#ifndef E_SECURITY_H
#define E_SECURITY_H

EINTERN int e_security_init(void);
EINTERN int e_security_shutdown(void);

E_API Eina_Bool e_security_privilege_check(pid_t pid, uid_t uid, const char *privilege);

#endif
#endif
