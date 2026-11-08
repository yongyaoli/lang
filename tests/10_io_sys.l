// 测试 io 与 sys 标准库
let test_file = "temp_test_io.txt";

// 1. 文件写、追加、检查存在
write_file(test_file, "line1\n");
print("exists1:", io_exists(test_file));
io_append(test_file, "line2\nline3\n");

// 2. 按行读取
let lines = io_read_lines(test_file);
print("lines count:", len(lines), "first:", lines[0], "last:", lines[2]);

// 3. 删除文件
io_remove(test_file);
print("exists2:", io_exists(test_file));

// 4. sys 库
print("platform:", sys_platform() != "");
print("version:", sys_version());
print("cwd:", len(sys_cwd()) > 0);
let alloc_before = sys_allocated();
sys_gc_collect();
let alloc_after = sys_allocated();
print("gc works:", alloc_before >= 0 and alloc_after >= 0);
