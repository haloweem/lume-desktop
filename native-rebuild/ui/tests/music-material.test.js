import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import {LIQUID_GLASS_STUDIO_PRESET,resolveLiquidGlassShapeRadiusFactor,
  resolveLiquidGlassRenderScale} from '../music-material/reference/material.js';
import {registerMusicMaterialSceneSource,musicMaterialSceneSource} from '../music-material/registry.js';

test('producer-only registry has no imports and stale cleanup cannot remove a new registration',()=>{
  const source=readFileSync(new URL('../music-material/registry.js',import.meta.url),'utf8');
  assert.doesNotMatch(source,/^\s*import\s/m);
  const canvas={getContext(){}},refresh=()=>{};
  const first=registerMusicMaterialSceneSource(canvas,{refresh:()=>{}});
  const second=registerMusicMaterialSceneSource(canvas,{refresh});
  first();assert.equal(musicMaterialSceneSource(canvas).refresh,refresh);
  second();second();assert.equal(musicMaterialSceneSource(canvas),undefined);
});

test('material keeps original whole-bar optical preset and source hashes',()=>{
  assert.equal(LIQUID_GLASS_STUDIO_PRESET.refThickness,20);
  assert.equal(LIQUID_GLASS_STUDIO_PRESET.refFactor,1.4);
  assert.equal(LIQUID_GLASS_STUDIO_PRESET.refDispersion,14);
  assert.equal(LIQUID_GLASS_STUDIO_PRESET.shapeRoundness,2);
  assert.equal(resolveLiquidGlassShapeRadiusFactor(112),1);
  assert.equal(resolveLiquidGlassRenderScale(2),1.5);
  const base=new URL('../music-material/reference/',import.meta.url);
  const manifest=JSON.parse(readFileSync(new URL('manifest.json',base)));
  for(const [file,hash] of Object.entries(manifest.generated))
    assert.equal(createHash('sha256').update(readFileSync(new URL(file,base))).digest('hex'),hash,file);
  const control=readFileSync(new URL('source/upstream/src/components/FloatingPlayerControls.tsx',base),'utf8');
  assert.match(control,/h-\[clamp\(104px,6\.2vw,112px\)\]/);
  assert.match(control,/w-\[clamp\(980px,68vw,1240px\)\]/);
  assert.match(control,/<LiquidGlassControlSurface\s+expanded/);
  const bundle=readFileSync(new URL('material.js',base),'utf8');
  assert.doesNotMatch(bundle,/from ["'](?:react|three|.*stores|.*account)/);
});
