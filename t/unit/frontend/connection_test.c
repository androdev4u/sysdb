/*
 * SysDB - t/unit/frontend/connection_test.c
 * Copyright (C) 2014 Sebastian 'tokkee' Harl <sh@tokkee.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#if HAVE_CONFIG_H
#	include "config.h"
#endif /* HAVE_CONFIG_H */

#include "frontend/connection.h"
#include "frontend/connection-private.h"
#include "libsysdb_test.h"

#include "utils/strbuf.h"

#include <check.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

/*
 * private helper functions
 */

static void
mock_conn_destroy(sdb_conn_t *conn)
{
	if (SDB_OBJ(conn)->name)
		free(SDB_OBJ(conn)->name);
	sdb_strbuf_destroy(conn->buf);
	sdb_strbuf_destroy(conn->errbuf);
	if (conn->fd >= 0)
		close(conn->fd);
	free(conn);
} /* mock_conn_destroy */

static sdb_conn_t *
mock_conn_create(void)
{
	sdb_conn_t *conn;

	char tmp_file[] = "connection_test_socket.XXXXXX";

	conn = calloc(1, sizeof(*conn));
	if (! conn) {
		fail("INTERNAL ERROR: failed to allocate connection object");
		return NULL;
	}

	SDB_OBJ(conn)->name = strdup("mock_connection");
	SDB_OBJ(conn)->ref_cnt = 1;

	conn->buf = sdb_strbuf_create(0);
	conn->errbuf = sdb_strbuf_create(0);
	if ((! conn->buf) || (! conn->errbuf)) {
		mock_conn_destroy(conn);
		fail("INTERNAL ERROR: failed to allocate connection object");
		return NULL;
	}

	conn->fd = mkstemp(tmp_file);
	if (conn->fd < 0) {
		mock_conn_destroy(conn);
		fail("INTERNAL ERROR: failed to allocate connection object");
		return NULL;
	}

	unlink(tmp_file);

	conn->cmd = CONNECTION_IDLE;
	conn->cmd_len = 0;
	return conn;
} /* mock_conn_create */

static void
mock_conn_rewind(sdb_conn_t *conn)
{
	lseek(conn->fd, 0, SEEK_SET);
} /* mock_conn_rewind */

static void
mock_conn_truncate(sdb_conn_t *conn)
{
	lseek(conn->fd, 0, SEEK_SET);
	ftruncate(conn->fd, 0);
} /* mock_conn_truncate */

static int
mock_unixsock_listener(char *sock_path)
{
	struct sockaddr_un sa;
	char *filename;
	int fd, status;

	filename = tmpnam(sock_path);
	fail_unless(filename != NULL,
			"INTERNAL ERROR: tmpnam() = NULL; expected: a string");

	fd = socket(AF_UNIX, SOCK_STREAM, 0);
	fail_unless(fd >= 0,
			"INTERNAL ERROR: socket() = %d; expected: >=0", fd);

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, filename, sizeof(sa.sun_path));

	status = bind(fd, (struct sockaddr *)&sa, sizeof(sa));
	fail_unless(status == 0,
			"INTERNAL ERROR: bind() = %d; expected: 0", status);
	status = listen(fd, 32);
	fail_unless(status == 0,
			"INTERNAL ERROR: listen() = %d; expected: 0", status);

	return fd;
} /* mock_unixsock */

static void *
mock_client(void *arg)
{
	char *socket_path = arg;

	struct sockaddr_un sa;
	int fd, check;

	fd = socket(AF_UNIX, SOCK_STREAM, /* protocol = */ 0);
	fail_unless(fd >= 0,
			"INTERNAL ERROR: socket() = %d; expected: >= 0", fd);

	memset(&sa, 0, sizeof(sa));
	sa.sun_family = AF_UNIX;
	strncpy(sa.sun_path, socket_path, sizeof(sa.sun_path));

	check = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	fail_unless(check == 0,
			"INTERNAL ERROR: connect() = %d; expected: 0", check);

	close(fd);
	return NULL;
} /* mock_client */

/*
 * tests
 */

START_TEST(test_conn_accept)
{
	char socket_path[L_tmpnam];
	int fd, check;

	sdb_conn_t *conn;

	pthread_t thr;

	conn = sdb_connection_accept(-1);
	fail_unless(conn == NULL,
			"sdb_connection_accept(-1) = %p; expected: NULL", conn);

	memset(&socket_path, 0, sizeof(socket_path));
	fd = mock_unixsock_listener(socket_path);
	check = pthread_create(&thr, /* attr = */ NULL, mock_client, socket_path);
	fail_unless(check == 0,
			"INTERNAL ERROR: pthread_create() = %i; expected: 0", check);

	conn = sdb_connection_accept(fd);
	fail_unless(conn != NULL,
			"sdb_connection_accept(%d) = %p; expected: <conn>", fd, conn);

	unlink(socket_path);
	sdb_connection_close(conn);
	pthread_join(thr, NULL);
}
END_TEST

START_TEST(test_conn_setup)
{
	sdb_conn_t *conn = mock_conn_create();

	struct {
		uint32_t code;
		const char *msg;
		const char *err;
	} golden_data[] = {
		{ UINT32_MAX,         NULL,       NULL },
		{ CONNECTION_IDLE,    "fakedata", NULL },
		{ CONNECTION_PING,    NULL,       "Authentication required" },
		{ CONNECTION_STARTUP, "fakeuser", NULL },
		{ CONNECTION_PING,    NULL,       NULL },
		{ CONNECTION_IDLE,    NULL,       NULL },
		{ CONNECTION_PING,    "fakedata", NULL },
		{ CONNECTION_IDLE,    NULL,       NULL },
	};

	size_t i;

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		ssize_t check, expected = 0;

		mock_conn_truncate(conn);

		if (golden_data[i].code != UINT32_MAX) {
			expected = 2 * sizeof(uint32_t)
				+ (golden_data[i].msg ? strlen(golden_data[i].msg) : 0);

			check = sdb_connection_send(conn, golden_data[i].code,
					(uint32_t)(golden_data[i].msg
						? strlen(golden_data[i].msg) : 0),
					golden_data[i].msg);
			fail_unless(check == expected,
					"sdb_connection_send(%d, %s) = %zi; expected: %zi",
					golden_data[i].code,
					golden_data[i].msg ? golden_data[i].msg : "<null>",
					check, expected);
		}

		mock_conn_rewind(conn);
		check = sdb_connection_read(conn);
		fail_unless(check == expected,
				"sdb_connection_read() = %zi; expected: %zi",
				check, expected);

		fail_unless(sdb_strbuf_len(conn->buf) == 0,
				"sdb_connection_read() left %zu bytes in the buffer; "
				"expected: 0", sdb_strbuf_len(conn->buf));

		if (golden_data[i].err) {
			const char *err = sdb_strbuf_string(conn->errbuf);
			fail_unless(strcmp(err, golden_data[i].err) == 0,
					"sdb_connection_read(): got error '%s'; "
					"expected: '%s'", err, golden_data[i].err);
		}
		else
			fail_unless(sdb_strbuf_len(conn->errbuf) == 0,
					"sdb_connection_read() left %zu bytes in the error "
					"buffer; expected: 0", sdb_strbuf_len(conn->errbuf));
	}

	mock_conn_destroy(conn);
}
END_TEST

Suite *
fe_conn_suite(void)
{
	Suite *s = suite_create("frontend::connection");
	TCase *tc;

	tc = tcase_create("core");
	tcase_add_test(tc, test_conn_accept);
	tcase_add_test(tc, test_conn_setup);
	suite_add_tcase(s, tc);

	return s;
} /* fe_conn_suite */

/* vim: set tw=78 sw=4 ts=4 noexpandtab : */
