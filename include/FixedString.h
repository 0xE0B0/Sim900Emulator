#pragma once

#include <Arduino.h>
#include <cstring>

template <size_t N>
struct FixedString {
    char buf[N];
    size_t len;
    FixedString() : len(0) { buf[0] = '\0'; }
    FixedString(const char* s) { set(s); }
    void set(const char* s) {
        if (!s) { len = 0; buf[0] = '\0'; return; }
        strncpy(buf, s, N - 1);
        buf[N - 1] = '\0';
        len = strnlen(buf, N - 1);
    }
    // append a C string, truncating if needed
    void append(const char* s) {
        if (!s) return;
        size_t avail = (N - 1) - len;
        if (avail == 0) return;
        size_t toCopy = strnlen(s, avail);
        memcpy(buf + len, s, toCopy);
        len += toCopy;
        buf[len] = '\0';
    }
    // append a single character
    void append(char c) {
        if (len < N - 1) {
            buf[len++] = c;
            buf[len] = '\0';
        }
    }
    const char* c_str() const { return buf; }
    size_t size() const { return len; }
    void clear() { len = 0; buf[0] = '\0'; }
    operator const char*() const { return buf; }
    void reserve(size_t) {}  // no-op for fixed buffer
    size_t length() const { return len; }
    size_t capacity() const { return N - 1; }
    bool startsWith(const char* s) const {
        if (!s) return false;
        return strncmp(buf, s, strlen(s)) == 0;
    }
    // remove count characters starting at pos
    void remove(size_t pos, size_t count) {
        if (pos >= len) return;
        size_t tail = len - (pos + count);
        if ((int)tail > 0) memmove(buf + pos, buf + pos + count, tail);
        len = pos + tail;
        buf[len] = '\0';
    }
    // trim whitespace from both ends
    void trim() {
        size_t start = 0;
        while (start < len && isspace((unsigned char)buf[start])) ++start;
        size_t end = len;
        while (end > start && isspace((unsigned char)buf[end-1])) --end;
        if (start == 0 && end == len) return;
        size_t newLen = end - start;
        if (newLen > 0) memmove(buf, buf + start, newLen);
        len = newLen;
        buf[len] = '\0';
    }
    // append helpers
    FixedString& operator+=(char c) { append(c); return *this; }
    FixedString& operator+=(const char* s) { append(s); return *this; }
};

using FixedString128 = FixedString<128>;
using FixedString160 = FixedString<160>;
