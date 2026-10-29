// CSV parsing and generation demo

print("=== CSV Demo ===\n");

// Example 1: Basic CSV parsing
print("1. Basic CSV parsing:");
let csv_text = "name,age,city\nfrank,30,NYC\nalice,25,LA\nbob,35,SF";
let rows = csv_parse(csv_text);

print("  Total rows:", len(rows));
print("  First row:", rows[0]);
print("  Second row:", rows[1]);
print("");

// Example 2: Header mode (returns list of maps)
print("2. Header mode:");
let with_headers = csv_parse(csv_text, {headers: true});

print("  Records:", len(with_headers));
for record in with_headers {
    print("  - Name:", record.name, "Age:", record.age, "City:", record.city);
}
print("");

// Example 3: Custom delimiter (TSV)
print("3. Tab-separated values (TSV):");
let tsv_text = "product\tprice\tstock\napple\t1.50\t100\nbanana\t0.75\t150";
let tsv_rows = csv_parse(tsv_text, {delimiter: "\t"});

print("  TSV rows:", len(tsv_rows));
print("  First item:", tsv_rows[0][0], "costs", tsv_rows[1][1]);
print("");

// Example 4: Semicolon-separated (common in Europe)
print("4. Semicolon-separated:");
let ssv_text = "name;country;population\nParis;France;2.2M\nBerlin;Germany;3.7M";
let ssv_rows = csv_parse(ssv_text, {delimiter: ";"});

print("  Cities:", len(ssv_rows) - 1);
print("  Capital:", ssv_rows[1][0], "in", ssv_rows[1][1]);
print("");

// Example 5: Quoted fields with special characters
print("5. Quoted fields:");
let quoted_csv = "id,description,price\n1,\"Widget, deluxe\",29.99\n2,\"Gadget \"\"Pro\"\"\",49.99";
let quoted_rows = csv_parse(quoted_csv);

print("  Item 1 description:", quoted_rows[1][1]);  // Has comma in it
print("  Item 2 description:", quoted_rows[2][1]);  // Has quotes in it
print("");

// Example 6: Whitespace trimming
print("6. Whitespace trimming:");
let spaced_csv = " name , age , city \n frank , 30 , NYC \n alice , 25 , LA ";
let trimmed = csv_parse(spaced_csv, {trim: true});
let untrimmed = csv_parse(spaced_csv, {trim: false});

print("  Without trim:", untrimmed[0][0]);
print("  With trim:", trimmed[0][0]);
print("");

// Example 7: Generating CSV
print("7. Generating CSV:");
let data = [
    ["product", "price", "stock"],
    ["apple", "1.50", "100"],
    ["banana", "0.75", "150"],
    ["cherry", "3.00", "50"]
];

let csv_output = csv_stringify(data);
print("  Generated CSV:");
print(csv_output);

// Example 8: Generating CSV with custom delimiter
print("8. Custom delimiter output:");
let tsv_output = csv_stringify(data, {delimiter: "\t"});
print("  Generated TSV:");
print(tsv_output);

// Example 9: Roundtrip (parse → modify → stringify)
print("9. Roundtrip example:");
let original = "name,score\nalice,85\nbob,90\ncarol,95";
let scores = csv_parse(original, {headers: true});

print("  Original scores:");
for student in scores {
    print("    -", student.name, ":", student.score);
}

// Convert back to CSV
let scores_list = [["name", "score"]];
for student in scores {
    push(scores_list, [student.name, student.score]);
}
let modified_csv = csv_stringify(scores_list);
print("  Regenerated CSV:");
print(modified_csv);

// Example 10: Practical use case - processing sales data
print("10. Practical example - sales data:");
let sales_csv = "date,product,quantity,revenue\n2024-01-15,Widget,5,149.95\n2024-01-16,Gadget,3,119.97\n2024-01-17,Widget,2,59.98";
let sales = csv_parse(sales_csv, {headers: true});

let total_revenue = 0.0;
for sale in sales {
    let revenue = to_int(str_replace(sale.revenue, ".", ""));  // Simple float parsing hack
    total_revenue = total_revenue + revenue;
}

print("  Total sales records:", len(sales));
print("  Products sold:");
for sale in sales {
    print("    -", sale.date, ":", sale.quantity, "x", sale.product);
}
print("");

print("=== CSV demo complete ===");
