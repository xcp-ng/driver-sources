/* Copyright (c) 2020 Cisco Systems, Inc.  All rights reserved. */

#include "fnic_config.h"

#include <linux/module.h>
#include <linux/mempool.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/if_ether.h>

#define PCI_VENDOR_ID_CISCO                  	0x1137 /* Cisco vendor id */
#define PCI_DEVICE_ID_CISCO_VIC_FC           	0x0045 /* fc vnic */

#define PCI_DEVICE_ID_CISCO_SERENO             0x004e /* sereno pcie switch */
#define PCI_DEVICE_ID_CISCO_CRUZ               0x007a /* Cruz */
#define PCI_DEVICE_ID_CISCO_BODEGA             0x0131 /* Bodega */
#define PCI_DEVICE_ID_CISCO_BEVERLY            0x025f /* Beverly */

/* Sereno */
#define PCI_SUBDEVICE_ID_CISCO_VASONA        	0x004f /* vasona mezz */
#define PCI_SUBDEVICE_ID_CISCO_COTATI        	0x0084 /* cotati mlom */
#define PCI_SUBDEVICE_ID_CISCO_LEXINGTON     	0x0085 /* lexington pcie */
#define PCI_SUBDEVICE_ID_CISCO_ICEHOUSE      	0x00cd /* Icehouse */
#define PCI_SUBDEVICE_ID_CISCO_KIRKWOODLAKE  	0x00ce /* KirkwoodLake pcie */
#define PCI_SUBDEVICE_ID_CISCO_SUSANVILLE    	0x012e /* Susanville MLOM */
#define PCI_SUBDEVICE_ID_CISCO_TORRANCE      	0x0139 /* Torrance MLOM */

/* Cruz */
#define PCI_SUBDEVICE_ID_CISCO_CALISTOGA     	0x012c /* Calistoga MLOM */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTAINVIEW  	0x0137 /* Cruz Mezz */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN     	0x014b /* Cruz MountTian SIOC */
#define PCI_SUBDEVICE_ID_CISCO_CLEARLAKE     	0x014d /* ClearLake pcie */
#define PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN2    	0x0157 /* Cruz MountTian2 SIOC */
#define PCI_SUBDEVICE_ID_CISCO_CLAREMONT     	0x015d /* Claremont MLOM */

/* Bodega */
#define PCI_SUBDEVICE_ID_CISCO_BRADBURY         0x0218 /* VIC 1457 PCIe mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BRENTWOOD        0x0217 /* VIC 1455 PCIe */
#define PCI_SUBDEVICE_ID_CISCO_BURLINGAME       0x021a /* VIC 1487 PCIe mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BAYSIDE          0x0219 /* VIC 1485 PCIe */
#define PCI_SUBDEVICE_ID_CISCO_BAKERSFIELD      0x0215 /* VIC 1440 Mezz mLOM */
#define PCI_SUBDEVICE_ID_CISCO_BOONVILLE        0x0216 /* VIC 1480 Mezz */
#define PCI_SUBDEVICE_ID_CISCO_BENICIA          0x024a /* VIC 1495 */
#define PCI_SUBDEVICE_ID_CISCO_BEAUMONT         0x024b /* VIC 1497 */
#define PCI_SUBDEVICE_ID_CISCO_BRISBANE         0x02af /* VIC 1467 */
#define PCI_SUBDEVICE_ID_CISCO_BENTON           0x02b0 /* VIC 1477 */
#define PCI_SUBDEVICE_ID_CISCO_TWIN_RIVER       0x02cf /* VIC 14425 */
#define PCI_SUBDEVICE_ID_CISCO_TWIN_PEAK        0x02d0 /* VIC 14825 */

/* Beverly */
#define PCI_SUBDEVICE_ID_CISCO_BERN             0x02de /* VIC 15420 */
#define PCI_SUBDEVICE_ID_CISCO_STOCKHOLM        0x02dd /* VIC 15428 */
#define PCI_SUBDEVICE_ID_CISCO_KRAKOW           0x02dc /* VIC 15411 */
#define PCI_SUBDEVICE_ID_CISCO_LUCERNE          0x02db /* VIC 15231 */
#define PCI_SUBDEVICE_ID_CISCO_TURKU            0x02e8 /* VIC 15238 */
#define PCI_SUBDEVICE_ID_CISCO_TURKU_PLUS       0x02f3 /* VIC 15237 */
#define PCI_SUBDEVICE_ID_CISCO_ZURICH           0x02df /* VIC 15230 */
#define PCI_SUBDEVICE_ID_CISCO_RIGA             0x02e0 /* VIC 15427 */
#define PCI_SUBDEVICE_ID_CISCO_GENEVA           0x02e1 /* VIC 15422 */
#define PCI_SUBDEVICE_ID_CISCO_HELSINKI         0x02e4 /* VIC 15235 */
#define PCI_SUBDEVICE_ID_CISCO_GOTHENBURG       0x02f2 /* VIC 15425 */

struct fnic_pcie_device {
    u32 device;
    u8 *desc;
    u32 subsystem_device;
    u8 *subsys_desc;
};

static struct fnic_pcie_device fnic_pcie_device_table[] = {
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_VASONA, 		"VIC 1280"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_COTATI,  		"VIC 1240"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_LEXINGTON, 	"VIC 1225"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_ICEHOUSE,  	"VIC 1285"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_KIRKWOODLAKE,  "VIC 1225T"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_SUSANVILLE,  	"VIC 1227"},
    { PCI_DEVICE_ID_CISCO_SERENO,  "Sereno",  PCI_SUBDEVICE_ID_CISCO_TORRANCE,  	"VIC 1227T"},

    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_CALISTOGA,  	"VIC 1340"},
    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_MOUNTAINVIEW,  "VIC 1380"},
    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN,  	"C3260-SIOC"},
    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_CLEARLAKE,  	"VIC 1385"},
    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_MOUNTTIAN2,  	"C3260-SIOC"},
    { PCI_DEVICE_ID_CISCO_CRUZ,    "Cruz",    PCI_SUBDEVICE_ID_CISCO_CLAREMONT,  	"VIC 1387"},

    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BRADBURY,  	"VIC 1457"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BRENTWOOD,  	"VIC 1455"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BURLINGAME,  	"VIC 1487"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BAYSIDE,  		"VIC 1485"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BAKERSFIELD,  	"VIC 1440"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BOONVILLE,  	"VIC 1480"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BENICIA,  		"VIC 1495"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BEAUMONT,  	"VIC 1497"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BRISBANE,  	"VIC 1467"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_BENTON,  		"VIC 1477"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_TWIN_RIVER,  	"VIC 14425"},
    { PCI_DEVICE_ID_CISCO_BODEGA,  "Bodega",  PCI_SUBDEVICE_ID_CISCO_TWIN_PEAK,        "VIC 14825"},

    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_BERN,         "VIC 15420"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_STOCKHOLM,    "VIC 15428"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_KRAKOW,       "VIC 15411"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_LUCERNE,      "VIC 15231"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_TURKU,        "VIC 15238"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_GENEVA,       "VIC 15422"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_HELSINKI,     "VIC 15235"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_GOTHENBURG,   "VIC 15425"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_TURKU_PLUS,   "VIC 15237"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_ZURICH,       "VIC 15230"},
    { PCI_DEVICE_ID_CISCO_BEVERLY, "Beverly",  PCI_SUBDEVICE_ID_CISCO_RIGA,         "VIC 15427"},

    { 0, }
};

int fnic_get_desc_by_devid(struct pci_dev *pdev, char **desc, char **subsys_desc)
{
	unsigned short device = PCI_DEVICE_ID_CISCO_VIC_FC;
	int max = sizeof(fnic_pcie_device_table)/sizeof(fnic_pcie_device_table[0]);
	struct fnic_pcie_device *t = fnic_pcie_device_table;
	int index = 0;

	if (memcmp((char *)&pdev->device, (char *)&device, sizeof(short)) != 0)
		return 1;

	while (t->device != 0) {	
		if (memcmp((char *)&pdev->subsystem_device, (char *)&t->subsystem_device, sizeof(short)) == 0)
			break;
		t++;
		index++;
	}

	if (index >= max - 1) {
		*desc = NULL;
		*subsys_desc = NULL;
		return 1; 
	}

	*desc = fnic_pcie_device_table[index].desc;
	*subsys_desc = fnic_pcie_device_table[index].subsys_desc;
	return 0;
}
