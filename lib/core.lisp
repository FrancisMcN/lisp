;; some core functions and macros

;; 'cond' makes large if-statements more convenient to read
;; and write.
(defmacro cond (&)
    (do
        (if (> (len &) 2)
            `(if ,(car &) ,(car (cdr &)) ,(apply cond (cdr (cdr &))))
            `(if ,(car &) ,(car (cdr &))))))
