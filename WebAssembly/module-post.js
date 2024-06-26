// Module.ready resolves when WASM instantiates. (ready is now a property and not function via @surma fork)
Module.ready = new Promise(function(resolve, reject) {
  addOnInit(function() {
    resolve("WebAssembly has been initialised.");
  });

  // Propagate error to Module.ready.catch()
  // WARNING: this is a hack based Emscripten's current abort() implementation
  // and could break in the future.
  // Rewrite existing abort(what) function to reject Promise before it executes.
  var origAbort = this.abort;
  this.abort = function(what) {
    reject(Error(what));
    origAbort.call(this, what);
  }
});
