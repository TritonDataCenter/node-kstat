/*
 * badops.js: tests invalid operations
 */

var assert = require('assert');
var kstat = require('bindings')('kstat');

var reader = new kstat.Reader({ 'module': 'memory_cap' });
console.log(reader.read());

reader.close();
assert.throws(function () { reader.read(); }, /already been closed/);
assert.throws(function () { reader.close(); }, /already been closed/);
console.log('test passed');
