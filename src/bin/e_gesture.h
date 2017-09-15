#ifdef E_TYPEDEFS

typedef enum _E_Gesture_Error E_Gesture_Error;
typedef enum _E_Gesture_Mode E_Gesture_Mode;
typedef enum _E_Gesture_Edge E_Gesture_Edge;
typedef enum _E_Gesture_Edge_Size E_Gesture_Edge_Size;

typedef struct _E_Gesture_Info E_Gesture_Info;

typedef struct _E_Event_Gesture_Edge_Swipe E_Event_Gesture_Edge_Swipe;
typedef struct _E_Event_Gesture_Edge_Drag E_Event_Gesture_Edge_Drag;
typedef struct _E_Event_Gesture_Tap E_Event_Gesture_Tap;
typedef struct _E_Event_Gesture_Palm_Cover E_Event_Gesture_Palm_Cover;
typedef struct _E_Event_Gesture_Pan E_Event_Gesture_Pan;
typedef struct _E_Event_Gesture_Pinch E_Event_Gesture_Pinch;

#else
#ifndef E_GESTURE_H
#define E_GESTURE_H

extern E_API E_Gesture_Info *e_gesture;

extern E_API int E_EVENT_GESTURE_EDGE_SWIPE;
extern E_API int E_EVENT_GESTURE_EDGE_DRAG;
extern E_API int E_EVENT_GESTURE_TAP;
extern E_API int E_EVENT_GESTURE_PALM_COVER;
extern E_API int E_EVENT_GESTURE_PAN;
extern E_API int E_EVENT_GESTURE_PINCH;

enum _E_Gesture_Error
{
   E_GESTURE_ERROR_NONE = 0,
   E_GESTURE_ERROR_INVAILD_DATA,
   E_GESTURE_ERROR_NO_PERMISSION,
   E_GESTURE_ERROR_NO_SYSTEM_RESOURCE,
   E_GESTURE_ERROR_GRABBED_ALREADY,
   E_GESTURE_ERROR_NOT_SUPPORTED
};

enum _E_Gesture_Mode
{
   E_GESTURE_MODE_NONE = 0,
   E_GESTURE_MODE_BEGIN,
   E_GESTURE_MODE_UPDATE,
   E_GESTURE_MODE_END,
   E_GESTURE_MODE_DONE
};

enum _E_Gesture_Edge
{
   E_GESTURE_EDGE_NONE = 0,
   E_GESTURE_EDGE_TOP,
   E_GESTURE_EDGE_RIGHT,
   E_GESTURE_EDGE_BOTTOM,
   E_GESTURE_EDGE_LEFT
};

enum _E_Gesture_Edge_Size
{
   E_GESTURE_EDGE_SIZE_NONE,
   E_GESTURE_EDGE_SIZE_FULL,
   E_GESTURE_EDGE_SIZE_PARTIAL
};

struct _E_Event_Gesture_Edge_Swipe
{
   E_Gesture_Mode mode;
   unsigned int fingers;
   int sx;
   int sy;
   unsigned int edge;
};

struct _E_Event_Gesture_Edge_Drag
{
   E_Gesture_Mode mode;
   unsigned int fingers;
   int cx;
   int cy;
   unsigned int edge;
};

struct _E_Event_Gesture_Tap
{
   E_Gesture_Mode mode;
   unsigned int fingers;
   unsigned int repeats;
};

struct _E_Event_Gesture_Palm_Cover
{
   E_Gesture_Mode mode;
   unsigned int duration;
   int cx;
   int cy;
   unsigned int size;
   double pressure;
};

struct _E_Event_Gesture_Pan
{
   E_Gesture_Mode mode;
   unsigned int fingers;
   int cx;
   int cy;
};

struct _E_Event_Gesture_Pinch
{
   E_Gesture_Mode mode;
   unsigned int fingers;
   double distance;
   double angle;
   int cx;
   int cy;
};

struct _E_Gesture_Info
{
   struct
     {
        int (*grab)(uint32_t fingers, uint32_t edge, uint32_t edge_size, uint32_t start_point, uint32_t end_point);
        int (*ungrab)(uint32_t fingers, uint32_t edge, uint32_t edge_size, uint32_t start_point, uint32_t end_point);
     } edge_swipe;
   struct
     {
        int (*grab)(uint32_t fingers, uint32_t edge, uint32_t edge_size, uint32_t start_point, uint32_t end_point);
        int (*ungrab)(uint32_t fingers, uint32_t edge, uint32_t edge_size, uint32_t start_point, uint32_t end_point);
     } edge_drag;
   struct
     {
        int (*grab)(uint32_t fingers, uint32_t repeats);
        int (*ungrab)(uint32_t fingers, uint32_t repeats);
     } tap;
   struct
     {
        int (*grab)(void);
        int (*ungrab)(void);
     } palm_cover;
   struct
     {
        int (*grab)(uint32_t fingers);
        int (*ungrab)(uint32_t fingers);
     } pan;
   struct
     {
        int (*grab)(uint32_t fingers);
        int (*ungrab)(uint32_t fingers);
     } pinch;
};

EINTERN void e_gesture_init(void);
EINTERN int e_gesture_shutdown(void);

E_API int e_gesture_edge_swipe_grab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point);
E_API int e_gesture_edge_swipe_ungrab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point);
E_API int e_gesture_edge_drag_grab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point);
E_API int e_gesture_edge_drag_ungrab(unsigned int fingers, unsigned int edge, unsigned int edge_size, unsigned int start_point, unsigned int end_point);
E_API int e_gesture_tap_grab(unsigned int fingers, unsigned int repeats);
E_API int e_gesture_tap_ungrab(unsigned int fingers, unsigned int repeats);
E_API int e_gesture_palm_cover_grab(void);
E_API int e_gesture_palm_cover_ungrab(void);
E_API int e_gesture_pan_grab(unsigned int fingers);
E_API int e_gesture_pan_ungrab(unsigned int fingers);
E_API int e_gesture_pinch_grab(unsigned int fingers);
E_API int e_gesture_pinch_ungrab(unsigned int fingers);

#endif
#endif
