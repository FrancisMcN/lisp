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

Quoting is common in Lisp and is supported by the `quote` special form. The shorthand `'` is not yet supported.

```lisp
> (quote (+ 1 2 3))
(+ 1 2 3)
> (+ 1 2 3)
6
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
