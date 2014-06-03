/*
 * Copyright (C) 2003-2011 The Music Player Daemon Project
 * http://www.musicpd.org
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"
#include "log.h"
#include "conf.h"
#include "utils.h"
#include "fd_util.h"
#include "mpd_error.h"

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
#include <ctype.h>

//In this file log might not be initialized yet, 
//so we just use fprintf(stderr, .

#define LOG_LEVEL_SECURE LOG_INFO

#define LOG_DATE_BUF_SIZE 16
#define LOG_DATE_LEN (LOG_DATE_BUF_SIZE - 1)

static int log_threshold = LOG_INFO;

static const char *log_charset;

static bool stdout_mode = true;
static int out_fd = -1;
static char *out_filename;

void (*log_handler)(int log_level, const char *str);

static void redirect_logs(int fd)
{
	assert(fd >= 0);
	if (dup2(fd, STDOUT_FILENO) < 0)
		MPD_ERROR("problems dup2 stdout : %s\n", strerror(errno));
	if (dup2(fd, STDERR_FILENO) < 0)
		MPD_ERROR("problems dup2 stderr : %s\n", strerror(errno));
}

static const char *log_date(void)
{
	static char buf[LOG_DATE_BUF_SIZE];
	time_t t = time(NULL);
	strftime(buf, LOG_DATE_BUF_SIZE, "%b %d %H:%M : ", localtime(&t));
	return buf;
}

/**
 * Determines the length of the string excluding trailing whitespace
 * characters.
 */
static int
chomp_length(const char *p)
{
	size_t length = strlen(p);

	while (length > 0 && isspace(p[length - 1]))
		--length;

	return (int)length;
}

static void
file_log_func(int log_level, const char *str)
{
	char *converted;

	if (log_level > log_threshold)
		return;

	if (log_charset != NULL) {
		converted = g_convert_with_fallback(str, -1,
						    log_charset, "utf-8",
						    NULL, NULL, NULL, NULL);
		if (converted != NULL)
			str = converted;
	} else
		converted = NULL;

	fprintf(stderr, "%s%.*s\n",
		stdout_mode ? "" : log_date(),
		chomp_length(str), str);

	free(converted);
}

static inline void
log_init_stdout(void)
{
	log_handler = file_log_func;
}

static int
open_log_file(void)
{
	assert(out_filename != NULL);

	return open_cloexec(out_filename, O_CREAT | O_WRONLY | O_APPEND, 0666);
}

static bool
log_init_file(unsigned line)
{
	assert(out_filename != NULL);

	out_fd = open_log_file();
	if (out_fd < 0) {
		fprintf(stderr, "log: failed to open log file \"%s\" (config line %u): %s",
			    out_filename, line, strerror(errno));
		return false;
	}

	log_handler = file_log_func;
	return true;
}

#ifdef HAVE_SYSLOG
static void
syslog_log_func(int log_level, const char *str)
{
	if (stdout_mode) {
		/* fall back to the file log function during
		   startup */
		file_log_func(log_level, str);
		return;
	}

	if (log_level > log_threshold)
		return;

	syslog(log_level, "%.*s", chomp_length(str), str);
}

static void
log_init_syslog(void)
{
	assert(out_filename == NULL);

	openlog(PACKAGE, 0, LOG_DAEMON);
	log_handler = syslog_log_func;
}
#endif

static inline int
parse_log_level(const char *value, unsigned line)
{
	if (0 == strcmp(value, "default"))
		return LOG_INFO;
	if (0 == strcmp(value, "secure"))
		return LOG_LEVEL_SECURE;
	else if (0 == strcmp(value, "verbose"))
		return LOG_DEBUG;
	else {
		MPD_ERROR("unknown log level \"%s\" at line %u\n",
			  value, line);
		return LOG_NOTICE;
	}
}

void
log_early_init(bool verbose)
{
	if (verbose)
		log_threshold = LOG_DEBUG;

	log_init_stdout();
}

bool
log_init(bool verbose, bool use_stdout)
{
	const struct config_param *param;

	g_get_charset(&log_charset);

	if (verbose)
		log_threshold = LOG_DEBUG;
	else if ((param = config_get_param(CONF_LOG_LEVEL)) != NULL)
		log_threshold = parse_log_level(param->value, param->line);

	if (use_stdout) {
		log_init_stdout();
		return true;
	} else {
		param = config_get_param(CONF_LOG_FILE);
		if (param == NULL) {
#ifdef HAVE_SYSLOG
			/* no configuration: default to syslog (if
			   available) */
			log_init_syslog();
			return true;
#else
			fprintf(stderr, "log: config parameter \"%s\" not found",
				    CONF_LOG_FILE);
			return false;
#endif
#ifdef HAVE_SYSLOG
		} else if (strcmp(param->value, "syslog") == 0) {
			log_init_syslog();
			return true;
#endif
		} else {
			out_filename = config_dup_path(CONF_LOG_FILE);
			return out_filename != NULL &&
				log_init_file(param->line);
		}
	}
}

static void
close_log_files(void)
{
	if (stdout_mode)
		return;

	if(out_fd >= 0)
		close(out_fd);

#ifdef HAVE_SYSLOG
	if (out_filename == NULL)
		closelog();
#endif
}

void
log_deinit(void)
{
	close_log_files();
	free(out_filename);
	out_filename = NULL;
	out_fd = -1;
}


void setup_log_output(bool use_stdout)
{
	fflush(NULL);
	if (!use_stdout) {
#ifndef WIN32
		if (out_filename == NULL)
			out_fd = open("/dev/null", O_WRONLY);
#endif

		if (out_fd >= 0) {
			redirect_logs(out_fd);
			close(out_fd);
		}

		stdout_mode = false;
		log_charset = NULL;
	}
}

int cycle_log_files(void)
{
	int fd;

	if (stdout_mode || out_filename == NULL)
		return 0;
	assert(out_filename);

	log_debug("Cycling log files...\n");
	close_log_files();

	fd = open_log_file();
	if (fd < 0) {
		log_warning("error re-opening log file: %s\n", out_filename);
		return -1;
	}

	redirect_logs(fd);
	log_debug("Done cycling log files\n");
	return 0;
}
