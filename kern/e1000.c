#include <inc/error.h>
#include <inc/ns.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/e1000.h>
#include <kern/env.h>

// 82540EM

// LAB 6: Your driver code here

#define TX_DESC_NUM 64

static volatile uint32_t  *base_address;
static struct e1000_tx_desc* tx_descriptors;

#define REG(id) ((uint32_t*) (base_address + (id / 4)))

int e1000_attach(struct pci_func *e1000){

    pci_func_enable(e1000);
    base_address = mmio_map_region(e1000->reg_base[0],e1000->reg_size[0]);

    cprintf("E1000 Status: 0x%x\n",*REG(E1000_STATUS));

    struct PageInfo *p = NULL;
    if ((p = page_alloc(ALLOC_ZERO)) == 0 ){
        return -E_NO_MEM;
    }

    // Transmit descriptors
    tx_descriptors = page2kva(p);
    *REG(E1000_TDBAL) = page2pa(p);
    *REG(E1000_TDBAH) = 0;
    *REG(E1000_TDLEN) = TX_DESC_NUM * sizeof (struct e1000_tx_desc);
    if (*REG(E1000_TDLEN) % 128){
        panic("E1000_TDLEN unvalid valude %d", *REG(E1000_TDLEN));
    }

    int i;
    for (i = 0; i < TX_DESC_NUM; i++){
        if ((p = page_alloc(ALLOC_ZERO)) == 0 ){ // TODO: clean memory on error
            return -E_NO_MEM;
        }
        cprintf("\nE1000 buffer %d pa %x va %x \n",i, page2pa(p), page2kva(p) );
        tx_descriptors[i].buffer_addr = page2pa(p);
        tx_descriptors[i].lower.data |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
        tx_descriptors[i].upper.data |= E1000_TXD_STAT_DD;
    }

    *REG(E1000_TIPG) = 0;
    *REG(E1000_TIPG) = 10 << 0; // IPGT [9:0]

    *REG(E1000_TDH) = 0;
    *REG(E1000_TDT) = 0;

    *REG(E1000_TCTL) = 0;
    *REG(E1000_TCTL) |= E1000_TCTL_PSP;
    *REG(E1000_TCTL) |= 0x10 << 4;  // E1000_TCTL_CT     0x00000ff0
    *REG(E1000_TCTL) |= 0x40 << 12; // E1000_TCTL_COLD   0x003ff000
    *REG(E1000_TCTL) |= E1000_TCTL_EN;



    return 0;
}

int e1000_tx_pkg(void* buffer, uint32_t size, bool last_pkg){


    //cprintf("\n====e1000_tx_pkg START envid %x buffer %p size %d last_pkg %d\n", curenv->env_id, buffer, size, last_pkg);

    int result;
    user_mem_assert(curenv, buffer, size, PTE_U);

    if (size > MAX_PKG_SIZE){
        panic ("MAX_PKG_SIZE");
        return -E_INVAL;
    }

    uint32_t i;
    //cprintf("\ne1000_tx_pkg Data:");
    char* buf = buffer;
    for (i = 0 ; i < size ; i += 4){
        //cprintf("\n%x", *(uint32_t*)&buf[i]);
    }

    uint32_t tx_tail = *REG(E1000_TDT);

    uint32_t last = tx_tail - 1 > tx_tail ? TX_DESC_NUM - 1 : 0;
    if (!(tx_descriptors[tx_tail].upper.data & E1000_TXD_STAT_DD)){
        panic ("E_E1000_TX_FULL");
        return -E_E1000_TX_FULL;
    }

    volatile void * rr = KADDR1(tx_descriptors[tx_tail].buffer_addr);
    //cprintf("\n DEBUG2 KADDR1 %p", rr);
    //cprintf("\n====e1000_tx_pkg D1 va %p pa %p \n", rr, tx_descriptors[tx_tail].buffer_addr);
    memcpy(KADDR1(tx_descriptors[tx_tail].buffer_addr), (void*) buffer, size);
    tx_descriptors[tx_tail].lower.flags.length = size;
    tx_descriptors[tx_tail].lower.data |= E1000_TXD_CMD_EOP;
    tx_descriptors[tx_tail].upper.data &= ~E1000_TXD_STAT_DD;

    char* sss = KADDR(tx_descriptors[tx_tail].buffer_addr);
    //cprintf("\nDebug_3");
    for (i = 0 ; i < size ; i += 4){
        //cprintf("\n%x", *(uint32_t*)&sss[i]);
    }

    *REG(E1000_TDT) = (++tx_tail) % TX_DESC_NUM;
    //cprintf("\n====e1000_tx_pkg FINISHED envid %x \n", curenv->env_id);
    return 0;
}
