#ifndef TZE_ATTR_H
#define TZE_ATTR_H

#if defined(__GNUC__)    || \
	defined(__clang__)   || \
	defined(__MINGW32__) || \
	defined(__MINGW64__)

#define TZE_ATTR_PRINTF(m, n)			__attribute__((format(printf, m, n)))
#define TZE_ATTR_PACKED					__attribute__((packed))

#else
#error "An unknown compiler used."
#endif

#endif /* TZE_ATTR_H */
