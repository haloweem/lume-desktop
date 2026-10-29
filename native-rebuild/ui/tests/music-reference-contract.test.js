import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync, readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const ui=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const reference=path.join(ui,'music-visualizers','reference');
const modes=['classic','cadenza','partita','fume','tilt','monet','sonnet'];
const availableModes=['partita','fume','sonnet','gallery','3d','lumiere','cadenza','tilt','cappella','diorama','pendolo','classic','tempera'];
const defaults={version:3,mode:'partita',fontFamily:'',animationIntensity:'normal',lyricsFontColor:'#ffffff',lyricsKaraokeFillColor:'#ffffff',effectMode:'none',
  sonnetColors:{lyricsFontColor:'#ffffff',karaokeFillColor:'#38bdf8',glowColor:'#60a5fa'}};

test('bounded preferences are atomic, retained across ticks and structural only on change',async()=>{
  const {createStateReducer}=await import('../music-visualizers/reference/src/protocol.js');
  const reducer=createStateReducer(lines=>lines);
  assert.equal(reducer.state.preferences,undefined);
  const preferences={...defaults,fontFamily:'Arial',lyricsFontColor:'#12abef'};
  assert.equal(reducer.update({preferences}).structural,true);
  assert.deepEqual(reducer.state.preferences,preferences);
  const stable=reducer.state.preferences;
  preferences.fontFamily='mutated';
  assert.equal(stable.fontFamily,'Arial','snapshot is owned by the reducer');
  assert.equal(reducer.update({preferences:{...stable}}).structural,false);
  assert.equal(reducer.state.preferences,stable);
  for(let i=0;i<120;i++)assert.equal(reducer.update({currentTime:i/60}).structural,false);
  assert.equal(reducer.state.preferences,stable);
  assert.equal(reducer.update({preferences:defaults}).structural,true);
  assert.deepEqual(reducer.state.preferences,defaults);
  for(const mode of availableModes)assert.doesNotThrow(()=>reducer.update({preferences:{...defaults,mode}}));
  for(const fontFamily of ['a'.repeat(128),'\u4e2d'.repeat(42)+'ab','\ud83c\udfb5'.repeat(32)])
    assert.doesNotThrow(()=>reducer.update({preferences:{...defaults,fontFamily}}));
  const missing={...defaults};delete missing.mode;
  for(const preferences of [null,[],{},missing,{...defaults,extra:true},{...defaults,version:4},
    {...defaults,mode:'unknown'},...['monet'].map(mode=>({...defaults,mode})),
    {...defaults,animationIntensity:'fast'},{...defaults,fontFamily:1},
    ...['a'.repeat(129),'\u4e2d'.repeat(43),'\ud83c\udfb5'.repeat(33),'A\nB','A\u007fB','A\u0085B',
      '\ud800','\udfff','A\ud800B','\ud800\ud800','\udc00\ud800'].map(fontFamily=>({...defaults,fontFamily})),
    ...['#FFF','#ABCDEF','red','#123456; color:red'].flatMap(color=>[
      {...defaults,lyricsFontColor:color},{...defaults,lyricsKaraokeFillColor:color}])]){
    const before=reducer.state;
    assert.throws(()=>reducer.update({preferences,lines:[{fullText:'rejected'}]}),TypeError);
    assert.equal(reducer.state,before,'invalid preference deltas cannot partially commit');
  }
});

test('Sonnet colors preserve the exact original theme override order and remain isolated from other modes',async()=>{
  const {resolvePreferenceTheme}=await import('../music-visualizers/reference/preference-theme.js');
  const {preferenceSnapshot}=await import('../music-visualizers/reference/src/protocol.js');
  const input={...defaults,mode:'sonnet',lyricsFontColor:'#112233',lyricsKaraokeFillColor:'#224466',
    sonnetColors:{lyricsFontColor:'#aabbcc',karaokeFillColor:'#dd8844',glowColor:'#446688'}};
  const snapshot=preferenceSnapshot(undefined,input);
  input.sonnetColors.karaokeFillColor='#000000';
  assert.equal(snapshot.sonnetColors.karaokeFillColor,'#dd8844');
  assert.throws(()=>{snapshot.sonnetColors.glowColor='#000000';},TypeError);
  assert.equal(preferenceSnapshot(snapshot,structuredClone(snapshot)),snapshot);
  const theme=resolvePreferenceTheme('sonnet',snapshot);
  for(const key of ['primaryColor','accentColor','secondaryColor'])assert.equal(theme[key],'#112233',
    'historical buildVisualizerTheme applies the global font override after Sonnet colors');
  assert.equal(theme.karaokeFillColor,'#dd8844');
  for(const mode of [...modes.filter(mode=>mode!=='sonnet'),'gallery','3d'])
    assert.equal(resolvePreferenceTheme(mode,snapshot).karaokeFillColor,'#224466');
});

test('generated preference themes preserve original build order and font quoting',async()=>{
  const {themes,resolvePreferenceTheme,resolveThemeFontStack}=await import('../music-visualizers/reference/preference-theme.js');
  for(const mode of [...modes,'gallery','3d']){
    assert.equal(resolvePreferenceTheme(mode),themes[mode]);
    const theme=resolvePreferenceTheme(mode,{...defaults,fontFamily:'  "A\\B, C"  ',animationIntensity:'calm',
      lyricsFontColor:'#123456',lyricsKaraokeFillColor:'#abcdef'});
    for(const key of ['primaryColor','accentColor','secondaryColor'])assert.equal(theme[key],'#123456');
    assert.equal(theme.karaokeFillColor,mode==='sonnet'?'#38bdf8':'#abcdef');
    assert.equal(theme.animationIntensity,'calm');
    assert.ok(resolveThemeFontStack(theme).startsWith('"A\\\\B, C", '));
    assert.deepEqual(resolvePreferenceTheme(mode,defaults),themes[mode]);
    assert.equal(resolvePreferenceTheme(mode,defaults).fontFamily,undefined);
  }
  assert.ok(resolveThemeFontStack({fontStyle:'sans',fontFamily:'serif'}).startsWith('serif, '));
  assert.ok(resolveThemeFontStack({fontStyle:'sans',fontFamily:'A"B\\C'}).startsWith('"A\\"B\\\\C", '));
});

test('host transfers preferences only on change and keeps one iframe',async()=>{
  const {mountReference}=await import('../music-visualizers/reference-host.js');
  const listeners=new Map(),messages=[];
  const window={addEventListener:(type,fn)=>listeners.set(type,fn),removeEventListener:type=>listeners.delete(type)};
  const frame={setAttribute(){},contentWindow:{postMessage:message=>messages.push(message)},remove(){this.removed=true;}};
  const host={ownerDocument:{defaultView:window,createElement:()=>frame},replaceChildren(node){this.child=node;},dataset:{}};
  const initial={lines:[],current:null,currentTime:0,preferences:{...defaults}};
  const mounted=mountReference(host,initial,'partita');
  const ready=()=>listeners.get('message')({source:frame.contentWindow,origin:new URL(frame.src).origin,data:{type:'lume-lyric-stage-ready'}});
  ready();assert.deepEqual(messages.at(-1).state.preferences,defaults);
  for(let i=0;i<120;i++){
    mounted.update({...initial,currentTime:i/60,preferences:{...defaults}});
    assert.equal(messages.at(-1).state.preferences,undefined);
  }
  const changed={...defaults,fontFamily:'Arial'};
  mounted.update({...initial,preferences:changed});
  assert.deepEqual(messages.at(-1).state.preferences,changed);
  mounted.update({...initial,preferences:undefined});
  assert.equal(messages.at(-1).state.preferences,undefined);
  ready();assert.deepEqual(messages.at(-1).state.preferences,changed,'readiness republishes the retained preference');
  mounted.update(initial);assert.deepEqual(messages.at(-1).state.preferences,defaults);
  assert.equal(host.child,frame);
  mounted.dispose();const count=messages.length;mounted.update(initial);
  assert.equal(messages.length,count);assert.equal(listeners.size,0);assert.equal(frame.removed,true);
});

test('reference host contract is present for every historical mode',()=>{
  assert.equal(existsSync(path.join(reference,'index.html')),true);
  assert.equal(existsSync(path.join(reference,'entry.js')),true);
  assert.equal(existsSync(path.join(reference,'reference.css')),true);
  const html=readFileSync(path.join(reference,'index.html'),'utf8');
  assert.match(html,/href=["']\.\/reference\.css["']/);
  assert.match(html,/src=["']\.\/entry\.js["']/);
  const entry=readFileSync(path.join(reference,'entry.js'),'utf8');
  for(const mode of modes)assert.match(entry,new RegExp(`['"]${mode}['"]`));
  assert.match(entry,/lume-lyric-stage-ready/);
  assert.match(entry,/lume-lyric-stage-error/);
});

test('renderer adapters import the generated protocol included in the staged package',()=>{
  const manifest=JSON.parse(readFileSync(path.join(reference,'manifest.json'),'utf8'));
  assert.ok(manifest.generated.some(item=>item.path==='preference-protocol.js'));
  for(const name of ['reference-host.js','gallery.js','three-d.js']){
    const source=readFileSync(path.join(ui,'music-visualizers',name),'utf8');
    assert.match(source,/from '\.\/reference\/preference-protocol\.js'/);
    assert.doesNotMatch(source,/from ['"][^'"]*\/(?:src|source)\//);
  }
});

test('generated graph is mode split and does not include forbidden app surfaces',()=>{
  const manifestPath=path.join(reference,'manifest.json');
  assert.equal(existsSync(manifestPath),true);
  const manifest=JSON.parse(readFileSync(manifestPath,'utf8'));
  assert.deepEqual(manifest.modes?.sort(),[...modes].sort());
  assert.equal(manifest.builder,'esbuild');
  assert.deepEqual(manifest.forbiddenImportGraph,[]);
  assert.equal(manifest.importGraph.some(value=>/electron|providers?\/|accounts?\/|backgrounds\/registry|components\/app|stores\//i.test(value)),false);
  assert.deepEqual(manifest.importGraph.filter(value=>value.startsWith('src/services/')),['src/services/baseThemes.ts']);
  assert.deepEqual(manifest.crossModeImports,[]);
  const graph=JSON.parse(readFileSync(path.join(reference,'source/build-metafile.json'),'utf8')).inputs;
  const owner=value=>value.match(/visualizer\/(classic|cadenza|partita|fume|tilt|monet|sonnet)\//)?.[1];
  for(const mode of modes){
    assert.equal(existsSync(path.join(reference,`mode-${mode}.js`)),true,mode);
    const seen=new Set();
    const visit=source=>{
      if(seen.has(source))return;seen.add(source);
      assert.ok(!owner(source)||owner(source)===mode,`${mode} imports another mode implementation: ${source}`);
      for(const item of graph[source]?.imports??[])visit(item.path);
    };
    visit(manifest.entries[mode]);
    assert.ok(manifest.modeBundles[mode].bytes>0);
  }
});

test('all generated artifacts match their recorded hashes',()=>{
  const manifest=JSON.parse(readFileSync(path.join(reference,'manifest.json'),'utf8'));
  assert.ok(manifest.adapters['../../music-preference-values.js'],'shared preference contract is covered by provenance');
  for(const item of manifest.generated){
    const bytes=readFileSync(path.join(reference,item.path));
    assert.equal(bytes.length,item.bytes,item.path);
    assert.equal(createHash('sha256').update(bytes).digest('hex'),item.sha256,item.path);
  }
  for(const [file,hash] of Object.entries(manifest.adapters))assert.equal(createHash('sha256').update(readFileSync(path.join(reference,file))).digest('hex'),hash,file);
  for(const dependency of Object.values(manifest.dependencies)){
    assert.equal(existsSync(path.join(reference,'licenses',dependency.licenseMetadata)),true);
    for(const license of dependency.licenses??[])assert.equal(existsSync(path.join(reference,'licenses',license)),true);
  }
});

test('delta ticks retain lyrics and large artwork without structural renders',async()=>{
  const {createStateReducer}=await import('../music-visualizers/reference/src/protocol.js');
  const {prepareLines}=await import('../music-visualizers/reference/contracts.js');
  const reducer=createStateReducer(prepareLines);
  const current={id:'fixture',title:'Song',artist:'Artist',album:'Album',source:'local',artwork:'data:image/png;base64,'+'a'.repeat(262144)};
  const lines=[{startTime:0,endTime:6,fullText:'A line of music',words:[],translation:''}];
  assert.equal(reducer.update({lines,current,activeLineIndex:0,playing:true}).structural,true);
  const stableLines=reducer.state.lines,stableTrack=reducer.state.current;
  assert.ok(stableLines[0].words.length>1);
  assert.equal(stableLines[0].referenceTiming,'estimated-from-line');
  assert.equal(stableLines[0].isWordByWord,false);
  assert.deepEqual(lines[0].words,[],'host data is never changed');
  for(let i=1;i<=240;i++){
    assert.equal(reducer.update({lines:undefined,current:undefined,currentTime:i/60}).structural,false);
    assert.equal(reducer.state.lines,stableLines);
    assert.equal(reducer.state.current,stableTrack);
  }
  assert.equal(reducer.update({current:{...current},lines:structuredClone(lines)}).structural,false);
  assert.equal(reducer.update({current:null,lines:[]}).structural,true);
  assert.equal(reducer.state.current,null);
  assert.equal(reducer.state.lines.length,0);
});

test('word timing remains authored when supplied and CJK ordinary LRC is renderable',async()=>{
  const {prepareLines}=await import('../music-visualizers/reference/contracts.js');
  const words=[{text:'Test',startTime:2.2,endTime:3.1}];
  const result=prepareLines([{startTime:2,endTime:6,fullText:'Test',words},
    {startTime:6,endTime:12,fullText:'\u98ce\u7a7f\u8fc7\u4e86\u5c71\u8c37',words:[]}]);
  assert.equal(result[0].words,words);
  assert.equal(result[0].referenceTiming,'authored-words');
  assert.ok(result[1].words.length>1);
  assert.equal(result[1].words.map(word=>word.text).join(''),result[1].fullText);
  assert.ok(result[1].words.every(word=>word.startTime>=6&&word.endTime<=12));
});

test('messages require same-origin parent, valid type and fixed iframe mode',async()=>{
  const {acceptsMessage,localArtwork}=await import('../music-visualizers/reference/src/protocol.js');
  const parent={},origin='http://127.0.0.1:8080';
  const valid={source:parent,origin,data:{type:'lume-lyric-stage',mode:'classic',state:{}}};
  assert.equal(acceptsMessage(valid,parent,origin,'classic'),true);
  for(const event of [{...valid,source:{}},{...valid,origin:'https://invalid.example'},
    {...valid,data:{...valid.data,mode:'sonnet'}},{...valid,data:{...valid.data,state:null}}]){
    assert.equal(acceptsMessage(event,parent,origin,'classic'),false);
  }
  assert.equal(localArtwork('https://remote.example/cover.jpg',origin),undefined);
  assert.equal(localArtwork('assets/music/forest.webp',origin+'/music.html'),origin+'/assets/music/forest.webp');
  const lease='http://127.0.0.1:49123/'+'a'.repeat(64)+'/media.png';
  assert.equal(localArtwork(lease,'https://lume.local/music.html'),lease);
  for(const value of ['https://p1.music.126.net/cover.jpg','https://y.gtimg.cn/cover.jpg','https://imge.kugou.com/cover.jpg'])
    assert.equal(localArtwork(value,origin),value);
  for(const value of [lease+'?path=private',lease.replace('49123','99999'),
    'http://127.0.0.1:49123/private.png','https://y.gtimg.cn:8443/cover.jpg',
    'https://secret@y.gtimg.cn/cover.jpg','https://y.gtimg.cn.attacker.invalid/cover.jpg'])
    assert.equal(localArtwork(value,origin),undefined);
});

test('source tool documents the historical parser fallback and hash manifest',()=>{
  const tool=readFileSync(path.join(ui,'tools','extract-music-modes.mjs'),'utf8');
  assert.match(tool,/esbuild/);
  assert.match(tool,/buildTimedWords/);
  assert.match(tool,/sha256/);
  assert.match(tool,/VisualizerShell/);
});

test('CSP keeps code and texture fetches local while allowing bounded cover origins',()=>{
  const html=readFileSync(path.join(reference,'index.html'),'utf8');
  assert.match(html,/connect-src data:;/);
  assert.match(html,/script-src 'self' 'unsafe-eval';/);
  assert.deepEqual(html.match(/img-src ([^;]+);/)[1].split(/\s+/),[
    "'self'","data:","blob:","https://p1.music.126.net","https://p2.music.126.net",
    "https://p3.music.126.net","https://p4.music.126.net","https://y.gtimg.cn",
    "https://imge.kugou.com","https://imgessl.kugou.com","http://127.0.0.1:*"]);
  assert.match(readFileSync(path.join(reference,'reference.css'),'utf8'),/:root\{color-scheme:dark;\}/);
});

test('production CSP permits QQ and Kugou collection cover CDNs without arbitrary remote images',()=>{
  const html=readFileSync(path.join(ui,'index.html'),'utf8');
  const origins=html.match(/img-src ([^;]+);/)[1].split(/\s+/);
  for(const origin of ['https://qpic.y.qq.com','https://img.kgimg.com'])
    assert.ok(origins.includes(origin),`Missing provider cover origin: ${origin}`);
  assert.ok(!origins.includes('*')&&!origins.includes('https:'));
  assert.match(html,/connect-src 'self' https:\/\/lume-music\.local https:\/\/lume-fonts\.local http:\/\/127\.0\.0\.1:\*;/);
});
