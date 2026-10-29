// finally expression example

fn work() {
  try {
    throw error("fail", "E_FAIL");
  } catch (e) {
    if (is_error(e)) {
      print("caught", e.msg);
    }
  } finally print("cleanup");
}

work();
