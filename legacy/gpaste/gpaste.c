/*
 *      This file is part of GPaste.
 *
 *      Copyright 2015 Marc-Antoine Perennou <Marc-Antoine@Perennou.com>
 *
 *      GPaste is free software: you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation, either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      GPaste is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with GPaste.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int
main (int   argc __attribute__((unused)),
      char *argv[])
{
    const char *gpaste_client = BINDIR "/gpaste-client";

    fprintf (stderr, "\nThe \"gpaste\" command is deprecated and will soon be removed, please use \"gpaste-client\" instead.\n\n");

    argv[0] = (char *) gpaste_client;
    execv (gpaste_client, argv);

    return EXIT_SUCCESS;
}
