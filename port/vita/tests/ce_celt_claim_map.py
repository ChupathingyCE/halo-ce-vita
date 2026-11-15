#!/usr/bin/env python3
# ce_celt_claim_map.py IN.map OUT.map SOUND_TAG [BANK]: a copy of a Halo
# Custom Edition map whose sound SOUND_TAG says, of each of its permutations,
# that it is a stream of MCC's CELT bank BANK (the sound itself made Xbox
# ADPCM, as MCC's own maps' are) (default 1, sounds_dialogs.fsb):
# compression CUSTOM_EDITION_PERMUTATION_MCC_CELT | BANK and the samples' flag
# CUSTOM_EDITION_SAMPLES_MCC_CELT_FLAG (port/linux/game/cache_file_formats.h),
# offsets and sizes left as the map's own - what a downloaded map could do to
# read the player's MCC banks (1.1.3 audit, LOW-1: run_mcc_sound_test.sh
# "foreign"). Prints the number of permutations changed.
import struct
import sys

TAG_CACHE_ADDRESS = 0x40440000
SOUND_COMPRESSION_OFFSET = 0x6E
SOUND_COMPRESSION_XBOX_ADPCM = 1
SOUND_PITCH_RANGES_OFFSET = 0x98
SOUND_PITCH_RANGE_BYTES = 0x48
SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET = 0x3C
SOUND_PERMUTATION_BYTES = 0x7C
SOUND_PERMUTATION_COMPRESSION_OFFSET = 0x28
SOUND_PERMUTATION_SAMPLES_OFFSET = 0x40
TAG_DATA_FLAGS_OFFSET = 4
MCC_CELT = 0x200
MCC_CELT_FLAG = 0x10000


def main():
    source, target, tag = sys.argv[1], sys.argv[2], sys.argv[3]
    bank = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    data = bytearray(open(source, 'rb').read())
    u32 = lambda at: struct.unpack_from('<I', data, at)[0]
    if data[0:4] != b'daeh' or u32(4) != 609:
        sys.exit('not a Custom Edition map: %s' % source)
    tags = u32(0x10)
    place = lambda address: tags + (address - TAG_CACHE_ADDRESS)

    def name_at(address):
        at = place(address)
        return data[at:data.index(b'\0', at)].decode('latin-1')

    instances, count = u32(tags), u32(tags + 0x0C)
    changed = 0
    for index in range(count):
        instance = place(instances) + index * 0x20
        if data[instance:instance + 4] != b'!dns' or name_at(u32(instance + 0x10)) != tag:
            continue
        sound = place(u32(instance + 0x14))
        struct.pack_into('<H', data, sound + SOUND_COMPRESSION_OFFSET, SOUND_COMPRESSION_XBOX_ADPCM)
        ranges, ranges_at = u32(sound + SOUND_PITCH_RANGES_OFFSET), u32(sound + SOUND_PITCH_RANGES_OFFSET + 4)
        for range_index in range(ranges):
            pitch_range = place(ranges_at) + range_index * SOUND_PITCH_RANGE_BYTES
            permutations = u32(pitch_range + SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET)
            permutations_at = u32(pitch_range + SOUND_PITCH_RANGE_PERMUTATIONS_OFFSET + 4)
            for permutation_index in range(permutations):
                permutation = place(permutations_at) + permutation_index * SOUND_PERMUTATION_BYTES
                struct.pack_into('<H', data, permutation + SOUND_PERMUTATION_COMPRESSION_OFFSET, MCC_CELT | bank)
                flags = permutation + SOUND_PERMUTATION_SAMPLES_OFFSET + TAG_DATA_FLAGS_OFFSET
                struct.pack_into('<I', data, flags, u32(flags) | MCC_CELT_FLAG)
                changed += 1
    if not changed:
        sys.exit('no permutations of %s in %s' % (tag, source))
    open(target, 'wb').write(data)
    print(changed)


main()
