#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <data.h>
#include <mac.h>
#include <port.h>
#include <trunk.h>
#include <log.h>
#include <utils.h>

enum status
data_encode_port_state (struct port_link_state *state,
                        const CPSS_PORT_ATTRIBUTES_STC *attrs)
{
  static enum port_speed psm[] = {
    [CPSS_PORT_SPEED_10_E]    = PORT_SPEED_10,
    [CPSS_PORT_SPEED_100_E]   = PORT_SPEED_100,
    [CPSS_PORT_SPEED_1000_E]  = PORT_SPEED_1000,
    [CPSS_PORT_SPEED_10000_E] = PORT_SPEED_10000,
    [CPSS_PORT_SPEED_12000_E] = PORT_SPEED_12000,
    [CPSS_PORT_SPEED_2500_E]  = PORT_SPEED_2500,
    [CPSS_PORT_SPEED_5000_E]  = PORT_SPEED_5000,
    [CPSS_PORT_SPEED_13600_E] = PORT_SPEED_13600,
    [CPSS_PORT_SPEED_20000_E] = PORT_SPEED_20000,
    [CPSS_PORT_SPEED_40000_E] = PORT_SPEED_40000,
    [CPSS_PORT_SPEED_16000_E] = PORT_SPEED_16000,
    [CPSS_PORT_SPEED_NA_E]    = PORT_SPEED_NA
  };

  state->link = attrs->portLinkUp == GT_TRUE;
  state->duplex = attrs->portDuplexity == CPSS_PORT_FULL_DUPLEX_E;
  assert (attrs->portSpeed >= 0 && attrs->portSpeed <= CPSS_PORT_SPEED_NA_E);
  state->speed = psm[attrs->portSpeed];

  return ST_OK;
}

enum status
data_decode_port_speed (CPSS_PORT_SPEED_ENT *outp, enum port_speed inp)
{
  static CPSS_PORT_SPEED_ENT psm[] = {
    [PORT_SPEED_10]    = CPSS_PORT_SPEED_10_E,
    [PORT_SPEED_100]   = CPSS_PORT_SPEED_100_E,
    [PORT_SPEED_1000]  = CPSS_PORT_SPEED_1000_E,
    [PORT_SPEED_10000] = CPSS_PORT_SPEED_10000_E,
    [PORT_SPEED_12000] = CPSS_PORT_SPEED_12000_E,
    [PORT_SPEED_2500]  = CPSS_PORT_SPEED_2500_E,
    [PORT_SPEED_5000]  = CPSS_PORT_SPEED_5000_E,
    [PORT_SPEED_13600] = CPSS_PORT_SPEED_13600_E,
    [PORT_SPEED_20000] = CPSS_PORT_SPEED_20000_E,
    [PORT_SPEED_40000] = CPSS_PORT_SPEED_40000_E,
    [PORT_SPEED_16000] = CPSS_PORT_SPEED_16000_E
  };

  if (inp < PORT_SPEED_10 || inp >= PORT_SPEED_NA)
    return ST_BAD_VALUE;

  *outp = psm[inp];
  return ST_OK;
}

enum status
data_decode_stp_state (CPSS_STP_STATE_ENT *cs, enum port_stp_state state)
{
  static CPSS_STP_STATE_ENT csm[] = {
    [STP_STATE_DISABLED] = CPSS_STP_DISABLED_E,
    [STP_STATE_DISCARDING] = CPSS_STP_BLCK_LSTN_E,
    [STP_STATE_LEARNING] =  CPSS_STP_LRN_E,
    [STP_STATE_FORWARDING] = CPSS_STP_FRWRD_E
  };

  if (state < 0 || state >= STP_STATE_MAX)
    return ST_BAD_VALUE;

  *cs = csm[state];

  return ST_OK;
}

void
data_encode_fdb_addrs (zmsg_t *msg, vid_t vid, port_id_t pid)
{
  GT_U32 i;
  port_id_t p;

  for (i = 0; i < FDB_MAX_ADDRS; i++) {

  if (fdb[i].valid ) { // TODO remove operator & block
    DEBUG("%04x: eType==%hhu, dst.type==%hhu, " MAC_FMT ", %hhu:%hhu:%hhu, Vid==%03x, "
          "%hX,  %s\n",
          i, fdb[i].me.key.entryType, fdb[i].me.dstInterface.type, MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther),
          fdb[i].me.dstInterface.devPort.devNum, fdb[i].me.dstInterface.devPort.portNum,
          fdb[i].me.dstInterface.trunkId, fdb[i].me.key.key.macVlan.vlanId, fdb[i].pc_aging_status,
          (fdb[i].me.userDefined == 0)? "UNUSED" : (fdb[i].me.userDefined == 1)? "FOREIGN" : (fdb[i].me.userDefined == 2)? "DYNAMIC" : (fdb[i].me.userDefined == 3)? "STATIC" : (fdb[i].me.userDefined == 4)? "OWN": "UNKNOWN" );
  }

/*  GT_BOOL fvalid = 0xff, fskip=0xff, faged=0xff; //TODO remove block
  GT_U8 fdev= 0xff;
  CPSS_MAC_ENTRY_EXT_STC fme;
  cpssDxChBrgFdbMacEntryRead(0, i, &fvalid, &fskip, &faged, &fdev, &fme);
  if (fvalid || faged || fskip || fme.key.key.macVlan.vlanId) {
    DEBUG("%04x: eType==%hhu, dst.type==%hhu, " MAC_FMT ", %hhu:%hhu:%hhu, Vid==%03x, "
          " %s, %d:%d:%d:%d\n",
          i, fme.key.entryType, fme.dstInterface.type, MAC_ARG(fme.key.key.macVlan.macAddr.arEther),
          fme.dstInterface.devPort.devNum, fme.dstInterface.devPort.portNum,
          fme.dstInterface.trunkId, fme.key.key.macVlan.vlanId,
          (fme.userDefined == 0)? "UNUSED" : (fme.userDefined == 1)? "FOREIGN" : (fme.userDefined == 2)? "DYNAMIC" : (fme.userDefined == 3)? "STATIC" : (fme.userDefined == 4)? "OWN": "UNKNOWN", fvalid, fskip, faged, fdev );
  }
*/
    if (fdb[i].valid &&
        fdb[i].me.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E &&
        (vid == ALL_VLANS || fdb[i].me.key.key.macVlan.vlanId == vid) &&
        fdb[i].me.dstInterface.type == CPSS_INTERFACE_PORT_E &&
/*        ((p = port_id (fdb[i].me.dstInterface.devPort.devNum,
                        fdb[i].me.dstInterface.devPort.portNum))  */
        ((p = port_id (stack_id,    /* FIXME do it a right way through gif  */
                        fdb[i].me.dstInterface.devPort.portNum)) &&
         (pid == ALL_PORTS || pid == p))
        && fdb[i].me.key.key.macVlan.vlanId != 4095) {
      struct {
        struct mac_entry me;
        port_id_t ports[1];
        uint16_t dev;
      } __attribute__ ((packed)) tmp;

      memcpy (tmp.me.mac, fdb[i].me.key.key.macVlan.macAddr.arEther, 6);
      tmp.me.vid = fdb[i].me.key.key.macVlan.vlanId;
      if (fdb[i].secure)
        tmp.me.type = MET_SECURE;
      else if (fdb[i].me.isStatic)
        tmp.me.type = MET_STATIC;
      else
        tmp.me.type = MET_DYNAMIC;
      tmp.ports[0] = p;
      tmp.dev = fdb[i].me.dstInterface.devPort.devNum;

      zmsg_addmem (msg, &tmp, sizeof (tmp));
    }
  }
}

void
data_encode_fdb_addrs_vif (zmsg_t *msg, vid_t vid, vif_id_t vif_target) {
  GT_U32 i;

  for (i = 0; i < FDB_MAX_ADDRS; i++) {
    struct vif *vif;

if (fdb[i].valid ) { // TODO remove operator & block
  DEBUG("%04x: eType==%hhu, dst.type==%hhu, " MAC_FMT ", %hhu:%hhu:%hhu, Vid==%03x, "
        "%hX,  %s\n",
        i, fdb[i].me.key.entryType, fdb[i].me.dstInterface.type, MAC_ARG(fdb[i].me.key.key.macVlan.macAddr.arEther),
        fdb[i].me.dstInterface.devPort.devNum, fdb[i].me.dstInterface.devPort.portNum,
        fdb[i].me.dstInterface.trunkId, fdb[i].me.key.key.macVlan.vlanId, fdb[i].pc_aging_status,
        (fdb[i].me.userDefined == 0)? "UNUSED" : (fdb[i].me.userDefined == 1)? "FOREIGN" : (fdb[i].me.userDefined == 2)? "DYNAMIC" : (fdb[i].me.userDefined == 3)? "STATIC" : (fdb[i].me.userDefined == 4)? "OWN": "UNKNOWN" );
}

/*  GT_BOOL fvalid = 0xff, fskip=0xff, faged=0xff; //TODO remove block
  GT_U8 fdev= 0xff;
  CPSS_MAC_ENTRY_EXT_STC fme;
  cpssDxChBrgFdbMacEntryRead(0, i, &fvalid, &fskip, &faged, &fdev, &fme);
  if (fvalid || faged || fskip || fme.key.key.macVlan.vlanId) {
    DEBUG("%04x: eType==%hhu, dst.type==%hhu, " MAC_FMT ", %hhu:%hhu:%hhu, Vid==%03x, "
          " %s, %d:%d:%d:%d\n",
          i, fme.key.entryType, fme.dstInterface.type, MAC_ARG(fme.key.key.macVlan.macAddr.arEther),
          fme.dstInterface.devPort.devNum, fme.dstInterface.devPort.portNum,
          fme.dstInterface.trunkId, fme.key.key.macVlan.vlanId,
          (fme.userDefined == 0)? "UNUSED" : (fme.userDefined == 1)? "FOREIGN" : (fme.userDefined == 2)? "DYNAMIC" : (fme.userDefined == 3)? "STATIC" : (fme.userDefined == 4)? "OWN": "UNKNOWN", fvalid, fskip, faged, fdev );
  }
*/

    if (fdb[i].valid &&
        fdb[i].me.key.entryType == CPSS_MAC_ENTRY_EXT_TYPE_MAC_ADDR_E &&
        (vid == ALL_VLANS || fdb[i].me.key.key.macVlan.vlanId == vid) &&
        ((fdb[i].me.dstInterface.type == CPSS_INTERFACE_PORT_E
          && fdb[i].me.dstInterface.devPort.portNum != CPSS_CPU_PORT_NUM_CNS
          && (vif = vif_by_hw(fdb[i].me.dstInterface.devPort.devNum,
                              fdb[i].me.dstInterface.devPort.portNum)))
         || (fdb[i].me.dstInterface.type == CPSS_INTERFACE_TRUNK_E
             && (vif = vif_by_trunkid(fdb[i].me.dstInterface.trunkId))))
         && (vif_target == ALL_VIFS || vif_target == vif->id)
        && fdb[i].me.key.key.macVlan.vlanId != 4095) {
      struct {
        struct mac_entry me;
        vif_id_t vifid;
      } __attribute__ ((packed)) tmp;

      memcpy (tmp.me.mac, fdb[i].me.key.key.macVlan.macAddr.arEther, 6);
      tmp.me.vid = fdb[i].me.key.key.macVlan.vlanId;
      if (fdb[i].secure)
        tmp.me.type = MET_SECURE;
      else if (fdb[i].me.isStatic)
        tmp.me.type = MET_STATIC;
      else
        tmp.me.type = MET_DYNAMIC;
      tmp.vifid = vif->id;

      zmsg_addmem (msg, &tmp, sizeof (tmp));
    }
  }
}

void
data_encode_vct_cable_status (struct vct_cable_status *d,
                              const CPSS_VCT_CABLE_STATUS_STC *s,
                              int fe)
{
  int i, num = fe ? 2 : 4;
  static const uint8_t tsm[] = {
    [CPSS_VCT_TEST_FAIL_E]          = VS_TEST_FAILED,
    [CPSS_VCT_NORMAL_CABLE_E]       = VS_NORMAL_CABLE,
    [CPSS_VCT_OPEN_CABLE_E]         = VS_OPEN_CABLE,
    [CPSS_VCT_SHORT_CABLE_E]        = VS_SHORT_CABLE,
    [CPSS_VCT_IMPEDANCE_MISMATCH_E] = VS_IMPEDANCE_MISMATCH,
    [CPSS_VCT_SHORT_WITH_PAIR0_E]   = VS_SHORT_WITH_PAIR0,
    [CPSS_VCT_SHORT_WITH_PAIR1_E]   = VS_SHORT_WITH_PAIR1,
    [CPSS_VCT_SHORT_WITH_PAIR2_E]   = VS_SHORT_WITH_PAIR2,
    [CPSS_VCT_SHORT_WITH_PAIR3_E]   = VS_SHORT_WITH_PAIR3
  };
  static const uint8_t clm[] = {
    [CPSS_VCT_LESS_THAN_50M_E] = CL_LESS_THAN_50M,
    [CPSS_VCT_50M_80M_E]       = CL_50M_80M,
    [CPSS_VCT_80M_110M_E]      = CL_80M_110M,
    [CPSS_VCT_110M_140M_E]     = CL_110M_140M,
    [CPSS_VCT_MORE_THAN_140_E] = CL_MORE_THAN_140M,
    [CPSS_VCT_UNKNOWN_LEN_E]   = CL_UNKNOWN
  };

  d->ok = 1;
  d->npairs = num;
  for (i = 0; i < num; i++) {
    d->pair_status[i].status = tsm[s->cableStatus[i].testStatus];
    d->pair_status[i].length = s->cableStatus[i].errCableLen;
    if (s->cableStatus[i].testStatus != CPSS_VCT_NORMAL_CABLE_E)
      d->ok = 0;
  }

  d->length = d->ok ? clm[s->normalCableLen] : CL_UNKNOWN;

  switch (s->phyType) {
  case CPSS_VCT_PHY_100M_E:
    d->phy_type = PT_100;
    break;
  case  CPSS_VCT_PHY_10000M_E:
    d->phy_type = PT_10000;
    break;
  default:
    d->phy_type = PT_1000;
  }
}
