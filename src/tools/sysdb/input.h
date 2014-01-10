/*
 * SysDB - src/tools/sysdb/input.h
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

#include "client/sock.h"
#include "utils/strbuf.h"

#ifndef SYSDB_INPUT_H
#define SYSDB_INPUT_H 1

typedef struct {
	sdb_client_t *client;

	sdb_strbuf_t *input;
	size_t tokenizer_pos;
	size_t query_len;
} sdb_input_t;

#define SDB_INPUT_INIT { NULL, NULL, 0, 0 }

/*
 * sdb_input_readline:
 * This function is supposed to be used with a flex scanner's YY_INPUT. It
 * reads input from the user using reading() and places available input in the
 * specified buffer, returning the number of bytes in 'n_chars' (no more than
 * 'max_chars'.
 *
 * Returns:
 *  - The number of newly read bytes.
 *  - A negative value in case of an error.
 */
ssize_t
sdb_input_readline(sdb_input_t *input, char *buf,
		int *n_chars, size_t max_chars);

/*
 * sdb_input_exec_query:
 * Execute the query currently stored in the input buffer. Waits for the
 * server's reply and prints errors or returned data to standard output.
 */
int
sdb_input_exec_query(sdb_input_t *input);

/*
 * scanner
 */

void
sdb_input_set(sdb_input_t *new_input);

#endif /* SYSDB_INPUT_H */

/* vim: set tw=78 sw=4 ts=4 noexpandtab : */

