# NullScript

> the language that judges you

NullScript is a dynamically-typed scripting language with a tree-walking interpreter written in C. It has strong opinions about your code quality and will let you know about them.

---

## Quick Start

```sh
# Build
make

# Run a script
./nullscript x hello.ns

# Compile to portable .nsx
./nullscript x -c hello.ns

# Run the compiled file
./nullscript x hello.nsx

# Help
./nullscript help
```

---

## Language Reference

### Comments

```
~ this is a single-line comment
~~ this is a
   multiline comment ~~
```

> **Note:** Adding extra tildes in comments will get you judged by the compiler. Inside a `~~` block, a bare `~` is a hard syntax error.

---

### Variables & Constants

```
var name="Alice" String Global
const pi=3.14159 Type=Decimal Scope=Function
var count=0 Integer
var anything                         ~ type=Any, scope=Function, value=Null
```

- `var` — mutable variable
- `const` — immutable constant
- Type is optional and defaults to `Any`
- Scope is optional: `Global` or `Function` (default)
- Value is optional and defaults to `Null`

**Compound assignment:**
```
count += 1
count -= 5
count *= 2
count /= 4
count **= 2
count %= 3
```

---

### Types

| Type        | Example                      | Notes                              |
|-------------|------------------------------|------------------------------------|
| `String`    | `"hello"`                    |                                    |
| `Integer`   | `42`                         |                                    |
| `Float`     | `3.14`                       | Has floating-point imprecision     |
| `Decimal`   | `3.14:Decimal`               | Stored as string, exact            |
| `Bool`      | `True` / `False`             |                                    |
| `Null`      | `Null`                       |                                    |
| `Array`     | `{"a", 1, {2, 3}}`           | 1-indexed                          |
| `Dict`      | `{key: "val", other: 123}`   |                                    |

**Arrays and Dicts:**
```
var arr={"apple", "banana", "cherry"}
println(arr{1})               ~ apple (1-indexed)
arr{2} = "blueberry"

var person={name: "Alice", age: 30}
var k="name"
println(person{k})            ~ Alice
person{"age"} = 31
```

**Ranges:**
```
var nums=1..10                ~ creates {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
```

---

### Functions

```
func greet(name, greeting) {
    println(e"$greeting, $name!")
}

~ Positional call
greet("World", "Hello")

~ Named argument call (any order)
greet(greeting="Howdy", name="partner")
```

> Mixing positional after named arguments is a syntax error and the compiler will roast you for it.

**Functions as values:**
```
func double(x) { return x * 2 }
var result=map({1, 2, 3}, double)

~ Inline function literal
var evens=filter(1..10, func isEven(n) { return n % 2 == 0 })
```

**Closures:**
```
func makeCounter(start) {
    var n=start Integer
    func next() {
        n += 1
        return n
    }
    return next
}

var counter=makeCounter(0)
println(counter())   ~ 1
println(counter())   ~ 2
println(counter())   ~ 3
```

---

### String Interpolation

```
var name="World"
println(e"Hello, $name!")
println(e"2 + 2 = ${2 + 2}")
println(e"type: ${type(name)}")
println(e"upper half: ${charsFrom(name, 1, 3)}")
```

---

### Conditionals

```
if (score >= 90) {
    println("A")
} else if (score >= 80) {
    println("B")
} else {
    println("try harder")
}
```

**Operators:** `==`, `!=`, `<`, `>`, `<=`, `>=`, `and`, `or`, `not`

> When in doubt about operator precedence, just use parentheses. The docs won't save you. Parentheses will.

---

### Loops

```
~ repeat N times, counter starts at 1
repeat i=10 {
    println(e"iteration $i")
}

~ repeat with computed count
repeat n=len(myArray) {
    println(myArray{n})
}
```

---

### try / catch

```
try {
    assert(x != 0, "x must not be zero")
    var result=10 / x
} catch(err) {
    println(e"caught: $err")
}
```

---

### Aliases

```
alias func fn
alias var v
alias println log

fn sayHi(who) {
    log(e"Hi, $who!")
}
```

---

### Import

```
import someLibrary          ~ from package manager
import myutils.ns           ~ local .ns file
import compiled.nsx         ~ compiled file
import file                 ~ built-in file module
~ import bad.exe            ~ syntax error (compiler is appalled)
```

---

## Built-in Functions

### I/O
| Function | Description |
|---|---|
| `println(val)` | Print with newline |
| `println(val, "warn")` | Print to stderr as warning |
| `println(val, "err")` | Print to stderr as error |
| `print(val)` | Print without newline |
| `readline()` | Read a line from stdin |
| `clear()` | Clear the terminal |

### Types & Conversion
| Function | Description |
|---|---|
| `type(val)` | Returns type name as String |
| `convert(val, Type)` | Convert value to another type |
| `isNull(val)` | True if Null or Undefined |

### Math
| Function | Description |
|---|---|
| `Math.abs(n)` | Absolute value |
| `Math.sqrt(n)` | Square root |
| `Math.floor(n)` | Round down |
| `Math.ceil(n)` | Round up |
| `Math.round(n)` | Round nearest |
| `Math.min(a, b)` | Minimum |
| `Math.max(a, b)` | Maximum |
| `x ** y` | Power (no Math.pow needed) |

### Strings
`len`, `split`, `join`, `replace`, `contains`, `startsWith`, `endsWith`, `index`, `charsFrom`, `insert`

### Arrays
`len`, `push`, `pop`, `append`, `remove`, `insert`, `sort`, `reverse`, `merge`, `contains`, `index`

### Dicts
`keys`, `values`, `hasKey`

### Functional
```
map({1,2,3}, func double(x) { return x * 2 })
filter(1..20, func isEven(n) { return n % 2 == 0 })
reduce({1,2,3,4,5}, func add(a, b) { return a + b }, 0)
{1,2,3}.forEach(func print_it(x) { println(x) })
```

### Misc
| Function | Description |
|---|---|
| `assert(cond, msg)` | Raises catchable error if false |
| `die(code)` | Exit (like PHP's exit) |
| `time(humanReadable)` | Unix timestamp or formatted string |
| `wait(ms)` | Sleep for N milliseconds |
| `random(min, max, type)` | Random number |
| `env(name)` | Read environment variable |
| `shell(cmd)` | Execute shell command, return output |
| `trace(...)` | Debug print to stderr |
| `args(n)` | CLI argument access (1-indexed) |

### File module (`import file` required)
```
import file
var contents=file.read("path.txt")
file.write("out.txt", "hello")
file.append("log.txt", "more\n")
var exists=file.exists("file.txt")
```

---

## CLI Reference

```
nullscript x file.ns              interpret
nullscript x -c file.ns           compile to .nsx  (--compile)
nullscript x file.nsx             run compiled file
nullscript x -w file.ns out.exe   compile for Windows  (--windows)
nullscript x -m file.ns out       compile for macOS    (--mac)
nullscript x -l file.ns out       compile for Linux    (--linux)

nullscript i library              install package
nullscript i -g library           install globally     (--global)
nullscript i -v library           install to venv      (--venv)

nullscript v -m myvenv            create + activate venv  (--make)
nullscript v -o myvenv            activate existing venv  (--open)

nullscript r -a https://url       add repository          (--add)
nullscript r -u                   update manifests        (--update)
nullscript r -U                   upgrade packages        (--upgrade)
nullscript r -uU                  update + upgrade

nullscript version
nullscript help
```

Every incorrect CLI invocation is met with sarcasm. This is by design.

---

## Project Structure

```
nullscript/
  include/
    token.h         token types
    lexer.h         lexer interface
    ast.h           AST node types
    parser.h        parser interface
    interp.h        interpreter, value types, environment
  src/
    token.c         token utilities
    lexer.c         lexer (handles comments, strings, numbers, operators)
    ast.c           AST node creation/printing/freeing
    parser.c        recursive descent parser
    value.c         runtime value system + ref-counted environment
    builtins.c      all built-in functions
    interp.c        tree-walking interpreter + string interpolation
    main.c          CLI entry point (x / i / v / r subcommands)
  tests/
    test_hello.ns   hello world, print, warn, err
    test_types.ns   variables, arrays, dicts, ranges, type(), convert()
    test_funcs.ns   functions, closures, named args, recursion, HOF, aliases
    test_loops.ns   if/else-if/else, repeat, FizzBuzz, try/catch
    test_builtins.ns all built-in functions
    demo.ns         showcase script
  Makefile
  README.md
```

---

## Building

Requires GCC and `make`. No external dependencies.

```sh
make          # build ./nullscript
make test     # run all tests
make clean    # remove binary
```

---

## Known Limitations

- No actual native compilation (`.exe`/`.app`/Linux binary stubs are `.nsx` wrappers)
- No HTTP / networking builtins yet
- `async`/`await`/`spawn` are parsed but run synchronously
- Package registry not yet online — add repos with `nullscript r -a <url>`
- No `args()` builtin implementation yet (placeholder)

---

## License

Do whatever you want with it. If something breaks, the compiler will judge you.
