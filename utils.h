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
#include <stdbool.h>

extern char *append_paths(const char *pre, const char *post);
extern int file_tar(const char *from, const char *to, bool compress);
extern int file_untar(const char *from, const char *to);
extern char *is_whiteout(char *file);
extern void *strmmap(void *addr, size_t length, int prot, int flags, int fd,
		     off_t offset);
extern int strmunmap(void *addr, size_t length);
extern int recursive_rmdir(const char *dirname, const char *exclude,
			   unsigned int nested, bool skip_top);
extern int rsync_layer(const char *from, const char *to);
extern int wait_for_pid(pid_t pid);
extern int delete_whiteouts(const char *oldpath, const char *newpath);

#endif // __LAYER_UTILS_H
