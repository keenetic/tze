#ifndef TZE_TZ_H
#define TZE_TZ_H

#include <stdint.h>
#include <stdbool.h>

struct tze_err_t;

int tze_tz_read(const char		  *const file_name,
				const char		  *const zone_name,
				char			 **rule,
				bool			  *v3,
				struct tze_err_t  *err);

#endif /* TZE_TZ_H */
