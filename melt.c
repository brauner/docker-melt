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

#include "list.h"
#include "utils.h"

struct list layer_list;

__attribute__((constructor))
void layer_list_constructor(void)
{
	list_init(&layer_list);
}

struct layer {
	char *id;
	char *parent;
	char *path;
	char *tar_path;
};

static char *extract_field(const char *field, const char *json);
static void free_layer_list(void);
static bool is_json(const char *file);
static struct layer *find_child(const char *id);
static int merge_layers(const char *image_out, const char *old_img_tmp,
			char *new_img_tmp, bool del_whiteout, bool compress);
static int open_layer_dir(const char *path);
static void usage(const char *name);


int main(int argc, char *argv[])
{
	int c, ret, fret = -1;
	size_t len;
	char *image = NULL, *image_out = NULL, *path = NULL;
	bool del_whiteout = false, compress = false;
	char old_img_tmp[PATH_MAX] = "/tmp/unify_XXXXXX";
	char new_img_tmp[PATH_MAX] = "/tmp/unify_XXXXXX";

	while ((c = getopt(argc, argv, "cwt:i:o:")) != EOF) {
		switch (c) {
		case 'c':
			compress = true;
			break;
		case 'w':
			del_whiteout = true;
			break;
		case 't':
			ret = snprintf(old_img_tmp, PATH_MAX, "%s/unify_XXXXXX", optarg);
			if (ret < 0 || ret >= PATH_MAX)
				exit(EXIT_FAILURE);
			ret = snprintf(new_img_tmp, PATH_MAX, "%s/unify_XXXXXX", optarg);
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

	if (open_layer_dir(old_img_tmp) < 0) {
		fprintf(stderr, "Failed to inspect layers.\n");
		goto out;
	}

	if (merge_layers(image_out, old_img_tmp, new_img_tmp, del_whiteout, compress) < 0) {
		fprintf(stderr, "Failed merging layers.\n");
		goto out;
	}
	fret = 0;

out:
	free_layer_list();
	recursive_rmdir(old_img_tmp);
	free(path);
	if (!fret)
		exit(EXIT_SUCCESS);
	exit(EXIT_FAILURE);
}

static char *extract_field(const char *field, const char *json)
{
	char *dup = NULL, *parent = NULL, *ret = NULL;
	size_t field_len;

	// field"1243sadf134fdsa13fd"
	dup = malloc(strlen(json) + 1);
	if (!dup)
		goto out;
	char *end = stpcpy(dup, json);

	parent = strstr(dup, field);
	if (!parent)
		goto out;

	field_len = strlen(field);
	// paranoid: check if we would point beyond the end of dup
	if ((parent + field_len + 1) > end)
		goto out;

	parent = parent + field_len + 1;
	// 1243sadf134fdsa13fd"

	// \"
	char *tmp = strchr(parent, '\"');
	if (!tmp)
		goto out;
	*tmp = '\0';

	ret = strdup(parent);
out:
	free(dup);
	return ret ;
}

static void free_layer_list(void)
{
	struct list *it;
	struct layer *lcast;
	list_for_each_safe(it, &layer_list, (&layer_list)->next) {
		lcast = it->elem;
		free(lcast->id);
		free(lcast->parent);
		free(lcast->path);
		free(lcast->tar_path);
		free(lcast);
		free(it);
	}
	/* Do not call free(it) here: The first node of layer_list is a non-heap
	 * object located in the data segment. */
}

static struct layer *find_child(const char *id)
{
	struct list *it;
	struct layer *lcast;

	list_for_each(it, &layer_list) {
		lcast = it->elem;
		// If no id was given we want to find the root ancestor.
		if (!id) {
			if (!lcast->parent)
				return lcast;
		} else {
			if (lcast->parent && !strcmp(lcast->parent, id))
				return lcast;
		}
	}
	/* Should not happen. (If it does it probably means we found a corrupt
	 * json file.) */
	return NULL;
}

static bool is_json(const char *file)
{
	if (!strcmp(file, "json"))
		return true;

	return false;
}

static int merge_layers(const char *image_out, const char *old_img_tmp,
			char *new_img_tmp, bool del_whiteout, bool compress)
{
	int ret = -1;
	size_t i;
	size_t len = list_len(&layer_list);
	struct layer *child, *cur;

	cur = find_child(NULL);
	if (!cur) {
		fprintf(stderr, "Failed to find root ancestor of all layers.\n");
		return -1;
	}

	if (!mkdtemp(new_img_tmp))
		return -1;
	if (chmod(new_img_tmp, 0755) < 0)
		goto out_remove_tmp;

	// root ancestor
	if (file_untar(cur->tar_path, new_img_tmp) < 0)
		goto out_remove_tmp;

	if (recursive_rmdir(cur->path) < 0)
		goto out_remove_tmp;

	/* Return hierarchy of layers starting from the root ancestor.
	 * (len - 1, because we need to account for the fact that we already
	 * extracted the parent.) */
	for (i = 0; i < len - 1; i++) {
		child = find_child(cur->id);
		if (!child) {
			fprintf(stderr, "Invalid json file.\n");
			goto out_remove_tmp;
		}

		if (file_untar(child->tar_path, new_img_tmp) < 0)
			goto out_remove_tmp;

		if (recursive_rmdir(child->path) < 0)
			goto out_remove_tmp;

		cur = child;
	}

	if (del_whiteout) {
		char cwd[PATH_MAX];
		if (!getcwd(cwd, PATH_MAX))
			goto out_remove_tmp;
		if (delete_whiteouts(new_img_tmp) < 0)
			goto out_remove_tmp;
		if (chdir(cwd) < 0)
			goto out_remove_tmp;
	}

	/* tar into one single layer and overwrite current topmost layer of the
	 * youngest child. */
	if (file_tar(new_img_tmp, image_out, compress) < 0)
		goto out_remove_tmp;

	ret = 0;

out_remove_tmp:
	recursive_rmdir(new_img_tmp);
	return ret;
}

static int open_layer_dir(const char *path)
{
	char *tar_path = NULL, *parent = NULL, *id = NULL, *json = NULL,
	     *newpath = NULL;
	struct list *new_lnode;
	struct layer *new_layer;
	struct dirent dirent, *direntp;
	struct dirent layerdirent, *layerdirentp;
	struct mapped_file m;
	DIR *dir;

	// open global directory
	dir = opendir(path);
	if (!dir)
		return -1;

	while (!readdir_r(dir, &dirent, &direntp)) {
		DIR *layerdir;

		if (!direntp)
			break;

		if (!strcmp(direntp->d_name, ".") ||
				!strcmp(direntp->d_name, ".."))
			continue;

		if (direntp->d_type != DT_DIR)
			continue;

		// create path to layer directory
		newpath = append_paths(path, direntp->d_name);
		if (!newpath)
			return -1;

		// descend into layer
		layerdir = opendir(newpath);
		if (!layerdir) {
			free(newpath);
			return -1;
		}

		while (!readdir_r(layerdir, &layerdirent, &layerdirentp)) {
			if (!layerdirentp)
				break;

			if (!strcmp(layerdirentp->d_name, ".") ||
					!strcmp(layerdirentp->d_name, ".."))
				continue;

			if (!is_json(layerdirentp->d_name))
				continue;

			// create path to json file
			json = append_paths(newpath, layerdirentp->d_name);
			if (!json) {
				closedir(layerdir);
				goto cleanup_on_error;
			}

			if (mmap_file_as_str(json, &m) < 0)
				goto cleanup_on_error;

			// extract id of current layer
			id = extract_field("\"id\":", m.buf);
			if (!id && errno == ENOMEM) {
				munmap_file_as_str(&m);
				closedir(layerdir);
				goto cleanup_on_error;
			}

			// extract parent of current layer
			parent = extract_field("\"parent\":", m.buf);
			if (!parent && errno == ENOMEM) {
				munmap_file_as_str(&m);
				closedir(layerdir);
				goto cleanup_on_error;
			}

			// new list element
			new_layer = malloc(sizeof(*new_layer));
			if (!new_layer) {
				munmap_file_as_str(&m);
				closedir(layerdir);
				goto cleanup_on_error;
			}

			// path to layer.tar file
			tar_path = append_paths(newpath, "layer.tar");
			if (!tar_path) {
				munmap_file_as_str(&m);
				closedir(layerdir);
				goto cleanup_on_error;
			}

			// new list node
			new_lnode = malloc(sizeof(*new_lnode));
			if (!new_lnode) {
				munmap_file_as_str(&m);
				closedir(layerdir);
				goto cleanup_on_error;
			}

			new_layer->id = id;
			new_layer->path = newpath;
			new_layer->parent = parent;
			new_layer->tar_path = tar_path;
			list_add_elem(new_lnode, new_layer);
			list_add_tail(&layer_list, new_lnode);

			free(json);
			munmap_file_as_str(&m);
		}
		closedir(layerdir);
	}
	closedir(dir);

	return 0;

cleanup_on_error:
	free(id);
	free(json);
	free(newpath);
	free(parent);
	free(tar_path);
	closedir(dir);
	return -1;
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
