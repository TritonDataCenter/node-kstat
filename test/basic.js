var kstat = require('bindings')('kstat');
var test = require('tape');

var check = function (t, results)
{
	var bymod = {};
	var i;

	t.assert(results instanceof Array);
	t.assert(results.length > 1);

	for (i = 0; i < results.length; i++) {
		if (results[i].class == 'mib2')
			bymod[results[i].module] = results[i];
	}

	t.assert(bymod.tcp);
	t.assert(bymod.tcp.hasOwnProperty('class'));
	t.assert(bymod.tcp.class == 'mib2');
	t.assert(bymod.tcp.hasOwnProperty('instance'));
	t.assert(bymod.tcp.hasOwnProperty('name'));
	t.assert(bymod.tcp.name == 'tcp');

	t.assert(bymod.tcp.hasOwnProperty('snaptime'));
	t.assert(bymod.tcp.hasOwnProperty('crtime'));

	t.assert(bymod.tcp.hasOwnProperty('data'));
	t.assert(bymod.tcp.data.rtoMax > 0);
	t.assert(bymod.tcp.data.hasOwnProperty('listenDrop'));

	t.assert(bymod.udp);
	t.assert(bymod.udp.hasOwnProperty('data'));
	t.assert(bymod.udp.data.hasOwnProperty('inDatagrams'));
	t.end();
};

test('basic open', function (t) {
	var reader = new kstat.Reader();
	var results = reader.read();

	check(t, results);
});

test('unspecified open', function (t) {
	var reader = new kstat.Reader({});
	var results = reader.read();

	check(t, results);
});

test('failed open', function (t) {
	var f = function (arg) { if (arg) t.assert(false); };

	t.throws(function () { f(new kstat.Reader(undefined)); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader('fooey')); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader({ 'class': 1234 })); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader({ 'module': -1 })); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader({ 'name': false })); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader({ 'instance': 'doogle' })); },
	    /illegal kstat specifier/);
	t.throws(function () { f(new kstat.Reader({ 'class': 'foo' }, 123)); },
	    /illegal kstat specifier/);
	t.end();
});

test('filtered open', function (t) {
	var reader = new kstat.Reader({ 'class': 'mib2' });
	var results = reader.read();

	check(t, results);
});

test('overly filtered open', function (t) {
	var checkempty = function (spec) {
		var reader = new kstat.Reader(spec);
		var results = reader.read();
		t.assert(results instanceof Array);
		t.assert(results.length === 0);
	};

	checkempty({ class: 'doogle' });
	checkempty({ module: 'bagnoogle' });
	checkempty({ name: 'thesack' });
	checkempty({ instance: 123724800 });
	t.end();
});

test('failed read', function (t) {
	var reader = new kstat.Reader();

	t.throws(function () { reader.read(undefined); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read('fooey'); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read({ 'class': 1234 }); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read({ 'module': -1 }); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read({ 'name': false }); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read({ 'instance': 'doogle' }); },
	    /illegal kstat specifier/);
	t.throws(function () { reader.read({ 'class': 'blech' }, 123); },
	    /illegal kstat specifier/);
	t.end();
});

test('filtered read', function (t) {
	var reader = new kstat.Reader();
	var results = reader.read({ 'class': 'mib2' });

	check(t, results);
});

test('overly filtered read', function (t) {
	var checkempty = function (spec) {
		var reader = new kstat.Reader();
		var results = reader.read(spec);
		t.assert(results instanceof Array);
		t.assert(results.length === 0);
	};

	checkempty({ class: 'doogle' });
	checkempty({ module: 'bagnoogle' });
	checkempty({ name: 'thesack' });
	checkempty({ instance: 123724800 });
	t.end();
});
