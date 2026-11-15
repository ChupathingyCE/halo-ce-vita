# zstd (decompressor only)

Zstandard's reference decoder, vendored for the Steam download
(port/steamdl/steam_chunk.c: depot chunks in Valve's "VSZa" container).

Upstream: https://github.com/facebook/zstd, release v1.5.7,
`zstd-1.5.7.tar.gz`, SHA-256
`eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3`.

Kept: `zstddeclib.c`, the single-file decoder made from the release by its own
script (`build/single_file_libs`: `sh combine.sh -r ../../lib -x
legacy/zstd_legacy.h -o zstddeclib.c zstddeclib-in.c`), unmodified; `zstd.h`
and `zstd_errors.h` from `lib/`; `LICENSE`.

Licence: zstd is dual-licensed BSD-3-Clause or GPL-2.0-only; Project Ark
takes it under **BSD-3-Clause** (`LICENSE`, also `LICENSES/BSD-3-Clause-zstd.txt`),
which is compatible with the project's GPL-3.0-only.
