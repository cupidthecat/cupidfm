import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

DRIVER = Path(__file__).resolve().parent / "archive_driver"

class Extraction(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="cupidarchive-")
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.dest = self.base / "dest"
        self.dest.mkdir()

    def tar(self, name, entries, mode="w"):
        path = self.base / name
        with tarfile.open(path, mode) as archive:
            for name, data, kind in entries:
                info = tarfile.TarInfo(name)
                info.mode = 0o644
                info.mtime = 1700000000
                if kind == "file":
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
                elif kind == "dir":
                    info.type = tarfile.DIRTYPE
                    info.mode = 0o555
                    archive.addfile(info)
                else:
                    info.type = tarfile.SYMTYPE if kind == "symlink" else tarfile.LNKTYPE
                    info.linkname = data
                    archive.addfile(info)
        return path

    def extract(self, path, stream=False):
        return subprocess.run([str(DRIVER), "stream" if stream else "path", str(path), str(self.dest)], timeout=10).returncode

    def test_compressed_tar_path_and_stream(self):
        for mode, suffix in (("w","tar"),("w:gz","tar.gz"),("w:bz2","tar.bz2"),("w:xz","tar.xz")):
            archive = self.tar("sample." + suffix, [("first",b"a"*10000,"file"),("empty",b"","file"),("last",b"end","file")], mode)
            for stream in (False, True):
                self.assertEqual(self.extract(archive, stream),0,(mode,stream))
                self.assertEqual((self.dest/"first").read_bytes(),b"a"*10000)
                self.assertEqual((self.dest/"empty").read_bytes(),b"")
                self.assertEqual((self.dest/"last").read_bytes(),b"end")

    def test_single_compressed_stream_ownership(self):
        import gzip, bz2, lzma
        for encode, extension in ((gzip.compress,"gz"),(bz2.compress,"bz2"),(lzma.compress,"xz")):
            archive=self.base/("content."+extension)
            archive.write_bytes(encode(b"payload" * 200))
            self.assertEqual(self.extract(archive),0)
            self.assertEqual((self.dest/"content").read_bytes(), b"payload"*200)
            self.assertEqual(self.extract(archive,True),0)
            self.assertEqual((self.dest/"file").read_bytes(), b"payload"*200)

    def test_existing_file_preserved_on_truncation(self):
        archive=self.tar("truncated.tar",[("file",b"a"*10000,"file")])
        archive.write_bytes(archive.read_bytes()[:1000])
        (self.dest/"file").write_bytes(b"original")
        self.assertNotEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"file").read_bytes(),b"original")
        self.assertEqual(sorted(p.name for p in self.dest.iterdir()),["file"])

    def test_reject_symlink_ancestors_and_traversal(self):
        outside=self.base/"outside"
        outside.mkdir()
        (self.dest/"nested").symlink_to(outside,target_is_directory=True)
        archive=self.tar("unsafe.tar",[("nested/file",b"bad","file"),("../escaped",b"bad","file")])
        self.assertNotEqual(self.extract(archive),0)
        self.assertFalse((outside/"file").exists())
        self.assertFalse((self.base/"escaped").exists())

    def test_forward_hardlinks_and_directories(self):
        archive=self.tar("links.tar",[("folder",b"","dir"),("folder/link","folder/target","hardlink"),("folder/target",b"hello","file")])
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"folder/link").read_bytes(),b"hello")
        self.assertEqual((self.dest/"folder/link").stat().st_ino,(self.dest/"folder/target").stat().st_ino)
        self.assertEqual((self.dest/"folder").stat().st_mode & 0o777,0o555)
        self.assertEqual(int((self.dest/"folder").stat().st_mtime),1700000000)
        (self.dest/"folder").chmod(0o755)

    def test_zip_content_and_empty(self):
        archive=self.base/"sample.zip"
        with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("folder/file",b"a"*10000)
            z.writestr("empty",b"")
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"folder/file").read_bytes(),b"a"*10000)
        self.assertEqual((self.dest/"empty").read_bytes(),b"")
        self.assertEqual(self.extract(archive,True),0)

    def test_corrupt_zip_preserves_destination(self):
        archive=self.base/"corrupt.zip"
        with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_STORED) as z:
            z.writestr("file",b"original")
        raw=archive.read_bytes()
        archive.write_bytes(raw.replace(b"original",b"modified",1))
        (self.dest/"file").write_bytes(b"existing")
        self.assertNotEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"file").read_bytes(),b"existing")

    def test_truncated_compressed_preserves_destination(self):
        import gzip, bz2, lzma
        for encode, extension in ((gzip.compress,"gz"),(bz2.compress,"bz2"),(lzma.compress,"xz")):
            archive=self.base/("content."+extension)
            archive.write_bytes(encode(b"payload" * 200)[:-4])
            (self.dest/"content").write_bytes(b"existing")
            self.assertNotEqual(self.extract(archive),0,extension)
            self.assertEqual((self.dest/"content").read_bytes(),b"existing")

    def test_reader_limits_are_independent(self):
        archive=self.tar("limits.tar",[("first",b"one","file"),("second",b"two","file")])
        result=subprocess.run([str(DRIVER),"path",str(archive),str(self.dest),"limits"],timeout=10)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual((self.dest/"first").read_bytes(),b"one")
        self.assertFalse((self.dest/"second").exists())

    def test_7z_supported_methods_names_and_encoded_headers(self):
        fixtures=Path(__file__).resolve().parent/"fixtures"
        payload=b"hello CupidArchive\n"*20
        for fixture,name in (("copy","sample.txt"),("lzma1","sample.txt"),("lzma2","sample.txt"),
                             ("encoded-header","sample.txt"),("unicode","caf\u00e9-\U0001f600.txt")):
            for stream in (False,True):
                self.assertEqual(self.extract(fixtures/(fixture+".7z"),stream),0,fixture)
                self.assertEqual((self.dest/name).read_bytes(),payload)
        self.assertEqual(self.extract(fixtures/"empty-file.7z"),0)
        self.assertEqual((self.dest/"empty").read_bytes(),b"")
        self.assertEqual(self.extract(fixtures/"directory.7z"),0)
        self.assertTrue((self.dest/"folder").is_dir())
        self.assertEqual(self.extract(fixtures/"empty-archive.7z"),0)

    def test_7z_checksums_and_filename_bounds(self):
        import struct,zlib
        original=(Path(__file__).resolve().parent/"fixtures/copy.7z").read_bytes()
        path=self.base/"corrupt.7z"
        (self.dest/"sample.txt").write_bytes(b"existing")
        variants=[]
        for position in (8,28,32):
            raw=bytearray(original);raw[position]^=1;variants.append(raw)
        offset,size=struct.unpack_from("<QQ",original,12)
        for name_size in (0,127):
            raw=bytearray(original)
            where=raw.index(b"\x05\x01\x11",32+offset)+3
            raw[where]=name_size
            struct.pack_into("<I",raw,28,zlib.crc32(raw[32+offset:32+offset+size]))
            struct.pack_into("<I",raw,8,zlib.crc32(raw[12:32]))
            variants.append(raw)
        for raw in variants:
            path.write_bytes(raw)
            self.assertNotEqual(self.extract(path),0)
            self.assertEqual((self.dest/"sample.txt").read_bytes(),b"existing")

    def test_empty_archives(self):
        archive=self.tar("empty.tar",[])
        self.assertEqual(self.extract(archive),0)
        self.assertEqual(self.extract(archive,True),0)
        self.assertEqual(list(self.dest.iterdir()),[])

if __name__ == "__main__": unittest.main()
