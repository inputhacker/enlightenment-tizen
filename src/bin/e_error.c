#include "e.h"

/* local subsystem functions */

/* local subsystem globals */

/* externally accessible functions */
E_API void
e_error_message_show_internal(char *txt)
{
   ELOGF("E ERROR", "%s", NULL, txt);
}

/* local subsystem functions */
