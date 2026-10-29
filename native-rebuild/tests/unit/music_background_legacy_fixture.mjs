import {readFile,mkdir,mkdtemp,writeFile} from 'node:fs/promises';
import {createRequire} from 'node:module';
import {createHash} from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

// Only the historical pure crypto/file class is loaded. No app bootstrap or user configuration.
const history=path.resolve(process.argv[2]||'H:/refactor/history/lume1.0.1');
const root=path.resolve(fileURLToPath(new URL('../../..',import.meta.url)));
const output=path.join(root,'build-artifacts/native-rebuild');
await mkdir(output,{recursive:true});
const directory=await mkdtemp(path.join(output,'music-background-legacy-fixture-'));
const source=path.join(history,'src/vscode-windhawk/src/utils/wallpaper/encryptedWallpaperFile.ts');
const require=createRequire(import.meta.url);
const ts=require(path.join(history,'src/vscode-windhawk/node_modules/typescript'));
const text=await readFile(source,'utf8');
const compiled=ts.transpileModule(text,{compilerOptions:{target:ts.ScriptTarget.ES2022,module:ts.ModuleKind.ES2022}}).outputText;
const {EncryptedWallpaperFile}=await import('data:text/javascript;base64,'+Buffer.from(compiled).toString('base64'));
const key=Buffer.from(Array.from({length:32},(_,i)=>i+1));
const plain=Buffer.from(Array.from({length:300017},(_,i)=>65+i%23));
for(const sealed of [true,false]) {
  const file=await EncryptedWallpaperFile.create(path.join(directory,sealed?'sealed.apk':'unsealed.apk'),key);
  try {
    await file.append(plain.subarray(0,3));
    await file.append(plain.subarray(3,100006));
    await file.append(plain.subarray(100006));
    if(sealed)await file.seal();
  } finally {await file.close();}
}
const verified=await EncryptedWallpaperFile.open(path.join(directory,'sealed.apk'),key);
const digest=createHash('sha256');
try {for await(const chunk of verified.createReadStream())digest.update(chunk);}
finally {await verified.close();}
if(digest.digest('hex')!==createHash('sha256').update(plain).digest('hex'))throw Error('历史样本校验失败');
await writeFile(path.join(directory,'provenance.json'),JSON.stringify({
  source,sha256:createHash('sha256').update(text).digest('hex'),plainBytes:plain.length,
  records:[3,100003,200011],key:'仅测试：字节 1 至 32；不包含用户密钥'
},null,2));
key.fill(0);plain.fill(0);
console.log(directory);
