/*
STEAM_NET.H

The Steam download's connections: a steam_stream over posix_https.c's
stream (TLS 1.2/1.3 with the built-in authorities and the host name
checked, nothing sent before; plain only to a loopback address, the PC
tests' fake servers), and the system's random numbers.
*/

#ifndef STEAM_NET_H
#define STEAM_NET_H

#include "steam_http.h"
#include "steam_ws.h"

#include <stddef.h>

/* url: wss:// or https:// (ws:// and http:// to a loopback address only):
the stream, its host and path: 1, or 0 with a reason */
int steam_net_open(struct steam_stream *stream, const char *url, char *host, size_t host_capacity, char *path,
	size_t path_capacity, char *error, int error_size);

/* a CDN connection to base_url (https://host or, in the tests,
http://127.0.0.1:port): 1, or 0 */
int steam_net_http_open(struct steam_http *http, const char *base_url, char *error, int error_size);

#endif
