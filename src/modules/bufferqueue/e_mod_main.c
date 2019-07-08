#include "e.h"
#include "bq_mgr_protocol.h"

#define E_BQ_MGR_TYPE         (int)0xE0b91001
#define E_BQ_QUEUE_TYPE       (int)0xE0b91002
#define E_BQ_CONSUMER_TYPE    (int)0xE0b91003
#define E_BQ_PROVIDER         (int)0xE0b91004
#define E_BQ_BUFFER_TYPE      (int)0xE0b91005

#define ARRAY_LENGTH(a)       (sizeof (a) / sizeof (a)[0])

typedef struct _E_Bq_Mgr E_Bq_Mgr;
typedef struct _E_Bq_Queue E_Bq_Queue;
typedef struct _E_Bq_Consumer E_Bq_Consumer;
typedef struct _E_Bq_Provider E_Bq_Provider;
typedef struct _E_Bq_Buffer E_Bq_Buffer;

typedef enum _E_Bq_Buffer_Type E_Bq_Buffer_Type;

enum _E_Bq_Buffer_Type
{
   E_BQ_BUFFER_TYPE_ID,
   E_BQ_BUFFER_TYPE_FD,
};

struct _E_Bq_Mgr
{
   E_Object e_obj_inherit;

   int self_dpy;
   struct wl_display *wdpy;
   struct wl_event_source *signals[3];
   struct wl_event_loop *loop;

   /* BufferQueue manager */
   struct wl_global *global;
   Eina_Hash *bqs;

   Ecore_Fd_Handler *fd_hdlr;
   Ecore_Idle_Enterer *idler;
};

struct _E_Bq_Queue
{
   E_Object e_obj_inherit;
   Eina_Hash *link;

   char *name;
   struct wl_signal connect;

   E_Bq_Consumer *consumer;
   E_Bq_Provider *provider;
   Eina_Inlist *buffers;
};

struct _E_Bq_Consumer
{
   E_Object e_obj_inherit;
   E_Bq_Queue *bq;

   int32_t queue_size;
   int32_t width;
   int32_t height;
};

struct _E_Bq_Provider
{
   E_Object e_obj_inherit;
   E_Bq_Queue *bq;
};

struct _E_Bq_Buffer
{
   E_Object e_obj_inherit; /*Don't use wl_resource in e_obj*/
   EINA_INLIST;

   struct wl_resource *consumer;
   struct wl_resource *provider;
   uint32_t serial;

   char *engine;
   E_Bq_Buffer_Type type;
   int32_t width;
   int32_t height;
   int32_t format;
   uint32_t flags;

   int32_t id;
   int32_t offset0;
   int32_t stride0;
   int32_t offset1;
   int32_t stride1;
   int32_t offset2;
   int32_t stride2;
};

static void _e_bq_mgr_consumer_side_buffer_set(E_Bq_Consumer *consumer, E_Bq_Buffer *buffer);

static Eina_Bool
_e_bq_mgr_wl_cb_read(void *data, Ecore_Fd_Handler *hdl EINA_UNUSED)
{
   E_Bq_Mgr *bq_mgr = data;

   if (!bq_mgr)
     return ECORE_CALLBACK_RENEW;

   if (!bq_mgr->wdpy)
     return ECORE_CALLBACK_RENEW;

   /* flush any pending client events */
   wl_display_flush_clients(bq_mgr->wdpy);

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(bq_mgr->loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static Eina_Bool
_e_bq_mgr_wl_cb_idle(void *data)
{
   E_Bq_Mgr *bq_mgr = data;

   if (!bq_mgr)
     return ECORE_CALLBACK_RENEW;

   if (!bq_mgr->wdpy)
     return ECORE_CALLBACK_RENEW;

   /* flush any pending client events */
   wl_display_flush_clients(bq_mgr->wdpy);

   /* dispatch any pending main loop events */
   wl_event_loop_dispatch(bq_mgr->loop, 0);

   return ECORE_CALLBACK_RENEW;
}

static int
_e_bq_mgr_on_term_signal(int signal_number, void *data)
{
   E_Bq_Mgr *bq_mgr = data;

   ERR("caught signal %d\n", signal_number);

   wl_display_terminate(bq_mgr->wdpy);

   return 1;
}

static void
_e_bq_mgr_free(E_Bq_Mgr *bq_mgr)
{
   int i;

   if (!bq_mgr->self_dpy)
     return;

   for (i = (ARRAY_LENGTH(bq_mgr->signals) - 1); i >= 0; i--)
     {
        if (bq_mgr->signals[i])
          wl_event_source_remove(bq_mgr->signals[i]);
     }

   if (bq_mgr->wdpy)
     wl_display_destroy(bq_mgr->wdpy);
}

static E_Bq_Mgr *
_e_bq_mgr_new(char *sock_name)
{
   E_Comp *comp;
   E_Bq_Mgr *bq_mgr;
   int fd;
   static char *default_sock_name = "e_bq_mgr_daemon";
   int res = 0;

   bq_mgr = E_OBJECT_ALLOC(E_Bq_Mgr, E_BQ_MGR_TYPE, _e_bq_mgr_free);
   if (!bq_mgr)
     return NULL;

   /* try to get the current compositor */
   if (!(comp = e_comp))
     {
        free(bq_mgr);
        return NULL;
     }

   if (sock_name != NULL)
     {
        bq_mgr->wdpy = wl_display_create();
        if (!bq_mgr->wdpy)
          {
             free(bq_mgr);
             return NULL;
          }

        bq_mgr->loop = wl_display_get_event_loop(bq_mgr->wdpy);
        if (!bq_mgr->loop)
          {
             wl_display_destroy(bq_mgr->wdpy);
             free(bq_mgr);
             return NULL;
          }

        bq_mgr->signals[0] =
           wl_event_loop_add_signal(bq_mgr->loop, SIGTERM,
                                    _e_bq_mgr_on_term_signal, bq_mgr);
        bq_mgr->signals[1] =
           wl_event_loop_add_signal(bq_mgr->loop, SIGINT,
                                    _e_bq_mgr_on_term_signal, bq_mgr);
        bq_mgr->signals[2] =
           wl_event_loop_add_signal(bq_mgr->loop, SIGQUIT,
                                    _e_bq_mgr_on_term_signal, bq_mgr);

        sock_name = default_sock_name;

        res = wl_display_add_socket(bq_mgr->wdpy, sock_name);
        if (res < 0)
          {
             wl_event_source_remove(bq_mgr->signals[0]);
             wl_event_source_remove(bq_mgr->signals[1]);
             wl_event_source_remove(bq_mgr->signals[2]);
             wl_display_destroy(bq_mgr->wdpy);
             free(bq_mgr);
             return NULL;
          }

        fd = wl_event_loop_get_fd(bq_mgr->loop);

        bq_mgr->fd_hdlr =
           ecore_main_fd_handler_add(fd, ECORE_FD_READ | ECORE_FD_WRITE,
                                     _e_bq_mgr_wl_cb_read, bq_mgr, NULL, NULL);

        bq_mgr->idler = ecore_idle_enterer_add(_e_bq_mgr_wl_cb_idle, bq_mgr);

        bq_mgr->self_dpy = 1;
     }
   else
     {
        /* try to get the compositor data */
        bq_mgr->wdpy = e_comp_wl->wl.disp;
        bq_mgr->loop = wl_display_get_event_loop(bq_mgr->wdpy);
        bq_mgr->self_dpy = 0;
     }

   return bq_mgr;
}

static void
_e_bq_mgr_bq_free(E_Bq_Queue *bq)
{
   E_Bq_Buffer *bq_buf;

   if (!bq)
     return;

   DBG("destroy buffer queue : %s\n", bq->name);

   while (bq->buffers)
     {
        bq_buf = EINA_INLIST_CONTAINER_GET(bq->buffers, E_Bq_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, EINA_INLIST_GET(bq_buf));

        if (bq_buf->consumer)
          {
             wl_resource_destroy(bq_buf->consumer);
             bq_buf->consumer = NULL;
          }

        if (bq_buf->provider)
          {
             wl_resource_destroy(bq_buf->provider);
             bq_buf->provider = NULL;
          }
     }

   if (bq->link)
     {
        eina_hash_del(bq->link, bq->name, bq);
        bq->link = NULL;
     }

   if (bq->name)
     {
        free(bq->name);
        bq->name = NULL;
     }
}

static E_Bq_Queue *
_e_bq_mgr_bq_new(E_Bq_Mgr *bq_mgr, const char *name)
{
   E_Bq_Queue *bq;

   bq = eina_hash_find(bq_mgr->bqs, name);
   if (bq)
     {
        e_object_ref(E_OBJECT(bq));
        return bq;
     }

   bq = E_OBJECT_ALLOC(E_Bq_Queue, E_BQ_QUEUE_TYPE, _e_bq_mgr_bq_free);
   if (!bq)
     return NULL;

   bq->link = bq_mgr->bqs;
   bq->name = strdup(name);

   if (!eina_hash_add(bq->link, bq->name, bq))
     {
        free(bq->name);
        free(bq);
        return NULL;
     }

   wl_signal_init(&bq->connect);
   bq->buffers = NULL;

   return bq;
}

static void
_e_bq_mgr_buffer_consumer_release_buffer(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer)
{
   E_Bq_Queue *bq;
   E_Bq_Provider *provider;
   E_Bq_Consumer *consumer = wl_resource_get_user_data(resource);
   E_Bq_Buffer *bq_buf = wl_resource_get_user_data(buffer);

   if ((!consumer) || (!bq_buf))
     return;

   bq = consumer->bq;
   provider = bq->provider;

   if (provider && bq_buf->provider)
     {
        bq_provider_send_add_buffer(e_object_data_get(E_OBJECT(provider)),
                                    bq_buf->provider, bq_buf->serial);
     }
}

static void
_e_bq_mgr_consumer_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct bq_consumer_interface _bq_consumer_interface = {
     _e_bq_mgr_buffer_consumer_release_buffer,
     _e_bq_mgr_consumer_cb_destroy
};

static void
_e_bq_mgr_buffer_consumer_destroy(struct wl_resource *resource)
{
   E_Bq_Consumer *consumer = wl_resource_get_user_data(resource);
   E_Bq_Provider *provider;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   if (!consumer)
     return;

   DBG("destroy buffer consumer : %s\n", consumer->bq->name);

   bq = consumer->bq;
   provider = bq->provider;

   bq->consumer = NULL;
   e_object_data_set(E_OBJECT(consumer), NULL);

   if (provider)
     bq_provider_send_disconnected(e_object_data_get(E_OBJECT(provider)));

   while (bq->buffers)
     {
        bq_buf = EINA_INLIST_CONTAINER_GET(bq->buffers, E_Bq_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, EINA_INLIST_GET(bq_buf));
        DBG("destroy BUFFER : %d\n", bq_buf->type);
     }

   e_object_unref(E_OBJECT(bq));
   e_object_del(E_OBJECT(consumer));
}

static void
_e_bq_mgr_buffer_consumer_free(E_Bq_Consumer *consumer)
{
   free(consumer);
}

static void
_e_bq_mgr_consumer_side_buffer_set(E_Bq_Consumer *consumer, E_Bq_Buffer *buffer)
{
   struct wl_resource *resource;

   if (!consumer)
     return;

   if (!buffer)
     return;

   if (!buffer->consumer)
     return;

   resource = e_object_data_get(E_OBJECT(consumer));

   if (buffer->type == E_BQ_BUFFER_TYPE_ID)
     {
        bq_consumer_send_set_buffer_id(resource,
                                       buffer->consumer,
                                       buffer->id,
                                       buffer->offset0,
                                       buffer->stride0,
                                       buffer->offset1,
                                       buffer->stride1,
                                       buffer->offset2,
                                       buffer->stride2);
     }
   else
     {
        bq_consumer_send_set_buffer_fd(resource,
                                       buffer->consumer,
                                       buffer->id,
                                       buffer->offset0,
                                       buffer->stride0,
                                       buffer->offset1,
                                       buffer->stride1,
                                       buffer->offset2,
                                       buffer->stride2);
        close(buffer->id);
     }
}

static void
_e_bq_mgr_buffer_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   E_Bq_Buffer *bq_buf;

   bq_buf = wl_resource_get_user_data(resource);
   if (!bq_buf)
     return;

   if (bq_buf->consumer == resource)
     {
        e_object_unref(E_OBJECT(bq_buf));
        bq_buf->consumer = NULL;
     }
   if (bq_buf->provider == resource)
     {
        e_object_del(E_OBJECT(bq_buf));
        bq_buf->provider = NULL;
     }

   wl_resource_destroy(resource);
}

static const struct bq_buffer_interface _bq_buffer_interface = {
   _e_bq_mgr_buffer_cb_destroy
};

static void
_e_bq_mgr_consumer_side_buffer_create(E_Bq_Consumer *consumer, E_Bq_Buffer *buffer)
{
   struct wl_resource *resource;

   if (!consumer)
     return;

   resource = e_object_data_get(E_OBJECT(consumer));
   buffer->consumer = wl_resource_create(wl_resource_get_client(resource),
                                         &bq_buffer_interface, 2, 0);

   if(!buffer->consumer)
     return;

   wl_resource_set_implementation(buffer->consumer, &_bq_buffer_interface,
                                  buffer, NULL);
   e_object_ref(E_OBJECT(buffer));

   bq_consumer_send_buffer_attached(resource,
                                    buffer->consumer,
                                    buffer->engine,
                                    buffer->width,
                                    buffer->height,
                                    buffer->format,
                                    buffer->flags);
}

static void
_e_bq_mgr_bq_create_consumer(struct wl_client *client, struct wl_resource *resource, uint32_t id, const char *name, int32_t queue_size, int32_t width, int32_t height)
{
   E_Bq_Mgr *bq_mgr = wl_resource_get_user_data(resource);
   E_Bq_Queue *bq;
   E_Bq_Consumer *consumer;
   E_Bq_Provider *provider;
   E_Bq_Buffer *bq_buf;
   struct wl_resource *new_resource;

   if (!bq_mgr)
     return;

   bq = _e_bq_mgr_bq_new(bq_mgr, name);
   if (!bq)
     return;

   if (bq->consumer)
     {
        e_object_unref(E_OBJECT(bq));
        wl_resource_post_error(resource,
                               BQ_MGR_ERROR_ALREADY_USED,
                               "%s consumer already used", name);
        return;
     }

   consumer = E_OBJECT_ALLOC(E_Bq_Consumer, E_BQ_CONSUMER_TYPE, _e_bq_mgr_buffer_consumer_free);
   if (!consumer)
     return;

   new_resource = wl_resource_create(client,
                                     &bq_consumer_interface,
                                     2, id);
   if (!new_resource)
     {
        free(consumer);
        wl_client_post_no_memory(client);
        return;
     }

   e_object_data_set(E_OBJECT(consumer), new_resource);
   wl_resource_set_implementation(new_resource,
                                  &_bq_consumer_interface,
                                  consumer,
                                  _e_bq_mgr_buffer_consumer_destroy);

   consumer->bq = bq;
   consumer->queue_size = queue_size;
   consumer->width = width;
   consumer->height = height;

   provider = bq->provider;
   bq->consumer = consumer;
   if (provider)
     {
        bq_provider_send_connected(e_object_data_get(E_OBJECT(provider)),
                                   queue_size, width, height);
        bq_consumer_send_connected(new_resource);
     }

   EINA_INLIST_FOREACH(bq->buffers, bq_buf)
     {
        _e_bq_mgr_consumer_side_buffer_create(consumer, bq_buf);
        _e_bq_mgr_consumer_side_buffer_set(consumer, bq_buf);
     }
}

static void
_e_bq_mgr_buffer_free(E_Bq_Buffer *bq_buf)
{
   if (bq_buf->engine)
     free(bq_buf->engine);
}

static void
_e_bq_mgr_provider_buffer_attach(struct wl_client *client, struct wl_resource *resource, uint32_t buffer, const char *engine, int32_t width, int32_t height, int32_t format, uint32_t flags)
{
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   if (!provider)
     return;

   bq = provider->bq;
   consumer = bq->consumer;

   bq_buf = E_OBJECT_ALLOC(E_Bq_Buffer, E_BQ_BUFFER_TYPE, _e_bq_mgr_buffer_free);
   if (!bq_buf)
     return;

   bq_buf->provider = wl_resource_create(client, &bq_buffer_interface, 2, buffer);
   if (!bq_buf->provider)
     {
        free(bq_buf);
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(bq_buf->provider, &_bq_buffer_interface, bq_buf, NULL);

   bq_buf->engine = strdup(engine);
   bq_buf->width = width;
   bq_buf->height = height;
   bq_buf->format = format;
   bq_buf->flags = flags;

   bq->buffers = eina_inlist_append(bq->buffers, EINA_INLIST_GET(bq_buf));

   _e_bq_mgr_consumer_side_buffer_create(consumer, bq_buf);
}

static void
_e_bq_mgr_provider_buffer_id_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, int32_t id, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   if (!provider)
     return;

   bq = provider->bq;
   consumer = bq->consumer;

   bq_buf = wl_resource_get_user_data(buffer);
   if (!bq_buf)
     return;

   bq_buf->type = E_BQ_BUFFER_TYPE_ID;
   bq_buf->id = id;
   bq_buf->offset0 = offset0;
   bq_buf->stride0 = stride0;
   bq_buf->offset1 = offset1;
   bq_buf->stride1 = stride1;
   bq_buf->offset2 = offset2;
   bq_buf->stride2 = stride2;

   _e_bq_mgr_consumer_side_buffer_set(consumer, bq_buf);
}

static void
_e_bq_mgr_provider_buffer_fd_set(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, int32_t fd, int32_t offset0, int32_t stride0, int32_t offset1, int32_t stride1, int32_t offset2, int32_t stride2)
{
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   if (!provider)
     return;

   bq = provider->bq;
   consumer = bq->consumer;

   bq_buf= wl_resource_get_user_data(buffer);
   if (!bq_buf)
     return;

   bq_buf->type = E_BQ_BUFFER_TYPE_FD;
   bq_buf->id = fd;
   bq_buf->offset0 = offset0;
   bq_buf->stride0 = stride0;
   bq_buf->offset1 = offset1;
   bq_buf->stride1 = stride1;
   bq_buf->offset2 = offset2;
   bq_buf->stride2 = stride2;

   _e_bq_mgr_consumer_side_buffer_set(consumer, bq_buf);
}

static void
_e_bq_mgr_provider_buffer_detach(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer)
{
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   EINA_SAFETY_ON_NULL_RETURN(provider);
   bq = provider->bq;
   consumer = bq->consumer;
   bq_buf = wl_resource_get_user_data(buffer);
   if (!bq_buf) return;

   if (consumer)
     bq_consumer_send_buffer_detached(e_object_data_get(E_OBJECT(consumer)),
                                       bq_buf->consumer);

   bq->buffers = eina_inlist_remove(bq->buffers, EINA_INLIST_GET(bq_buf));
}

static void
_e_bq_mgr_provider_buffer_enqueue(struct wl_client *client, struct wl_resource *resource, struct wl_resource *buffer, uint32_t serial)
{
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Queue *bq;
   E_Bq_Buffer *bq_buf;

   if (!provider)
     return;

   bq = provider->bq;

   consumer = bq->consumer;
   if (!consumer)
     return;

   bq_buf= wl_resource_get_user_data(buffer);
   if (!bq_buf)
     return;

   bq_buf->serial = serial;

   bq_consumer_send_add_buffer(e_object_data_get(E_OBJECT(consumer)),
                               bq_buf->consumer,
                               bq_buf->serial);
}

static void
_e_bq_mgr_provider_cb_destroy(struct wl_client *client, struct wl_resource *resource)
{
   wl_resource_destroy(resource);
}

static const struct bq_provider_interface _bq_provider_interface = {
   _e_bq_mgr_provider_buffer_attach,
   _e_bq_mgr_provider_buffer_id_set,
   _e_bq_mgr_provider_buffer_fd_set,
   _e_bq_mgr_provider_buffer_detach,
   _e_bq_mgr_provider_buffer_enqueue,
   _e_bq_mgr_provider_cb_destroy
};

static void
_e_bq_mgr_provider_destroy(struct wl_resource *resource)
{
   E_Bq_Queue *bq;
   E_Bq_Provider *provider = wl_resource_get_user_data(resource);
   E_Bq_Consumer *consumer;
   E_Bq_Buffer *bq_buf;
   struct wl_resource *consumer_res;

   if (!provider)
     return;

   DBG("destroy buffer provider : %s\n", provider->bq->name);

   bq = provider->bq;
   consumer = bq->consumer;

   e_object_data_set(E_OBJECT(provider), NULL);
   bq->provider = NULL;

   while (bq->buffers)
     {
        bq_buf = EINA_INLIST_CONTAINER_GET(bq->buffers, E_Bq_Buffer);
        bq->buffers = eina_inlist_remove(bq->buffers, EINA_INLIST_GET(bq_buf));

        if (bq_buf->consumer)
          bq_consumer_send_buffer_detached(e_object_data_get(E_OBJECT(consumer)),
                                           bq_buf->consumer);
     }

   if (consumer)
     {
        consumer_res = e_object_data_get(E_OBJECT(consumer));
        if (consumer_res)
          bq_consumer_send_disconnected(consumer_res);
     }

   e_object_del(E_OBJECT(provider));
   e_object_unref(E_OBJECT(bq));
}

static void
_e_bq_mgr_provider_free(E_Bq_Provider *provider)
{
   free(provider);
}

static void
_e_bq_mgr_provider_create(struct wl_client *client, struct wl_resource *resource, uint32_t id, const char *name)
{
   E_Bq_Mgr *bq_mgr = wl_resource_get_user_data(resource);
   E_Bq_Queue *bq;
   E_Bq_Provider *provider;
   E_Bq_Consumer *consumer;
   struct wl_resource *new_resource;

   if (!bq_mgr)
     return;

   bq = _e_bq_mgr_bq_new(bq_mgr, name);
   if (!bq)
     return;

   if (bq->provider)
     {
        e_object_unref(E_OBJECT(bq));
        wl_resource_post_error(resource,
                               BQ_MGR_ERROR_ALREADY_USED,
                               "%s provider already used", name);
        return;
     }

   provider = E_OBJECT_ALLOC(E_Bq_Provider, E_BQ_PROVIDER, _e_bq_mgr_provider_free);
   if (!provider)
     goto on_error;

   new_resource = wl_resource_create(client,
                                     &bq_provider_interface,
                                     2, id);
   if (!new_resource)
     goto on_error;

   e_object_data_set(E_OBJECT(provider), new_resource);

   wl_resource_set_implementation(new_resource,
                                  &_bq_provider_interface,
                                  provider,
                                  _e_bq_mgr_provider_destroy);

   provider->bq = bq;
   bq->provider = provider;
   consumer = bq->consumer;
   if (consumer)
     {
        /*Send connect*/
        bq_consumer_send_connected(e_object_data_get(E_OBJECT(consumer)));
        bq_provider_send_connected(new_resource,
                                   consumer->queue_size,
                                   consumer->width, consumer->height);
     }

   return;

on_error:
   if (bq) e_object_unref(E_OBJECT(bq));
   if (provider) free(provider);
   wl_client_post_no_memory(client);
}

static const struct bq_mgr_interface _bq_mgr_interface =
{
   _e_bq_mgr_bq_create_consumer,
   _e_bq_mgr_provider_create
};

static void
_e_bq_mgr_bind(struct wl_client *client, void *data,
                          uint32_t version, uint32_t id)
{
   E_Bq_Mgr *bq_mgr = (E_Bq_Mgr *)data;
   struct wl_resource *resource;

   resource = wl_resource_create(client, &bq_mgr_interface, 1, id);
   if (resource == NULL)
     {
        wl_client_post_no_memory(client);
        return;
     }

   wl_resource_set_implementation(resource,
                                  &_bq_mgr_interface,
                                  bq_mgr, NULL);
}

Eina_Bool
_e_bq_mgr_init(E_Bq_Mgr *bq_mgr)
{
   if (!bq_mgr)
     return EINA_FALSE;

   bq_mgr->global =
      wl_global_create(bq_mgr->wdpy, &bq_mgr_interface,
                       1, bq_mgr, _e_bq_mgr_bind);

   if (!bq_mgr->global)
     return EINA_FALSE;

   bq_mgr->bqs = eina_hash_string_superfast_new(NULL);
   if (!bq_mgr->bqs)
     return EINA_FALSE;

   return EINA_TRUE;
}

/* this is needed to advertise a label for the module IN the code (not just
 * the .desktop file) but more specifically the api version it was compiled
 * for so E can skip modules that are compiled for an incorrect API version
 * safely) */
E_API E_Module_Api e_modapi =
{
   E_MODULE_API_VERSION, "Buffer queue manager"
};

E_API void *
e_modapi_init(E_Module *m)
{
   E_Bq_Mgr *bq_mgr = NULL;

   bq_mgr = _e_bq_mgr_new(NULL);
   if (!bq_mgr)
     return NULL;

   if (!_e_bq_mgr_init(bq_mgr))
     {
        e_object_del(E_OBJECT(bq_mgr));
        return NULL;
     }

   m->data = bq_mgr;

   return m;
}

E_API int
e_modapi_shutdown(E_Module *m)
{
   E_Bq_Mgr *bq_mgr = m->data;

   e_object_del(E_OBJECT(bq_mgr));

   return 1;
}

E_API int
e_modapi_save(E_Module *m)
{
   /* Do Something */
   return 1;
}

