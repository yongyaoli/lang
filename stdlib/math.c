/* 标准库:数学 */
#include <math.h>
#include <stdint.h>
#include <time.h>

#include "../interp.h"

static Value f_abs(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    if (argv[0].type == V_INT) {        /* int 保持 int (INT_MIN 回绕为自身, 与 C 一致) */
        int32_t x = argv[0].as.i;
        return val_int(x < 0 ? (int32_t)(0u - (uint32_t)x) : x);
    }
    return val_num(fabs(num_arg(in, at, "abs", argv, 0)));
}
static Value f_floor(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc; return val_whole(floor(num_arg(in, at, "floor", argv, 0)));
}
static Value f_ceil(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc; return val_whole(ceil(num_arg(in, at, "ceil", argv, 0)));
}
static Value f_round(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc; return val_whole(round(num_arg(in, at, "round", argv, 0)));   /* 四舍五入,0.5 远离 0 */
}
static Value f_sqrt(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    double x = num_arg(in, at, "sqrt", argv, 0);
    if (x < 0) rt_error(in, at, "sqrt() 的参数不能为负数");
    return val_num(sqrt(x));
}
static Value f_pow(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    return val_num(pow(num_arg(in, at, "pow", argv, 0), num_arg(in, at, "pow", argv, 1)));
}

/* min / max:参数可以是若干个数字,也可以是一个数字数组 */
static Value extremum(Interp *in, const Node *at, const char *fn, int argc, Value *argv, int want_max) {
    const Value *items = argv;
    int n = argc;
    if (argc == 1 && argv[0].type == V_ARRAY) {
        items = argv[0].as.array->items;
        n = argv[0].as.array->count;
        if (n == 0) rt_error(in, at, "%s() 不能用于空数组", fn);
    }
    Value best = val_nil();
    for (int i = 0; i < n; i++) {
        if (!val_is_number(items[i]))
            rt_error(in, at, "%s() 只能比较数字,实际遇到 %s", fn, type_name(items[i]));
        if (i == 0) { best = items[i]; continue; }
        double x = val_to_double(items[i]), b = val_to_double(best);
        if (want_max ? x > b : x < b) best = items[i];   /* 保持原有的 int/float 类型 */
    }
    return best;
}
static Value f_min(Interp *in, const Node *at, int argc, Value *argv) {
    return extremum(in, at, "min", argc, argv, 0);
}
static Value f_max(Interp *in, const Node *at, int argc, Value *argv) {
    return extremum(in, at, "max", argc, argv, 1);
}

/* 随机数:xorshift64*,不依赖 rand()(其 RAND_MAX 在 Windows 上只有 32767) */
static uint64_t rng_state = 0;

static void rng_seed(uint64_t s) { rng_state = s ? s : 0x9E3779B97F4A7C15ULL; }

static uint64_t rng_next(void) {
    if (rng_state == 0) rng_seed((uint64_t)time(NULL) * 6364136223846793005ULL + (uint64_t)clock());
    rng_state ^= rng_state >> 12;
    rng_state ^= rng_state << 25;
    rng_state ^= rng_state >> 27;
    return rng_state * 0x2545F4914F6CDD1DULL;
}

static Value f_random(Interp *in, const Node *at, int argc, Value *argv) {
    (void)in; (void)at; (void)argc; (void)argv;
    return val_num((double)(rng_next() >> 11) / 9007199254740992.0);    /* [0, 1) */
}

static Value f_randint(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    int lo = int_arg(in, at, "randint", argv, 0), hi = int_arg(in, at, "randint", argv, 1);
    if (lo > hi) rt_error(in, at, "randint() 要求下界不大于上界");
    return val_int((int32_t)(lo + (int64_t)(rng_next() % ((uint64_t)hi - (uint64_t)lo + 1))));
}

static Value f_random_seed(Interp *in, const Node *at, int argc, Value *argv) {
    (void)argc;
    rng_seed((uint64_t)int_arg(in, at, "random_seed", argv, 0));
    return val_nil();
}

static const Native math_natives[] = {
    {"abs", 1, 1, f_abs, "abs(x)", "绝对值"},
    {"floor", 1, 1, f_floor, "floor(x)", "向下取整"},
    {"ceil", 1, 1, f_ceil, "ceil(x)", "向上取整"},
    {"round", 1, 1, f_round, "round(x)", "四舍五入到整数"},
    {"sqrt", 1, 1, f_sqrt, "sqrt(x)", "平方根,x 不能为负"},
    {"pow", 2, 2, f_pow, "pow(x, y)", "x 的 y 次方"},
    {"min", 1, -1, f_min, "min(x, ...) / min(数组)", "最小值"},
    {"max", 1, -1, f_max, "max(x, ...) / max(数组)", "最大值"},
    {"random", 0, 0, f_random, "random()", "返回 [0, 1) 的随机小数"},
    {"randint", 2, 2, f_randint, "randint(下界, 上界)", "返回闭区间内的随机整数"},
    {"random_seed", 1, 1, f_random_seed, "random_seed(整数)", "设置随机数种子,使结果可重复"},
};

const NativeGroup group_math = {
    "math", "数学", math_natives, sizeof math_natives / sizeof math_natives[0]
};
