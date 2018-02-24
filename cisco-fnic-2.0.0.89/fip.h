/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
This program is free software; you may redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; version 2 of the License.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */

#ifndef _FIP_H_
#define _FIP_H_

#include "fdls_fc.h"

#define FCOE_ALL_FCFS_MAC {0x01, 0x10, 0x18, 0x01, 0x00, 0x02}
#define FIP_ETH_TYPE 0x8914

#define FIP_ETH_TYPE_LE 0x1489
#define FCOE_MAX_SIZE_LE 0x2E08

#define WWNN_LEN 8

#define FCOE_CTLR_FIPVLAN_TOV 3*1000
#define FCOE_CTLR_FCS_TOV     3*1000
#define FCOE_CTLR_VN_KA_TOV    90*1000
#define FCOE_CTLR_MAX_SOL      5*1000

/*
 * VLAN entry.
 */
struct fcoe_vlan {
        struct list_head list;
        uint16_t vid;                /* vlan ID */
        uint16_t sol_count;          /* no. of sols sent */
        uint16_t state;              /* state */
};

typedef enum fdls_vlan_state_s {
        FIP_VLAN_AVAIL,
        FIP_VLAN_SENT
} fdls_vlan_state_t;

typedef enum fdls_fip_state_s {
        FDLS_FIP_INIT,
        FDLS_FIP_VLAN_DISCOVERY_STARTED,
        FDLS_FIP_FCF_DISCOVERY_STARTED,
        FDLS_FIP_FLOGI_STARTED,
        FDLS_FIP_FLOGI_COMPLETE,
} fls_fip_state_t;

typedef enum fip_protocol_code_s {
        FIP_DISCOVERY = 1,
        FIP_FLOGI,
        FIP_KA_CVL,
        FIP_VLAN_DISC
} fip_protocol_code_t;

#define FIP_SUBCODE_REQ  1
#define FIP_SUBCODE_RESP 2

typedef struct eth_hdr_s {
        uint8_t dmac[6];
        uint8_t smac[6];
        uint16_t eth_type;
} eth_hdr_t;

typedef struct fip_header_s {
        uint32_t ver : 16;

        uint32_t protocol : 16;
        uint32_t subcode : 16;

        uint32_t desc_len : 16;
        uint32_t flags : 16;
} __attribute__((__packed__)) fip_header_t;

#define FIP_FLAG_S 0x2
#define FIP_FLAG_A 0x4

typedef enum fip_desc_type_s {
        FIP_TYPE_PRIORITY =1,
        FIP_TYPE_MAC,
        FIP_TYPE_FCMAP,
        FIP_TYPE_NAME_ID,
        FIP_TYPE_FABRIC,
        FIP_TYPE_MAX_FCOE,
        FIP_TYPE_FLOGI,
        FIP_TYPE_FDISC,
        FIP_TYPE_LOGO,
        FIP_TYPE_ELP,
        FIP_TYPE_VX_PORT,
        FIP_TYPE_FKA_ADV,
        FIP_TYPE_VENDOR_ID,
        FIP_TYPE_VLAN
} fip_desc_type_t;

typedef struct fip_mac_desc_s {
        uint8_t type;
        uint8_t len;
        uint8_t mac[6];
}  __attribute__((__packed__)) fip_mac_desc_t;

typedef struct fip_vlan_desc_s {
        uint8_t type;
        uint8_t len;
        uint16_t vlan;
}  __attribute__((__packed__)) fip_vlan_desc_t;

typedef struct fip_vlan_req_s {
        eth_hdr_t eth;
        fip_header_t fip;
        fip_mac_desc_t mac_desc;
}  __attribute__((__packed__)) fip_vlan_req_t;

 /*
  * Variables:
  * eth.smac, mac_desc.mac
  */
fip_vlan_req_t fip_vlan_req_tmpl = {
        .eth = { .dmac = FCOE_ALL_FCFS_MAC,
                .eth_type = FIP_ETH_TYPE_LE},
        .fip = { .ver = 0x10,
                 .protocol = FIP_VLAN_DISC << 8,
                 .subcode = FIP_SUBCODE_REQ << 8,
                 .desc_len = 2 << 8},
        .mac_desc = {.type = FIP_TYPE_MAC, .len = 2 }
};

typedef struct fip_vlan_notif_s {
        fip_header_t fip;
        fip_vlan_desc_t vlans_desc[0];
} __attribute__((__packed__)) fip_vlan_notif_t;

typedef struct fip_vn_port_desc_s {
        uint8_t type;
        uint8_t len;
        uint8_t vn_port_mac[6];
        uint8_t rsvd[1];
        uint8_t vn_port_id[3];
        uint64_t vn_port_name;
} __attribute__((__packed__)) fip_vn_port_desc_t;

typedef struct fip_vn_port_ka_s {
        eth_hdr_t eth;
        fip_header_t fip;
        fip_mac_desc_t mac_desc;
        fip_vn_port_desc_t vn_port_desc;
} __attribute__((__packed__)) fip_vn_port_ka_t;

/*
 * Variables:
 * fcf_mac, eth.smac, mac_desc.enode_mac
 * vn_port_desc:mac, id, port_name
 */
fip_vn_port_ka_t fip_vn_port_ka_tmpl =
{
        .eth = {
                .eth_type = FIP_ETH_TYPE_LE},
        .fip = {
                .ver = 0x10,
                .protocol = FIP_KA_CVL << 8,
                .subcode = FIP_SUBCODE_REQ << 8,
                .desc_len = 7 << 8
               },
        .mac_desc = {.type = FIP_TYPE_MAC, .len = 2 },
        .vn_port_desc = {.type = FIP_TYPE_VX_PORT, .len = 5}
};

typedef struct fip_enode_ka_s {
        eth_hdr_t eth;
        fip_header_t fip;
        fip_mac_desc_t mac_desc;
} __attribute__((__packed__)) fip_enode_ka_t;

/*
 * Variables:
 * fcf_mac, eth.smac, mac_desc.enode_mac
 */
fip_enode_ka_t fip_enode_ka_tmpl =
{
        .eth = {
                .eth_type = FIP_ETH_TYPE_LE},
        .fip = {
                .ver = 0x10,
                .protocol = FIP_KA_CVL << 8,
                .subcode = FIP_SUBCODE_REQ <<8,
                .desc_len = 2 <<8
               },
        .mac_desc = {.type = FIP_TYPE_MAC, .len = 2 }
};

typedef struct fip_name_desc_s {
        uint8_t type;
        uint8_t len;
        uint8_t rsvd[2];
        uint64_t name;
} __attribute__((__packed__)) fip_name_desc_t;

typedef struct fip_cvl_s {
        fip_header_t fip;
        fip_mac_desc_t fcf_mac_desc;
        fip_name_desc_t name_desc;
        fip_vn_port_desc_t vn_ports_desc[0];
} __attribute__((__packed__)) fip_cvl_t;

typedef struct fip_flogi_desc_s {
        uint8_t type;
        uint8_t len;
        uint16_t rsvd;
        fc_els_t flogi;
} __attribute__((__packed__)) fip_flogi_desc_t;

typedef struct fip_flogi_rsp_desc_s {
        uint8_t type;
        uint8_t len;
        uint16_t rsvd;
        fc_els_t els;
} __attribute__((__packed__)) fip_flogi_rsp_desc_t;

typedef struct fip_flogi_s {
        eth_hdr_t eth;
        fip_header_t fip;
        fip_flogi_desc_t flogi_desc;
        fip_mac_desc_t mac_desc;
} __attribute__((__packed__)) fip_flogi_t;

typedef struct fip_flogi_rsp_s {
        fip_header_t fip;
        fip_flogi_rsp_desc_t rsp_desc;
        fip_mac_desc_t mac_desc;
} __attribute__((__packed__)) fip_flogi_rsp_t;

/*
 * Variables:
 * fcf_mac, eth.smac, mac_desc.enode_mac
 */
fip_flogi_t fip_flogi_tmpl =
{
        .eth = {
                .eth_type = FIP_ETH_TYPE_LE},
        .fip = {
                .ver = 0x10,
                .protocol = FIP_FLOGI << 8,
                .subcode = FIP_SUBCODE_REQ << 8,
                .desc_len = 38 << 8,
                .flags = 0x80 },
        .flogi_desc = {
                .type = FIP_TYPE_FLOGI, .len = 36,
                .flogi = {
                        .fchdr = {
                                .r_ctl = 0x22,
                                .did = {0xFF, 0xFF, 0xFE},
                                .type = 0x01,
                                .f_ctl = FNIC_ELS_REQ_FCTL,
                                .ox_id = FNIC_FLOGI_OXID,
                                .rx_id = 0xFFFF },
                        .command = FC_ELS_FLOGI_REQ,
                        .u.csp_flogi = {
                                .fc_ph_ver = FNIC_FC_PH_VER,
                                .b2b_credits = FNIC_FC_B2B_CREDIT,
                                .b2b_rdf_size = FNIC_FC_B2B_RDF_SZ },
                        .spc3 = {0x88, 0x00}
                }
        },
        .mac_desc = {.type = FIP_TYPE_MAC, .len = 2 }
};

typedef struct fip_fcoe_desc_s {
        uint8_t type;
        uint8_t len;
        uint16_t max_fcoe_size;
} __attribute__((__packed__)) fip_fcoe_desc_t;

typedef struct fip_discovery_s {
        eth_hdr_t eth;
        fip_header_t fip;
        fip_mac_desc_t mac_desc;
        fip_name_desc_t name_desc;
        fip_fcoe_desc_t fcoe_desc;
} __attribute__((__packed__)) fip_discovery_t;

/*
 * Variables:
 * eth.smac, mac_desc.enode_mac, node_name
 */
fip_discovery_t fip_discovery_tmpl =
{
        .eth = {.dmac = FCOE_ALL_FCFS_MAC,
                .eth_type = FIP_ETH_TYPE_LE},
        .fip = {
                .ver = 0x10, .protocol = FIP_DISCOVERY << 8,
                .subcode = FIP_SUBCODE_REQ << 8, .desc_len = 6 << 8,
                .flags = 0x80},
        .mac_desc = {.type = FIP_TYPE_MAC, .len = 2 },
        .name_desc = {.type = FIP_TYPE_NAME_ID, .len =3},
        .fcoe_desc = {
            .type = FIP_TYPE_MAX_FCOE, .len = 1,
            .max_fcoe_size = FCOE_MAX_SIZE_LE
        }    //htons(2094)
};

typedef struct fip_prio_desc_s {
        uint8_t type;
        uint8_t len;
        uint8_t rsvd;
        uint8_t priority;
} __attribute__((__packed__)) fip_prio_desc_t;

typedef struct fip_fabric_desc_s {
        uint8_t type;
        uint8_t len;
        uint16_t vf_id;
        uint8_t rsvd;
        uint8_t fc_map[3];
        uint64_t  fabric_name;
} __attribute__((__packed__)) fip_fabric_desc_t;

typedef struct fip_fka_adv_desc_s {
        uint8_t type;
        uint8_t len;
        uint8_t rsvd;
        uint8_t  rsvd_D;
        uint32_t fka_adv;
} __attribute__((__packed__)) fip_fka_adv_desc_t;

typedef struct fip_disc_adv_s {
        fip_header_t fip;
        fip_prio_desc_t prio_desc;
        fip_mac_desc_t mac_desc;
        fip_name_desc_t name_desc;
        fip_fabric_desc_t fabric_desc;
        fip_fka_adv_desc_t fka_adv_desc;
} __attribute__((__packed__)) fip_disc_adv_t;

#endif  /* _FIP_H_ */
