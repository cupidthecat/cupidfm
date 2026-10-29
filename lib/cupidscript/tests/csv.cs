// CSV parsing and stringify tests

// Test 1: Basic parsing
let basic = "a,b,c\n1,2,3";
let rows = csv_parse(basic);
assert(len(rows) == 2, "basic: should have 2 rows");
assert(len(rows[0]) == 3, "basic: first row should have 3 fields");
assert(rows[0][0] == "a", "basic: rows[0][0] == 'a'");
assert(rows[1][2] == "3", "basic: rows[1][2] == '3'");

// Test 2: Quoted fields with embedded commas
let quoted = "a,\"b,c\",d\n1,\"2,3\",4";
let q_rows = csv_parse(quoted);
assert(q_rows[0][1] == "b,c", "quoted: embedded comma");
assert(q_rows[1][1] == "2,3", "quoted: embedded comma in data");

// Test 3: Quote escaping
let escaped = "a,\"b\"\"c\",d\n1,\"2\"\"3\",4";
let e_rows = csv_parse(escaped);
assert(e_rows[0][1] == "b\"c", "escaped: double quote becomes single");
assert(e_rows[1][1] == "2\"3", "escaped: data with escaped quote");

// Test 4: Multi-line fields
let multiline = "a,\"b\nc\",d\n1,\"2\n3\",4";
let m_rows = csv_parse(multiline);
assert(m_rows[0][1] == "b\nc", "multiline: newline in quoted field");
assert(m_rows[1][1] == "2\n3", "multiline: data with newline");

// Test 5: Custom delimiter (tab)
let tsv = "a\tb\tc\n1\t2\t3";
let t_rows = csv_parse(tsv, {delimiter: "\t"});
assert(len(t_rows) == 2, "tsv: should have 2 rows");
assert(t_rows[0][0] == "a", "tsv: first field");
assert(t_rows[1][2] == "3", "tsv: last field");

// Test 6: Custom delimiter (semicolon)
let ssv = "a;b;c\n1;2;3";
let s_rows = csv_parse(ssv, {delimiter: ";"});
assert(s_rows[0][1] == "b", "ssv: semicolon delimiter");

// Test 7: Header mode
let with_headers = "name,age,city\nfrank,30,NYC\nalice,25,LA";
let h_rows = csv_parse(with_headers, {headers: true});
assert(len(h_rows) == 2, "headers: should have 2 data rows");
assert(h_rows[0].name == "frank", "headers: first row name");
assert(h_rows[0].age == "30", "headers: first row age");
assert(h_rows[1].city == "LA", "headers: second row city");

// Test 8: Empty rows with skip_empty
let with_empty = "a,b,c\n\n1,2,3\n\n4,5,6";
let ne_rows = csv_parse(with_empty, {skip_empty: false});
let se_rows = csv_parse(with_empty, {skip_empty: true});
assert(len(ne_rows) == 5, "no skip_empty: 5 rows total");
assert(len(se_rows) == 3, "skip_empty: 3 non-empty rows");

// Test 9: Whitespace trimming
let with_spaces = " a , b , c \n 1 , 2 , 3 ";
let nt_rows = csv_parse(with_spaces, {trim: false});
let tr_rows = csv_parse(with_spaces, {trim: true});
assert(nt_rows[0][0] == " a ", "no trim: spaces preserved");
assert(tr_rows[0][0] == "a", "trim: spaces removed");
assert(tr_rows[1][2] == "3", "trim: trailing spaces removed");

// Test 10: Empty file
let empty = "";
let empty_rows = csv_parse(empty);
assert(len(empty_rows) == 0, "empty file: no rows");

// Test 11: Single row
let single = "a,b,c";
let single_rows = csv_parse(single);
assert(len(single_rows) == 1, "single row: 1 row");
assert(len(single_rows[0]) == 3, "single row: 3 fields");

// Test 12: Single column
let single_col = "a\nb\nc";
let sc_rows = csv_parse(single_col);
assert(len(sc_rows) == 3, "single column: 3 rows");
assert(len(sc_rows[0]) == 1, "single column: 1 field per row");

// Test 13: Windows line endings
let windows = "a,b,c\r\n1,2,3\r\n";
let w_rows = csv_parse(windows);
assert(len(w_rows) == 2, "windows: 2 rows");
assert(w_rows[0][0] == "a", "windows: parsing works");

// Test 14: Mac line endings
let mac = "a,b,c\r1,2,3\r";
let mac_rows = csv_parse(mac);
assert(len(mac_rows) == 2, "mac: 2 rows");

// Test 15: Mixed line endings
let mixed = "a,b,c\n1,2,3\r\n4,5,6\r";
let mixed_rows = csv_parse(mixed);
assert(len(mixed_rows) == 3, "mixed: 3 rows");

// Test 16: Basic stringify
let data = [["a", "b", "c"], ["1", "2", "3"]];
let csv_out = csv_stringify(data);
assert(contains(csv_out, "a,b,c"), "stringify: header row");
assert(contains(csv_out, "1,2,3"), "stringify: data row");

// Test 17: Stringify with quoting needed
let data_with_comma = [["a", "b,c", "d"]];
let quoted_out = csv_stringify(data_with_comma);
assert(contains(quoted_out, "\"b,c\""), "stringify: quotes field with comma");

// Test 18: Stringify with quote escaping
let data_with_quote = [["a", "b\"c", "d"]];
let quote_out = csv_stringify(data_with_quote);
assert(contains(quote_out, "\"b\"\"c\""), "stringify: escapes quotes");

// Test 19: Stringify with custom delimiter
let data_custom = [["a", "b", "c"]];
let custom_out = csv_stringify(data_custom, {delimiter: ";"});
assert(contains(custom_out, "a;b;c"), "stringify: custom delimiter");

// Test 20: Roundtrip test
let original = "name,age,city\nfrank,30,NYC\nalice,25,LA";
let parsed = csv_parse(original);
let stringified = csv_stringify(parsed);
let reparsed = csv_parse(stringified);
assert(len(reparsed) == len(parsed), "roundtrip: same number of rows");
assert(reparsed[0][0] == parsed[0][0], "roundtrip: first field matches");
assert(reparsed[1][2] == parsed[1][2], "roundtrip: last field matches");

// Test 21: Empty field in middle
let empty_field = "a,,c\n1,,3";
let ef_rows = csv_parse(empty_field);
assert(len(ef_rows[0]) == 3, "empty field: 3 fields");
assert(ef_rows[0][1] == "", "empty field: middle field is empty string");
assert(ef_rows[1][1] == "", "empty field: data row middle field empty");

// Test 22: Trailing comma
let trailing = "a,b,c,\n1,2,3,";
let tc_rows = csv_parse(trailing);
assert(len(tc_rows[0]) == 4, "trailing comma: creates empty last field");
assert(tc_rows[0][3] == "", "trailing comma: last field is empty");

print("csv.cs: all tests passed");
