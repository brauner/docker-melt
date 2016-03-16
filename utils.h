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

#ifndef __LAYER_UTILS_H
#define __LAYER_UTILS_H

#include <stdio.h>

struct mapped_file {
	char *buf;
	size_t len;
	int fd;
};

extern char *append_paths(const char *pre, const char *post);
extern int file_tar(const char *from, const char *to);
extern int file_untar(const char *from, const char *to);
extern int mmap_file_as_str(const char *file, struct mapped_file *m);
extern int munmap_file_as_str(struct mapped_file *m);
extern int recursive_rmdir(char *dirname);
extern int wait_for_pid(pid_t pid);

#endif // __LAYER_UTILS_H
