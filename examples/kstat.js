/*
 * A simple demonstration of the kstat reader that (if given no argument) reads
 * every kstat and prints module, class, module, name and instance.  If an
 * argument is provided, only kstats that have the specified class, module,
 * name and/or instance will be displayed.  If the "-v" option is specified,
 * all fields in the named kstat (and their values) will be printed.
 */
var kstat = require('bindings')('kstat');

var options = { c: 'class', n: 'name', m: 'module', i: 'instance', v: true };
var stats = {};
var verbose = false;
var i;

for (i = 2; i < process.argv.length; i++) {
	var arg = process.argv[i];
	var opt = arg.charAt(1);

	if (arg.charAt(0) != '-' || !opt || !options[opt]) {
		console.log('invalid option "' + arg + '"');
		process.exit(1);
	}

	if (opt == 'v') {
		verbose = true;
		continue;
	}

	if (!(arg = process.argv[++i])) {
		console.log('expected argument for option "' + opt + '"');
		process.exit(1);
	}

	stats[options[opt]] = arg;
}

if (stats.hasOwnProperty('instance'))
	stats.instance = parseInt(stats.instance, 10);

var fixed = function (str, len) {
	var rval = str, j;

	for (j = 0; j < len - str.length; j++)
		rval += ' ';

	return (rval);
};

var reader = new kstat.Reader(stats);

var data = reader.read();

var fields = {
	module: 20,
	'class': 20,
	name: 30,
	instance: 8
};

var header = function () {
	var str = '';

	for (f in fields)
		str += fixed(f.toUpperCase(), fields[f]);
	console.log(str);
};

header();

data.sort(function (l, r) {
	for (f in fields) {
		if (f == 'instance')
			return (l[f] - r[f]);

		var rval = l[f].localeCompare(r[f]);

		if (rval)
			return (rval);
	}

	return (0);
});

for (i = 0; i < data.length; i++) {
	var s = '', f;

	for (f in fields)
		s += fixed(data[i][f], fields[f] + '');

	console.log(s);

	if (!verbose || !data[i].data)
		continue;

	console.log('|\n+--> ' + fixed('FIELD', 35) + 'VALUE');

	for (f in data[i].data)
		console.log('     ' + fixed(f, 35) + data[i].data[f] + '');

	if (i < data.length - 1) {
		console.log('');
		header();
	}
}
