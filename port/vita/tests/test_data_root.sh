# test_data_root.sh, sourced by the tests after root=: data_root, the folder whose data2276, triage/menus/data,
# custom-maps-dl, vita-save-backups and other builds' worktrees the tests' defaults name. That is the folder
# beside the main checkout ($root/..); a git worktree elsewhere (git worktree add ~/halo-test-out/...) has none
# of them beside it, and every data-driven test skipped there unless its HALO_TEST_* were set: the main
# checkout's parent then, found by the worktree's common git folder.
data_root=$root/..
if [ ! -e "$data_root/triage" ] && [ ! -e "$data_root/data2276" ]; then
	test_data_git=$(git -C "$root" rev-parse --path-format=absolute --git-common-dir 2>/dev/null) &&
		[ -e "$(dirname "$test_data_git")/../triage" ] && data_root=$(cd "$(dirname "$test_data_git")/.." && pwd)
	unset test_data_git
fi
