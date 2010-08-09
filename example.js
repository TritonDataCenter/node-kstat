/*
 * A simple demonstration of the kstat reader that reads every kstat and
 * prints module, class, name and instance.
 */

var sys = require('sys');
var kstat = require('kstat');

var fixed = function (str, len) {
	var rval = str, i;

	for (i = 0; i < len - str.length; i++)
		rval += ' ';

	return (rval);
};

var reader = new kstat.Reader();

var data = reader.read();

var fields = {
	module: 20,
	'class': 20,
	name: 30,
	instance: 8
};

var str = '';

for (f in fields)
	str += fixed(f.toUpperCase(), fields[f]);

sys.puts(str);

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
}
