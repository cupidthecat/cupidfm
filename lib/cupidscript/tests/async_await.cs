async fn add(a, b) { return a + b; }

let r = await add(2, 3);
assert(r == 5, "await async fn");

let twice = async fn(x) { return x * 2; };
assert(await twice(4) == 8, "await async literal");

async fn delayed_sum(a, b) {
	await sleep(1);
	return a + b;
}

assert(await delayed_sum(3, 4) == 7, "await sleep in async fn");

let p = promise();
resolve(p, 42);
assert(await p == 42, "await resolved promise");

print("async await ok");
