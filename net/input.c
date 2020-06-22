#include "ns.h"

extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

	while(true) {

	    int result;
	    int perm;
	    envid_t whom;

	    if ((result = sys_page_alloc(thisenv->env_id, &nsipcbuf, PTE_W)) < 0){
	        panic ("INPUT error %e", result);
	    }

	    result = sys_rx_pkg((void*) nsipcbuf.pkt.jp_data, MAX_PKG_SIZE);

	    if (result < 0){
	        cprintf ("\nINPUT: ipc_recv failed on %e. Ignoring", result);
	        continue;
	    }

	    if (result == 0){
	        continue;
	    }

	    int length = result;
	    nsipcbuf.pkt.jp_len = result;

#if 0
	    int i;
	    cprintf("\n::USER Start length %d\n", result);
	    uint32_t* ptr = (uint32_t*) nsipcbuf.pkt.jp_data;
	    for (i = 0; i < result / 4; i++){
	        cprintf("0x%08x ", ptr[i]);
	    }
	    cprintf("\n::USER end\n");
#endif

	    ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_W | PTE_U | PTE_P);

	    // Make sure receiving env will have access event even after getting a new package
	    sys_page_unmap(0, &nsipcbuf);

	}
}
