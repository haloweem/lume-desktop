import fs from 'node:fs';
import assert from 'node:assert/strict';
const [referencePath,nativePath]=process.argv.slice(2);
assert(referencePath&&nativePath,'Pass reference and native agenda JSON paths');
const reference=JSON.parse(fs.readFileSync(referencePath)),native=JSON.parse(fs.readFileSync(nativePath));
let checked=0,maxError=0;
for(const sample of native){
  const expected=reference.results.find(row=>row.theme===sample.theme&&row.width===sample.width&&row.height===sample.height);
  assert(expected,'reference size and theme missing');
  for(const [selector,actual] of Object.entries(sample.rects)){
    const rect=expected.agenda.selectors[selector][0];
    const values=[rect.x,rect.y,rect.width,rect.height];
    for(let i=0;i<4;i++){
      const error=Math.abs(actual[i]-values[i]);maxError=Math.max(maxError,error);checked++;
      assert(error<=.05,`${sample.theme} ${sample.width}x${sample.height} ${selector}[${i}]: ${actual[i]} vs ${values[i]}`);
    }
  }
}
assert.equal(native.length,14);
console.log(JSON.stringify({result:'CALENDAR_TODO_LAYOUT_PASS',layouts:native.length,values:checked,maxErrorDIP:maxError}));
