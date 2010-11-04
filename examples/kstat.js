/*
 * A simple demonstration of the kstat reader that (if given no argument) reads
 * every kstat and prints module, class, module, name and instance.  If an
 * argument is provided, only kstats that have the specified class, module,
 * name and/or instance will be displayed.  If the "-v" option is specified,
 * all fields in the named kstat (and their values) will be printed.
 */
var sys = require('sys');
var kstat = require('kstat');

var options = { c: 'class', n: 'name', m: 'module', i: 'instance', v: true };
var stats = {};
var verbose = false;

for (i = 2; i < process.argv.length; i++) {
	var arg = process.argv[i];
	var opt = arg.charAt(1);

	if (arg.charAt(0) != '-' || !opt || !options[opt]) {
		sys.puts('invalid option "' + arg + '"');
		process.exit(1);
        }

	if (opt == 'v') {
		verbose = true;
		continue;
	}

	if (!(arg = process.argv[++i])) {
		sys.puts('expected argument for option "' + opt + '"');
		process.exit(1);
	}

	stats[options[opt]] = arg;
}

if (stats.hasOwnProperty('instance'))
	stats.instance = parseInt(stats.instance, 10);

var fixed = function (str, len) {
	var rval = str, i;

	for (i = 0; i < len - str.length; i++)
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
	sys.puts(str);
}

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
	str = '';

	for (f in fields)
		str += fixed(data[i][f], fields[f] + '');

	sys.puts(str);

	if (!verbose || !data[i].data)
		continue;

	sys.puts('|\n+--> ' + fixed('FIELD', 35) + 'VALUE');

	for (f in data[i].data)
		sys.puts('     ' + fixed(f, 35) + data[i].data[f] + '');

	if (i < data.length - 1) {
		sys.puts('');
		header();
	}
}
