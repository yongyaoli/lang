/* 标准库:日期与时间
 * 时间戳 = 从 1970-01-01 00:00:00 UTC 起的秒数(数字);所有日期时间按本地时区解释。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../interp.h"
#include "../l_hal.h"

#if L_WITH_STDLIB_TIME

/* 第 i 个参数是可选的时间戳,缺省为当前时间 */
static time_t ts_arg(Interp *in, const Node *at, const char *fn, int argc, Value *argv, int i) {
    if (argc <= i) return time(NULL);
    return (time_t)num_arg(in, at, fn, argv, i);
}

static struct tm *to_local(Interp *in, const Node *at, const char *fn, time_t t) {
    struct tm *tm = localtime(&t);
    if (!tm) rt_error(in, at, "%s() 的时间戳超出范围", fn);
    return tm;
}

/* 逐个处理 % 指令,其余字节原样复制。
 * 直接把整个格式串交给 strftime 时,Windows 的 C 运行库遇到中文(多字节字符)会整体失败。 */
static Value format_ts(Interp *in, const Node *at, const char *fn, time_t t, const char *fmt) {
    struct tm *tm = to_local(in, at, fn, t);
    size_t cap = strlen(fmt) * 8 + 64, len = 0;
    char *out = malloc(cap);
    for (const char *p = fmt; *p; p++) {
        if (*p == '%' && p[1]) {
            char spec[3] = {'%', p[1], '\0'};
            char piece[128];
            size_t n = strftime(piece, sizeof piece, spec, tm);
            if (len + n + 1 > cap) { cap = (len + n + 1) * 2; out = realloc(out, cap); }
            memcpy(out + len, piece, n);
            len += n;
            p++;
        } else {
            if (len + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[len++] = *p;
        }
    }
    out[len] = '\0';
    Str *res = str_new(in, out, len);
    free(out);
    return val_str(res);
}

static Value f_time(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_whole((double)time(NULL));   /* 2038 年后自动变 float */
}

static Value f_date(Interp *in, const Node *at, int argc, Value *argv) {
    return format_ts(in, at, "date", ts_arg(in, at, "date", argc, argv, 0), "%Y-%m-%d");
}

static Value f_datetime(Interp *in, const Node *at, int argc, Value *argv) {
    return format_ts(in, at, "datetime", ts_arg(in, at, "datetime", argc, argv, 0),
                     "%Y-%m-%d %H:%M:%S");
}

/* format_time(时间戳, 格式):格式同 C 的 strftime,如 "%Y年%m月%d日 %H:%M" */
static Value f_format_time(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    num_arg(in, at, "format_time", argv, 0);
    need_arg(in, at, "format_time", argv, 1, V_STR, "string");
    return format_ts(in, at, "format_time", (time_t)val_to_double(argv[0]), val_get_cstr(argv[1]));
}

/* localtime([时间戳]):拆成 map */
static Value f_localtime(Interp *in, const Node *at, int argc, Value *argv) {
    struct tm *tm = to_local(in, at, "localtime", ts_arg(in, at, "localtime", argc, argv, 0));
    Map *m = map_new(in);
    Value mval = val_map(m);
    gc_push_root(in, mval);

    static const char *field_names[] = {
        "year", "month", "day", "hour", "minute", "second", "weekday", "yearday"
    };
    double field_vals[] = {
        (double)(tm->tm_year + 1900),
        (double)(tm->tm_mon + 1),
        (double)(tm->tm_mday),
        (double)(tm->tm_hour),
        (double)(tm->tm_min),
        (double)(tm->tm_sec),
        (double)(tm->tm_wday),
        (double)(tm->tm_yday + 1)
    };

    for (int i = 0; i < 8; i++) {
        Str *k = str_new(in, field_names[i], strlen(field_names[i]));
        map_set(m, k, val_int((int32_t)field_vals[i]));
    }

    gc_pop_roots(in, 1);
    return mval;
}

/* 由年月日时分秒得到时间戳;日期不存在(如 2 月 30 日)或超出系统支持范围返回 nil */
static Value make_ts(int y, int mo, int d, int h, int mi, int s) {
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 ||
        s < 0 || s > 59)
        return val_nil();
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);
    if (t == (time_t)-1 || tm.tm_mday != d || tm.tm_mon != mo - 1) return val_nil();
    return val_whole((double)t);
}

/* make_time(年, 月, 日, [时, 分, 秒]) */
static Value f_make_time(Interp *in, const Node *at, int argc, Value *argv) {
    int v[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < argc; i++) v[i] = int_arg(in, at, "make_time", argv, i);
    if (argc < 3) rt_error(in, at, "make_time() 至少需要 年、月、日 三个参数");
    return make_ts(v[0], v[1], v[2], v[3], v[4], v[5]);
}

/* parse_time("2026-11-06") 或 parse_time("2026-11-06 14:30:05"),格式不对返回 nil */
static Value f_parse_time(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    need_arg(in, at, "parse_time", argv, 0, V_STR, "string");
    const char *s = val_get_cstr(argv[0]);
    int y, mo, d, h, mi, sec, pos = 0;
    if (sscanf(s, "%d-%d-%d%n", &y, &mo, &d, &pos) == 3 && s[pos] == '\0')
        return make_ts(y, mo, d, 0, 0, 0);
    pos = 0;
    if (sscanf(s, "%d-%d-%d %d:%d:%d%n", &y, &mo, &d, &h, &mi, &sec, &pos) == 6 && s[pos] == '\0')
        return make_ts(y, mo, d, h, mi, sec);
    return val_nil();
}

static Value f_sleep(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    double sec = num_arg(in, at, "sleep", argv, 0);
    if (sec < 0) rt_error(in, at, "sleep() 的时间不能为负数");
    fflush(stdout);
    /* 分片睡眠 + 分片派发: 睡眠期间定时器/中断事件也能得到响应 */
    int32_t left = (int32_t)(sec * 1000.0);
    while (left > 0) {
        int32_t step = (left > 20) ? 20 : left;
        l_hal_delay_ms((uint32_t)step);
        left -= step;
        hw_poll_events(in);
    }
    return val_nil();
}

static const Native time_natives[] = {
    {"time", 0, 0, f_time, "time()", "当前时间戳(秒)"},
    {"date", 0, 1, f_date, "date([时间戳])", "返回 \"YYYY-MM-DD\",缺省为今天"},
    {"datetime", 0, 1, f_datetime, "datetime([时间戳])", "返回 \"YYYY-MM-DD HH:MM:SS\",缺省为现在"},
    {"format_time", 2, 2, f_format_time, "format_time(时间戳, 格式)", "按 strftime 格式化,如 \"%Y/%m/%d %H:%M\""},
    {"localtime", 0, 1, f_localtime, "localtime([时间戳])", "拆成 map:year month day hour minute second weekday yearday"},
    {"make_time", 3, 6, f_make_time, "make_time(年, 月, 日, [时, 分, 秒])", "构造时间戳,日期不存在返回 nil"},
    {"parse_time", 1, 1, f_parse_time, "parse_time(字符串)", "解析 \"YYYY-MM-DD[ HH:MM:SS]\",失败返回 nil"},
    {"sleep", 1, 1, f_sleep, "sleep(秒)", "暂停指定的秒数(可以是小数)"},
};

const NativeGroup group_time = {
    "time", "日期时间", time_natives, sizeof time_natives / sizeof time_natives[0]
};

#endif /* L_WITH_STDLIB_TIME */
