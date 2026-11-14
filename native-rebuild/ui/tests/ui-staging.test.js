import test from 'node:test';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {mkdir,mkdtemp,readFile,writeFile,access} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const repo=fileURLToPath(new URL('../../../',import.meta.url));
const stageScript=path.join(repo,'native-rebuild/cmake/StageUi.cmake');

test('UI staging excludes browser evidence and removes artifacts from incremental output',async t=>{
  const artifacts=path.join(repo,'build-artifacts/ui-staging-tests');
  await mkdir(artifacts,{recursive:true});
  const root=await mkdtemp(path.join(artifacts,'case-'));
  const source=path.join(root,'source'),destination=path.join(root,'staged');
  const header=path.join(root,'generated/ui_integrity_manifest.h');
  const put=async(base,relative,text='fixture')=>{
    const file=path.join(base,relative);await mkdir(path.dirname(file),{recursive:true});await writeFile(file,text);
  };
  const run=()=>{
    const result=spawnSync(process.env.CMAKE||'cmake',[
      `-DUI_SOURCE=${source}`,`-DUI_DESTINATION=${destination}`,`-DUI_INTEGRITY_HEADER=${header}`,'-P',stageScript,
    ],{encoding:'utf8',windowsHide:true});
    assert.equal(result.status,0,result.error?.message||result.stderr||result.stdout);
  };
  const included=['index.html','app.js','assets/icons/example.png','assets/sources/example.md',
    'vendor/notes-editor/LICENSE.txt','music-visualizers/reference/runtime.js'];
  const excluded=['build-artifacts/screenshots/new.png','test-results/example.png','playwright-report/report.html',
    'coverage/report.js','.cache/example.png','tests/fixture.js','tools/probe.js','node_modules/example/index.js',
    'music-visualizers/reference/verification/frame.png','music-effects/rain/evidence/frame.png',
    'music-visualizers/reference/source/editor.js'];
  for(const relative of [...included,...excluded])await put(source,relative);
  run();
  const manifest=await readFile(path.join(destination,'payload-manifest.txt'),'utf8');
  const integrity=await readFile(header,'utf8');
  assert.deepEqual(manifest.trim().split(/\r?\n/).sort(),[...included].sort());
  for(const relative of included)assert.equal(await readFile(path.join(destination,relative),'utf8'),'fixture');
  for(const relative of excluded){
    await assert.rejects(access(path.join(destination,relative)),{code:'ENOENT'});
    assert.ok(!integrity.includes(relative),'compiled integrity manifest excludes development files');
  }
  const stale=['build-artifacts/old-browser/frame.png','music-visualizers/reference/verification/stale.png'];
  for(const relative of stale)await put(destination,relative,'stale evidence');
  await put(source,'build-artifacts/new-browser/extra.png');
  run();
  for(const relative of stale)await assert.rejects(access(path.join(destination,relative)),{code:'ENOENT'});
  assert.equal(await readFile(path.join(destination,'payload-manifest.txt'),'utf8'),manifest);
  assert.equal(await readFile(header,'utf8'),integrity,'new test evidence cannot change the product identity');
  t.diagnostic(`UI staging fixture: ${root}`);
});
