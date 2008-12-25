/* ncmpc
 * (c) 2004 by Kalle Wallin <kaw@linux.se>
 * Copyright (C) 2008 Max Kellermann <max@duempel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ncu.h"
#include "config.h"

#ifdef ENABLE_COLORS
#include "colors.h"
#endif

#ifdef HAVE_GETMOUSE
#include "options.h"
#endif

#ifdef HAVE_NCURSESW_NCURSES_H
#include <ncursesw/ncurses.h>
#else
#include <ncurses.h>
#endif

void
ncu_init(void)
{
	/* initialize the curses library */
	initscr();

	/* initialize color support */
#ifdef ENABLE_COLORS
	colors_start();
#endif

	/* tell curses not to do NL->CR/NL on output */
	nonl();

	/* don't echo input */
	noecho();

	/* set cursor invisible */
	curs_set(0);

	/* enable extra keys */
	keypad(stdscr, TRUE);

	/* initialize mouse support */
#ifdef HAVE_GETMOUSE
	if (options.enable_mouse)
		mousemask(ALL_MOUSE_EVENTS, NULL);
#endif
}

void
ncu_deinit(void)
{
	endwin();
}