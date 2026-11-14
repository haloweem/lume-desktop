# miniz 3.0.2

Upstream: https://github.com/richgel999/miniz
Revision: 293d4db1b7d0ffee9756d035b9ac6f7431ef8492 (tag 3.0.2)

Unmodified upstream files: miniz.h, miniz_common.h, miniz_tinfl.h,
miniz_tinfl.c, miniz_tdef.h, miniz_zip.h, LICENSE.
Local miniz_export.h selects a static build. Only the bounded low-level
inflate API is compiled into the NetEase playback adapter. Compression,
archive, zlib compatibility and heap allocation APIs are disabled.

The adapter validates the gzip header, CRC32, output length and a 256 KiB
decompressed size limit. Used for the encrypted gzip responses of xeapi.
