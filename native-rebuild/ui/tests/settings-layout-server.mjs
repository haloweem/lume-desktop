// Development-only comparison: original Ant Card/Switch/Select + original styled
// blocks, versus the production lightweight clock page. No dependency is shipped.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {clockPage} from '../clock.js';
const ui=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const reference=process.argv[2];
if(!reference) throw Error('Pass original src/vscode-windhawk-ui directory');
const require=createRequire(path.join(reference,'package.json'));
const React=require('react');
const {renderToStaticMarkup}=require('react-dom/server');
const Card=require('antd/lib/card').default;
const Switch=require('antd/lib/switch').default;
const Select=require('antd/lib/select').default;
const Button=require('antd/lib/button').default;
const source=fs.readFileSync(path.join(reference,'apps/vscode-windhawk-ui/src/app/panel/DesktopWidgetDetails.tsx'),'utf8');
const e=React.createElement;
const description='在桌面显示时间，支持 Welfare、Mond 时钟和 Inside Dream 样式。';
const baseline=renderToStaticMarkup(e('div',{className:'Settings'},e(Card,{
  tabList:[{key:'settings',tab:'设置'},{key:'details',tab:'详情'}],activeTabKey:'settings',
  title:e('div',{className:'SettingsHead'},e('button',{className:'SettingsBack'},'←'),e('div',{className:'SettingsHeading'},e('h3',{className:'SettingsTitle'},'时间'),e('p',{className:'SettingsSubtitle'},description)))
},e('div',{className:'SettingsBody'},
  e('div',{className:'Row'},e('label',{className:'Label'},'启用组件'),e('div',{className:'Control'},e(Switch,{checked:true}))),
  e('div',{className:'Row'},e('label',{className:'Label'},'时间样式'),e('div',{className:'Control'},e(Select,{value:'insideDream',options:[{value:'insideDream',label:'Inside Dream'}]}))),
  e('div',{className:'ResetRow'},e(Button,null,'恢复默认'))))));
const block=name=>source.match(new RegExp('const '+name+' = styled[^`]*`([^`]+)`'))?.[1];
let originalStyles=['SettingsHead','SettingsHeading','SettingsTitle','SettingsSubtitle','SettingsBody','Row','Label','Control','ResetRow'].map(name=>`.${name}{${block(name)}}`).join('\n');
// Control's nested selector is expressed without nesting for the browser baseline.
originalStyles=originalStyles.replace('.ant-select { width: 240px; max-width: 100%; }','');
originalStyles+=`.Control .ant-select{width:240px;max-width:100%}.Settings{height:100%;overflow:auto;padding:20px clamp(24px,4vw,72px) 28px;padding-top:calc(clamp(50px,5.2vh,64px) - 8px);background:#000}.Settings>.ant-card{min-height:100%;border:0;border-radius:0;background:transparent;box-shadow:none}.Settings>.ant-card>.ant-card-head{border-bottom-color:rgba(255,255,255,.09);background:transparent}.SettingsBack{display:inline-flex;align-items:center;justify-content:center;flex:0 0 48px;width:48px;height:48px;padding:0;border:0;background:transparent;color:rgba(255,255,255,.6)}html,body{margin:0;width:100%;height:100%}@media(max-width:700px){.Settings{padding-inline:12px}}`;
const html=(css,body)=>`<!doctype html><meta charset="utf-8"><style>${css}</style>${body}`;
const originalCss=fs.readFileSync(path.join(reference,'apps/vscode-windhawk-ui/src/app/app.css'),'utf8');
const server=http.createServer((req,res)=>{
 const route=new URL(req.url,'http://localhost').pathname;
 res.setHeader('Content-Type','text/html;charset=utf-8');
 if(route==='/reference') res.end(html(originalCss+originalStyles,baseline));
 else if(route==='/current') res.end(html(fs.readFileSync(path.join(ui,'reference.css'),'utf8')+fs.readFileSync(path.join(ui,'app.css'),'utf8'),clockPage({clock:{enabled:true,style:'insideDream'},status:'saved'})));
 else res.end('<!doctype html><meta charset="utf-8"><h1>原版 / 重构设置页同尺寸对照</h1>'+[1536,1280,680].map(w=>`<h2>${w} × 864</h2><iframe title="原版 ${w}" width="${w}" height="864" src="/reference"></iframe><iframe title="重构 ${w}" width="${w}" height="864" src="/current"></iframe>`).join(''));
});
server.listen(18761,'127.0.0.1',()=>console.log('Settings comparison http://127.0.0.1:18761'));
