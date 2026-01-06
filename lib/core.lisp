;; some core functions and macros

;; 'defmacro' is itself a macro, it's used to associate a
;; macro with a name.
(define defmacro (macro (name args body)
    `(define ,name (macro ,args ,body))))

;; 'defn' is a macro used to associate a lambda with a name. Lambda's
;; are annonymous functions.
(defmacro defn (name args body)
    `(define ,name (lambda ,args ,body)))
    
;; 'deftest' is used to implement unit tests
(defmacro deftest (name body)
    `(let (err nil) (do (defn ,name () ,body)
        (,name) (= err nil))))

;; 'cond' makes large if-statements more convenient to read
;; and write.
(defmacro cond (&)
    (do
        (if (> (len &) 2)
            `(if ,(car &) ,(car (cdr &)) ,(apply cond (cdr (cdr &))))
            `(if ,(car &) ,(car (cdr &))))))

;; 'assert' is currently intended for unit testing but may be expanded
;; in the future.
(defmacro assert (condition)
    `(do (if ,condition
        true
        (set err (error "assertion failed")))))

;; recursive-dotimes is a recursive function which evaluates
;; 'body' the associated number of times
(defn recursive-dotimes (body times)
    (do (eval body)
        (if (= times 1) nil
            (recursive-dotimes body (- times 1)))))

;; dotimes is a macro which just wraps around the recursive-dotimes
;; function to make the interface better
(defmacro dotimes (body times)
    `(recursive-dotimes (quote ,body) ,times))
        
;; recursive-dowhile is a recursive function which evaluates
;; 'body' repeatedly until 'condition' is false. The repetition
;; is achieved using recursion.
(defn recursive-dowhile (condition body)
    (if (= (eval condition) true)
        (do (eval body)
            (recursive-dowhile condition body))))

;; dowhile is a macro which just wraps around the recursive-dowhile
;; function to make the interface better
(defmacro dowhile (condition body)
    `(recursive-dowhile (quote ,condition) (quote ,body)))

;; recursive-for is a recursive function which evaluates 'body'
;; for on each iteration. The number of iterations is determined by
;; the number of items in 'iterable'.
;; currently 'iterable' is presumed to be of type CONS
(defn recursive-for (iterator iterable body) (do
    (if (> (len iterable) 0) (do
        (eval (list 'set iterator (car iterable)))
        (eval body)
        (recursive-for iterator (cdr iterable) body)))))

;; for is just a macro which wraps around the recursive-for function
;; to make the interface better.
(defmacro for (&)
    `(recursive-for (quote ,(car &))
    ,(car (cdr (cdr &)))
    (quote ,(car (cdr (cdr (cdr &)))))))

