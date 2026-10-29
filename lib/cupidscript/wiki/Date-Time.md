# Date & Time

## Table of Contents

- [Unix Time](#unix-time)
- [Date/Time Maps](#datetime-maps)
- [Example](#example)

CupidScript provides simple date/time helpers in the standard library.

## Unix Time

### `unix_ms() -> int`

Returns current Unix time in **milliseconds**.

### `unix_s() -> int`

Returns current Unix time in **seconds**.

## Date/Time Maps

Date/time functions return a map with these fields:

* `year` - full year (e.g., 2026)
* `month` - 1-12
* `day` - 1-31
* `hour` - 0-23
* `minute` - 0-59
* `second` - 0-60
* `ms` - 0-999
* `wday` - day of week (0=Sunday)
* `yday` - day of year (1-366)
* `is_dst` - daylight saving time flag
* `is_utc` - whether the timestamp is UTC

### `datetime_now() -> map`

Local time.

### `datetime_utc() -> map`

UTC time.

### `datetime_from_unix_ms(ms: int|float) -> map`

Converts Unix milliseconds to local time.

### `datetime_from_unix_ms_utc(ms: int|float) -> map`

Converts Unix milliseconds to UTC.

## Example

```c
let ms = unix_ms();
let local = datetime_now();
let utc = datetime_utc();

print("ms:", ms);
print("local:", local);
print("utc:", utc);

let epoch = datetime_from_unix_ms_utc(0);
print("epoch:", epoch.year, epoch.month, epoch.day, epoch.hour, epoch.minute, epoch.second);
```
