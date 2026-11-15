"""A scenario device that names a device group the map never made
(source/devices/devices.c, device_add_scenario_information): an index past
the scenario's groups, or past what device_groups_data holds. datum_get gave
NULL and reading the group's value crashed as the map loaded (a crafted or
broken map); such a group now counts as none and the device gets one of its
own. The real function is compiled over fake groups and devices. After
ChupathingyCE (911bb6fdb, MrMilenko; GPL-3.0-or-later)."""
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent
DEVICES_C = ROOT / "source/devices/devices.c"

PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
typedef int boolean;
typedef float real;
#define NONE (-1L)
#define FLAG(bit) (1u << (bit))
#define TEST_FLAG(flags, bit) (((flags) & FLAG(bit)) != 0)
enum { _scenario_device_initially_open_bit, _scenario_device_initially_off_bit,
	_scenario_device_changes_only_once_bit, _scenario_device_position_reversed_bit,
	_scenario_device_not_usable_bit };
enum { _device_group_runtime_bit, _device_group_can_change_only_once_bit };
enum { _device_position_reversed_bit, _device_not_usable_bit };
struct device_group_datum { short identifier; real actual_value; };
struct scenario_device_datum { short power_group_index; short position_group_index; unsigned flags; };
struct device_part { short power_group_index; short position_group_index; real power; real position; unsigned flags; };
struct device_datum { long definition_index; struct device_part device; };
struct data_array { int count; };
static struct data_array groups = { 2 };
static struct data_array *device_groups_data = &groups;
static struct device_group_datum group_values[8] = { { 1, 0.25f }, { 1, 0.75f } };
static struct device_datum the_device;
static struct device_datum *device_get(long index) { (void)index; return &the_device; }
static void *device_definition_get(long index) { (void)index; return NULL; }
static void *datum_try_and_get(struct data_array *data, long index)
{
	return index >= 0 && index < data->count ? &group_values[index] : NULL;
}
static void *datum_get(struct data_array *data, long index)
{
	void *datum = datum_try_and_get(data, index);

	if (!datum)
	{
		printf("device group #%ld read, never made\n", index);
		exit(1);
	}
	return datum;
}
static short device_group_new(real value, unsigned flags)
{
	(void)flags;
	group_values[groups.count].actual_value = value;
	return (short)groups.count++;
}
"""

MAIN = r"""
int main(void)
{
	struct scenario_device_datum good = { 1, 0, 0 };
	struct scenario_device_datum missing = { 5, 300, FLAG(_scenario_device_initially_open_bit) };

	device_add_scenario_information(0, &good);
	if (the_device.device.power_group_index != 1 || the_device.device.power != 0.75f ||
		the_device.device.position_group_index != 0 || the_device.device.position != 0.25f)
	{
		printf("a device's own groups were not kept\n");
		return 1;
	}
	device_add_scenario_information(0, &missing);
	if (the_device.device.power_group_index < 2 || the_device.device.power != 1.0f ||
		the_device.device.position_group_index < 2 || the_device.device.position != 1.0f)
	{
		printf("a missing group was not replaced by a new one\n");
		return 1;
	}
	printf("device_groups_test: ok\n");
	return 0;
}
"""


def function(source, signature):
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def test_a_device_of_a_group_never_made_gets_its_own(tmp_path):
    cc = shutil.which("clang") or shutil.which("cc")
    if not cc:
        pytest.skip("no C compiler")
    source = tmp_path / "device_groups_test.c"
    source.write_text(PRELUDE + function(DEVICES_C.read_text(encoding="utf-8"),
                                         "void device_add_scenario_information(\n") + MAIN, encoding="utf-8")
    binary = tmp_path / "device_groups_test"
    build = subprocess.run([cc, "-std=c99", "-w", str(source), "-o", str(binary)], capture_output=True, text=True)
    assert build.returncode == 0, build.stdout + build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0 and "device_groups_test: ok" in run.stdout, run.stdout + run.stderr
