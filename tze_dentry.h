#ifndef TZE_DENTRY_H
#define TZE_DENTRY_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#define TZE_DENTRY_INIT					\
	{									\
		.name		= 0,				\
		.capacity	= 0					\
	}

struct tze_dentry_t {
	char   *name;
	size_t  capacity;
};

static inline void tze_dentry_init(struct tze_dentry_t *dentry)
{
	dentry->name = NULL;
	dentry->capacity = 0;
}

static inline int tze_dentry_set(struct tze_dentry_t *dentry,
								 const char			 *const root,
								 const char			 *const leaf)
{
	const int n = snprintf(dentry->name, dentry->capacity,
						   "%s/%s", root, leaf);

	if (n < 0) {
		/* snprintf() format error */
		return -1;
	}

	if ((size_t) n < dentry->capacity) {
		return 0;
	}

	const size_t capacity = (size_t) (n + 1);
	char *name = realloc(dentry->name, capacity);

	if (name == NULL) {
		return -1;
	}

	dentry->name = name;
	dentry->capacity = capacity;

	return tze_dentry_set(dentry, root, leaf);
}

static inline const char *tze_dentry_name(struct tze_dentry_t *dentry)
{
	return dentry->name;
}

static inline void tze_dentry_free(struct tze_dentry_t *dentry)
{
	free(dentry->name);
	tze_dentry_init(dentry);
}

#endif /* TZE_DENTRY_H */
