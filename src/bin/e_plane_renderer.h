#ifdef E_TYPEDEFS

typedef enum _E_Plane_Renderer_State
{
   E_PLANE_RENDERER_STATE_NONE,
   E_PLANE_RENDERER_STATE_CANDIDATE,
   E_PLANE_RENDERER_STATE_ACTIVATE,
} E_Plane_Renderer_State;

typedef struct _E_Plane_Renderer             E_Plane_Renderer;
typedef struct _E_Plane_Renderer_Client      E_Plane_Renderer_Client;
#else
#ifndef E_PLANE_RENDERER_H
#define E_PLANE_RENDERER_H

#include "e_comp_screen.h"
#include "e_output.h"
#include "e_plane.h"

struct _E_Plane_Renderer {
   tbm_surface_queue_h tqueue;
   int tqueue_width;
   int tqueue_height;
   int tqueue_size;

   tbm_surface_h cursor_tsurface;

   E_Client           *ec;
   E_Plane_Renderer_State    state;

   tbm_surface_h       displaying_tsurface; /* current tsurface displaying */
   tbm_surface_h       previous_tsurface;   /* previous tsurface displayed */

   Eina_List          *disp_surfaces;
   Eina_List          *sent_surfaces;
   Eina_List          *exported_surfaces;
   Eina_List          *released_surfaces;

   Ecore_Evas         *ee;
   Evas               *evas;
   Eina_Bool           update_ee;
   Eina_Bool           update_exist;
   Eina_Bool           pending;

   E_Plane            *plane;

   Ecore_Fd_Handler   *event_hdlr;
   int                 event_fd;
};

EINTERN Eina_Bool                  e_plane_renderer_init(void);
EINTERN void                       e_plane_renderer_shutdown(void);
EINTERN E_Plane_Renderer          *e_plane_renderer_new(E_Plane *plane);
EINTERN void                       e_plane_renderer_del(E_Plane_Renderer *renderer);
EINTERN Eina_Bool                  e_plane_renderer_render(E_Plane_Renderer *renderer, Eina_Bool is_fb);
EINTERN Eina_Bool                  e_plane_renderer_activate(E_Plane_Renderer *renderer, E_Client *ec);
EINTERN Eina_Bool                  e_plane_renderer_deactivate(E_Plane_Renderer *renderer);
EINTERN E_Plane_Renderer_State     e_plane_renderer_state_get(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_update_exist_set(E_Plane_Renderer *renderer, Eina_Bool update_exit);
EINTERN Eina_Bool                  e_plane_renderer_update_exist_check(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_pending_set(E_Plane_Renderer *renderer, Eina_Bool pending);
EINTERN Eina_Bool                  e_plane_renderer_pending_check(E_Plane_Renderer *renderer);
EINTERN E_Plane                   *e_plane_renderer_plane_get(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_displaying_surface_set(E_Plane_Renderer *renderer, tbm_surface_h tsurface);
EINTERN tbm_surface_h              e_plane_renderer_displaying_surface_get(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_previous_surface_set(E_Plane_Renderer *renderer, tbm_surface_h tsurface);

EINTERN E_Plane_Renderer_Client   *e_plane_renderer_client_new(E_Client *ec);
EINTERN void                       e_plane_renderer_client_free(E_Plane_Renderer_Client *renderer_client);
EINTERN E_Plane_Renderer_Client   *e_plane_renderer_client_get(E_Client *ec);
EINTERN tbm_surface_h              e_plane_renderer_client_surface_recieve(E_Plane_Renderer_Client *renderer_client);
EINTERN E_Plane_Renderer          *e_plane_renderer_client_renderer_get(E_Plane_Renderer_Client *renderer_client);

EINTERN tbm_surface_queue_h        e_plane_renderer_surface_queue_create(E_Plane_Renderer *renderer, int width, int height, unsigned int buffer_flags);
EINTERN Eina_Bool                  e_plane_renderer_surface_queue_set(E_Plane_Renderer *renderer, tbm_surface_queue_h tqueue);
EINTERN void                       e_plane_renderer_surface_queue_destroy(E_Plane_Renderer *renderer);
EINTERN tbm_surface_h              e_plane_renderer_surface_queue_acquire(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_surface_queue_release(E_Plane_Renderer *renderer, tbm_surface_h tsurface);
EINTERN Eina_Bool                  e_plane_renderer_surface_queue_enqueue(E_Plane_Renderer *renderer, tbm_surface_h tsurface);
EINTERN Eina_Bool                  e_plane_renderer_surface_queue_can_dequeue(E_Plane_Renderer *renderer);
EINTERN tbm_surface_h              e_plane_renderer_surface_queue_dequeue(E_Plane_Renderer *renderer);
EINTERN Eina_Bool                  e_plane_renderer_surface_queue_clear(E_Plane_Renderer *renderer);
EINTERN void                       e_plane_renderer_surface_send(E_Plane_Renderer *renderer, E_Client *ec, tbm_surface_h tsurface);
EINTERN Eina_Bool                  e_plane_renderer_ec_set(E_Plane_Renderer *renderer, E_Client *ec);
EINTERN Eina_Bool                  e_plane_renderer_cursor_ec_set(E_Plane_Renderer *renderer, E_Client *ec);
EINTERN tbm_surface_h              e_plane_renderer_cursor_surface_get(E_Plane_Renderer *renderer);
EINTERN Eina_Bool                  e_plane_renderer_cursor_surface_refresh(E_Plane_Renderer *renderer, E_Client *ec);
EINTERN Eina_Bool                  e_plane_renderer_ecore_evas_use(E_Plane_Renderer *renderer);

EINTERN void                       e_plane_renderer_hwc_trace_debug(Eina_Bool onoff);

#endif
#endif
