exports.hello = fn(name) {
  return "hello " + name;
};

exports.make_sample_data = fn() {
  return {
    "name": "cupid",
    "age": 5,
    "tags": ["plugin", "demo"]
  };
};
