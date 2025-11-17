[![Lisp pipeline](https://github.com/FrancisMcN/lisp/actions/workflows/lisp.yml/badge.svg)](https://github.com/FrancisMcN/lisp/actions/workflows/lisp.yml)

Lisp Interpreter implemented in C
=================================

My attempt at a Lisp interpreter implemented in C. I've still got lots of work to do. So far the interpreter just starts up a REPL and has some basic functionality.

My interpreter is implemented from scratch in a single C file.

You can associate a symbol with a value using the define special form.

```lisp
> (define x 123)
nil
> x
123
```

The interpreter also has some built-in functions such as `+`, `car` and `cdr`.

```lisp
> (+ 1 2 3)
6
> (car (quote (+ 4 5 6)))
+
> (cdr (quote (5 4 3)))
(4 3)
```

Lists are represented using chains (linked lists) of cons cells. Printing of cons cells also works as expected.

```lisp
> (cons 1 (cons 2 (cons 3))) 
(1 2 3)
> (cons "a" "b")
(a . b)
> (cons 1 (cons 2 3))
(1 2 . 3)
```

I've implemented various Lisp data types such as numbers, strings and symbols. Numbers are currently just C integers.

```lisp
> (type "hello world")
string
> (type (quote abc))
symbol
> (type 123)
number
```

The builtin `append` function is now supported. Append takes one or more lists and creates a new list containing all of the elements from the original lists.
```lisp
> (define x '(a b c d))
> (define y '(x y z))
> (append x y)
(a b c d x y z)
```

Quoting is common in Lisp and is supported by the `quote` special form. The shorthand `'` is also supported.

```lisp
> (quote (+ 1 2 3))
(+ 1 2 3)
> (+ 1 2 3)
6
> 'abc
abc
```

Quasi-quoting is also supported to make writing macros easier. Quasi-quoting allows you to `unquote` certain elements within your quoted list. It also makes generating lists within a macro easier

```lisp
> (define b 7)
> `(a ,b c)
(a 7 c)
```

I've implemented the beginnings of a simple mark and sweep garbage collector which can be triggered using two built-in functions.

```lisp
> (gc-mark)
marked 6 objects out of 9 total objects.
nil
> (gc-sweep)
freed 6 objects.
nil
>
```

The garbage collector is now automatically triggered after every 100 object allocations which is too frequent and too simple but it'll work for now.

The interpreter can also execute multiple Lisp expressions in sequence and it can run programs directly from text files.
```lisp
> (+ 1 2 3) (+ 3 4 5)
6
12
```

The `do` special form is now supported, it's useful for when you want to include multiple expressions inside a single form such as a lambda special form or an if special form.
```lisp
> (define hello-world (lambda (a b c) (do (print a) (print b) (print c))))
> (hello-world 1 2 3)
1
2
3
```

The `let` special form is supported too. You can use the let special form to create local variables.
```lisp
(let (x 1 y 2)
    (do (+ x y)))
```

User-defined functions are now supported using the built-in `lambda` special form. You can either assign the function to a symbol or you can call it directly.
```lisp
> ((lambda (name) (print name)) "francis")
francis
```
Below is an example showing how to assign a name to a function.
```lisp
> (define double (lambda (a) (+ a a)))
> (double 10)
20
```

Unit tests can be written using the special `deftest` macro. Currently this macro just expands to a function definition and a function call. An example unit test proving a list of of type `cons` is shown below.
```lisp
(deftest test_list_is_cons
    (assert (= (type (list 1 2 3 4)) "cons")))
```
