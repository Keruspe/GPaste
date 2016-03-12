## This file is part of GPaste.
##
## Copyright (c) 2010-2016, Marc-Antoine Perennou <Marc-Antoine@Perennou.com>

bashcompletion_DATA = %D%/completions/gpaste-client
zshcompletion_DATA  = %D%/completions/_gpaste-client

EXTRA_DIST +=                  \
	$(bashcompletion_DATA) \
	$(zshcompletion_DATA)  \
	$(NULL)
