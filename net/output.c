#include "ns.h"

extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver

	while (true){

	    int result;
        int perm;
        bool error = false;
        if (( result = ipc_recv(&ns_envid, &nsipcbuf, &perm)) < 0 ){
            cprintf("\nnet output ipc_recv: %e", result);
            error = true;
        }

        if (result != NSREQ_OUTPUT){
            cprintf("\nnet output ipc_recv incorrect result: result %d", result);
            error = true;
        }

        if (perm & (PTE_P|PTE_W|PTE_U) != (PTE_P|PTE_W|PTE_U)){
            cprintf("net output ipc_recv incorrect perm: 0x%x", perm);
            error = true;
        }

        if (error){
            continue;
        }

        char* data = nsipcbuf->pkt.jp_data;
        uint32_t len = nsipcbuf->pkt.jp_len;

        while (true){
            if ((result = sys_tx_pkg(data, len, LAST_PKG)) < 0){
                if (result == -E_E1000_TX_FULL){
                    sys_yield();
                    continue;
                }
                panic ("\nnet output sys_tx_pkg %e", result);
            }
        }
	}
}
