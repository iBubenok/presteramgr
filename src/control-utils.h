#ifndef __CONTROL_UTILS_H__
#define __CONTROL_UTILS_H__

static inline zmsg_t *
make_reply (enum status code)
{
  zmsg_t *msg = zmsg_new ();
  assert (msg);

  status_t val = code;
  zmsg_addmem (msg, &val, sizeof (val));

  return msg;
}

static inline void
__send_reply (zmsg_t *reply, void *sock)
{
  zmsg_send (&reply, sock);
}

static inline void
__report_status (enum status code, void *sock)
{
  __send_reply (make_reply (code), sock);
}

static inline void
__report_ok (void *sock)
{
  __report_status (ST_OK, sock);
}

static inline enum status
pop_size (void *buf, zmsg_t *msg, size_t size, int opt)
{
  if (zmsg_size (msg) < 1)
    return opt ? ST_DOES_NOT_EXIST : ST_BAD_FORMAT;

  zframe_t *frame = zmsg_pop (msg);
  if (zframe_size (frame) != size) {
    zframe_destroy (&frame);
    return ST_BAD_FORMAT;
  }

  memcpy (buf, zframe_data (frame), size);
  zframe_destroy (&frame);

  return ST_OK;
}

typedef void (*cmd_handler_t) (zmsg_t *, void *);

#define DECLARE_HANDLER(cmd)                    \
  static void handle_##cmd (zmsg_t *, void *)

#define DEFINE_HANDLER(cmd)                                 \
  static void handle_##cmd (zmsg_t *__args, void *__sock)

#define HANDLER(cmd) [cmd] = handle_##cmd

#define send_reply(reply) __send_reply ((reply), __sock)

#define report_status(status) __report_status ((status), __sock)

#define report_ok() __report_ok (__sock)

#define ARGS_SIZE (zmsg_size (__args))

#define POP_ARG_SZ(buf, size) (pop_size (buf, __args, size, 0))

#define POP_ARG(ptr) ({                         \
      typeof (ptr) __buf = ptr;                 \
      POP_ARG_SZ (__buf, sizeof (*__buf));      \
    })

#define POP_OPT_ARG_SZ(buf, size) (pop_size (buf, __args, size, 1))

#define POP_OPT_ARG(ptr) ({                     \
      typeof (ptr) __buf = ptr;                 \
      POP_OPT_ARG_SZ (__buf, sizeof (*__buf));  \
    })

#define FIRST_ARG (zmsg_first (__args))

#define GET_FROM_FRAME(val, frame)                              \
  ({                                                            \
    zframe_t *__f = (frame);                                    \
    typeof (val) *__v = &(val);                                 \
    int __r = 0;                                                \
    if (__f) {                                                  \
      *__v = *((typeof (__v)) zframe_data (__f));               \
      __r = 1;                                                  \
    }                                                           \
    __r;                                                        \
  })


struct handler_data {
  void *sock;
  cmd_handler_t *handlers;
  int nhandlers;
};

extern int control_handler (zloop_t *, zmq_pollitem_t *, void *);


#endif /* __CONTROL_UTILS_H__ */
