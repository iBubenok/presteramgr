#ifndef __SYSDEPS_H__
#define __SYSDEPS_H__

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <gtExtDrv/drivers/gtPciDrv.h>
#include <cpss/dxCh/dxChxGen/cpssHwInit/cpssDxChHwInit.h>

#include <control-proto.h>

#define FDB_MAX_ADDRS 16384

struct dev_info {
  uint32_t dev_id;
  GT_PCI_INT int_num;
  int n_ic_ports;
  int *ic_ports;
  int n_xg_phys;
  unsigned *xg_phys;
  CPSS_DXCH_PP_PHASE1_INIT_INFO_STC ph1_info;
};
extern struct dev_info *dev_info;

struct pm {
  uint8_t dev;
  uint8_t port;
};

extern CPSS_PORTS_BMP_STC ic0_ports_bmp;

#if defined (VARIANT_ARLAN_3424FE) || defined (VARIANT_ARLAN_3424PFE)

#define NDEVS 1
#define CPU_DEV 0
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                     \
  struct pm name[NPORTS] = {                       \
    {0, 1},  {0, 0},  {0, 3},  {0, 2},             \
    {0, 5},  {0, 4},  {0, 7},  {0, 6},             \
    {0, 9},  {0, 8},  {0, 11}, {0, 10},            \
    {0, 13}, {0, 12}, {0, 15}, {0, 14},            \
    {0, 17}, {0, 16}, {0, 19}, {0, 18},            \
    {0, 21}, {0, 20}, {0, 23}, {0, 22},            \
    {0, 26}, {0, 27}, {0, 24}, {0, 25}             \
  }

static inline int
IS_FE_PORT (int n)
{
  return n >= 0 && n < 24;
}

static inline int
IS_GE_PORT (int n)
{
  return n >= 24 && n < 28;
}

static inline int
IS_XG_PORT (int n)
{
  return 0;
}

static inline int
IS_PORT_PHYLESS (int n) {
  return 0;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 26: return PSR_PRIMARY;
  case 27: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_SM_12F)

#define NDEVS 1
#define CPU_DEV 0
#define NPORTS 16
#define DECLARE_PORT_MAP(name)                              \
  struct pm name[NPORTS] = {                                \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},                     \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},                     \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},                     \
    {0, 27}, {0, 26}, {0, 25}, {0, 24}                      \
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

static inline int
IS_PORT_PHYLESS (int n) {
  return 0;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 14: return PSR_PRIMARY;
  case 15: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_ARLAN_3424GE)

#define NDEVS 1
#define CPU_DEV 0
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 0},  {0, 1},  {0, 2},  {0, 3},          \
    {0, 4},  {0, 5},  {0, 6},  {0, 7},          \
    {0, 8},  {0, 9},  {0, 10}, {0, 11},         \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},         \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},         \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},         \
    {0, 26}, {0, 27}, {0, 24}, {0, 25}          \
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

static inline int
IS_PORT_PHYLESS (int n) {
  return 0;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 26: return PSR_PRIMARY;
  case 27: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_ARLAN_3212GE)

#define NDEVS 1
#define CPU_DEV 0
#define NPORTS 28
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 16}, {0, 17}, {0, 18}, {0, 19},         \
    {0, 0},  {0, 1},  {0, 2},  {0, 3},          \
    {0, 4},  {0, 5},  {0, 6},  {0, 7},          \
    {0, 8},  {0, 9},  {0, 10}, {0, 11},         \
    {0, 12}, {0, 13}, {0, 14}, {0, 15},         \
    {0, 20}, {0, 21}, {0, 22}, {0, 23},         \
    {0, 26}, {0, 27}, {0, 24}, {0, 25}          \
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

static inline int
IS_PORT_PHYLESS (int n) {
  return 0;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 26: return PSR_PRIMARY;
  case 27: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_ARLAN_3448PGE) || defined (VARIANT_ARLAN_3448GE)

#define NDEVS 2
#define NPORTS 52
#define CPU_DEV 1
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 1},  {0, 0},  {0, 3},  {0, 2},          \
    {0, 5},  {0, 4},  {0, 7},  {0, 6},          \
    {0, 9},  {0, 8},  {0, 11}, {0, 10},         \
    {0, 13}, {0, 12}, {0, 15}, {0, 14},         \
    {0, 17}, {0, 16}, {0, 19}, {0, 18},         \
    {0, 21}, {0, 20}, {0, 23}, {0, 22},         \
    {1, 1},  {1, 0},  {1, 3},  {1, 2},          \
    {1, 5},  {1, 4},  {1, 7},  {1, 6},          \
    {1, 9},  {1, 8},  {1, 11}, {1, 10},         \
    {1, 13}, {1, 12}, {1, 15}, {1, 14},         \
    {1, 17}, {1, 16}, {1, 19}, {1, 18},         \
    {1, 21}, {1, 20}, {1, 23}, {1, 22},         \
    {1, 26}, {1, 27}, {0, 24}, {0, 25}          \
  }

static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 48;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 48 && n < 52;
}

static inline int
IS_PORT_PHYLESS (int n) {
  return 0;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 50: return PSR_PRIMARY;
  case 51: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_ARLAN_3050PGE) || defined (VARIANT_ARLAN_3050GE)

#define NDEVS 2
#define NPORTS 52
#define CPU_DEV 1
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 1},  {0, 0},  {0, 3},  {0, 2},          \
    {0, 5},  {0, 4},  {0, 7},  {0, 6},          \
    {0, 9},  {0, 8},  {0, 11}, {0, 10},         \
    {0, 13}, {0, 12}, {0, 15}, {0, 14},         \
    {0, 17}, {0, 16}, {0, 19}, {0, 18},         \
    {0, 21}, {0, 20}, {0, 23}, {0, 22},         \
    {1, 1},  {1, 0},  {1, 3},  {1, 2},          \
    {1, 5},  {1, 4},  {1, 7},  {1, 6},          \
    {1, 9},  {1, 8},  {1, 11}, {1, 10},         \
    {1, 13}, {1, 12}, {1, 15}, {1, 14},         \
    {1, 17}, {1, 16}, {1, 19}, {1, 18},         \
    {1, 21}, {1, 20}, {1, 23}, {1, 22},         \
    {1, 26}, {1, 27}, {0, 24}, {0, 25}          \
  }

static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 50;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 50 && n < 52;
}

static inline int
IS_PORT_PHYLESS (int n) {
  return n >=48 && n < 52;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 50: return PSR_PRIMARY;
  case 51: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#elif defined (VARIANT_ARLAN_3226PGE) || defined (VARIANT_ARLAN_3226GE)

#define NDEVS 1
#define NPORTS 28
#define CPU_DEV 0
#define DECLARE_PORT_MAP(name)                  \
  struct pm name[NPORTS] = {                    \
    {0, 1},  {0, 0},  {0, 3},  {0, 2},          \
    {0, 5},  {0, 4},  {0, 7},  {0, 6},          \
    {0, 9},  {0, 8},  {0, 11}, {0, 10},         \
    {0, 13}, {0, 12}, {0, 15}, {0, 14},         \
    {0, 17}, {0, 16}, {0, 19}, {0, 18},         \
    {0, 21}, {0, 20}, {0, 23}, {0, 22},         \
    {0, 26}, {0, 27}, {0, 24}, {0, 25},         \
  }

static inline int IS_FE_PORT (int n)
{
  return 0;
}

static inline int IS_GE_PORT (int n)
{
  return n >= 0 && n < 26;
}

static inline int IS_XG_PORT (int n)
{
  return n >= 26 && n < 28;
}

static inline int
IS_PORT_PHYLESS (int n) {
  return n >=24 && n < 28;
}

static inline enum port_stack_role
PORT_STACK_ROLE (int n)
{
  switch (n) {
  case 26: return PSR_PRIMARY;
  case 27: return PSR_SECONDARY;
  default: return PSR_NONE;
  }
}

#else
#error Undefined or unsupported variant.
#endif /* VARIANT_* */

#define for_each_dev(var) for (var = 0; var < NDEVS; var++)

extern size_t sysdeps_default_stack_size;

#define SYSD_CSCD_TRUNK 127
extern void sysd_setup_ic (void);
extern int sysd_hw_dev_num (int);


#endif /* __SYSDEPS_H__ */
