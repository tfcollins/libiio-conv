/* SPDX-License-Identifier: MIT */
/* The enum name tables that matlab/tools/gen_enums.py generates from the pinned iio.h, and
 * the iio.h constants that M code repeats as numbers. */
#include "mx_util.h"
#undef printf
#include "test_util.h"

#include <limits.h>

static bool name_is(enum gw_enum which, long long value, const char *want)
{
	const char *got = gw_enum_name(which, value);

	return got && !strcmp(got, want);
}

int main(void)
{
	const struct gw_enum_entry *t;
	size_t n, total = 0;

	CHECK(name_is(GW_ENUM_CHAN_TYPE, IIO_VOLTAGE, "IIO_VOLTAGE"));
	CHECK(name_is(GW_ENUM_MODIFIER, IIO_MOD_Q, "IIO_MOD_Q"));
	CHECK(name_is(GW_ENUM_EVENT_TYPE, IIO_EV_TYPE_THRESH, "IIO_EV_TYPE_THRESH"));
	CHECK(name_is(GW_ENUM_EVENT_DIRECTION, IIO_EV_DIR_FALLING, "IIO_EV_DIR_FALLING"));
	CHECK(name_is(GW_ENUM_HWMON_CHAN_TYPE, HWMON_TEMP, "HWMON_TEMP"));
	for (int e = 0; e < GW_ENUM_COUNT; e++)
		CHECK(gw_enum_name((enum gw_enum)e, 123456) == NULL);
	CHECK(gw_enum_name(GW_ENUM_COUNT, 0) == NULL);

	n = 99;
	t = gw_enum_table(GW_ENUM_EVENT_DIRECTION, &n);
	CHECK(n == 7);
	CHECK(t && !strcmp(t[0].name, "IIO_EV_DIR_EITHER") && t[0].value == IIO_EV_DIR_EITHER);
	n = 99;
	t = gw_enum_table(GW_ENUM_HWMON_CHAN_TYPE, &n);
	CHECK(t && n == 10);
	for (int e = 0; e < GW_ENUM_COUNT; e++) {
		n = 0;
		t = gw_enum_table((enum gw_enum)e, &n);
		CHECK(t && n > 0);
		total += n;
		/* every entry is found again under its own value (the first name of a value wins) */
		for (size_t i = 0; t && i < n; i++)
			CHECK(gw_enum_name((enum gw_enum)e, t[i].value) != NULL);
	}
	CHECK(total == 129);
	n = 99;
	CHECK(gw_enum_table(GW_ENUM_COUNT, &n) == NULL && n == 0);

	/* The "unknown" enumerators are INT_MAX: the value column must hold them. */
	CHECK(name_is(GW_ENUM_CHAN_TYPE, INT_MAX, "IIO_CHAN_TYPE_UNKNOWN"));
	CHECK(name_is(GW_ENUM_HWMON_CHAN_TYPE, INT_MAX, "HWMON_CHAN_TYPE_UNKNOWN"));

	/* Constants that M code repeats (iio.internal.params, iio.Context). */
	CHECK(LEVEL_NOLOG == 1);
	CHECK(LEVEL_ERROR == 2);
	CHECK(LEVEL_WARNING == 3);
	CHECK(LEVEL_INFO == 4);
	CHECK(LEVEL_DEBUG == 5);
	CHECK(IIO_TIMEOUT_BACKEND == 0);
	CHECK(IIO_TIMEOUT_INFINITE == -1);
	CHECK(IIO_TIMEOUT_NONBLOCK == INT_MIN);

	TEST_MAIN_RETURN();
}
