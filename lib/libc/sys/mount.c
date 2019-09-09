/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Oleg Derevenetz <oleg.derevenetz@gmail.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/mount.h>

#include <fs/msdosfs/msdosfsmount.h>
#include <fs/nandfs/nandfs_mount.h>
#include <fs/smbfs/smbfs.h>
#include <isofs/cd9660/cd9660_mount.h>
#include <nfsclient/nfsargs.h>
#include <ufs/ufs/ufsmount.h>

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libc_private.h"

struct nmount_args {
	bool error;
	u_int iov_count; /* number of elements in iov */
	size_t iov_size; /* real size of iov in bytes */
	struct iovec *iov;
};

static void
conv_oexport_to_export(struct oexport_args *oexp, struct export_args *exp)
{
	memset(exp, 0, sizeof(*exp));
	memcpy(exp, oexp, sizeof(*oexp));
}

static void
init_nmount_args(struct nmount_args *nm_args)
{
	size_t size;

	memset(nm_args, 0, sizeof(*nm_args));

	/* All mounts have at least two name-value pairs: fstype and fspath. */
	size = sizeof(*(nm_args->iov)) * 4;
	nm_args->iov = malloc(size);

	if (nm_args->iov == NULL) {
		nm_args->error = true;
		return;
	}

	nm_args->iov_size = size;
}

static void
add_to_nmount_args(struct nmount_args *nm_args, const char *name,
	const void *value, size_t value_size)
{
	size_t size;
	void *tmp_ptr;

	if (nm_args->error)
		return;

	/* Each name-value pair takes two struct iovec's. */
	size = sizeof(*(nm_args->iov)) * (nm_args->iov_count + 2);

	if (nm_args->iov_size < size) {
		if (size < nm_args->iov_size * 2)
			size = nm_args->iov_size * 2;

		tmp_ptr = realloc(nm_args->iov, size);

		if (tmp_ptr == NULL) {
			nm_args->error = true;
			return;
		}

		nm_args->iov = tmp_ptr;
		nm_args->iov_size = size;
	}

	tmp_ptr = strdup(name);

	if (tmp_ptr == NULL) {
		nm_args->error = true;
		return;
	}

	nm_args->iov[nm_args->iov_count].iov_base = tmp_ptr;
	nm_args->iov[nm_args->iov_count].iov_len = strlen(tmp_ptr) + 1;
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
			nm_args->iov[nm_args->iov_count].iov_len = value_size;
		} else {
			tmp_ptr = strdup(value);

			if (tmp_ptr == NULL) {
				nm_args->error = true;
				return;
			}

			nm_args->iov[nm_args->iov_count].iov_base = tmp_ptr;
			nm_args->iov[nm_args->iov_count].iov_len =
				strlen(tmp_ptr) + 1;
		}
	} else {
		nm_args->iov[nm_args->iov_count].iov_base = NULL;
		nm_args->iov[nm_args->iov_count].iov_len = 0;
	}

	nm_args->iov_count++;
}

/*
 * Add name-value pair to nmount_args, where value is a null-terminated string.
 *
 * Syntax example:
 *
 * add_str_to_nmount_args(nm_args, "from", fspec);
 */
static void
add_str_to_nmount_args(struct nmount_args *nm_args, const char *name,
	const char *value)
{
	add_to_nmount_args(nm_args, name, value, 0);
}

/*
 * Add name-value pair to nmount_args, where value is a printf(3)-style
 * formatted string.
 *
 * Syntax example:
 *
 * add_fmt_to_nmount_args(nm_args, "ssector", "%d", ssector);
 */
static void
add_fmt_to_nmount_args(struct nmount_args *nm_args, const char *name,
	const char *format, ...)
{
	char tmp_str[64];
	va_list ap;

	if (nm_args->error)
		return;

	va_start(ap, format);

	if (vsnprintf(tmp_str, sizeof(tmp_str), format, ap) < 0)
		nm_args->error = true;
	else
		add_to_nmount_args(nm_args, name, tmp_str, 0);

	va_end(ap);
}

/*
 * Add name-value pair to nmount_args, where name is a fs driver flag name and
 * value is always NULL.
 *
 * Value of the "name" argument should always start with "no" (for example,
 * "noflag"), function will add it as is or remove the "no" part depending
 * on the value of the "flag" argument.
 *
 * Syntax examples:
 *
 * add_flag_to_nmount_args(nm_args, "norrip", flags & ISOFSMNT_NORRIP);
 * add_flag_to_nmount_args(nm_args, "nogens", !(flags & ISOFSMNT_GENS));
 */
static void
add_flag_to_nmount_args(struct nmount_args *nm_args, const char *name,
	bool flag)
{
	if (nm_args->error)
		return;

	if (name[0] == 'n' && name[1] == 'o' && name[2] != '\0') {
		if (flag)
			add_to_nmount_args(nm_args, name, NULL, 0);
		else
			add_to_nmount_args(nm_args, &name[2], NULL, 0);
	} else {
		errno = EDOOFUS;
		nm_args->error = true;
	}
}

static void
free_nmount_args(struct nmount_args *nm_args)
{
	u_int i;

	for (i = 0; i < nm_args->iov_count; i++)
		free(nm_args->iov[i].iov_base);

	free(nm_args->iov);

	nm_args->iov_count = 0;
	nm_args->iov_size = 0;
	nm_args->iov = NULL;
}

static void
make_nmount_args_for_cd9660(struct nmount_args *nm_args, void *data)
{
	struct iso_args *args;
	struct export_args exp;

	if (data == NULL)
		return;

	args = data;
	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "export", &exp, sizeof(exp));

	add_str_to_nmount_args(nm_args, "from", args->fspec);
	add_str_to_nmount_args(nm_args, "cs_disk", args->cs_disk);
	add_str_to_nmount_args(nm_args, "cs_local", args->cs_local);

	add_fmt_to_nmount_args(nm_args, "ssector", "%d", args->ssector);

	add_flag_to_nmount_args(nm_args, "norrip",
		args->flags & ISOFSMNT_NORRIP);
	add_flag_to_nmount_args(nm_args, "nogens",
		!(args->flags & ISOFSMNT_GENS));
	add_flag_to_nmount_args(nm_args, "noextatt",
		!(args->flags & ISOFSMNT_EXTATT));
	add_flag_to_nmount_args(nm_args, "nojoliet",
		args->flags & ISOFSMNT_NOJOLIET);
	add_flag_to_nmount_args(nm_args, "nobrokenjoliet",
		!(args->flags & ISOFSMNT_BROKENJOLIET));
	add_flag_to_nmount_args(nm_args, "nokiconv",
		!(args->flags & ISOFSMNT_KICONV));
}

static void
make_nmount_args_for_msdosfs(struct nmount_args *nm_args, void *data)
{
	struct msdosfs_args *args;
	struct export_args exp;

	if (data == NULL)
		return;

	args = data;
	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "export", &exp, sizeof(exp));

	add_str_to_nmount_args(nm_args, "from", args->fspec);
	add_str_to_nmount_args(nm_args, "cs_win", args->cs_win);
	add_str_to_nmount_args(nm_args, "cs_dos", args->cs_dos);
	add_str_to_nmount_args(nm_args, "cs_local", args->cs_local);

	add_fmt_to_nmount_args(nm_args, "uid", "%d", args->uid);
	add_fmt_to_nmount_args(nm_args, "gid", "%d", args->gid);
	add_fmt_to_nmount_args(nm_args, "mask", "%d", args->mask);
	add_fmt_to_nmount_args(nm_args, "dirmask", "%d", args->dirmask);

	add_flag_to_nmount_args(nm_args, "noshortnames",
		!(args->flags & MSDOSFSMNT_SHORTNAME));
	add_flag_to_nmount_args(nm_args, "nolongnames",
		!(args->flags & MSDOSFSMNT_LONGNAME));
	add_flag_to_nmount_args(nm_args, "nowin95",
		args->flags & MSDOSFSMNT_NOWIN95);
	add_flag_to_nmount_args(nm_args, "nokiconv",
		!(args->flags & MSDOSFSMNT_KICONV));
}

static void
make_nmount_args_for_nandfs(struct nmount_args *nm_args, void *data)
{
	struct nandfs_args *args;

	if (data == NULL)
		return;

	args = data;

	add_str_to_nmount_args(nm_args, "from", args->fspec);
	add_fmt_to_nmount_args(nm_args, "snap", "%"PRId64, args->cpno);
}

static void
make_nmount_args_for_nfs(struct nmount_args *nm_args, void *data)
{
	struct nfs_args *args;

	if (data == NULL)
		return;

	args = data;

	add_to_nmount_args(nm_args, "nfs_args", args, sizeof(*args));
}

static void
make_nmount_args_for_smbfs(struct nmount_args *nm_args, void *data)
{
	struct smbfs_args *args;

	if (data == NULL)
		return;

	args = data;

	add_str_to_nmount_args(nm_args, "rootpath", args->root_path);

	add_fmt_to_nmount_args(nm_args, "dev", "%d", args->dev);
	add_fmt_to_nmount_args(nm_args, "uid", "%d", args->uid);
	add_fmt_to_nmount_args(nm_args, "gid", "%d", args->gid);
	add_fmt_to_nmount_args(nm_args, "file_mode", "%d", args->file_mode);
	add_fmt_to_nmount_args(nm_args, "dir_mode", "%d", args->dir_mode);
	add_fmt_to_nmount_args(nm_args, "caseopt", "%d", args->caseopt);

	add_flag_to_nmount_args(nm_args, "nosoft",
		!(args->flags & SMBFS_MOUNT_SOFT));
	add_flag_to_nmount_args(nm_args, "nointr",
		!(args->flags & SMBFS_MOUNT_INTR));
	add_flag_to_nmount_args(nm_args, "nostrong",
		!(args->flags & SMBFS_MOUNT_STRONG));
	add_flag_to_nmount_args(nm_args, "nohave_nls",
		!(args->flags & SMBFS_MOUNT_HAVE_NLS));
	add_flag_to_nmount_args(nm_args, "nolong",
		args->flags & SMBFS_MOUNT_NO_LONG);
}

static void
make_nmount_args_for_ufs(struct nmount_args *nm_args, void *data)
{
	struct ufs_args *args;
	struct export_args exp;

	if (data == NULL)
		return;

	args = data;
	conv_oexport_to_export(&(args->export), &exp);

	add_to_nmount_args(nm_args, "export", &exp, sizeof(exp));
	add_str_to_nmount_args(nm_args, "from", args->fspec);
}

struct fs_entry {
	const char * const type;
	void (* const make_nmount_args_for_type)(struct nmount_args *, void *);
};

static const struct fs_entry supported_fs[] = {
	{"cd9660", make_nmount_args_for_cd9660},
	{"fdescfs", NULL},
	{"linprocfs", NULL},
	{"msdosfs", make_nmount_args_for_msdosfs},
	{"nandfs", make_nmount_args_for_nandfs},
	{"procfs", NULL},
	{"nfs", make_nmount_args_for_nfs},
	{"smbfs", make_nmount_args_for_smbfs},
	{"ufs", make_nmount_args_for_ufs}
};

#pragma weak mount
int
mount(const char *type, const char *dir, int flags, void *data)
{
	bool supported = false;
	int i, result;
	struct nmount_args nm_args;

	init_nmount_args(&nm_args);

	for (i = 0; i < nitems(supported_fs); i++) {
		if (strcmp(type, supported_fs[i].type) == 0) {
			supported = true;

			add_str_to_nmount_args(&nm_args, "fstype", type);
			add_str_to_nmount_args(&nm_args, "fspath", dir);

			if (supported_fs[i].make_nmount_args_for_type != NULL)
				supported_fs[i].make_nmount_args_for_type(
					&nm_args, data);

			break;
		}
	}

	if (nm_args.error)
		result = -1;
	else if (supported)
		result = __sys_nmount(nm_args.iov, nm_args.iov_count, flags);
	else
		result = __sys_mount(type, dir, flags, data);

	free_nmount_args(&nm_args);

	return (result);
}
