#include "rl_net.h"
 
// Accept or deny connection from remote Telnet client.
// If this function is missing, all remote clients are accepted.
bool telnet_accept_client (const uint8_t *ip_addr, uint16_t port) {
  /* Example
  if (ip_addr[0] == 192  &&
      ip_addr[1] == 168  &&
      ip_addr[2] == 0    &&
      ip_addr[3] == 1) {
    // Accept connection.
    return (true);
  }
  // Deny connection.
  return (false);
  */
  return (true);
}
