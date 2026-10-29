// Duff’s Device in CupidScript (switch with fallthrough)

fn copy_duff(dst, src, n) {
  let i = 0;
  let count = (n + 7) / 8;

  switch (n % 8) {
    case 0 { dst[i] = src[i]; i += 1; }
    case 7 { dst[i] = src[i]; i += 1; }
    case 6 { dst[i] = src[i]; i += 1; }
    case 5 { dst[i] = src[i]; i += 1; }
    case 4 { dst[i] = src[i]; i += 1; }
    case 3 { dst[i] = src[i]; i += 1; }
    case 2 { dst[i] = src[i]; i += 1; }
    case 1 { dst[i] = src[i]; i += 1; }
  }

  count -= 1;
  while (count > 0) {
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    dst[i] = src[i]; i += 1;
    count -= 1;
  }
}

let src = [1,2,3,4,5,6,7,8,9,10,11];
let dst = [];
extend(dst, [nil,nil,nil,nil,nil,nil,nil,nil,nil,nil,nil]);

copy_duff(dst, src, len(src));

print("src:", src);
print("dst:", dst);
