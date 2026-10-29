
# Classes & Inheritance

## Table of Contents

- [Defining Classes](#defining-classes)
- [Creating Instances](#creating-instances)
- [Inheritance](#inheritance)
- [Method Calls](#method-calls)

## Table of Contents

- [Defining Classes](#defining-classes)
- [Creating Instances](#creating-instances)
- [Inheritance](#inheritance)
- [Method Calls](#method-calls)

CupidScript supports classes with constructors, methods, and single inheritance.

## Defining Classes

```c
class File {
  fn new(path) {
    self.path = path;
  }

  fn is_hidden() {
    return starts_with(self.path, ".");
  }
}
```

* `fn new(...)` is the constructor.
* Methods are declared with `fn` inside the class body.
* `self` refers to the instance inside methods.

## Creating Instances

Classes are callable. Calling a class creates an instance and runs `new` if present:

```c
let f = File(".secret");
print(f.is_hidden());
```

Instance fields are dynamic and can be assigned at runtime:

```c
f.owner = "frank";
print(f.owner);
```

## Inheritance

Use `: Parent` to inherit methods and reuse constructors:

```c
class ImageFile : File {
  fn new(path) {
    super.new(path);
    self.is_img = ends_with(path, ".png");
  }

  fn is_image() {
    return self.is_img;
  }
}
```

* `super` refers to the parent class inside methods.
* `super.new(...)` is typically used in child constructors.

## Method Calls

```c
let img = ImageFile("cat.png");
print(img.is_image());
print(img.is_hidden()); // inherited
```
