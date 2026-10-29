// import/export syntax

import mod from "./modules/_import_export.cs";
assert(mod.foo == 1, "default import map foo");
assert(mod.bar == 2, "default import map bar");

import {foo, bar as b, renamed} from "./modules/_import_export.cs";
assert(foo == 1, "named import foo");
assert(b == 2, "named import alias");
assert(renamed == 3, "named import renamed");

import "./modules/_import_export.cs";

print("import_export ok");
