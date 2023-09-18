/*
 * A node.js implementation of the venerable Solaris command, mpstat(1).
 * Note that (1) we implement user time, system time and idle time as
 * nanoseconds not ticks (making it more accurate than mpstat) and (2) wait
 * time is always zero (operating under the principle that if you can't say
 * something accurate, you shouldn't say anything at all).
 */

var kstat = require('bindings')('kstat');

var fields = {
	CPU: { sys: { value: function (s) { return (s.instance); } } },
	minf: { vm: { value: [ 'as_fault', 'hat_fault' ] } },
	mjf: { vm: { value: 'maj_fault' } },
	xcal: { sys: { value: 'xcalls' } },
	intr: { sys: { value: 'intr', width: 5 } },
	ithr: { sys: { value: 'intrthread' } },
	csw: { sys: { value: 'pswitch', width: 4 } },
	icsw: { sys: { value: 'inv_swtch' } },
	migr: { sys: { value: 'cpumigrate' } },
	smtx: { sys: { value: 'mutex_adenters' } },
	srw: { sys: { value: [ 'rw_rdfails', 'rw_wrfails' ], width: 4 } },
	syscl: { sys: { value: 'syscall' } },
	usr: { sys: { value: 'cpu_nsec_user', time: true } },
	sys: { sys: { value: 'cpu_nsec_kernel', time: true } },
	wt: { sys: { value: function () { return (0); }, width: 3 } },
	idl: { sys: { value: 'cpu_nsec_idle', time: true } }
};

var reader = {};
var field;

for (field in fields) {
	var stat;

	for (stat in fields[field]) {
		if (reader[stat])
			continue;

		reader[stat] = new kstat.Reader({ module: 'cpu',
		    'class': 'misc', name: stat });
	}
}

var pad = function (str, len) {
	var rval = '', i;

	for (i = 0; i < len - str.length; i++)
		rval += ' ';

	rval += str;

	return (rval);
};

var outputheader = function ()
{
	var f, s, w;
	var str = '';

	for (f in fields) {
		for (s in fields[f]) {
			if ((w = fields[f][s].width) !== 0)
				break;
		}

		if (str.length > 0)
			str += ' ';

		str += pad(f, w ? w : f.length);
	}

	console.log(str);
};

var outputcpu = function (now, last)
{
	var f, s;
	var line = '', i;
	var value;

	for (f in fields) {
		for (s in fields[f])
			stat = fields[f][s];

		if (stat.value instanceof Function) {
			value = stat.value(now[s], last[s]);
		} else if (stat.value instanceof Array) {
			value = 0;

			for (i = 0; i < stat.value.length; i++) {
				value += (now[s].data[stat.value[i]] -
				    last[s].data[stat.value[i]]);
			}
		} else {
			value = now[s].data[stat.value] -
			    last[s].data[stat.value];
		}

		if (stat.time) {
			/*
			 * If this is an expression of percentage of time, we
			 * need to divide by the delta in snap time.
			 */
			value = parseInt((value / (now[s].snaptime -
			    last[s].snaptime) * 100.0) + '', 10);
		}

		if (line.length > 0)
			line += ' ';

		line += pad(value + '', stat.width ? stat.width : f.length);
	}

	console.log(line);
};

var data = [];
var gen = 0;

var output = function ()
{
	var now = {};
	var cpus = [];
	var header = false, i;

	gen = gen ^ 1;
	data[gen] = {};

	for (stat in reader) {
		now = reader[stat].read();

		for (i = 0; i < now.length; i++) {
			var id = now[i].instance;

			if (!data[gen][id]) {
				cpus.push(id);
				data[gen][id] = {};
			}

			data[gen][id][stat] = now[i];
		}
	}

	cpus.sort();

	for (i = 0; i < cpus.length; i++) {
		if (data[gen ^ 1] && data[gen ^ 1][cpus[i]]) {
			if (!header) {
				outputheader();
				header = true;
			}

			outputcpu(data[gen][cpus[i]], data[gen ^ 1][cpus[i]]);
		}
	}
};

setInterval(output, 1000);
