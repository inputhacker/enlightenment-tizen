#ifdef E_TYPEDEFS

#else
#ifndef E_HWC_H
#define E_HWC_H

/* This module is responsible for evaluate which an ec will be composite by a hwc
extension and commit the changes to hwc extension. */

EINTERN Eina_Bool            e_hwc_init(void);
/* evaluate which e_window will be composited by hwc and wich by GLES */
EINTERN Eina_Bool            e_hwc_re_evaluate();
EINTERN Eina_Bool            e_hwc_commit();

#endif
#endif
