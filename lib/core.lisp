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
    `(do (defn ,name () ,body)
        (,name)))

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
    `(cond ,condition true))
