#include <inc/error.h>
#include <inc/ns.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/env.h>
#include <kern/picirq.h>
#include <kern/e1000.h>
#include <kern/sched.h>

// 82540EM

// LAB 6: Your driver code here

#define PTE_COW     0x800

#define TX_DESC_NUM 64
#define RX_DESC_NUM 128

#define TX_BUF_ADDR (USTACKTOP-PTSIZE-(TX_DESC_NUM * PGSIZE))
#define GET_TX_BUF(index) (TX_BUF_ADDR + (index * PGSIZE))

static volatile uint32_t  *base_address;
static struct e1000_tx_desc* tx_descriptors;
static struct e1000_rx_desc* rx_descriptors;

static int irq = -1;

static struct Env* env_wait_receive = NULL;
static struct Env* env_wait_send = NULL;

#define REG(id) ((uint32_t*) (base_address + (id / 4)))

int e1000_get_irq(){
    if (irq == -1){
        panic("E1000 IRQ was not set");
    }
    return irq;
}

int e1000_attach(struct pci_func *e1000){

    ////////////////////////////////////////////
    // PCI configuration
    ///////////////////////////////////////////

    pci_func_enable(e1000);
    base_address = mmio_map_region(e1000->reg_base[0],e1000->reg_size[0]);

    // Interrupt activation
    irq = e1000->irq_line;
    irq_setmask_8259A(irq_mask_8259A & ~(1<<irq));
    *REG(E1000_IMS) = E1000_IMS_TXQE | E1000_IMS_RXSEQ | E1000_IMS_RXO | E1000_IMS_RXT0;
    *REG(E1000_ICS) = E1000_ICS_TXQE | E1000_ICS_RXSEQ | E1000_ICS_RXO | E1000_ICS_RXT0;

    cprintf("\nE1000 Status: 0x%x IRQ: %d\n",*REG(E1000_STATUS), irq);

    ////////////////////////////////////////////
    // Receive descriptors
    ///////////////////////////////////////////

    struct PageInfo *p = NULL;
    if ((p = page_alloc(ALLOC_ZERO)) == 0 ){
        panic ("e1000_attach no mem");
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
        tx_descriptors[i].lower.data |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
        tx_descriptors[i].upper.data |= E1000_TXD_STAT_DD;
    }

    *REG(E1000_TIPG) = 10 << 0; // IPGT [9:0]

    *REG(E1000_TDH) = 0;
    *REG(E1000_TDT) = 0;

    *REG(E1000_TCTL) = 0;
    *REG(E1000_TCTL) |= E1000_TCTL_PSP;
    *REG(E1000_TCTL) |= 0x10 << 4;  // E1000_TCTL_CT     0x00000ff0
    *REG(E1000_TCTL) |= 0x40 << 12; // E1000_TCTL_COLD   0x003ff000
    *REG(E1000_TCTL) |= E1000_TCTL_EN;

    ////////////////////////////////////////////
    // Receive descriptors
    ///////////////////////////////////////////

    if ((p = page_alloc(ALLOC_ZERO)) == 0 ){
        panic ("e1000_attach no mem");
    }

    rx_descriptors = page2kva(p);

    // Section 14.4
    *(uint64_t*) REG(E1000_RA) = e1000_get_mac_address();
    *REG(E1000_RAH) |= E1000_RAH_AV;

    cprintf("\n RAL 0x%x", *REG(E1000_RAL));
    cprintf("\n RAH 0x%x", *REG(E1000_RAH));


    *REG(E1000_MTA) = 0; //initalize MTA to 0

    *REG(E1000_RDBAL) = page2pa(p);
    *REG(E1000_RDBAH) = 0;
    *REG(E1000_RDLEN) = RX_DESC_NUM * sizeof (struct e1000_rx_desc);

    *REG(E1000_RDH) = 1;
    *REG(E1000_RDT) = 0;

    // Set Rx buffers
    for (i = 0; i < RX_DESC_NUM; i++){
        if ((p = page_alloc(ALLOC_ZERO)) == 0 ){ // TODO: clean memory on error
            panic ("e1000_attach no mem");
        }
        rx_descriptors[i].buffer_addr = page2pa(p) + 4;
        rx_descriptors[i].status &= ~E1000_RXD_STAT_DD;
    }

    // Control
    *REG(E1000_RCTL) &= ~E1000_RCTL_BSEX; // Use pkg in range up to 2048
    *REG(E1000_RCTL) &= ~E1000_RCTL_SZ_256; // This will set pkg sizes to 2048 (bits [16:17] == 0
    *REG(E1000_RCTL) &= ~E1000_RCTL_LPE; // Don't use jambo packages
    *REG(E1000_RCTL) &= ~(0b11 << 6); // use E1000_RCTL_LBM_NO [6:7]
    *REG(E1000_RCTL) &= ~(E1000_RCTL_MPE); // Disable multi case
    *REG(E1000_RCTL) |= E1000_RCTL_SECRC; // Strip crc

    // Enable Rx
    *REG(E1000_RCTL) |= E1000_RCTL_EN; // Enable rx;

    return 0;
}

int e1000_tx_pkg(void* buffer, uint32_t size){

    int result;
    user_mem_assert(curenv, buffer, size, PTE_U);

    if (size > MAX_PKG_SIZE){
        panic ("MAX_PKG_SIZE");
    }

    uint32_t tx_tail = *REG(E1000_TDT);
    uint32_t last = tx_tail - 1 > tx_tail ? TX_DESC_NUM - 1 : 0;
    while (!(tx_descriptors[tx_tail].upper.data & E1000_TXD_STAT_DD)){
        if (env_wait_send != NULL){
            panic ("env_wait_send not empty");
        }
        env_wait_send = curenv;
        curenv->env_status = ENV_NOT_RUNNABLE;
        curenv->env_tf.tf_regs.reg_eax = 0;
        sched_yield();
    }

    struct e1000_tx_desc* desc = tx_descriptors + tx_tail;

    struct PageInfo *pp;
    pte_t* pte;
    if ((pp = page_lookup(curenv->env_pgdir,buffer, &pte)) == NULL){
        panic("e1000_tx_pkg page lookup");
    }

    if (page_insert(curenv->env_pgdir, pp, (void*) GET_TX_BUF(tx_tail), PTE_W) < 0){
        panic("e1000_tx_pkg page_insert not enough mem");
    }

    *pte |= PTE_COW;
    *pte &= ~PTE_W;

    tx_descriptors[tx_tail].lower.flags.length = size;
    tx_descriptors[tx_tail].lower.data |= E1000_TXD_CMD_EOP;
    tx_descriptors[tx_tail].upper.data &= ~E1000_TXD_STAT_DD;
    tx_descriptors[tx_tail].buffer_addr = page2pa(pp) + 4;

    *REG(E1000_TDT) = (++tx_tail) % TX_DESC_NUM;

    return 0;
}

int e1000_rx_pkg(void* buffer, uint32_t size){

    uint32_t index = (*REG(E1000_RDT) + 1) % RX_DESC_NUM;
    volatile struct e1000_rx_desc* desc = &rx_descriptors[index];

    int i;

    while  (!(desc->status & E1000_RXD_STAT_DD)){
        if (env_wait_send != NULL){
            panic ("env_wait_receive not empty");
        }
        env_wait_receive = curenv;
        curenv->env_status = ENV_NOT_RUNNABLE;
        curenv->env_tf.tf_regs.reg_eax = 0;
        sched_yield();
    }

    struct PageInfo *pp;
    if ((pp = page_lookup(kern_pgdir,KADDR(desc->buffer_addr), NULL)) == NULL){
        panic("e1000_rx_pkg page lookup");
    }

    if (page_insert(curenv->env_pgdir, pp, buffer, PTE_U | PTE_W) < 0){
        panic("e1000_rx_pkg page_insert not enough mem");
    }

    if ((pp = page_alloc(ALLOC_ZERO)) == 0 ){ // TODO: clean memory on error
        panic("e1000_rx_pkg page_alloc not enough mem");
    }

    desc->buffer_addr = page2pa(pp) + 4;

    desc->status &= ~E1000_RXD_STAT_DD;
    *REG(E1000_RDT) = index;

    return size < desc->length ? size : desc->length;

}

void e1000_interrupt_handler(){


    uint32_t cause = *REG(E1000_ICR);

    if (cause & E1000_ICR_TXDW){
        if (env_wait_send != NULL){
            env_wait_send->env_status = ENV_RUNNABLE;
            env_wait_send = NULL;
        }
    }

    if ((cause & E1000_ICR_RXT0) && env_wait_receive != NULL) {
        env_wait_receive->env_status = ENV_RUNNABLE;
        env_wait_receive = NULL;
    }
}

u64_t read_eeprom(uint32_t address){
    *REG(E1000_EERD) = (address << E1000_EEPROM_RW_ADDR_SHIFT) | E1000_EEPROM_RW_REG_START;
    uint64_t data = 0;
    while(((data = *REG(E1000_EERD)) & E1000_EEPROM_RW_REG_DONE) == 0);
    return data >> E1000_EEPROM_RW_REG_DATA;
}

uint64_t e1000_get_mac_address(){
    uint64_t address = read_eeprom(0x00);
    address |= read_eeprom(0x01) << 16;
    address |= read_eeprom(0x02) << 32;
    return address;
}

