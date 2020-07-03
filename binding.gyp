{
  'targets': [
    {
      'target_name': 'traceroute',
      'sources': [ 'binding.c', 'myicmp.c', 'poll.c' ],
      'defines': ['__FAVOR_BSD', '_BSD_SOURCE']
    }
  ]
}
