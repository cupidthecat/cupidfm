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

    def test_tar_compression_trailers_are_checked(self):
        import gzip,bz2,lzma
        raw=self.tar("raw.tar",[("file",b"payload","file")]).read_bytes()
        for encode,ext in ((gzip.compress,"gz"),(bz2.compress,"bz2"),(lzma.compress,"xz")):
            packed=encode(raw)
            for stream in (False,True):
                for data in (packed[:-8],packed[:-8]+bytes([packed[-8]^1])+packed[-7:]):
                    archive=self.base/("bad.tar."+ext)
                    archive.write_bytes(data)
                    self.assertNotEqual(self.extract(archive,stream),0,(ext,stream))

    def test_compressed_zip_and_7z_use_decoded_random_access(self):
        import gzip,bz2,lzma
        zipped=io.BytesIO()
        with zipfile.ZipFile(zipped,"w",compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("zip-file",b"ZIP contents")
        seven=(Path(__file__).resolve().parent/"fixtures/lzma2.7z").read_bytes()
        for encode,ext in ((gzip.compress,"gz"),(bz2.compress,"bz2"),(lzma.compress,"xz")):
            for contents,format,name,expected in ((zipped.getvalue(),"zip","zip-file",b"ZIP contents"),
                                                 (seven,"7z","sample.txt",b"hello CupidArchive\n"*20)):
                archive=self.base/("packed."+format+"."+ext)
                archive.write_bytes(encode(contents))
                for stream in (False,True):
                    self.assertEqual(self.extract(archive,stream),0,(format,ext,stream))
                    self.assertEqual((self.dest/name).read_bytes(),expected)

    def test_exact_decoded_limit_allows_container_trailer(self):
        import gzip,bz2,lzma
        raw=self.tar("raw.tar",[("file",b"payload","file")]).read_bytes()
        for encode,ext in ((gzip.compress,"gz"),(bz2.compress,"bz2"),(lzma.compress,"xz")):
            archive=self.base/("limit.tar."+ext)
            archive.write_bytes(encode(raw))
            result=subprocess.run([str(DRIVER),"byte-limit",str(archive),str(self.dest),str(len(raw))],timeout=10)
            self.assertEqual(result.returncode,0,ext)
            result=subprocess.run([str(DRIVER),"byte-limit",str(archive),str(self.dest),str(len(raw)-1)],timeout=10)
            self.assertNotEqual(result.returncode,0,ext)

    def test_xz_without_content_checksum_is_valid(self):
        import lzma
        archive=self.base/"nocheck.xz"
        archive.write_bytes(lzma.compress(b"payload",check=lzma.CHECK_NONE))
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"nocheck").read_bytes(),b"payload")

    def test_short_reads_in_archive_streams(self):
        import gzip
        archive=self.tar("short.tar",[("file",b"contents","file")])
        compressed=self.base/"short.tar.gz";compressed.write_bytes(gzip.compress(archive.read_bytes()))
        zipped=self.base/"short.zip"
        with zipfile.ZipFile(zipped,"w",compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr("zip-file",b"ZIP contents")
        cases=((archive,"file",b"contents"),(compressed,"file",b"contents"),(zipped,"zip-file",b"ZIP contents"),
               (Path(__file__).resolve().parent/"fixtures/lzma1.7z","sample.txt",b"hello CupidArchive\n"*20))
        for file,name,expected in cases:
            result=subprocess.run([str(DRIVER),"chunked",str(file),str(self.dest)],timeout=10)
            self.assertEqual(result.returncode,0,file)
            self.assertEqual((self.dest/name).read_bytes(),expected)

    def test_directory_names_do_not_change_compressed_file_detection(self):
        import gzip
        directory=self.base/"folder.tar.cache";directory.mkdir()
        archive=directory/"payload.gz";archive.write_bytes(gzip.compress(b"contents"))
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"payload").read_bytes(),b"contents")

    def test_path_reader_rejects_fifo_without_blocking(self):
        archive=self.base/"fifo.tar";os.mkfifo(archive)
        self.assertNotEqual(self.extract(archive),0)

    def test_gnu_and_pax_sparse_files_reconstruct_holes_and_following_members(self):
        import gzip,shutil
        if not shutil.which("tar") or b"GNU tar" not in subprocess.check_output(["tar","--version"]):
            self.skipTest("GNU tar is needed to generate independent sparse fixtures")
        original=self.base/"sparse-file"
        with original.open("wb") as out:
            for i in range(40):
                out.seek(i*16384);out.write(("island-%d"%i).encode())
            out.truncate(40*16384+100)
        (self.base/"following").write_bytes(b"after sparse data")
        expected=original.read_bytes()
        for format,version in (("gnu",None),("pax","0.0"),("pax","0.1"),("pax","1.0")):
            archive=self.base/("sparse-"+(version or format)+".tar")
            args=["tar","--sparse","--hole-detection=raw","--format="+format]
            if version: args.append("--sparse-version="+version)
            subprocess.run(args+["-cf",str(archive),"-C",str(self.base),"sparse-file","following"],check=True)
            with tarfile.open(archive) as independent:
                self.assertEqual(independent.extractfile("sparse-file").read(),expected)
            compressed=archive.with_suffix(".tar.gz");compressed.write_bytes(gzip.compress(archive.read_bytes()))
            for path in (archive,compressed):
                for stream in (False,True):
                    self.assertEqual(self.extract(path,stream),0,(format,version,stream,path))
                    output=self.dest/"sparse-file"
                    self.assertEqual(output.read_bytes(),expected)
                    self.assertEqual((self.dest/"following").read_bytes(),b"after sparse data")
                    if hasattr(original.stat(),"st_blocks") and original.stat().st_blocks*512<len(expected)//2:
                        self.assertLess(output.stat().st_blocks*512,len(expected)//2,"extraction should retain holes")

    def test_all_hole_sparse_file(self):
        import shutil
        if not shutil.which("tar") or b"GNU tar" not in subprocess.check_output(["tar","--version"]):
            self.skipTest("GNU tar is needed for sparse fixtures")
        source=self.base/"holes"
        with source.open("wb") as out: out.truncate(100001)
        for format,version in (("gnu",None),("pax","0.0"),("pax","0.1"),("pax","1.0")):
            archive=self.base/("holes-"+(version or format)+".tar")
            args=["tar","--sparse","--hole-detection=raw","--format="+format]
            if version: args.append("--sparse-version="+version)
            subprocess.run(args+["-cf",str(archive),"-C",str(self.base),"holes"],check=True)
            self.assertEqual(self.extract(archive),0,(format,version))
            self.assertEqual((self.dest/"holes").read_bytes(),b"\0"*100001)

    def test_pax_size_override_controls_data_and_alignment(self):
        archive=self.base/"pax-size.tar"
        with tarfile.open(archive,"w",format=tarfile.PAX_FORMAT) as out:
            info=tarfile.TarInfo("first");info.size=7;info.pax_headers={"size":"7"}
            out.addfile(info,io.BytesIO(b"payload"))
            info=tarfile.TarInfo("second");info.size=3;out.addfile(info,io.BytesIO(b"end"))
        raw=bytearray(archive.read_bytes())
        raw[1024+124:1024+136]=b"00000000000\0"
        raw[1024+148:1024+156]=b"        "
        raw[1024+148:1024+156]=("%06o\0 " % sum(raw[1024:1536])).encode()
        archive.write_bytes(raw)
        with tarfile.open(archive) as independent:
            self.assertEqual(independent.extractfile("first").read(),b"payload")
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/"first").read_bytes(),b"payload")
        self.assertEqual((self.dest/"second").read_bytes(),b"end")

    def test_gnu_long_names_keep_newlines(self):
        archive=self.base/"long.tar"
        name="a"*110+"\nline"
        with tarfile.open(archive,"w",format=tarfile.GNU_FORMAT) as out:
            info=tarfile.TarInfo(name);info.size=7;out.addfile(info,io.BytesIO(b"payload"))
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/name).read_bytes(),b"payload")

    def test_invalid_pax_records_and_sparse_maps_are_rejected(self):
        def pax_record(key,value):
            body=(key+"="+value+"\n").encode();size=len(body)+2
            while True:
                record=str(size).encode()+b" "+body
                if len(record)==size:return record
                size=len(record)
        for fields in (("GNU.sparse.map","0,8,4,8"),("GNU.sparse.map","18446744073709551615,1"),
                       ("GNU.sparse.map","0,16,"),("GNU.sparse.map","0,7")):
            body=b"".join(pax_record(k,v) for k,v in (("GNU.sparse.size","16"),("GNU.sparse.numblocks","2"),fields))
            archive=self.base/"bad-sparse.tar"
            with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
                metadata=tarfile.TarInfo("pax");metadata.type=tarfile.XHDTYPE;metadata.size=len(body)
                out.addfile(metadata,io.BytesIO(body))
                info=tarfile.TarInfo("file");info.size=16;out.addfile(info,io.BytesIO(b"x"*16))
            (self.dest/"file").write_bytes(b"existing")
            self.assertNotEqual(self.extract(archive),0,fields)
            self.assertEqual((self.dest/"file").read_bytes(),b"existing")
        for body in (b"1 path=x\n",b"999999999999999999999999999999 path=x\n",b"9 path=x!",b"9 path=\0\n"):
            archive=self.base/"bad-pax.tar"
            with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
                info=tarfile.TarInfo("pax");info.type=tarfile.XHDTYPE;info.size=len(body);out.addfile(info,io.BytesIO(body))
            self.assertNotEqual(self.extract(archive),0,body)

    def test_global_and_local_pax_attributes(self):
        archive=self.base/"global-pax.tar"
        with tarfile.open(archive,"w",format=tarfile.PAX_FORMAT,pax_headers={"uid":"777","gid":"333","mtime":"3.25"}) as out:
            info=tarfile.TarInfo("first");info.size=1;info.pax_headers={"uid":"888"};out.addfile(info,io.BytesIO(b"a"))
            info=tarfile.TarInfo("second");info.size=1;out.addfile(info,io.BytesIO(b"b"))
        result=subprocess.run([str(DRIVER),"inspect",str(archive),str(self.dest)],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines(),["first\t1\t888\t333\t3","second\t1\t777\t333\t3"])

    def test_pax_global_value_deletion_and_metadata_only_archive(self):
        def record(key,value):
            body=(key+"="+value+"\n").encode();size=len(body)+2
            while len(str(size).encode()+b" "+body)!=size:size=len(str(size).encode()+b" "+body)
            return str(size).encode()+b" "+body
        archive=self.base/"global-delete.tar"
        with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
            for value,name in (("777","first"),("","second")):
                body=record("uid",value)
                info=tarfile.TarInfo("global");info.type=tarfile.XGLTYPE;info.size=len(body);out.addfile(info,io.BytesIO(body))
                info=tarfile.TarInfo(name);info.uid=42;out.addfile(info)
        result=subprocess.run([str(DRIVER),"inspect",str(archive),str(self.dest)],capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(result.stdout.splitlines(),["first\t0\t777\t0\t0","second\t0\t42\t0\t0"])
        with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
            body=record("uid","777");info=tarfile.TarInfo("global");info.type=tarfile.XGLTYPE;info.size=len(body)
            out.addfile(info,io.BytesIO(body))
        self.assertEqual(self.extract(archive),0)

    def test_tar_checksum_encodings_and_numeric_overflow(self):
        archive=self.tar("numeric.tar",[("file",b"payload","file")])
        original=archive.read_bytes()
        def checksum(raw,signed=False):
            raw[148:156]=b"        "
            total=sum((x if x<128 or not signed else x-256) for x in raw[:512])
            raw[148:156]=("%06o\0 "%total).encode()
        raw=bytearray(original)
        total=sum(raw[:148])+8*32+sum(raw[156:512])
        raw[148:156]=("%7o\0"%total).encode();archive.write_bytes(raw)
        self.assertEqual(self.extract(archive),0)
        raw=bytearray(original);raw[0]=233;checksum(raw,True);archive.write_bytes(raw)
        self.assertEqual(self.extract(archive),0)
        self.assertEqual((self.dest/os.fsdecode(bytes([233])+b"ile")).read_bytes(),b"payload")
        for size in (b"\x80\x01"+b"\0"*10,b"\xff"*12):
            raw=bytearray(original);raw[124:136]=size;checksum(raw);archive.write_bytes(raw)
            (self.dest/"file").write_bytes(b"existing")
            self.assertNotEqual(self.extract(archive),0)
            self.assertEqual((self.dest/"file").read_bytes(),b"existing")

    def test_directory_attributes_use_last_entry_for_canonical_path(self):
        archive=self.base/"duplicate-dir.tar"
        with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
            for name,mode,mtime in (("directory",0o750,1),("./directory/.",0o700,2)):
                entry=tarfile.TarInfo(name);entry.type=tarfile.DIRTYPE;entry.mode=mode;entry.mtime=mtime;out.addfile(entry)
        self.assertEqual(self.extract(archive),0)
        st=(self.dest/"directory").stat()
        self.assertEqual(st.st_mode&0o777,0o700)
        self.assertEqual(int(st.st_mtime),2)

    def test_restrictive_parent_metadata_is_applied_after_children(self):
        archive=self.base/"unordered-dir.tar"
        with tarfile.open(archive,"w",format=tarfile.USTAR_FORMAT) as out:
            for name,mode in (("parent/child",0o711),("parent",0)):
                entry=tarfile.TarInfo(name);entry.type=tarfile.DIRTYPE;entry.mode=mode;entry.mtime=1;out.addfile(entry)
            entry=tarfile.TarInfo("parent/child/file");entry.size=7;out.addfile(entry,io.BytesIO(b"payload"))
        try:
            self.assertEqual(self.extract(archive),0)
            parent=self.dest/"parent"
            self.assertEqual(parent.stat().st_mode&0o777,0)
            parent.chmod(0o755)
            self.assertEqual((parent/"child").stat().st_mode&0o777,0o711)
            self.assertEqual((parent/"child/file").read_bytes(),b"payload")
        finally:
            if (self.dest/"parent").exists(): (self.dest/"parent").chmod(0o755)
            if (self.dest/"parent/child").exists(): (self.dest/"parent/child").chmod(0o755)

    def test_empty_archives(self):
        archive=self.tar("empty.tar",[])
        self.assertEqual(self.extract(archive),0)
        self.assertEqual(self.extract(archive,True),0)
        self.assertEqual(list(self.dest.iterdir()),[])

if __name__ == "__main__": unittest.main()
