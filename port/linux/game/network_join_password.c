/*
NETWORK_JOIN_PASSWORD.C

A host's rule for a game with a password: network_join_password.h. The
check itself is network_server_manager.c's network_game_server_machine_refused,
which every join goes through.
*/

#include "network_join_password.h"

int network_join_address_is_local(uint32_t address)
{
	return address == 0 ||
		(address & 0xFF000000u) == 0x7F000000u ||
		(address & 0xFF000000u) == 0x0A000000u ||
		(address & 0xFFF00000u) == 0xAC100000u ||
		(address & 0xFFFF0000u) == 0xC0A80000u ||
		(address & 0xFFFF0000u) == 0xA9FE0000u ||
		(address & 0xFFC00000u) == 0x64400000u;
}

int network_join_password_refused(uint32_t address, int has_password)
{
	return has_password && !network_join_address_is_local(address);
}
