#ifdef E_TYPEDEFS
#else
#ifndef E_MAIN_H
#define E_MAIN_H

typedef struct _E_Main_Hook E_Main_Hook;

typedef enum _E_Main_Hook_Point
{
   E_MAIN_HOOK_MODULE_LOAD_DONE,
   E_MAIN_HOOK_E_INFO_READY,
   E_MAIN_HOOK_LAST
} E_Main_Hook_Point;

typedef void (*E_Main_Hook_Cb)(void *data);

struct _E_Main_Hook
{
   EINA_INLIST;
   E_Main_Hook_Point hookpoint;
   E_Main_Hook_Cb    func;
   void             *data;
   unsigned char     delete_me : 1;
};

E_API E_Main_Hook *e_main_hook_add(E_Main_Hook_Point hookpoint, E_Main_Hook_Cb func, const void *data);
E_API void e_main_hook_del(E_Main_Hook *ph);

E_API void e_main_hook_call(E_Main_Hook_Point hookpoint);
#endif
#endif
