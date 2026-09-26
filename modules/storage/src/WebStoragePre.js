// SPDX-License-Identifier: MS-PL
// General XNA StorageDevice backing store for Emscripten applications.
// This runs before main() and holds a run dependency until previously saved
// files are visible. IDBFS autoPersist flushes subsequent closes and deletes.
if (!Module.preRun) Module.preRun = [];
else if (!Array.isArray(Module.preRun)) Module.preRun = [Module.preRun];
Module.preRun.push(function () {
  const dependency = 'cna-storage-idbfs';
  Module.cnaStorageReady = false;
  addRunDependency(dependency);
  try {
    if (!FS.analyzePath('/cna-storage').exists) FS.mkdir('/cna-storage');
    // Emscripten's Node runner has no IndexedDB; keep its existing process-local
    // filesystem behavior instead of making the XNA storage API unavailable there.
    if (typeof process !== 'undefined' && process.versions?.node &&
        typeof indexedDB === 'undefined') {
      Module.cnaStorageReady = true;
      removeRunDependency(dependency);
      return;
    }
    FS.mount(IDBFS, {autoPersist: true}, '/cna-storage');
    FS.syncfs(true, function (error) {
      if (error) console.error('CNA StorageDevice IDBFS restore failed:', error);
      else Module.cnaStorageReady = true;
      removeRunDependency(dependency);
    });
  } catch (error) {
    console.error('CNA StorageDevice IDBFS mount failed:', error);
    removeRunDependency(dependency);
  }
});
