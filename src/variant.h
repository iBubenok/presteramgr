#ifndef __VARIANT_H__
#define __VARIANT_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if defined (VARIANT_ARLAN_3424FE)              \
  || defined (VARIANT_ARLAN_3424FE_POE)         \
  || defined (VARIANT_SM_12F)
#define VARIANT_FE
#elif defined (VARIANT_ARLAN_3424GE)
#define VARIANT_GE
#else
#error Undefined or unsupported variant.
#endif /* VARIANT_* */

#endif /* __VARIANT_H__ */
