#ifndef TZE_NAME_H
#define TZE_NAME_H

#include <stddef.h>
#include <stdbool.h>

static inline bool tze_name_has_sep(const char   *const name,
									const size_t  name_size,
									const char	  sep)
{
	for (size_t i = 0; i < name_size; i++) {
		if (name[i] == sep) {
			return true;
		}
	}

	return false;
}

#endif /* TZE_NAME_H */
