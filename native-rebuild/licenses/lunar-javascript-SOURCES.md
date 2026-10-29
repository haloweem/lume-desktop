# Lunar calendar data source

The generated `core/calendar/lunar_data.inc` derives from the original Lume
`src/lume-electron-shell/widgets/lunar-calendar.js` adapter and its intact
`vendor/lunar.js` (lunar-javascript 1.7.7, https://github.com/6tail/lunar-javascript).
The upstream MIT license is reproduced in `lunar-javascript-LICENSE.txt`.

The immutable source snapshot is checked before generation:

| File relative to the original widgets directory | SHA-256 |
| --- | --- |
| `lunar-calendar.js` | `a20464beb83aebce31003f22ec6c0961e687836381c4d0b57d87b42c5da7dfec` |
| `vendor/lunar.js` | `5e6f2c23c669235cdad68681cbb39775b5565e99854a215f843a8cd53db996f1` |

From the repository root, regenerate with:

```powershell
node native-rebuild/tools/generate-lunar-data.mjs --source H:/refactor/history/lume1.0.1/src/lume-electron-shell/widgets
cmake --build build-artifacts/native-rebuild/debug --target lume-lunar-calendar-test
ctest --test-dir build-artifacts/native-rebuild/debug -R '^lume-lunar-calendar-test$' --output-on-failure
node native-rebuild/tests/scripts/compare-lunar-data.mjs H:/refactor/history/lume1.0.1/src/lume-electron-shell/widgets build-artifacts/native-rebuild/debug/tests/unit/lume-lunar-calendar-test.exe build-artifacts/native-rebuild/verification/calendar-c-lunar/debug-comparison.json
```

The source directory is read-only input. The generator never loads its contents
in the shipped runtime and does not modify the original checkout. Pass
`--output <data.inc> --metadata <manifest.json>` to regenerate into an isolated
verification directory for a byte comparison. No timestamp or machine-dependent
path is embedded in either generated file.

The inclusive Gregorian range 1900-01-01 through 2100-12-31 contains 73,414 days.
Each day indexes a deduplicated detail record; strings and festival sequences
are pooled. `core/calendar/lunar_data.sources.json` records exact counts, bytes,
hashes, and generation parameters. The native lookup accesses these static
arrays directly and allocates only the returned festival vector when nonempty.
String views remain valid for the program's lifetime. It has no JS, Win32, JSON
parsing, time-zone dependency, or runtime expansion of the full table.

Fields preserve the adapter's complete result: `label`, `lunarDate`, ordered
`festivals`, `solarTerm`, and nullable `holiday` (`name`, `work`). Holidays are
only the schedules present in that source snapshot; future statutory schedules
are not predicted. A later schedule update requires an explicitly reviewed
source/hash update, regeneration, and full native comparison. Gregorian invalid
dates and dates outside the supported range return empty native fields.

The almanac uses the same reviewed 1.7.7 source. `tools/generate-almanac-data.mjs`
generates exact ordered yi/ji and year/month/day cycles for the same date range;
`core/almanac/almanac_data.sources.json` records hashes and compiled size.
The almanac data adds 549,655 bytes of immutable arrays. It reuses the calendar
lunar-date data, without loading a JavaScript runtime or expanding all days into
heap objects. `tests/scripts/compare-almanac-data.mjs` compares all six output
fields for every supported date against the original `almanacForDate` adapter.
Dates outside this explicit range return unavailable, never fabricated values.

The calendar comparison script independently invokes the native export and the original
adapter for all 73,414 dates. It checks every field, ordered/contiguous dates,
holiday nullness, names and work flags, and byte-identical serialized output.
The generated JSON manifest and verification scripts are development artifacts;
the product only needs the compiled data and the distributed MIT notice.
