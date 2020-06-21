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

	    result = sys_rx_pkg((void*) nsipcbuf.pkt.jp_data, nsipcbuf.pkt.jp_len);

	    if (result < 0){
	        cprintf ("\nINPUT: ipc_recv failed on %e. Ignoring", result);
	        continue;
	    }

	    int length = result;

	    ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_W | PTE_U);

	    // Make sure receiving env will have access event even after getting a new package
	    sys_page_unmap(0, &nsipcbuf);

	}
}
