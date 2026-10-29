#define _POSIX_C_SOURCE 200809L
#include "arc_7z.h"
#include "arc_base.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <lzma.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <unistd.h>

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
    size_t props_size, sub_count;
    uint64_t *sizes;
    uint32_t *crcs;
    bool *defined;
    uint32_t packed_crc, unpacked_crc;
    bool has_packed_crc, has_unpacked_crc;
} Folder;
typedef struct Streams { Folder *folders; size_t count; } Streams;
typedef struct FileRecord {
    ArcEntry entry;
    size_t folder;
    uint64_t offset;
    uint32_t crc;
    bool defined, has_mode;
} FileRecord;
typedef struct SevenZReader {
    ArcReaderBase base;
    Streams streams;
    FileRecord *files;
    size_t count, next, current, cached_folder;
    ArcStream *cache;
    uint64_t archive_size;
} SevenZReader;
static void streams_free(Streams *streams) {
    for(size_t i=0;i<streams->count;i++) {
        free(streams->folders[i].sizes);free(streams->folders[i].crcs);free(streams->folders[i].defined);
    }
    free(streams->folders);memset(streams,0,sizeof(*streams));
}
static void files_free(SevenZReader *reader) {
    for(size_t i=0;i<reader->count;i++)arc_entry_free(&reader->files[i].entry);
    free(reader->files);
}

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
static int definitions(Cursor *c,size_t count,const uint8_t **bits,bool *all) {
    int flag=byte(c);
    if(flag<0 || flag>1)return invalid();
    *all=flag!=0;*bits=NULL;
    if(!*all && count) { *bits=take(c,(count+7)/8);if(!*bits)return -1; }
    return 0;
}
static bool bit(const uint8_t *bits,size_t index) { return (bits[index/8]&(128>>(index%8)))!=0; }
static int digests(Cursor *c,size_t count,bool *defined,uint32_t *crcs) {
    const uint8_t *bits;bool all;
    if(definitions(c,count,&bits,&all))return -1;
    for(size_t i=0;i<count;i++) {
        defined[i]=all || bit(bits,i);
        if(defined[i]) {const uint8_t *p=take(c,4);if(!p)return -1;crcs[i]=le32(p);}
    }
    return 0;
}
static bool count_ok(uint64_t count,const ArcLimits *limits,size_t item_size) {
    return count<=limits->max_entries && count<=HEADER_LIMIT/item_size;
}
static int coder(Cursor *c,Folder *f) {
    uint64_t count;
    if(number(c,&count))return -1;
    if(count!=1)return unsupported();
    int flags=byte(c);
    if(flags<0 || !(flags&15) || (flags&15)>8 || (flags&0xc0))return invalid();
    const uint8_t *method=take(c,(size_t)(flags&15));if(!method)return -1;
    for(int i=0;i<(flags&15);i++)f->method=(f->method<<8)|method[i];
    if(flags&16) {
        uint64_t inputs,outputs;
        if(number(c,&inputs)||number(c,&outputs))return -1;
        if(inputs!=1 || outputs!=1)return unsupported();
    }
    if(flags&32) {
        if(number(c,&count))return -1;
        if(count>sizeof(f->props))return unsupported();
        const uint8_t *p=take(c,(size_t)count);if(!p)return -1;
        memcpy(f->props,p,(size_t)count);f->props_size=(size_t)count;
    }
    if((f->method==METHOD_COPY && f->props_size==0) ||
       (f->method==METHOD_LZMA && f->props_size==5) ||
       (f->method==METHOD_LZMA2 && f->props_size==1))return 0;
    return unsupported();
}
static int sub_arrays(Folder *f) {
    f->sizes=calloc(f->sub_count ? f->sub_count : 1,sizeof(*f->sizes));
    f->crcs=calloc(f->sub_count ? f->sub_count : 1,sizeof(*f->crcs));
    f->defined=calloc(f->sub_count ? f->sub_count : 1,sizeof(*f->defined));
    return f->sizes && f->crcs && f->defined ? 0 : -1;
}
static int substreams(Cursor *c,Streams *out,const ArcLimits *limits) {
    int id=byte(c);size_t total=0;
    if(id==NUM_SUBSTREAMS) {
        for(size_t i=0;i<out->count;i++) {
            uint64_t count;if(number(c,&count)||!count_ok(count,limits,sizeof(uint64_t)))return invalid();
            out->folders[i].sub_count=(size_t)count;
        }
        id=byte(c);
    }
    for(size_t i=0;i<out->count;i++) {
        Folder *f=&out->folders[i];
        if(f->sub_count>limits->max_entries-total || sub_arrays(f))return invalid();
        total+=f->sub_count;
        uint64_t sum=0;
        for(size_t j=0;j+1<f->sub_count;j++) {
            if(id!=SIZE || number(c,&f->sizes[j]) || f->sizes[j]>f->unpacked-sum)return invalid();
            sum+=f->sizes[j];
        }
        if(f->sub_count)f->sizes[f->sub_count-1]=f->unpacked-sum;
        else if(f->unpacked)return invalid();
        if(f->sub_count==1 && f->has_unpacked_crc) {f->defined[0]=true;f->crcs[0]=f->unpacked_crc;}
    }
    if(id==SIZE)id=byte(c);
    if(id==CRC) {
        size_t count=0;
        for(size_t i=0;i<out->count;i++) {
            Folder *f=&out->folders[i];if(f->sub_count!=1 || !f->has_unpacked_crc)count+=f->sub_count;
        }
        bool *defined=calloc(count ? count : 1,sizeof(bool));uint32_t *crcs=calloc(count ? count : 1,sizeof(uint32_t));
        if(!defined || !crcs) {free(defined);free(crcs);return -1;}
        if(digests(c,count,defined,crcs)) {free(defined);free(crcs);return -1;}
        size_t k=0;
        for(size_t i=0;i<out->count;i++) {
            Folder *f=&out->folders[i];
            if(f->sub_count==1 && f->has_unpacked_crc)continue;
            for(size_t j=0;j<f->sub_count;j++,k++) {f->defined[j]=defined[k];f->crcs[j]=crcs[k];}
        }
        free(defined);free(crcs);id=byte(c);
    }
    return id==END ? 0 : invalid();
}
static int streams(Cursor *c,Streams *out,const ArcLimits *limits) {
    uint64_t offset,count;
    uint64_t *packed=NULL;bool *defined=NULL;uint32_t *crcs=NULL;
    size_t packs=0;int result=-1,id=byte(c);
    if(id!=PACK || number(c,&offset)||number(c,&count)||!count || !count_ok(count,limits,sizeof(Folder)))goto fail;
    packs=(size_t)count;
    packed=calloc(packs,sizeof(*packed));defined=calloc(packs,sizeof(*defined));crcs=calloc(packs,sizeof(*crcs));
    if(!packed||!defined||!crcs)goto fail;
    if(byte(c)!=SIZE)goto fail;
    for(size_t i=0;i<packs;i++)if(number(c,&packed[i]))goto fail;
    id=byte(c);
    if(id==CRC) {if(digests(c,packs,defined,crcs))goto fail;id=byte(c);}
    if(id!=END || byte(c)!=UNPACK || byte(c)!=FOLDER || number(c,&count) || count!=packs || byte(c)!=0)goto fail;
    out->folders=calloc(packs,sizeof(Folder));if(!out->folders)goto fail;out->count=packs;
    for(size_t i=0;i<packs;i++) {
        Folder *f=&out->folders[i];f->offset=offset;f->packed=packed[i];f->packed_crc=crcs[i];f->has_packed_crc=defined[i];f->sub_count=1;
        if(packed[i]>UINT64_MAX-offset || coder(c,f))goto fail;
        offset+=packed[i];
    }
    if(byte(c)!=UNPACK_SIZE)goto fail;
    uint64_t total=0;
    for(size_t i=0;i<packs;i++) {
        Folder *f=&out->folders[i];
        if(number(c,&f->unpacked) || f->unpacked>limits->max_uncompressed_bytes-total ||
           (f->method==METHOD_COPY && f->packed!=f->unpacked))goto fail;
        total+=f->unpacked;
    }
    id=byte(c);
    if(id==CRC) {
        memset(defined,0,packs*sizeof(*defined));memset(crcs,0,packs*sizeof(*crcs));
        if(digests(c,packs,defined,crcs))goto fail;
        for(size_t i=0;i<packs;i++) {out->folders[i].has_unpacked_crc=defined[i];out->folders[i].unpacked_crc=crcs[i];}
        id=byte(c);
    }
    if(id!=END)goto fail;
    id=byte(c);
    if(id==SUBSTREAMS) {if(substreams(c,out,limits))goto fail;id=byte(c);}
    else {
        for(size_t i=0;i<packs;i++) {
            Folder *f=&out->folders[i];if(sub_arrays(f))goto fail;
            f->sizes[0]=f->unpacked;f->defined[0]=f->has_unpacked_crc;f->crcs[0]=f->unpacked_crc;
        }
    }
    if(id!=END)goto fail;
    result=0;
fail:
    free(packed);free(defined);free(crcs);
    if(result && !errno)errno=EINVAL;
    return result;
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
static int files(Cursor *c,SevenZReader *reader) {
    uint64_t count;
    if(number(c,&count)||!count_ok(count,reader->base.limits,sizeof(FileRecord)))return invalid();
    reader->files=calloc(count ? (size_t)count : 1,sizeof(*reader->files));
    bool *empty=calloc(count ? (size_t)count : 1,sizeof(bool)),*regular=calloc(count ? (size_t)count : 1,sizeof(bool));
    if(!reader->files || !empty || !regular) {free(empty);free(regular);return -1;}
    reader->count=(size_t)count;
    for(size_t i=0;i<reader->count;i++) {reader->files[i].entry.mode=0644;reader->files[i].folder=SIZE_MAX;}
    bool have_names=false,have_empty=false,have_regular=false;size_t empty_count=0;
    int id,result=-1;
    while((id=byte(c))>0) {
        uint64_t length;
        if(number(c,&length) || length>c->size-c->pos || length>reader->base.limits->max_extra)goto fail;
        const uint8_t *p=take(c,(size_t)length);if(!p)goto fail;
        if(id==NAME) {
            if(have_names || !length || p[0])goto fail;
            have_names=true;
            size_t pos=1;
            for(size_t i=0;i<reader->count;i++) {
                size_t first=pos;
                while(pos+1<length && (p[pos]||p[pos+1]))pos+=2;
                if(pos+1>=length)goto fail;
                pos+=2;reader->files[i].entry.path=name_utf8(p+first,pos-first,reader->base.limits->max_name);
                if(!reader->files[i].entry.path)goto fail;
            }
            if(pos!=length)goto fail;
        } else if(id==EMPTY_STREAM) {
            if(have_empty || length!=(count+7)/8)goto fail;
            have_empty=true;
            for(size_t i=0;i<reader->count;i++) {empty[i]=bit(p,i);if(empty[i])empty_count++;}
        } else if(id==EMPTY_FILE) {
            if(!have_empty || have_regular || length!=(empty_count+7)/8)goto fail;
            have_regular=true;
            size_t k=0;for(size_t i=0;i<reader->count;i++)if(empty[i])regular[i]=bit(p,k++);
        } else if(id==16) {
            if(!have_empty || length!=(empty_count+7)/8)goto fail;
            for(size_t i=0;i<empty_count;i++)if(bit(p,i)) {unsupported();goto fail;}
        }
        else if(id==ATTRIBUTES || id==MTIME || id==18 || id==19) {
            Cursor property={p,(size_t)length,0};const uint8_t *bits;bool all;
            if(definitions(&property,reader->count,&bits,&all) || byte(&property)!=0)goto fail;
            for(size_t i=0;i<reader->count;i++)if(all || bit(bits,i)) {
                const uint8_t *value=take(&property,id==ATTRIBUTES ? 4 : 8);if(!value)goto fail;
                if(id==ATTRIBUTES && (le32(value)>>16)) {
                    unsigned mode=le32(value)>>16,kind=mode&0170000;
                    if(kind && kind!=0100000 && kind!=0040000) {unsupported();goto fail;}
                    reader->files[i].entry.mode=mode&0777;reader->files[i].has_mode=true;
                }
                if(id==MTIME && le64(value)>=116444736000000000ULL)
                    reader->files[i].entry.mtime=(le64(value)-116444736000000000ULL)/10000000;
            }
            if(property.pos!=property.size)goto fail;
        }
    }
    if(id<0)goto fail;
    for(size_t i=0;i<reader->count;i++) {
        FileRecord *file=&reader->files[i];file->entry.type=empty[i]&&!regular[i] ? ARC_ENTRY_DIR : ARC_ENTRY_FILE;
        if(file->entry.type==ARC_ENTRY_DIR && !file->has_mode)file->entry.mode=0755;
        if(!file->entry.path) {
            char fallback[64];snprintf(fallback,sizeof(fallback),reader->count==1 ? "file" : "file-%zu",i+1);
            file->entry.path=strdup(fallback);if(!file->entry.path)goto fail;
        }
        /* A sentinel marks records that need a decoded substream. */
        if(!empty[i])file->folder=0;
    }
    result=0;
fail:
    free(empty);free(regular);return result ? invalid() : 0;
}
static int parse_header(Cursor *c,SevenZReader *reader) {
    if(byte(c)!=HEADER)return invalid();
    bool have_files=false;int id;
    while((id=byte(c))>0) {
        if(id==MAIN_STREAMS) {
            if(reader->streams.count || have_files || streams(c,&reader->streams,reader->base.limits))return -1;
        } else if(id==FILES) {
            if(have_files || files(c,reader))return -1;
            have_files=true;
        } else if(id==ARCHIVE_PROPERTIES) {
            while((id=byte(c))>0) {uint64_t size;if(number(c,&size)||size>c->size-c->pos||!take(c,(size_t)size))return invalid();}
            if(id<0)return -1;
        } else return unsupported();
    }
    if(id<0 || !have_files || c->pos!=c->size)return invalid();
    size_t folder=0,sub=0;uint64_t offset=0;
    for(size_t i=0;i<reader->count;i++) {
        FileRecord *file=&reader->files[i];if(file->folder==SIZE_MAX)continue;
        while(folder<reader->streams.count && sub==reader->streams.folders[folder].sub_count) {folder++;sub=0;offset=0;}
        if(folder==reader->streams.count)return invalid();
        Folder *f=&reader->streams.folders[folder];
        file->folder=folder;file->offset=offset;file->entry.size=f->sizes[sub];file->crc=f->crcs[sub];file->defined=f->defined[sub];
        offset+=file->entry.size;sub++;
    }
    while(folder<reader->streams.count && sub==reader->streams.folders[folder].sub_count) {folder++;sub=0;}
    return folder==reader->streams.count ? 0 : invalid();
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
    reader->current=SIZE_MAX;reader->cached_folder=SIZE_MAX;reader->archive_size=(uint64_t)length;
    if(!size) return (ArcReader *)reader;
    uint8_t *buffer=malloc((size_t)size);
    if(!buffer) {free(reader);return NULL;}
    if(arc_stream_seek(stream,(int64_t)offset+32,SEEK_SET) ||
       arc_stream_read_fully(stream,buffer,(size_t)size)!=(ssize_t)size ||
       crc32(0,buffer,(uInt)size)!=le32(signature+28)) goto fail;
    if(buffer[0]==ENCODED_HEADER) {
        Cursor encoded={buffer,(size_t)size,1};
        Streams encoded_streams={0};
        if(streams(&encoded,&encoded_streams,reader->base.limits) || encoded.pos!=encoded.size ||
           encoded_streams.count!=1 || encoded_streams.folders[0].sub_count!=1 || encoded_streams.folders[0].unpacked>HEADER_LIMIT) {
            streams_free(&encoded_streams);goto fail;
        }
        Folder *folder=&encoded_streams.folders[0];uint64_t unpacked=folder->unpacked;
        ArcStream *data=open_folder(stream,folder,reader->base.limits,(uint64_t)length);
        if(!data) {streams_free(&encoded_streams);goto fail;}
        uint8_t *decoded=malloc(unpacked ? (size_t)unpacked : 1);
        if(!decoded) {arc_stream_close(data);streams_free(&encoded_streams);goto fail;}
        size_t total=0;ssize_t n=0;
        while(total<unpacked && (n=arc_stream_read_fully(data,decoded+total,(size_t)unpacked-total))>0)total+=(size_t)n;
        uint8_t extra;bool ok=total==unpacked && n>=0 && arc_stream_read_fully(data,&extra,1)==0;
        arc_stream_close(data);streams_free(&encoded_streams);
        if(!ok) {free(decoded);goto fail;}
        free(buffer);buffer=decoded;size=unpacked;
    }
    Cursor header={buffer,(size_t)size,0};
    if(parse_header(&header,reader)) goto fail;
    for(size_t i=0;i<reader->streams.count;i++) {
        Folder *f=&reader->streams.folders[i];
        if(f->offset>(uint64_t)length-32 || f->packed>(uint64_t)length-32-f->offset)goto fail;
    }
    free(buffer);return (ArcReader *)reader;
fail:
    free(buffer);files_free(reader);streams_free(&reader->streams);free(reader);
    if(!errno) errno=EINVAL;
    return NULL;
}
int arc_7z_next(ArcReader *reader,ArcEntry *entry) {
    if(!reader||!entry)return invalid();
    SevenZReader *seven=(SevenZReader *)reader;
    if(seven->next==seven->count) {seven->current=SIZE_MAX;return 1;}
    *entry=seven->files[seven->next].entry;entry->path=strdup(entry->path);entry->link_target=NULL;
    if(!entry->path)return -1;
    seven->current=seven->next++;return 0;
}
static int cache_folder(SevenZReader *seven,size_t index) {
    if(seven->cache && seven->cached_folder==index)return 0;
    arc_stream_close(seven->cache);seven->cache=NULL;seven->cached_folder=SIZE_MAX;
    FILE *temporary=tmpfile();if(!temporary)return -1;
    Folder *f=&seven->streams.folders[index];
    ArcStream *data=open_folder(seven->base.stream,f,seven->base.limits,seven->archive_size);
    if(!data) {fclose(temporary);return -1;}
    uint8_t buffer[65536];ssize_t n;
    while((n=arc_stream_read(data,buffer,sizeof(buffer)))>0)
        if(fwrite(buffer,1,(size_t)n,temporary)!=(size_t)n) {n=-1;break;}
    arc_stream_close(data);
    if(n<0 || fflush(temporary) || fseek(temporary,0,SEEK_SET)) {fclose(temporary);return -1;}
    int fd=fcntl(fileno(temporary),F_DUPFD_CLOEXEC,0);
    int saved=errno;
    if(fclose(temporary) && fd>=0) {saved=errno;close(fd);fd=-1;}
    if(fd<0) {errno=saved;return -1;}
    seven->cache=arc_stream_from_fd(fd,0);
    if(!seven->cache) {close(fd);return -1;}
    seven->cached_folder=index;return 0;
}
ArcStream *arc_7z_open_data(ArcReader *reader) {
    if(!reader)return NULL;
    SevenZReader *seven=(SevenZReader *)reader;
    if(seven->current==SIZE_MAX)return NULL;
    FileRecord *file=&seven->files[seven->current];
    if(file->entry.type!=ARC_ENTRY_FILE)return NULL;
    if(file->folder==SIZE_MAX)return arc_stream_from_memory("",0,0);
    Folder *folder=&seven->streams.folders[file->folder];
    ArcStream *data;
    if(folder->sub_count==1)data=open_folder(seven->base.stream,folder,seven->base.limits,seven->archive_size);
    else {
        if(cache_folder(seven,file->folder))return NULL;
        data=arc_stream_substream(seven->cache,(int64_t)file->offset,(int64_t)file->entry.size);
    }
    return data ? checked(data,file->entry.size,file->defined,file->crc) : NULL;
}
int arc_7z_skip_data(ArcReader *reader) {return reader ? 0 : invalid();}
void arc_7z_close(ArcReader *reader) {
    if(!reader)return;
    SevenZReader *seven=(SevenZReader *)reader;
    files_free(seven);streams_free(&seven->streams);arc_stream_close(seven->cache);
    arc_stream_close(seven->base.stream);
    if(seven->base.owned_stream && seven->base.owned_stream!=seven->base.stream)arc_stream_close(seven->base.owned_stream);
    free(seven);
}
