/* the Music Player Daemon (MPD)
 * (c)2007 by Warren Dukes (warren.dukes@gmail.com)
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

#include "locate.h"
#include "path.h"
#include "tag.h"
#include "song.h"

#include <glib.h>

#include <stdlib.h>

#define LOCATE_TAG_FILE_KEY     "file"
#define LOCATE_TAG_FILE_KEY_OLD "filename"
#define LOCATE_TAG_ANY_KEY      "any"

int getLocateTagItemType(const char *str)
{
	int i;

	if (0 == strcasecmp(str, LOCATE_TAG_FILE_KEY) ||
	    0 == strcasecmp(str, LOCATE_TAG_FILE_KEY_OLD)) 
	{
		return LOCATE_TAG_FILE_TYPE;
	}

	if (0 == strcasecmp(str, LOCATE_TAG_ANY_KEY)) 
	{
		return LOCATE_TAG_ANY_TYPE;
	}

	for (i = 0; i < TAG_NUM_OF_ITEM_TYPES; i++) 
	{
		if (0 == strcasecmp(str, mpdTagItemKeys[i]))
			return i;
	}

	return -1;
}

static int initLocateTagItem(LocateTagItem * item,
			     const char *typeStr, const char *needle)
{
	item->tagType = getLocateTagItemType(typeStr);

	if (item->tagType < 0)
		return -1;

	item->needle = g_strdup(needle);

	return 0;
}

LocateTagItem *newLocateTagItem(const char *typeStr, const char *needle)
{
	LocateTagItem *ret = g_new(LocateTagItem, 1);

	if (initLocateTagItem(ret, typeStr, needle) < 0) {
		free(ret);
		ret = NULL;
	}

	return ret;
}

void freeLocateTagItemArray(int count, LocateTagItem * array)
{
	int i;

	for (i = 0; i < count; i++)
		free(array[i].needle);

	free(array);
}

int newLocateTagItemArrayFromArgArray(char *argArray[],
				      int numArgs, LocateTagItem ** arrayRet)
{
	int i, j;
	LocateTagItem *item;

	if (numArgs == 0)
		return 0;

	if (numArgs % 2 != 0)
		return -1;

	*arrayRet = g_new(LocateTagItem, numArgs / 2);

	for (i = 0, item = *arrayRet; i < numArgs / 2; i++, item++) {
		if (initLocateTagItem
		    (item, argArray[i * 2], argArray[i * 2 + 1]) < 0)
			goto fail;
	}

	return numArgs / 2;

fail:
	for (j = 0; j < i; j++) {
		free((*arrayRet)[j].needle);
	}

	free(*arrayRet);
	*arrayRet = NULL;
	return -1;
}

void freeLocateTagItem(LocateTagItem * item)
{
	free(item->needle);
	free(item);
}

static int
strstrSearchTag(struct song *song, enum tag_type type, char *str)
{
	int i;
	char *duplicate;
	int ret = 0;
	int8_t visitedTypes[TAG_NUM_OF_ITEM_TYPES] = { 0 };

	if (type == LOCATE_TAG_FILE_TYPE || type == LOCATE_TAG_ANY_TYPE) {
		char *uri, *p;

		uri = song_get_uri(song);
		p = g_utf8_casefold(uri, -1);
		g_free(uri);

		if (strstr(p, str))
			ret = 1;
		g_free(p);
		if (ret == 1 || type == LOCATE_TAG_FILE_TYPE)
			return ret;
	}

	if (!song->tag)
		return 0;

	for (i = 0; i < song->tag->numOfItems && !ret; i++) {
		visitedTypes[song->tag->items[i]->type] = 1;
		if (type != LOCATE_TAG_ANY_TYPE &&
		    song->tag->items[i]->type != type) {
			continue;
		}

		duplicate = g_utf8_casefold(song->tag->items[i]->value, -1);
		if (*str && strstr(duplicate, str))
			ret = 1;
		g_free(duplicate);
	}

	/** If the search critieron was not visited during the sweep
	 * through the song's tag, it means this field is absent from
	 * the tag or empty. Thus, if the searched string is also
	 *  empty (first char is a \0), then it's a match as well and
	 *  we should return 1.
	 */
	if (!*str && !visitedTypes[type])
		return 1;

	return ret;
}

int
strstrSearchTags(struct song *song, int numItems, LocateTagItem *items)
{
	int i;

	for (i = 0; i < numItems; i++) {
		if (!strstrSearchTag(song, items[i].tagType,
				     items[i].needle)) {
			return 0;
		}
	}

	return 1;
}

static int
tagItemFoundAndMatches(struct song *song, enum tag_type type, char *str)
{
	int i;
	int8_t visitedTypes[TAG_NUM_OF_ITEM_TYPES] = { 0 };

	if (type == LOCATE_TAG_FILE_TYPE || type == LOCATE_TAG_ANY_TYPE) {
		char *uri = song_get_uri(song);
		bool matches = strcmp(str, uri) == 0;
		g_free(uri);

		if (matches)
			return 1;

		if (type == LOCATE_TAG_FILE_TYPE)
			return 0;
	}

	if (!song->tag)
		return 0;

	for (i = 0; i < song->tag->numOfItems; i++) {
		visitedTypes[song->tag->items[i]->type] = 1;
		if (type != LOCATE_TAG_ANY_TYPE &&
		    song->tag->items[i]->type != type) {
			continue;
		}

		if (0 == strcmp(str, song->tag->items[i]->value))
			return 1;
	}

	/** If the search critieron was not visited during the sweep
	 * through the song's tag, it means this field is absent from
	 * the tag or empty. Thus, if the searched string is also
	 *  empty (first char is a \0), then it's a match as well and
	 *  we should return 1.
	 */
	if (!*str && !visitedTypes[type])
		return 1;

	return 0;
}


int
tagItemsFoundAndMatches(struct song *song, int numItems,
			LocateTagItem * items)
{
	int i;

	for (i = 0; i < numItems; i++) {
		if (!tagItemFoundAndMatches(song, items[i].tagType,
					    items[i].needle)) {
			return 0;
		}
	}

	return 1;
}