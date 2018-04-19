#ifndef __VARIANT_H__
#define __VARIANT_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#if defined (VARIANT_ARLAN_3424FE)              \
  || defined (VARIANT_ARLAN_3424PFE)            \
  || defined (VARIANT_SM_12F)
#define VARIANT_FE
#elif defined (VARIANT_ARLAN_3424GE)            \
  || defined (VARIANT_ARLAN_3448PGE)            \
  || defined (VARIANT_ARLAN_3448GE)             \
  || defined (VARIANT_ARLAN_3226PGE)            \
  || defined (VARIANT_ARLAN_3226GE)             \
  || defined (VARIANT_ARLAN_3050PGE)            \
  || defined (VARIANT_ARLAN_3050GE)             \
  || defined (VARIANT_ARLAN_3250PGE_SR)         \
  || defined (VARIANT_ARLAN_3226GE_SR)          \
  || defined (VARIANT_ARLAN_3226PGE_SR)
  || defined (VARIANT_ARLAN_3212GE)
#define VARIANT_GE
#else
#error Undefined or unsupported variant.
#endif /* VARIANT_* */

#endif /* __VARIANT_H__ */
