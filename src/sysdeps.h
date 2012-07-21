#ifndef __SYSDEPS_H__
#define __SYSDEPS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>

#if defined (VARIANT_ARLAN_3424FE)
#define DEVICE_ID CPSS_98DX2122_CNS
#define NDEVS 1
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                  \
  int name[NPORTS] = {                          \
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,   \
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, \
    27, 26, 25, 24                              \
  }

static inline int IS_FE_PORT (int n)
{
  return n >= 0 && n < 24;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 24 && n < 28;
}

static inline int IS_XG_PORT (int n)
{
  return 0;
}

#elif defined (VARIANT_SM_12F)
#define DEVICE_ID CPSS_98DX2122_CNS
#define NDEVS 1
#define NPORTS 16
#define DECLARE_PORT_MAP(name)                      \
  int name[NPORTS] = {                              \
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, \
    27, 26, 25, 24                                  \
  }

static inline int IS_FE_PORT (int n)
{
  return n >= 0 && n < 12;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 12 && n < 16;
}

static inline int IS_XG_PORT (int n)
{
  return 0;
}

#elif defined (VARIANT_ARLAN_3424GE)
#define DEVICE_ID CPSS_98DX4122_CNS
#define NDEVS 1
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                  \
  int name[NPORTS] = {                          \
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,   \
    13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, \
    27, 26, 25, 24                              \
  }

static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 24;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 24 && n < 28;
}

#else
#error Undefined or unsupported variant.
#endif /* VARIANT_* */

extern size_t sysdeps_default_stack_size;

#endif /* __SYSDEPS_H__ */
