# L 语言 EBNF 文法(v0.1)

> 记号约定:
> - `a | b` 选择;`( ... )` 分组;`[ ... ]` 可选(0 或 1 次);`{ ... }` 重复(0 次或多次)
> - `"xx"` 为终结符(关键字/符号);大写名称为词法 Token
> - `(* ... *)` 为注释
> - 语句以 `;` 结尾,代码块用 `{ }`

## 1. 词法(Lexical)

```ebnf
(* ---------- 空白与注释:被词法分析器忽略 ---------- *)
whitespace    = " " | "\t" | "\r" | "\n" ;
line_comment  = "//" , { any_char - "\n" } ;
block_comment = "/*" , { any_char } , "*/" ;            (* 不嵌套 *)

(* ---------- 基础字符 ---------- *)
letter        = "a".."z" | "A".."Z" | "_" ;
digit         = "0".."9" ;

(* ---------- Token ---------- *)
IDENT         = letter , { letter | digit } ;           (* 不能是关键字 *)

NUMBER        = INT | FLOAT ;
INT           = digit , { digit } | "0x" , hex , { hex } | "0b" , bin , { bin } ;   (* 32 位整数 *)
FLOAT         = digit , { digit } , "." , digit , { digit } ;                       (* 64 位浮点 *)

STRING        = '"' , { string_char | escape } , '"' ;
string_char   = any_char - ( '"' | "\\" | "\n" ) ;
escape        = "\\" , ( "n" | "t" | "r" | '"' | "\\" ) ;

(* ---------- 关键字(保留字) ---------- *)
keyword       = "let" | "fn" | "return" | "if" | "else" | "while"
              | "for" | "break" | "continue"
              | "true" | "false" | "nil"
              | "and" | "or" | "not" | "import" ;
```

## 2. 语法(Syntactic)

### 2.1 程序与语句

```ebnf
program       = { declaration } , EOF ;

declaration   = import_stmt
              | fn_decl
              | var_decl
              | statement ;

import_stmt   = "import" , STRING , [ "as" , IDENT ] , ";" ;

var_decl      = "let" , IDENT , [ "=" , expression ] , ";" ;

fn_decl       = "fn" , IDENT , "(" , [ parameters ] , ")" , block ;
parameters    = IDENT , { "," , IDENT } ;

statement     = expr_stmt
              | if_stmt
              | while_stmt
              | for_stmt
              | return_stmt
              | break_stmt
              | continue_stmt
              | block ;

expr_stmt     = expression , ";" ;

if_stmt       = "if" , expression , block ,
                [ "else" , ( if_stmt | block ) ] ;

while_stmt    = "while" , expression , block ;

for_stmt      = "for" , ( var_decl | expr_stmt | ";" ) ,
                [ expression ] , ";" ,
                [ expression ] , block ;

return_stmt   = "return" , [ expression ] , ";" ;
break_stmt    = "break" , ";" ;
continue_stmt = "continue" , ";" ;

block         = "{" , { declaration } , "}" ;
```

> 说明:`if`/`while` 的条件不加括号,但必须用 `{}` 块,可避免悬空 else 问题。
> `print` 不是关键字,作为**内置函数**提供:`print("hi");`

### 2.2 表达式(按优先级从低到高)

```ebnf
expression    = assignment ;

assignment    = ( call_or_index , "=" , assignment )     (* 左侧须为变量/下标/属性 *)
              | ternary ;

ternary       = logic_or , [ "?" , expression , ":" , ternary ] ; (* 三元表达式, 右结合 *)

logic_or      = logic_and , { "or" , logic_and } ;
logic_and     = equality  , { "and" , equality } ;
equality      = comparison , { ( "==" | "!=" ) , comparison } ;
comparison    = bit_or , { ( "<" | "<=" | ">" | ">=" ) , bit_or } ;
bit_or        = bit_xor , { "|" , bit_xor } ;            (* 位运算优先级高于比较 *)
bit_xor       = bit_and , { "^" , bit_and } ;
bit_and       = shift   , { "&" , shift } ;
shift         = term    , { ( "<<" | ">>" ) , term } ;
term          = factor , { ( "+" | "-" ) , factor } ;
factor        = unary  , { ( "*" | "/" | "%" ) , unary } ;   (* int/int 为整数除法 *)
unary         = ( "not" | "!" | "-" | "~" ) , unary
              | call_or_index ;

call_or_index = primary , { call_suffix | index_suffix | member_suffix } ;
call_suffix   = "(" , [ arguments ] , ")" ;
index_suffix  = "[" , expression , "]" ;                 (* v0.2:数组/map 下标 *)
member_suffix = "." , IDENT ;                            (* v0.2:map.key 语法糖 *)
arguments     = expression , { "," , expression } ;

primary       = NUMBER
              | STRING
              | "true" | "false" | "nil"
              | IDENT
              | fn_expr
              | array_literal                            (* v0.2 *)
              | map_literal                              (* v0.2 *)
              | "(" , expression , ")" ;

fn_expr       = "fn" , "(" , [ parameters ] , ")" , block ;   (* 匿名函数/闭包 *)
```

### 2.3 数组与 Map(v0.2,先预留)

```ebnf
array_literal = "[" , [ expression , { "," , expression } , [ "," ] ] , "]" ;

map_literal   = "{" , [ map_entry , { "," , map_entry } , [ "," ] ] , "}" ;
map_entry     = ( IDENT | STRING ) , ":" , expression ;
```

> 注意歧义:语句开头的 `{` 解析为 `block`,表达式位置的 `{` 解析为 `map_literal`。
> 解析器按位置区分即可,无需回溯。

## 3. 运算符优先级表(低 → 高)

| 级别 | 运算符 | 结合性 |
|---|---|---|
| 1 | `=` | 右 |
| 2 | `? :` (三元运算符) | 右 |
| 3 | `or` | 左 |
| 4 | `and` | 左 |
| 5 | `==` `!=` | 左 |
| 6 | `<` `<=` `>` `>=` | 左 |
| 7 | `+` `-` | 左 |
| 8 | `*` `/` `%` | 左 |
| 9 | `not` `!` `-`(一元) | 右 |
| 10 | `()` 调用、`[]` 下标、`.` 成员 | 左 |

## 4. 语义约定

- **类型**:`int`(32 位整数)、`float`(64 位浮点)、`string`、`bool`、`nil`;v0.2 增加 `array`、`map`
- **真值**:只有 `false` 和 `nil` 为假,其余(含 `0`、`""`)都为真
- **`+`**:数字相加或字符串拼接,混合类型报运行时错误
- **`==`**:不同类型不相等;数组/map 按引用比较
- **作用域**:词法作用域,`block` 创建新作用域;函数是一等值,支持闭包
- **返回值**:函数无 `return` 或 `return;` 时返回 `nil`

## 5. 示例

```
// 函数
fn add(a, b) { return a + b; }
print(add(1, 2));            // 3

// 变量、控制流
let i = 0;
while i < 3 {
    if i == 1 { i = i + 1; continue; }
    print("i=" + str(i));
    i = i + 1;
}

// 闭包
fn counter() {
    let n = 0;
    return fn() { n = n + 1; return n; };
}

// v0.2:数组与 map
let xs = [1, 2, 3];
let p  = { name: "L", ver: 0.1 };
print(xs[0], p.name);
```

## 6. 版本规划

| 版本 | 内容 |
|---|---|
| v0.1 | number/string/bool/nil、变量、控制流、函数、闭包 |
| v0.2 | 数组、map、下标、`.` 成员访问 |
| 之后 | 标准库、模块 `import`、错误处理等 |
