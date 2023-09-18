/*
 * filter.js: tests filtering in both the Reader constructor and read().
 */

var kstat = require('bindings')('kstat');
var test = require('tape');

function kstatToIdent(stat)
{
	return ({
	    'module': stat['module'],
	    'name': stat['name'],
	    'class': stat['class'],
	    'instance': stat['instance']
	});
}

function identCompare(k1, k2)
{
	var key1 = k1['module'] + '.' + k1['name'] + '.' + k1['class'] + '.' +
	    k1['instance'];
	var key2 = k2['module'] + '.' + k2['name'] + '.' + k2['class'] + '.' +
	    k2['instance'];
	if (key1 < key2)
		return (-1);
	if (key1 > key2)
		return (1);
	return (0);
}

var all_reader = new kstat.Reader({});
var cpu_reader = new kstat.Reader({ 'module': 'cpu' });
var ncpustats = 0;
var cpustats, allstats;

test('constructor-based filtering', function (t) {
	allstats = all_reader.read();
	t.assert(allstats.length > 0);
	t.comment(allstats.length + ' total stats');

	cpustats = cpu_reader.read();
	t.assert(cpustats.length > 0);
	t.assert(allstats.length > cpustats.length);
	t.comment(cpustats.length + ' cpu stats');

	allstats.forEach(function (stat) {
		if (stat['module'] == 'cpu')
			ncpustats++;
	});

	t.assert(cpustats.length == ncpustats);
	t.end();
});

test('read-based filtering', function (t) {
	var filteredstats = all_reader.read({ 'module': 'cpu' });
	t.equal(filteredstats.length, ncpustats);
	t.deepEqual(filteredstats.map(kstatToIdent).sort(identCompare),
	    cpustats.map(kstatToIdent).sort(identCompare));

	filteredstats = all_reader.read({ 'instance': 1 });
	t.assert(filteredstats.length < allstats.length);
	t.assert(filteredstats.length > 0);
	filteredstats.forEach(
	    function (stat) { t.equal(stat['instance'], 1); });
	t.comment(filteredstats.length + ' stats with instance == 1');

	filteredstats = all_reader.read({ 'class': 'net' });
	t.assert(filteredstats.length < allstats.length);
	t.assert(filteredstats.length > 0);
	filteredstats.forEach(
	    function (stat) { t.equal(stat['class'], 'net'); });
	t.comment(filteredstats.length + ' stats with class == "net"');

	filteredstats = all_reader.read({ 'name': 'intrstat' });
	t.assert(filteredstats.length < allstats.length);
	t.assert(filteredstats.length > 0);
	filteredstats.forEach(
	    function (stat) { t.equal(stat['name'], 'intrstat'); });
	t.comment(filteredstats.length + ' stats with name == "intrstat"');
	t.end();
});

test('both kinds of filtering', function (t) {
	var filteredstats = cpu_reader.read({ 'instance': 0 });
	t.assert(filteredstats.length < cpustats.length);
	t.assert(filteredstats.length > 0);
	filteredstats.forEach(
	    function (stat) { t.assert(stat['instance'] === 0); });
	t.comment(filteredstats.length + ' cpu stats with instance === 0');
	t.end();
});
