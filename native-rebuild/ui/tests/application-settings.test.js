import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync} from 'node:fs';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {homedir} from 'node:os';
import path from 'node:path';
const moduleUrl=new URL('../application-settings.js',import.meta.url);
test('独立设置模块验证固定快照，不接受旧更新协议或凭据',async()=>{
  assert.ok(existsSync(moduleUrl),'需要独立 application-settings.js');
  const {mountApplicationSettings,isApplicationSettingsSnapshot}=await import(moduleUrl);
  assert.equal(typeof mountApplicationSettings,'function');
  const message={version:1,type:'app-settings-state',revision:1,language:'zh-CN',balancedMode:true,autostart:false,
    busy:false,error:'',update:{status:'idle',version:'',progress:0}};
  assert.equal(isApplicationSettingsSnapshot(message),true);
  for(const fields of [{language:'fr'},{balancedMode:1},{autostart:null},{busy:'true'},{revision:-1},
    {revision:Infinity},{error:'<script>'},{token:'secret'},{update:{status:'latest',version:'',progress:0}},
    {update:{status:'available',version:'2',progress:101}}])
    assert.equal(isApplicationSettingsSnapshot({...message,...fields}),false,JSON.stringify(fields));
  const css=readFileSync(new URL('../application-settings.css',import.meta.url),'utf8');
  assert.match(css,/grid-template-columns:\s*220px minmax\(0,\s*1fr\)/);
  assert.match(css,/grid-template-columns:\s*190px minmax\(0,\s*1fr\)/);
  assert.match(css,/width:\s*88px/);
});
test('设置 DOM 的 pending、语言、返回与更新兼容性回归',t=>{
  const jsdom=process.env.LUME_JSDOM_PATH || process.env.LUME_JSDOM;
  if(!jsdom) return t.skip('设置 LUME_JSDOM_PATH 为已有 jsdom/lib/api.js 绝对路径后运行 DOM 回归');
  assert.ok(existsSync(moduleUrl),'需要独立 application-settings.js');
  const result=spawnSync(process.execPath,[fileURLToPath(new URL('./application-settings-dom.mjs',import.meta.url)),jsdom],{encoding:'utf8'});
  assert.equal(result.status,0,result.stdout+result.stderr);
  t.diagnostic(result.stdout.trim());
});
test('真实浏览器验证标题栏鼠标退出、头像回退及五种尺寸设置页面',{
  skip:process.env.LUME_UI_BROWSER_TESTS!=='1',
},()=>{
  const playwright=process.env.LUME_PLAYWRIGHT_DIR ||
    path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
  const output=fileURLToPath(new URL('../../../build-artifacts/native-rebuild/application-settings-browser/',import.meta.url));
  const result=spawnSync(process.execPath,[
    fileURLToPath(new URL('./application-settings-browser.mjs',import.meta.url)),playwright,output,
  ],{encoding:'utf8',timeout:120000});
  assert.equal(result.status,0,result.stdout+result.stderr);
});
