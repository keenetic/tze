#include <fcntl.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "tze_tz.h"
#include "tze_err.h"
#include "tze_list.h"
#include "tze_rule.h"
#include "tze_name.h"
#include "tze_dentry.h"
#include "tze_version.h"
#include "tze_locality.h"

#define TZE_DEF_SEP						';'
#define TZE_CHR_SPACE					0x20
#define TZE_LOCALITY_MAX				PATH_MAX
#define TZE_SYSERROR_MAX				128

enum scan_t {
	SCAN_FILES,
	SCAN_LINKS
};

static int tze_extract(const char		 *const file_name,
					   const char		 *const locality,
					   const char		  sep,
					   const enum scan_t  scan,
					   struct tze_list_t *loc_list,
					   struct tze_err_t	 *err)
{
	char *rule = NULL;
	bool v3 = false;
	int ret = tze_tz_read(file_name, locality, &rule, &v3, err);

	if (ret != 0) {
		if (ret < 0) {
			return -1;
		}
		/* unknown file format, skip an entry */
		return 0;
	}

	ret = -1;

	if (tze_rule_check(rule, locality, v3, err) < 0) {
		goto free_rule;
	}

	if (tze_name_has_sep(rule, strlen(rule), sep)) {
		tze_err_set(err, 0,
					"%s: a timezone rule \"%s\" contains \"%c\" separator",
					locality, rule, sep);
		goto free_rule;
	}

	if (tze_name_has_sep(locality, strlen(locality), sep)) {
		tze_err_set(err, 0,
					"%s: a timezone locality contains \"%c\" separator",
					locality, sep);
		goto free_rule;
	}

	char *target_file = NULL;

	if (scan == SCAN_FILES) {
		struct tze_locality_t *loc = tze_locality_alloc(locality, rule);

		if (loc == NULL) {
			tze_err_set(err, errno,
						"%s: unable to allocate a locality", locality);
			goto free_rule;
		}

		tze_list_add_tail(loc_list, &loc->list);
	} else {
		target_file = realpath(file_name, NULL);

		if (target_file == NULL) {
			tze_err_set(err, errno,
						"%s: unable to read a symlink target", locality);
			goto free_rule;
		}

		const size_t file_name_size = strlen(file_name);
		const size_t locality_size = strlen(locality);

		if (file_name_size <= locality_size + 1) {
			tze_err_set(err, 0, "%s: invalid file name: \"%s\"",
						locality, file_name);
			goto free_target_file;
		}

		const size_t target_file_size = strlen(target_file);
		const size_t root_size = file_name_size - locality_size;

		if (target_file_size <= root_size ||
			memcmp(target_file, file_name, root_size) != 0) {
			tze_err_set(err, 0,
						"%s: a symlink points out of "
						"the timezone root directory", locality);
			goto free_target_file;
		}

		const char *const target = target_file + root_size;
		struct tze_locality_t *loc, *target_loc = NULL;

		tze_list_foreach_entry(loc, struct tze_locality_t, list, loc_list) {
			if (strcmp(loc->name, target) == 0) {
				target_loc = loc;
				break;
			}
		}

		if (target_loc == NULL) {
			tze_err_set(err, errno,
						"%s: no \"%s\" target found in a timezone list",
						locality, target);
			goto free_target_file;
		}

		if (tze_locality_add_link(target_loc, sep, locality) != 0) {
			tze_err_set(err, errno,
						"%s: unable to add a link for \"%s\" target",
						locality, target);
			goto free_target_file;
		}
	}

	ret = 0;

free_target_file:
	free(target_file);

free_rule:
	free(rule);
	return ret;
}

static int tze_filter(const struct dirent *const e)
{
	if (e->d_name[0] == '.') {
		if (e->d_name[1] == '\0') {
			return 0;
		}

		if (e->d_name[1] == '.' && e->d_name[2] == '\0') {
			return 0;
		}
	}

	return 1;
}

static int tze_compar(const struct dirent **l,
					  const struct dirent **r)
{
	return strcoll((*l)->d_name, (*r)->d_name);
}

static int tze_scan_dir(const char			*const dir_name,
						const size_t		 root_size,
						const char			 sep,
						const enum scan_t	 scan,
						struct tze_dentry_t	*dentry,
						struct tze_list_t	*loc_list,
						struct tze_err_t	*err)
{
	int ret = -1;
	int is_root = (strlen(dir_name) == root_size);
	struct dirent **namelist;
	const int n = scandir(dir_name, &namelist, tze_filter, tze_compar);

	if (n < 0) {
		tze_err_set(err, errno, "failed to list \"%s\" subdirectory",
					is_root ? "." : (dir_name + root_size));
		return ret;
	}

	size_t i = 0;

	for (; i < (size_t) n; i++) {
		const char *const d_name = namelist[i]->d_name;

		if (tze_dentry_set(dentry, dir_name, d_name) < 0) {
			tze_err_set(err, errno,
						"%s%s%s: unable to create a directory entry name",
						is_root ? "" : dir_name + root_size,
						is_root ? "" : "/",
						d_name);
			goto free_namelist;
		}

		struct stat st;
		const char *const locality = tze_dentry_name(dentry) + root_size + 1;

		if (lstat(tze_dentry_name(dentry), &st) < 0) {
			tze_err_set(err, errno,
						"failed to get \"%s\" "
						"directory entry information",
						locality);
			goto free_namelist;
		}

		if (S_ISDIR(st.st_mode)) {
			struct tze_dentry_t sub_dentry = TZE_DENTRY_INIT;
			const int scan_ret = tze_scan_dir(tze_dentry_name(dentry),
											  root_size, sep, scan,
											  &sub_dentry, loc_list, err);

			tze_dentry_free(&sub_dentry);

			if (scan_ret < 0) {
				goto free_namelist;
			}
		} else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
			const bool is_link = S_ISLNK(st.st_mode);

			if (((scan == SCAN_FILES) && !is_link) ||
				((scan == SCAN_LINKS) &&  is_link)) {
				if (strlen(locality) > TZE_LOCALITY_MAX) {
					tze_err_set(err, 0, "%s: a locality name is too long",
								locality);
					goto free_namelist;
				}

				if (tze_extract(tze_dentry_name(dentry), locality,
								sep, scan, loc_list, err) < 0) {
					goto free_namelist;
				}
			}
		} else {
			/* not a regular file, symlink or directory */
			tze_err_set(err, 0, "%s: unsupported filesystem node type",
						locality);
			goto free_namelist;
		}

		free(namelist[i]);
	}

	ret = 0;

free_namelist:
	for (; i < (size_t) n; i++) {
		free(namelist[i]);
	}

	free(namelist);
	return ret;
}

static int tze_check_sep(const char		   sep,
						 struct tze_err_t *err)
{
	static const char WRONG_SEP[] = "+-<>,./\r\n";

	for (size_t i = 0; i < sizeof(WRONG_SEP) - 1; i++) {
		if (sep == WRONG_SEP[i]) {
			goto wrong_sep;
		}
	}

	if (isalnum(sep)) {
		goto wrong_sep;
	}

	return 0;

wrong_sep:
	tze_err_set(err, 0, "\"%c\" (0x%02hhx) separator can not be used",
				(sep < TZE_CHR_SPACE) ? TZE_CHR_SPACE : sep, sep);
	return -1;
}

static void tze_show_error(const struct tze_err_t *err,
						   const char			  *const ident)
{
	const char *const msg = tze_err_msg(err);

	if (*msg == '\0') {
		/* already reported */
	} else {
		const int code = tze_err_code(err);
		char syserror[TZE_SYSERROR_MAX];

		if (code != 0) {
			snprintf(syserror, sizeof(syserror), "%s", strerror(code));
			syserror[0] = (char) tolower(syserror[0]);
		} else {
			syserror[0] = '\0';
		}

		fprintf(stderr, "*** Error: %s: %s%s%s.\n", ident, msg,
				(*syserror == '\0') ? "" : ": ", syserror);
	}
}

static int
tze_get_args(int			    argc,
			 char			  **argv,
			 const char		  **root,
			 char			   *sep,
			 struct tze_err_t  *err)
{
	*root = NULL;
	*sep = TZE_DEF_SEP;

	int sep_set = 0;

	while (1) {
		const int c = getopt(argc, argv, ":d:s:");

		if (c == -1) {
			break;
		}

		switch (c) {
		case 'd': {
			if (*root != NULL) {
				tze_err_set(err, 0, "\"%s\" root directory redefined",
							*root);
				goto wrong_args;
			}

			*root = optarg;
			break;
		}

		case 's': {
			if (sep_set) {
				tze_err_set(err, 0, "a separator character redefined");
				goto wrong_args;
			}

			if (strlen(optarg) > 1) {
				tze_err_set(err, 0,
							"\"%s\" separator should be "
							"a single ASCII character", optarg);
				goto wrong_args;
			}

			if (tze_check_sep(*optarg, err) < 0) {
				goto wrong_args;
			}

			*sep = *optarg;
			sep_set = 1;

			break;
		}

		case ':': {
			switch (optopt) {
			case 'd': {
				tze_err_set(err, 0,
							"\"-%c\" option requires a root directory name",
							(int) optopt);
				goto wrong_args;
			}

			case 's': {
				tze_err_set(err, 0,
							"\"-%c\" option requires "
							"a description separator",
							(int) optopt);
				goto wrong_args;
			}

			default:
				tze_err_set(err, 0, "unknown option \"-%c\"", (int) optopt);
				goto wrong_args;
			}
		}

		default:
			tze_err_set(err, 0, "unknown option \"-%c\"", (int) optopt);
			goto wrong_args;
		}
	}

	if (*root == NULL) {
		tze_err_set(err, 0, "no root directory specified");
		goto wrong_args;
	}

	if (optind != argc) {
		tze_err_set(err, 0, "unknown trailing arguments specified");
		goto wrong_args;
	}

	return 0;

wrong_args:
	return -1;
}

static int tze_show_usage()
{
	printf("Timezone extractor utility, v%s.\n"
		   "\n"
		   "  -d {root directory}\n"
		   "  -s {description separator} (default is \"%c\")\n",
		   TZE_VERSION,
		   TZE_DEF_SEP);

	return EXIT_FAILURE;
}

static int tze_loc_list_scan(struct tze_list_t *loc_list,
							 const char		   *const root,
							 const char			sep,
							 const enum scan_t  scan,
							 struct tze_err_t  *err)
{
	struct tze_dentry_t dentry = TZE_DENTRY_INIT;
	const size_t root_size = strlen(root);
	const int ret = tze_scan_dir(root, root_size, sep, scan,
								 &dentry, loc_list, err);
	tze_dentry_free(&dentry);

	return ret;
}

static void tze_loc_list_print(const struct tze_list_t *loc_list,
							   const char				sep)
{
	struct tze_locality_t *loc;

	tze_list_foreach_entry(loc, struct tze_locality_t, list, loc_list) {
		if (loc->links == NULL) {
			printf("%s%c%s\n", loc->name, sep, loc->rule);
		} else {
			printf("%s%c%s%c%s\n",
				   loc->name, sep, loc->links, sep, loc->rule);
		}
	}
}

static void tze_loc_list_free(struct tze_list_t *loc_list)
{
	while (!tze_list_is_empty(loc_list)) {
		struct tze_locality_t *loc = tze_list_entry(loc_list->next,
													struct tze_locality_t,
													list);
		tze_list_del(&loc->list);
		tze_locality_free(loc);
	}
}

int main(int    argc,
		 char **argv)
{
	if (argc <= 1) {
		return tze_show_usage();
	}

	int ret = -1;
	const char *root = NULL;
	char sep = TZE_DEF_SEP;
	struct tze_err_t err = TZE_ERR_INIT;

	if (tze_get_args(argc, argv, &root, &sep, &err) >= 0) {
		TZE_LIST_HEAD(loc_list);

		ret = tze_loc_list_scan(&loc_list, root, sep,
								SCAN_FILES, &err);

		if (ret >= 0) {
			if (tze_list_is_empty(&loc_list)) {
				tze_err_set(&err, 0, "no timezone files found");
				ret = -1;
			} else {
				ret = tze_loc_list_scan(&loc_list, root, sep,
										SCAN_LINKS, &err);

				if (ret >= 0) {
					tze_loc_list_print(&loc_list, sep);
				}
			}
		}

		tze_loc_list_free(&loc_list);
	}

	if (ret < 0) {
		const char *const name = strrchr(argv[0], '/');
		const char *const ident = (name == NULL) ? argv[0] : name + 1;

		tze_show_error(&err, ident);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
