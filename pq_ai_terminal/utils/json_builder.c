/**
 * @file json_builder.c
 * @brief 轻量级JSON构造器实现
 */

#include "json_builder.h"
#include <stdio.h>
#include <string.h>

static int g_json_pos = 0;
static int g_json_depth = 0;

static int append(char *buf, int cap, const char *s)
{
    int len = (int)strlen(s);
    if (g_json_pos + len >= cap - 1) return -1;
    strcpy(buf + g_json_pos, s);
    g_json_pos += len;
    return 0;
}

int json_begin(char *buf, int cap)
{
    g_json_pos = 0;
    g_json_depth = 0;
    if (cap > 0) {
        buf[0] = '\0';
        return append(buf, cap, "{");
    }
    return -1;
}

int json_add_string(char *buf, int cap, const char *key, const char *val, int is_last)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "\"%s\":\"%s\"%s", key, val, is_last ? "" : ",");
    return append(buf, cap, tmp);
}

int json_add_float(char *buf, int cap, const char *key, float val, int is_last)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":%.4f%s", key, val, is_last ? "" : ",");
    return append(buf, cap, tmp);
}

int json_add_int(char *buf, int cap, const char *key, int val, int is_last)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":%d%s", key, val, is_last ? "" : ",");
    return append(buf, cap, tmp);
}

int json_add_object_begin(char *buf, int cap, const char *key)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":{", key);
    g_json_depth++;
    return append(buf, cap, tmp);
}

int json_add_object_end(char *buf, int cap, int is_last)
{
    g_json_depth--;
    return append(buf, cap, is_last ? "}" : "},");
}

int json_add_array_begin(char *buf, int cap, const char *key)
{
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "\"%s\":[", key);
    g_json_depth++;
    return append(buf, cap, tmp);
}

int json_add_array_end(char *buf, int cap, int is_last)
{
    g_json_depth--;
    return append(buf, cap, is_last ? "]" : "],");
}

int json_end(char *buf, int cap)
{
    (void)cap;
    if (g_json_depth != 0) {
        return -1; /* 未闭合 */
    }
    return append(buf, cap, "}");
}
