// Date/time examples

let ms = unix_ms();
let s = unix_s();
print("unix_ms:", ms);
print("unix_s:", s);

let local = datetime_now();
print("local:", local);

let utc = datetime_utc();
print("utc:", utc);

let from_ms = datetime_from_unix_ms_utc(1700000000123);
print("from_ms_utc:", from_ms);
