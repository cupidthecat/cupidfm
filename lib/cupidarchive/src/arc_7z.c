#define _POSIX_C_SOURCE 200809L
#include "arc_7z.h"
#include "arc_base.h"
#include <errno.h>
#include <limits.h>
#include <lzma.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define HEADER_LIMIT (64U * 1024U * 1024U)
#define DECODER_MEMORY_LIMIT (128U * 1024U * 1024U)
#define METHOD_COPY 0
#define METHOD_LZMA 0x030101
#define METHOD_LZMA2 0x21

enum { END=0, HEADER=1, ARCHIVE_PROPERTIES=2, ADDITIONAL_STREAMS=3,
       MAIN_STREAMS=4, FILES=5, PACK=6, UNPACK=7, SUBSTREAMS=8, SIZE=9,
       CRC=10, FOLDER=11, UNPACK_SIZE=12, NUM_SUBSTREAMS=13,
       EMPTY_STREAM=14, EMPTY_FILE=15, NAME=17, MTIME=20, ATTRIBUTES=21,
       ENCODED_HEADER=23 };

typedef struct Cursor { const uint8_t *data; size_t size, pos; } Cursor;
typedef struct Folder {
    uint64_t offset, packed, unpacked, method;
    uint8_t props[5];
    size_t props_size;
    uint32_t packed_crc, unpacked_crc;
    bool has_packed_crc, has_unpacked_crc, present;
} Folder;
typedef struct SevenZReader {
    ArcReaderBase base;
    ArcEntry entry;
    Folder folder;
    bool returned, valid;
} SevenZReader;

static int invalid(void) { errno=EINVAL; return -1; }
static int unsupported(void) { errno=ENOSYS; return -1; }
static const uint8_t *take(Cursor *c, size_t n) {
    if (c->pos>c->size || n>c->size-c->pos) { invalid(); return NULL; }
    const uint8_t *result=c->data+c->pos; c->pos+=n; return result;
}
static int byte(Cursor *c) {
    const uint8_t *p=take(c,1); return p ? *p : -1;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | (uint64_t)le32(p+4)<<32; }
static int number(Cursor *c,uint64_t *value) {
    int first=byte(c);
    if(first<0) return -1;
    unsigned mask=128; *value=0;
    for(unsigned i=0;i<8;i++,mask>>=1) {
        if(!(first&mask)) { *value|=(uint64_t)(first&(mask-1))<<(8*i); return 0; }
        int b=byte(c); if(b<0) return -1;
        *value|=(uint64_t)b<<(8*i);
    }
    return 0;
}
static int digest(Cursor *c,bool *defined,uint32_t *crc) {
    int all=byte(c);
    if(all<0 || all>1) return invalid();
    int bits=all ? 128 : byte(c);
    if(bits<0) return -1;
    *defined=(bits&128)!=0;
    if(*defined) { const uint8_t *p=take(c,4); if(!p) return -1; *crc=le32(p); }
    return 0;
}
static int streams(Cursor *c,Folder *f) {
    bool have_pack=false,have_unpack=false;
    int id;
    while((id=byte(c))>0) {
        uint64_t count;
        if(id==PACK) {
            if(have_pack || number(c,&f->offset) || number(c,&count)) return invalid();
            if(count!=1) return unsupported();
            have_pack=true;
            bool have_size=false;
            while((id=byte(c))>0) {
                if(id==SIZE) { if(have_size || number(c,&f->packed)) return invalid(); have_size=true; }
                else if(id==CRC) { if(digest(c,&f->has_packed_crc,&f->packed_crc)) return -1; }
                else return unsupported();
            }
            if(id<0 || !have_size) return invalid();
        } else if(id==UNPACK) {
            if(have_unpack || byte(c)!=FOLDER || number(c,&count)) return invalid();
            if(count!=1 || byte(c)!=0 || number(c,&count) || count!=1) return unsupported();
            have_unpack=true;
            int flags=byte(c);
            if(flags<0 || !(flags&15) || (flags&15)>8 || (flags&0xc0)) return invalid();
            const uint8_t *method=take(c,(size_t)(flags&15));
            if(!method) return -1;
            for(int i=0;i<(flags&15);i++) f->method=(f->method<<8)|method[i];
            if(flags&16) {
                uint64_t inputs,outputs;
                if(number(c,&inputs)||number(c,&outputs)) return -1;
                if(inputs!=1 || outputs!=1) return unsupported();
            }
            if(flags&32) {
                if(number(c,&count)) return -1;
                if(count>sizeof(f->props)) return unsupported();
                const uint8_t *p=take(c,(size_t)count);
                if(!p) return -1;
                memcpy(f->props,p,(size_t)count); f->props_size=(size_t)count;
            }
            if(byte(c)!=UNPACK_SIZE || number(c,&f->unpacked)) return invalid();
            id=byte(c);
            if(id==CRC) { if(digest(c,&f->has_unpacked_crc,&f->unpacked_crc)) return -1; id=byte(c); }
            if(id!=END) return invalid();
        } else if(id==SUBSTREAMS) {
            if(!have_unpack) return invalid();
            while((id=byte(c))>0) {
                if(id==NUM_SUBSTREAMS) {
                    if(number(c,&count)) return -1;
                    if(count!=1) return unsupported();
                } else if(id==CRC) {
                    if(f->has_unpacked_crc) return invalid();
                    if(digest(c,&f->has_unpacked_crc,&f->unpacked_crc)) return -1;
                } else return unsupported();
            }
            if(id<0) return -1;
        } else return unsupported();
    }
    if(id<0 || !have_pack || !have_unpack) return invalid();
    if((f->method==METHOD_COPY && f->props_size==0 && f->packed==f->unpacked) ||
       (f->method==METHOD_LZMA && f->props_size==5) ||
       (f->method==METHOD_LZMA2 && f->props_size==1)) { f->present=true; return 0; }
    return unsupported();
}

/* A checked stream owns its input. Validate as soon as the last byte is read,
 * so a decoder need not make another input read to discover a checksum error. */
typedef struct Checked {
    ArcStream *input;
    uint64_t expected,total;
    uint32_t crc,expected_crc;
    bool check_crc,failed;
} Checked;
static ssize_t checked_read(ArcStream *s,void *buffer,size_t size) {
    Checked *d=s->user_data;
    if(d->failed) { errno=EIO; return -1; }
    if(size>UINT_MAX) size=UINT_MAX;
    ssize_t n=arc_stream_read_fully(d->input,buffer,size);
    if(n>0) {
        if((uint64_t)n>d->expected-d->total) { d->failed=true; errno=EIO; return -1; }
        d->total+=(uint64_t)n; d->crc=crc32(d->crc,buffer,(uInt)n);
    }
    if((n==0 && d->total!=d->expected) ||
       (d->total==d->expected && d->check_crc && d->crc!=d->expected_crc)) {
        d->failed=true; errno=EIO; return -1;
    }
    return n;
}
static int64_t checked_tell(ArcStream *s) { return (int64_t)((Checked *)s->user_data)->total; }
static void checked_close(ArcStream *s) {
    Checked *d=s->user_data; arc_stream_close(d->input); free(d); free(s);
}
static ArcStream *checked(ArcStream *input,uint64_t size,bool crc,uint32_t expected_crc) {
    static const struct ArcStreamVtable table={checked_read,NULL,checked_tell,checked_close};
    Checked *d=calloc(1,sizeof(*d)); ArcStream *s=calloc(1,sizeof(*s));
    if(!d || !s) { free(d);free(s);arc_stream_close(input);return NULL; }
    d->input=input; d->expected=size; d->check_crc=crc; d->expected_crc=expected_crc;
    s->vtable=&table; s->user_data=d; return s;
}

typedef struct Decoder {
    ArcStream *input;
    lzma_stream stream;
    lzma_filter filters[2];
    uint8_t buffer[65536];
    bool done,input_done;
} Decoder;
static ssize_t decoder_read(ArcStream *s,void *buffer,size_t size) {
    Decoder *d=s->user_data;
    if(d->done) return 0;
    d->stream.next_out=buffer; d->stream.avail_out=size;
    while(d->stream.avail_out) {
        if(!d->stream.avail_in && !d->input_done) {
            ssize_t n=arc_stream_read_fully(d->input,d->buffer,sizeof(d->buffer));
            if(n<0) return -1;
            d->input_done=n==0;
            d->stream.next_in=d->buffer; d->stream.avail_in=(size_t)n;
        }
        size_t before_in=d->stream.avail_in,before_out=d->stream.avail_out;
        lzma_ret ret=lzma_code(&d->stream,d->input_done ? LZMA_FINISH : LZMA_RUN);
        if(ret==LZMA_STREAM_END) { d->done=true;break; }
        if(ret!=LZMA_OK && ret!=LZMA_BUF_ERROR) { errno=EIO;return -1; }
        if(before_in==d->stream.avail_in && before_out==d->stream.avail_out &&
           (d->input_done || d->stream.avail_in)) { errno=EIO;return -1; }
    }
    size_t n=size-d->stream.avail_out; s->bytes_read+=(int64_t)n;return (ssize_t)n;
}
static int64_t decoder_tell(ArcStream *s) { return s->bytes_read; }
static void decoder_close(ArcStream *s) {
    Decoder *d=s->user_data;
    lzma_end(&d->stream); lzma_filters_free(d->filters,NULL);
    arc_stream_close(d->input);free(d);free(s);
}
static ArcStream *open_folder(ArcStream *source,const Folder *f,const ArcLimits *limits,uint64_t archive_size) {
    if(f->offset>archive_size || archive_size-f->offset<32 || f->packed>archive_size-32-f->offset ||
       f->offset>INT64_MAX-32 || f->packed>INT64_MAX || f->unpacked>INT64_MAX ||
       f->unpacked>limits->max_uncompressed_bytes) { errno=EOVERFLOW;return NULL; }
    ArcStream *input=arc_stream_substream(source,(int64_t)f->offset+32,(int64_t)f->packed);
    if(!input) return NULL;
    input=checked(input,f->packed,f->has_packed_crc,f->packed_crc);
    if(!input) return NULL;
    if(f->method!=METHOD_COPY) {
        static const struct ArcStreamVtable table={decoder_read,NULL,decoder_tell,decoder_close};
        Decoder *d=calloc(1,sizeof(*d)); ArcStream *s=calloc(1,sizeof(*s));
        if(!d||!s) {free(d);free(s);arc_stream_close(input);return NULL;}
        d->input=input; d->stream=(lzma_stream)LZMA_STREAM_INIT;
        d->filters[0].id=f->method==METHOD_LZMA ? LZMA_FILTER_LZMA1 : LZMA_FILTER_LZMA2;
        d->filters[1].id=LZMA_VLI_UNKNOWN;
        s->vtable=&table; s->user_data=d;
        if(lzma_properties_decode(&d->filters[0],NULL,f->props,f->props_size)!=LZMA_OK) {
            decoder_close(s);errno=EINVAL;return NULL;
        }
        if(f->method==METHOD_LZMA) {
            lzma_options_lzma *options=d->filters[0].options;
            options->ext_flags=LZMA_LZMA1EXT_ALLOW_EOPM;
            lzma_set_ext_size(*options,f->unpacked);
            d->filters[0].id=LZMA_FILTER_LZMA1EXT;
        }
        if(lzma_raw_decoder_memusage(d->filters)>DECODER_MEMORY_LIMIT ||
           lzma_raw_decoder(&d->stream,d->filters)!=LZMA_OK) {
            decoder_close(s);errno=EOVERFLOW;return NULL;
        }
        input=s;
    }
    return checked(input,f->unpacked,f->has_unpacked_crc,f->unpacked_crc);
}

static char *name_utf8(const uint8_t *data,size_t size,uint64_t max_name) {
    if(size<2 || size%2 || size>SIZE_MAX/2-1) {invalid();return NULL;}
    if(size/2-1>max_name) {invalid();return NULL;}
    char *out=malloc(size*2+1);
    if(!out) return NULL;
    size_t n=0;
    for(size_t pos=0;pos<size;pos+=2) {
        uint32_t ch=(uint32_t)data[pos]|(uint32_t)data[pos+1]<<8;
        if(!ch) {
            if(pos+2!=size || !n || n>max_name) break;
            out[n]=0;return out;
        }
        if(ch>=0xd800 && ch<=0xdbff) {
            if(pos+3>=size) break;
            uint32_t low=(uint32_t)data[pos+2]|(uint32_t)data[pos+3]<<8;
            if(low<0xdc00 || low>0xdfff) break;
            ch=0x10000+((ch-0xd800)<<10)+(low-0xdc00);pos+=2;
        } else if(ch>=0xdc00 && ch<=0xdfff) break;
        if(ch<0x80) out[n++]=(char)ch;
        else if(ch<0x800) {out[n++]=(char)(0xc0|(ch>>6));out[n++]=(char)(0x80|(ch&63));}
        else if(ch<0x10000) {out[n++]=(char)(0xe0|(ch>>12));out[n++]=(char)(0x80|((ch>>6)&63));out[n++]=(char)(0x80|(ch&63));}
        else {out[n++]=(char)(0xf0|(ch>>18));out[n++]=(char)(0x80|((ch>>12)&63));out[n++]=(char)(0x80|((ch>>6)&63));out[n++]=(char)(0x80|(ch&63));}
        if(n>max_name) break;
    }
    free(out);invalid();return NULL;
}
static int files(Cursor *c,SevenZReader *reader,uint64_t *count) {
    if(number(c,count)) return -1;
    if(*count>1) return unsupported();
    bool empty=false,regular_empty=false;
    int id;
    while((id=byte(c))>0) {
        uint64_t length;
        if(number(c,&length) || length>c->size-c->pos) return invalid();
        const uint8_t *p=take(c,(size_t)length);
        if(!p) return -1;
        if(id==NAME) {
            if(!*count || length<3 || p[0] || reader->entry.path) return invalid();
            reader->entry.path=name_utf8(p+1,(size_t)length-1,reader->base.limits->max_name);
            if(!reader->entry.path) return -1;
        } else if(id==EMPTY_STREAM || id==EMPTY_FILE) {
            if(length!=1 || !*count) return invalid();
            if(id==EMPTY_STREAM) empty=(p[0]&128)!=0;
            else regular_empty=(p[0]&128)!=0;
        } else if(id==16) {
            return unsupported();
        } else if(id==ATTRIBUTES || id==MTIME) {
            Cursor property={p,(size_t)length,0};
            int all=byte(&property);
            if(all<0 || all>1) return invalid();
            int defined=all ? 128 : byte(&property);
            if(defined<0 || byte(&property)!=0) return unsupported();
            if(defined&128) {
                const uint8_t *value=take(&property,id==ATTRIBUTES ? 4 : 8);
                if(!value) return -1;
                if(id==ATTRIBUTES && (le32(value)>>16)) reader->entry.mode=(le32(value)>>16)&0777;
                if(id==MTIME && le64(value)>=116444736000000000ULL)
                    reader->entry.mtime=(le64(value)-116444736000000000ULL)/10000000;
            }
            if(property.pos!=property.size) return invalid();
        }
    }
    if(id<0 || (regular_empty && !empty)) return invalid();
    reader->entry.type=empty && !regular_empty ? ARC_ENTRY_DIR : ARC_ENTRY_FILE;
    if(empty && reader->folder.present) return invalid();
    if(!empty && *count && !reader->folder.present) return invalid();
    return 0;
}
static int parse_header(Cursor *c,SevenZReader *reader) {
    if(byte(c)!=HEADER) return invalid();
    bool have_files=false;
    uint64_t count=0;
    int id;
    while((id=byte(c))>0) {
        if(id==MAIN_STREAMS) {
            if(reader->folder.present || streams(c,&reader->folder)) return -1;
        } else if(id==FILES) {
            if(have_files || files(c,reader,&count)) return -1;
            have_files=true;
        } else if(id==ARCHIVE_PROPERTIES) {
            while((id=byte(c))>0) {
                uint64_t size;
                if(number(c,&size)||size>c->size-c->pos||!take(c,(size_t)size)) return invalid();
            }
            if(id<0) return -1;
        } else return unsupported();
    }
    if(id<0 || !have_files || c->pos!=c->size || (!count && reader->folder.present)) return invalid();
    if(count && !reader->entry.path) {reader->entry.path=strdup("file");if(!reader->entry.path)return -1;}
    reader->valid=count!=0;
    reader->entry.size=reader->folder.unpacked;
    return 0;
}

ArcReader *arc_7z_open(ArcStream *stream) { return arc_7z_open_ex(stream,arc_default_limits()); }
ArcReader *arc_7z_open_ex(ArcStream *stream,const ArcLimits *limits) {
    if(!stream) {invalid();return NULL;}
    uint8_t signature[32];
    if(arc_stream_seek(stream,0,SEEK_END)) return NULL;
    int64_t length=arc_stream_tell(stream);
    if(length<32 || arc_stream_seek(stream,0,SEEK_SET) ||
       arc_stream_read_fully(stream,signature,sizeof(signature))!=(ssize_t)sizeof(signature)) return NULL;
    if(memcmp(signature,"7z\xbc\xaf\x27\x1c",6) || signature[6]!=0 ||
       crc32(0,signature+12,20)!=le32(signature+8)) {invalid();return NULL;}
    uint64_t offset=le64(signature+12),size=le64(signature+20);
    if(offset>(uint64_t)length-32 || size>(uint64_t)length-32-offset || size>HEADER_LIMIT) {invalid();return NULL;}
    SevenZReader *reader=calloc(1,sizeof(*reader));
    if(!reader) return NULL;
    reader->base.format=3;reader->base.stream=stream;
    arc_base_set_limits(&reader->base,limits);
    reader->entry.mode=0644;
    if(!size) return (ArcReader *)reader;
    uint8_t *buffer=malloc((size_t)size);
    if(!buffer) {free(reader);return NULL;}
    if(arc_stream_seek(stream,(int64_t)offset+32,SEEK_SET) ||
       arc_stream_read_fully(stream,buffer,(size_t)size)!=(ssize_t)size ||
       crc32(0,buffer,(uInt)size)!=le32(signature+28)) goto fail;
    if(buffer[0]==ENCODED_HEADER) {
        Cursor encoded={buffer,(size_t)size,1};
        Folder folder={0};
        if(streams(&encoded,&folder) || encoded.pos!=encoded.size || folder.unpacked>HEADER_LIMIT) goto fail;
        ArcStream *data=open_folder(stream,&folder,reader->base.limits,(uint64_t)length);
        if(!data) goto fail;
        uint8_t *decoded=malloc(folder.unpacked ? (size_t)folder.unpacked : 1);
        if(!decoded) {arc_stream_close(data);goto fail;}
        size_t total=0;
        ssize_t n=0;
        while(total<folder.unpacked && (n=arc_stream_read_fully(data,decoded+total,(size_t)folder.unpacked-total))>0) total+=(size_t)n;
        uint8_t extra;
        bool ok=total==folder.unpacked && n>=0 && arc_stream_read_fully(data,&extra,1)==0;
        arc_stream_close(data);
        if(!ok) {free(decoded);goto fail;}
        free(buffer);buffer=decoded;size=folder.unpacked;
    }
    Cursor header={buffer,(size_t)size,0};
    if(parse_header(&header,reader)) goto fail;
    if(reader->folder.present && (reader->folder.offset>(uint64_t)length-32 ||
       reader->folder.packed>(uint64_t)length-32-reader->folder.offset ||
       reader->folder.unpacked>reader->base.limits->max_uncompressed_bytes)) goto fail;
    free(buffer);return (ArcReader *)reader;
fail:
    free(buffer);arc_entry_free(&reader->entry);free(reader);
    if(!errno) errno=EINVAL;
    return NULL;
}
int arc_7z_next(ArcReader *reader,ArcEntry *entry) {
    if(!reader||!entry)return invalid();
    SevenZReader *seven=(SevenZReader *)reader;
    if(!seven->valid||seven->returned)return 1;
    *entry=seven->entry;entry->path=strdup(seven->entry.path);entry->link_target=NULL;
    if(!entry->path)return -1;
    seven->returned=true;return 0;
}
ArcStream *arc_7z_open_data(ArcReader *reader) {
    if(!reader)return NULL;
    SevenZReader *seven=(SevenZReader *)reader;
    if(!seven->valid||seven->entry.type!=ARC_ENTRY_FILE)return NULL;
    if(!seven->folder.present)return arc_stream_from_memory("",0,0);
    if(arc_stream_seek(seven->base.stream,0,SEEK_END))return NULL;
    int64_t size=arc_stream_tell(seven->base.stream);
    if(size<0)return NULL;
    return open_folder(seven->base.stream,&seven->folder,seven->base.limits,(uint64_t)size);
}
int arc_7z_skip_data(ArcReader *reader) {
    if(!reader)return invalid();
    ((SevenZReader *)reader)->returned=true;return 0;
}
void arc_7z_close(ArcReader *reader) {
    if(!reader)return;
    SevenZReader *seven=(SevenZReader *)reader;
    arc_entry_free(&seven->entry);arc_stream_close(seven->base.stream);
    if(seven->base.owned_stream && seven->base.owned_stream!=seven->base.stream)arc_stream_close(seven->base.owned_stream);
    free(seven);
}
