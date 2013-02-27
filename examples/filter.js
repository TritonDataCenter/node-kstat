/*
 * filter.js: tests filtering in both the Reader constructor and read().
 */

var assert = require('assert');
var kstat = require('kstat');

function kstatToIdent(kstat)
{
	return ({
	    'module': kstat['module'],
	    'name': kstat['name'],
	    'class': kstat['class'],
	    'instance': kstat['instance'],
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

/*
 * Test constructor-based filtering.
 */
var allstats = all_reader.read();
assert.ok(allstats.length > 0);
console.log('%d total stats', allstats.length);

var cpustats = cpu_reader.read();
assert.ok(cpustats.length > 0);
assert.ok(allstats.length > cpustats.length);
console.log('%d cpu stats', cpustats.length);

var ncpustats = 0;
allstats.forEach(function (kstat) {
	if (kstat['module'] == 'cpu')
		ncpustats++;
});

assert.ok(cpustats.length == ncpustats);


/*
 * Test read-based filtering.
 */
var filteredstats = all_reader.read({ 'module': 'cpu' });
assert.equal(filteredstats.length, ncpustats);
assert.deepEqual(filteredstats.map(kstatToIdent).sort(identCompare),
    cpustats.map(kstatToIdent).sort(identCompare));

filteredstats = all_reader.read({ 'instance': 1 });
assert.ok(filteredstats.length < allstats.length);
assert.ok(filteredstats.length > 0);
filteredstats.forEach(
    function (kstat) { assert.equal(kstat['instance'], 1); });
console.log('%d stats with instance == 1', filteredstats.length);

filteredstats = all_reader.read({ 'class': 'net' });
assert.ok(filteredstats.length < allstats.length);
assert.ok(filteredstats.length > 0);
filteredstats.forEach(
    function (kstat) { assert.equal(kstat['class'], 'net'); });
console.log('%d stats with class == "net"', filteredstats.length);

filteredstats = all_reader.read({ 'name': 'intrstat' });
assert.ok(filteredstats.length < allstats.length);
assert.ok(filteredstats.length > 0);
filteredstats.forEach(
    function (kstat) { assert.equal(kstat['name'], 'intrstat'); });
console.log('%d stats with name == "intrstat"', filteredstats.length);

/*
 * Test both kinds of filtering at once.
 */
filteredstats = cpu_reader.read({ 'instance': 0 });
assert.ok(filteredstats.length < cpustats.length);
assert.ok(filteredstats.length > 0);
filteredstats.forEach(
    function (kstat) { assert.ok(kstat['instance'] === 0); });
console.log('%d cpu stats with instance === 0', filteredstats.length);

console.log('test passed');
