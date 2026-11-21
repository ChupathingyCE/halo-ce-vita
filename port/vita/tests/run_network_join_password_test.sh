#!/bin/sh
# A host's rule for a game with a password (network_join_password_test.c,
# port/linux/game/network_join_password.c), and that the check sits where
# every join is taken (network_server_manager.c's
# network_game_server_machine_refused, which the join request's handler
# reaches through network_game_server_accept_client_machine_into_game and
# network_game_server_port_join_make_room), before the machine is added,
# with the protocol's own bad password code. Built 32-bit, warnings as
# errors, plain and with AddressSanitizer and UBSan. Needs clang that
# targets i686 (CLANG, default clang). The game run for real:
# run_netns_join_password_test.sh.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${TMPDIR:-/tmp}/network_join_password_test.$$
. "$here/test_out.sh"
test_out_begin "$out"
clang=${CLANG:-clang}
mkdir -p "$out"
cd "$root"
sanitize="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
status=0
for variant in plain sanitized; do
	flags=
	[ "$variant" = sanitized ] && flags=$sanitize
	$clang -m32 -std=c11 -Wall -Wextra -Werror -O1 $flags port/vita/tests/network_join_password_test.c \
		port/linux/game/network_join_password.c -o "$out/network_join_password_test_$variant"
	"$out/network_join_password_test_$variant" || status=1
done

# (the sources: the check in the one place every join is refused)
fail() { echo "FAIL $*"; status=1; }
manager=source/networking/network_server_manager.c
handler=source/networking/network_server_message_handler.c
refused=$(awk '/^static boolean network_game_server_machine_refused\(/ { on = 1 } on { print } on && /^}/ { exit }' "$manager")
grep -q 'network_join_password_refused((uint32_t)address.address.long_words\[0\],' <<EOT || fail "no password check in network_game_server_machine_refused"
$refused
EOT
grep -q 'p2p_lobby_hosting_has_password()' <<EOT || fail "the check does not ask whether the game has a password"
$refused
EOT
grep -q 'network_game_server_refusal_code = _rejection_code_bad_password;' <<EOT || fail "the refusal is not the protocol's bad password code"
$refused
EOT
# (the host's own machine never refused)
grep -B4 'network_join_password_refused' <<EOT | grep -q '!network_game_server_client_machine_is_local(server, machine)' || fail "the host's own machine is not left out"
$refused
EOT
accept=$(awk '/^boolean network_game_server_accept_client_machine_into_game\(/ { on = 1 } on { print } on && /^}/ { exit }' "$manager")
first_refused=$(grep -n 'network_game_server_machine_refused(server, machine)' <<EOT | head -1 | cut -d: -f1
$accept
EOT
)
first_add=$(grep -n 'network_game_add_machine(&server->game, &new_machine)' <<EOT | head -1 | cut -d: -f1
$accept
EOT
)
[ -n "$first_refused" ] && [ -n "$first_add" ] && [ "$first_refused" -lt "$first_add" ] ||
	fail "network_game_server_accept_client_machine_into_game adds a machine before the refusals"
make_room=$(awk '/^boolean network_game_server_port_join_make_room\(/ { on = 1 } on { print } on && /^}/ { exit }' "$manager")
grep -q 'network_game_server_machine_refused(server, machine)' <<EOT || fail "network_game_server_port_join_make_room skips the refusals"
$make_room
EOT
# (machines join the game in one place: network_game_add_machine's only caller is the accept)
adds_in_accept=$(grep -c 'success = network_game_add_machine(&server->game, &new_machine);' "$manager")
[ "$adds_in_accept" = 1 ] || fail "network_game_add_machine is not called once from the accept"
callers=$(grep -rn 'network_game_add_machine(' source port --include='*.c' | grep -v 'third_party\|/tests/\|"' |
	grep -v 'network_game_manager.c:[0-9]*:boolean network_game_add_machine(' | grep -vc 'network_server_manager.c' || true)
[ "$callers" = 0 ] || fail "network_game_add_machine has callers besides the accept ($callers)"
callers=$(grep -rn 'network_game_server_accept_client_machine_into_game(server' source port --include='*.c' |
	grep -v 'third_party\|/tests/' | grep -v '^source/networking/network_server_message_handler.c:' | wc -l)
[ "$callers" = 0 ] || fail "the accept has callers besides the join request's handler ($callers)"
[ $status -eq 0 ] && echo "network_join_password_test: sources ok"
test_out_done $status
exit $status
