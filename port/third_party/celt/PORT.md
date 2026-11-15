# CELT 0.11

[CELT](https://web.archive.org/web/2011/http://www.celt-codec.org/) 0.11.0
(bitstream `0x80000010`), the codec of the dialogue, music and streamed
sounds of Halo: The Master Chief Collection's Halo CE (`halo1\sound\pc\*.fsb`:
FMOD's CELT, `port/linux/src/fsb_celt.c`, made Xbox ADPCM for the sound
cache as Halo 2's Opus sounds are, `h2_sound_codec.c`). The Opus library's
CELT (`port/third_party/opus`) is a later codec and does not decode it.

The source is Mumble's copy of the 0.11.0 release, the one its clients
interoperate with: <https://github.com/mumble-voip/celt-0.11.0>, branch
`master` at `e18de7747fb1655e66bf8d291560587036bfe53c` (the archive
`master.tar.gz`, SHA-256
`b3f2df3a0540682d14bb61f5cc09a368bdda9e2e847f158617c4dea806ec7df1`).
BSD 2-Clause licence (Xiph.Org, CSIRO and the other contributors):
`COPYING`, kept here with the files.

Only `libcelt/` is kept, without the encoder's tools, the tests, the
fixed-point DSP headers and the fixed-point static modes; `sources.txt`
lists the files the builds compile (`tools/linux_build.py`), as plain C in
the floating-point configuration (`VAR_ARRAYS`, `CUSTOM_MODES`) with
`OPUS_BUILD` (the pitch postfilter on, the decoder's signalling a setting:
FMOD's frames decode only so, every frame of MCC's banks). Nothing in
the files is changed: `celt_port.h` (the port's, force-included before
each) gives the library's external names a `celt011_` prefix, as Opus has
functions of the same names, and routes its few allocations (the mode's,
made once: about 6 KB) to the port's.
