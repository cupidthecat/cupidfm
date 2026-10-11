#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#define _POSIX_C_SOURCE 200809L
#include "../src/fs/files.c"
#include <assert.h>
volatile sig_atomic_t is_editing = 1;
char g_editor_path[MAX_PATH_LENGTH];
static int notifications;
void plugins_notify_editor_save(PluginManager *pm, const char *path) { (void)pm; (void)path; ++notifications; }
void plugins_request_reload(PluginManager *pm) { (void)pm; }
void plugins_request_select(PluginManager *pm, const char *s) { (void)pm; (void)s; }
enum { FAIL_NONE, FAIL_WRITE, FAIL_FLUSH, FAIL_SYNC, FAIL_CLOSE, FAIL_RENAME, FAIL_OWNER, FAIL_MODE, FAIL_XATTR, RACE_NEW };
static long fail_after = -1;
static int allocation_failed;
static int allocation_fails(void) {
    if (fail_after < 0) return 0;
    if (fail_after-- == 0) { fail_after = -1; allocation_failed = 1; return 1; }
    return 0;
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n) { return allocation_fails() ? NULL : __real_malloc(n); }
static int failure;
static int hit(int stage) { if (failure != stage) return 0; failure = 0; errno = EIO; return 1; }
int __real_fputs(const char *, FILE *);
int __wrap_fputs(const char *s, FILE *f) { if (hit(FAIL_WRITE)) { __real_fputs("partial", f); return EOF; } return __real_fputs(s,f); }
int __real_fflush(FILE *);
int __wrap_fflush(FILE *f) { int n = __real_fflush(f); return hit(FAIL_FLUSH) ? EOF : n; }
int __real_fsync(int);
int __wrap_fsync(int fd) { return hit(FAIL_SYNC) ? -1 : __real_fsync(fd); }
int __real_fclose(FILE *);
int __wrap_fclose(FILE *f) { int n = __real_fclose(f); return hit(FAIL_CLOSE) ? EOF : n; }
int __real_rename(const char *, const char *);
int __wrap_rename(const char *a,const char *b) { return hit(FAIL_RENAME) ? -1 : __real_rename(a,b); }
int __real_fchown(int,uid_t,gid_t);
int __wrap_fchown(int fd,uid_t u,gid_t g) { return hit(FAIL_OWNER) ? -1 : __real_fchown(fd,u,g); }
int __real_fchmod(int,mode_t);
int __wrap_fchmod(int fd,mode_t m) { return hit(FAIL_MODE) ? -1 : __real_fchmod(fd,m); }
int __real_fsetxattr(int,const char*,const void*,size_t,int);
int __wrap_fsetxattr(int fd,const char*n,const void*v,size_t s,int f) { return hit(FAIL_XATTR) ? -1 : __real_fsetxattr(fd,n,v,s,f); }
int __real_mkstemp(char *);
int __wrap_mkstemp(char *path) {
    int fd = __real_mkstemp(path);
    if (fd >= 0) __real_fsetxattr(fd, "user.cupidfm-inherited", "extra", 5, 0);
    return fd;
}
int __real_link(const char *,const char *);
int __wrap_link(const char *a,const char *b) {
    if (hit(RACE_NEW)) { FILE *f = fopen(b,"w"); assert(f); assert(fputs("racer",f)>=0); assert(fclose(f)==0); }
    return __real_link(a,b);
}
static void put(const char *path,const char *text) { FILE *f=fopen(path,"w"); assert(f); assert(fputs(text,f)>=0); assert(fclose(f)==0); }
static void expect(const char *path,const char *text) { FILE *f=fopen(path,"r"); assert(f); char bytes[256]; size_t n=fread(bytes,1,sizeof(bytes),f); assert(n==strlen(text)&&memcmp(bytes,text,n)==0); fclose(f); }
static void no_temps(const char *directory) { DIR *d=opendir(directory); assert(d); struct dirent *e; while ((e=readdir(d))) assert(strncmp(e->d_name,".cupidfm-save-",14)!=0); closedir(d); }
int main(void) {
    char dir[]="/tmp/cupidfm-atomic-XXXXXX"; assert(mkdtemp(dir));
    char target[MAX_PATH_LENGTH], alias[MAX_PATH_LENGTH], other[MAX_PATH_LENGTH];
    snprintf(target,sizeof(target),"%s/file",dir); snprintf(alias,sizeof(alias),"%s/alias",dir); snprintf(other,sizeof(other),"%s/new",dir);
    TextBuffer b={0}; FILE *input=tmpfile(); assert(input); fputs("new\r\nlast",input); rewind(input); assert(editor_load_stream(input,&b)); fclose(input); g_editor_buffer=&b;
    int attribute_supported=0;
    for (int stage=FAIL_WRITE;stage<=FAIL_XATTR;++stage) {
        put(target,"original"); assert(chmod(target,0640)==0);
        attribute_supported=setxattr(target,"user.cupidfm-test","value",5,0)==0;
        if (stage==FAIL_XATTR&&!attribute_supported) continue;
        struct stat before,after; assert(stat(target,&before)==0);
        strcpy(g_editor_path,target); g_editor_dirty=true; g_editor_dirty_clear_requested=false;
        failure=stage; assert(!editor_save_current((PluginManager *)&b)); assert(failure==0);
        assert(g_editor_dirty&&!g_editor_dirty_clear_requested&&!notifications); expect(target,"original");
        assert(stat(target,&after)==0&&before.st_ino==after.st_ino&&(after.st_mode&0777)==0640); no_temps(dir);
    }
    int failed_allocations = 0, successful_allocations = 0;
    for (int point = 0; point < 12; ++point) {
        put(target, "original");
        allocation_failed = 0; fail_after = point;
        bool ok = editor_save_buffer_to_path(target, &b);
        fail_after = -1;
        if (allocation_failed) {
            ++failed_allocations; assert(!ok); expect(target, "original");
        } else {
            ++successful_allocations; assert(ok); expect(target, "new\r\nlast");
        }
        no_temps(dir);
    }
    assert(successful_allocations && (!attribute_supported || failed_allocations));
    assert(editor_save_current((PluginManager *)&b)); expect(target,"new\r\nlast"); assert(notifications==1&&!g_editor_dirty);
    struct stat st; assert(stat(target,&st)==0&&(st.st_mode&0777)==0640);
    assert(getxattr(target,"user.cupidfm-inherited",NULL,0) < 0);
    if(attribute_supported) { char value[16]; assert(getxattr(target,"user.cupidfm-test",value,sizeof(value))==5&&!memcmp(value,"value",5)); }
    assert(symlink(target,alias)==0); put(target,"old"); assert(editor_save_buffer_to_path(alias,&b)); expect(target,"new\r\nlast"); assert(lstat(alias,&st)==0&&S_ISLNK(st.st_mode)); unlink(alias);
    assert(link(target,alias)==0); assert(!editor_save_buffer_to_path(target,&b)); assert(stat(target,&st)==0&&st.st_nlink==2); unlink(alias);
    assert(chmod(target,0444)==0); if (geteuid()!=0) assert(!editor_save_buffer_to_path(target,&b)); assert(chmod(target,0640)==0);
    assert(symlink(other,alias)==0); assert(!editor_save_buffer_to_path(alias,&b)); assert(access(other,F_OK)!=0); unlink(alias);
    assert(!editor_save_buffer_to_path(dir,&b));
    failure=RACE_NEW; assert(!editor_save_buffer_to_path(other,&b)); assert(failure==0); expect(other,"racer"); unlink(other); no_temps(dir);
    strcpy(g_editor_path, target);
    g_editor_dirty = true; g_editor_dirty_clear_requested = false;
    failure = FAIL_FLUSH;
    assert(!editor_save_as(NULL, other));
    assert(!strcmp(g_editor_path, target) && g_editor_dirty && !g_editor_dirty_clear_requested);
    assert(access(other, F_OK) != 0); no_temps(dir);
    assert(editor_save_as(NULL, other));
    assert(!strcmp(g_editor_path, other) && !g_editor_dirty);
    expect(other,"new\r\nlast"); assert(stat(other,&st)==0&&(st.st_mode&0777)==0600); no_temps(dir);
    for (int i = 0; i < b.num_lines; ++i) free(b.lines[i]);
    free(b.lines);
    free(b.eols);
    unlink(target); unlink(other); rmdir(dir); puts("Editor atomic save checks passed");
}
