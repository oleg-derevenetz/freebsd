/*
 * Copyright (c) 2019 Oleg Derevenetz <oleg.derevenetz@gmail.com>
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
#include <sys/uio.h>
#include <sys/mount.h>

#include <isofs/cd9660/cd9660_mount.h>
#include <fs/msdosfs/msdosfsmount.h>
#include <ufs/ufs/ufsmount.h>

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "libc_private.h"

int _nmount(struct iovec *, unsigned int, int);

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
	size_t size;
	void  *tmp_ptr;

	if (nm_args->error) {
		return;
	}

	size = sizeof(*(nm_args->iov)) * (nm_args->iov_count + 2);

	if (nm_args->iov_size < size) {
		if (size < nm_args->iov_size * 2) {
			size = nm_args->iov_size * 2;
		}

		tmp_ptr = realloc(nm_args->iov, size);

		if (tmp_ptr == NULL) {
			nm_args->error = true;

			return;
		}

		nm_args->iov      = tmp_ptr;
		nm_args->iov_size = size;
	}

	tmp_ptr = strdup(name);

	if (tmp_ptr == NULL) {
		nm_args->error = true;

		return;
	}

	nm_args->iov[nm_args->iov_count].iov_base = tmp_ptr;
	nm_args->iov[nm_args->iov_count].iov_len  = strlen(tmp_ptr) + 1;

	nm_args->iov_count++;

	if (value != NULL) {
		if (value_size > 0) {
			tmp_ptr = malloc(value_size);

			if (tmp_ptr == NULL) {
				nm_args->error = true;

				return;
			}

			memcpy(tmp_ptr, value, value_size);

			nm_args->iov[nm_args->iov_count].iov_base = tmp_ptr;
			nm_args->iov[nm_args->iov_count].iov_len  = value_size;
		} else {
			tmp_ptr = strdup(value);

			if (tmp_ptr == NULL) {
				nm_args->error = true;

				return;
			}

			nm_args->iov[nm_args->iov_count].iov_base = tmp_ptr;
			nm_args->iov[nm_args->iov_count].iov_len  = strlen(tmp_ptr) + 1;
		}
	} else {
		nm_args->iov[nm_args->iov_count].iov_base = NULL;
		nm_args->iov[nm_args->iov_count].iov_len  = 0;
	}

	nm_args->iov_count++;
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
make_nmount_args_for_cd9660(struct nmount_args *nm_args, void *data)
{
	char               tmp_str[64];
	struct iso_args   *args;
	struct export_args exp;

	if (data == NULL) {
		return;
	}

	args = data;

	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "from",   args->fspec, 0);
	add_to_nmount_args(nm_args, "export", &exp,        sizeof(exp));

	if (snprintf(tmp_str, sizeof(tmp_str), "%d", args->ssector) < 0) {
		nm_args->error = true;

		return;
	}

	add_to_nmount_args(nm_args, "ssector", tmp_str, 0);

	add_to_nmount_args(nm_args, "cs_disk",  args->cs_disk,  0);
	add_to_nmount_args(nm_args, "cs_local", args->cs_local, 0);

	if (args->flags & ISOFSMNT_NORRIP) {
		add_to_nmount_args(nm_args, "norrip",         NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "rrip",           NULL, 0);
	}
	if (args->flags & ISOFSMNT_GENS) {
		add_to_nmount_args(nm_args, "gens",           NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "nogens",         NULL, 0);
	}
	if (args->flags & ISOFSMNT_EXTATT) {
		add_to_nmount_args(nm_args, "extatt",         NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "noextatt",       NULL, 0);
	}
	if (args->flags & ISOFSMNT_NOJOLIET) {
		add_to_nmount_args(nm_args, "nojoliet",       NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "joliet",         NULL, 0);
	}
	if (args->flags & ISOFSMNT_BROKENJOLIET) {
		add_to_nmount_args(nm_args, "brokenjoliet",   NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "nobrokenjoliet", NULL, 0);
	}
	if (args->flags & ISOFSMNT_KICONV) {
		add_to_nmount_args(nm_args, "kiconv",         NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "nokiconv",       NULL, 0);
	}
}

static void
make_nmount_args_for_msdosfs(struct nmount_args *nm_args, void *data)
{
	char                 tmp_str[64];
	struct msdosfs_args *args;
	struct export_args   exp;

	if (data == NULL) {
		return;
	}

	args = data;

	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "from",   args->fspec, 0);
	add_to_nmount_args(nm_args, "export", &exp,        sizeof(exp));

	if (snprintf(tmp_str, sizeof(tmp_str), "%d", args->uid) < 0) {
		nm_args->error = true;

		return;
	}

	add_to_nmount_args(nm_args, "uid", tmp_str, 0);

	if (snprintf(tmp_str, sizeof(tmp_str), "%d", args->gid) < 0) {
		nm_args->error = true;

		return;
	}

	add_to_nmount_args(nm_args, "gid", tmp_str, 0);

	if (snprintf(tmp_str, sizeof(tmp_str), "%d", args->mask) < 0) {
		nm_args->error = true;

		return;
	}

	add_to_nmount_args(nm_args, "mask", tmp_str, 0);

	if (snprintf(tmp_str, sizeof(tmp_str), "%d", args->dirmask) < 0) {
		nm_args->error = true;

		return;
	}

	add_to_nmount_args(nm_args, "dirmask", tmp_str, 0);

	add_to_nmount_args(nm_args, "cs_win",   args->cs_win,   0);
	add_to_nmount_args(nm_args, "cs_dos",   args->cs_dos,   0);
	add_to_nmount_args(nm_args, "cs_local", args->cs_local, 0);

	if (args->flags & MSDOSFSMNT_SHORTNAME) {
		add_to_nmount_args(nm_args, "shortname",   NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "noshortname", NULL, 0);
	}
	if (args->flags & MSDOSFSMNT_LONGNAME) {
		add_to_nmount_args(nm_args, "longname",    NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "nolongname",  NULL, 0);
	}
	if (args->flags & MSDOSFSMNT_NOWIN95) {
		add_to_nmount_args(nm_args, "nowin95",     NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "win95",       NULL, 0);
	}
	if (args->flags & MSDOSFSMNT_KICONV) {
		add_to_nmount_args(nm_args, "kiconv",      NULL, 0);
	} else {
		add_to_nmount_args(nm_args, "nokiconv",    NULL, 0);
	}
}

static void
make_nmount_args_for_ufs(struct nmount_args *nm_args, void *data)
{
	struct ufs_args   *args;
	struct export_args exp;

	if (data == NULL) {
		return;
	}

	args = data;

	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "from",   args->fspec, 0);
	add_to_nmount_args(nm_args, "export", &exp,        sizeof(exp));
}

struct fs_entry
{
	const char * const type;
	void      (* const make_nmount_args_for_type)(struct nmount_args *, void *);
};

const struct fs_entry supported_fs[] = {
	{"cd9660",  make_nmount_args_for_cd9660},
	{"msdosfs", make_nmount_args_for_msdosfs},
	{"ufs",     make_nmount_args_for_ufs},
	{NULL,      NULL}
};

#pragma weak mount
int
mount(const char *type, const char *dir, int flags, void *data)
{
	bool               supported = false;
	int                i, result;
	struct nmount_args nm_args = {};

	fprintf(stderr, "DEBUG: WRAPPER CALLED\n");

	for (i = 0;; i++) {
		if (supported_fs[i].type == NULL) {
			break;
		}

		if (strcmp(type, supported_fs[i].type) == 0) {
			supported = true;

			add_to_nmount_args(&nm_args, "fstype", (void*)type, 0);
			add_to_nmount_args(&nm_args, "fspath", (void*)dir,  0);

			if (supported_fs[i].make_nmount_args_for_type != NULL) {
				supported_fs[i].make_nmount_args_for_type(&nm_args, data);
			}

			break;
		}
	}

	if (nm_args.error) {
		result = -1;
	} else if (supported) {
		result = _nmount(nm_args.iov, nm_args.iov_count, flags);
	} else {
		fprintf(stderr, "DEBUG: UNSUPPORTED FS, CALLING __sys_mount()\n");

		result = __sys_mount(type, dir, flags, data);
	}

	free_nmount_args(&nm_args);

	return result;
}
