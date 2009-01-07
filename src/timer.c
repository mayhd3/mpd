/* the Music Player Daemon (MPD)
 * Copyright (C) 2007 by Warren Dukes (warren.dukes@gmail.com)
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

#include "timer.h"
#include "utils.h"
#include "audio_format.h"

#include <glib.h>

#include <assert.h>
#include <limits.h>
#include <sys/time.h>
#include <stddef.h>

static uint64_t now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);

	return ((uint64_t)tv.tv_sec * 1000000) + tv.tv_usec;
}

Timer *timer_new(const struct audio_format *af)
{
	Timer *timer = g_new(Timer, 1);
	timer->time = 0;
	timer->started = 0;
	timer->rate = af->sample_rate * audio_format_frame_size(af);

	return timer;
}

void timer_free(Timer *timer)
{
	g_free(timer);
}

void timer_start(Timer *timer)
{
	timer->time = now();
	timer->started = 1;
}

void timer_reset(Timer *timer)
{
	timer->time = 0;
	timer->started = 0;
}

void timer_add(Timer *timer, int size)
{
	assert(timer->started);

	timer->time += ((uint64_t)size * 1000000) / timer->rate;
}

void timer_sync(Timer *timer)
{
	int64_t sleep_duration;

	assert(timer->started);

	sleep_duration = timer->time - now();
	if (sleep_duration > 0)
		my_usleep(sleep_duration);
}