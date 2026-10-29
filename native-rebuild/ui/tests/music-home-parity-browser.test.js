import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile, mkdir, writeFile} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {createServer} from 'node:http';
import {createRequire} from 'node:module';
import {homedir} from 'node:os';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// Test-only source harness. Every generated file belongs to the evidence directory.
const ui = fileURLToPath(new URL('..', import.meta.url));
const upstream = 'H:/refactor/history/lume1.0.1/third_party/folia-major/upstream';
const evidence = path.resolve(ui, `../../build-artifacts/native-rebuild/music-home-parity-${new Date().toISOString().replace(/[:.]/g,'-')}`);
const slash = value => value.replaceAll('\\', '/');
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const round = value => Math.round(value * 1000) / 1000;
const geometryTolerance = 1 / 64;
const layoutFile = 'src/components/folia-grid/MusicHomeNextLayout.tsx';
const gridFile = 'src/components/Grid3D.tsx';
const cssFile = 'dist/assets/bootstrap-DG1iL-Sw.css';
const historicalAllowed = new Set([
  layoutFile, 'src/components/app/ResilientCoverImage.tsx',
  'src/components/app/SongMembershipBadge.tsx',
  'src/components/app/home/OnlineProviderConnectPanel.tsx',
  'src/components/shared/PlaylistSelectionDropdown.tsx',
  'src/utils/coverUrl.ts', 'src/components/folia-grid/collectionCoverUrl.ts',
  'src/services/onlineMusic/songMembershipAccess.ts',
]);

function walk(node, visit) {
  if (!node || typeof node !== 'object') return;
  if (node.type) visit(node);
  for (const [key, value] of Object.entries(node)) {
    if (['loc', 'tokens', 'comments'].includes(key)) continue;
    if (Array.isArray(value)) value.forEach(child => walk(child, visit));
    else if (value && typeof value === 'object') walk(value, visit);
  }
}

function extractGrid(parse, source) {
  const ast = parse(source, {sourceType:'module', plugins:['typescript', 'jsx']});
  const find = predicate => {
    const found = [];
    walk(ast, node => { if (predicate(node)) found.push(node); });
    assert.equal(found.length, 1, 'Historical extraction must be unambiguous');
    return found[0];
  };
  const variable = name => find(node => node.type === 'VariableDeclarator' && node.id.name === name).init;
  const hasAttribute = (node, name) => node.type === 'JSXOpeningElement' &&
    node.attributes.some(attribute => attribute.name?.name === name);
  const fallback = find(node => node.type === 'JSXAttribute' && node.name.name === 'fallbackContent');
  const parts = {
    search:variable('searchControl'), style:variable('homeSurfaceStyle'),
    root:find(node => hasAttribute(node, 'data-lumenic-home-surface')),
    guest:fallback.value.expression.alternate.consequent,
  };
  return Object.fromEntries(Object.entries(parts).map(([name, node]) => [name, {
    text:source.slice(node.start, node.end), startLine:node.loc.start.line, endLine:node.loc.end.line,
  }]));
}

function fakeModule() {
  return `
const data = () => window.parityFixture;
const guest = () => window.parityScenario === 'guest';
const deny = name => () => { throw new Error('Unimplemented parity service: ' + name); };
const search = {isSearchOpen:false,searchQuery:'',searchResults:[],isSearching:false,
  isLoadingMore:false,searchError:null,hasMore:false,clearSearchContext(){}};
export const useSearchNavigationStore = selector => selector(search);
export const useOnlineProviderAccountStore = selector => selector({activeProviderId:guest() ? '' : 'netease'});
export const useShallow = selector => selector;
export const useTranslation = () => ({t:key => ({'home.playlists':data().listTitle,
  'home.recommendations':'For you','noSongsFound':'No songs'})[key] || key});
export const getSongArtistLabel = song => song.artists.map(artist => artist.name).join(', ');
export const getSongCoverUrl = song => song.coverUrl;
export const getPlaybackSourceRef = () => ({kind:'online',providerId:'netease',providerData:{}});
export const getLumeEmbeddedTopInset = () => data().topInset;
const trackState = () => ({tracks:guest() ? [] : data().historicalTracks,loading:false,error:null,retry:deny('retry')});
export const usePlaylistHomeTracks = trackState;
export const useLikedTracks = trackState;
export const useRecentPlayedTracks = trackState;
export const isLikedCollection = () => false;
export const libraryService = {getCollectionTracks:deny('getCollectionTracks')};
export const accountService = {isSongLiked:() => false,getPlaylistsForSong:() => [],
  toggleSongLike:deny('toggleSongLike'),addSongToPlaylist:deny('addSongToPlaylist'),
  getProviderSummaries:() => data().providers};
`;
}

function referenceEntry(parts) {
  return `
import React, {useRef,useState} from 'react';
import {createRoot} from 'react-dom/client';
import {Search,Loader2} from 'lucide-react';
import {MusicHomeNextLayout} from 'historical-layout';
import HistoricalOnlineProviderConnectPanel from 'historical-guest';
import {getLumeEmbeddedTopInset,accountService} from 'parity-fakes';
const f = window.parityFixture;
// T09 intentionally changed the content, not the original guest-panel layout.
function OnlineProviderConnectPanel(props) {
  if(props.prompt!==f.historicalGuestPrompt)throw new Error('Historical guest copy changed; review the fixture adaptation');
  return <HistoricalOnlineProviderConnectPanel {...props} prompt={f.guestPrompt}/>;
}
function Harness() {
  const guest = window.parityScenario === 'guest';
  const [homeViewTab,setHomeViewTab] = useState(window.parityScenario === 'list' ? 'playlist' : 'recommendations');
  const [focusedIndex,setFocusedIndex] = useState(0);
  const [searchQuery,setSearchQuery] = useState('');
  const gridRootRef = useRef(null);
  const isSearchingActive = false, homePrimaryColor = '#ffffff', homeIsDaylight = false;
  const activeProviderId = guest ? '' : 'netease', activeProviderNeedsRelogin = false;
  const activeProviderSummary = null, onlineProviderPlatform = {providers:f.providers};
  const initLogin = () => {throw new Error('No account access in parity harness');};
  const t = (key, options) => key === 'home.connectProviderAccount' ? '\\u8fde\\u63a5' + options.provider : key;
  const handleSearch = event => {event?.preventDefault();window.parityActions.push({action:'search',query:searchQuery});};
  const homeSurfaceStyle = ${parts.style.text};
  const searchControl = ${parts.search.text};
  const items = guest ? [] : homeViewTab === 'playlist' ? f.historicalPlaylists : f.historicalRecommendations;
  return ${parts.root.text}
    <div data-music-home-content="true" className="flex-1 min-h-0 flex w-full">
      <MusicHomeNextLayout title={f.listTitle} items={items} playlistItems={guest ? [] : f.historicalPlaylists}
        showPlaylistLibrary={!guest} focusedIndex={focusedIndex} onFocusedIndexChange={setFocusedIndex}
        onPlaySong={song => window.parityActions.push({action:'playNow',id:song.id})}
        searchSlot={searchControl} homeViewTab={homeViewTab} setHomeViewTab={setHomeViewTab}
        theme={{}} activeUserId={guest ? null : 'synthetic-user'} canCreatePlaylist={false}
        fallbackContent={guest ? ${parts.guest.text} : undefined} />
    </div>
  </div>;
}
createRoot(document.getElementById('parity-root')).render(<Harness/>);
`;
}

async function buildReference({requireHistory, remember, report}) {
  const esbuild = requireHistory('./node_modules/.pnpm/esbuild@0.25.12/node_modules/esbuild');
  const {parse} = requireHistory('@babel/parser');
  const grid = (await remember('historical', gridFile)).toString('utf8');
  const layout = (await remember('historical', layoutFile)).toString('utf8');
  const parts = extractGrid(parse, grid);
  report.extraction = Object.fromEntries(Object.entries(parts).map(([name, part]) => [name,
    {file:gridFile,startLine:part.startLine,endLine:part.endLine,sha256:hash(part.text)}]));
  report.sourceGeometry = {
    sidebar:'w-52 = 208px; px-3 = 12px; py-5 = 20px; embedded inset = 34px',
    main:'md:px-10 = 40px; md:pt-7 = 28px; search margin-bottom = 28px',
    search:'Grid3D h-[42px], max-w-[480px], px-[14px], input pl-[24px]',
    table:'minmax(0,1fr) 4.5rem minmax(10rem,0.55fr) 5rem; gap-4; px-4 py-2',
    rows:'h-11 cover = 44px; 8px top/bottom padding + 1px border = 61px; header = 32px',
    recommendations:'xl >= 80rem: 2fr repeat(4,minmax(0,1fr)); supply 6 columns; gap 12px',
    zoom125:'1152 CSS px hits sm featured 2 columns and lg supply 4 columns',
    evidence:layout.split(/\r?\n/).flatMap((line,index) =>
      /data-music-home-|grid-cols-\[minmax|xl:grid-cols|sm:grid-cols/.test(line) ? [{line:index+1,text:line.trim()}] : []),
  };
  await writeFile(path.join(evidence,'source-geometry.json'), JSON.stringify(report.sourceGeometry,null,2)+'\n');
  await writeFile(path.join(evidence,'reference-entry.tsx'), referenceEntry(parts));
  await writeFile(path.join(evidence,'reference-fakes.js'), fakeModule());
  const fakePaths = [
    'src/services/onlineMusic/songMetadata', 'src/services/onlineMusic/application/libraryService',
    'src/services/onlineMusic/application/accountService', 'src/utils/appPlaybackGuards',
    'src/components/visualizer/lumeTopVisualCompositor', 'src/components/folia-grid/likedTracks',
    'src/components/folia-grid/playlistHomeTracks', 'src/components/folia-grid/recentPlayedSongs',
    'src/stores/useSearchNavigationStore', 'src/stores/useOnlineProviderAccountStore',
  ];
  const aliases = new Set(fakePaths.map(file => slash(path.resolve(upstream,file)).toLowerCase()));
  report.aliases = [...fakePaths,'react-i18next','zustand/react/shallow'];
  const result = await esbuild.build({
    absWorkingDir:upstream,stdin:{contents:referenceEntry(parts),loader:'tsx',resolveDir:upstream,sourcefile:'parity-entry.tsx'},
    bundle:true,write:false,metafile:true,platform:'browser',format:'esm',jsx:'automatic',
    outfile:path.join(evidence,'reference.js'),define:{'process.env.NODE_ENV':'"production"'},
    plugins:[{name:'closed-parity-dependency-boundary',setup(build) {
      build.onResolve({filter:/.*/}, args => {
        if (['parity-fakes','react-i18next','zustand/react/shallow'].includes(args.path))
          return {path:'fakes',namespace:'parity'};
        if (args.path === 'historical-layout') return {path:path.resolve(upstream,layoutFile)};
        if (args.path === 'historical-guest') return {path:path.resolve(upstream,'src/components/app/home/OnlineProviderConnectPanel.tsx')};
        if (args.path.startsWith('.')) {
          const target = slash(path.resolve(path.dirname(args.importer),args.path)).toLowerCase();
          if (aliases.has(target)) return {path:'fakes',namespace:'parity'};
        }
      });
      build.onLoad({filter:/.*/,namespace:'parity'}, () => ({contents:fakeModule(),loader:'js'}));
      build.onLoad({filter:/\.[cm]?[jt]sx?$/}, async args => {
        const relative = slash(path.relative(upstream,args.path));
        if (!relative.startsWith('src/')) return;
        assert.ok(historicalAllowed.has(relative), `Disallowed historical dependency: ${relative}`);
        return {contents:await remember('historical',relative),loader:relative.endsWith('tsx') ? 'tsx' : 'ts'};
      });
    }}],
  });
  assert.ok(Object.keys(result.metafile.inputs).some(file => slash(file).endsWith(layoutFile)));
  report.runtime = {esbuild:esbuild.version,react:requireHistory('react/package.json').version,
    reactDom:requireHistory('react-dom/package.json').version};
  report.bundleInputs = Object.keys(result.metafile.inputs);
  await writeFile(path.join(evidence,'reference-metafile.json'),JSON.stringify(result.metafile,null,2)+'\n');
  return result.outputFiles[0].contents;
}

function makeFixture(fixture, PNG) {
  const covers = fixture.recommendations.map((_, index) => {
    const png = new PNG({width:128,height:128});
    for (let y=0;y<128;y++) for (let x=0;x<128;x++) {
      const offset=(y*128+x)*4, band=Math.floor(x/32)+Math.floor(y/32);
      png.data[offset]=(37+index*31+band*15)%220;
      png.data[offset+1]=(83+index*19+y)%220;
      png.data[offset+2]=(139+index*23+x)%220;
      png.data[offset+3]=255;
    }
    return PNG.sync.write(png);
  });
  const artwork = index => 'data:image/png;base64,'+covers[index%covers.length].toString('base64');
  const tracks = fixture.tracks.map((track,index) => ({...track,artwork:artwork(index),source:''}));
  const recommendations = fixture.recommendations.map((item,index) => ({...item,artwork:artwork(index),
    description:item.trackCount+' \u9996',tracks}));
  const playlists = fixture.playlists.map((item,index) => ({...item,artwork:artwork(index),tracks}));
  const historicalCollection = item => ({id:item.id,name:item.title,coverUrl:item.artwork,
    trackCount:item.trackCount || tracks.length,type:'playlist',raw:{id:item.id,name:item.title,
      providerId:'netease',type:'playlist',isOwned:item.owned === true,coverUrl:item.artwork}});
  return {covers,data:{...fixture,tracks,recommendations,playlists,
    historicalTracks:tracks.map(track => ({id:track.id,name:track.title,artists:[{name:track.artist}],
      album:{name:track.album},durationMs:track.duration*1000,coverUrl:track.artwork})),
    historicalRecommendations:recommendations.map(historicalCollection),
    historicalPlaylists:playlists.map(historicalCollection)}};
}

function html(side, fixture) {
  const styles = side === 'historical' ? '<link rel="stylesheet" href="/historical.css">' :
    '<link rel="stylesheet" href="/reference.css"><link rel="stylesheet" href="/app.css"><link rel="stylesheet" href="/music.css">';
  return `<!doctype html><html lang="zh-CN"><head><meta charset="utf-8"><title>Music home parity</title>${styles}
<style>
html {font-size:16px; color-scheme:dark; --font-sans:${fixture.fontFamily};}
html,body,#parity-root {width:100%;height:100%;margin:0;overflow:hidden;}
body,#parity-root,.MusicRoot,[data-lumenic-home-surface] {font-family:${fixture.fontFamily};}
body {background:#09090b;}
#parity-root {position:relative;isolation:isolate;}
body > .parity-background {position:absolute;inset:0;width:100%;height:100%;object-fit:cover;object-position:center;}
</style></head><body>${side === 'historical' ? '<img class="parity-background" src="/assets/music/forest.webp" alt="">' : ''}
<div id="parity-root"></div><script type="module" src="/${side === 'historical' ? 'reference' : 'current-entry'}.js"></script></body></html>`;
}

const currentEntry = `
import {initialMusicState} from '/music.js';
import {mountMusicView} from '/music-view.js';
import {musicCollectionKey} from '/music-collections.js';
import {createMusicProviderAccount} from '/music-provider-account.js';
const f=window.parityFixture,guest=window.parityScenario==='guest';
const snapshot={...initialMusicState(),session:203,pageOpen:true,pageVisible:true,
  account:{signedIn:!guest},queue:guest?[]:f.tracks,current:null};
window.parityView=mountMusicView(document.getElementById('parity-root'),{snapshot,
  providerAccount:createMusicProviderAccount({send:()=>{throw new Error('No account access in parity harness');}}),
  viewState:{nav:window.parityScenario==='list'?'playlists':'recommendations',
    playlistId:window.parityScenario==='list'?musicCollectionKey(f.playlists[0]):''},
  library:{signedIn:!guest,tracks:guest?[]:f.tracks,recommendations:guest?[]:f.recommendations,
    playlists:guest?[]:f.playlists},
  send:(action,payload)=>{window.parityActions.push({action,payload});return true;},
  onImport:()=>{window.parityActions.push({action:'import'});return [];}
});
`;

async function geometry(page, side) {
  return page.evaluate(side => {
    const historical = side === 'historical';
    const q = selector => document.querySelector(selector);
    const all = selector => [...document.querySelectorAll(selector)];
    const measure = element => {
      if (!element) return null;
      const rect = element.getBoundingClientRect(),style=getComputedStyle(element);
      return {x:rect.x,y:rect.y,width:rect.width,height:rect.height,
        font:style.fontFamily,fontSize:style.fontSize,lineHeight:style.lineHeight,
        padding:style.padding,gap:style.gap,grid:style.gridTemplateColumns,
        color:style.color,background:style.backgroundColor,letterSpacing:style.letterSpacing,
        backdropFilter:style.backdropFilter,fontWeight:style.fontWeight,
        textStartX:rect.x+parseFloat(style.paddingLeft)+parseFloat(style.borderLeftWidth),
        scrollHeight:element.scrollHeight,clientHeight:element.clientHeight,
        text:element.textContent.trim().replace(/\s+/g,' ').slice(0,180)};
    };
    const table=q('[data-music-home-track-list]'), rec=q('[data-music-home-recommendation-playlists]');
    const header=table?.querySelector('[role="row"]'), rows=table?[...table.querySelectorAll('[role="row"]')].slice(1):[];
    const cards=rec?[...rec.querySelectorAll('button')]:[];
    const sidebar=q('[data-music-home-sidebar]');
    const guest=window.parityScenario==='guest'?q(historical?'[data-music-home-scroll] h2':'.MusicGuest h1'):null;
    return {
      viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio,visualScale:visualViewport.scale,
        xl:matchMedia('(min-width:1280px)').matches},
      sidebar:measure(sidebar),brand:measure(sidebar?.firstElementChild),
      sidebarPlaylists:all(historical?'[data-music-home-playlist-list] button':'.MusicPlaylistItem').map(button=>({
        ...measure(button),active:button.getAttribute('aria-current')==='page'})),
      navigation:[...sidebar.querySelectorAll('nav button')].map(measure),
      main:measure(q(historical?'[data-music-home-main]':'.MusicMain')),
      search:measure(q(historical?'[data-music-home-search-form]':'.MusicSearchShell')),
      searchInput:measure(q(historical?'[data-music-home-search-input]':'[data-music-search]')),
      scroller:measure(q(historical?'[data-music-home-scroll]':'.MusicContent')),
      sectionTitle:measure(q(historical?'[data-music-home-grid] h1':'.MusicSectionHeader h1')),
      songTab:measure(q(historical?'[data-music-home-grid] header button':'.MusicSongTab')),
      listSearch:measure(q('[aria-label="\u641c\u7d22\u5f53\u524d\u5217\u8868"]')),
      toolbar:measure(q('[data-music-home-toolbar]')),table:measure(table),header:measure(header),
      headerCells:header?[...header.children].map(measure):[],
      rows:rows.map(measure),rowCells:rows[0]?[...rows[0].children].filter(e=>e.tagName!=='BUTTON').map(measure):[],
      recommendations:measure(rec),cards:cards.map(measure),covers:cards.map(card=>measure(card.firstElementChild)),
      recommendationGrids:rec?[...rec.querySelectorAll('div')].filter(e=>getComputedStyle(e).display==='grid').map(measure):[],
      guestTitle:measure(guest),guestPrompt:measure(guest?.nextElementSibling),
      guestIcon:measure(q(historical?'[data-music-home-scroll] .w-20':'.MusicGuestIcon')),
      guestButtons:guest?all(historical?'[data-music-home-scroll] button':'.MusicProviderButton').map(button=>({...measure(button),disabled:button.disabled})):[],
      images:all('img').map(img=>({loaded:img.complete&&img.naturalWidth>0,width:img.naturalWidth,height:img.naturalHeight})),
      horizontalOverflow:document.documentElement.scrollWidth>innerWidth,
    };
  },side);
}

function deltas(before, after) {
  const diff = (a,b) => !a||!b ? {historical:a,current:b} : Object.fromEntries(
    ['x','y','width','height'].map(key=>[key,round(b[key]-a[key])]));
  return {sidebar:diff(before.sidebar,after.sidebar),brand:diff(before.brand,after.brand),
    navigation:before.navigation.map((item,index)=>diff(item,after.navigation[index])),
    search:diff(before.search,after.search),searchInput:diff(before.searchInput,after.searchInput),
    searchTextStartX:round(after.searchInput.textStartX-before.searchInput.textStartX),
    scroller:diff(before.scroller,after.scroller),toolbar:diff(before.toolbar,after.toolbar),
    header:diff(before.header,after.header),firstRow:diff(before.rows[0],after.rows[0]),
    headerCells:before.headerCells.map((item,index)=>diff(item,after.headerCells[index])),
    recommendations:diff(before.recommendations,after.recommendations),
    covers:before.covers.map((item,index)=>diff(item,after.covers[index])),
    guestTitle:diff(before.guestTitle,after.guestTitle),guestIcon:diff(before.guestIcon,after.guestIcon)};
}

function compareStableGeometry(before, after, view) {
  const result={view,toleranceCssPx:geometryTolerance,comparedFields:0,skippedFields:[],maxDeltaCssPx:0,failures:[]};
  const pixelValue=value => typeof value==='number' ? value :
    typeof value==='string' && /^-?\d+(?:\.\d+)?px$/.test(value) ? Number.parseFloat(value) : NaN;
  const compare=(historical,current,label,fields=['x','y','width','height'])=>{
    for(const field of fields) {
      const key=`${label}.${field}`,a=historical?.[field],b=current?.[field];
      if(a===undefined || a===null || b===undefined || b===null) {
        result.skippedFields.push(key);
        continue;
      }
      result.comparedFields++;
      const delta=pixelValue(b)-pixelValue(a);
      if(Number.isFinite(delta))result.maxDeltaCssPx=Math.max(result.maxDeltaCssPx,Math.abs(delta));
      if(!Number.isFinite(delta) || Math.abs(delta)>geometryTolerance)
        result.failures.push({field:key,historical:a,current:b,deltaCssPx:Number.isFinite(delta)?delta:null});
    }
  };
  // Compare visible shared content, not the different host wrappers or materials.
  compare(before.sidebar,after.sidebar,'sidebar',['width']);
  compare(before.searchInput,after.searchInput,'searchInput',['textStartX','y','height']);
  for(const region of ['brand','search','scroller','toolbar','table','header','songTab','recommendations',
    'guestIcon','guestTitle','guestPrompt','listSearch'])compare(before[region],after[region],region);
  compare(before.listSearch,after.listSearch,'listSearch',['lineHeight']);
  for(const region of ['navigation','sidebarPlaylists','covers','recommendationGrids','rows']) {
    const a=before[region]||[],b=after[region]||[];
    for(let index=0;index<Math.max(a.length,b.length);index++)compare(a[index],b[index],`${region}[${index}]`);
  }
  for(let index=0;index<Math.max(before.headerCells.length,after.headerCells.length);index++) {
    const a=before.headerCells[index],b=after.headerCells[index];
    // The empty actions header stretches in the historical grid but has no current text box.
    compare(a,b,`headerCells[${index}]`,a?.text && b?.text ? ['x','y','width','height'] : ['x','width']);
  }
  return result;
}

async function comparisonImages(PNG, historical, current, name) {
  const a=PNG.sync.read(historical),b=PNG.sync.read(current);
  assert.equal(a.width,b.width);assert.equal(a.height,b.height);
  const pair=new PNG({width:a.width*2,height:a.height}),diff=new PNG({width:a.width,height:a.height});
  PNG.bitblt(a,pair,0,0,a.width,a.height,0,0);
  PNG.bitblt(b,pair,0,0,b.width,b.height,a.width,0);
  let changed=0;
  for (let offset=0;offset<a.data.length;offset+=4) {
    const distance=Math.abs(a.data[offset]-b.data[offset])+Math.abs(a.data[offset+1]-b.data[offset+1])+Math.abs(a.data[offset+2]-b.data[offset+2]);
    if(distance>45)changed++;
    for(let channel=0;channel<3;channel++)diff.data[offset+channel]=Math.abs(a.data[offset+channel]-b.data[offset+channel]);
    diff.data[offset+3]=255;
  }
  await writeFile(path.join(evidence,`${name}-pair.png`),PNG.sync.write(pair));
  await writeFile(path.join(evidence,`${name}-diff.png`),PNG.sync.write(diff));
  return {changedPixels:changed,totalPixels:a.width*a.height,fraction:round(changed/(a.width*a.height)),
    threshold:'sum(abs(RGB historical-current)) > 45; diagnostic, not a parity pass threshold'};
}

test('T02/T03 historical home JSX and current musicPage share controlled scene evidence',
  {skip:process.env.LUME_UI_BROWSER_TESTS!=='1',timeout:240000},async () => {
  await mkdir(evidence,{recursive:true});
  const require=createRequire(import.meta.url),requireHistory=createRequire(path.join(upstream,'package.json'));
  const runtime=path.join(homedir(),'.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules');
  const {chromium}=require(process.env.LUME_PLAYWRIGHT_DIR||path.join(runtime,'playwright'));
  const {PNG}=require(path.join(runtime,'pngjs'));
  const fixtureBytes=await readFile(new URL('./fixtures/music-parity.json',import.meta.url));
  const originalFixture=JSON.parse(fixtureBytes);
  const fixture={...originalFixture,guestPrompt:'\u626b\u7801\u8fde\u63a5\u97f3\u4e50\u5e73\u53f0\u8d26\u6237\u3002'};
  const {data,covers}=makeFixture(fixture,PNG);
  const report={schema:1,status:'running',startedAt:new Date().toISOString(),upstream,evidence,
    fixtureSha256:hash(fixtureBytes),sources:[],scenarios:[],
    geometryPolicy:{toleranceCssPx:geometryTolerance,missingFields:'Compare only fields present on both sides; record skipped fields.',
      excluded:['Materials, colors, opacity, account capabilities and extra local sidebar entries.',
        'Sidebar host y/height and search input x/width use different wrappers.',
        'Empty action header y/height has no shared text geometry.']},
    scaling:'Fixed 1440x900 raster; CSS viewport 1440/scale x 900/scale, deviceScaleFactor=scale. Browser zoom layout equivalent, not a native DPI or browser-toolbar zoom test.',
    controls:{fontFamily:fixture.fontFamily,rootRem:16,embeddedTopInset:34,background:'assets/music/forest.webp',
      currentTrack:null,playing:false,account:'synthetic in-memory only',network:'loopback allowlist; all external requests aborted',
      guestPrompt:fixture.guestPrompt},
    fixtureAdaptations:[{field:'providers.availability.configured',value:true,
      reason:'T09 exposes QR login for all three signed-out providers; no native account operation is invoked.'},
      {field:'guestPrompt',original:originalFixture.guestPrompt,value:fixture.guestPrompt,
        reason:'T10 connected online lists; both sides receive the current guest copy without changing production visuals or the hashed fixture file.'},
      {field:'historical guest prompt',original:fixture.historicalGuestPrompt,value:fixture.guestPrompt,
        reason:'Original historical JSX is captured unchanged. Its panel receives the current T09 copy in a test wrapper so line wrapping is compared with equal content; original copy is asserted before replacement.'}],
    limitations:['No original App, account store, native host, login, audio, player or titlebar imported.',
      'Current library data is injected into mountMusicView/musicPage; this does not prove production library reachability.',
      'Both sides use unliked non-VIP tracks and available, signed-out providers; no account operations exercised.',
      'User desktop screenshots are references only, never an A/B baseline.'],
  };
  const snapshots=new Map();
  const remember=async (side,relative) => {
    const key=`${side}/${relative}`;
    if(snapshots.has(key))return snapshots.get(key).bytes;
    const absolute=path.resolve(side==='historical'?upstream:ui,relative),bytes=await readFile(absolute);
    snapshots.set(key,{absolute,bytes});
    report.sources.push({side,path:relative,sha256:hash(bytes),bytes:bytes.length});
    const destination=path.join(evidence,'sources',side,relative);
    await mkdir(path.dirname(destination),{recursive:true});await writeFile(destination,bytes);
    return bytes;
  };
  let browser,server;
  try {
    const historicalCss=await remember('historical',cssFile);
    const bundle=await buildReference({requireHistory,remember,report});
    await writeFile(path.join(evidence,'reference.js'),bundle);
    await writeFile(path.join(evidence,'fixture-expanded.json'),JSON.stringify(data,null,2)+'\n');
    for(let index=0;index<covers.length;index++)await writeFile(path.join(evidence,`cover-${index+1}.png`),covers[index]);
    const routes=new Map([['/reference.js',{bytes:bundle,type:'text/javascript'}],
      ['/historical.css',{bytes:historicalCss,type:'text/css'}],
      ['/current-entry.js',{bytes:currentEntry,type:'text/javascript'}]]);
    for(const file of ['music.js','music-view.js','music-collections.js','music-settings.js','music-preferences.js','music-preference-values.js','music-player-panel.js','music-background-view.js','music-background/reference/background.js','music-effects.js','music-cover-actions.js',
      'music-icons.js','music-visualizer.js','music.css','reference.css','app.css','assets/music/forest.webp',
      'music-provider-account.js','music-provider-account.css','music-playlist-selection.js','music-playlist-selection.css','music-wallpaper-gallery.js','music-wallpaper-gallery.css',
      'music-online.js','settings-frame.js','dom.js','localization.js',
      'music-background.js','music-font-picker.js','music-font-assets.js']) {
      routes.set('/'+file,{bytes:await remember('current',file),type:file.endsWith('.js')?'text/javascript':file.endsWith('.css')?'text/css':'image/webp'});
    }
    await writeFile(path.join(evidence,'current-entry.js'),currentEntry);
    for(const side of ['historical','current']) {
      const source=html(side,fixture);routes.set('/'+side,{bytes:source,type:'text/html'});
      await writeFile(path.join(evidence,side+'.html'),source);
    }
    const served=[],unknown=[];
    report.requests={served,unknown};
    server=createServer((req,res)=>{
      const pathname=new URL(req.url,'http://localhost').pathname;
      served.push(pathname);
      if(pathname==='/favicon.ico')return res.writeHead(204).end();
      const resource=routes.get(pathname);
      if(!resource){unknown.push(pathname);return res.writeHead(404).end();}
      res.writeHead(200,{'Content-Type':resource.type,'Cache-Control':'no-store'}).end(resource.bytes);
    });
    await new Promise((resolve,reject)=>{server.once('error',reject);server.listen(0,'127.0.0.1',resolve);});
    const origin=`http://127.0.0.1:${server.address().port}`;
    browser=await chromium.launch({headless:true,channel:'msedge'});
    report.browser=browser.version();
    for(const scale of fixture.scales) for(const scenario of ['recommendations','list','guest']) {
      const name=`${scenario}-${Math.round(scale*100)}`,sides={},screens={};
      const row={name,scenario,scale,sides,deltas:null,pixels:{}};
      report.scenarios.push(row);
      for(const side of ['historical','current']) {
        const context=await browser.newContext({viewport:{width:fixture.physicalViewport.width/scale,
          height:fixture.physicalViewport.height/scale},deviceScaleFactor:scale,locale:'zh-CN',timezoneId:'Asia/Shanghai',
          colorScheme:'dark',reducedMotion:'reduce',serviceWorkers:'block'});
        const page=await context.newPage(),errors=[],external=[];
        sides[side]={errors,external,screenshots:[]};screens[side]={};
        page.on('pageerror',error=>errors.push(error.message));
        page.on('console',message=>{if(message.type()==='error')errors.push(message.text());});
        page.on('response',response=>{if(response.status()>=400)errors.push(`${response.status()} ${response.url()}`);});
        await context.route('**/*',route=>{
          const url=route.request().url();
          if(url.startsWith(origin+'/')||url.startsWith('data:'))return route.continue();
          external.push(url);return route.abort();
        });
        await context.addInitScript(({data,scenario})=>{
          window.parityFixture=data;window.parityScenario=scenario;window.parityActions=[];
        },{data,scenario});
        await page.goto(origin+'/'+side);
        await page.locator('[data-music-home-sidebar]').waitFor({timeout:10000}).catch(error=>{
          throw new Error(`${side}/${name} did not mount: ${errors.join('; ')}`,{cause:error});
        });
        const scroller=side==='historical'?'[data-music-home-scroll]':'.MusicContent';
        await page.evaluate(async()=>{
          await document.fonts.ready;
          document.querySelectorAll('img[loading="lazy"]').forEach(img=>img.loading='eager');
          await Promise.all([...document.images].map(img=>img.decode()));
          await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
        });
        const capture=async suffix=>{
          await page.mouse.move(1,1);
          const filename=`${side}-${name}-${suffix}.png`;
          const bytes=await page.screenshot({path:path.join(evidence,filename),animations:'disabled',scale:'device'});
          const png=PNG.sync.read(bytes);
          assert.equal(png.width,1440);assert.equal(png.height,900);
          sides[side].screenshots.push(filename);screens[side][suffix]=bytes;
          return geometry(page,side);
        };
        const initial=await capture('home');sides[side].initial=initial;
        assert.deepEqual(initial.viewport,{width:1440/scale,height:900/scale,dpr:scale,visualScale:1,xl:scale===1});
        assert.equal(initial.horizontalOverflow,false);
        assert.ok(initial.images.every(image=>image.loaded),'All fixture covers and background must decode');
        assert.equal(initial.search.height,42);assert.equal(initial.search.width,480);
        assert.equal(initial.search.font,fixture.fontFamily);
        assert.equal(initial.sidebar.width,208);
        if(scenario!=='guest') {
          assert.equal(initial.rows.length,fixture.tracks.length);
          fixture.tracks.forEach((track,index)=>assert.ok(initial.rows[index].text.includes(track.title)));
          assert.equal(initial.cards.length,scenario==='recommendations'?fixture.recommendations.length:0);
          if(scenario==='list')assert.equal(initial.sidebarPlaylists[0].active,true,'Same selected collection on both sides');
          if(scenario==='recommendations') {
            fixture.recommendations.forEach((item,index)=>assert.ok(initial.cards[index].text.includes(item.title)));
            await page.locator(scroller).evaluate(element=>{
              const grids=[...element.querySelectorAll('[data-music-home-recommendation-playlists] div')]
                .filter(item=>getComputedStyle(item).display==='grid');
              element.scrollTop+=grids[1].getBoundingClientRect().top-element.getBoundingClientRect().top-40;
            });
            sides[side].supplyView=await capture('supply');
          }
          await page.locator(scroller).evaluate(element=>{
            const head=element.querySelector('[data-music-home-track-list]');
            element.scrollTop+=head.getBoundingClientRect().top-element.getBoundingClientRect().top;
          });
          sides[side].tableView=await capture('table');
          if(scenario==='list') {
            await page.locator(scroller).evaluate(element=>element.scrollTop=0);
            await page.locator('[data-music-home-toolbar] button[aria-expanded]').click();
            await page.getByRole('textbox',{name:'\u641c\u7d22\u5f53\u524d\u5217\u8868',exact:true}).fill(fixture.filterQuery);
            await page.waitForFunction(()=>document.querySelectorAll('[data-music-home-track-list] [role="row"]').length===3);
            sides[side].filtered=await capture('filtered');
            assert.equal(sides[side].filtered.rows.length,2);
            await page.getByRole('button',{name:'\u5173\u95ed\u5217\u8868\u641c\u7d22',exact:true}).click();
            await page.waitForFunction(()=>document.querySelectorAll('[data-music-home-track-list] [role="row"]').length===13);
            await page.getByRole('button',{name:'\u64ad\u653e '+fixture.tracks[0].title,exact:true}).click();
            sides[side].actions=await page.evaluate(()=>window.parityActions);
            assert.equal(sides[side].actions.at(-1).action,'playNow');
          }
        } else {
          assert.equal(initial.rows.length,0);assert.equal(initial.cards.length,0);
          assert.equal(initial.guestButtons.length,3);
          assert.ok(initial.guestButtons.every(button=>!button.disabled),'T09 signed-out providers expose the scan-login entry');
          assert.equal(initial.guestTitle.text,fixture.guestTitle);
          assert.equal(initial.guestPrompt.text,fixture.guestPrompt,'Both guest layouts receive identical explicit fixture copy');
        }
        assert.deepEqual(errors,[],`${side}/${name} browser errors`);
        assert.deepEqual(external,[],`${side}/${name} unexpected external requests`);
        await context.close();
      }
      row.deltas=deltas(sides.historical.initial,sides.current.initial);
      if(sides.historical.tableView)row.tableDeltas=deltas(sides.historical.tableView,sides.current.tableView);
      if(sides.historical.filtered)row.filteredDeltas=deltas(sides.historical.filtered,sides.current.filtered);
      for(const suffix of Object.keys(screens.historical))row.pixels[suffix]=await comparisonImages(PNG,screens.historical[suffix],screens.current[suffix],`${name}-${suffix}`);
      row.geometryChecks=[];
      for(const view of ['initial','supplyView','tableView','filtered']) {
        if(sides.historical[view] && sides.current[view])
          row.geometryChecks.push(compareStableGeometry(sides.historical[view],sides.current[view],view));
      }
      console.log(`${name}: historical/current captured; header=${sides.historical.initial.header?.height ?? 'n/a'}/${sides.current.initial.header?.height ?? 'n/a'}, row=${sides.historical.initial.rows[0]?.height ?? 'n/a'}/${sides.current.initial.rows[0]?.height ?? 'n/a'}`);
    }
    assert.deepEqual(unknown,[],'HTTP only serves the captured allowlist');
    report.served=[...new Set(served)];
    report.sourceDrift=[];
    for(const [key,{absolute,bytes}] of snapshots)if(hash(await readFile(absolute))!==hash(bytes))report.sourceDrift.push(key);
    assert.deepEqual(report.sourceDrift,[],'Sources changed during evidence run; rerun required');
    const checks=report.scenarios.flatMap(scenario=>scenario.geometryChecks);
    report.geometrySummary={toleranceCssPx:geometryTolerance,comparedViews:checks.length,
      comparedFields:checks.reduce((sum,check)=>sum+check.comparedFields,0),
      maxDeltaCssPx:Math.max(...checks.map(check=>check.maxDeltaCssPx)),
      failures:report.scenarios.flatMap(scenario=>scenario.geometryChecks.flatMap(check=>
        check.failures.map(failure=>({scenario:scenario.name,view:check.view,...failure}))))};
    assert.deepEqual(report.geometrySummary.failures,[],
      `Historical/current shared geometry differs by more than ${geometryTolerance} CSS px`);
    report.status='captured';
  } catch(error) {
    report.status='blocked';report.blocker={message:error.message,stack:error.stack};throw error;
  } finally {
    await browser?.close();
    if(server?.listening)await new Promise(resolve=>server.close(resolve));
    report.serverClosed=!server?.listening;
    report.finishedAt=new Date().toISOString();
    await writeFile(path.join(evidence,'report.json'),JSON.stringify(report,null,2)+'\n');
  }
});
