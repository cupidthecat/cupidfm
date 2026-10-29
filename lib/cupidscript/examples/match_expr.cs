// match expression example

let status = 404;
let msg = match (status) {
  case 200: "ok";
  case 404: "not found";
  default: "error";
};

print(msg);
