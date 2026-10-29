// Benchmark for CupidScript (CPU-bound, single-threaded).
// Includes newer language features and QoL helpers for profiling.
// Run: bin/cupidscript examples/benchmark.cs

fn bench_arith(n) {
  let i = 0;
  let x = 1;
  let y = 2;
  let acc = 0;
  while (i < n) {
    x = x * 1664525 + 1013904223;
    y = y * 1103515245 + 12345;
    acc = acc + (x % 97) + (y % 89);
    i = i + 1;
  }
  return acc;
}

fn bench_calls(n) {
  fn f(a, b, c) { return (a + b) * c + (a % 7); }
  let i = 0;
  let acc = 0;
  while (i < n) {
    acc = acc + f(i, i + 1, 3);
    i = i + 1;
  }
  return acc;
}

fn bench_for_range(n) {
  let acc = 0.0;
  for i in range(n) {
    acc = acc + sqrt(i + 1);
  }
  for i in range(0, n, 3) {
    acc = acc + (i % 7);
  }
  return acc;
}

fn bench_lists(n) {
  let xs = list();
  let i = 0;
  while (i < n) {
    push(xs, i);
    i = i + 1;
  }
  // mutate + sum
  i = 0;
  let sum = 0;
  while (i < n) {
    xs[i] = xs[i] * 2;
    sum = sum + xs[i];
    i = i + 1;
  }
  return sum;
}

fn bench_maps(n) {
  let m = map();
  let i = 0;
  while (i < n) {
    m[fmt("k%d", i)] = i;
    i = i + 1;
  }
  let ks = keys(m);
  i = 0;
  let sum = 0;
  while (i < len(ks)) {
    sum = sum + m[ks[i]];
    i = i + 1;
  }
  return sum;
}

fn bench_iter_helpers(n) {
  let xs = [];
  for i in range(n) {
    push(xs, i);
  }
  fn is_even(x) { return x % 2 == 0; }
  fn sum(a, b) { return a + b; }
  let evens = filter(xs, is_even);
  let squares = map(evens, fn(x) => x * x);
  let total = reduce(squares, sum, 0);
  return total;
}

fn bench_match_patterns(n) {
  let acc = 0;
  let i = 0;
  while (i < n) {
    let v = [i % 5, i];
    let r = match (v) {
      case [k, x] if k == 0: x;
      case [k, x] if k == 1: x + 1;
      case [k, x] if k == 2 && x % 2 == 0: x + 2;
      default: 0;
    };
    acc = acc + r;
    i = i + 1;
  }
  return acc;
}

fn bench_pipe(n) {
  fn add(a, b) => a + b;
  fn mul(a, b) => a * b;
  fn inc(x) => x + 1;
  let acc = 0;
  let i = 0;
  while (i < n) {
    acc = acc + (i |> add(2, _) |> mul(3, _) |> inc());
    i = i + 1;
  }
  return acc;
}

fn bench_strings_append(n) {
  let i = 0;
  let s = "";
  while (i < n) {
    // common pattern in plugins: incremental append
    s = s + "x";
    i = i + 1;
  }
  return len(s);
}

fn bench_strings_replace(n) {
  let i = 0;
  let s = "";
  while (i < n) {
    // worst-case: forces full-copy style string processing
    if (len(s) > 256) { s = str_replace(s, "x", "x"); }
    s = s + "x";
    i = i + 1;
  }
  return len(s);
}

fn bench_strings_qol(n) {
  let i = 0;
  let acc = 0;
  let base = "  Hello CupidScript  ";
  while (i < n) {
    let s = trim(base);
    if (starts_with(s, "Hello") && ends_with(s, "Script") && contains(s, "Cupid")) {
      acc = acc + len(s);
    }
    let low = lower(s);
    let up = upper(s);
    let t = "${i}:${low}:${up}";
    acc = acc + len(t);
    i = i + 1;
  }
  return acc;
}

fn bench_optional_nullish(n) {
  let user = {profile: {score: 42}};
  let missing = nil;
  let i = 0;
  let acc = 0;
  while (i < n) {
    acc = acc + (user?.profile?.score ?? 0);
    acc = acc + (missing?.profile?.score ?? 1);
    i = i + 1;
  }
  return acc;
}

fn bench_destructure_spread(n) {
  let acc = 0;
  let i = 0;
  while (i < n) {
    let pair = [i, i + 1, i + 2];
    let [a, b, ...rest] = pair;
    acc = acc + a + b + len(rest);

    let base = {a: a, b: b};
    let merged = {...base, c: i};
    let {a: a2, c} = merged;
    acc = acc + a2 + c;

    i = i + 1;
  }
  return acc;
}

fn bench_strbuf(n) {
  let b = strbuf();
  let i = 0;
  while (i < n) {
    b.append(i);
    b.append(",");
    i = i + 1;
  }
  let s = b.str();
  return len(s);
}

class Counter {
  fn new(start) {
    self.value = start;
  }

  fn inc() {
    self.value = self.value + 1;
    return self.value;
  }
}

struct Pair { a, b = 0 }

enum Mode { Idle, Busy = 5, Done }

fn bench_classes_structs_enums(n) {
  let acc = 0;
  let i = 0;
  let c = Counter(0);
  while (i < n) {
    let p = Pair(i, i + 1);
    acc = acc + p.a + p.b;
    acc = acc + c.inc();
    let m = Mode.Idle;
    if (i % 3 == 1) { m = Mode.Busy; }
    if (i % 3 == 2) { m = Mode.Done; }
    if (m == Mode.Busy) { acc = acc + 2; }
    i = i + 1;
  }
  return acc;
}

fn bench_switch(n) {
  let acc = 0;
  let i = 0;
  while (i < n) {
    switch (i % 5) {
      case 0 { acc = acc + 1; }
      case 1 { acc = acc + 2; }
      case 2 { acc = acc + 3; }
      default { acc = acc + 4; }
    }
    i = i + 1;
  }
  return acc;
}

fn bench_try_catch_finally(n) {
  let acc = 0;
  let i = 0;
  while (i < n) {
    try {
      if (i % 7 == 0) { throw i; }
      acc = acc + 1;
    } catch (e) {
      acc = acc + e;
    } finally {
      acc = acc + 1;
    }
    i = i + 1;
  }
  return acc;
}

fn bench_defer(n) {
  let box = [0];
  fn work(b) {
    defer { b[0] = b[0] + 1; }
    return b[0];
  }
  let i = 0;
  while (i < n) {
    work(box);
    i = i + 1;
  }
  return box[0];
}

fn gen_squares(n) {
  let i = 0;
  while (i < n) {
    yield i * i;
    i = i + 1;
  }
}

fn bench_generators(n) {
  let xs = gen_squares(n);
  let i = 0;
  let acc = 0;
  while (i < len(xs)) {
    acc = acc + xs[i];
    i = i + 1;
  }
  return acc;
}

async fn async_inc(x) { return x + 1; }

async fn bench_async_await(n) {
  let i = 0;
  let acc = 0;
  while (i < n) {
    acc = acc + await async_inc(i);
    i = i + 1;
  }
  return acc;
}

fn bench_json(n) {
  let acc = 0;
  let i = 0;
  while (i < n) {
    let obj = {"id": i, "name": "user", "tags": ["a", "b", "c"], "ok": i % 2 == 0};
    let s = json_stringify(obj);
    let back = json_parse(s);
    if (back.ok) { acc = acc + back.id; }
    i = i + 1;
  }
  return acc;
}

let N = 200000;
let Nmed = 50000;
let Nsmall = 5000;
let Ntiny = 1000;

print("=== Cupidscript benchmark ===");
print("N =", N, "Nmed =", Nmed, "Nsmall =", Nsmall);

let t0 = now_ms();
let a = bench_arith(N);
let t1 = now_ms();

let b = bench_calls(N);
let t2 = now_ms();

let c = bench_for_range(N);
let t3 = now_ms();

let d = bench_lists(Nsmall);
let t4 = now_ms();

let e = bench_maps(Nsmall);
let t5 = now_ms();

let f = bench_iter_helpers(Nmed);
let t6 = now_ms();

let g = bench_match_patterns(Nmed);
let t7 = now_ms();

let h = bench_pipe(N);
let t8 = now_ms();

let i = bench_strings_append(200000);
let t9 = now_ms();

let j = bench_strings_replace(20000);
let t10 = now_ms();

let k = bench_strings_qol(Nmed);
let t11 = now_ms();

let l = bench_strbuf(200000);
let t12 = now_ms();

let m = bench_optional_nullish(N);
let t13 = now_ms();

let n = bench_destructure_spread(Nmed);
let t14 = now_ms();

let o = bench_switch(N);
let t15 = now_ms();

let p = bench_try_catch_finally(Nsmall);
let t16 = now_ms();

let q = bench_classes_structs_enums(Nmed);
let t17 = now_ms();

let r = bench_defer(Nsmall);
let t18 = now_ms();

let s = bench_generators(Nsmall);
let t19 = now_ms();

let u = await bench_async_await(Ntiny);
let t20 = now_ms();

let v = bench_json(Ntiny);
let t21 = now_ms();

print("arith  ms =", t1 - t0, "result =", a);
print("calls  ms =", t2 - t1, "result =", b);
print("range  ms =", t3 - t2, "result =", c);
print("lists  ms =", t4 - t3, "result =", d);
print("maps   ms =", t5 - t4, "result =", e);
print("iter   ms =", t6 - t5, "result =", f);
print("match  ms =", t7 - t6, "result =", g);
print("pipe   ms =", t8 - t7, "result =", h);
print("str+   ms =", t9 - t8, "result =", i);
print("str~   ms =", t10 - t9, "result =", j);
print("strq   ms =", t11 - t10, "result =", k);
print("strb   ms =", t12 - t11, "result =", l);
print("opt??  ms =", t13 - t12, "result =", m);
print("dstr   ms =", t14 - t13, "result =", n);
print("switch ms =", t15 - t14, "result =", o);
print("tryf   ms =", t16 - t15, "result =", p);
print("types  ms =", t17 - t16, "result =", q);
print("defer  ms =", t18 - t17, "result =", r);
print("gen    ms =", t19 - t18, "result =", s);
print("async  ms =", t20 - t19, "result =", u);
print("json   ms =", t21 - t20, "result =", v);
print("total  ms =", t21 - t0);
