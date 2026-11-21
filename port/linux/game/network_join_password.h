/*
NETWORK_JOIN_PASSWORD.H

A host's rule for a game with a password (network.lobby_password: Server
Setup's PASSWORD, a dedicated server's sv_password). The password is asked
for where internet play hands out the game: the server browser's listing
and a friend's presence carry its token sealed with it (p2p_lobby.c), so a
machine that joins through internet play came in through them (or through
the host's own code, invite or friend invite), and the game sees it at its
tunnel stand-in, an address in 100.64.0.0/10 (p2p.c). A join request
carries no password (retail's message has none, and no build sends one), so
a machine that reaches the game's port itself, as system link does, would
skip the password: the host refuses such a machine unless it is on the
host's own network. Nothing on the wire changed (ChupathingyCE #1684's rule,
the same ranges).
*/

#ifndef __NETWORK_JOIN_PASSWORD_H
#define __NETWORK_JOIN_PASSWORD_H
#pragma once

#include <stdint.h>

/* whether a machine joining from an IPv4 address (host byte order) is on a
network of the host's own, where a game with a password takes it as a LAN
game does: loopback (127/8), the private ranges (10/8, 172.16/12,
192.168/16), link-local (169.254/16), 0 (not known), and 100.64/10 (internet
play's and ad hoc play's stand-ins, which came through internet play, and
private networks such as Tailscale's). Any other address is the internet's */
int network_join_address_is_local(uint32_t address);

/* whether a host refuses a machine joining from address (host byte order)
for the password (_rejection_code_bad_password): the game has one
(has_password nonzero) and the machine came from the internet straight to
the game's port. A game without a password: never */
int network_join_password_refused(uint32_t address, int has_password);

#endif
