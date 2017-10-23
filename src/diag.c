#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <cpssdefs.h>
#include <cpss/dxCh/dxChxGen/diag/cpssDxChDiag.h>
#include <cpss/dxCh/dxChxGen/bridge/cpssDxChBrgCount.h>
#include <cpss/dxCh/dxChxGen/diag/cpssDxChDiagDescriptor.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpCtrl.h>
#include <cpss/generic/smi/cpssGenSmi.h>

#include <diag.h>
#include <sysdeps.h>
#include <debug.h>
#include <port.h>

uint32_t diag_pci_base_addr = 0;

enum status
diag_reg_read (uint32_t reg, uint32_t *val)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChDiagRegRead
            (diag_pci_base_addr,
             CPSS_CHANNEL_PEX_E,
             CPSS_DIAG_PP_REG_INTERNAL_E,
             reg, val, GT_FALSE));
  switch (rc) {
  case GT_OK: return ST_OK;
  default:    return ST_HEX;
  }
}

enum status
diag_bdc_set_mode (uint8_t mode)
{
  GT_STATUS rc;

  if (mode > 33)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChBrgCntDropCntrModeSet (0, mode));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
diag_ipdc_read (uint32_t *val) {
  GT_STATUS rc;

  rc = CRP (cpssDxChIpDropCntGet (0, val));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}

enum status
diag_ipdc_set_mode (uint8_t mode) {
  GT_STATUS rc;

  if (mode > 14)
    return ST_BAD_VALUE;

  rc = CRP (cpssDxChIpSetDropCntMode (0, mode));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_NOT_SUPPORTED: return ST_NOT_SUPPORTED;
  default:               return ST_HEX;
  }
}

enum status
diag_bdc_read (uint32_t *val)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgCntDropCntrGet (0, val));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}

enum status
diag_bic_set_mode (uint8_t set, uint8_t mode, uint8_t port, vid_t vid)
{
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgCntBridgeIngressCntrModeSet (0, set, mode, port, vid));
  switch (rc) {
  case GT_OK:            return ST_OK;
  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  case GT_OUT_OF_RANGE:  return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}

enum status
diag_bic_read (uint8_t set, uint32_t *dptr)
{
  CPSS_BRIDGE_INGRESS_CNTR_STC data;
  GT_STATUS rc;

  rc = CRP (cpssDxChBrgCntBridgeIngressCntrsGet (0, set, &data));
  switch (rc) {
  case GT_OK:
    *dptr++ = data.gtBrgInFrames;
    *dptr++ = data.gtBrgVlanIngFilterDisc;
    *dptr++ = data.gtBrgSecFilterDisc;
    *dptr   = data.gtBrgLocalPropDisc;
    return ST_OK;

  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}

enum status
diag_desc_read (uint8_t subj, uint8_t *v, uint32_t *p)
{
  CPSS_DXCH_DIAG_DESCRIPTOR_STC desc;
  GT_STATUS rc;

  rc = CRP (cpssDxChDiagDescriptorGet (0, subj, &desc));

  if (rc == GT_OK) {
    *v = desc.fieldValueValid[subj] == GT_TRUE;
    *p = desc.fieldValue[subj];
    return ST_OK;
  }

  switch (rc) {
  case GT_BAD_PARAM: return ST_BAD_VALUE;
  default:           return ST_HEX;
  }
}

enum status
diag_read_ret_cnt (uint8_t set, uint32_t *dptr) {
  CPSS_DXCH_IP_COUNTER_SET_STC data;
  GT_STATUS rc;

  rc = CRP (cpssDxChIpCntGet (0, set, &data));
  switch (rc) {
  case GT_OK:
    *dptr++ = data.inUcPkts;
    *dptr++ = data.inMcPkts;
    *dptr++ = data.inUcNonRoutedExcpPkts;
    *dptr++ = data.inUcNonRoutedNonExcpPkts;
    *dptr++ = data.inMcNonRoutedExcpPkts;
    *dptr++ = data.inMcNonRoutedNonExcpPkts;
    *dptr++ = data.inUcTrappedMirrorPkts;
    *dptr++ = data.inMcTrappedMirrorPkts;
    *dptr++ = data.mcRfpFailPkts;
    *dptr++ = data.outUcRoutedPkts;
    return ST_OK;

  case GT_HW_ERROR:      return ST_HW_ERROR;
  case GT_BAD_PARAM:     return ST_BAD_VALUE;
  default:               return ST_HEX;
  }
}

static pthread_t diag_dump_xg_port_qt2025_tid;
static pthread_mutex_t diag_dump_xg_port_qt2025_mutex;
static bool_t diag_dump_xg_port_qt2025_mutex_initialized = 0;
static bool_t diag_dump_xg_port_qt2025_in_progress = 0;

struct dump_qt2025_args {
  GT_U8           devNum;
  GT_U32          xsmiAddr;
  int             fd;
} __attribute__ ((packed));

static void*
dump_qt2025 (void *args)
{
  DEBUG("%s thread started", __FUNCTION__);

  struct dump_qt2025_args *dump_qt2025_args = NULL;
  struct {
    GT_U32 phyDev;
    GT_U32 regAddr;
    GT_U16 val;
  } __attribute__ ((packed)) dump_entry;
  GT_STATUS rc;
  ssize_t size;

  if (args == NULL) {
    DEBUG("ERROR: args == NULL");
    goto out;
  }

  dump_qt2025_args = (struct dump_qt2025_args *)args;

  for (dump_entry.phyDev = 0; dump_entry.phyDev <= 7; dump_entry.phyDev++) {
    switch (dump_entry.phyDev) {
      case 1:
      case 2:
      case 3:
      case 4:
      case 7:
        DEBUG("dump phy %d", dump_entry.phyDev);
        for (dump_entry.regAddr = 0; dump_entry.regAddr <= 0xffff; dump_entry.regAddr++) {
          rc = cpssXsmiRegisterRead(dump_qt2025_args->devNum,
                                    dump_qt2025_args->xsmiAddr,
                                    dump_entry.regAddr,
                                    dump_entry.phyDev,
                                    &dump_entry.val);

          if (rc == GT_OK) {
            size = write(dump_qt2025_args->fd, &dump_entry, sizeof(dump_entry));

            if (size != sizeof(dump_entry)) {
              DEBUG("write failed");
              goto out;
            }
          }
        }
        break;
      default:
        break;
    };
  }

out:
  if (pthread_mutex_lock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    DEBUG("ERROR: failed to lock qt2025 mutex");
    goto exit;
  }

  DEBUG("LOCK: qt2025 mutex");
  diag_dump_xg_port_qt2025_in_progress = 0;
  DEBUG("set in_progress == %d", diag_dump_xg_port_qt2025_in_progress);

  if (dump_qt2025_args) {
    close(dump_qt2025_args->fd);
  }

  if (pthread_mutex_unlock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    DEBUG("ERROR: failed to unlock qt2025 mutex");
    goto exit;
  }

  DEBUG("UNLOCK: qt2025 mutex");

exit:
  if (dump_qt2025_args) {
    free(dump_qt2025_args);
  }

  DEBUG("%s thread exit", __FUNCTION__);
  return NULL;
}

enum status
diag_dump_xg_port_qt2025_start (port_id_t pid, const char *filename)
{
  DEBUG("call %s (%d, %s)", __FUNCTION__, pid, filename);

  enum status result = ST_OK;
  struct port *port = NULL;
  struct dump_qt2025_args *args = NULL;

  port = port_ptr(pid);

  if (!port || !IS_XG_PORT(pid - 1)) {
    DEBUG("ERROR: port not valid or is not 10G");
    result = ST_BAD_VALUE;
    goto out;
  }


  if (!diag_dump_xg_port_qt2025_mutex_initialized) {
    DEBUG("qt2025 mutex need initialization");

    if (pthread_mutex_init(&diag_dump_xg_port_qt2025_mutex, NULL) != 0) {
      DEBUG("ERROR: failed to initialize qt2025 mutex");
      result = ST_HEX;
      goto out;
    }

    diag_dump_xg_port_qt2025_mutex_initialized = 1;
  }

  if (pthread_mutex_lock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    DEBUG("ERROR: failed to lock qt2025 mutex");
    result = ST_HEX;
    goto out;
  };

  DEBUG("LOCK: qt2025 mutex");

  if (diag_dump_xg_port_qt2025_in_progress) {
    DEBUG("ERROR: thread allready in progress");
    result = ST_BUSY;
    goto unlock;
  };

  args = malloc(sizeof(struct dump_qt2025_args));

  if (args == NULL) {
    DEBUG("ERROR: malloc error");
    result = ST_MALLOC_ERROR;
    goto unlock;
  }

  args->devNum = port->ldev;
  args->xsmiAddr = 0x18 + port->lport - 24;
  args->fd = open(filename, O_WRONLY | O_TRUNC | O_SYNC | O_CREAT);

  if (args->fd == -1) {
    DEBUG("ERROR: cannot open file: %s", filename);
    result = ST_HW_ERROR;
    goto unlock;
  }

  diag_dump_xg_port_qt2025_in_progress = 1;
  DEBUG("set in_progress == %d", diag_dump_xg_port_qt2025_in_progress);

  if (pthread_mutex_unlock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    DEBUG("ERROR: failed to unlock qt2025 mutex");
    result = ST_HEX;
  }

  DEBUG("UNLOCK: qt2025 mutex");

  if (pthread_create(&diag_dump_xg_port_qt2025_tid, NULL, dump_qt2025, args) != 0) {
    DEBUG("ERROR: failed to create thread");
    result = ST_HEX;
    diag_dump_xg_port_qt2025_in_progress = 0;
    DEBUG("set in_progress == %d", diag_dump_xg_port_qt2025_in_progress);
    goto out;
  }

  if (pthread_detach(diag_dump_xg_port_qt2025_tid) != 0) {
    DEBUG("ERROR: failed to detach thread");
    result = ST_HEX;
    goto out;
  }


unlock:
  if (pthread_mutex_unlock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    DEBUG("ERROR: failed to unlock qt2025 mutex");
    result = ST_HEX;
  }

  DEBUG("UNLOCK: qt2025 mutex");

out:
  DEBUG("%s: returns %d", __FUNCTION__, result);
  return result;
}

enum status
diag_dump_xg_port_qt2025_check (bool_t *in_progress)
{
  enum status result;

  if (in_progress == NULL) {
    result = ST_BAD_VALUE;
    goto out;
  }

  if (!diag_dump_xg_port_qt2025_mutex_initialized) {
    if (pthread_mutex_init(&diag_dump_xg_port_qt2025_mutex, NULL) != 0) {
      result = ST_HEX;
      goto out;
    }
    diag_dump_xg_port_qt2025_mutex_initialized = 1;
  }

  if (pthread_mutex_lock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    result = ST_HEX;
    goto out;
  };

  *in_progress = diag_dump_xg_port_qt2025_in_progress;
  result = ST_OK;

  if (pthread_mutex_unlock(&diag_dump_xg_port_qt2025_mutex) != 0) {
    result = ST_HEX;
  }
out:
  return result;
}

enum status
diag_dump_xg_port_qt2025 (port_id_t pid, uint32_t phy, uint32_t reg, uint16_t *val)
{
  struct port *port = NULL;
  GT_STATUS rc;

  port = port_ptr(pid);

  if (!port || !IS_XG_PORT(pid - 1)) {
    return ST_BAD_VALUE;
  }

  rc = cpssXsmiRegisterRead(port->ldev,
                            0x18 + port->lport - 24,
                            (GT_U32) reg,
                            (GT_U32) phy,
                            (GT_U16*) val);

  if (rc != GT_OK) {
    return ST_HW_ERROR;
  }

  return ST_OK;
}
