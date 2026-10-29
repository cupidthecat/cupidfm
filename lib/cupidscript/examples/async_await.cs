async fn fetch_value(x) { return x * 3; }

let result = await fetch_value(7);
print(result);

let inc = async fn(n) { return n + 1; };
print(await inc(41));

async fn delayed(msg) {
	await sleep(5);
	return "done: " + msg;
}

print(await delayed("work"));
