/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
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

#ifndef SONG_H
#define SONG_H

#include <sys/time.h>

#define SONG_BEGIN	"songList begin"
#define SONG_END	"songList end"

#define SONG_FILE	"file: "
#define SONG_TIME	"Time: "

struct song {
	struct tag *tag;
	struct directory *parentDir;
	time_t mtime;
	char url[sizeof(int)];
};

struct song *
song_alloc(const char *url, struct directory *parent);

struct song *
newSong(const char *url, struct directory *parentDir);

void
freeJustSong(struct song *);

int
updateSongInfo(struct song *song);

/*
 * get_song_url - Returns a path of a song in UTF8-encoded form
 * path_max_tmp is the argument that the URL is written to, this
 * buffer is assumed to be MPD_PATH_MAX or greater (including
 * terminating '\0').
 */
char *
get_song_url(char *path_max_tmp, struct song *song);

static inline int
song_is_file(const struct song *song)
{
	return !!song->parentDir;
}

#endif
