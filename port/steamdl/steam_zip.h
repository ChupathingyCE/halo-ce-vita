/*
STEAM_ZIP.H

The two deflate containers Steam sends:
- a zip of one file (a manifest from the CDN; a chunk whose plaintext starts
  "PK\3\4"): its one entry stored or deflated, read through the central
  directory, its CRC-32 checked;
- gzip (a CM Multi message's body when size_unzipped is set): its header,
  the deflate data, its CRC-32 and size checked.
Both inflate into a buffer of the exact size expected and no more
(port/third_party/libdeflate).
*/

#ifndef STEAM_ZIP_H
#define STEAM_ZIP_H

#include <stddef.h>
#include <stdint.h>

/* the one entry's uncompressed size: 1, or 0 (not a zip of one entry, or
larger than maximum) */
int steam_zip_entry_size(const uint8_t *zip, size_t size, size_t maximum, size_t *entry_size);
/* the one entry into output (exactly entry_size bytes, steam_zip_entry_size's):
1, or 0 with a reason */
int steam_zip_extract(const uint8_t *zip, size_t size, uint8_t *output, size_t entry_size, char *error, int error_size);

/* gzip data into output, which must come out exactly output_size bytes: 1,
or 0 */
int steam_gunzip(const uint8_t *data, size_t size, uint8_t *output, size_t output_size);

#endif
