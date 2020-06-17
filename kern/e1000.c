#include <kern/pci.h>
#include <kern/e1000.h>
#include <kern/pmap.h>

// 82540EM

// LAB 6: Your driver code here

volatile uint32_t  *base_address;

#define REG(id) ((uint32_t*) (base_address + (id / 4)))

int attach_e1000(struct pci_func *e1000){

    pci_func_enable(e1000);
    base_address = mmio_map_region(e1000->reg_base[0],e1000->reg_size[0]);

    cprintf("E1000 Status: 0x%x\n",*REG(E1000_STATUS));

    *REG(E1000_TCTL) = 0;
    *REG(E1000_TCTL) |= E1000_TCTL_EN;
    *REG(E1000_TCTL) |= E1000_TCTL_PSP;
    *REG(E1000_TCTL) |= 0x10 << 4;  // E1000_TCTL_CT     0x00000ff0
    *REG(E1000_TCTL) |= 0x40 << 12; // E1000_TCTL_COLD   0x003ff000

    *REG(E1000_TIPG) = 0;
    *REG(E1000_TIPG) = 10 << 0; // IPGT [9:0]

    return 0;
}

