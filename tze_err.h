#ifndef TZE_ERR_H
#define TZE_ERR_H

#include "tze_attr.h"

#define TZE_ERR_MSG_MAX_LEN				256

#define TZE_ERR_INIT					\
	{									\
		.code	= 0,					\
		.msg	= { '\0' }				\
	}

struct tze_err_t {
	int	 code;
	char msg[TZE_ERR_MSG_MAX_LEN];
};

static inline int
tze_err_code(const struct tze_err_t *const err)
{
	return err->code;
}

static inline const char *
tze_err_msg(const struct tze_err_t *const err)
{
	return err->msg;
}

void tze_err_set(struct tze_err_t *err,
				 const int		   code,
				 const char		  *const format,
				 ...) TZE_ATTR_PRINTF(3, 4);

static inline void
tze_err_clear(struct tze_err_t *err)
{
	err->code = 0;
	err->msg[0] = '\0';
}

#endif /* TZE_ERR_H */
