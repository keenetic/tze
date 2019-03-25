#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <inttypes.h>
#include "tze_err.h"
#include "tze_rule.h"

/**
 * For a full POSIX rule description see
 * http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html
 **/

#define TZE_M_IN_H						(60)
#define TZE_S_IN_M						(60)

#define TZE_MIN_HOURS					(0)
#define TZE_MAX_HOURS					(24)
#define TZE_MAX_HOURS_V3				(167)
#define TZE_MIN_MINUTES					(0)
#define TZE_MAX_MINUTES					(59)
#define TZE_MIN_SECONDS					(0)
#define TZE_MAX_SECONDS					(59)

#define TZE_MAX_OFFSET					\
	(TZE_MAX_HOURS * TZE_M_IN_H * TZE_S_IN_M)

#define TZE_MAX_OFFSET_V3				\
	(TZE_MAX_HOURS_V3 * TZE_M_IN_H * TZE_S_IN_M)

#define TZE_MIN_NAME					(3)
#define TZE_MAX_NAME					(32) /* system-dependent really */

#define TZE_MIN_DAY						(1)
#define TZE_MAX_DAY						(365)
#define TZE_MIN_MONTH					(1)
#define TZE_MAX_MONTH					(12)
#define TZE_MIN_WEEK					(1)
#define TZE_MAX_WEEK					(5)
#define TZE_MIN_WDAY					(0)
#define TZE_MAX_WDAY					(6)

static size_t tze_rule_max_name_length()
{
	long max_length = -1;

#if defined(_SC_TZNAME_MAX)

	max_length = sysconf(_SC_TZNAME_MAX);

#endif

	if (max_length <= 0) {
#if defined(_POSIX_TZNAME_MAX)

		max_length = _POSIX_TZNAME_MAX;

#endif
	}

	if (max_length <= 0) {
		max_length = TZE_MAX_NAME;
	}

	return (size_t) max_length;
}

static int
tze_rule_check_name(const char **rule)
{
	/**
	 * The name string specifies the name of the time zone.
	 * It must be three or more characters long and must not contain
	 * a leading colon, embedded digits, commas, nor plus and minus signs.
	 **/

	const char *p = *rule;
	size_t length = 0;

	if (*p == ':') {
		goto wrong_name;
	}

	if (*p == '<') {
		/* quoted name: "<+04>..." */
		p++;

		const char *const start = p;

		if (*p != '+' && *p != '-') {
			goto wrong_name;
		}

		p++;

		while (isalnum(*p)) {
			p++;
		}

		if (*p != '>') {
			goto wrong_name;
		}

		length = (size_t) (p - start);
		p++;
	} else {
		/* unquoted name: "CET..." */
		const char *const start = p;

		while (isalpha(*p)) {
			p++;
		}

		length = (size_t) (p - start);
	}

	if (length < TZE_MIN_NAME ||
		length > tze_rule_max_name_length()) {
		goto wrong_name;
	}

	*rule = p;
	return 0;

wrong_name:
	*rule = p;
	return -1;
}

static int
tze_rule_check_int(const char	 **rule,
				   const int32_t   min,
				   const int32_t   max,
				   int32_t		  *n)
{
	*n = 0;
	errno = 0;

	const char *start = *rule;
	char *end = NULL;
	const uintmax_t res = strtoumax(start, &end, 10);

	if (end != NULL) {
		*rule = end;
	}

	if (errno != 0) {
		return -1;
	}

#if INT32_MAX < UINTMAX_MAX

	if (res > INT32_MAX) {
		return -1;
	}

#endif

	*n = (int32_t) res;

	if (*n < min || *n > max) {
		*n = 0;
		return -1;
	}

	return 0;
}

static int tze_rule_check_offset(const char **rule,
								 const bool	  v3)
{
	const char *p = *rule;

	if (*p != '+' && *p != '-' && !isdigit(*p)) {
		return -1;
	}

	if (!isdigit(*p)) {
		p++;
	}

	/**
	 * Parse a time string in the following format: "hh[:mm[:ss]]".
	 **/

	int32_t h = TZE_MIN_HOURS;
	int32_t m = TZE_MIN_MINUTES;
	int32_t s = TZE_MIN_SECONDS;
	int ret = tze_rule_check_int(&p, TZE_MIN_HOURS,
								 v3 ? TZE_MAX_HOURS_V3 : TZE_MAX_HOURS, &h);

	if (ret < 0) {
		goto wrong_offset;
	}

	if (*p == ':') {
		p++;
		ret = tze_rule_check_int(&p, TZE_MIN_MINUTES, TZE_MAX_MINUTES, &m);

		if (ret < 0) {
			goto wrong_offset;
		}

		if (*p == ':') {
			p++;
			ret = tze_rule_check_int(&p, TZE_MIN_SECONDS,
									 TZE_MAX_SECONDS, &s);

			if (ret < 0) {
				goto wrong_offset;
			}
		}
	}

	const int32_t offset = h * TZE_M_IN_H * TZE_S_IN_M +
						   m * TZE_S_IN_M +
						   s;
	const int32_t max_offset = v3 ? TZE_MAX_OFFSET_V3 : TZE_MAX_OFFSET;

	if (offset > max_offset) {
		goto wrong_offset;
	}

	*rule = p;
	return 0;

wrong_offset:
	*rule = p;
	return -1;
}

static int tze_rule_check_date(const char **rule,
							   const bool	v3)
{
	const char *p = *rule;
	int32_t n = 0;
	int ret = -1;

	if (*p == ',') {
		p++;

		if (*p == 'J') {
			/**
			 * "Jn" format.
			 * This specifies the Julian day, with n between 1 and 365.
			 * February 29 is never counted, even in leap years.
			 **/

			p++;
			ret = tze_rule_check_int(&p, TZE_MIN_DAY, TZE_MAX_DAY, &n);
		} else if (*p == 'M') {
			/**
			 * "Mm.w.d" format.
			 * This specifies day d of week w of month m.
			 * The day d must be between 0 (Sunday) and 6.
			 * The week w must be between 1 and 5;
			 * week 1 is the first week in which day d occurs,
			 * and week 5 specifies the last d day in the month.
			 * The month m should be between 1 and 12.
			 **/

			p++;
			ret = tze_rule_check_int(&p, TZE_MIN_MONTH, TZE_MAX_MONTH, &n);

			if (ret == 0 && *p == '.') {
				p++;
				ret = tze_rule_check_int(&p, TZE_MIN_WEEK, TZE_MAX_WEEK, &n);

				if (ret == 0 && *p == '.') {
					p++;
					ret = tze_rule_check_int(&p, TZE_MIN_WDAY,
											 TZE_MAX_WDAY, &n);
				}
			}
		} else if (isdigit(*p)) {
			/**
			 * "n" format.
			 * This specifies the Julian day, with n between 0 and 365.
			 * February 29 is counted in leap years.
			 **/

			ret = tze_rule_check_int(&p, TZE_MIN_DAY, TZE_MAX_DAY, &n);
		} else {
			/**
			 * Syntax error.
			 **/
		}
	}

	if (ret < 0) {
		*rule = p;
		return -1;
	}

	if (*p == '/') {
		p++;
		ret = tze_rule_check_offset(&p, v3);

		if (ret < 0) {
			*rule = p;
			return -1;
		}
	}

	*rule = p;
	return 0;
}

int tze_rule_check(const char		*const rule,
				   const char		*const locality,
				   const bool		 v3,
				   struct tze_err_t *err)
{
	const char *p = rule;

	if (*p == '\0') {
		return 0;
	}

	if (tze_rule_check_name(&p) < 0) {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has a wrong STD timezone name",
					locality, rule);
		return -1;
	}

	if (tze_rule_check_offset(&p, v3) < 0) {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has a wrong STD time offset",
					locality, rule);
		return -1;
	}

	if (*p == '\0') {
		return 0;
	}

	if (tze_rule_check_name(&p) < 0) {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has a wrong DST timezone name",
					locality, rule);
		return -1;
	}

	if (*p == '+' || *p == '-' || isdigit(*p)) {
		if (tze_rule_check_offset(&p, v3) < 0) {
			tze_err_set(err, 0,
						"%s: \"%s\" rule has a wrong DST time offset",
						locality, rule);
			return -1;
		}
	}

	if (tze_rule_check_date(&p, v3) < 0) {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has a wrong DST time transition date",
					locality, rule);
		return -1;
	}

	if (tze_rule_check_date(&p, v3) < 0) {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has a wrong STD time transition date",
					locality, rule);
		return -1;
	}

	if (*p != '\0') {
		tze_err_set(err, 0,
					"%s: \"%s\" rule has unexpected trailing characters",
					locality, rule);
		return -1;
	}

	return 0;
}
