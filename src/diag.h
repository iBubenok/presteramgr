#ifndef __DIAG_H__
#define __DIAG_H__

#include <control-proto.h>

extern uint32_t diag_pci_base_addr;
extern enum status diag_reg_read (uint32_t, uint32_t *);
extern enum status diag_bdc_set_mode (uint8_t);
extern enum status diag_bdc_read (uint32_t *);
extern enum status diag_bic_set_mode (uint8_t, uint8_t, uint8_t, vid_t);
extern enum status diag_bic_read (uint8_t, uint32_t *);
extern enum status diag_desc_read (uint8_t, uint8_t *, uint32_t *);

#endif /* __DIAG_H__ */
