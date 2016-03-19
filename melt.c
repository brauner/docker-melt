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

static int extract_ordered_layers(struct mapped_file *manifest, char ***arr);
static void free_ordered_layer_list(char **arr);
static int mmap_manifest(const char *path, struct mapped_file *f);
static int merge_layers(const char *image_out, const char *old_img_tmp,
			char **layers, char *tmp_prefix, bool del_whiteout,
			bool compress);
static void usage(const char *name);


int main(int argc, char *argv[])
{
	char **ordered_layers = NULL;
	int c, ret, fret = -1;
	size_t len;
	char *image = NULL, *image_out = NULL, *path = NULL;
	bool del_whiteout = false, compress = false;
	char *tmp_prefix = "/tmp";
	char old_img_tmp[PATH_MAX] = "/tmp/unify_XXXXXX";
	struct mapped_file f;

	while ((c = getopt(argc, argv, "cwt:i:o:")) != EOF) {
		switch (c) {
		case 'c':
			compress = true;
			break;
		case 'w':
			del_whiteout = true;
			break;
		case 't':
			tmp_prefix = optarg;
			ret = snprintf(old_img_tmp, PATH_MAX, "%s/unify_XXXXXX", tmp_prefix);
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

	len = strlen(old_img_tmp) + strlen(image) + 1 /* / */;
	path = malloc(len + 1);
	if (!path)
		goto out;

	ret = snprintf(path, len + 1, "%s/%s", old_img_tmp, image);
	if (ret < 0 || (size_t)ret >= len + 1)
		goto out;

	if (file_untar(image, old_img_tmp) < 0) {
		fprintf(stderr, "Failed to untar original image.\n");
		goto out;
	}

	ret = mmap_manifest(old_img_tmp, &f);
	if (ret < 0) {
		fprintf(stderr, "Failed to inspect manifest.json.\n");
		goto out;
	}

	if (extract_ordered_layers(&f, &ordered_layers) < 0) {
		fprintf(stderr, "Failed to extract layers.\n");
		goto out;
	}

	if (merge_layers(image_out, old_img_tmp, ordered_layers, tmp_prefix, del_whiteout, compress) < 0) {
		fprintf(stderr, "Failed merging layers.\n");
		goto out;
	}
	fret = 0;

out:
	recursive_rmdir(old_img_tmp);
	free(path);
	if (!ret)
		munmap_file_as_str(&f);
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

	char **tmp = realloc(*arr, (len + 2) * sizeof(char **));
	if (!tmp)
		return -1;
	*arr = tmp;
	(*arr)[len + 1] = NULL;
	return len;
}

static int extract_ordered_layers(struct mapped_file *manifest, char ***arr)
{
	ssize_t pos = 0;
	char *layers = strstr(manifest->buf, "\"Layers\":[\"");
	if (!layers)
		return -1; // corrupt manifest.json file

	char *list_start = layers + /* "Layers":[" */ + 10;
	char *list_end = strchr(list_start, ']');
	if (!list_end)
		return -1;
	*list_end = '\0';

	char *a = list_start, *b = list_start;
	while ((a = strchr(b, '"'))) {
		a++;
		b = strchr(a, '"');
		if (!b)
			return -1;
		pos = add_to_array(arr);
		if (pos < 0)
			return -1;
		size_t len = b - a;
		(*arr)[pos] = malloc(len + 1);
		if (!(*arr)[pos])
			return -1;
		strncpy((*arr)[pos], a, len);
		(*arr)[pos][len] = '\0'; // strncpy() does not necessarily \0-terminate
		b++;
	}

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
			char **layers, char *tmp_prefix, bool del_whiteout,
			bool compress)
{
	int r, ret = -1;
	char *path = NULL;
	char img_tmp1[PATH_MAX] = "/tmp/unify_XXXXXX";

	ret = snprintf(img_tmp1, PATH_MAX, "%s/unify_XXXXXX", tmp_prefix);
	if (ret < 0 || ret >= PATH_MAX)
		exit(EXIT_FAILURE);

	if (!mkdtemp(img_tmp1))
		return -1;
	if (chmod(img_tmp1, 0755) < 0)
		goto out_remove_tmp;

	for (; layers && *layers; layers++) {
		path = append_paths(old_img_tmp, *layers);
		if (!path)
			goto out_remove_tmp;
		r = file_untar(path, img_tmp1);
		free(path);
		if (r < 0)
			goto out_remove_tmp;
	}

	if (del_whiteout) {
		char cwd[PATH_MAX];
		if (!getcwd(cwd, PATH_MAX))
			goto out_remove_tmp;
		if (delete_whiteouts(img_tmp1) < 0)
			goto out_remove_tmp;
		if (chdir(cwd) < 0)
			goto out_remove_tmp;
	}

	if (file_tar(img_tmp1, image_out, compress) < 0)
		goto out_remove_tmp;

	ret = 0;

out_remove_tmp:
	recursive_rmdir(img_tmp1);
	return ret;
}

static int mmap_manifest(const char *path, struct mapped_file *f)
{
	int ret = -1;
	char *newpath = NULL;
	struct dirent dirent, *direntp;
	DIR *dir;
	// open global directory
	dir = opendir(path);
	if (!dir)
		return -1;

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
			return -1;

		ret = mmap_file_as_str(newpath, f);
		break;
	}

	free(newpath);
	closedir(dir);
	return ret;
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
	printf("-w\n");
	printf("	Delete whiteouts in final rootfs.\n");
	printf("-c\n");
	printf("	Compress tar file through xz.\n");
	printf("\n");
	exit(EXIT_FAILURE);
}
