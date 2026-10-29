# dr_flac

Version: 0.13.3. Upstream: https://github.com/mackron/dr_libs

Pinned tag `flac-0.13.3`, commit `69d777c482775858e8ea8a7b047c9bcd451febc8`.
`dr_flac.h` is vendored in `vendor/dr-flac/0.13.3/`.
Distributed under the MIT No Attribution option in `dr-flac-LICENSE.txt`.

Local patch: in `drflac__read_next_flac_frame_header`, a tentative frame sync
whose bit depth differs from STREAMINFO is skipped instead of terminating the
scan. Encoded sample bytes can resemble an unchecked header during random
seeking; treating that candidate as EOF made binary seeking abandon its bounds
and scan from the beginning. Frame CRC checks remain enabled and unchanged.
The loopback decoder regression includes these false sync bytes and checks
both exact PCM after distant seeks and the amount of HTTP data requested.

Only the native FLAC container is enabled. The decoder is used for seekable
HTTP FLAC; Media Foundation remains the fallback for unsupported transports
and other formats. Decoding stays in the isolated music host.
