// EXPECT_FAIL
// Unterminated /* ... */ comment should lex as error

/* this comment never closes
let x = 1;
print(x);
