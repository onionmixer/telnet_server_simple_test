#include <string.h>
#include "rl_net.h"
 
// Check if an user account exist in the user database.
uint8_t telnet_check_username (const char *username) {
  /* Example
  if (strcmp (username, "guest") == 0) {
    // Username is correct
    return (1);
  }
  */
  return (0);
}
 
// Check user account password in the user database.
bool telnet_check_password (uint8_t user_id, const char *password) {
  /* Example
  if (user_id == 1) {
    if (strcmp (password, "guest") == 0) {
      // Password is correct
      return (true);
    }
  }
  */
  return (false);
}
