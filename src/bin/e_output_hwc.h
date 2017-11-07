#ifdef E_TYPEDEFS

#else
#ifndef E_OUTPUT_HWC_H
#define E_OUTPUT_HWC_H

/* This module is responsible for evaluate which an ec will be composite by a hwc
extension and commit the changes to hwc extension. */

EINTERN Eina_Bool            e_output_hwc_init(E_Output *output);
/* evaluate which e_output_hwc_window will be composited by hwc and wich by GLES */
EINTERN Eina_Bool            e_output_hwc_re_evaluate(E_Output *output);
EINTERN Eina_Bool            e_output_hwc_commit(E_Output *output);
EINTERN void                 e_output_hwc_opt_hwc_set(E_Output *output, Eina_Bool set);
EINTERN Eina_Bool            e_output_hwc_opt_hwc_enabled(E_Output *output);

#endif // E_OUTPUT_HWC_H
#endif
