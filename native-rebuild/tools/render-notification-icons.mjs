import {createRequire} from 'node:module';
import {readFile,mkdir} from 'node:fs/promises';
import path from 'node:path';
import {homedir} from 'node:os';
const root=path.resolve(process.argv[2]||'.');
const assets=path.join(root,'native-rebuild/ui/assets/notifications');
const output=path.join(root,'build-artifacts/island-notifications-20261107/icons');
await mkdir(output,{recursive:true});
const {chromium}=createRequire(import.meta.url)(process.env.LUME_PLAYWRIGHT_DIR||path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
const browser=await chromium.launch({channel:'msedge',headless:true});
try{
 const page=await browser.newPage({viewport:{width:256,height:256},deviceScaleFactor:1});
 const names=['water','stand','sleep','severe','rain','temperature','preview'];
 // The six reminder PNGs are now generated artwork; do not overwrite them
 // with the retired SVG masters. Only the unchanged summary uses SVG.
 for(const name of ['preview']){const svg=await readFile(path.join(assets,name+'.svg'),'utf8');await page.setContent('<html><style>html,body{margin:0;width:256px;height:256px;background:transparent}svg{width:256px;height:256px;display:block}</style>'+svg+'</html>');await page.screenshot({path:path.join(assets,name+'.png'),omitBackground:true});}
 const labels={water:'喝水',stand:'久坐',sleep:'睡眠',severe:'恶劣天气',rain:'降雨',temperature:'降温',preview:'消息摘要',qq:'QQ 原图标',wechat:'微信原图标'};
 const images=await Promise.all([...names,'qq','wechat'].map(async name=>({name,src:'data:image/png;base64,'+(await readFile(path.join(assets,name+'.png'))).toString('base64')})));
 await page.setViewportSize({width:1140,height:310});
 await page.setContent('<html><meta charset=utf-8><style>*{box-sizing:border-box}body{margin:0;background:#111214;color:#eef1f7;font-family:Microsoft YaHei UI,Microsoft YaHei,sans-serif;padding:26px 30px}h1{font-size:22px;margin:0 0 20px;font-weight:600}.grid{display:grid;grid-template-columns:repeat(9,1fr);gap:9px}.cell{text-align:center;background:#191b20;border:1px solid #ffffff06;border-radius:16px;padding:16px 5px 12px}.large{display:block;width:64px;height:64px;object-fit:contain;margin:0 auto 9px}.label{font-size:13px;margin-bottom:17px}.small{width:34px;height:34px;object-fit:contain}.caption{margin-top:14px;color:#858d9c;font-size:12px}</style><h1>通知图标 · 统一风格预览</h1><div class=grid>'+images.map(x=>'<div class=cell><img class=large src="'+x.src+'"><div class=label>'+labels[x.name]+'</div><img class=small src="'+x.src+'"></div>').join('')+'</div><div class=caption>上排查看造型，下排查看小尺寸清晰度。QQ、微信使用应用原图标。</div></html>');
 await page.evaluate(()=>Promise.all([...document.images].map(img=>img.decode())));
 await page.screenshot({path:path.join(output,'vector-icon-preview.png')});
 console.log('Rendered the summary PNG and previewed the existing notification artwork');
}finally{await browser.close();}
