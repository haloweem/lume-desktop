// Independently compare every native output field with the original adapter.
// Usage: node compare-lunar-data.mjs <original-widgets> <native-test-exe> <report.json>
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import {createRequire} from 'node:module';
import path from 'node:path';
import {isDeepStrictEqual} from 'node:util';

const [sourceArg, executableArg, reportArg] = process.argv.slice(2);
assert(sourceArg && executableArg && reportArg && process.argv.length === 5,
  'Pass original widgets directory, native test executable, and output report file');
const source = path.resolve(sourceArg);
const executable = path.resolve(executableArg);
const reportPath = path.resolve(reportArg);
const exportPath = reportPath.replace(/\.json$/i, '') + '.native.jsonl';
assert.notEqual(exportPath, reportPath);
fs.mkdirSync(path.dirname(reportPath), {recursive: true});
const run = spawnSync(executable, ['--export-json', exportPath], {encoding: 'utf8'});
assert.ifError(run.error);
assert.equal(run.status, 0, `Native export failed: ${run.stderr}\n${run.stdout}`);
const nativeBytes = fs.readFileSync(exportPath);
const lines = nativeBytes.toString('utf8').trimEnd().split(/\r?\n/);
const require = createRequire(import.meta.url);
const {dateDetails} = require(path.join(source, 'lunar-calendar.js'));
const first = Date.UTC(1900, 0, 1);
const end = Date.UTC(2101, 0, 1);
const expectedDays = (end - first) / 86400000;
assert.equal(lines.length, expectedDays, 'Native export must contain the complete supported range');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const differences = [], fields = ['label', 'lunarDate', 'festivals', 'solarTerm', 'holiday'];
let differenceCount = 0, comparedFields = 0, holidayDates = 0, workDates = 0;
const expectedHash = crypto.createHash('sha256');
for (let i = 0, time = first; time < end; ++i, time += 86400000) {
  const date = new Date(time);
  const dateKey = date.toISOString().slice(0, 10);
  const actual = JSON.parse(lines[i]);
  assert.equal(actual.date, dateKey, 'Native dates must be unique, ordered, and contiguous');
  assert.deepEqual(Object.keys(actual).sort(), ['date', ...fields].sort(), 'Unexpected native fields');
  const expected = dateDetails(date.getUTCFullYear(), date.getUTCMonth() + 1, date.getUTCDate());
  const expectedRecord = {date: dateKey};
  for (const field of fields) {
    expectedRecord[field] = expected[field];
    ++comparedFields;
    if (!isDeepStrictEqual(actual[field], expected[field])) {
      ++differenceCount;
      if (differences.length < 50) differences.push({date: dateKey, field, expected: expected[field], actual: actual[field]});
    }
  }
  expectedHash.update(`${JSON.stringify(expectedRecord)}\n`);
  if (expected.holiday) { ++holidayDates; if (expected.holiday.work) ++workDates; }
}
const report = {
  passed: differenceCount === 0,
  range: {first: '1900-01-01', last: '2100-12-31', days: expectedDays},
  fields, comparedFields, differenceCount, differences,
  holidayDates, workDates,
  sourceFiles: ['lunar-calendar.js', 'vendor/lunar.js'].map(file => ({
    file, sha256: sha256(fs.readFileSync(path.join(source, file))),
  })),
  nativeExecutable: executable, nativeExecutableSha256: sha256(fs.readFileSync(executable)),
  export: {file: exportPath, bytes: nativeBytes.length, sha256: sha256(nativeBytes)},
  expectedExportSha256: expectedHash.digest('hex'),
};
fs.writeFileSync(reportPath, `${JSON.stringify(report, null, 2)}\n`);
assert.equal(differenceCount, 0, `Lunar output differences; see ${reportPath}`);
assert.equal(report.export.sha256, report.expectedExportSha256, 'Native serialized fields must also match byte for byte');
console.log(`LUNAR_COMPARISON_PASS: ${expectedDays} dates, ${comparedFields} fields, 0 differences`);
