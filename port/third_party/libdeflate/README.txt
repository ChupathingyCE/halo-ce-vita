libdeflate's decompressor, vendored for Project Ark (port/linux/game/h2_cache.c:
the Halo 2 maps' and textures.dat's chunks inflated whole).

Source: https://github.com/ebiggers/libdeflate, release v1.25
(commit c8c56a20f8f621e6a966b716b31f1dedab6a41e3, 2025-10-31), unmodified.
Licence: MIT (COPYING).

Kept: libdeflate.h, common_defs.h, COPYING and of lib/ the decompressor alone -
utils.c, deflate_decompress.c, decompress_template.h, deflate_constants.h,
zlib_decompress.c, zlib_constants.h, adler32.c, lib_common.h,
cpu_features_common.h, x86/ (cpu_features, decompress_impl, adler32_impl,
adler32_template) and arm/ (cpu_features, adler32_impl). The compressor, gzip,
crc32 and the programs are left out. Built by tools/linux_build.py,
vita_build.py, windows_build.py and android_build.py (LIBDEFLATE_SOURCES).
