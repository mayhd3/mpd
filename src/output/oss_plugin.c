/* the Music Player Daemon (MPD)
 * Copyright (C) 2003-2007 by Warren Dukes (warren.dukes@gmail.com)
 * This project's homepage is: http://www.musicpd.org
 *
 * OSS audio output (c) 2004, 2005, 2006, 2007 by Eric Wong <eric@petta-tech.com>
 *                   and Warren Dukes <warren.dukes@gmail.com>
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

#include "../output_api.h"
#include "../mixer_api.h"

#include <glib.h>

#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "oss"

#if defined(__OpenBSD__) || defined(__NetBSD__)
# include <soundcard.h>
#else /* !(defined(__OpenBSD__) || defined(__NetBSD__) */
# include <sys/soundcard.h>
#endif /* !(defined(__OpenBSD__) || defined(__NetBSD__) */

#if G_BYTE_ORDER == G_BIG_ENDIAN
# define	AFMT_S16_MPD	 AFMT_S16_BE
#else
# define	AFMT_S16_MPD	 AFMT_S16_LE
#endif

typedef struct _OssData {
	int fd;
	const char *device;
	struct audio_format audio_format;
	int bitFormat;
	int *supported[3];
	int numSupported[3];
	int *unsupported[3];
	int numUnsupported[3];
	struct oss_mixer *mixer;
} OssData;

enum oss_support {
	OSS_SUPPORTED = 1,
	OSS_UNSUPPORTED = 0,
	OSS_UNKNOWN = -1,
};

enum oss_param {
	OSS_RATE = 0,
	OSS_CHANNELS = 1,
	OSS_BITS = 2,
};

static enum oss_param
getIndexForParam(unsigned param)
{
	enum oss_param idx = OSS_RATE;

	switch (param) {
	case SNDCTL_DSP_SPEED:
		idx = OSS_RATE;
		break;
	case SNDCTL_DSP_CHANNELS:
		idx = OSS_CHANNELS;
		break;
	case SNDCTL_DSP_SAMPLESIZE:
		idx = OSS_BITS;
		break;
	}

	return idx;
}

static int findSupportedParam(OssData * od, unsigned param, int val)
{
	int i;
	enum oss_param idx = getIndexForParam(param);

	for (i = 0; i < od->numSupported[idx]; i++) {
		if (od->supported[idx][i] == val)
			return 1;
	}

	return 0;
}

static int canConvert(int idx, int val)
{
	switch (idx) {
	case OSS_BITS:
		if (val != 16)
			return 0;
		break;
	case OSS_CHANNELS:
		if (val != 2)
			return 0;
		break;
	}

	return 1;
}

static int getSupportedParam(OssData * od, unsigned param, int val)
{
	int i;
	enum oss_param idx = getIndexForParam(param);
	int ret = -1;
	int least = val;
	int diff;

	for (i = 0; i < od->numSupported[idx]; i++) {
		diff = od->supported[idx][i] - val;
		if (diff < 0)
			diff = -diff;
		if (diff < least) {
			if (!canConvert(idx, od->supported[idx][i])) {
				continue;
			}
			least = diff;
			ret = od->supported[idx][i];
		}
	}

	return ret;
}

static int findUnsupportedParam(OssData * od, unsigned param, int val)
{
	int i;
	enum oss_param idx = getIndexForParam(param);

	for (i = 0; i < od->numUnsupported[idx]; i++) {
		if (od->unsupported[idx][i] == val)
			return 1;
	}

	return 0;
}

static void addSupportedParam(OssData * od, unsigned param, int val)
{
	enum oss_param idx = getIndexForParam(param);

	od->numSupported[idx]++;
	od->supported[idx] = g_realloc(od->supported[idx],
				       od->numSupported[idx] * sizeof(int));
	od->supported[idx][od->numSupported[idx] - 1] = val;
}

static void addUnsupportedParam(OssData * od, unsigned param, int val)
{
	enum oss_param idx = getIndexForParam(param);

	od->numUnsupported[idx]++;
	od->unsupported[idx] = g_realloc(od->unsupported[idx],
					 od->numUnsupported[idx] *
					 sizeof(int));
	od->unsupported[idx][od->numUnsupported[idx] - 1] = val;
}

static void removeSupportedParam(OssData * od, unsigned param, int val)
{
	int i;
	int j = 0;
	enum oss_param idx = getIndexForParam(param);

	for (i = 0; i < od->numSupported[idx] - 1; i++) {
		if (od->supported[idx][i] == val)
			j = 1;
		od->supported[idx][i] = od->supported[idx][i + j];
	}

	od->numSupported[idx]--;
	od->supported[idx] = g_realloc(od->supported[idx],
				       od->numSupported[idx] * sizeof(int));
}

static void removeUnsupportedParam(OssData * od, unsigned param, int val)
{
	int i;
	int j = 0;
	enum oss_param idx = getIndexForParam(param);

	for (i = 0; i < od->numUnsupported[idx] - 1; i++) {
		if (od->unsupported[idx][i] == val)
			j = 1;
		od->unsupported[idx][i] = od->unsupported[idx][i + j];
	}

	od->numUnsupported[idx]--;
	od->unsupported[idx] = g_realloc(od->unsupported[idx],
					 od->numUnsupported[idx] *
					 sizeof(int));
}

static enum oss_support
isSupportedParam(OssData * od, unsigned param, int val)
{
	if (findSupportedParam(od, param, val))
		return OSS_SUPPORTED;
	if (findUnsupportedParam(od, param, val))
		return OSS_UNSUPPORTED;
	return OSS_UNKNOWN;
}

static void supportParam(OssData * od, unsigned param, int val)
{
	enum oss_support supported = isSupportedParam(od, param, val);

	if (supported == OSS_SUPPORTED)
		return;

	if (supported == OSS_UNSUPPORTED) {
		removeUnsupportedParam(od, param, val);
	}

	addSupportedParam(od, param, val);
}

static void unsupportParam(OssData * od, unsigned param, int val)
{
	enum oss_support supported = isSupportedParam(od, param, val);

	if (supported == OSS_UNSUPPORTED)
		return;

	if (supported == OSS_SUPPORTED) {
		removeSupportedParam(od, param, val);
	}

	addUnsupportedParam(od, param, val);
}

static OssData *newOssData(void)
{
	OssData *ret = g_new(OssData, 1);

	ret->device = NULL;
	ret->fd = -1;

	ret->supported[OSS_RATE] = NULL;
	ret->supported[OSS_CHANNELS] = NULL;
	ret->supported[OSS_BITS] = NULL;
	ret->unsupported[OSS_RATE] = NULL;
	ret->unsupported[OSS_CHANNELS] = NULL;
	ret->unsupported[OSS_BITS] = NULL;

	ret->numSupported[OSS_RATE] = 0;
	ret->numSupported[OSS_CHANNELS] = 0;
	ret->numSupported[OSS_BITS] = 0;
	ret->numUnsupported[OSS_RATE] = 0;
	ret->numUnsupported[OSS_CHANNELS] = 0;
	ret->numUnsupported[OSS_BITS] = 0;

	supportParam(ret, SNDCTL_DSP_SPEED, 48000);
	supportParam(ret, SNDCTL_DSP_SPEED, 44100);
	supportParam(ret, SNDCTL_DSP_CHANNELS, 2);
	supportParam(ret, SNDCTL_DSP_SAMPLESIZE, 16);

	ret->mixer = oss_mixer_init();

	return ret;
}

static void freeOssData(OssData * od)
{
	g_free(od->supported[OSS_RATE]);
	g_free(od->supported[OSS_CHANNELS]);
	g_free(od->supported[OSS_BITS]);
	g_free(od->unsupported[OSS_RATE]);
	g_free(od->unsupported[OSS_CHANNELS]);
	g_free(od->unsupported[OSS_BITS]);

	oss_mixer_finish(od->mixer);

	free(od);
}

#define OSS_STAT_NO_ERROR 	0
#define OSS_STAT_NOT_CHAR_DEV	-1
#define OSS_STAT_NO_PERMS	-2
#define OSS_STAT_DOESN_T_EXIST	-3
#define OSS_STAT_OTHER		-4

static int oss_statDevice(const char *device, int *stErrno)
{
	struct stat st;

	if (0 == stat(device, &st)) {
		if (!S_ISCHR(st.st_mode)) {
			return OSS_STAT_NOT_CHAR_DEV;
		}
	} else {
		*stErrno = errno;

		switch (errno) {
		case ENOENT:
		case ENOTDIR:
			return OSS_STAT_DOESN_T_EXIST;
		case EACCES:
			return OSS_STAT_NO_PERMS;
		default:
			return OSS_STAT_OTHER;
		}
	}

	return 0;
}

static const char *default_devices[] = { "/dev/sound/dsp", "/dev/dsp" };

static bool oss_testDefault(void)
{
	int fd, i;

	for (i = G_N_ELEMENTS(default_devices); --i >= 0; ) {
		if ((fd = open(default_devices[i], O_WRONLY)) >= 0) {
			close(fd);
			return true;
		}
		g_warning("Error opening OSS device \"%s\": %s\n",
			  default_devices[i], strerror(errno));
	}

	return false;
}

static void *oss_open_default(ConfigParam *param)
{
	int i;
	int err[G_N_ELEMENTS(default_devices)];
	int ret[G_N_ELEMENTS(default_devices)];

	for (i = G_N_ELEMENTS(default_devices); --i >= 0; ) {
		ret[i] = oss_statDevice(default_devices[i], &err[i]);
		if (ret[i] == 0) {
			OssData *od = newOssData();
			od->device = default_devices[i];
			oss_mixer_configure(od->mixer, param);
			return od;
		}
	}

	if (param)
		g_warning("error trying to open specified OSS device"
			  " at line %i\n", param->line);
	else
		g_warning("error trying to open default OSS device\n");

	for (i = G_N_ELEMENTS(default_devices); --i >= 0; ) {
		const char *dev = default_devices[i];
		switch(ret[i]) {
		case OSS_STAT_DOESN_T_EXIST:
			g_warning("%s not found\n", dev);
			break;
		case OSS_STAT_NOT_CHAR_DEV:
			g_warning("%s is not a character device\n", dev);
			break;
		case OSS_STAT_NO_PERMS:
			g_warning("%s: permission denied\n", dev);
			break;
		default:
			g_warning("Error accessing %s: %s\n",
				  dev, strerror(err[i]));
		}
	}
	exit(EXIT_FAILURE);
	return NULL; /* some compilers can be dumb... */
}

static void *oss_initDriver(G_GNUC_UNUSED struct audio_output *audioOutput,
			    G_GNUC_UNUSED const struct audio_format *audio_format,
			    ConfigParam * param)
{
	if (param) {
		BlockParam *bp = getBlockParam(param, "device");
		if (bp) {
			OssData *od = newOssData();
			od->device = bp->value;
			oss_mixer_configure(od->mixer, param);
			return od;
		}
	}
	return oss_open_default(param);
}

static void oss_finishDriver(void *data)
{
	OssData *od = data;

	freeOssData(od);
}

static int setParam(OssData * od, unsigned param, int *value)
{
	int val = *value;
	int copy;
	enum oss_support supported = isSupportedParam(od, param, val);

	do {
		if (supported == OSS_UNSUPPORTED) {
			val = getSupportedParam(od, param, val);
			if (copy < 0)
				return -1;
		}
		copy = val;
		if (ioctl(od->fd, param, &copy)) {
			unsupportParam(od, param, val);
			supported = OSS_UNSUPPORTED;
		} else {
			if (supported == OSS_UNKNOWN) {
				supportParam(od, param, val);
				supported = OSS_SUPPORTED;
			}
			val = copy;
		}
	} while (supported == OSS_UNSUPPORTED);

	*value = val;

	return 0;
}

static void oss_close(OssData * od)
{
	if (od->fd >= 0)
		while (close(od->fd) && errno == EINTR) ;
	od->fd = -1;
}

static bool oss_open(OssData *od)
{
	int tmp;

	if ((od->fd = open(od->device, O_WRONLY)) < 0) {
		g_warning("Error opening OSS device \"%s\": %s\n", od->device,
			  strerror(errno));
		goto fail;
	}

	tmp = od->audio_format.channels;
	if (setParam(od, SNDCTL_DSP_CHANNELS, &tmp)) {
		g_warning("OSS device \"%s\" does not support %u channels: %s\n",
			  od->device, od->audio_format.channels,
			  strerror(errno));
		goto fail;
	}
	od->audio_format.channels = tmp;

	tmp = od->audio_format.sample_rate;
	if (setParam(od, SNDCTL_DSP_SPEED, &tmp)) {
		g_warning("OSS device \"%s\" does not support %u Hz audio: %s\n",
			  od->device, od->audio_format.sample_rate,
			  strerror(errno));
		goto fail;
	}
	od->audio_format.sample_rate = tmp;

	switch (od->audio_format.bits) {
	case 8:
		tmp = AFMT_S8;
		break;
	case 16:
		tmp = AFMT_S16_MPD;
		break;

	default:
		/* not supported by OSS - fall back to 16 bit */
		od->audio_format.bits = 16;
		tmp = AFMT_S16_MPD;
		break;
	}

	if (setParam(od, SNDCTL_DSP_SAMPLESIZE, &tmp)) {
		g_warning("OSS device \"%s\" does not support %u bit audio: %s\n",
			  od->device, tmp, strerror(errno));
		goto fail;
	}

	return true;

fail:
	oss_close(od);
	return false;
}

static bool
oss_openDevice(void *data, struct audio_format *audioFormat)
{
	bool ret;
	OssData *od = data;

	od->audio_format = *audioFormat;

	ret = oss_open(od);
	if (!ret)
		return false;

	*audioFormat = od->audio_format;

	g_debug("device \"%s\" will be playing %u bit %u channel audio at "
		"%u Hz\n", od->device,
		od->audio_format.bits, od->audio_format.channels,
		od->audio_format.sample_rate);

	oss_mixer_open(od->mixer);

	return ret;
}

static void oss_closeDevice(void *data)
{
	OssData *od = data;

	oss_close(od);
	oss_mixer_close(od->mixer);
}

static void oss_dropBufferedAudio(void *data)
{
	OssData *od = data;

	if (od->fd >= 0) {
		ioctl(od->fd, SNDCTL_DSP_RESET, 0);
		oss_close(od);
	}
}

static bool
oss_playAudio(void *data, const char *playChunk, size_t size)
{
	OssData *od = data;
	ssize_t ret;

	/* reopen the device since it was closed by dropBufferedAudio */
	if (od->fd < 0 && oss_open(od) < 0)
		return false;

	while (size > 0) {
		ret = write(od->fd, playChunk, size);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			g_warning("closing oss device \"%s\" due to write error: "
				  "%s\n", od->device, strerror(errno));
			return false;
		}
		playChunk += ret;
		size -= ret;
	}

	return true;
}

static bool
oss_control(void *data, int cmd, void *arg)
{
	OssData *od = data;
	return oss_mixer_control(od->mixer, cmd, arg);
}

const struct audio_output_plugin ossPlugin = {
	.name = "oss",
	.test_default_device = oss_testDefault,
	.init = oss_initDriver,
	.finish = oss_finishDriver,
	.open = oss_openDevice,
	.play = oss_playAudio,
	.cancel = oss_dropBufferedAudio,
	.close = oss_closeDevice,
	.control = oss_control,
};