#!/usr/bin/env node
// Changes the product version everywhere it is written, in one step.
//   node tools/release/bump-version.mjs 1.0.9
// Run from native-rebuild/. Reads the current version from CMakeLists.txt,
// replaces it (dotted and 1,0,9,0 resource form) in the files listed below,
// and fails if any listed file does not contain the current version, so a
// new place that stores the version cannot be missed silently. Afterwards run
// `node --test ui/tests/release-version.test.js`. See docs/current/RELEASE.md.
import fs from 'node:fs';
import path from 'node:path';

const next = process.argv[2];
if (!/^\d+\.\d+\.\d+$/.test(next ?? '')) {
  console.error('usage: node tools/release/bump-version.mjs <major.minor.patch>');
  process.exit(2);
}
const root = process.cwd();
const read = relative => fs.readFileSync(path.join(root, relative), 'utf8');
const current = read('CMakeLists.txt').match(/project\(LumeNativeRebuild VERSION ([\d.]+)/)?.[1];
if (!current) { console.error('CMakeLists.txt has no project VERSION; run from native-rebuild/'); process.exit(2); }
if (current === next) { console.error(`already ${next}`); process.exit(2); }
const compare = (a, b) => { const x = a.split('.').map(Number), y = b.split('.').map(Number);
  for (let i = 0; i < 3; ++i) if (x[i] !== y[i]) return x[i] - y[i]; return 0; };
if (compare(next, current) <= 0) {
  // Installed clients only offer an update whose version is higher.
  console.error(`${next} is not higher than ${current}`); process.exit(2);
}
const comma = v => `${v.split('.').join(',')},0`;

// Every file that stores the version. Keep this list complete.
const files = [
  'CMakeLists.txt',
  'core/application_settings/application_settings.h',
  'platform/windows/lume_app.rc',
  'packaging/LumeSetup.iss',
  'packaging/AcceptanceNotes-seven-features.txt',
  'tests/scripts/package-original-pages.ps1',
  'platform/windows/application_settings/update_transport.cpp',
  'platform/windows/native_home_window.cpp',
  'platform/windows/wallpaper/download.cpp',
  'platform/windows/weather/weather_http.cpp',
  'core/weather/weather_provider.cpp',
  'core/weather/weather_location.cpp',
  'ui/app.js',
  'ui/application-settings.js',
  'ui/diagnostics.js',
  'ui/tests/release-version.test.js',
  'tests/unit/application_settings_update_test.cpp',
  'tests/unit/application_settings_service_test.cpp',
  'tests/unit/modal_preview.cpp',
  'tests/unit/update_presenter_test.cpp',
];
const escape = text => text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
// Version text only where it stands alone (not inside 11.0.8 or 1.0.80).
const dotted = new RegExp(`(?<![\\d.])${escape(current)}(?![\\d])`, 'g');
const resource = new RegExp(`(?<![\\d,])${escape(comma(current))}(?![\\d])`, 'g');
const missing = [];
const changes = [];
for (const relative of files) {
  const file = path.join(root, relative);
  if (!fs.existsSync(file)) { missing.push(`${relative} (file not found)`); continue; }
  const before = fs.readFileSync(file, 'utf8');
  const after = before.replace(dotted, next).replace(resource, comma(next));
  if (after === before) { missing.push(relative); continue; }
  changes.push([file, after, relative]);
}
if (missing.length) {
  console.error(`current version ${current} not found in:\n  ${missing.join('\n  ')}\nNothing was changed.`);
  process.exit(1);
}
for (const [file, after] of changes) fs.writeFileSync(file, after);
console.log(`${current} -> ${next} in ${changes.length} files`);
