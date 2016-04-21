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
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils.h"

static int extract_ordered_layers(char *path, char ***arr);
static void free_ordered_layer_list(char **arr);
static char *find_manfiest_json(const char *path);
static int merge_layers(const char *image_out, const char *old_img_tmp,
			char **layers, char *tmp_prefix, bool compress);
static void usage(const char *name);


int main(int argc, char *argv[])
{
	char **ordered_layers = NULL;
	int c, ret, fret = -1;
	char *image = NULL, *image_out = NULL;
	bool compress = false;
	char *tmp_prefix = "/tmp";
	char old_img_tmp[PATH_MAX] = "/tmp/melt_XXXXXX";

	while ((c = getopt(argc, argv, "ct:i:o:")) != EOF) {
		switch (c) {
		case 'c':
			compress = true;
			break;
		case 't':
			tmp_prefix = optarg;
			ret = snprintf(old_img_tmp, PATH_MAX, "%s/melt_XXXXXX", tmp_prefix);
			if (ret < 0 || ret >= PATH_MAX)
				exit(EXIT_FAILURE);
			break;
		case 'i':
			image = optarg;
			break;
		case 'o':
			image_out = optarg;
			break;
		default:
			usage(argv[0]);
		}
	};

	if (!image || !image_out)
		usage(argv[0]);

	if (!mkdtemp(old_img_tmp)) {
		exit(EXIT_FAILURE);
	}
	if (chmod(old_img_tmp, 0755) < 0)
		goto out;

	if (file_untar(image, old_img_tmp) < 0) {
		fprintf(stderr, "Failed to untar original image.\n");
		goto out;
	}

	if (extract_ordered_layers(old_img_tmp, &ordered_layers) < 0) {
		fprintf(stderr, "Failed to extract layers.\n");
		goto out;
	}

	if (merge_layers(image_out, old_img_tmp, ordered_layers, tmp_prefix, compress) < 0) {
		fprintf(stderr, "Failed merging layers.\n");
		goto out;
	}

	fret = 0;

out:
	recursive_rmdir(old_img_tmp, false);
	free_ordered_layer_list(ordered_layers);
	if (!fret)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}

static int add_to_array(char ***arr)
{
	size_t len = 0;
	if (*arr)
		for (; (*arr)[len]; len++)
			;

	char **tmp = realloc(*arr, (len + 2) * sizeof(char *));
	if (!tmp)
		return -1;
	*arr = tmp;
	(*arr)[len + 1] = NULL;
	return len;
}

static int extract_ordered_layers(char *path, char ***arr)
{
	int fd;
	struct stat fbuf;
	ssize_t pos = 0;

	char *manifest_json = find_manfiest_json(path);
	if (!manifest_json)
		return -1;

	fd = open(manifest_json, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		goto out_free;

	if (fstat(fd, &fbuf) < 0)
		goto out_close;

	if (fbuf.st_size == 0)
		goto out_close;

	char *buf = strmmap(NULL, fbuf.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (buf == MAP_FAILED)
		goto out_close;

	char *layers = strstr(buf, "\"Layers\":[\"");
	if (!layers)
		goto out_unmap; // corrupt manifest.json file

	char *list_start = layers + /* "Layers":[" */ + 10;
	char *list_end = strchr(list_start, ']');
	if (!list_end)
		goto out_unmap;
	*list_end = '\0';

	char *a, *b = list_start;
	while ((a = strchr(b, '"'))) {
		a++;
		b = strchr(a, '"');
		if (!b)
			goto out_unmap;
		pos = add_to_array(arr);
		if (pos < 0)
			goto out_unmap;
		size_t len = b - a;
		(*arr)[pos] = malloc(len + 1);
		if (!(*arr)[pos])
			goto out_unmap;
		strncpy((*arr)[pos], a, len);
		(*arr)[pos][len] = '\0'; // strncpy() does not necessarily \0-terminate
		b++;
	}

out_unmap:
	strmunmap(layers, fbuf.st_size);

out_close:
	close(fd);

out_free:
	free(manifest_json);

	return 0;
}

static void free_ordered_layer_list(char **arr)
{
	if (!arr)
		return;

	char **it;
	for (it = arr; it && *it; it++) {
		free(*it);
	}
	free(arr);
}

static int merge_layers(const char *image_out, const char *old_img_tmp,
			char **layers, char *tmp_prefix, bool compress)
{
	int fret = -1;
	char *path = NULL, *tmp1 = NULL, *tmp2 = NULL;

	tmp1 = append_paths(tmp_prefix, "melt_XXXXXX");
	if (!tmp1)
		goto out_free;

	tmp2 = append_paths(tmp_prefix, "melt_XXXXXX");
	if (!tmp2)
		goto out_free;

	if (!mkdtemp(tmp1))
		goto out_free;
	if (chmod(tmp1, 0755) < 0)
		goto out_rm_tmp1;

	if (!mkdtemp(tmp2))
		goto out_rm_tmp1;
	if (chmod(tmp2, 0755) < 0)
		goto out_rm_tmp2;

	for (; layers && *layers; layers++) {
		path = append_paths(old_img_tmp, *layers);
		if (!path)
			goto out_rm_tmp2;
		if (file_untar(path, tmp2) < 0) {
			free(path);
			goto out_rm_tmp2;
		}
		/* save space by immediately deleting layers we've already
		 * untared. */
		if (unlink(path) < 0) {
			free(path);
			goto out_rm_tmp2;
		}
		free(path);
		// rsync and only leave whiteout files behind
		if (rsync_layer(tmp2, tmp1) < 0)
			goto out_rm_tmp2;
		if (delete_whiteouts(tmp2, tmp1) < 0)
			goto out_rm_tmp2;
		// empty contents of tmp dir but leave itself intact
		if (recursive_rmdir(tmp2, true) < 0)
			goto out_rm_tmp2;
	}

	if (file_tar(tmp1, image_out, compress) < 0)
		goto out_rm_tmp2;

	fret = 0;

out_rm_tmp2:
	recursive_rmdir(tmp2, false);
out_rm_tmp1:
	recursive_rmdir(tmp1, false);
out_free:
	free(tmp1);
	free(tmp2);
	return fret;
}

static char *find_manfiest_json(const char *path)
{
	char *newpath = NULL;
	struct dirent dirent, *direntp;
	DIR *dir;
	// open global directory
	dir = opendir(path);
	if (!dir)
		return NULL;

	while (!readdir_r(dir, &dirent, &direntp)) {
		if (!direntp)
			break;

		if (!strcmp(direntp->d_name, ".") ||
		    !strcmp(direntp->d_name, ".."))
			continue;

		if (strcmp(direntp->d_name, "manifest.json"))
			continue;

		// create path to layer directory
		newpath = append_paths(path, direntp->d_name);
		if (!newpath)
			return NULL;

		break;
	}

	closedir(dir);
	return newpath;
}

static void usage(const char *name)
{
	printf("usage: %s -i <input-image> -o <output-image> [-t <temporary-folder> ] [-w] [-c]\n", name);
	printf("\n");
	printf("-i <input-image>\n");
	printf("	Specify the location of the image.\n");
	printf("-o <output-image>\n");
	printf("	Specify where to store the new image.\n");
	printf("-t <temporary-folder>\n");
	printf("	Specify a location where temporary files produced by this executable are stored.\n");
	printf("-c\n");
	printf("	Compress tar file through xz.\n");
	printf("\n");
	exit(EXIT_FAILURE);
}
