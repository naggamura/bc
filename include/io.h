﻿/*
 * *****************************************************************************
 *
 * Copyright 2018 Gavin D. Howard
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * *****************************************************************************
 *
 * Definitions for bc I/O.
 *
 */

#ifndef BC_IO_H
#define BC_IO_H

#include <stdio.h>
#include <stdlib.h>

#include <bc.h>

typedef int (*BcIoGetc)(void*);

// ** Exclude start. **
long bc_io_frag(char *buf, long len, int term, BcIoGetc bcgetc, void *ctx);

long bc_io_fgets(char * buf, int n, FILE* fp);

BcStatus bc_io_fgetline(char** p, size_t *n, FILE* fp);

BcStatus bc_io_fread(const char *path, char** buf);
// ** Exclude end. **

#define bc_io_gets(buf, n) bc_io_fgets((buf), (n), stdin)
#define bc_io_getline(p, n) bc_io_fgetline((p), (n), stdin)

#endif // BC_IO_H
