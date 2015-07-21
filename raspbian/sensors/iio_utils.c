/* IIO - useful set of util functionality
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <dirent.h>
#include <errno.h>
#include "iio_utils.h"

const char *iio_dir = "/sys/bus/iio/devices/";

static char * const iio_direction[] = {
        "in",
        "out",
};

/**
 * iioutils_break_up_name() - extract generic name from full channel name
 * @full_name: the full channel name
 * @generic_name: the output generic channel name
 *
 * Returns 0 on success, or a negative error code if string extraction failed.
 **/
int iioutils_break_up_name(const char *full_name,
				  char **generic_name)
{
	char *current = NULL;
	char *w, *r;
	char *working, *prefix = "";
	int i, ret = 0;

	for (i = 0; i < sizeof(iio_direction) / sizeof(iio_direction[0]); i++)
		if (!strncmp(full_name, iio_direction[i],
			     strlen(iio_direction[i]))) {
			prefix = iio_direction[i];
			break;
		}

	current = strdup(full_name + strlen(prefix) + 1);
	if (!current) {
		ret = -ENOMEM;
		goto error_ret;
	}

	working = strtok(current, "_\0");
	if (!working) {
		ret = -EINVAL;
		goto error_ret;
	}

	w = working;
	r = working;

	while (*r != '\0') {
		if (!isdigit(*r)) {
			*w = *r;
			w++;
		}
		r++;
	}
	*w = '\0';
	if (asprintf(generic_name, "%s_%s", prefix, working) < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}

error_ret:
	free(current);
	return ret;
}

/**
 * iioutils_get_type() - find and process _type attribute data
 * @is_signed: output whether channel is signed
 * @bytes: output how many bytes the channel storage occupies
 * @bits_used: output number of valid bits of data
 * @shift: output amount of bits to shift right data before applying bit mask
 * @mask: output a bit mask for the raw data
 * @be: output if data in big endian
 * @device_dir: the IIO device directory
 * @name: the channel name
 * @generic_name: the channel type name
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int iioutils_get_type(unsigned *is_signed,
			     unsigned *bytes,
			     unsigned *bits_used,
			     unsigned *shift,
			     uint64_t *mask,
			     unsigned *be,
			     const char *device_dir,
			     const char *name,
			     const char *generic_name)
{
	FILE *sysfsfp = NULL;
	int ret = -ENOENT;
	DIR *dp = NULL;
	char *scan_el_dir = NULL;
	char *builtname = NULL;
	char *builtname_generic = NULL;
	char *filename = NULL;
	char signchar, endianchar;
	unsigned padint;
	const struct dirent *ent;

	if ((asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir) < 0) ||
	    (asprintf(&builtname, FORMAT_TYPE_FILE, name) < 0) ||
	    (asprintf(&builtname_generic, FORMAT_TYPE_FILE, generic_name) < 0)) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_ret;
	}
	while (ent = readdir(dp), ent != NULL)
		/*
		 * Do we allow devices to override a generic name with
		 * a specific one?
		 */
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_ret;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				ret = -errno;
				printf("failed to open %s\n", filename);
				goto error_ret;
			}

			ret = fscanf(sysfsfp,
				     "%ce:%c%u/%u>>%u",
				     &endianchar,
				     &signchar,
				     bits_used,
				     &padint, shift);
			if (ret < 0) {
				ret = -errno;
				printf("failed to pass scan type description\n");
				goto error_ret;
			} else if (ret != 5) {
				ret = -EIO;
				printf("scan type description didn't match\n");
				goto error_ret;
			}
			*be = (endianchar == 'b');
			*bytes = padint / 8;
			if (*bits_used == 64)
				*mask = ~0;
			else
				*mask = (1 << *bits_used) - 1;
			*is_signed = (signchar == 's');
			if (fclose(sysfsfp)) {
				ret = -errno;
				printf("Failed to close %s\n", filename);
				goto error_ret;
			}
			sysfsfp = NULL;

			free(filename);
			filename = NULL;
		}

error_ret:
	if (sysfsfp)
		if (fclose(sysfsfp))
			perror("iioutils_get_type(): Failed to close file");
	if (dp)
		if (closedir(dp) == -1)
			perror("iioutils_get_type(): Failed to close directory");
	free(filename);
	free(builtname_generic);
	free(builtname);
	free(scan_el_dir);
	return ret;
}

/**
 * iioutils_get_param_float() - read a float value from a channel parameter
 * @output: output the float value
 * @param_name: the parameter name to read
 * @device_dir: the IIO device directory in sysfs
 * @name: the channel name
 * @generic_name: the channel type name
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int iioutils_get_param_float(float *output,
				    const char *param_name,
				    const char *device_dir,
				    const char *name,
				    const char *generic_name)
{
	FILE *sysfsfp = NULL;
	int ret = -ENOENT;
	DIR *dp = NULL;
	char *builtname = NULL;
	char *builtname_generic = NULL;
	char *filename = NULL;
	const struct dirent *ent;

	if ((asprintf(&builtname, "%s_%s", name, param_name) < 0) ||
	    (asprintf(&builtname_generic, "%s_%s", generic_name, param_name) < 0)) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dp = opendir(device_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_ret;
	}
	while (ent = readdir(dp), ent != NULL)
		if ((strcmp(builtname, ent->d_name) == 0) ||
		    (strcmp(builtname_generic, ent->d_name) == 0)) {
			ret = asprintf(&filename,
				       "%s/%s", device_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_ret;
			}
			sysfsfp = fopen(filename, "r");
			if (!sysfsfp) {
				ret = -errno;
				goto error_ret;
			}
			errno = 0;
			if (fscanf(sysfsfp, "%f", output) != 1)
				ret = errno ? -errno : -ENODATA;

			break;
		}

error_ret:
	if (sysfsfp)
		if (fclose(sysfsfp))
			perror("iioutils_get_param_float(): Failed to close file");
	if (dp)
		if (closedir(dp) == -1)
			perror("iioutils_get_param_float(): Failed to close directory");
	free(filename);
	free(builtname_generic);
	free(builtname);
	return ret;
}

/**
 * size_from_channelarray() - calculate the storage size of a scan
 * @channels:		the channel info array
 * @num_channels:	number of channels
 *
 * Has the side effect of filling the channels[i].location values used
 * in processing the buffer output.
 **/
int size_from_channelarray(struct iio_channel_info *channels, int num_channels)
{
	int bytes = 0;
	int i = 0;
	while (i < num_channels) {
		if (bytes % channels[i].bytes == 0)
			channels[i].location = bytes;
		else
			channels[i].location = bytes - bytes%channels[i].bytes
				+ channels[i].bytes;
		bytes = channels[i].location + channels[i].bytes;
		i++;
	}
	return bytes;
}

/**
 * bsort_channel_array_by_index() - sort the array in index order
 * @ci_array: the iio_channel_info array to be sorted
 * @cnt: the amount of array elements
 **/

void bsort_channel_array_by_index(struct iio_channel_info **ci_array,
					 int cnt)
{

	struct iio_channel_info temp;
	int x, y;

	for (x = 0; x < cnt; x++)
		for (y = 0; y < (cnt - 1); y++)
			if ((*ci_array)[y].index > (*ci_array)[y+1].index) {
				temp = (*ci_array)[y + 1];
				(*ci_array)[y + 1] = (*ci_array)[y];
				(*ci_array)[y] = temp;
			}
}

/**
 * build_channel_array() - function to figure out what channels are present
 * @device_dir: the IIO device directory in sysfs
 * @ci_array: output the resulting array of iio_channel_info
 * @counter: output the amount of array elements
 *
 * Returns 0 on success, otherwise a negative error code.
 **/
int build_channel_array(const char *device_dir,
			      struct iio_channel_info **ci_array,
			      int *counter)
{
	int ret;
	int count;
	char *scan_el_dir = NULL;
	DIR *dp = NULL;
	const struct dirent *ent;
	char *filename = NULL;
	FILE *sysfsfp = NULL;
	struct iio_channel_info *current;

	*ci_array = NULL;
	*counter = 0;
	ret = asprintf(&scan_el_dir, FORMAT_SCAN_ELEMENTS_DIR, device_dir);
	if (ret < 0) {
		ret = -ENOMEM;
		goto error_ret;
	}
	dp = opendir(scan_el_dir);
	if (dp == NULL) {
		ret = -errno;
		goto error_ret;
	}
	while (ent = readdir(dp), ent != NULL)
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_ret;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				ret = -errno;
				goto error_ret;
			}
			errno = 0;
			if (fscanf(sysfsfp, "%i", &ret) != 1) {
				ret = errno ? -errno : -ENODATA;
				goto error_ret;
			}

			if (ret == 1)
				(*counter)++;
			if (fclose(sysfsfp)) {
				ret = -errno;
				goto error_ret;
			}
			sysfsfp = NULL;

			free(filename);
			filename = NULL;
		}

	*ci_array = malloc(sizeof(**ci_array) * (*counter));
	if (*ci_array == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	memset(*ci_array, 0, sizeof(**ci_array) * (*counter));
	seekdir(dp, 0);
	count = 0;
	while ((count < *counter) && (ent = readdir(dp), ent != NULL)) {
		if (strcmp(ent->d_name + strlen(ent->d_name) - strlen("_en"),
			   "_en") == 0) {
			int current_enabled = 0;
			current = &(*ci_array)[count];
			ret = asprintf(&filename,
				       "%s/%s", scan_el_dir, ent->d_name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_ret;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				ret = -errno;
				goto error_ret;
			}
			errno = 0;
			if (fscanf(sysfsfp, "%i", &current_enabled) != 1) {
				ret = errno ? -errno : -ENODATA;
				goto error_ret;
			}

			if (fclose(sysfsfp)) {
				ret = -errno;
				goto error_ret;
			}
			sysfsfp = NULL;
			if (!current_enabled) {
				free(filename);
				filename = NULL;
				continue;
			}
			count++;

			current->scale = 1.0;
			current->offset = 0;
			current->name = strndup(ent->d_name,
						strlen(ent->d_name) -
						strlen("_en"));
			if (current->name == NULL) {
				ret = -ENOMEM;
				goto error_ret;
			}
			// Get the generic and specific name elements
			ret = iioutils_break_up_name(current->name,
						     &current->generic_name);
			if (ret)
				goto error_ret;

			free(filename);
			filename = NULL;
			ret = asprintf(&filename,
				       "%s/%s_index",
				       scan_el_dir,
				       current->name);
			if (ret < 0) {
				ret = -ENOMEM;
				goto error_ret;
			}
			sysfsfp = fopen(filename, "r");
			if (sysfsfp == NULL) {
				ret = -errno;
				printf("failed to open %s\n", filename);
				goto error_ret;
			}

			errno = 0;
			if (fscanf(sysfsfp, "%u", &current->index) != 1) {
				ret = errno ? -errno : -ENODATA;
				goto error_ret;
			}

			if (fclose(sysfsfp)) {
				ret = -errno;
				goto error_ret;
			}
			sysfsfp = NULL;

			free(filename);
			filename = NULL;
			// Find the scale
			ret = iioutils_get_param_float(&current->scale,
						       "scale",
						       device_dir,
						       current->name,
						       current->generic_name);
			if (ret < 0)
				goto error_ret;
			// offset is optional
			iioutils_get_param_float(&current->offset,
						 "offset",
						 device_dir,
						 current->name,
						 current->generic_name);
			ret = iioutils_get_type(&current->is_signed,
						&current->bytes,
						&current->bits_used,
						&current->shift,
						&current->mask,
						&current->be,
						device_dir,
						current->name,
						current->generic_name);
			if (ret < 0)
				goto error_ret;
			ret = 0;
		}
	}

	// reorder so that the array is in index order
	bsort_channel_array_by_index(ci_array, count);

error_ret:
	if (*ci_array)
	{
		for (count = 0; count < (*counter); count++)
		{
			current = &(*ci_array)[count];
			if (current->name)
			{
				free(current->name);
				current->name = NULL;
			}
			if (current->name)
			{
				free(current->generic_name);
				current->generic_name = NULL;
			}
		}
	}
	if (sysfsfp)
		if (fclose(sysfsfp))
			perror("build_channel_array(): Failed to close file");
	free(filename);
	if (dp)
		if (closedir(dp) == -1)
			perror("build_channel_array(): Failed to close dir");
	free(scan_el_dir);
	return ret;
}


static int calc_digits(int num)
{
	int count = 0;

	while (num != 0) {
		num /= 10;
		count++;
	}

	return count;
}

/**
 * find_type_by_name() - function to match top level types by name
 * @name: top level type instance name
 * @type: the type of top level instance being searched
 *
 * Returns the device number of a matched IIO device on success, otherwise a
 * negative error code.
 * Typical types this is used for are device and trigger.
 **/
int find_type_by_name(const char *name, const char *type)
{
	const struct dirent *ent;
	int number, numstrlen, ret;

	FILE *namefp = NULL;
	DIR *dp = NULL;
	char thisname[IIO_MAX_NAME_LENGTH];
	char *filename = NULL;

	dp = opendir(iio_dir);
	if (dp == NULL) {
		printf("No industrialio devices available\n");
		return -ENODEV;
	}

	while (ent = readdir(dp), ent != NULL) {
		if (strcmp(ent->d_name, ".") != 0 &&
			strcmp(ent->d_name, "..") != 0 &&
			strlen(ent->d_name) > strlen(type) &&
			strncmp(ent->d_name, type, strlen(type)) == 0) {
			errno = 0;
			ret = sscanf(ent->d_name + strlen(type), "%d", &number);
			if (ret < 0) {
				ret = -errno;
				printf("failed to read element number\n");
				goto error_close_dir;
			} else if (ret != 1) {
				ret = -EIO;
				printf("failed to match element number\n");
				goto error_close_dir;
			}

			numstrlen = calc_digits(number);
			/* verify the next character is not a colon */
			if (strncmp(ent->d_name + strlen(type) + numstrlen,
					":",
					1) != 0) {
				filename = malloc(strlen(iio_dir)
						+ strlen(type)
						+ numstrlen
						+ 6);
				if (filename == NULL) {
					ret = -ENOMEM;
					goto error_close_dir;
				}

				ret = sprintf(filename, "%s%s%d/name", iio_dir,
					      type, number);
				if (ret < 0) {
					free(filename);
					filename = NULL;
					goto error_close_dir;
				}

				namefp = fopen(filename, "r");
				if (!namefp) {
					free(filename);
					filename = NULL;
					continue;
				}
				free(filename);
				filename = NULL;
				errno = 0;
				if (fscanf(namefp, "%s", thisname) != 1) {
					ret = errno ? -errno : -ENODATA;
					goto error_close_dir;
				}

				if (fclose(namefp)) {
					ret = -errno;
					goto error_close_dir;
				}
				namefp = NULL;

				if (strcmp(name, thisname) == 0) {
					if (closedir(dp) == -1)
						return -errno;
					return number;
				}
			}
		}
	}
	if (closedir(dp) == -1)
		return -errno;

	return -ENODEV;

error_close_dir:
	if (closedir(dp) == -1)
		perror("find_type_by_name(): Failed to close directory");
	return ret;
}

static int _write_sysfs_int(const char *filename, char *basedir, int val, int verify)
{
	int ret = 0;
	FILE *sysfsfp;
	int test;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL)
		return -ENOMEM;
	ret = sprintf(temp, "%s/%s", basedir, filename);
	if (ret < 0)
		goto error_free;

	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		ret = -errno;
		printf("failed to open %s\n", temp);
		goto error_free;
	}
	ret = fprintf(sysfsfp, "%d", val);
	if (ret < 0) {
		if (fclose(sysfsfp))
			perror("_write_sysfs_int(): Failed to close dir");

		goto error_free;
	}

	if (fclose(sysfsfp)) {
		ret = -errno;
		goto error_free;
	}

	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			ret = -errno;
			printf("failed to open %s\n", temp);
			goto error_free;
		}
		if (fscanf(sysfsfp, "%d", &test) != 1) {
			ret = errno ? -errno : -ENODATA;
			if (fclose(sysfsfp))
				perror("_write_sysfs_int(): Failed to close dir");

			goto error_free;
		}

		if (fclose(sysfsfp)) {
			ret = -errno;
			goto error_free;
		}

		if (test != val) {
			printf("Possible failure in int write %d to %s%s\n",
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);
	return ret;
}

/**
 * write_sysfs_int() - write an integer value to a sysfs file
 * @filename: name of the file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: integer value to write to file
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int write_sysfs_int(const char *filename, char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 0);
}

/**
 * write_sysfs_int_and_verify() - write an integer value to a sysfs file
 *				  and verify
 * @filename: name of the file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: integer value to write to file
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int write_sysfs_int_and_verify(const char *filename, char *basedir, int val)
{
	return _write_sysfs_int(filename, basedir, val, 1);
}

static int _write_sysfs_string(const char *filename, char *basedir, char *val, int verify)
{
	int ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed\n");
		return -ENOMEM;
	}
	ret = sprintf(temp, "%s/%s", basedir, filename);
	if (ret < 0)
		goto error_free;

	sysfsfp = fopen(temp, "w");
	if (sysfsfp == NULL) {
		ret = -errno;
		printf("Could not open %s\n", temp);
		goto error_free;
	}
	ret = fprintf(sysfsfp, "%s", val);
	if (ret < 0) {
		if (fclose(sysfsfp))
			perror("_write_sysfs_string(): Failed to close dir");

		goto error_free;
	}

	if (fclose(sysfsfp)) {
		ret = -errno;
		goto error_free;
	}

	if (verify) {
		sysfsfp = fopen(temp, "r");
		if (sysfsfp == NULL) {
			ret = -errno;
			printf("could not open file to verify\n");
			goto error_free;
		}
		if (fscanf(sysfsfp, "%s", temp) != 1) {
			ret = errno ? -errno : -ENODATA;
			if (fclose(sysfsfp))
				perror("_write_sysfs_string(): Failed to close dir");

			goto error_free;
		}

		if (fclose(sysfsfp)) {
			ret = -errno;
			goto error_free;
		}

		if (strcmp(temp, val) != 0) {
			printf("Possible failure in string write of %s "
				"Should be %s "
				"written to %s\%s\n",
				temp,
				val,
				basedir,
				filename);
			ret = -1;
		}
	}
error_free:
	free(temp);

	return ret;
}

/**
 * write_sysfs_string_and_verify() - string write, readback and verify
 * @filename: name of file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: the string to write
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int write_sysfs_string_and_verify(const char *filename, char *basedir, char *val)
{
	return _write_sysfs_string(filename, basedir, val, 1);
}

/**
 * write_sysfs_string() - write string to a sysfs file
 * @filename: name of file to write to
 * @basedir: the sysfs directory in which the file is to be found
 * @val: the string to write
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int write_sysfs_string(const char *filename, char *basedir, char *val)
{
	return _write_sysfs_string(filename, basedir, val, 0);
}

/**
 * read_sysfs_posint() - read an integer value from file
 * @filename: name of file to read from
 * @basedir: the sysfs directory in which the file is to be found
 *
 * Returns the read integer value >= 0 on success, otherwise a negative error
 * code.
 **/
int read_sysfs_posint(const char *filename, char *basedir)
{
	int ret;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	ret = sprintf(temp, "%s/%s", basedir, filename);
	if (ret < 0)
		goto error_free;

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	errno = 0;
	if (fscanf(sysfsfp, "%d\n", &ret) != 1) {
		ret = errno ? -errno : -ENODATA;
		if (fclose(sysfsfp))
			perror("read_sysfs_posint(): Failed to close dir");

		goto error_free;
	}

	if (fclose(sysfsfp))
		ret = -errno;

error_free:
	free(temp);
	return ret;
}

/**
 * read_sysfs_float() - read a float value from file
 * @filename: name of file to read from
 * @basedir: the sysfs directory in which the file is to be found
 * @val: output the read float value
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int read_sysfs_float(const char *filename, char *basedir, float *val)
{
	int ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	ret = sprintf(temp, "%s/%s", basedir, filename);
	if (ret < 0)
		goto error_free;

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	errno = 0;
	if (fscanf(sysfsfp, "%f\n", val) != 1) {
		ret = errno ? -errno : -ENODATA;
		if (fclose(sysfsfp))
			perror("read_sysfs_float(): Failed to close dir");

		goto error_free;
	}

	if (fclose(sysfsfp))
		ret = -errno;

error_free:
	free(temp);
	return ret;
}

/**
 * read_sysfs_string() - read a string from file
 * @filename: name of file to read from
 * @basedir: the sysfs directory in which the file is to be found
 * @str: output the read string
 *
 * Returns a value >= 0 on success, otherwise a negative error code.
 **/
int read_sysfs_string(const char *filename, const char *basedir, char *str)
{
	int ret = 0;
	FILE  *sysfsfp;
	char *temp = malloc(strlen(basedir) + strlen(filename) + 2);
	if (temp == NULL) {
		printf("Memory allocation failed");
		return -ENOMEM;
	}
	ret = sprintf(temp, "%s/%s", basedir, filename);
	if (ret < 0)
		goto error_free;

	sysfsfp = fopen(temp, "r");
	if (sysfsfp == NULL) {
		ret = -errno;
		goto error_free;
	}
	errno = 0;
	if (fscanf(sysfsfp, "%s\n", str) != 1) {
		ret = errno ? -errno : -ENODATA;
		if (fclose(sysfsfp))
			perror("read_sysfs_string(): Failed to close dir");

		goto error_free;
	}

	if (fclose(sysfsfp))
		ret = -errno;

error_free:
	free(temp);
	return ret;
}
