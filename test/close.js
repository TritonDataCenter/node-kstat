var kstat = require('kstat');
var test = require('tape');

test('basic close', function (t) {
	var reader = new kstat.Reader({ 'module': 'memory_cap' });
	reader.read();
	reader.close();
	t.pass('called close successfully');
	t.end();
});

test('spurious read', function (t) {
	var reader = new kstat.Reader({ 'module': 'memory_cap' });
	reader.close();

	t.throws(function () { reader.read(); }, /already been closed/);
	t.end();
});

test('spurious close', function (t) {
	var reader = new kstat.Reader({ 'module': 'memory_cap' });
	reader.close();

	t.throws(function () { reader.close(); }, /already been closed/);
	t.end();
});
