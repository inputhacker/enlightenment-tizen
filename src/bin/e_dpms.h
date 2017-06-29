#ifdef E_TYPEDEFS

#else
#ifndef E_DPMS_H
#define E_DPMS_H

#include <Ecore_Drm.h>

EINTERN int e_dpms_init(void);
EINTERN int e_dpms_shutdown(void);

EINTERN unsigned int e_dpms_get(Ecore_Drm_Output *output);

#endif
#endif
