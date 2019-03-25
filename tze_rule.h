#ifndef TZE_RULE_H
#define TZE_RULE_H

#include <stdbool.h>

struct tze_err_t;

int tze_rule_check(const char		*const rule,
				   const char		*const locality,
				   const bool		 v3,
				   struct tze_err_t *err);

#endif /* TZE_RULE_H */
