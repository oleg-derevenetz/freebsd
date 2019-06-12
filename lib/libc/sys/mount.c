/*
 * Copyright (c) 2019 Oleg Derevenetz <oleg.derevenetz@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/mount.h>

#include <isofs/cd9660/cd9660_mount.h>
#include <ufs/ufs/ufsmount.h>

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libc_private.h"

struct nmount_args
{
	bool          error;
	u_int         iov_count; /* number of elements in iov */
	size_t        iov_size;  /* real size of iov in bytes */
	struct iovec *iov;
};

static void
conv_oexport_to_export(struct oexport_args *oexp, struct export_args *exp)
{
	memset(exp, 0,    sizeof(*exp));
	memcpy(exp, oexp, sizeof(*oexp));
}

static void
add_to_nmount_args(struct nmount_args *nm_args, const char *name, void *value, size_t value_size)
{
	size_t        size;
	void         *tmp_base;
	struct iovec *tmp_iov;

	if (!(nm_args->error)) {
		size = sizeof(*(nm_args->iov)) * (nm_args->iov_count + 2);

		if (nm_args->iov_size < size) {
			if (size < nm_args->iov_size * 2) {
				size = nm_args->iov_size * 2;
			}

			tmp_iov = realloc(nm_args->iov, size);

			if (tmp_iov != NULL) {
				nm_args->iov      = tmp_iov;
				nm_args->iov_size = size;
			} else {
				nm_args->error = true;
			}
		}
	}

	if (!(nm_args->error)) {
		tmp_base = strdup(name);

		if (tmp_base != NULL) {
			nm_args->iov[nm_args->iov_count].iov_base = tmp_base;
			nm_args->iov[nm_args->iov_count].iov_len  = strlen(tmp_base) + 1;

			nm_args->iov_count++;
		} else {
			nm_args->error = true;
		}
	}

	if (!(nm_args->error)) {
		if (value != NULL) {
			if (value_size > 0) {
				tmp_base = malloc(value_size);

				if (tmp_base != NULL) {
					memcpy(tmp_base, value, value_size);

					nm_args->iov[nm_args->iov_count].iov_base = tmp_base;
					nm_args->iov[nm_args->iov_count].iov_len  = value_size;

					nm_args->iov_count++;
				} else {
					nm_args->error = true;
				}
			} else {
				tmp_base = strdup(value);

				if (tmp_base != NULL) {
					nm_args->iov[nm_args->iov_count].iov_base = tmp_base;
					nm_args->iov[nm_args->iov_count].iov_len  = strlen(tmp_base) + 1;

					nm_args->iov_count++;
				} else {
					nm_args->error = true;
				}
			}
		} else {
			nm_args->iov[nm_args->iov_count].iov_base = NULL;
			nm_args->iov[nm_args->iov_count].iov_len  = 0;

			nm_args->iov_count++;
		}
	}
}

static void
free_nmount_args(struct nmount_args *nm_args)
{
	u_int i;

	for (i = 0; i < nm_args->iov_count; i++) {
		free(nm_args->iov[i].iov_base);
	}

	free(nm_args->iov);

	nm_args->iov_count = 0;
	nm_args->iov_size  = 0;
	nm_args->iov       = NULL;
}

static void
make_nmount_args_for_ufs(struct nmount_args *nm_args, void *data)
{
	struct ufs_args   *args;
	struct export_args exp;

	if (data != NULL) {
		args = data;

		conv_oexport_to_export(&(args->export), &exp);

		add_to_nmount_args(nm_args, "from",   args->fspec, 0);
		add_to_nmount_args(nm_args, "export", &exp,        sizeof(exp));
	}
}

static void
make_nmount_args_for_cd9660(struct nmount_args *nm_args, void *data)
{
	char               ssector_str[64];
	struct iso_args   *args;
	struct export_args exp;

	if (data != NULL) {
		args = data;

		snprintf(ssector_str, sizeof(ssector_str), "%d", args->ssector);

		conv_oexport_to_export(&(args->export), &exp);

		add_to_nmount_args(nm_args, "from",     args->fspec,    0);
		add_to_nmount_args(nm_args, "export",   &exp,           sizeof(exp));
		add_to_nmount_args(nm_args, "ssector",  ssector_str,    0);
		add_to_nmount_args(nm_args, "cs_disk",  args->cs_disk,  0);
		add_to_nmount_args(nm_args, "cs_local", args->cs_local, 0);

		if (args->flags & ISOFSMNT_NORRIP) {
			add_to_nmount_args(nm_args, "norrip", NULL, 0);
		}
		if (!(args->flags & ISOFSMNT_GENS)) {
			add_to_nmount_args(nm_args, "nogens", NULL, 0);
		}
		if (!(args->flags & ISOFSMNT_EXTATT)) {
			add_to_nmount_args(nm_args, "noextatt", NULL, 0);
		}
		if (args->flags & ISOFSMNT_NOJOLIET) {
			add_to_nmount_args(nm_args, "nojoliet", NULL, 0);
		}
		if (!(args->flags & ISOFSMNT_BROKENJOLIET)) {
			add_to_nmount_args(nm_args, "nobrokenjoliet", NULL, 0);
		}
		if (!(args->flags & ISOFSMNT_KICONV)) {
			add_to_nmount_args(nm_args, "nokiconv", NULL, 0);
		}
	}
}

__weak_reference(__sys_mount, __mount);

#pragma weak mount
int
mount(const char *type, const char *dir, int flags, void *data)
{
	int                result;
	struct nmount_args nm_args;

	fprintf(stderr, "DEBUG: WRAPPER CALLED\n");

	nm_args.error     = false;
	nm_args.iov_count = 0;
	nm_args.iov_size  = 0;
	nm_args.iov       = NULL;

	add_to_nmount_args(&nm_args, "fstype", (void*)type, 0);
	add_to_nmount_args(&nm_args, "fspath", (void*)dir,  0);

	if (strcmp(type, "ufs") == 0) {
		make_nmount_args_for_ufs(&nm_args, data);
	} else if (strcmp(type, "cd9660") == 0) {
		make_nmount_args_for_cd9660(&nm_args, data);
	}

	if (!(nm_args.error)) {
		result = nmount(nm_args.iov, nm_args.iov_count, flags);
	} else {
		result = -1;
	}

	free_nmount_args(&nm_args);

	return result;
}
