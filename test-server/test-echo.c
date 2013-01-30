/*
 * libwebsockets-test-echo - libwebsockets echo test implementation
 *
 * This implements both the client and server sides.  It defaults to
 * serving, use --client <remote address> to connect as client.
 *
 * Copyright (C) 2010-2013 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <syslog.h>
#include <signal.h>

#include "../lib/libwebsockets.h"

int force_exit = 0;

#define MAX_ECHO_PAYLOAD 1400
#define LOCAL_RESOURCE_PATH INSTALL_DATADIR"/libwebsockets-test-server"

struct per_session_data__echo {
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MAX_ECHO_PAYLOAD + LWS_SEND_BUFFER_POST_PADDING];
	unsigned int len;
	unsigned int index;
};

static int
callback_echo(struct libwebsocket_context *context,
		struct libwebsocket *wsi,
		enum libwebsocket_callback_reasons reason, void *user,
							   void *in, size_t len)
{
	struct per_session_data__echo *pss = (struct per_session_data__echo *)user;
	int n;

	switch (reason) {

	/* when the callback is used for server operations --> */

	case LWS_CALLBACK_SERVER_WRITEABLE:
		n = libwebsocket_write(wsi, &pss->buf[LWS_SEND_BUFFER_PRE_PADDING], pss->len, LWS_WRITE_TEXT);
		if (n < 0) {
			lwsl_err("ERROR %d writing to socket, hanging up\n", n);
			return 1;
		}
		break;

	case LWS_CALLBACK_RECEIVE:
		if (len > MAX_ECHO_PAYLOAD) {
			lwsl_err("Server received packet bigger than %u, hanging up\n", MAX_ECHO_PAYLOAD);
			return 1;
		}
		memcpy(&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], in, len);
		pss->len = len;
		libwebsocket_callback_on_writable(context, wsi);
		break;

	/* when the callback is used for client operations --> */

	case LWS_CALLBACK_CLIENT_ESTABLISHED:
		lwsl_notice("Client has connected\n");
		pss->index = 0;
		break;

	case LWS_CALLBACK_CLIENT_RECEIVE:
		lwsl_notice("Client RX: %s", (char *)in);
		break;

	case LWS_CALLBACK_CLIENT_WRITEABLE:
		/* we will send our packet... */
		pss->len = sprintf((char *)&pss->buf[LWS_SEND_BUFFER_PRE_PADDING], "hello from libwebsockets-test-echo client pid %d index %d\n", getpid(), pss->index++);
		lwsl_notice("Client TX: %s", &pss->buf[LWS_SEND_BUFFER_PRE_PADDING]);
		n = libwebsocket_write(wsi, &pss->buf[LWS_SEND_BUFFER_PRE_PADDING], pss->len, LWS_WRITE_TEXT);
		if (n < 0) {
			lwsl_err("ERROR %d writing to socket, hanging up\n", n);
			return 1;
		}
		break;

	default:
		break;
	}

	return 0;
}



static struct libwebsocket_protocols protocols[] = {
	/* first protocol must always be HTTP handler */

	{
		"default",		/* name */
		callback_echo,		/* callback */
		sizeof(struct per_session_data__echo)	/* per_session_data_size */
	},
	{
		NULL, NULL, 0		/* End of list */
	}
};

void sighandler(int sig)
{
	force_exit = 1;
}

static struct option options[] = {
	{ "help",	no_argument,		NULL, 'h' },
	{ "debug",	required_argument,	NULL, 'd' },
	{ "port",	required_argument,	NULL, 'p' },
	{ "client",	required_argument,	NULL, 'c' },
	{ "ratems",	required_argument,	NULL, 'r' },
	{ "ssl",	no_argument,		NULL, 's' },
	{ "interface",  required_argument,	NULL, 'i' },
#ifndef LWS_NO_DAEMONIZE
	{ "daemonize", 	no_argument,		NULL, 'D' },
#endif
	{ NULL, 0, 0, 0 }
};

int main(int argc, char **argv)
{
	int n = 0;
	const char *cert_path =
			    LOCAL_RESOURCE_PATH"/libwebsockets-test-server.pem";
	const char *key_path =
			LOCAL_RESOURCE_PATH"/libwebsockets-test-server.key.pem";
	int port = 7681;
	int use_ssl = 0;
	struct libwebsocket_context *context;
	struct libwebsocket *wsi;
	int opts = 0;
	char interface_name[128] = "";
	const char *interface = NULL;
	int syslog_options = LOG_PID | LOG_PERROR;
	unsigned int oldus = 0;
	int client = 0;
	int listen_port;
	char address[256];
	int rate_us = 250000;

	int debug_level = 7;
#ifndef LWS_NO_DAEMONIZE
	int daemonize = 0;
#endif

	while (n >= 0) {
		n = getopt_long(argc, argv, "ci:hsp:d:Dr:", options, NULL);
		if (n < 0)
			continue;
		switch (n) {
#ifndef LWS_NO_DAEMONIZE
		case 'D':
			daemonize = 1;
			syslog_options &= ~LOG_PERROR;
			break;
#endif
		case 'c':
			client = 1;
			strcpy(address, optarg);
			port = 80;
			break;
		case 'd':
			debug_level = atoi(optarg);
			break;
		case 'r':
			rate_us = atoi(optarg) * 1000;
			break;
		case 's':
			use_ssl = 1; /* 1 = take care about cert verification, 2 = allow anything */
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'i':
			strncpy(interface_name, optarg, sizeof interface_name);
			interface_name[(sizeof interface_name) - 1] = '\0';
			interface = interface_name;
			break;
		case 'h':
			fprintf(stderr, "Usage: libwebsockets-test-echo "
					"[--ssl] [--client <remote ads>] [--port=<p>] "
					"[--ratems <ms>] "
					"[-d <log bitfield>]\n");
			exit(1);
		}
	}

#ifndef LWS_NO_DAEMONIZE
	/*
	 * normally lock path would be /var/lock/lwsts or similar, to
	 * simplify getting started without having to take care about
	 * permissions or running as root, set to /tmp/.lwsts-lock
	 */
	if (!client && daemonize && lws_daemonize("/tmp/.lwsts-lock")) {
		fprintf(stderr, "Failed to daemonize\n");
		return 1;
	}
#endif

	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	lwsl_notice("libwebsockets echo client + server - "
			"(C) Copyright 2010-2013 Andy Green <andy@warmcat.com> - "
						    "licensed under LGPL2.1\n");
	if (!use_ssl || client)
		cert_path = key_path = NULL;

	if (client) {
		lwsl_notice("Running in client mode\n");
		listen_port = CONTEXT_PORT_NO_LISTEN;
		if (use_ssl)
			use_ssl = 2;
	} else {
		lwsl_notice("Running in server mode\n");
		listen_port = port;
	}
	context = libwebsocket_create_context(listen_port, interface, protocols,
#ifndef LWS_NO_EXTENSIONS
				libwebsocket_internal_extensions,
#else
				NULL,
#endif
				cert_path, key_path, NULL, -1, -1, opts, NULL);

	if (context == NULL) {
		lwsl_err("libwebsocket init failed\n");
		return -1;
	}

	if (client) {
		lwsl_notice("Client connecting to %s:%u....\n", address, port);
		/* we are in client mode */
		wsi = libwebsocket_client_connect(context, address,
				port, use_ssl, "/", address,
				 "origin", NULL, -1);
		if (!wsi) {
			lwsl_err("Client failed to connect to %s:%u\n", address, port);
			goto bail;
		}
		lwsl_notice("Client connected to %s:%u\n", address, port);
	}

	signal(SIGINT, sighandler);

	n = 0;
	while (n >= 0 && !force_exit) {
		struct timeval tv;

		if (client) {
			gettimeofday(&tv, NULL);

			if (((unsigned int)tv.tv_usec - oldus) > rate_us) {
				libwebsocket_callback_on_writable_all_protocol(&protocols[0]);
				oldus = tv.tv_usec;
			}
		}
		n = libwebsocket_service(context, 10);
	}
bail:
	libwebsocket_context_destroy(context);

	lwsl_notice("libwebsockets-test-echo exited cleanly\n");

	closelog();

	return 0;
}
