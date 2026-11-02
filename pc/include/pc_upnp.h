/* pc_upnp.h - forwarding the room's port on the home router (UPnP IGD) */
#ifndef PC_UPNP_H
#define PC_UPNP_H

#include "pc_net.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Blocking (a few seconds): 1 if the router now forwards UDP port to this
 * machine; *external_ip gets its outside address (0 if it did not say) */
int pc_upnp_map(uint16_t port, uint32_t* external_ip);
/* Takes the forwarding away again */
void pc_upnp_unmap(uint16_t port);

#ifdef __cplusplus
}
#endif

#endif
