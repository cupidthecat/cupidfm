"""Regenerate the checked-in 7z fixtures using system libarchive and Python lzma."""
from pathlib import Path
import ctypes as C, ctypes.util, lzma, struct, zlib
r=Path(__file__).resolve().parent/'fixtures';r.mkdir(exist_ok=True)
lib=C.CDLL(ctypes.util.find_library('archive'))
def function(name,args,ret=C.c_int):
    f=getattr(lib,name);f.argtypes=args;f.restype=ret;return f
P=C.c_void_p
new=function('archive_write_new',[],P);fmt=function('archive_write_set_format_7zip',[P])
opts=function('archive_write_set_options',[P,C.c_char_p]);opened=function('archive_write_open_filename',[P,C.c_char_p])
enew=function('archive_entry_new',[],P);name=function('archive_entry_set_pathname_utf8',[P,C.c_char_p],None)
kind=function('archive_entry_set_filetype',[P,C.c_uint],None);perm=function('archive_entry_set_perm',[P,C.c_int],None)
size=function('archive_entry_set_size',[P,C.c_longlong],None);header=function('archive_write_header',[P,P])
data=function('archive_write_data',[P,P,C.c_size_t],C.c_ssize_t);efree=function('archive_entry_free',[P],None)
close=function('archive_write_close',[P]);free=function('archive_write_free',[P]);error=function('archive_error_string',[P],C.c_char_p)
import locale
locale.setlocale(locale.LC_ALL,'C.UTF-8')
payload=b'hello CupidArchive\n'*20
for method,filename,entryname,content,entrykind in [
    ('copy','copy.7z','sample.txt',payload,0o100000),
    ('lzma1','lzma1.7z','sample.txt',payload,0o100000),
    ('lzma2','lzma2.7z','sample.txt',payload,0o100000),
    ('lzma2','unicode.7z','caf\u00e9-\U0001f600.txt',payload,0o100000),
    ('copy','empty-file.7z','empty',b'',0o100000),
    ('copy','directory.7z','folder',b'',0o040000),
    ('copy','empty-archive.7z',None,b'',0o100000)]:
    a=new();assert fmt(a)==0;assert opts(a,('7zip:compression='+method).encode())==0
    assert opened(a,str(r/filename).encode())==0
    if entryname is not None:
        e=enew();name(e,entryname.encode());kind(e,entrykind);perm(e,0o755 if entrykind==0o040000 else 0o644)
        size(e,len(content));assert header(a,e)==0,error(a)
        if content: assert data(a,C.c_char_p(content),len(content))==len(content),error(a)
        efree(e)
    assert close(a)==0,error(a);free(a)

def vint(value):
    for i in range(8):
        if value < 1 << (7*(i+1)):
            return bytes([((0xff << (8-i)) & 255) | (value >> (8*i))])+value.to_bytes(8,'little')[:i]
    return b'\xff'+value.to_bytes(8,'little')
raw=(r/'lzma1.7z').read_bytes();offset,header_size=struct.unpack_from('<QQ',raw,12)
plain=raw[32+offset:32+offset+header_size]
packed=lzma.compress(plain,format=lzma.FORMAT_RAW,filters=[{'id':lzma.FILTER_LZMA2,'dict_size':1<<20}])
encoded=(b'\x17\x06'+vint(offset)+b'\x01\x09'+vint(len(packed))+b'\x00\x07\x0b\x01\x00\x01\x21\x21\x01\x10\x0c'+vint(len(plain))+b'\x0a\x01'+struct.pack('<I',zlib.crc32(plain))+b'\x00\x00')
start=struct.pack('<QQI',offset+len(packed),len(encoded),zlib.crc32(encoded))
encoded_archive=raw[:8]+struct.pack('<I',zlib.crc32(start))+start+raw[32:32+offset]+packed+encoded
(r/'encoded-header.7z').write_bytes(encoded_archive)
# libarchive reads the encoded-header fixture independently of CupidArchive.
rnew=function('archive_read_new',[],P);support=function('archive_read_support_format_7zip',[P]);ropen=function('archive_read_open_filename',[P,C.c_char_p,C.c_size_t])
rnext=function('archive_read_next_header',[P,C.POINTER(P)]);rdata=function('archive_read_data',[P,P,C.c_size_t],C.c_ssize_t);rfree=function('archive_read_free',[P])
a=rnew();assert support(a)==0;assert ropen(a,str(r/'encoded-header.7z').encode(),10240)==0,error(a)
e=P();assert rnext(a,C.byref(e))==0,error(a)
buf=C.create_string_buffer(4096);n=rdata(a,buf,len(buf));assert buf.raw[:n]==payload,(n,error(a));rfree(a)
print('Generated and independently checked 8 small 7z fixtures')