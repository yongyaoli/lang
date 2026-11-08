// 测试 io / sys 扩展函数
let d = "tmp_io_dir";
if (io_exists(d)) { io_rmdir(d); }
print("mkdir:", io_mkdir(d));
print("isdir:", io_isdir(d), io_isdir("tests/13_io_sys_ext.l"));

let f = io_join(d, "a.txt");
print("join:", f);
write_file(f, "hello");
print("size:", io_size(f), io_size("不存在的文件"));
print("copy:", io_copy(f, io_join(d, "b.txt")));
print("rename:", io_rename(io_join(d, "b.txt"), io_join(d, "c.txt")));
print("list:", io_listdir(d));
print("list missing:", io_listdir("不存在的目录"));

print("basename:", io_basename("x/y/z.tar.gz"));
print("dirname:", io_dirname("x/y/z.txt"), io_dirname("z.txt"));
print("ext:", io_ext("x/y/z.tar.gz"), "[" + io_ext("Makefile") + "]");

io_remove(io_join(d, "a.txt"));
io_remove(io_join(d, "c.txt"));
print("rmdir:", io_rmdir(d), !io_exists(d));

// sys 扩展
let args = sys_args();
print("args:", len(args));
print("script:", io_basename(sys_script()));
print("pid:", sys_pid() > 0);
print("setenv:", sys_setenv("L_TEST_VAR", "abc"), sys_getenv("L_TEST_VAR"));
let old = sys_cwd();
print("chdir:", sys_chdir("tests"), io_exists("13_io_sys_ext.l"));
sys_chdir(old);
