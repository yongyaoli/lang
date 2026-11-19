// ============================================
// 测试 25: 统一虚拟文件系统 (FS / File 标准库, M13)
// ============================================

fn assert(cond, msg) {
    if not cond {
        print("断言失败:", msg);
        exit(1);
    }
}

print("--- 测试 1. 全局单例与模块导入 ---");
assert(FS != nil, "全局单例 FS 应当已就绪");
import "fs";
assert(fs.FS != nil, "模块导出应当包含 FS");
assert(fs.File != nil, "模块导出应当包含 File");
print("FS 与 File 对象加载正常");

print("--- 测试 2. 基础写入与存在性检查 (FS.write / FS.exists) ---");
let test_path = "/flash/test_vfs.txt";
// 清理历史残留
if FS.exists(test_path) {
    FS.remove(test_path);
}
assert(FS.exists(test_path) == false, "清理后文件应当不存在");

let written = FS.write(test_path, "Hello Lang VFS!\nLine 2\n");
assert(written > 0, "写入字节数应当大于 0");
assert(FS.exists(test_path) == true, "写入后文件应当存在");
print("文件写入成功, 大小:", FS.size(test_path), "字节");

print("--- 测试 3. 一次性读取与追加写入 (FS.read / FS.append) ---");
let content = FS.read(test_path);
assert(content == "Hello Lang VFS!\nLine 2\n", "读取内容应当与写入一致");

let append_bytes = FS.append(test_path, "Line 3: Appended\n");
assert(append_bytes > 0, "追加字节数应当大于 0");
let content2 = FS.read(test_path);
assert(content2 == "Hello Lang VFS!\nLine 2\nLine 3: Appended\n", "追加后内容应当完整匹配");
print("追加写入与全量读取正常");

print("--- 测试 4. 递归创建目录与目录遍历 (FS.mkdir / FS.list / FS.isdir) ---");
let sub_dir = "/flash/data/configs";
FS.mkdir(sub_dir);
assert(FS.exists(sub_dir) == true, "创建后多级目录应当存在");
assert(FS.isdir(sub_dir) == true, "FS.isdir 应当识别目录");

let config_file = sub_dir + "/device.json";
FS.write(config_file, "{\"device\":\"ESP32\",\"online\":true}");
assert(FS.exists(config_file) == true, "子目录下文件应当存在");

let flash_list = FS.list("/flash");
assert(len(flash_list) > 0, "/flash 根目录应当至少包含条目");
print("/flash 条目列表扫描正常, 条目数 > 0:", len(flash_list) > 0);

print("--- 测试 5. 文件复制、重命名与删除 (FS.copy / FS.rename / FS.remove) ---");
let copy_target = "/flash/data/test_backup.txt";
assert(FS.copy(test_path, copy_target) == true, "文件复制应当成功");
assert(FS.exists(copy_target) == true, "副本文件应当存在");
assert(FS.read(copy_target) == content2, "副本内容应当与源文件一致");

let renamed_target = "/flash/data/test_renamed.txt";
assert(FS.rename(copy_target, renamed_target) == true, "重命名应当成功");
assert(FS.exists(copy_target) == false, "旧文件应当已不存在");
assert(FS.exists(renamed_target) == true, "新文件应当存在");

assert(FS.remove(renamed_target) == true, "删除文件应当返回 true");
assert(FS.exists(renamed_target) == false, "删除后文件应当不存在");
print("文件复制/重命名/删除功能验证通过");

print("--- 测试 6. 存储看板信息 (FS.info) ---");
let info = FS.info();
assert(info.total_bytes > 0, "总容量应当大于 0");
assert(info.free_bytes > 0, "剩余容量应当大于 0");
assert(info.fs_type != "", "文件系统类型应当有效");
print("文件系统类型:", info.fs_type, "总空间 > 0:", info.total_bytes > 0);

print("--- 测试 7. 大文件流式句柄 (File.open / write / readline / seek / eof) ---");
let log_path = "/flash/stream_log.txt";
let writer = File.open(log_path, "w");
writer.write("LOG_ROW_1: System Boot\n");
writer.write("LOG_ROW_2: Network Ready\n");
writer.write("LOG_ROW_3: Sensor Attached\n");
writer.flush();
writer.close();

let reader = File.open(log_path, "r");
let lines = [];
while true {
    let line = reader.readline();
    if line == nil { break; }
    lines.push(line);
}
assert(len(lines) == 3, "逐行读取应当准确读取 3 行");
assert(lines[0] == "LOG_ROW_1: System Boot\n", "第 1 行内容匹配");
assert(lines[1] == "LOG_ROW_2: Network Ready\n", "第 2 行内容匹配");
assert(lines[2] == "LOG_ROW_3: Sensor Attached\n", "第 3 行内容匹配");
assert(reader.eof() == true, "读完后 eof 应当为 true");

// 定位测试: seek 到起始位置重新读取部分内容
reader.seek(0, 0);
assert(reader.tell() == 0, "seek(0, 0) 后偏移应当为 0");
let prefix = reader.read(9);
assert(prefix == "LOG_ROW_1", "读取前 9 字节应为 LOG_ROW_1");
reader.close();
print("File 流式句柄逐行读写与定位正常");

print("--- 测试 8. 异常防护与错误定位 ---");
let caught_read_err = false;
try {
    FS.read("/flash/non_existing_file_9999.xyz");
} catch (e) {
    caught_read_err = true;
    print("捕获读取不存在文件异常:", e.message);
}
assert(caught_read_err == true, "读取不存在文件应当抛出错误");

let caught_open_err = false;
try {
    File.open("/flash/non_existing_file_9999.xyz", "r");
} catch (e) {
    caught_open_err = true;
    print("捕获打开不存在文件只读异常:", e.message);
}
assert(caught_open_err == true, "只读打开不存在文件应当抛出错误");

print("--- 测试 9. 动态多文件 import 支持 ---");
let dynamic_lib_path = "/flash/dynamic_calc.l";
FS.write(dynamic_lib_path, "fn add(a, b) { return a + b; }\nlet answer = 42;\n");

import "/flash/dynamic_calc.l" as calc;
let sum_res = calc.add(10, 32);
assert(sum_res == 42, "动态导入模块计算应当正确");
assert(calc.answer == 42, "动态导入模块常量应当正确");
print("动态文件 import 验证成功: calc.add(10, 32) =", sum_res);

print("=== FS 统一虚拟文件系统测试全部通过 ===");
