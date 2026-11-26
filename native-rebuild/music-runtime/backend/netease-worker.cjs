'use strict';

const os = require('node:os');
const path = require('node:path');
const { workerData } = require('node:worker_threads');
const { installPrivateListeners } = require('./private-listener.cjs');

// Keep Folia's complete worker/request pipeline, with a private listener and an app-owned cache.
if (!workerData || typeof workerData.vendorRoot !== 'string' || !path.isAbsolute(workerData.vendorRoot) ||
    typeof workerData.tempRoot !== 'string' || !path.isAbsolute(workerData.tempRoot))
    throw new Error('Invalid NetEase worker configuration');
os.tmpdir = () => workerData.tempRoot;
installPrivateListeners({ token: workerData.token });
require(path.join(workerData.vendorRoot, 'electron', 'netease', 'neteaseApiWorker.cjs'));
