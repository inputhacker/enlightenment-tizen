#ifdef E_TYPEDEFS

#else
#ifndef E_DPMS_H
#define E_DPMS_H

#include <Ecore_Drm.h>

int e_dpms_init(void);
int e_dpms_shutdown(void);

unsigned int e_dpms_get(Ecore_Drm_Output *output);

#endif
#endif
