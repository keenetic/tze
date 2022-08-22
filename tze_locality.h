#ifndef TZE_LOCALITY_H
#define TZE_LOCALITY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "tze_list.h"

struct tze_locality_t {
	char			  *name;
	char			  *links;
	char			  *rule;
	struct tze_list_t  list;
};

static inline void tze_locality_free(struct tze_locality_t *loc)
{
	if (loc != NULL) {
		free(loc->name);
		free(loc->links);
		free(loc->rule);
		free(loc);
	}
}

static inline struct tze_locality_t *
tze_locality_alloc(const char *const name,
				   const char *const rule)
{
	if (name == NULL || rule == NULL || *name == '\0' || *rule == '\0') {
		errno = -EINVAL;
		return NULL;
	}

	struct tze_locality_t *loc = malloc(sizeof(*loc));

	if (loc == NULL) {
		return NULL;
	}

	loc->name = strdup(name);
	loc->links = NULL;
	loc->rule = strdup(rule);
	tze_list_init(&loc->list);

	if (loc->name == NULL || loc->rule == NULL) {
		tze_locality_free(loc);
		errno = ENOMEM;
		return NULL;
	}

	return loc;
}

static inline int tze_locality_add_link(struct tze_locality_t *loc,
										const char			   sep,
										const char			  *const link)
{
	const size_t link_size = strlen(link);

	if (link_size == 0) {
		errno = EINVAL;
		return -1;
	}

	if (loc->links == NULL) {
		loc->links = strndup(link, link_size);

		if (loc->links == NULL) {
			return -1;
		}

		return 0;
	}

	const size_t loc_links_size = strlen(loc->links);
	char *links = realloc(loc->links, loc_links_size + 1 + link_size + 1);

	if (links == NULL) {
		return -1;
	}

	memcpy(links + loc_links_size + 1, link, link_size);
	links[loc_links_size] = sep;
	links[loc_links_size + 1 + link_size] = '\0';

	loc->links = links;

	return 0;
}

#endif /* TZE_LOCALITY_H */
