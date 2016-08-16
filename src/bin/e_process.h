# ifdef E_TYPEDEFS

typedef enum _E_Process_Action
{
   E_PROCESS_ACT_LAUNCH = 0,
   E_PROCESS_ACT_RESUME = 1,
   E_PROCESS_ACT_TERMINATE = 2,
   E_PROCESS_ACT_FOREGROUND = 3,
   E_PROCESS_ACT_BACKGROUND = 4,
   E_PROCESS_ACT_ACTIVATE = 5,
   E_PROCESS_ACT_DEACTIVATE = 6,
} E_Process_Action;

typedef enum _E_Process_State
{
   E_PROCESS_STATE_UNKNOWN,
   E_PROCESS_STATE_BACKGROUND,
   E_PROCESS_STATE_FOREGROUND,
} E_Process_State;

typedef enum _E_Process_Hook_Point
{
   E_PROCESS_HOOK_STATE_CHANGE,
   E_PROCESS_HOOK_ACTION_CHANGE,
   E_PROCESS_HOOK_LAST
} E_Process_Hook_Point;

typedef struct _E_Process_Manager E_Process_Manager;
typedef struct _E_Process E_Process;

typedef struct _E_Process_Hook E_Process_Hook;

typedef void (*E_Process_Hook_Cb)(void *data, E_Process *epro, void *user);


# else

# ifndef E_PROCESSMGR_H
# define E_PROCESSMGR_H

struct _E_Process_Hook
{
   EINA_INLIST;
   E_Process_Hook_Point hookpoint;
   E_Process_Hook_Cb    func;
   void                *data;
   unsigned char        delete_me : 1;
};

struct _E_Process_Manager
{
   Eina_Hash         *pids_hash;
   Eina_Inlist       *process_list;
   E_Client          *active_win;
};

struct _E_Process
{
   EINA_INLIST;
   pid_t            pid;
   Eina_List       *ec_list;
   E_Process_State  state;
};

E_API Eina_Bool  e_process_init(void);
E_API int        e_process_shutdown(void);

E_API E_Process_Hook *e_process_hook_add(E_Process_Hook_Point hookpoint, E_Process_Hook_Cb func, const void *data);
E_API void            e_process_hook_del(E_Process_Hook *ph);


#endif
#endif

