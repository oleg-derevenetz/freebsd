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

#include <sys/stat.h>
#include <sys/mount.h>

#include <errno.h>
#include <unistd.h>
#include <stdio.h>

#include <atf-c.h>

const char * const dir = "mount";

ATF_TC_WITH_CLEANUP(mount_fdescfs);

ATF_TC_HEAD(mount_fdescfs, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of mount() for fdescfs");
}

ATF_TC_BODY(mount_fdescfs, tc)
{
	int result;

	result = mkdir(dir, 0700);
	ATF_REQUIRE_MSG(result == 0, "mkdir(): %s", strerror(errno));

	result = mount("fdescfs", dir, 0, NULL);
	ATF_REQUIRE_MSG(result == 0, "mount(): %s", strerror(errno));

	result = unmount(dir, 0);
	ATF_REQUIRE_MSG(result == 0, "unmount(): %s", strerror(errno));

	result = rmdir(dir);
	ATF_REQUIRE_MSG(result == 0, "rmdir(): %s", strerror(errno));
}

ATF_TC_CLEANUP(mount_fdescfs, tc)
{
	(void)unmount(dir, 0);
	(void)rmdir(dir);
}

ATF_TC_WITH_CLEANUP(mount_linprocfs);

ATF_TC_HEAD(mount_linprocfs, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of mount() for linprocfs");
}

ATF_TC_BODY(mount_linprocfs, tc)
{
	int result;

	result = mkdir(dir, 0700);
	ATF_REQUIRE_MSG(result == 0, "mkdir(): %s", strerror(errno));

	result = mount("linprocfs", dir, 0, NULL);
	ATF_REQUIRE_MSG(result == 0, "mount(): %s", strerror(errno));

	result = unmount(dir, 0);
	ATF_REQUIRE_MSG(result == 0, "unmount(): %s", strerror(errno));

	result = rmdir(dir);
	ATF_REQUIRE_MSG(result == 0, "rmdir(): %s", strerror(errno));
}

ATF_TC_CLEANUP(mount_linprocfs, tc)
{
	(void)unmount(dir, 0);
	(void)rmdir(dir);
}

ATF_TC_WITH_CLEANUP(mount_procfs);

ATF_TC_HEAD(mount_procfs, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of mount() for procfs");
}

ATF_TC_BODY(mount_procfs, tc)
{
	int result;

	result = mkdir(dir, 0700);
	ATF_REQUIRE_MSG(result == 0, "mkdir(): %s", strerror(errno));

	result = mount("procfs", dir, 0, NULL);
	ATF_REQUIRE_MSG(result == 0, "mount(): %s", strerror(errno));

	result = unmount(dir, 0);
	ATF_REQUIRE_MSG(result == 0, "unmount(): %s", strerror(errno));

	result = rmdir(dir);
	ATF_REQUIRE_MSG(result == 0, "rmdir(): %s", strerror(errno));
}

ATF_TC_CLEANUP(mount_procfs, tc)
{
	(void)unmount(dir, 0);
	(void)rmdir(dir);
}

ATF_TC_WITH_CLEANUP(mount_unknownfs);

ATF_TC_HEAD(mount_unknownfs, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of mount() for unknown fs");
}

ATF_TC_BODY(mount_unknownfs, tc)
{
	int result;

	result = mkdir(dir, 0700);
	ATF_REQUIRE_MSG(result == 0, "mkdir(): %s", strerror(errno));

	ATF_REQUIRE_ERRNO(ENOENT, mount("unknownfs", dir, 0, NULL) == -1);

	result = rmdir(dir);
	ATF_REQUIRE_MSG(result == 0, "rmdir(): %s", strerror(errno));
}

ATF_TC_CLEANUP(mount_unknownfs, tc)
{
	(void)rmdir(dir);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mount_fdescfs);
	ATF_TP_ADD_TC(tp, mount_linprocfs);
	ATF_TP_ADD_TC(tp, mount_procfs);
	ATF_TP_ADD_TC(tp, mount_unknownfs);

	return atf_no_error();
}
