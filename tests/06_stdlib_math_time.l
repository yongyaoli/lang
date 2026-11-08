// 标准库:数学、日期时间(结果与本地时区无关)
print(abs(-3), floor(2.7), ceil(2.1), round(2.5), round(-2.5), sqrt(16), pow(2, 10));
print(min(3, 1, 2), max([4, 9, 2]), PI > 3.14, E > 2.7);

random_seed(42);
let a = random();
random_seed(42);
print(a == random(), a >= 0 and a < 1);
let ok = true;
for let i = 0; i < 200; i = i + 1 {
    let k = randint(3, 5);
    if k < 3 or k > 5 or k != floor(k) { ok = false; }
}
print(ok, randint(7, 7));

let ts = parse_time("2026-11-06 14:30:05");
print(datetime(ts), date(ts), format_time(ts, "%H:%M"));
let t = localtime(ts);
print(t.year, t.month, t.day, t.hour, t.minute, t.second, t.weekday, t.yearday);
print(make_time(2026, 11, 6, 14, 30, 5) == ts, parse_time("2026-02-30"), parse_time("bad"), parse_time("2026-11-06 25:00:00"));
print(date(parse_time("2026-11-06")), len(date()), len(datetime()), time() > 1700000000);
print((parse_time("2026-11-07") - parse_time("2026-11-06")) / 3600);
print(format_time(ts, "%Y年%m月%d日"));
sleep(0.01);
