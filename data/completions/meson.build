if get_option('bash-completion')
  install_data(
    'gpaste-client',
    install_dir: join_paths(get_option('datadir'), 'bash-completion', 'completions'),
  )
endif

if get_option('zsh-completion')
  install_data(
    '_gpaste-client',
    install_dir: join_paths(get_option('datadir'), 'zsh', 'site-functions'),
  )
endif
