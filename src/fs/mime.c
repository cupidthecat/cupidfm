// File: mime.c
// MIME type utilities and file emoji mapping
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "mime.h"

#include <string.h>
#include <strings.h>

// Supported MIME types
const char *supported_mime_types[] = {
    "text/plain",               // Plain text files
    "text/x-c",                 // C source files
    "application/json",         // JSON files
    "application/xml",          // XML files
    "text/x-shellscript",       // Shell scripts
    "text/x-python",            // Python source files (common)
    "text/x-script.python",     // Python source files (alternative)
    "text/x-java-source",       // Java source files
    "text/html",                // HTML files
    "text/css",                 // CSS files
    "text/x-c++src",            // C++ source files
    "application/x-yaml",       // YAML files
    "application/x-sh",         // Shell scripts
    "application/x-perl",       // Perl scripts
    "application/x-php",        // PHP scripts
    "text/x-rustsrc",           // Rust source files
    "text/x-go",                // Go source files
    "text/x-swift",             // Swift source files
    "text/x-kotlin",            // Kotlin source files
    "text/x-makefile",          // Makefile files
    "text/x-script.*",          // Generic scripting files
    "text/javascript",          // JavaScript files
    "application/javascript",   // Alternative JavaScript MIME type
    "application/x-javascript", // Another JavaScript MIME type
    "text/x-javascript",        // Legacy JavaScript MIME type
    "text/x-*",                 // Any text-based x- files
};

const size_t num_supported_mime_types = sizeof(supported_mime_types) / sizeof(supported_mime_types[0]);

static const char *emoji_from_extension(const char *ext) {
    if (!ext) return NULL;

    if (strcmp(ext, ".py") == 0) return "🐍";
    if (strcmp(ext, ".js") == 0) return "📜";
    if (strcmp(ext, ".html") == 0) return "🌐";
    if (strcmp(ext, ".css") == 0) return "🎨";
    if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return "📝";
    if (strcmp(ext, ".java") == 0) return "☕";
    if (strcmp(ext, ".sh") == 0) return "💻";
    if (strcmp(ext, ".rs") == 0) return "🦀";
    if (strcmp(ext, ".md") == 0) return "📘";
    if (strcmp(ext, ".csv") == 0) return "📊";
    if (strcmp(ext, ".pl") == 0) return "🐪";
    if (strcmp(ext, ".rb") == 0) return "💎";
    if (strcmp(ext, ".php") == 0) return "🐘";
    if (strcmp(ext, ".go") == 0) return "🐹";
    if (strcmp(ext, ".swift") == 0) return "🦅";
    if (strcmp(ext, ".kt") == 0) return "🎯";
    if (strcmp(ext, ".scala") == 0) return "⚡";
    if (strcmp(ext, ".hs") == 0) return "λ";
    if (strcmp(ext, ".lua") == 0) return "🌙";
    if (strcmp(ext, ".r") == 0) return "📊";
    if (strcmp(ext, ".json") == 0) return "🔣";
    if (strcmp(ext, ".xml") == 0) return "📑";
    if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return "📋";
    if (strcmp(ext, ".toml") == 0) return "⚙";
    if (strcmp(ext, ".ini") == 0) return "🔧";
    if (strcmp(ext, ".sql") == 0) return "🗄";
    if (strcmp(ext, ".png") == 0) return "🖼";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "📸";
    if (strcmp(ext, ".gif") == 0) return "🎭";
    if (strcmp(ext, ".svg") == 0) return "✨";
    if (strcmp(ext, ".bmp") == 0) return "🎨";
    if (strcmp(ext, ".ico") == 0) return "🎯";
    if (strcmp(ext, ".mp3") == 0) return "🎵";
    if (strcmp(ext, ".wav") == 0) return "🔊";
    if (strcmp(ext, ".flac") == 0) return "🎶";
    if (strcmp(ext, ".mp4") == 0) return "🎥";
    if (strcmp(ext, ".mkv") == 0) return "🎬";
    if (strcmp(ext, ".avi") == 0) return "📽";
    if (strcmp(ext, ".webm") == 0) return "▶";
    if (strcmp(ext, ".mov") == 0) return "🎦";
    if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".tar") == 0 ||
        strcmp(ext, ".gz") == 0 || strcmp(ext, ".rar") == 0 ||
        strcmp(ext, ".7z") == 0) return "📦";
    if (strcmp(ext, ".pdf") == 0) return "📕";
    if (strcmp(ext, ".doc") == 0 || strcmp(ext, ".docx") == 0) return "📝";
    if (strcmp(ext, ".xls") == 0 || strcmp(ext, ".xlsx") == 0) return "📊";
    if (strcmp(ext, ".ppt") == 0 || strcmp(ext, ".pptx") == 0) return "📊";
    if (strcmp(ext, ".epub") == 0) return "📚";
    if (strcmp(ext, ".ttf") == 0 || strcmp(ext, ".otf") == 0 ||
        strcmp(ext, ".woff") == 0 || strcmp(ext, ".woff2") == 0) return "🔤";

    return NULL;
}

const char* get_file_emoji(const char *mime_type, const char *filename) {
    const char *default_icon = "📄";
    const char *ext = (filename != NULL) ? strrchr(filename, '.') : NULL;

    if (mime_type == NULL) {
        const char *from_ext = emoji_from_extension(ext);
        return from_ext ? from_ext : default_icon;
    }

    if (strncmp(mime_type, "text/", 5) == 0) {
        if (strstr(mime_type, "python")) return "🐍";
        if (strstr(mime_type, "javascript")) return "📜";
        if (strstr(mime_type, "html")) return "🌐";
        if (strstr(mime_type, "css")) return "🎨";
        if (strstr(mime_type, "x-c")) return "📝";
        if (strstr(mime_type, "x-java")) return "☕";
        if (strstr(mime_type, "x-shellscript")) return "💻";
        if (strstr(mime_type, "x-rust")) return "🦀";
        if (strstr(mime_type, "markdown")) return "📘";
        if (strstr(mime_type, "csv")) return "📊";
        if (strstr(mime_type, "x-perl")) return "🐪";
        if (strstr(mime_type, "x-ruby")) return "💎";
        if (strstr(mime_type, "x-php")) return "🐘";
        if (strstr(mime_type, "x-go")) return "🐹";
        if (strstr(mime_type, "x-swift")) return "🦅";
        if (strstr(mime_type, "x-kotlin")) return "🎯";
        if (strstr(mime_type, "x-scala")) return "⚡";
        if (strstr(mime_type, "x-haskell")) return "λ";
        if (strstr(mime_type, "x-lua")) return "🌙";
        if (strstr(mime_type, "x-r")) return "📊";
        if (strstr(mime_type, "json")) return "🔣";
        if (strstr(mime_type, "xml")) return "📑";
        if (strstr(mime_type, "yaml")) return "📋";
        if (strstr(mime_type, "toml")) return "⚙";
        if (strstr(mime_type, "ini")) return "🔧";
        return "📄";
    }

    if (strcmp(mime_type, "text/plain") == 0) {
        const char *from_ext = emoji_from_extension(ext);
        return from_ext ? from_ext : "📄";
    }

    if (strncmp(mime_type, "image/", 6) == 0) {
        if (strstr(mime_type, "gif")) return "🎭";
        if (strstr(mime_type, "svg")) return "✨";
        if (strstr(mime_type, "png")) return "🖼";
        if (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg")) return "📸";
        if (strstr(mime_type, "webp")) return "🌅";
        if (strstr(mime_type, "tiff")) return "📷";
        if (strstr(mime_type, "bmp")) return "🎨";
        if (strstr(mime_type, "ico")) return "🎯";
        return "🖼";
    }

    if (strncmp(mime_type, "audio/", 6) == 0) {
        if (strstr(mime_type, "midi")) return "🎹";
        if (strstr(mime_type, "mp3")) return "🎵";
        if (strstr(mime_type, "wav")) return "🔊";
        if (strstr(mime_type, "ogg")) return "🎼";
        if (strstr(mime_type, "flac")) return "🎶";
        if (strstr(mime_type, "aac")) return "🔉";
        return "🎵";
    }

    if (strncmp(mime_type, "video/", 6) == 0) {
        if (strstr(mime_type, "mp4")) return "🎥";
        if (strstr(mime_type, "avi")) return "📽";
        if (strstr(mime_type, "mkv")) return "🎬";
        if (strstr(mime_type, "webm")) return "▶";
        if (strstr(mime_type, "mov")) return "🎦";
        if (strstr(mime_type, "wmv")) return "📹";
        return "🎞";
    }

    if (strncmp(mime_type, "application/", 12) == 0) {
        if (strstr(mime_type, "zip") || strstr(mime_type, "x-tar") ||
            strstr(mime_type, "x-rar") || strstr(mime_type, "x-7z") ||
            strstr(mime_type, "gzip") || strstr(mime_type, "x-bzip") ||
            strstr(mime_type, "x-xz") || strstr(mime_type, "x-compress")) {
            return "📦";
        }

        if (strstr(mime_type, "pdf")) return "📕";
        if (strstr(mime_type, "msword")) return "📝";
        if (strstr(mime_type, "vnd.ms-excel")) return "📊";
        if (strstr(mime_type, "vnd.ms-powerpoint")) return "📊";
        if (strstr(mime_type, "vnd.oasis.opendocument.text")) return "📃";
        if (strstr(mime_type, "rtf")) return "📄";
        if (strstr(mime_type, "epub")) return "📚";
        if (strstr(mime_type, "js")) return "📜";
        if (strstr(mime_type, "json")) return "🔣";
        if (strstr(mime_type, "xml")) return "📑";
        if (strstr(mime_type, "yaml")) return "📋";
        if (strstr(mime_type, "sql")) return "🗄";

        if (strstr(mime_type, "x-executable")) return "⚙";
        if (strstr(mime_type, "x-sharedlib")) return "🔧";
        if (strstr(mime_type, "x-object")) return "🔨";
        if (strstr(mime_type, "x-pie-executable")) return "🎯";
        if (strstr(mime_type, "x-dex")) return "🤖";
        if (strstr(mime_type, "java-archive")) return "☕";
        if (strstr(mime_type, "x-msdownload")) return "🪟";
    }

    if (strncmp(mime_type, "font/", 5) == 0) {
        return "🔤";
    }

    if (strstr(mime_type, "database") || strstr(mime_type, "sql")) {
        return "🗄";
    }

    if (strstr(mime_type, "x-git")) {
        return "📥";
    }

    if (strstr(mime_type, "x-x509-ca-cert")) {
        return "🔐";
    }

    const char *from_ext = emoji_from_extension(ext);
    return from_ext ? from_ext : default_icon;
}

bool is_archive_file(const char *filename) {
    if (!filename) return false;
    
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
    return (strcasecmp(ext, ".zip") == 0 ||
            strcasecmp(ext, ".tar") == 0 ||
            strcasecmp(ext, ".gz") == 0 ||
            strcasecmp(ext, ".tgz") == 0 ||
            strcasecmp(ext, ".bz2") == 0 ||
            strcasecmp(ext, ".xz") == 0 ||
            strcasecmp(ext, ".7z") == 0 ||
            strcasecmp(ext, ".rar") == 0);
}

bool is_supported_mime_type(const char *mime_type) {
    if (!mime_type) return false;
    
    for (size_t i = 0; i < num_supported_mime_types; i++) {
        if (strncmp(mime_type, supported_mime_types[i], strlen(supported_mime_types[i])) == 0) {
            return true;
        }
    }
    return false;
}
