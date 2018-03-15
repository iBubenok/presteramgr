#ifndef __ENV_H__
#define __ENV_H__

#define HWST_ARLAN_3424GE_C   0
#define HWST_ARLAN_3424GE_C_S 1
#define HWST_ARLAN_3424GE_F   2
#define HWST_ARLAN_3424GE_F_S 3
#define HWST_ARLAN_3424GE_U   4
#define HWST_ARLAN_3250GE_SR  10
#define HWST_ARLAN_3250GE_FSR 13
#define HWST_ARLAN_3250GE_USR 16

extern void env_init (void);
extern int env_hw_subtype (void);

#endif /* __ENV_H__ */
