#ifndef __ENV_H__
#define __ENV_H__

#define HWST_ARLAN_3424GE     0
#define HWST_ARLAN_3424GE_F   1
#define HWST_ARLAN_3424GE_C   2
#define HWST_ARLAN_3424GE_F_S 3
#define HWST_ARLAN_3424GE_C_S 4

extern void env_init (void);
extern int env_hw_subtype (void);

#endif /* __ENV_H__ */
