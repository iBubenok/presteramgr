
#include <cpssdefs.h>
#include <cpss/generic/cpssTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIpTypes.h>
#include <cpss/dxCh/dxChxGen/ip/cpssDxChIp.h>
#include <cpss/dxCh/dxChxGen/ipLpmEngine/cpssDxChIpLpm.h>

#include <utlist.h>
#include <uthash.h>
#include <route.h>
#include <route-p.h>
#include <ret.h>
#include <debug.h>
#include <ret_group.h>
#include <sysdeps.h>
#include <port.h>


struct re_idx {
  struct gw gw;
  int idx;
};

struct list_re_idx {
    struct re_idx val;
    struct list_re_idx *prev; /* needed for a doubly-linked list only */
    struct list_re_idx *next; /* needed for singly- or doubly-linked lists */
};


struct re_group {
  int group_id;
  int re_count;
  int first_idx;
  int numOfPaths;
  struct list_re_idx *re_idx;
  struct list_route_pfx_pbr *pfx_pbr;
  
  UT_hash_handle hh;
};


static struct list_int *group_id = NULL;
static struct re_group *re_group = NULL;

#define FR(term)                   \
  if ((term) != ST_OK) {  \
    DEBUG("\nError: %s\n", #term); \
    return ST_BAD_VALUE;           \
  }

static enum status
get_port_ptr (uint16_t pid, struct port** port)
{
  (*port) = port_ptr(pid);
  if (!(*port)) {
    DEBUG("%s: port: %d - invalid port_ptr (NULL)\n", __FUNCTION__, pid);
    return ST_BAD_VALUE;
  }
  if (is_stack_port(*port)) {
    DEBUG("%s: port: %d - is stack port\n", __FUNCTION__, pid);
    return ST_BAD_VALUE;
  }
  return ST_OK;
}

int ret_group_list_int_cmp(struct list_int *a, struct list_int *b) {
  
  if (a->val > b->val) {
    return 1;
  }

  if (a->val < b->val) {
    return -1;
  }

  return 0;
}

int get_new_group_id(){
  struct list_int *out = NULL, elt, *new = NULL;
  int i = 1;

  while (true) {
    elt.val = i;
    DL_SEARCH(group_id, out, &elt, ret_group_list_int_cmp);
    if (!out) {
      new = calloc(1 , sizeof(struct list_int));
      new->val = i;
      DL_APPEND(group_id, new);
      return i;
    }
    i++;
  }
  return 0;
}

void del_id(int id) {
  struct list_int del, *out;
  del.val = id;
  DL_SEARCH(group_id, out, &del, ret_group_list_int_cmp);
  if (out){
    DL_DELETE(group_id, out);
    free(out);    
  }
}

int list_re_idx_cmp(struct list_re_idx *a,  struct list_re_idx *b) {
  if (a->val.gw.addr.u32Ip == b->val.gw.addr.u32Ip && a->val.gw.vid == b->val.gw.vid) 
    return 0;
  else 
    return 1;
}
int list_re_idx_cmp2(struct list_re_idx *a,  struct list_re_idx *b) {
  if (a->val.idx == b->val.idx) 
    return 0;
  else 
    return 1;
}

int list_route_pfx_pbr_cmp_pfx(struct list_route_pfx_pbr *a, struct list_route_pfx_pbr *b) {
  if (a->val.type != ROUTE_PFX || b->val.type != ROUTE_PFX) return 1;
  if (a->val.data.pfx.addr.u32Ip == b->val.data.pfx.addr.u32Ip && a->val.data.pfx.alen == b->val.data.pfx.alen) 
    return 0;
  else 
    return 1;
}

int list_route_pfx_pbr_cmp_pbr(struct list_route_pfx_pbr *a, struct list_route_pfx_pbr *b) {
  if (a->val.type != ROUTE_PBR || b->val.type != ROUTE_PBR) return 1;
  if (a->val.data.pbr.ltt_index.row == b->val.data.pbr.ltt_index.row
  && a->val.data.pbr.ltt_index.colum == b->val.data.pbr.ltt_index.colum
  && a->val.data.pbr.interface.num == b->val.data.pbr.interface.num
  && a->val.data.pbr.interface.type == b->val.data.pbr.interface.type)
    return 0;
  else
    return 1;
}

bool_t compare_lists(int gw_count, struct gw *gw, struct list_re_idx *list) {
      struct list_re_idx *out, elt;
      int list_count = 0;
      DL_FOREACH(list, out) {
        list_count++;
      }

      if (gw_count != list_count) return false;

      int i = 0;
      for (i = 0; i < gw_count; i++) {
        elt.val.gw = gw[i];
        DL_SEARCH(list, out, &elt, list_re_idx_cmp);
        if (!out) {
          return false;
        }
      }
      return true;
}

int list_re_idx_cmp_sort(struct list_re_idx *a, struct list_re_idx *b){
  if (a->val.idx > b->val.idx) return 1;
  if (a->val.idx < b->val.idx) return -1;
  return 0;
}

int ret_group_add(int gw_count, struct gw *gw, uint32_t ip, int alen) {
  struct re *ret = NULL;
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  struct re_group *el, *tmp, *group_add;
  struct list_route_pfx_pbr *out, elt, *add;
  struct list_re_idx *re_idx_add;
  GT_IPADDR addr;
  int idxs[8];
  addr.u32Ip = htonl(ip);
  memset(&elt, 0, sizeof(struct list_route_pfx_pbr));
  memset(&re, 0, sizeof(re));
  HASH_ITER(hh, re_group, el, tmp) {
    bool_t result = compare_lists(gw_count, gw, el->re_idx);
    if (result) {
      elt.val.type = ROUTE_PFX;
      elt.val.data.pfx.addr = addr;
      elt.val.data.pfx.alen = alen;
      DL_SEARCH(el->pfx_pbr, out, &elt, list_route_pfx_pbr_cmp_pfx);
      if (!out) {
        add = calloc(1, sizeof(struct list_route_pfx_pbr));
        add->val.type = ROUTE_PFX;
        add->val.data.pfx.addr.u32Ip = htonl(ip);
        add->val.data.pfx.alen = alen;
        DL_APPEND(el->pfx_pbr, add);
      }

      // DL_SORT(re_group->re_idx, list_re_idx_cmp_sort);
      // DL_FOREACH(re_group->re_idx, re_idx_el) {
      //   if (re_idx_el->)
      // }
      re.ipLttEntry.routeEntryBaseIndex = el->first_idx;
      re.ipLttEntry.numOfPaths = el->numOfPaths;
      if (el->numOfPaths == -1) {
        re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
        re.ipLttEntry.numOfPaths = 0;
      }
      CRP (cpssDxChIpLpmIpv4UcPrefixAdd
           (0, 0, addr, alen, &re, GT_TRUE));
      return el->group_id;
    }
  }

  group_add = calloc(1, sizeof(struct re_group));
  group_add->group_id = get_new_group_id();
  group_add->re_count = gw_count;

  add = calloc(1, sizeof(struct list_route_pfx_pbr));
  add->val.type = ROUTE_PFX;
  add->val.data.pfx.addr = addr;
  add->val.data.pfx.alen = alen;
  DL_APPEND(group_add->pfx_pbr, add);

  if (!res_pop(gw_count, idxs)) {
    DEBUG("res_pop gw_count %d \n", gw_count);
  }
  group_add->first_idx = idxs[0];

  int i = 0;
  int start = 0;
  int end = gw_count - 1;
  for (i = 0; i < gw_count; i++) {
    re_idx_add = calloc(1, sizeof(struct list_re_idx));
    //
    re_idx_add->val.gw.addr.u32Ip = gw[i].addr.u32Ip;
    re_idx_add->val.gw.vid = gw[i].vid;
    // ret_add();
    ret = ret_add(&gw[i], group_add->group_id);

    if (ret == NULL) {
      DEBUG("ret = NULL \n");
    }
    if (ret != NULL && ret_get_valid(ret)){
      re_idx_add->val.idx = idxs[start];
      start++;
      ret_set_re_to_idx(ret, re_idx_add->val.idx);
    }
    if (ret != NULL && !ret_get_valid(ret)) {
      re_idx_add->val.idx = idxs[end];
      re_idx_add->val.idx = DROP_RE_IDX;
      end--;
    }
    DL_APPEND(group_add->re_idx, re_idx_add);
  }
  group_add->numOfPaths = start - 1;

  re.ipLttEntry.routeEntryBaseIndex = group_add->first_idx;
  re.ipLttEntry.numOfPaths = group_add->numOfPaths;
  if (group_add->numOfPaths == -1) {
    re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
    re.ipLttEntry.numOfPaths = 0;
  }
  CRP (cpssDxChIpLpmIpv4UcPrefixAdd
        (0, 0, addr, alen, &re, GT_TRUE));
  HASH_ADD(hh, re_group, group_id, sizeof(int), group_add);

  for (i = 0; i < gw_count; i++) {
    ret_arpc_request_addr(&gw[i]);
  }

  return group_add->group_id;
}

enum status ret_group_ltt_tcam_set(struct route_pbr *pbr, struct re_group *group) {
  int devs[NDEVS];
  int dev_count = 0;
  int d;
  struct port *port = NULL;
  switch (pbr->interface.type) {
    case PCL_INTERFACE_TYPE_VLAN:
      dev_count = NDEVS;
      for_each_dev(d) { devs[d] = d; };
      break;
    case PCL_INTERFACE_TYPE_PORT:
      FR(get_port_ptr(pbr->interface.num, &port));
      dev_count = 1;
      devs[0]   = port->ldev;
      break;
    default:
      return ST_BAD_VALUE;
  };

  for (d = 0; d < dev_count; d++) {
    CPSS_DXCH_IP_LTT_ENTRY_STC ipLttEntry;
    memset(&ipLttEntry, 0, sizeof(CPSS_DXCH_IP_LTT_ENTRY_STC));

    ipLttEntry.routeEntryBaseIndex = group->first_idx;
    ipLttEntry.numOfPaths = group->numOfPaths;
    if (group->numOfPaths == -1) {
      ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
      ipLttEntry.numOfPaths = 0;
    }
    CRP (cpssDxChIpLttWrite(devs[d], pbr->ltt_index.row, pbr->ltt_index.colum, &ipLttEntry));

    // CPSS_DXCH_IPV4_PREFIX_STC   prefixPtr;
    // CPSS_DXCH_IPV4_PREFIX_STC   maskPtr;

    // prefixPtr.vrId = 0;
    // prefixPtr.isMcSource = GT_FALSE;
    // // prefixPtr.ipAddr.u32Ip = nextHop;
    // memcpy(prefixPtr.ipAddr.arIP, nextHop,4);

    // maskPtr.vrId = 0;
    // maskPtr.isMcSource = GT_FALSE;
    // maskPtr.ipAddr.u32Ip = 0xFFFFFFFF;
    // // rc = cpssDxChIpv4PrefixSet(devs[d], ltt_index->row, ltt_index->colum, &prefixPtr, &maskPtr);
    // // DEBUG("SUFIK %u\n", rc);
    // CRP (cpssDxChIpv4PrefixSet(devs[d], ltt_index->row, ltt_index->colum, &prefixPtr, &maskPtr));
  }
  return ST_OK;
}

int ret_group_add_pbr(int gw_count, struct gw *gw, struct pbr_entry *pe ) {
  struct re *ret = NULL;
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  struct re_group *el, *tmp, *group_add;
  struct list_route_pfx_pbr *out, elt, *add = NULL;
  struct list_re_idx *re_idx_add;
  // GT_IPADDR addr;
  int idxs[8];
  // addr.u32Ip = htonl(ip);
  memset(&elt, 0, sizeof(struct list_route_pfx_pbr));
  memset(&re, 0, sizeof(re));
  HASH_ITER(hh, re_group, el, tmp) {
    bool_t result = compare_lists(gw_count, gw, el->re_idx);
    if (result) {
      elt.val.type = ROUTE_PBR;
      elt.val.data.pbr.ltt_index = *pbr_get_ltt_index(pe);
      elt.val.data.pbr.interface = *pbr_get_interface(pe);

      // elt.val.data.pfx.addr = addr;
      // elt.val.data.pfx.alen = alen;
      DL_SEARCH(el->pfx_pbr, out, &elt, list_route_pfx_pbr_cmp_pbr);
      if (!out) {
        add = calloc(1, sizeof(struct list_route_pfx_pbr));
        add->val.type = ROUTE_PBR;
        add->val.data.pbr.ltt_index = *pbr_get_ltt_index(pe);
        add->val.data.pbr.interface = *pbr_get_interface(pe);
        // add->val.data.pfx.addr.u32Ip = htonl(ip);
        // add->val.data.pfx.alen = alen;
        DL_APPEND(el->pfx_pbr, add);
        out = add;
      }

      // DL_SORT(re_group->re_idx, list_re_idx_cmp_sort);
      // DL_FOREACH(re_group->re_idx, re_idx_el) {
      //   if (re_idx_el->)
      // }
      re.ipLttEntry.routeEntryBaseIndex = el->first_idx;
      re.ipLttEntry.numOfPaths = el->numOfPaths;
      if (el->numOfPaths == -1) {
        re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
        re.ipLttEntry.numOfPaths = 0;
      }
      // CRP (cpssDxChIpLpmIpv4UcPrefixAdd
      //      (0, 0, addr, alen, &re, GT_TRUE));
      ret_group_ltt_tcam_set(&out->val.data.pbr, re_group);
      return el->group_id;
    }
  }

  group_add = calloc(1, sizeof(struct re_group));
  group_add->group_id = get_new_group_id();
  group_add->re_count = gw_count;

  add = calloc(1, sizeof(struct list_route_pfx_pbr));
  add->val.type = ROUTE_PBR;
  add->val.data.pbr.ltt_index = *pbr_get_ltt_index(pe);
  add->val.data.pbr.interface = *pbr_get_interface(pe);
  // add->val.data.pfx.addr = addr;
  // add->val.data.pfx.alen = alen;
  DL_APPEND(group_add->pfx_pbr, add);

  if (!res_pop(gw_count, idxs)) {
    DEBUG("res_pop gw_count %d \n", gw_count);
  }
  group_add->first_idx = idxs[0];

  int i = 0;
  int start = 0;
  int end = gw_count - 1;
  for (i = 0; i < gw_count; i++) {
    re_idx_add = calloc(1, sizeof(struct list_re_idx));
    //
    re_idx_add->val.gw.addr.u32Ip = gw[i].addr.u32Ip;
    re_idx_add->val.gw.vid = gw[i].vid;
    // ret_add();
    ret = ret_add(&gw[i], group_add->group_id);

    if (ret == NULL) {
      DEBUG("ret = NULL \n");
    }
    if (ret != NULL && ret_get_valid(ret)){
      re_idx_add->val.idx = idxs[start];
      start++;
      ret_set_re_to_idx(ret, re_idx_add->val.idx);
    }
    if (ret != NULL && !ret_get_valid(ret)) {
      re_idx_add->val.idx = idxs[end];
      re_idx_add->val.idx = DROP_RE_IDX;
      end--;
    }
    DL_APPEND(group_add->re_idx, re_idx_add);
  }
  group_add->numOfPaths = start - 1;

  re.ipLttEntry.routeEntryBaseIndex = group_add->first_idx;
  re.ipLttEntry.numOfPaths = group_add->numOfPaths;
  if (group_add->numOfPaths == -1) {
    re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
    re.ipLttEntry.numOfPaths = 0;
  }
  // CRP (cpssDxChIpLpmIpv4UcPrefixAdd
  //       (0, 0, addr, alen, &re, GT_TRUE));
  ret_group_ltt_tcam_set(&add->val.data.pbr, group_add);
  HASH_ADD(hh, re_group, group_id, sizeof(int), group_add);

  for (i = 0; i < gw_count; i++) {
    ret_arpc_request_addr(&gw[i]);
  }

  return group_add->group_id;
}

// struct fib_entry;

void ret_group_del_2(struct re_group *re_group) {
  struct list_re_idx *el, *tmp;
  DL_FOREACH_SAFE(re_group->re_idx, el, tmp) {
    ret_unref(&el->val.gw, re_group->group_id);
  }

  res_push_count_first(re_group->re_count, re_group->first_idx);

  del_id(re_group->group_id);  
}

void ret_group_remove_pfx(struct list_route_pfx_pbr **head, uint32_t pfx, int alen) {
  struct list_route_pfx_pbr *prx_del, pfx_elt;

  pfx_elt.val.type = ROUTE_PFX;
  pfx_elt.val.data.pfx.addr.u32Ip = htonl(pfx);
  pfx_elt.val.data.pfx.alen = alen;

  DL_SEARCH(*head, prx_del, &pfx_elt, list_route_pfx_pbr_cmp_pfx);
  if (prx_del) {
    DL_DELETE(*head, prx_del);
    free(prx_del);
  }
}

void ret_group_remove_pbr(struct list_route_pfx_pbr **head, struct pbr_entry *pe) {
  struct list_route_pfx_pbr *prx_del, pfx_elt;

  pfx_elt.val.type = ROUTE_PBR;
  pfx_elt.val.data.pbr.interface = *pbr_get_interface(pe);
  pfx_elt.val.data.pbr.ltt_index = *pbr_get_ltt_index(pe);
  // pfx_elt.val.data.pfx.addr.u32Ip = htonl(pfx);
  // pfx_elt.val.data.pfx.alen = alen;

  DL_SEARCH(*head, prx_del, &pfx_elt, list_route_pfx_pbr_cmp_pbr);
  if (prx_del) {
    DL_DELETE(*head, prx_del);
    free(prx_del);
  }
}

void ret_group_del(int group_id, uint32_t pfx, int alen, bool_t is_children) {
  struct re_group *re_group_out;
  HASH_FIND(hh, re_group, &group_id, sizeof(int), re_group_out);
  if (re_group_out) {
      ret_group_remove_pfx(&re_group_out->pfx_pbr, pfx, alen);
      if (!re_group_out->pfx_pbr) {
        ret_group_del_2(re_group_out);
        HASH_DELETE(hh, re_group, re_group_out);
        free(re_group_out);
      }   
  }
}

void ret_group_del_pbr(struct pbr_entry *pe) {
  struct re_group *re_group_out;
  int group_id = pbr_get_group_id(pe);
  HASH_FIND(hh, re_group, &group_id, sizeof(int), re_group_out);
  if (re_group_out) {
      ret_group_remove_pbr(&re_group_out->pfx_pbr, pe);
      if (!re_group_out->pfx_pbr) {
        ret_group_del_2(re_group_out);
        HASH_DELETE(hh, re_group, re_group_out);
        free(re_group_out);
      }
  }
}

void ret_group_update_lpm_table(struct re_group *re_group) {
  CPSS_DXCH_IP_TCAM_ROUTE_ENTRY_INFO_UNT re;
  struct list_route_pfx_pbr *out;
  memset (&re, 0, sizeof (re));
  re.ipLttEntry.routeEntryBaseIndex = re_group->first_idx;
  re.ipLttEntry.numOfPaths = re_group->numOfPaths;
  if (re_group->numOfPaths == -1) {
    re.ipLttEntry.routeEntryBaseIndex = DROP_RE_IDX;
    re.ipLttEntry.numOfPaths = 0;
  }
  DL_FOREACH(re_group->pfx_pbr, out) {
    if (out->val.type == ROUTE_PFX) { 
      CRP (cpssDxChIpLpmIpv4UcPrefixAdd
            (0, 0, out->val.data.pfx.addr, out->val.data.pfx.alen, &re, GT_TRUE));
    }
    if (out->val.type == ROUTE_PBR) {
      ret_group_ltt_tcam_set(&out->val.data.pbr, re_group);
    }
  } 
}

void ret_group_gw_changed_enable (struct re_group *re_group, const struct re *re){
  struct list_re_idx *out, elt;
  int idx;
  if (re_group->numOfPaths == -1) {
    idx = re_group->first_idx;
  } 
  else {
    idx = re_group->first_idx + re_group->numOfPaths + 1;
  }
  re_group->numOfPaths++;  
  elt.val.gw = *ret_get_gw(re);
  DL_SEARCH(re_group->re_idx, out, &elt, list_re_idx_cmp); 
  if (out) {
    out->val.idx = idx;
  }
  else {
    DEBUG("ret_group_gw_changed_enable\n");
  }

  ret_set_re_to_idx(re, idx);
  ret_group_update_lpm_table(re_group);
}
void ret_group_gw_changed_enable_changed (struct re_group *re_group, const struct re *re){
  struct list_re_idx *out, elt;
  int idx;


  elt.val.gw = *ret_get_gw(re);
  DL_SEARCH(re_group->re_idx, out, &elt, list_re_idx_cmp); 
  if (out) {
    idx = out->val.idx;
  }
  else {
    return;
  }

  ret_set_re_to_idx(re, idx);
  // ret_group_update_lpm_table(re_group);

}
void ret_group_gw_changed_disable (struct re_group *re_group, const struct re *re){
  struct list_re_idx *out, elt;
  int idx;
  elt.val.gw = *ret_get_gw(re);
  DL_SEARCH(re_group->re_idx, out, &elt, list_re_idx_cmp);
  if (out) { 
    idx = out->val.idx;
    out->val.idx = DROP_RE_IDX;
  }
  else {
    return;
  }
  memset(&elt, 0, sizeof(elt));
  if (re_group->numOfPaths == -1) {
    return;
  } else if (re_group->numOfPaths == 0) {
  } else {
    elt.val.idx = re_group->first_idx + re_group->numOfPaths;
    DL_SEARCH(re_group->re_idx, out, &elt, list_re_idx_cmp2);
    if (out) {
      out->val.idx = idx;
      ret_set_re_to_idx_gw(&out->val.gw, idx);
    } else {
      DEBUG("ret_group_gw_changed_disable\n"); 

    }
  }  

  re_group->numOfPaths--;
  ret_group_update_lpm_table(re_group);
  //disable
}
void ret_group_gw_changed_disable_changed (struct re_group *re_group, const struct re *re){
  DEBUG("ret_group_gw_changed_disable_changed\n");
  //disable
}

void ret_group_gw_changed(int group_id, const struct re *re, bool_t is_changed_valid){
  struct re_group *re_group_out;
  int valid = ret_get_valid(re);
  HASH_FIND(hh, re_group, &group_id, sizeof(int), re_group_out);
  if (re_group_out) {
    if (valid && is_changed_valid) {
      ret_group_gw_changed_enable(re_group_out, re);
    }
    else if (valid && !is_changed_valid) {
      ret_group_gw_changed_enable_changed(re_group_out, re);
    }
    else if (!valid && is_changed_valid) {
      ret_group_gw_changed_disable(re_group_out, re);
    }
    else if (!valid && !is_changed_valid) {
      ret_group_gw_changed_disable_changed(re_group_out, re);
    }

  }
}

struct re_group *ret_group_get(int group_id) {
  struct re_group *re_group_out = NULL;
  HASH_FIND(hh, re_group, &group_id, sizeof(int), re_group_out);
  return re_group_out;
}

void ret_group_copy_pfx_pbr(struct list_route_pfx_pbr **pfx_pbr, struct re_group *group) {
  struct list_route_pfx_pbr *el, *out, *add;
  if (group) {
    DL_FOREACH(group->pfx_pbr, el) {
      DL_SEARCH(*pfx_pbr,out,el,list_route_pfx_pbr_cmp_pfx);
      if (!out) {
        add = calloc(1, sizeof(struct list_route_pfx_pbr));
        add->val = el->val;
        DL_APPEND(*pfx_pbr, add);
      }
    }
  }
}
