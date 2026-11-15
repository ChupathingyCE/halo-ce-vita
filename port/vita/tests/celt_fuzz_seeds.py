#!/usr/bin/env python3
# celt_fuzz_seeds.py SOUND_FOLDER CORPUS [COUNT]: celt_fuzz.c's seeds from the
# player's own MCC CELT banks (sounds_dialogs.fsb, sounds_music.fsb): COUNT
# (default 24) runs of real frames from each, each at an FMOD CELT frame
# header (big-endian 0x17C30DF3, fsb_celt.h), about 4 KB, with the harness's
# 6 control bytes before (mono and stereo, both outputs). Prints the number.
import os
import sys

MAGIC = bytes((0x17, 0xC3, 0x0D, 0xF3))
folder, corpus = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 24
os.makedirs(corpus, exist_ok=True)
made = 0
for bank in ('sounds_dialogs.fsb', 'sounds_music.fsb'):
    path = os.path.join(folder, bank)
    if not os.path.exists(path):
        continue
    size = os.path.getsize(path)
    with open(path, 'rb') as stream:
        for index in range(count):
            stream.seek(size * (index + 1) // (count + 2))
            window = stream.read(1 << 20)
            at = window.find(MAGIC)
            if at < 0 or at + 4000 > len(window):
                continue
            control = bytes((index & 3, 0, 0, 0, 0, 0x40))
            with open(os.path.join(corpus, '%s-%02d' % (bank[:-4], index)), 'wb') as seed:
                seed.write(control + window[at:at + 4000])
            made += 1
print(made)
