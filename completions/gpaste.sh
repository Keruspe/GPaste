_gpaste()
{
    local cur opts
    COMPREPLY=()
    cur="${COMP_WORDS[${COMP_CWORD}]}"
    opts="add set delete empty quit applet preferences version help"
    COMPREPLY=( $(compgen -W "$opts" -- $cur ) )
}

complete -F _gpaste gpaste
