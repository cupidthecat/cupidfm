// Promise demo

let p = promise();
resolve(p, "ok");
print(await p);

async fn delayed_value(v) {
  await sleep(10);
  return v;
}

print(await delayed_value(123));
