#include <stdio.h>
#include <stdarg.h>
#include "tze_err.h"

void tze_err_set(struct tze_err_t *err,
				 const int		   code,
				 const char		  *const format,
				 ...)
{
	va_list ap;

	err->code = code;

	va_start(ap, format);
	vsnprintf(err->msg, sizeof(err->msg), format, ap);
	va_end(ap);
}
