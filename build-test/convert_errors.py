import re

path = "/mnt/d/github/lang_g/vm.c"
src = open(path, encoding="utf-8").read()

# 1) fprintf(stderr, "[%d:%d] 运行时错误: XXX\n", CURRENT_LINE(), CURRENT_COL(), args...);  ->  VM_ERROR("XXX", args...);
pat = re.compile(r'fprintf\(stderr, "\[%d:%d\] 运行时错误: (.*?)\\n", CURRENT_LINE\(\), CURRENT_COL\(\)(.*?)\);')
n = len(pat.findall(src))
src = pat.sub(lambda m: 'VM_ERROR("%s"%s);' % (m.group(1), m.group(2)), src)

# 2) 删除紧随 VM_ERROR(...); 之后多余的 return 1;
pat2 = re.compile(r'(VM_ERROR\([^\n]*\);)\n[ \t]*return 1;[^\n]*\n')
src, n2 = pat2.subn(lambda m: m.group(1) + "\n", src)

open(path, "w", encoding="utf-8", newline="\n").write(src)
print("converted fprintf sites:", n, "removed returns:", n2)
