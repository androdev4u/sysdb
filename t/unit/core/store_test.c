/*
 * SysDB - t/unit/core/store_test.c
 * Copyright (C) 2013 Sebastian 'tokkee' Harl <sh@tokkee.org>
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

#include "core/store.h"
#include "core/store-private.h"
#include "libsysdb_test.h"

#include <check.h>
#include <string.h>

static void
populate(void)
{
	sdb_data_t datum;

	sdb_store_host("h1", 1);
	sdb_store_host("h2", 3);

	datum.type = SDB_TYPE_STRING;
	datum.data.string = "v1";
	sdb_store_attribute("h1", "k1", &datum, 1);
	datum.data.string = "v2";
	sdb_store_attribute("h1", "k2", &datum, 2);
	datum.data.string = "v3";
	sdb_store_attribute("h1", "k3", &datum, 2);

	/* make sure that older updates don't overwrite existing values */
	datum.data.string = "fail";
	sdb_store_attribute("h1", "k2", &datum, 1);
	sdb_store_attribute("h1", "k3", &datum, 2);

	sdb_store_metric("h1", "m1", /* store */ NULL, 2);
	sdb_store_metric("h1", "m2", /* store */ NULL, 1);

	sdb_store_service("h2", "s1", 1);
	sdb_store_service("h2", "s2", 2);

	datum.type = SDB_TYPE_INTEGER;
	datum.data.integer = 42;
	sdb_store_metric_attr("h1", "m1", "k3", &datum, 2);

	datum.data.integer = 123;
	sdb_store_service_attr("h2", "s2", "k1", &datum, 2);
	datum.data.integer = 4711;
	sdb_store_service_attr("h2", "s2", "k2", &datum, 1);

	/* don't overwrite k1 */
	datum.data.integer = 666;
	sdb_store_service_attr("h2", "s2", "k1", &datum, 2);
} /* populate */

START_TEST(test_store_host)
{
	struct {
		const char *name;
		sdb_time_t  last_update;
		int         expected;
	} golden_data[] = {
		{ "a", 1, 0 },
		{ "a", 2, 0 },
		{ "a", 1, 1 },
		{ "b", 1, 0 },
		{ "b", 1, 1 },
		{ "A", 1, 1 }, /* case-insensitive */
		{ "A", 3, 0 },
	};

	struct {
		const char *name;
		_Bool       has;
	} golden_hosts[] = {
		{ "a", 1 == 1 },
		{ "b", 1 == 1 },
		{ "c", 0 == 1 },
		{ "A", 1 == 1 },
	};

	size_t i;

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		int status;

		status = sdb_store_host(golden_data[i].name,
				golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_host(%s, %d) = %d; expected: %d",
				golden_data[i].name, (int)golden_data[i].last_update,
				status, golden_data[i].expected);
	}

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_hosts); ++i) {
		_Bool has;

		has = sdb_store_has_host(golden_hosts[i].name);
		fail_unless(has == golden_hosts[i].has,
				"sdb_store_has_host(%s) = %d; expected: %d",
				golden_hosts[i].name, has, golden_hosts[i].has);
	}
}
END_TEST

START_TEST(test_store_get_host)
{
	char *golden_hosts[] = { "a", "b", "c" };
	char *unknown_hosts[] = { "x", "y", "z" };
	size_t i;

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_hosts); ++i) {
		int status = sdb_store_host(golden_hosts[i], 1);
		fail_unless(status >= 0,
				"sdb_store_host(%s) = %d; expected: >=0",
				golden_hosts[i], status);
	}

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_hosts); ++i) {
		sdb_store_obj_t *sobj1, *sobj2;
		int ref_cnt;

		fail_unless(sdb_store_has_host(golden_hosts[i]),
				"sdb_store_has_host(%s) = FALSE; expected: TRUE",
				golden_hosts[i]);

		sobj1 = sdb_store_get_host(golden_hosts[i]);
		fail_unless(sobj1 != NULL,
				"sdb_store_get_host(%s) = NULL; expected: <host>",
				golden_hosts[i]);
		ref_cnt = SDB_OBJ(sobj1)->ref_cnt;

		fail_unless(ref_cnt > 1,
				"sdb_store_get_host(%s) did not increment ref count: "
				"got: %d; expected: >1", golden_hosts[i], ref_cnt);

		sobj2 = sdb_store_get_host(golden_hosts[i]);
		fail_unless(sobj2 != NULL,
				"sdb_store_get_host(%s) = NULL; expected: <host>",
				golden_hosts[i]);

		fail_unless(sobj1 == sobj2,
				"sdb_store_get_host(%s) returned different objects "
				"in successive calls", golden_hosts[i]);
		fail_unless(SDB_OBJ(sobj2)->ref_cnt == ref_cnt + 1,
				"sdb_store_get_hosts(%s) did not increment ref count "
				"(first call: %d; second call: %d)",
				golden_hosts[i], ref_cnt, SDB_OBJ(sobj2)->ref_cnt);

		sdb_object_deref(SDB_OBJ(sobj1));
		sdb_object_deref(SDB_OBJ(sobj2));
	}
	for (i = 0; i < SDB_STATIC_ARRAY_LEN(unknown_hosts); ++i) {
		sdb_store_obj_t *sobj;

		fail_unless(!sdb_store_has_host(unknown_hosts[i]),
				"sdb_store_has_host(%s) = TRUE; expected: FALSE",
				unknown_hosts[i]);

		sobj = sdb_store_get_host(unknown_hosts[i]);
		fail_unless(!sobj, "sdb_store_get_host(%s) = <host:%s>; expected: NULL",
				unknown_hosts[i], sobj ? SDB_OBJ(sobj)->name : "NULL");
	}
}
END_TEST

START_TEST(test_store_attr)
{
	struct {
		const char *host;
		const char *key;
		char       *value;
		sdb_time_t  last_update;
		int         expected;
	} golden_data[] = {
		{ "k", "k",  "v",  1, -1 },
		{ "k", "k",  "v",  1, -1 }, /* retry to ensure the host is not created */
		{ "l", "k1", "v1", 1,  0 },
		{ "l", "k1", "v2", 2,  0 },
		{ "l", "k1", "v3", 2,  1 },
		{ "l", "k2", "v1", 1,  0 },
		{ "m", "k",  "v1", 1,  0 },
		{ "m", "k",  "v2", 1,  1 },
	};

	size_t i;

	sdb_store_host("l", 1);
	sdb_store_host("m", 1);
	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		sdb_data_t datum;
		int status;

		/* XXX: test other types as well */
		datum.type = SDB_TYPE_STRING;
		datum.data.string = golden_data[i].value;

		status = sdb_store_attribute(golden_data[i].host,
				golden_data[i].key, &datum,
				golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_attribute(%s, %s, %s, %d) = %d; expected: %d",
				golden_data[i].host, golden_data[i].key, golden_data[i].value,
				golden_data[i].last_update, status, golden_data[i].expected);
	}
}
END_TEST

START_TEST(test_store_metric)
{
	sdb_metric_store_t store1 = { "dummy-type1", "dummy-id1" };
	sdb_metric_store_t store2 = { "dummy-type2", "dummy-id2" };

	struct {
		const char *host;
		const char *metric;
		sdb_metric_store_t *store;
		sdb_time_t  last_update;
		int         expected;
	} golden_data[] = {
		{ "k", "m",  NULL,    1, -1 },
		{ "k", "m",  NULL,    1, -1 }, /* retry to ensure the host is not created */
		{ "k", "m",  &store1, 1, -1 },
		{ "l", "m1", NULL,    1,  0 },
		{ "l", "m1", &store1, 2,  0 },
		{ "l", "m1", &store1, 3,  0 },
		{ "l", "m1", NULL,    3,  1 },
		{ "l", "m2", &store1, 1,  0 },
		{ "l", "m2", &store2, 2,  0 },
		{ "l", "m2", NULL,    3,  0 },
		{ "m", "m",  &store1, 1,  0 },
		{ "m", "m",  NULL,    2,  0 },
		{ "m", "m",  NULL,    2,  1 },
		{ "m", "m",  &store1, 3,  0 },
		{ "m", "m",  &store2, 4,  0 },
		{ "m", "m",  NULL,    5,  0 },
	};

	size_t i;

	sdb_store_host("m", 1);
	sdb_store_host("l", 1);
	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		int status;

		status = sdb_store_metric(golden_data[i].host,
				golden_data[i].metric, golden_data[i].store,
				golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_metric(%s, %s, %p, %d) = %d; expected: %d",
				golden_data[i].host, golden_data[i].metric,
				golden_data[i].store, golden_data[i].last_update,
				status, golden_data[i].expected);
	}
}
END_TEST

START_TEST(test_store_metric_attr)
{
	struct {
		const char *host;
		const char *metric;
		const char *attr;
		const sdb_data_t value;
		sdb_time_t  last_update;
		int expected;
	} golden_data[] = {
		{ "k", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		/* retry, it should still fail */
		{ "k", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		{ "l", "mX", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		/* retry, it should still fail */
		{ "l", "mX", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		{ "l", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "l", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  1 },
		{ "l", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 2,  0 },
		{ "l", "m1", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "l", "m1", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  1 },
		{ "l", "m2", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "m", "m1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
	};

	size_t i;

	sdb_store_host("m", 1);
	sdb_store_host("l", 1);
	sdb_store_metric("m", "m1", NULL, 1);
	sdb_store_metric("l", "m1", NULL, 1);
	sdb_store_metric("l", "m2", NULL, 1);

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		int status;

		status = sdb_store_metric_attr(golden_data[i].host,
				golden_data[i].metric, golden_data[i].attr,
				&golden_data[i].value, golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_metric_attr(%s, %s, %s, %d, %d) = %d; "
				"expected: %d", golden_data[i].host, golden_data[i].metric,
				golden_data[i].attr, golden_data[i].value.data.integer,
				golden_data[i].last_update, status, golden_data[i].expected);
	}
}
END_TEST

START_TEST(test_store_service)
{
	struct {
		const char *host;
		const char *svc;
		sdb_time_t  last_update;
		int         expected;
	} golden_data[] = {
		{ "k", "s",  1, -1 },
		{ "k", "s",  1, -1 }, /* retry to ensure the host is not created */
		{ "l", "s1", 1,  0 },
		{ "l", "s1", 2,  0 },
		{ "l", "s1", 2,  1 },
		{ "l", "s2", 1,  0 },
		{ "m", "s",  1,  0 },
		{ "m", "s",  1,  1 },
	};

	size_t i;

	sdb_store_host("m", 1);
	sdb_store_host("l", 1);
	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		int status;

		status = sdb_store_service(golden_data[i].host,
				golden_data[i].svc, golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_service(%s, %s, %d) = %d; expected: %d",
				golden_data[i].host, golden_data[i].svc,
				golden_data[i].last_update, status, golden_data[i].expected);
	}
}
END_TEST

START_TEST(test_store_service_attr)
{
	struct {
		const char *host;
		const char *svc;
		const char *attr;
		const sdb_data_t value;
		sdb_time_t  last_update;
		int expected;
	} golden_data[] = {
		{ "k", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		/* retry, it should still fail */
		{ "k", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		{ "l", "sX", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		/* retry, it should still fail */
		{ "l", "sX", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1, -1 },
		{ "l", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "l", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  1 },
		{ "l", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 2,  0 },
		{ "l", "s1", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "l", "s1", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  1 },
		{ "l", "s2", "a2", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
		{ "m", "s1", "a1", { SDB_TYPE_INTEGER, { .integer = 123 } }, 1,  0 },
	};

	size_t i;

	sdb_store_host("m", 1);
	sdb_store_host("l", 1);
	sdb_store_service("m", "s1", 1);
	sdb_store_service("l", "s1", 1);
	sdb_store_service("l", "s2", 1);

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		int status;

		status = sdb_store_service_attr(golden_data[i].host,
				golden_data[i].svc, golden_data[i].attr,
				&golden_data[i].value, golden_data[i].last_update);
		fail_unless(status == golden_data[i].expected,
				"sdb_store_service_attr(%s, %s, %s, %d, %d) = %d; "
				"expected: %d", golden_data[i].host, golden_data[i].svc,
				golden_data[i].attr, golden_data[i].value.data.integer,
				golden_data[i].last_update, status, golden_data[i].expected);
	}
}
END_TEST

static void
verify_json_output(sdb_strbuf_t *buf, const char *expected,
		sdb_store_matcher_t *filter, int flags)
{
	int pos;
	size_t len1, len2;
	size_t i;

	len1 = strlen(sdb_strbuf_string(buf));
	len2 = strlen(expected);

	pos = -1;
	if (len1 != len2)
		pos = (int)(len1 <= len2 ? len1 : len2);

	for (i = 0; i < (len1 <= len2 ? len1 : len2); ++i) {
		if (sdb_strbuf_string(buf)[i] != expected[i]) {
			pos = (int)i;
			break;
		}
	}

	fail_unless(pos == -1,
			"sdb_store_tojson(<buf>, %p, %x) returned unexpected result\n"
			"         got: %s\n              %*s\n    expected: %s",
			filter, flags, sdb_strbuf_string(buf), pos + 1, "^",
			expected);
} /* verify_json_output */

START_TEST(test_store_tojson)
{
	sdb_strbuf_t *buf;
	size_t i;

	struct {
		struct {
			sdb_store_matcher_t *(*m)(sdb_store_expr_t *,
					sdb_store_expr_t *);
			int field;
			sdb_data_t value;
		} filter;
		int flags;
		const char *expected;
	} golden_data[] = {
		{ { NULL, 0, SDB_DATA_INIT }, 0,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": ["
						"{\"name\": \"k1\", \"value\": \"v1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k2\", \"value\": \"v2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k3\", \"value\": \"v3\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []}"
					"], "
					"\"metrics\": ["
						"{\"name\": \"m1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": ["
								"{\"name\": \"k3\", \"value\": 42, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []}"
							"]},"
						"{\"name\": \"m2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": []}"
					"], "
					"\"services\": []},"
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], "
					"\"metrics\": [], "
					"\"services\": ["
						"{\"name\": \"s1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": []},"
						"{\"name\": \"s2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": ["
								"{\"name\": \"k1\", \"value\": 123, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []},"
								"{\"name\": \"k2\", \"value\": 4711, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []}"
							"]}"
					"]}"
			"]" },
		{ { NULL, 0, SDB_DATA_INIT }, SDB_SKIP_SERVICES,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": ["
						"{\"name\": \"k1\", \"value\": \"v1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k2\", \"value\": \"v2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k3\", \"value\": \"v3\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []}"
					"], "
					"\"metrics\": ["
						"{\"name\": \"m1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": ["
								"{\"name\": \"k3\", \"value\": 42, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []}"
							"]},"
						"{\"name\": \"m2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": []}"
					"]},"
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], "
					"\"metrics\": []}"
			"]" },
		{ { NULL, 0, SDB_DATA_INIT }, SDB_SKIP_METRICS,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": ["
						"{\"name\": \"k1\", \"value\": \"v1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k2\", \"value\": \"v2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"k3\", \"value\": \"v3\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []}"
					"], "
					"\"services\": []},"
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], "
					"\"services\": ["
						"{\"name\": \"s1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": []},"
						"{\"name\": \"s2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": ["
								"{\"name\": \"k1\", \"value\": 123, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []},"
								"{\"name\": \"k2\", \"value\": 4711, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []}"
							"]}"
					"]}"
			"]" },
		{ { NULL, 0, SDB_DATA_INIT }, SDB_SKIP_ATTRIBUTES,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"metrics\": ["
						"{\"name\": \"m1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"m2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []}"
					"], "
					"\"services\": []},"
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"metrics\": [], "
					"\"services\": ["
						"{\"name\": \"s1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
						"{\"name\": \"s2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []}"
					"]}"
			"]" },
		{ { NULL, 0, SDB_DATA_INIT }, SDB_SKIP_ALL,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": []},"
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": []}"
			"]" },
		{ { sdb_store_cmp_eq, SDB_FIELD_NAME,
				{ SDB_TYPE_STRING, { .string = "h1" } } }, 0,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], \"metrics\": [], \"services\": []}"
			"]" },
		{ { sdb_store_cmp_gt, SDB_FIELD_LAST_UPDATE,
				{ SDB_TYPE_DATETIME, { .datetime = 1 } } }, 0,
			"["
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], "
					"\"metrics\": [], "
					"\"services\": ["
						"{\"name\": \"s2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": ["
								"{\"name\": \"k1\", \"value\": 123, "
									"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
									"\"update_interval\": \"0s\", \"backends\": []},"
							"]}"
					"]}"
			"]" },
		{ { sdb_store_cmp_le, SDB_FIELD_LAST_UPDATE,
				{ SDB_TYPE_DATETIME, { .datetime = 1 } } }, 0,
			"["
				"{\"name\": \"h1\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": ["
						"{\"name\": \"k1\", \"value\": \"v1\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": []},"
					"], "
					"\"metrics\": ["
						"{\"name\": \"m2\", "
							"\"last_update\": \"1970-01-01 00:00:00 +0000\", "
							"\"update_interval\": \"0s\", \"backends\": [], "
							"\"attributes\": []}"
					"], "
					"\"services\": []}"
			"]" },
		{ { sdb_store_cmp_ge, SDB_FIELD_LAST_UPDATE,
				{ SDB_TYPE_DATETIME, { .datetime = 3 } } }, 0,
			"["
				"{\"name\": \"h2\", \"last_update\": \"1970-01-01 00:00:00 +0000\", "
					"\"update_interval\": \"0s\", \"backends\": [], "
					"\"attributes\": [], "
					"\"metrics\": [], "
					"\"services\": []}"
			"]" },
	};

	buf = sdb_strbuf_create(0);
	fail_unless(buf != NULL, "INTERNAL ERROR: failed to create string buffer");
	populate();

	for (i = 0; i < SDB_STATIC_ARRAY_LEN(golden_data); ++i) {
		sdb_store_matcher_t *filter = NULL;
		int status;

		sdb_strbuf_clear(buf);

		if (golden_data[i].filter.m) {
			sdb_store_expr_t *field;
			sdb_store_expr_t *value;

			field = sdb_store_expr_fieldvalue(golden_data[i].filter.field);
			fail_unless(field != NULL,
					"INTERNAL ERROR: sdb_store_expr_fieldvalue() = NULL");
			value = sdb_store_expr_constvalue(&golden_data[i].filter.value);
			fail_unless(value != NULL,
					"INTERNAL ERROR: sdb_store_expr_constvalue() = NULL");

			filter = golden_data[i].filter.m(field, value);
			fail_unless(filter != NULL,
					"INTERNAL ERROR: sdb_store_*_matcher() = NULL");

			sdb_object_deref(SDB_OBJ(field));
			sdb_object_deref(SDB_OBJ(value));
		}

		status = sdb_store_tojson(buf, filter, golden_data[i].flags);
		fail_unless(status == 0,
				"sdb_store_tojson(<buf>, %p, %x) = %d; expected: 0",
				filter, golden_data[i].flags, status);

		verify_json_output(buf, golden_data[i].expected,
				filter, golden_data[i].flags);
		sdb_object_deref(SDB_OBJ(filter));
	}
	sdb_strbuf_destroy(buf);
}
END_TEST

START_TEST(test_get_field)
{
	sdb_store_obj_t *host;
	sdb_data_t value = SDB_DATA_INIT;
	int check;

	sdb_store_host("host", 10);
	sdb_store_host("host", 20);

	host = sdb_store_get_host("host");
	fail_unless(host != NULL,
			"INTERNAL ERROR: store doesn't have host after adding it");

	check = sdb_store_get_field(NULL, 0, NULL);
	fail_unless(check < 0,
			"sdb_store_get_field(NULL, 0, NULL) = %d; expected: <0");
	check = sdb_store_get_field(NULL, SDB_FIELD_LAST_UPDATE, NULL);
	fail_unless(check < 0,
			"sdb_store_get_field(NULL, SDB_FIELD_LAST_UPDATE, NULL) = %d; "
			"expected: <0");
	check = sdb_store_get_field(NULL, SDB_FIELD_LAST_UPDATE, &value);
	fail_unless(check < 0,
			"sdb_store_get_field(NULL, SDB_FIELD_LAST_UPDATE, <value>) = %d; "
			"expected: <0");

	check = sdb_store_get_field(host, SDB_FIELD_LAST_UPDATE, NULL);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_LAST_UPDATE, NULL) = %d; "
			"expected: 0");
	/* 'name' is dynamically allocated; make sure it's not leaked even
	 * if there is no result parameter */
	check = sdb_store_get_field(host, SDB_FIELD_NAME, NULL);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_LAST_UPDATE, NULL) = %d; "
			"expected: 0");

	check = sdb_store_get_field(host, SDB_FIELD_NAME, &value);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_NAME, <value>) = "
			"%d; expected: 0");
	fail_unless((value.type == SDB_TYPE_STRING)
			&& (! strcmp(value.data.string, "host")),
			"sdb_store_get_field(<host>, SDB_FIELD_NAME, <value>) "
			"returned value {%d, %s}; expected {%d, host}",
			value.type, value.data.string, SDB_TYPE_STRING);
	sdb_data_free_datum(&value);

	check = sdb_store_get_field(host, SDB_FIELD_LAST_UPDATE, &value);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_LAST_UPDATE, <value>) = "
			"%d; expected: 0");
	fail_unless((value.type == SDB_TYPE_DATETIME)
			&& (value.data.datetime == 20),
			"sdb_store_get_field(<host>, SDB_FIELD_LAST_UPDATE, <value>) "
			"returned value {%d, %lu}; expected {%d, 20}",
			value.type, value.data.datetime, SDB_TYPE_DATETIME);

	check = sdb_store_get_field(host, SDB_FIELD_AGE, &value);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_AGE, <value>) = "
			"%d; expected: 0");
	/* let's assume we're at least in year 1980 ;-) */
	fail_unless((value.type == SDB_TYPE_DATETIME)
			&& (value.data.datetime > 10L * SDB_INTERVAL_YEAR),
			"sdb_store_get_field(<host>, SDB_FIELD_AGE, <value>) "
			"returned value {%d, %lu}; expected {%d, >%lu}",
			value.type, value.data.datetime,
			SDB_TYPE_DATETIME, 10L * SDB_INTERVAL_YEAR);

	check = sdb_store_get_field(host, SDB_FIELD_INTERVAL, &value);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_INTERVAL, <value>) = "
			"%d; expected: 0");
	fail_unless((value.type == SDB_TYPE_DATETIME)
			&& (value.data.datetime == 10),
			"sdb_store_get_field(<host>, SDB_FIELD_INTERVAL, <value>) "
			"returned value {%d, %lu}; expected {%d, 10}",
			value.type, value.data.datetime, SDB_TYPE_DATETIME);

	check = sdb_store_get_field(host, SDB_FIELD_BACKEND, &value);
	fail_unless(check == 0,
			"sdb_store_get_field(<host>, SDB_FIELD_BACKEND, <value>) = "
			"%d; expected: 0");
	/* there are no backends in this test */
	fail_unless((value.type == (SDB_TYPE_ARRAY | SDB_TYPE_STRING))
			&& (value.data.array.length == 0)
			&& (value.data.array.values == NULL),
			"sdb_store_get_field(<host>, SDB_FIELD_BACKEND, <value>) "
			"returned value {%d, %lu, %p}; expected {%d, 0, NULL}",
			value.type, value.data.array.length, value.data.array.values,
			SDB_TYPE_ARRAY | SDB_TYPE_STRING);
}
END_TEST

START_TEST(test_interval)
{
	sdb_store_obj_t *host;

	/* 10 us interval */
	sdb_store_host("host", 10);
	sdb_store_host("host", 20);
	sdb_store_host("host", 30);
	sdb_store_host("host", 40);

	host = sdb_store_get_host("host");
	fail_unless(host != NULL,
			"INTERNAL ERROR: store doesn't have host after adding it");

	fail_unless(host->interval == 10,
			"sdb_store_host() did not calculate interval correctly: "
			"got: %"PRIsdbTIME"; expected: %"PRIsdbTIME, host->interval, 10);

	/* multiple updates for the same timestamp don't modify the interval */
	sdb_store_host("host", 40);
	sdb_store_host("host", 40);
	sdb_store_host("host", 40);
	sdb_store_host("host", 40);

	fail_unless(host->interval == 10,
			"sdb_store_host() changed interval when doing multiple updates "
			"using the same timestamp; got: %"PRIsdbTIME"; "
			"expected: %"PRIsdbTIME, host->interval, 10);

	/* multiple updates using an timestamp don't modify the interval */
	sdb_store_host("host", 20);
	sdb_store_host("host", 20);
	sdb_store_host("host", 20);
	sdb_store_host("host", 20);

	fail_unless(host->interval == 10,
			"sdb_store_host() changed interval when doing multiple updates "
			"using an old timestamp; got: %"PRIsdbTIME"; expected: %"PRIsdbTIME,
			host->interval, 10);

	/* new interval: 20 us */
	sdb_store_host("host", 60);
	fail_unless(host->interval == 11,
			"sdb_store_host() did not calculate interval correctly: "
			"got: %"PRIsdbTIME"; expected: %"PRIsdbTIME, host->interval, 11);

	/* new interval: 40 us */
	sdb_store_host("host", 100);
	fail_unless(host->interval == 13,
			"sdb_store_host() did not calculate interval correctly: "
			"got: %"PRIsdbTIME"; expected: %"PRIsdbTIME, host->interval, 11);

	sdb_object_deref(SDB_OBJ(host));
}
END_TEST

static int
iter_incr(sdb_store_obj_t *obj, void *user_data)
{
	intptr_t *i = user_data;

	fail_unless(obj != NULL,
			"sdb_store_iterate callback received NULL obj; expected: "
			"<store base obj>");
	fail_unless(i != NULL,
			"sdb_store_iterate callback received NULL user_data; "
			"expected: <pointer to data>");

	++(*i);
	return 0;
} /* iter_incr */

static int
iter_error(sdb_store_obj_t *obj, void *user_data)
{
	intptr_t *i = user_data;

	fail_unless(obj != NULL,
			"sdb_store_iterate callback received NULL obj; expected: "
			"<store base obj>");
	fail_unless(i != NULL,
			"sdb_store_iterate callback received NULL user_data; "
			"expected: <pointer to data>");

	++(*i);
	return -1;
} /* iter_error */

START_TEST(test_iterate)
{
	intptr_t i = 0;
	int check;

	/* empty store */
	check = sdb_store_iterate(iter_incr, &i);
	fail_unless(check == -1,
			"sdb_store_iterate(), empty store = %d; expected: -1", check);
	fail_unless(i == 0,
			"sdb_store_iterate called callback %d times; expected: 0", (int)i);

	populate();

	check = sdb_store_iterate(iter_incr, &i);
	fail_unless(check == 0,
			"sdb_store_iterate() = %d; expected: 0", check);
	fail_unless(i == 2,
			"sdb_store_iterate called callback %d times; expected: 1", (int)i);

	i = 0;
	check = sdb_store_iterate(iter_error, &i);
	fail_unless(check == -1,
			"sdb_store_iterate(), error callback = %d; expected: -1", check);
	fail_unless(i == 1,
			"sdb_store_iterate called callback %d times "
			"(callback returned error); expected: 1", (int)i);
}
END_TEST

Suite *
core_store_suite(void)
{
	Suite *s = suite_create("core::store");
	TCase *tc;

	tc = tcase_create("core");
	tcase_add_test(tc, test_store_tojson);
	tcase_add_test(tc, test_store_host);
	tcase_add_test(tc, test_store_get_host);
	tcase_add_test(tc, test_store_attr);
	tcase_add_test(tc, test_store_metric);
	tcase_add_test(tc, test_store_metric_attr);
	tcase_add_test(tc, test_store_service);
	tcase_add_test(tc, test_store_service_attr);
	tcase_add_test(tc, test_get_field);
	tcase_add_test(tc, test_interval);
	tcase_add_test(tc, test_iterate);
	tcase_add_unchecked_fixture(tc, NULL, sdb_store_clear);
	suite_add_tcase(s, tc);

	return s;
} /* core_store_suite */

/* vim: set tw=78 sw=4 ts=4 noexpandtab : */

