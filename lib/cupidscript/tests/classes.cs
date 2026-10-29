// Classes and inheritance

class File {
    fn new(path) {
        self.path = path;
    }

    fn is_hidden() {
        return starts_with(self.path, ".");
    }
}

class ImageFile : File {
    fn new(path) {
        super.new(path);
        self.is_img = ends_with(path, ".png");
    }

    fn is_image() {
        return self.is_img;
    }
}

let f = File(".secret");
assert(f.is_hidden() == true, "hidden file");

let img = ImageFile("cat.png");
assert(img.is_hidden() == false, "inherited method");
assert(img.is_image() == true, "image file");

img.extra = 123;
assert(img.extra == 123, "dynamic field");

print("classes ok");
