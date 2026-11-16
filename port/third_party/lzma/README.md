# LZMA SDK decoder (LzmaDec)

Igor Pavlov's LZMA decoder from the LZMA SDK / 7-Zip, vendored for the Steam
download (port/steamdl/steam_chunk.c: depot chunks in Valve's "VZa"
container).

Upstream: https://github.com/ip7z/7zip, tag `24.09`, `C/LzmaDec.c`,
`C/LzmaDec.h`, `C/7zTypes.h`, `C/Precomp.h`, `C/Compiler.h`,
`C/7zWindows.h`, unmodified.

Licence: **public domain**. Each kept file says so in its header ("Igor
Pavlov : Public domain"), and 7-Zip's DOC/License.txt says files that state
public domain status are public domain. Nothing else of 7-Zip (whose other
files are LGPL) is vendored. See also `LICENSES/LZMA-SDK-public-domain.txt`.
