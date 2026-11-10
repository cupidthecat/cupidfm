#include "markup.h"

#include <string.h>

typedef unsigned char byte;

typedef struct {
    const byte *data;
    size_t size, at;
} Markup;

static int space(byte c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void whitespace(Markup *m) {
    while (m->at < m->size && space(m->data[m->at]))
        m->at++;
}

static int starts(const Markup *m, const char *text) {
    size_t length = strlen(text);
    return length <= m->size - m->at && !memcmp(m->data + m->at, text, length);
}

static int equal(const byte *data, size_t size, const char *text) {
    return size == strlen(text) && !memcmp(data, text, size);
}

static int ci_token(const Markup *m, const char *token) {
    size_t i, length = strlen(token);
    if (length >= m->size - m->at)
        return 0;
    for (i = 0; i < length; i++) {
        byte c = m->data[m->at + i];
        if (c >= 'A' && c <= 'Z')
            c = (byte)(c + ('a' - 'A'));
        if (c != (byte)token[i])
            return 0;
    }
    return space(m->data[m->at + length]) || m->data[m->at + length] == '>';
}

static int through(Markup *m, const char *end) {
    size_t length = strlen(end);
    while (length <= m->size - m->at) {
        if (starts(m, end)) {
            m->at += length;
            return 1;
        }
        m->at++;
    }
    m->at = m->size;
    return 0;
}

/* Quotes, internal subsets and comments can contain '>' without ending a DTD.
 * No declaration is evaluated, no entity is expanded, and no resource is read. */
static int doctype(Markup *m) {
    size_t depth = 0;
    byte quote = 0;
    m->at += 9;
    for (; m->at < m->size; m->at++) {
        byte c = m->data[m->at];
        if (quote) {
            if (c == quote)
                quote = 0;
        } else if (starts(m, "<!--")) {
            m->at += 4;
            if (!through(m, "-->"))
                return 0;
            m->at--;
        } else if (c == '\'' || c == '"')
            quote = c;
        else if (c == '[')
            depth++;
        else if (c == ']') {
            if (!depth)
                return 0;
            depth--;
        } else if (c == '>' && !depth) {
            m->at++;
            return 1;
        }
    }
    return 0;
}

static int name_start(byte c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == ':';
}

static int name_char(byte c) {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static size_t name(Markup *m) {
    size_t start = m->at;
    if (m->at < m->size && name_start(m->data[m->at])) {
        m->at++;
        while (m->at < m->size && name_char(m->data[m->at]))
            m->at++;
    }
    return m->at - start;
}

static void identify(cm_result *r, const char *mime, const char *why, int ambiguous) {
    r->mime = mime;
    r->reason = why;
    r->evidence = CM_SIGNATURE;
    if (ambiguous)
        r->flags |= CM_AMBIGUOUS;
    else
        r->flags &= ~(unsigned)CM_AMBIGUOUS;
}

/* Only the root's own namespace binding is relevant; attribute contents and
 * declarations containing a familiar tag name cannot establish a subtype. */
static void root(Markup *m, cm_result *r, int xml) {
    size_t start, length, prefix = 0, local, i;
    int binding = -1, rss_version = 0;
    int svg, html, atom, rss;
    if (m->at == m->size || m->data[m->at++] != '<')
        return;
    start = m->at;
    length = name(m);
    if (!length)
        return;
    local = start;
    for (i = start; i < start + length; i++)
        if (m->data[i] == ':') {
            if (prefix || i == start || i + 1 == start + length)
                return;
            prefix = i - start;
            local = i + 1;
        }
    svg = equal(m->data + local, start + length - local, "svg");
    html = equal(m->data + local, start + length - local, "html");
    atom = equal(m->data + local, start + length - local, "feed") ||
           equal(m->data + local, start + length - local, "entry");
    rss = equal(m->data + local, start + length - local, "rss");
    for (;;) {
        size_t before = m->at, attr, attr_length, value, value_length;
        byte quote;
        whitespace(m);
        if (m->at == m->size)
            break;
        if (m->data[m->at] == '>' || starts(m, "/>")) {
            if (binding >= 0 || xml || svg || atom || rss)
                identify(r, "application/xml", "XML root; subtype unresolved", 1);
            if (svg && ((!prefix && binding < 0) || binding == 1))
                identify(r, "image/svg+xml", "SVG root and supported namespace binding", 0);
            else if (html && binding == 2)
                identify(r, "application/xhtml+xml", "HTML root in the XHTML namespace", 0);
            else if (atom && binding == 3)
                identify(r, "application/atom+xml", "Atom feed or entry root and namespace", 0);
            else if (rss && !prefix && binding < 0 && rss_version)
                identify(r, "application/rss+xml", "RSS root with version 2.0", 0);
            else if (html && !xml && !prefix && binding < 0)
                identify(r, "text/html", "HTML root without an XML namespace", 0);
            return;
        }
        if (m->at == before)
            return; /* Attributes must be separated by whitespace. */
        attr = m->at;
        attr_length = name(m);
        if (!attr_length)
            return;
        whitespace(m);
        if (m->at == m->size || m->data[m->at++] != '=')
            return;
        whitespace(m);
        if (m->at == m->size)
            break;
        quote = m->data[m->at++];
        if (quote != '\'' && quote != '"')
            return;
        value = m->at;
        while (m->at < m->size && m->data[m->at] != quote) {
            if (m->data[m->at] == '<')
                return;
            m->at++;
        }
        if (m->at == m->size)
            break;
        value_length = m->at++ - value;
        if ((!prefix && equal(m->data + attr, attr_length, "xmlns")) ||
            (prefix && attr_length == prefix + 6 && !memcmp(m->data + attr, "xmlns:", 6) &&
             !memcmp(m->data + attr + 6, m->data + start, prefix))) {
            if (binding >= 0) {
                identify(r, "application/xml", "Duplicate root namespace declaration", 1);
                r->flags |= CM_MALFORMED;
                return;
            }
            binding = equal(m->data + value, value_length, "http://www.w3.org/2000/svg")     ? 1
                      : equal(m->data + value, value_length, "http://www.w3.org/1999/xhtml") ? 2
                      : equal(m->data + value, value_length, "http://www.w3.org/2005/Atom")  ? 3
                                                                                             : 0;
        }
        if (rss && equal(m->data + attr, attr_length, "version"))
            rss_version = equal(m->data + value, value_length, "2.0");
    }
    /* A recognized XML declaration remains useful even when the root is cut. */
    if (xml) {
        r->flags |= CM_TRUNCATED;
        r->reason = "Incomplete XML root start tag";
    }
}

void cm_markup_detect(const byte *data, size_t size, cm_result *r) {
    Markup m = {data, size, 0};
    int xml = 0;
    whitespace(&m);
    if (starts(&m, "<?xml") && size - m.at > 5 && space(data[m.at + 5])) {
        xml = 1;
        identify(r, "application/xml", "XML declaration; subtype unresolved", 1);
        m.at += 5;
        if (!through(&m, "?>")) {
            r->flags |= CM_TRUNCATED;
            return;
        }
    }
    for (;;) {
        whitespace(&m);
        if (starts(&m, "<!--")) {
            m.at += 4;
            if (!through(&m, "-->"))
                break;
        } else if (starts(&m, "<?")) {
            m.at += 2;
            if (!through(&m, "?>"))
                break;
        } else if (!xml && ci_token(&m, "<!doctype html")) {
            identify(r, "text/html", "HTML document type token", 0);
            return;
        } else if (starts(&m, "<!DOCTYPE") && size - m.at > 9 && space(data[m.at + 9])) {
            if (!doctype(&m))
                break;
        } else {
            static const char *tokens[] = {"<html", "<head", "<body", "<script", "<iframe"};
            size_t i;
            /* Inspect the HTML root first so its namespace can establish XHTML. */
            Markup leading = m;
            root(&m, r, xml);
            if (!strcmp(r->mime, "text/plain") && !xml) {
                for (i = 0; i < sizeof(tokens) / sizeof(tokens[0]); i++)
                    if (ci_token(&leading, tokens[i])) {
                        identify(r, "text/html", "HTML leading token with delimiter", 0);
                        return;
                    }
            }
            return;
        }
    }
    if (xml) {
        r->flags |= CM_TRUNCATED;
        r->reason = "Incomplete XML prolog";
    }
}
