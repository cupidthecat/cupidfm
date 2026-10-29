// Date/time stdlib tests

let ms = unix_ms();
let s = unix_s();
assert(ms > 1600000000000, "unix_ms reasonable");
assert(s > 1600000000, "unix_s reasonable");
assert(abs(ms - s * 1000) < 5000, "unix_ms vs unix_s");

let now = datetime_now();
assert(now.year >= 2020, "datetime_now year");
assert(now.month >= 1 && now.month <= 12, "datetime_now month");
assert(now.day >= 1 && now.day <= 31, "datetime_now day");
assert(now.is_utc == false, "datetime_now is_utc");

let utc = datetime_utc();
assert(utc.is_utc == true, "datetime_utc is_utc");
assert(utc.month >= 1 && utc.month <= 12, "datetime_utc month");

let epoch = datetime_from_unix_ms_utc(0);
assert(epoch.year == 1970, "epoch year");
assert(epoch.month == 1, "epoch month");
assert(epoch.day == 1, "epoch day");
assert(epoch.hour == 0 && epoch.minute == 0 && epoch.second == 0, "epoch time");

let t = datetime_from_unix_ms_utc(1234);
assert(t.second == 1, "ms second");
assert(t.ms == 234, "ms remainder");

print("date_time ok");
