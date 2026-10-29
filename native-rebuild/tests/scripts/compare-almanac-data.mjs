// Compare native output with the original *adapter*, independently of the generator.
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const [source,executable,report]=process.argv.slice(2);
assert(source&&executable&&report&&process.argv.length===5,'Pass source widgets, native test exe, report JSON');
const reportPath=path.resolve(report),exportPath=reportPath.replace(/\.json$/i,'')+'.native.jsonl';
assert.notEqual(reportPath,exportPath);
fs.mkdirSync(path.dirname(reportPath),{recursive:true});
const run=spawnSync(executable,['--export',exportPath],{encoding:'utf8'});
assert.equal(run.status,0,run.stderr||run.error?.message);
const nativeBytes=fs.readFileSync(exportPath),lines=nativeBytes.toString('utf8').trimEnd().split('\n');
assert.equal(lines.length,73414);
const {almanacForDate}=createRequire(import.meta.url)(path.resolve(source,'lunar-calendar.js'));
const expectedHash=crypto.createHash('sha256'),hash=data=>crypto.createHash('sha256').update(data).digest('hex');
for(let i=0,time=Date.UTC(1900,0,1);i<lines.length;++i,time+=86400000){
  const utc=new Date(time),date=new Date(utc.getUTCFullYear(),utc.getUTCMonth(),utc.getUTCDate());
  const expected=almanacForDate(date),actual=JSON.parse(lines[i]);
  assert.deepEqual(actual,expected,`original almanac mismatch ${expected.date}`);
  expectedHash.update(JSON.stringify(expected)+'\n');
}
const expectedSha256=expectedHash.digest('hex');
assert.equal(hash(nativeBytes),expectedSha256,'byte exact native output');
const result={passed:true,days:73414,comparedFields:73414*6,differences:0,
  nativeExecutable:path.resolve(executable),nativeExecutableSha256:hash(fs.readFileSync(executable)),
  exportSha256:expectedSha256,sourceFiles:['lunar-calendar.js','vendor/lunar.js'].map(file=>({
    file,sha256:hash(fs.readFileSync(path.join(source,file))),
  }))};
fs.writeFileSync(reportPath,JSON.stringify(result,null,2)+'\n');
console.log(`ALMANAC_COMPARISON_PASS: ${result.days} dates, ${result.comparedFields} fields, zero differences`);
