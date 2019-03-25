#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "tze_tz.h"
#include "tze_err.h"
#include "tze_attr.h"

#define TZE_TZ_CHR_SPACE				0x20
#define TZE_TZ_CHR_LAST					0x7f

#define	TZE_TZ_MAGIC					"TZif"
#define TZE_TZ_VERSION_2				'2'
#define TZE_TZ_VERSION_3				'3'

#define TZE_TZ_RESERVED					(15)
#define TZE_TZ_MAX_OFFSET				(1 << 21)
#define TZE_TZ_DEF_OFFSET				(0)
#define TZE_TZ_TIMECNT_MAX				(0x400)
#define TZE_TZ_TYPECNT_MAX				(0x0ff)

struct tze_tz_header_t {
	uint8_t	 tzh_magic[sizeof(TZE_TZ_MAGIC) - 1];
	uint8_t  tzh_version;
	uint8_t  tzh_zero[TZE_TZ_RESERVED];	/* reserved for future use		 */
	uint32_t tzh_ttisgmtcnt;			/* UTC indicators count			 */
	uint32_t tzh_ttisstdcnt;			/* wall-clock time count		 */
	uint32_t tzh_leapcnt;				/* leap second count			 */
	uint32_t tzh_timecnt;				/* time transition moments count */
	uint32_t tzh_typecnt;				/* local time type count		 */
										/* (should be nonzero)			 */
	uint32_t tzh_charcnt;				/* time zone abbreviation		 */
										/* string count					 */
} TZE_ATTR_PACKED;

struct tze_tz_ttinfo_t {
	int32_t  tt_gmtoff;					/* GMT second offset			 */
	uint8_t  tt_isdst;					/* DST is active				 */
	uint8_t  tt_abbrind;				/* timezone abbreviation index	 */
} TZE_ATTR_PACKED;

static inline void
tze_tz_close_fd(int fd)
{
	while (close(fd) < 0) {
		if (errno == EINTR || errno == EAGAIN) {
			continue;
		}
	}
}

static inline int
tze_tz_read_all(int				  fd,
				const off_t		  file_size,
				const char		 *const locality,
				const char		 *const data_description,
				void			 *data,
				const size_t	  data_size,
				struct tze_err_t *err)
{
	const off_t pos = lseek(fd, 0, SEEK_CUR);

	if (pos < 0) {
		tze_err_set(err, errno, "%s: unable to get a file position",
					locality);
		return -1;
	}

	if (pos + (off_t) data_size > file_size) {
		tze_err_set(err, 0,
					"%s: trying to read beyond of a file end (%zi/%zi)",
					locality, (ssize_t) ((size_t) pos + data_size),
					(ssize_t) file_size);
		return -1;
	}

	uint8_t *p = data;
	size_t remain = data_size;

	while (remain > 0) {
		const ssize_t n = read(fd, p, remain);

		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}

			tze_err_set(err, errno, "%s: unable to read %s",
						locality, data_description);
			return -1;
		}

		p += n;
		remain -= (size_t) n;
	}

	return 0;
}

static inline int
tze_tz_read_all_at(int				 fd,
				   const off_t		 file_size,
				   const char		*const locality,
				   const char		*const data_description,
				   const off_t		 offs,
				   void				*data,
				   const size_t		 data_size,
				   struct tze_err_t *err)
{
	if (lseek(fd, offs, SEEK_SET) < 0) {
		tze_err_set(err, errno, "%s: unable to seek to %zi/%zi",
					locality, (ssize_t) offs, (ssize_t) file_size);
		return -1;
	}

	return tze_tz_read_all(fd, file_size, locality,
						   data_description, data,
						   data_size, err);
}

static inline int
tze_tz_read_header_at(int					  fd,
					  const off_t			  file_size,
					  const char			 *const locality,
					  const off_t			  offs,
					  struct tze_tz_header_t *hdr,
					  struct tze_err_t		 *err)
{
	const char *const htype = (offs == 0) ?
		"a primary header" : "a secondary header";

	if (tze_tz_read_all_at(fd, file_size, locality, htype,
						   offs, hdr, sizeof(*hdr), err) < 0) {
		return -1;
	}

	if (memcmp(hdr->tzh_magic, TZE_TZ_MAGIC,
			   sizeof(TZE_TZ_MAGIC) - 1) != 0) {
		/* not a timezone file */
		return 1;
	}

	if (hdr->tzh_version != TZE_TZ_VERSION_2 &&
		hdr->tzh_version != TZE_TZ_VERSION_3) {
		tze_err_set(err, 0,
					"%s: unsupported format version \"%c\" (0x%02hhx)",
					locality,
					(hdr->tzh_version < TZE_TZ_CHR_SPACE) ?
					TZE_TZ_CHR_SPACE : hdr->tzh_version,
					hdr->tzh_version);
		return -1;
	}

	hdr->tzh_ttisgmtcnt	= ntohl(hdr->tzh_ttisgmtcnt);
	hdr->tzh_ttisstdcnt	= ntohl(hdr->tzh_ttisstdcnt);
	hdr->tzh_leapcnt	= ntohl(hdr->tzh_leapcnt);
	hdr->tzh_timecnt	= ntohl(hdr->tzh_timecnt);
	hdr->tzh_typecnt	= ntohl(hdr->tzh_typecnt);
	hdr->tzh_charcnt	= ntohl(hdr->tzh_charcnt);

	if (hdr->tzh_ttisgmtcnt > hdr->tzh_typecnt) {
		tze_err_set(err, 0,
					"%s: %s corrupted: wrong UTC indicators count "
					"(%" PRIu32 " > %" PRIu32 ")",
					locality, htype, hdr->tzh_ttisgmtcnt, hdr->tzh_typecnt);
		return -1;
	}

	if (hdr->tzh_ttisstdcnt > hdr->tzh_typecnt) {
		tze_err_set(err, 0,
					"%s: %s corrupted: wrong wall-clock time count "
					"(%" PRIu32 " > %" PRIu32 ")",
					locality, htype, hdr->tzh_ttisstdcnt, hdr->tzh_typecnt);
		return -1;
	}

	if (hdr->tzh_typecnt == 0 ||
		hdr->tzh_typecnt > TZE_TZ_TYPECNT_MAX) {
		tze_err_set(err, 0,
					"%s: %s corrupted: wrong local time type count "
					"(%" PRIu32 ")", locality, htype, hdr->tzh_typecnt);
		return -1;
	}

	if (hdr->tzh_timecnt > TZE_TZ_TIMECNT_MAX) {
		tze_err_set(err, 0,
					"%s: %s corrupted: wrong time transition moments count "
					"(%" PRIu32 " > %i)",
					locality, htype, hdr->tzh_timecnt, TZE_TZ_TIMECNT_MAX);
		return -1;
	}

	return 0;
}

int tze_tz_read(const char		  *const file_name,
				const char		  *const locality,
				char			 **rule,
				bool			  *v3,
				struct tze_err_t  *err)
{
	*rule = NULL;
	*v3 = false;

	int fd = open(file_name, O_RDONLY);

	if (fd < 0) {
		tze_err_set(err, errno, "%s: unable to open", locality);
		return -1;
	}

	const off_t file_size = lseek(fd, 0, SEEK_END);

	if (file_size < 0) {
		tze_err_set(err, errno, "%s: unable to get a file size", locality);
		goto close_fd;
	}

	struct tze_tz_header_t hdr;

	if (file_size <= sizeof(hdr)) {
		/* wrong format */
		return 1;
	}

	int ret = tze_tz_read_header_at(fd, file_size, locality, 0, &hdr, err);

	if (ret != 0) {
		if (ret > 0) {
			tze_tz_close_fd(fd);
			return ret;
		}

		goto close_fd;
	}

	const off_t tzh_offs = (off_t)
		(sizeof(hdr) +
		hdr.tzh_timecnt * (sizeof(uint32_t) + 1) +
		hdr.tzh_typecnt * sizeof(struct tze_tz_ttinfo_t) +
		hdr.tzh_charcnt +
		hdr.tzh_leapcnt * (2 * sizeof(uint32_t)) +
		hdr.tzh_ttisgmtcnt +
		hdr.tzh_ttisstdcnt);

	ret = tze_tz_read_header_at(fd, file_size, locality,
								tzh_offs, &hdr, err);

	if (ret != 0) {
		goto close_fd;
	}

	const off_t indexes_offs = tzh_offs +
		(off_t) (sizeof(hdr) + hdr.tzh_timecnt * sizeof(int64_t));
	uint8_t indexes[TZE_TZ_TIMECNT_MAX];
	const size_t indexes_count = (hdr.tzh_timecnt > 0) ? hdr.tzh_timecnt : 1;

	if (hdr.tzh_timecnt == 0) {
		indexes[0] = 0;
	} else {
		if (tze_tz_read_all_at(fd, file_size, locality,
							   "transition time moments",
							   indexes_offs, indexes,
							   sizeof(indexes[0]) * indexes_count,
							   err) < 0) {
			goto close_fd;
		}

		for (size_t i = 0; i < indexes_count; i++) {
			if (indexes[i] < hdr.tzh_typecnt) {
				continue;
			}

			tze_err_set(err, 0, "%s: wrong transition type index "
						"(%" PRIu8 " >= %" PRIu32 ")",
						locality, indexes[i], hdr.tzh_typecnt);
			goto close_fd;
		}
	}

	struct tze_tz_ttinfo_t ttinfo[TZE_TZ_TYPECNT_MAX];
	const size_t ttinfo_count = (hdr.tzh_typecnt > 0) ? hdr.tzh_typecnt : 1;

	if (hdr.tzh_typecnt == 0) {
		ttinfo[0].tt_isdst = 0;
		ttinfo[0].tt_gmtoff = 0;
		ttinfo[0].tt_abbrind = 0; /* should be ignored */
	} else {
		if (tze_tz_read_all(fd, file_size, locality,
							"transition time moments", ttinfo,
							sizeof(ttinfo[0]) * ttinfo_count, err) < 0) {
			goto close_fd;
		}

		for (size_t i = 0; i < ttinfo_count; i++) {
			struct tze_tz_ttinfo_t *info = &ttinfo[i];

			info->tt_gmtoff = (int32_t) ntohl((uint32_t) info->tt_gmtoff);

			if (info->tt_gmtoff <= -TZE_TZ_MAX_OFFSET / 2 ||
				info->tt_gmtoff >= +TZE_TZ_MAX_OFFSET / 2) {
				tze_err_set(err, 0,
							"%s: time offset %" PRIi32 " is out of range "
							"(%i, %i)",
							locality, info->tt_gmtoff,
							+TZE_TZ_MAX_OFFSET / 2,
							-TZE_TZ_MAX_OFFSET / 2);
				goto close_fd;
			}
		}
	}

	const off_t tzh2_size = (off_t)
		(sizeof(hdr) +
		 hdr.tzh_timecnt * (sizeof(int64_t) + 1) +
		 hdr.tzh_typecnt * sizeof(struct tze_tz_ttinfo_t) +
		 hdr.tzh_charcnt +
		 hdr.tzh_leapcnt * (sizeof(int64_t) + sizeof(uint32_t)) +
		 hdr.tzh_ttisgmtcnt +
		 hdr.tzh_ttisstdcnt);
	const off_t rule_offs = tzh_offs + tzh2_size + 1;

	if (rule_offs >= file_size) {
		tze_err_set(err, 0, "%s: invalid rule offset: %zi",
					locality, (ssize_t) rule_offs);
		goto close_fd;
	}

	if (lseek(fd, rule_offs, SEEK_SET) < 0) {
		tze_err_set(err, errno, "%s: unable to seek to read a rule",
					locality);
		goto close_fd;
	}

	size_t rule_size = (size_t) (file_size - rule_offs);
	char *rule_value = malloc(rule_size);

	if (rule_value == NULL) {
		tze_err_set(err, errno,
					"%s: unable to allocate a rule buffer", locality);
		goto close_fd;
	}

	if (tze_tz_read_all_at(fd, file_size, locality, "a transition rule",
						   rule_offs, rule_value, rule_size, err) < 0) {
		goto free_rule;
	}

	rule_size--;

	if (rule_value[rule_size] != '\n') {
		tze_err_set(err, 0, "%s: wrong rule trailer (0x%02" PRIx8 ")",
					locality, rule_value[rule_size]);
		goto free_rule;
	}

	rule_value[rule_size] = '\0';

	for (size_t i = 0; i < rule_size; i++) {
		const uint8_t c = (uint8_t) rule_value[i];

		if (c > TZE_TZ_CHR_SPACE && c <= TZE_TZ_CHR_LAST) {
			continue;
		}

		tze_err_set(err, 0, "%s: a rule has non-ASCII characters", locality);
		goto free_rule;
	}

	*rule = rule_value;
	*v3 = (hdr.tzh_version == TZE_TZ_VERSION_3);

	tze_tz_close_fd(fd);
	return 0;

free_rule:
	free(rule_value);

close_fd:
	tze_tz_close_fd(fd);
	return -1;
}
