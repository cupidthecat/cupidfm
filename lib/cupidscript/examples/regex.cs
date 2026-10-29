// Regex examples

let text = "User: alice42, email: alice@example.com";

print("has digits?", regex_is_match("[0-9]+", text));
print("full match?", regex_match("^User: \\w+$", text));

let email = regex_find("([a-z]+)@([a-z]+\\.[a-z]+)", text);
if (email != nil) {
  print("email match:", email["match"]);
  print("user:", email.groups[0]);
  print("domain:", email.groups[1]);
}

let nums = regex_find_all("[0-9]+", "x=7 y=42 z=105");
print("numbers:", nums);

let masked = regex_replace("[a-z]+@[a-z]+\\.[a-z]+", text, "<hidden>");
print("masked:", masked);
