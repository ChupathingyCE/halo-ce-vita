"""A host's age check of a client's hit report
(port/linux/game/network_damage.c, distributed_report_stamp_taken): a stamp
no later than the host's tick and at most REPORT_MAXIMUM_AGE_TICKS before
it is taken; one in the future, NONE, or far below zero (which made the
old `now - stamp` overflow into a small age, taken) is not. The real
function is compiled 32-bit (as the Vita and the game's x86 builds have
`long`) under UBSan. After ChupathingyCE (bb065b422, MrMilenko;
GPL-3.0-or-later)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DAMAGE_C = ROOT / "port/linux/game/network_damage.c"

PRELUDE = r"""
#include <stdio.h>
#include <limits.h>
typedef int boolean;
#define TRUE 1
#define FALSE 0
#define NONE (-1L)
"""

MAIN = r"""
static int failures;
static void check(long now, long stamp, boolean expected)
{
	if (distributed_report_stamp_taken(now, stamp, 90) != expected)
	{
		printf("FAIL now %ld stamp %ld: expected %d\n", now, stamp, expected);
		failures++;
	}
}
int main(void)
{
	check(1000, 1000, TRUE);
	check(1000, 910, TRUE);
	check(1000, 909, FALSE);
	check(1000, 1001, FALSE);
	check(1000, NONE, FALSE);
	check(0, 0, TRUE);
	check(1000, -2, FALSE);
	check(1000, LONG_MIN, FALSE);
	check(1000, LONG_MIN + 1, FALSE);
	check(LONG_MAX, LONG_MAX - 90, TRUE);
	printf(failures ? "hit_report_stamp_test: FAILED\n" : "hit_report_stamp_test: ok\n");
	return failures != 0;
}
"""


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def test_hit_report_stamps(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "hit_report_stamp_test.c"
    source.write_text(PRELUDE + function(DAMAGE_C.read_text(encoding="utf-8"),
                                         "static boolean distributed_report_stamp_taken(\n") + MAIN, encoding="utf-8")
    binary = tmp_path / "hit_report_stamp_test"
    for target in (["-m32"], []):
        build = subprocess.run([cc, *target, "-std=c99", "-Wall", "-Werror", "-fsanitize=undefined",
                                "-fno-sanitize-recover=all", str(source), "-o", str(binary)],
                               capture_output=True, text=True)
        assert build.returncode == 0, build.stdout + build.stderr
        run = subprocess.run([str(binary)], capture_output=True, text=True)
        assert run.returncode == 0 and "hit_report_stamp_test: ok" in run.stdout, run.stdout + run.stderr
