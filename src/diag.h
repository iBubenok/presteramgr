#ifndef __DIAG_H__
#define __DIAG_H__

#include <control-proto.h>

extern uint32_t diag_pci_base_addr;
extern enum status diag_reg_read (uint32_t, uint32_t *);
extern enum status diag_bdc_set_mode (uint8_t);
extern enum status diag_bdc_read (uint32_t *);
extern enum status diag_ipdc_read (uint32_t *);
extern enum status diag_ipdc_set_mode (uint8_t);
extern enum status diag_bic_set_mode (uint8_t, uint8_t, uint8_t, vid_t);
extern enum status diag_bic_read (uint8_t, uint32_t *);
extern enum status diag_desc_read (uint8_t, uint8_t *, uint32_t *);
extern enum status diag_read_ret_cnt (uint8_t, uint32_t *);
extern enum status diag_dump_xg_port_qt2025_start (port_id_t, const char *);
extern enum status diag_dump_xg_port_qt2025_check (bool_t *);
extern enum status diag_dump_xg_port_qt2025 (port_id_t, uint32_t, uint32_t, uint16_t*);

#endif /* __DIAG_H__ */
