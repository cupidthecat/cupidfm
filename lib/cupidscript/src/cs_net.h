#ifndef CS_NET_H
#define CS_NET_H

#include "cupidscript.h"
#include "cs_event_loop.h"

// DNS resolution
int cs_resolve_host(const char* host, int port, struct sockaddr_in* addr);

// Create socket map value
cs_value cs_make_socket_map(cs_vm* vm, cs_socket_t fd, const char* type, const char* host, int port);

// Extract fd from socket map
cs_socket_t cs_socket_map_fd(cs_value sock);

// Register network stdlib functions
void cs_register_net_stdlib(cs_vm* vm);

#endif
