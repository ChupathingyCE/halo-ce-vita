# stb_image (C)

Sean Barrett's single-file image loader, vendored for the Steam Workshop
browser's preview pictures (port/steamdl/steam_image.c decodes the JPEG or PNG
preview Steam's image CDN sends, then shrinks it to a card's picture).

Upstream: https://github.com/nothings/stb, commit
`9f755b604db3492df2cbeb39bf84b81e3e3932e3` (Nov 12 2026, "update version
numbers": stb_image v2.32, with the Oct-Nov 2026 parsing fixes; v2.31 of Nov 9
does not build with JPEG and PNG alone), file `stb_image.h`, unmodified
(SHA-256 `deb4f18c9ca21b751a1323019031c94c0bf5ca1d2929ef5913cc56f00aab6e91`).

How it is used (steam_image.c is the only includer): JPEG and PNG only
(STBI_ONLY_JPEG, STBI_ONLY_PNG), no stdio, no HDR or linear, no SIMD, no
thread-locals, STBI_MAX_DIMENSIONS 4096, every allocation through a bounded
allocator (a byte budget a decode may hold at once), the picture's size read
(stbi_info_from_memory) and capped before it is decoded. Fuzzed by
port/vita/tests/steam_fuzz.c (target ws_image).

Licence: public domain or MIT, at the user's choice (`LICENSE`; also
`LICENSES/MIT-or-public-domain-stb.txt`), compatible with the project's
GPL-3.0-only.
