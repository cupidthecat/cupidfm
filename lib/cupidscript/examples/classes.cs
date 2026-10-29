// Classes and inheritance examples

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
print(f.is_hidden());

let img = ImageFile("cat.png");
print(img.is_image());
print(img.is_hidden());
