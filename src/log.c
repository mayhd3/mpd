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

#include "log.h"
#include "conf.h"
#include "utils.h"
#include "config.h"

#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <glib.h>

#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif

#define LOG_LEVEL_SECURE G_LOG_LEVEL_INFO

#define LOG_DATE_BUF_SIZE 16
#define LOG_DATE_LEN (LOG_DATE_BUF_SIZE - 1)

static GLogLevelFlags log_threshold = G_LOG_LEVEL_MESSAGE;

static const char *log_charset;

static bool stdout_mode = true;
static int out_fd;
static const char *out_filename;

static void redirect_logs(int fd)
{
	assert(fd >= 0);
	if (dup2(fd, STDOUT_FILENO) < 0)
		g_error("problems dup2 stdout : %s\n", strerror(errno));
	if (dup2(fd, STDERR_FILENO) < 0)
		g_error("problems dup2 stderr : %s\n", strerror(errno));
}

static const char *log_date(void)
{
	static char buf[LOG_DATE_BUF_SIZE];
	time_t t = time(NULL);
	strftime(buf, LOG_DATE_BUF_SIZE, "%b %d %H:%M : ", localtime(&t));
	return buf;
}

static void
file_log_func(const gchar *log_domain,
	      G_GNUC_UNUSED GLogLevelFlags log_level,
	      const gchar *message, G_GNUC_UNUSED gpointer user_data)
{
	char *converted;

	if (log_level > log_threshold)
		return;

	if (log_charset != NULL) {
		converted = g_convert_with_fallback(message, -1,
						    log_charset, "utf-8",
						    NULL, NULL, NULL, NULL);
		if (converted != NULL)
			message = converted;
	} else
		converted = NULL;

	if (log_domain == NULL)
		log_domain = "";

	fprintf(stderr, "%s%s%s%s",
		stdout_mode ? "" : log_date(),
		log_domain, *log_domain == 0 ? "" : ": ",
		message);

	g_free(converted);
}

static void
log_init_stdout(void)
{
	g_log_set_default_handler(file_log_func, NULL);
}

static int
open_log_file(void)
{
	assert(out_filename != NULL);

	return open(out_filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
}

static void
log_init_file(const char *path, unsigned line)
{
	out_filename = path;
	out_fd = open_log_file();
	if (out_fd < 0)
		g_error("problem opening log file \"%s\" (config line %u) for "
			"writing\n", path, line);

	g_log_set_default_handler(file_log_func, NULL);
}

#ifdef HAVE_SYSLOG

static int
glib_to_syslog_level(GLogLevelFlags log_level)
{
	switch (log_level & G_LOG_LEVEL_MASK) {
	case G_LOG_LEVEL_ERROR:
	case G_LOG_LEVEL_CRITICAL:
		return LOG_ERR;

	case G_LOG_LEVEL_WARNING:
		return LOG_WARNING;

	case G_LOG_LEVEL_MESSAGE:
		return LOG_NOTICE;

	case G_LOG_LEVEL_INFO:
		return LOG_INFO;

	case G_LOG_LEVEL_DEBUG:
		return LOG_DEBUG;

	default:
		return LOG_NOTICE;
	}
}

static void
syslog_log_func(const gchar *log_domain,
		GLogLevelFlags log_level, const gchar *message,
		G_GNUC_UNUSED gpointer user_data)
{
	if (stdout_mode) {
		/* fall back to the file log function during
		   startup */
		file_log_func(log_domain, log_level,
			      message, user_data);
		return;
	}

	if (log_level > log_threshold)
		return;

	if (log_domain == NULL)
		log_domain = "";

	syslog(glib_to_syslog_level(log_level), "%s%s%s",
	       log_domain, *log_domain == 0 ? "" : ": ",
	       message);
}

static void
log_init_syslog(void)
{
	assert(out_filename == NULL);

	openlog(PACKAGE, 0, LOG_DAEMON);
	g_log_set_default_handler(syslog_log_func, NULL);
}

#endif

static inline GLogLevelFlags
parse_log_level(const char *value, unsigned line)
{
	if (0 == strcmp(value, "default"))
		return G_LOG_LEVEL_MESSAGE;
	if (0 == strcmp(value, "secure"))
		return LOG_LEVEL_SECURE;
	else if (0 == strcmp(value, "verbose"))
		return G_LOG_LEVEL_DEBUG;
	else
		g_error("unknown log level \"%s\" at line %u\n",
			value, line);
}

void log_init(bool verbose, bool use_stdout)
{
	ConfigParam *param;

	g_get_charset(&log_charset);

	if (verbose)
		log_threshold = G_LOG_LEVEL_DEBUG;
	else if ((param = getConfigParam(CONF_LOG_LEVEL)) != NULL)
		log_threshold = parse_log_level(param->value, param->line);

	if (use_stdout) {
		log_init_stdout();
	} else {
		param = getConfigParam(CONF_LOG_FILE);
		if (param == NULL) {
#ifdef HAVE_SYSLOG
			/* no configuration: default to syslog (if
			   available) */
			log_init_syslog();
#else
			g_error("config parameter \"%s\" not found\n",
				CONF_LOG_FILE);
#endif
#ifdef HAVE_SYSLOG
		} else if (strcmp(param->value, "syslog") == 0) {
			log_init_syslog();
#endif
		} else {
			char *path = parsePath(param->value);
			g_free(param->value);

			if (path == NULL)
				g_error("error parsing \"%s\" at line %i\n",
					CONF_LOG_FILE, param->line);
			param->value = path;

			log_init_file(param->value, param->line);
		}
	}
}

void setup_log_output(bool use_stdout)
{
	fflush(NULL);
	if (!use_stdout) {
		if (out_filename != NULL) {
			redirect_logs(out_fd);
			close(out_fd);
		}

		stdout_mode = false;
		log_charset = NULL;
	}
}

#define log_func(func,level) \
G_GNUC_PRINTF(1, 2) void func(const char *fmt, ...) \
{ \
	if (level <= log_threshold) { \
		va_list args; \
		va_start(args, fmt); \
		g_logv(NULL, level, fmt, args);	\
		va_end(args); \
	} \
}

log_func(ERROR, G_LOG_LEVEL_WARNING)
log_func(WARNING, G_LOG_LEVEL_WARNING)
log_func(LOG, G_LOG_LEVEL_MESSAGE)
log_func(SECURE, LOG_LEVEL_SECURE)
log_func(DEBUG, G_LOG_LEVEL_DEBUG)

#undef log_func

G_GNUC_PRINTF(1, 2) G_GNUC_NORETURN void FATAL(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	g_logv(NULL, G_LOG_LEVEL_ERROR, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

int cycle_log_files(void)
{
	int fd;

	if (stdout_mode || out_filename == NULL)
		return 0;
	assert(out_filename);

	g_debug("Cycling log files...\n");
	close_log_files();

	fd = open_log_file();
	if (fd < 0) {
		g_warning("error re-opening log file: %s\n", out_filename);
		return -1;
	}

	redirect_logs(fd);
	g_debug("Done cycling log files\n");
	return 0;
}

void close_log_files(void)
{
	if (stdout_mode)
		return;

	if (out_filename == NULL)
		closelog();
}
