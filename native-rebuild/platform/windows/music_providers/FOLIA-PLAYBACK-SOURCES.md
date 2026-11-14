# Folia Playback Port, 2026-11-14

Folia: https://github.com/chthollyphile/folia-major
Revision: 77d52a3168ab2608174d2e5ad6d51b7bc8befd11, version 0.7.28.
License: AGPL-3.0. Lume retains its existing AGPL-3.0 licensing.

Reference call chain:

- src/services/onlineMusic/netease/neteaseProvider.ts: getAudioSource.
- src/services/onlineMusic/netease/netease.ts: getSongUrl/getLyric.
- src/services/onlinePlayback.ts: independent audio/lyrics, successful lyric
  cache, current-selection checks around retry waits.
- src/services/onlineMusic/failure/onlineFailureCase.ts: only reachability
  failures are retried; delays 1 s / 3 s, or one 2 s retry after timeout.
- src/services/onlineMusic/netease/neteaseFailure.ts: preserve signed upstream
  codes; distinguish account rights, no URL, request refusal and network.

Folia's pinned dependency: @neteasecloudmusicapienhanced/api 4.41.1 (MIT).
Tarball SHA-512:
D100CAB4BAD22DFCF888FD8CE7B9D04EA450B89933255D8050E8C748248182A385404A06930E334AE7BC6DB7E4CF3FE51512EF9C8AFD4A3B2B3E3F7905387151

The exact source package integrity is recorded by npm as:
sha512-0QDKtLrSLfz4iP2M57nQTqRQuJkzJV2AUOjHSCSBgqOFQEoGkw4zSue8bbfkzz/lFRLvnIr9SjsrPj95BThxUQ==

Ported module/song_url_v1.js, module/register_xeapikey.js and the xeapi
parts of util/request.js and util/crypto.js to netease_playback.cpp.
The MIT notice is in NETEASE-API-LICENSE.txt. This is request-envelope
encryption, not audio decryption. Windows CNG implements X25519, HKDF,
AES-GCM, AES-ECB and HMAC. miniz 3.0.2 handles bounded gzip responses.

Native integration keeps WinHTTP, credential leases and the existing
audio host. No Electron/Node runtime is shipped. Public keys are verified
against the upstream response signature and cached for at most 24 hours,
with at most 16 devices. Playback reads have an 8 s per-attempt and 20 s
total budget; cancellations are checked during reads and waits. Lyrics
share one in-flight request and at most 16 successful results, partitioned
by provider, track, account generation, account ID and duration. Account
change and shutdown clear that cache.

Scope differences: no unblockmusic, alternate-song replacement, spoofed
network region, audio-file cache, background next-song prefetch or Electron
renderer is imported. Provider-authorized preview clips remain separate
from full-song playback and get an explicit preview-only notice. An empty
successful lyric response is not fabricated into lyrics. Network failures
and platform refusals never trigger automatic quality downgrade.

Validation: native provider/lease/session regressions, a Node crypto
interoperability test using the RFC 7748 Bob key, and read-only live startup
probes. Live probes emit timing, HTTP/provider codes, bitrate and lyric
count only; no credentials, response bodies or signed media URLs.
