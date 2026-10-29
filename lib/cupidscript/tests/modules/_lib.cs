// helper module for module caching tests
exports.count = 0;
exports.inc = fn() {
  exports.count = exports.count + 1;
  return exports.count;
};
