/* ncmpc (Ncurses MPD Client)
 * (c) 2004-2009 The Music Player Daemon Project
 * Project homepage: http://musicpd.org
 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef SCREEN_BROWSER_H
#define SCREEN_BROWSER_H

#include "screen.h"
#include "mpdclient.h"
#include "config.h"

#include <stdbool.h>

struct list_window;
struct list_window_state;

struct screen_browser {
	struct list_window *lw;

	mpdclient_filelist_t *filelist;
};

#ifndef NCMPC_MINI

void
sync_highlights(mpdclient_t *c, mpdclient_filelist_t *fl);

void
browser_playlist_changed(struct screen_browser *browser, mpdclient_t *c,
			 int event, gpointer data);

#endif

const char *browser_lw_callback(unsigned index, bool *highlight, void *filelist);

bool
browser_change_directory(struct screen_browser *browser, mpdclient_t *c,
			 filelist_entry_t *entry, const char *new_path);

bool
browser_cmd(struct screen_browser *browser,
	    struct mpdclient *c, command_t cmd);

#endif