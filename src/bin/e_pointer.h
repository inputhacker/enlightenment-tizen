#ifdef E_TYPEDEFS

typedef struct _E_Pointer E_Pointer;

typedef enum
{
   /* These are compatible with netwm */
   E_POINTER_RESIZE_TL = 0,
   E_POINTER_RESIZE_T = 1,
   E_POINTER_RESIZE_TR = 2,
   E_POINTER_RESIZE_R = 3,
   E_POINTER_RESIZE_BR = 4,
   E_POINTER_RESIZE_B = 5,
   E_POINTER_RESIZE_BL = 6,
   E_POINTER_RESIZE_L = 7,
   E_POINTER_MOVE = 8,
   E_POINTER_RESIZE_NONE = 11
} E_Pointer_Mode;

typedef enum
{
   E_POINTER_NONE = 0,
   E_POINTER_MOUSE = 1,
   E_POINTER_TOUCH = 2
} E_Pointer_Device;

#else
# ifndef E_POINTER_H
#  define E_POINTER_H

#  define E_POINTER_TYPE 0xE0b01013

struct _E_Pointer
{
   E_Object e_obj_inherit;

   Evas *evas;
   Ecore_Evas *ee;
   Evas_Object *o_ptr;

   int x, y, w, h;
   int rotation;

   E_Pointer_Device device;

   Eina_Bool e_cursor : 1;
   Eina_Bool canvas : 1;
};

EINTERN int        e_pointer_init(void);
EINTERN int        e_pointer_shutdown(void);
EINTERN E_Pointer *e_pointer_canvas_new(Ecore_Evas *ee, Eina_Bool filled);
EINTERN void       e_pointer_object_set(E_Pointer *ptr, Evas_Object *obj, int x, int y);
EINTERN void       e_pointer_touch_move(E_Pointer *ptr, int x, int y);
EINTERN void       e_pointer_mouse_move(E_Pointer *ptr, int x, int y);

E_API void         e_pointer_hide(E_Pointer *ptr);
E_API Eina_Bool    e_pointer_is_hidden(E_Pointer *ptr);
E_API void         e_pointer_rotation_set(E_Pointer *ptr, int rotation);
E_API void         e_pointer_position_get(E_Pointer *ptr, int *x, int *y);
# endif
#endif
