{
  'targets': [
    {
      'target_name': 'kstat',
      'sources': [ 'kstat.cc' ],
      'libraries': [ '-lkstat' ],
      'cflags_cc': [ '-Wno-write-strings' ],
      'cflags_cc!': [ '-fno-exceptions' ],
    }
  ]
}
