{
  "targets": [
    {
      "target_name": "kstat",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "link_settings": {
        "libraries": [ "-lnvpair" ]
      },
      "sources": [ "addon.cc", "kstat.c" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
    }
  ]
}
