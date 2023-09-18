# node-kstat

This repository is part of the Triton Data Center project. See the
[contribution guidelines](https://github.com/TritonDataCenter/triton/blob/master/CONTRIBUTING.md)
and general documentation at the main
[Triton project](https://github.com/TritonDataCenter/triton) page.

A node.js binding to the illumos [kstat](https://illumos.org/man/3KSTAT/kstat)
interface.  This allows one to read kernel statistics via the kstat framework.
It is likely to work unmodified on Solaris too.

## Usage

The `kstat` module exports a single class, `Reader` that has the following
methods:

```
Reader():  Takes an optional object specifying the kstats to read.  This
           object may have the following members:

           class    =>  optional string denoting class of kstat(s) to read
           module   =>  optional string denoting module of kstat(s) to read
           name     =>  optional string denoting name of kstat(s) to read
           instance =>  optional integer denoting instance of kstat(s) to read

           Together, these members form a specification of kstats to read.

read():    Returns an array of kstats that match the specification with
           which the reader instance was constructed *and* an optional
           specification passed to this function.  Each element of the
           array is an object that contains the following members:

           class    =>  string denoting class of kstat
           module   =>  string denoting module of kstat
           name     =>  string denoting name of kstat
           instance =>  integer denoting instance of kstat
           snaptime =>  nanoseconds since boot of this snapshot
           data     =>  an object containing the named kstat data itself
```

## Examples

Here is a simple program that dumps the kstats of class `mib2`:

```javascript
var kstat = require('kstat');
var util = require('util');
var reader = new kstat.Reader({ 'class': 'mib2' } );
console.log(util.inspect(reader.read()));
```

Here is a the same program that reads only the `mib2` class kstats from the
`icmp` module:

```javascript
var kstat = require('kstat');
var util = require('util');
var reader = new kstat.Reader({ 'class': 'mib2', module: 'icmp' } );
console.log(util.inspect(reader.read()));
```

Finally, here is a simple program that prints the number of ICMP datagrams
received per second:

```javascript
var kstat = require('kstat');
var reader = new kstat.Reader({ 'class': 'mib2', module: 'icmp' } );

var data = [];
var gen = 0;

setInterval(function() {
	data[gen] = reader.read()[0];
	gen ^= 1;

	if (!(data[0] && data[1]))
		return;

	console.log(data[gen ^ 1].data.inDatagrams - data[gen].data.inDatagrams);
}, 1000);
```

You can find further example programs in the [examples](/examples) directory.

## License

MIT.
