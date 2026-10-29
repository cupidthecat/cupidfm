// export statement in non-module script

export answer = 7;
assert(exports.answer == 7, "exports in direct script");

print("exports_direct ok");
