// Build-time extraction only. React and Lucide are never shipped as runtimes.
import {createRequire} from 'node:module';
import {mkdirSync, writeFileSync, copyFileSync} from 'node:fs';
import {fileURLToPath} from 'node:url';
import path from 'node:path';

const reference = path.resolve(process.argv[2]);
const require = createRequire(path.join(reference, 'package.json'));
const React = require('react');
const {renderToStaticMarkup} = require('react-dom/server');
const lucide = require('lucide-react');
const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const names = {focus:'Focus',pin:'Pin',down:'ChevronDown',check:'Check',home:'House', discover:'Compass', heart:'Heart', recent:'Clock3',
  queue:'ListMusic', search:'Search', play:'Play', pause:'Pause', previous:'SkipBack',
  next:'SkipForward', close:'X', upload:'Upload', user:'User', plus:'Plus',
  forward:'ChevronRight', back:'ChevronLeft', repeat:'Repeat', repeatOne:'Repeat1',
  ordered:'ListOrdered', lyrics:'AlignLeft', disc:'Disc3', remove:'Trash2', reset:'RotateCcw',
  settings:'Settings2', sliders:'SlidersHorizontal', panelDisc:'Disc',logout:'LogOut',loader:'LoaderCircle',
  volume:'Volume2',muted:'VolumeX',shuffle:'Shuffle'};
const icons = Object.fromEntries(Object.entries(names).map(([key, name]) => [key,
  renderToStaticMarkup(React.createElement(lucide[name], {className:'MusicIcon', 'aria-hidden':true}))]));
writeFileSync(path.join(ui, 'music-icons.js'),
  '// Generated from the reference Lucide icons; see assets/music/SOURCES.md.\nexport const MUSIC_ICONS = ' + JSON.stringify(icons, null, 2) + ';\n');
if (!process.argv.includes('--icons-only')) {
  const assets = path.join(ui, 'assets/music');
  mkdirSync(assets, {recursive:true});
  copyFileSync(path.join(reference, 'node_modules/lucide-react/LICENSE'), path.join(assets, 'lucide-LICENSE.txt'));
}
