#include "cupidmime.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void type(const char *text, const char *mime, unsigned flags) {
    size_t size = strlen(text), i;
    cm_result result;
    assert(cm_detect(text, size, &result) == CM_OK);
    if (strcmp(result.mime, mime) || result.flags != flags) {
        fprintf(stderr, "%s\nexpected %s flags %u; got %s flags %u\n", text, mime, flags,
                result.mime, result.flags);
        abort();
    }
    /* Exercise every boundary with separately allocated, exact-size buffers. */
    for (i = 0; i < size; i++) {
        char *prefix = malloc(i ? i : 1);
        assert(prefix);
        memcpy(prefix, text, i);
        assert(cm_detect(prefix, i, &result) == CM_OK);
        assert(cm_detect_prefix(prefix, i, &result) == CM_OK);
        assert(result.flags & CM_PARTIAL);
        free(prefix);
    }
}

int main(void) {
    type("<svg/>", "image/svg+xml", 0);
    type(" \t<svg viewBox='0 0 1 1'></svg>", "image/svg+xml", 0);
    type("<?xml version='1.0'?>\n<svg xmlns='http://www.w3.org/2000/svg'/>", "image/svg+xml", 0);
    type("\xef\xbb\xbf<!-- <html> --><?xml-stylesheet href='x'?>\n<svg/>", "image/svg+xml", 0);
    type("<q:svg xmlns:q='http://www.w3.org/2000/svg'/>", "image/svg+xml", 0);
    type("<svg:q xmlns:svg='http://www.w3.org/2000/svg'/>", "application/xml", CM_AMBIGUOUS);
    type("<q:svg xmlns='http://www.w3.org/2000/svg'/>", "application/xml", CM_AMBIGUOUS);
    type("<svg xmlns='urn:unrelated'/>", "application/xml", CM_AMBIGUOUS);
    type("<svg xmlns=''/>", "application/xml", CM_AMBIGUOUS);
    type("<svg xmlns='http://www.w3.org/2000/svg' xmlns='urn:bad'/>", "application/xml",
         CM_AMBIGUOUS | CM_MALFORMED);
    type("<svg xmlns='urn:bad' xmlns='http://www.w3.org/2000/svg'/>", "application/xml",
         CM_AMBIGUOUS | CM_MALFORMED);
    type("<svg note=\"xmlns='urn:bad' >\" xmlns='http://www.w3.org/2000/svg'/>", "image/svg+xml",
         0);
    type("<svg xmlns='http://www.w3.org/2000/svg-more'/>", "application/xml", CM_AMBIGUOUS);
    type("<svg xmlns='http://www.w3.org/2000/sv&#x67;'/>", "application/xml", CM_AMBIGUOUS);
    type("<configuration xmlns='urn:example'/>", "application/xml", CM_AMBIGUOUS);
    type("<configuration/>", "text/plain", 0);
    type("<svgish/>", "text/plain", 0);
    type("<SVG/>", "text/plain", 0);
    type("<svg", "text/plain", 0);
    type("<svg title='unfinished", "text/plain", 0);
    type("<!-- <svg/> -->plain", "text/plain", 0);
    type("<document value='<svg/>'/>", "text/plain", 0);
    type("<?xml version='1.0'?><document><svg/></document>", "application/xml", CM_AMBIGUOUS);
    type("<?xml version='1.0'?><svg title='", "application/xml", CM_AMBIGUOUS | CM_TRUNCATED);
    type("<?xml version='1.0'", "application/xml", CM_AMBIGUOUS | CM_TRUNCATED);
    type("<?xml version='1.0'?><!-- unfinished <svg/>", "application/xml",
         CM_AMBIGUOUS | CM_TRUNCATED);
    type("<?xml version='1.0'?><!DOCTYPE svg [<!-- ] > <fake/> -->"
         "<!ENTITY token '<not-a-root/>'>]><svg/>",
         "image/svg+xml", 0);
    type("<!DOCTYPE svg PUBLIC '-//Example//DTD SVG 1.1//EN' 'https://invalid.example/no-read'>"
         "<svg xmlns='http://www.w3.org/2000/svg'/>",
         "image/svg+xml", 0);
    type("<!DOCTYPE svg [<!ENTITY token '<svg/>'>]><other/>", "text/plain", 0);
    type("<!-- comment --><HTML lang=en>", "text/html", 0);
    type("<htmlish>", "text/plain", 0);
    type("<!DoCtYpE HtMl><html>", "text/html", 0);
    type("<html xmlns='http://www.w3.org/1999/xhtml'/>", "application/xhtml+xml", 0);
    type("<?xml version='1.0'?><h:html xmlns:h='http://www.w3.org/1999/xhtml'/>",
         "application/xhtml+xml", 0);
    type("<html xmlns='urn:other'/>", "application/xml", CM_AMBIGUOUS);
    type("<feed xmlns='http://www.w3.org/2005/Atom'/>", "application/atom+xml", 0);
    type("<a:entry xmlns:a='http://www.w3.org/2005/Atom'/>", "application/atom+xml", 0);
    type("<feed xmlns='http://www.w3.org/2005/Atom-bad'/>", "application/xml", CM_AMBIGUOUS);
    type("<rss version='2.0'/>", "application/rss+xml", 0);
    type("<rss version='2.0-extra'/>", "application/xml", CM_AMBIGUOUS);
    type("<?xml version='1.0'?>", "application/xml", CM_AMBIGUOUS);
    type("<?xmlish><svg/>", "text/plain", 0);
    puts("Markup roots, namespaces, prologs, false positives, and all prefix boundaries passed");
    return 0;
}
