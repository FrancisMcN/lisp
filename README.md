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
5
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
