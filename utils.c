/*
 *
 * Copyright © 2016 Christian Brauner <christian.brauner@mailbox.org>.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
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

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "utils.h"

char *append_paths(const char *pre, const char *post)
{
	size_t len = strlen(pre) + strlen(post) + 1;
	const char *fmt = "%s%s";
	char *prepost = NULL;

	if (post[0] != '/') {
		len += 1;
		fmt = "%s/%s";
	}

	prepost = calloc(sizeof(char), len);
	if (!prepost)
		return NULL;

	snprintf(prepost, len, fmt, pre, post);
	return prepost;
}

int file_tar(const char *from, const char *to)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		execlp("tar", "tar", "-C", from, "-cf", to, ".", (char *)NULL);
		return -1; // should not happen
	}

	if (wait_for_pid(pid) < 0)
		return -1;

	return 0;
}

int file_untar(const char *from, const char *to)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		execlp("tar", "tar", "--overwrite", "--xattrs", "--same-owner", "--preserve-permissions", "-xf", from, "-C", to, (char *)NULL);
		return -1; // should not happen
	}

	if (wait_for_pid(pid) < 0)
		return -1;

	return 0;
}

int recursive_rmdir(char *dirname)
{
	struct stat s;
	struct dirent dirent, *direntp;
	DIR *dir;
	int ret;
	bool err = false;
	char delete[PATH_MAX];

	dir = opendir(dirname);
	if (!dir)
		return -1;

	while (!readdir_r(dir, &dirent, &direntp)) {
		int rc;

		if (!direntp)
			break;

		if (!strcmp(direntp->d_name, ".") ||
		    !strcmp(direntp->d_name, ".."))
			continue;

		rc = snprintf(delete, PATH_MAX, "%s/%s", dirname, direntp->d_name);
		if (rc < 0 || rc >= PATH_MAX) {
			err = true;
			continue;
		}

		ret = lstat(delete, &s);
		if (ret) {
			err = true;
			continue;
		}

		if (S_ISDIR(s.st_mode)) {
			if (recursive_rmdir(delete) < 0)
				err = true;
		} else {
			if (unlink(delete) < 0)
				err = true;
		}
	}

	if (rmdir(dirname) < 0)
		err = true;

	closedir(dir);
	if (ret)
		err = true;

	return err ? -1 : 0;
}

int wait_for_pid(pid_t pid)
{
	int status, ret;

again:
	ret = waitpid(pid, &status, 0);
	if (ret == -1) {
		if (errno == EINTR) // we got interrupted... try again...
			goto again;
		return -1;
	}

	if (ret != pid) // status not available... try again...
		goto again;

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;

	return 0;
}