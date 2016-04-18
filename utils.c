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
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "utils.h"

char *append_paths(const char *pre, const char *post)
{
	const char *fmt = "%s%s";
	char *prepost = NULL;
	size_t len = strlen(pre) + strlen(post) + 1;

	if (post[0] != '/') {
		len++;
		fmt = "%s/%s";
	}

	prepost = calloc(sizeof(char), len);
	if (!prepost)
		return NULL;

	snprintf(prepost, len, fmt, pre, post);
	return prepost;
}

int file_tar(const char *from, const char *to, bool compress)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		execlp("tar", "tar", "--acls", "--xattrs", "--xattrs-include=*",
		       "--same-owner", "--numeric-owner",
		       "--preserve-permissions", "--atime-preserve=system",
		       "-S", "-C", from, compress ? "-cJf" : "-cf", to, ".",
		       (char *)NULL);
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
		execlp("tar", "tar", "--acls", "--xattrs", "--xattrs-include=*",
		       "--same-owner", "--numeric-owner",
		       "--preserve-permissions", "--atime-preserve=system",
		       "-S", "-xf", from, "-C", to, (char *)NULL);
		return -1; // should not happen
	}

	if (wait_for_pid(pid) < 0)
		return -1;

	return 0;
}

char *is_whiteout(char *file)
{
	if (!strncmp(file, ".wh.", 4) && strcmp(file, ".wh.")) {
		return file + 4;
	}

	return NULL;
}

int recursive_rmdir(const char *dirname, bool skip_top)
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
			if (recursive_rmdir(delete, skip_top) < 0)
				err = true;
		} else {
			if (unlink(delete) < 0)
				err = true;
		}
	}

	if (!skip_top)
		if (rmdir(dirname) < 0)
			err = true;

	closedir(dir);

	return err ? -1 : 0;
}

void *strmmap(void *addr, size_t length, int prot, int flags, int fd,
	      off_t offset)
{
	void *tmp = NULL, *overlap = NULL;

	/* We establish an anonymous mapping that is one byte larger than the
	 * underlying file. The pages handed to us are zero filled. */
	tmp = mmap(addr, length + 1, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (tmp == MAP_FAILED)
		return tmp;

	/* Now we establish a fixed-address mapping starting at the address we
	 * received from our anonymous mapping and replace all bytes excluding
	 * the additional \0-byte with the file. This allows us to use normal
	 * string-handling functions. */
	overlap = mmap(tmp, length, prot, MAP_FIXED | flags, fd, offset);
	if (overlap == MAP_FAILED)
		munmap(tmp, length + 1);

	return overlap;
}

int strmunmap(void *addr, size_t length)
{
	return munmap(addr, length + 1);
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

int delete_whiteouts(const char *oldpath, const char *newpath)
{
	struct stat s;
	struct dirent dirent, *direntp;
	DIR *dir;
	int ret;
	bool err = false;
	char recurse[PATH_MAX];
	char delete[PATH_MAX];
	char *whiteout;

	dir = opendir(oldpath);
	if (!dir)
		return -1;

	while (!readdir_r(dir, &dirent, &direntp)) {
		int rc;

		if (!direntp)
			break;

		if (!strcmp(direntp->d_name, ".") ||
		    !strcmp(direntp->d_name, ".."))
			continue;

		whiteout = is_whiteout(direntp->d_name);
		rc = snprintf(delete, PATH_MAX, "%s/%s", newpath, whiteout ? whiteout : direntp->d_name);
		if (rc < 0 || rc >= PATH_MAX) {
			err = true;
			continue;
		}
		if (whiteout) {
			ret = lstat(delete, &s);
			if (ret)
				continue;
			if (S_ISDIR(s.st_mode)) {
				if (recursive_rmdir(delete, false) < 0)
					err = true;
			} else {
				if (unlink(delete) < 0)
					err = true;
			}
		}

		rc = snprintf(recurse, PATH_MAX, "%s/%s", oldpath, direntp->d_name);
		if (rc < 0 || rc >= PATH_MAX) {
			err = true;
			continue;
		}
		ret = lstat(recurse, &s);
		if (ret) {
			err = true;
			continue;
		}
		if (S_ISDIR(s.st_mode))
			if (delete_whiteouts(recurse, delete) < 0)
				err = true;
	}

	closedir(dir);

	return err ? -1 : 0;
}

int rsync_layer(const char *from, const char *to)
{
	pid_t pid = fork();
	if (pid < 0)
		return -1;

	if (!pid) {
		char tmp[PATH_MAX];
		int ret = snprintf(tmp, PATH_MAX, "%s/./", from);
		if (ret < 0 || ret >= PATH_MAX)
			return -1;

		execlp("rsync", "rsync", "-aXhsrpR", "--numeric-ids",
		       "--remove-source-files", "--exclude=.wh.*", tmp, to,
		       (char *)NULL);
		return -1; // should not happen
	}

	if (wait_for_pid(pid) < 0)
		return -1;

	return 0;
}
