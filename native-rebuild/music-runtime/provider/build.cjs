'use strict';

// Keep the runtime package's CommonJS build entry while esbuild configuration uses ESM.
import('./build.mjs').catch(error => { console.error(error); process.exitCode = 1; });
