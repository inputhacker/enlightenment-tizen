#ifdef E_TYPEDEFS

#else
#ifndef E_COMP_WL_EOM_H
#define E_COMP_WL_EOM_H

#include <tdm.h>

EINTERN int e_eom_init(void);
EINTERN int e_eom_shutdown(void);

E_API   Eina_Bool e_eom_is_ec_external(E_Client *ec);

#endif
#endif
