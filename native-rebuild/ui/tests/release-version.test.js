import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';

const version = '1.0.8';
const numericVersion = '1,0,8,0';
const read = relative => fs.readFileSync(new URL(`../../${relative}`, import.meta.url), 'utf8');

test('build and update policy identify the 1.0.8 release', () => {
  assert.equal(read('CMakeLists.txt').match(/project\(LumeNativeRebuild VERSION ([\d.]+)/)?.[1], version);
  assert.equal(read('core/application_settings/application_settings.h').match(/CurrentVersion\[\] = "([\d.]+)"/)?.[1], version);
});

test('Windows numeric and display resource versions match the new release', () => {
  const resource = read('platform/windows/lume_app.rc');
  assert.equal(resource.match(/FILEVERSION\s+([\d,]+)/)?.[1], numericVersion);
  assert.equal(resource.match(/PRODUCTVERSION\s+([\d,]+)/)?.[1], numericVersion);
  for (const field of ['FileVersion', 'ProductVersion']) {
    assert.equal(resource.match(new RegExp(`VALUE "${field}", "([\\d.]+)"`))?.[1], version);
  }
});

test('installer and package outputs cannot overwrite the accepted old installer', () => {
  const installer = read('packaging/LumeSetup.iss');
  assert.equal(installer.match(/^AppVersion=([\d.]+)/m)?.[1], version);
  assert.equal(installer.match(/^VersionInfoVersion=([\d.]+)/m)?.[1], version);
  assert.equal(installer.match(/^VersionInfoProductVersion=([\d.]+)/m)?.[1], version);
  assert.ok(installer.includes(`"LumeSetup-${version}"`));
  const script = read('tests/scripts/package-original-pages.ps1');
  assert.ok(script.includes(`$outputName = 'LumeSetup-${version}.exe'`));
  assert.ok(script.includes(`previous-LumeSetup-${version}.exe`));
  assert.doesNotMatch(script, /LumeSetup-1\.0\.2\.1/);
});

test('settings, preview and HTTP client identities use the new release', () => {
  for (const relative of [
    'ui/application-settings.js',
    'platform/windows/native_home_window.cpp',
    'platform/windows/application_settings/update_transport.cpp',
    'platform/windows/weather/weather_http.cpp',
    'platform/windows/wallpaper/download.cpp',
    'core/weather/weather_provider.cpp',
    'core/weather/weather_location.cpp'
  ]) {
    const source = read(relative);
    assert.ok(source.includes(version), relative);
    assert.doesNotMatch(source, /1\.0\.2\.1/, relative);
  }
  const acceptanceTitle = read('packaging/AcceptanceNotes-seven-features.txt').split(/\r?\n/, 1)[0];
  assert.ok(acceptanceTitle.startsWith(`Lume ${version} - `));
});

test('protected playback audit stays inside the new workspace', () => {
  const audit = read('tests/unit/wallpaper_protected_playback_test.cpp');
  assert.ok(audit.includes('lume1.0.3\\\\native-rebuild\\\\build-artifacts\\\\wallpaper-library-audit'));
  assert.doesNotMatch(audit, /lume1\.0\.2\.1/);
});
