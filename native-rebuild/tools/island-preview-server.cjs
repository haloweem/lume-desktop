const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const {execFile} = require('node:child_process');

const root = path.resolve(__dirname, '../..');
const ui = path.join(root, 'native-rebuild/ui');
const artifacts = path.join(root, 'build-artifacts');
const cache = path.join(artifacts, 'island-preview-server');
const renderer = path.join(artifacts, 'native-rebuild/release/lume-island-preview-demo.exe');
const jobs = new Map();
const choices = {
  shape: ['standard', 'capsule'],
  size: ['small', 'standard', 'large'],
  mode: ['compact', 'expanded'],
  material: ['none', 'liquid-glass', 'frosted-glass'],
};
const mime = {'.html':'text/html; charset=utf-8','.css':'text/css; charset=utf-8',
  '.js':'text/javascript; charset=utf-8','.svg':'image/svg+xml','.png':'image/png',
  '.jpg':'image/jpeg','.webp':'image/webp'};

function valid(params, names) {
  return names.every(name => choices[name].includes(params.get(name)));
}
function frames(params, transition) {
  const names = transition
    ? ['fromShape','fromSize','fromMode','shape','size','mode','material']
    : ['shape','size','mode','material'];
  const values = names.map(name => params.get(name));
  const key = values.join('_');
  const folder = path.join(cache, transition ? 'transitions' : 'static', key);
  if (fs.existsSync(path.join(folder, 'count.txt'))) return Promise.resolve(folder);
  if (!jobs.has(folder)) {
    jobs.set(folder, new Promise((resolve, reject) => {
      fs.mkdirSync(folder, {recursive:true});
      const args = transition
        ? [folder, ...values]
        : [folder, values[0], values[1], values[2], values[0], values[1], values[2], values[3]];
      execFile(renderer, args, {windowsHide:true, timeout:15000}, error => error ? reject(error) : resolve());
    }).finally(() => jobs.delete(folder)));
  }
  return jobs.get(folder).then(() => folder);
}
function sendFile(response, file, type) {
  fs.readFile(file, (error, bytes) => {
    response.writeHead(error ? 404 : 200, {'Content-Type':type, 'Cache-Control':'no-store'});
    response.end(error ? 'Not found' : bytes);
  });
}

const server = http.createServer(async (request, response) => {
  let url;
  try { url = new URL(request.url, 'http://127.0.0.1'); }
  catch { response.writeHead(400).end(); return; }
  let pathname;
  try { pathname = decodeURIComponent(url.pathname); }
  catch { response.writeHead(400).end(); return; }
  if (pathname === '/__preview/native') {
    if (!valid(url.searchParams, ['shape','size','mode','material'])) {
      response.writeHead(400).end(); return;
    }
    try {
      const folder = await frames(url.searchParams, false);
      sendFile(response, path.join(folder, '0.png'), 'image/png');
    } catch { response.writeHead(500).end('Could not render native preview'); }
    return;
  }
  if (pathname === '/__preview/transition') {
    const names = ['shape','size','mode','material'];
    if (!valid(url.searchParams, names) ||
        !['standard','capsule'].includes(url.searchParams.get('fromShape')) ||
        !choices.size.includes(url.searchParams.get('fromSize')) ||
        !choices.mode.includes(url.searchParams.get('fromMode'))) {
      response.writeHead(400).end(); return;
    }
    try {
      const folder = await frames(url.searchParams, true);
      const count = Number(fs.readFileSync(path.join(folder, 'count.txt'), 'utf8'));
      if (!Number.isInteger(count) || count < 1 || count > 90) throw Error('Invalid frame count');
      const key = path.basename(folder);
      response.writeHead(200, {'Content-Type':'application/json', 'Cache-Control':'no-store'});
      response.end(JSON.stringify({frames:Array.from({length:count}, (_, index) =>
        `/__preview/transition-frame/${key}/${index}.png`)}));
    } catch { response.writeHead(500).end('Could not render native transition'); }
    return;
  }
  if (pathname.startsWith('/__preview/transition-frame/')) {
    const suffix = pathname.slice('/__preview/transition-frame/'.length);
    if (!/^[a-z_-]+\/[0-9]{1,2}\.png$/.test(suffix)) { response.writeHead(400).end(); return; }
    sendFile(response, path.join(cache, 'transitions', suffix), 'image/png');
    return;
  }
  const relative = pathname === '/' ? 'dynamic-island-preview.html' : pathname.slice(1);
  const file = path.resolve(ui, relative);
  if (!file.startsWith(ui + path.sep)) { response.writeHead(403).end(); return; }
  sendFile(response, file, mime[path.extname(file)] || 'application/octet-stream');
});
const port = Number(process.env.LUME_ISLAND_PREVIEW_PORT || 8876);
server.listen(port, '127.0.0.1');
