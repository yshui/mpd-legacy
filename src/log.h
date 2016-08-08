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

#ifndef MPD_LOG_H
#define MPD_LOG_H

#ifndef LOG_DOMAIN
# define LOG_PREFIX "(missing log domain, file: " __BASE_FILE__ ") "
#else
# define LOG_PREFIX (LOG_DOMAIN ": ")
#endif

#include "macros.h"
#include "err.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_SYSLOG
#include <syslog.h>
#else
//Some loglevel definitions
#define LOG_EMERG	0
#define LOG_ALERT	1
#define LOG_CRIT	2
#define LOG_ERR		3
#define LOG_WARNING	4
#define LOG_NOTICE	5
#define LOG_INFO	6
#define LOG_DEBUG	7
#endif


/**
 * Configure a logging destination for daemon startup, before the
 * configuration file is read.  This allows the daemon to use the
 * logging library (and the command line verbose level) before it's
 * daemonized.
 *
 * @param verbose true when the program is started with --verbose
 */

extern void (*log_handler)(int log_level, const char *str);

void
log_early_init(bool verbose);

bool
log_init(bool verbose, bool use_stdout);

void
log_deinit(void);

void setup_log_output(bool use_stdout);

int cycle_log_files(void);

static inline void log_metav(int log_level, const char *fmt, va_list args) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
	char *buf;
	vasprintf(&buf, fmt, args);

	char *buf2 = (char *)malloc(strlen(buf)+strlen(LOG_PREFIX)+1);
	strcpy(buf2, LOG_PREFIX);
	strcpy(buf2+strlen(LOG_PREFIX), buf);

	log_handler(log_level, buf2);
	free(buf);
	free(buf2);
#pragma GCC diagnostic pop
}
static inline void __attribute__ ((format(printf, 2, 3)))
log_meta(int log_level, const char *fmt, ...){
	char *buf;
	va_list args;
	va_start(args, fmt);

	vasprintf(&buf, fmt, args);
	char *buf2 = (char *)malloc(strlen(buf)+strlen(LOG_PREFIX)+1);
	strcpy(buf2, LOG_PREFIX);
	strcpy(buf2+strlen(LOG_PREFIX), buf);

	log_handler(log_level, buf2);
	free(buf);
	free(buf2);
}

#define log_debug(...) log_meta(LOG_DEBUG, __VA_ARGS__)
#define log_info(...) log_meta(LOG_INFO, __VA_ARGS__)
#define log_warning(...) log_meta(LOG_WARNING, __VA_ARGS__)
#define log_err(...) log_meta(LOG_ERR, __VA_ARGS__)

#endif /* LOG_H */
