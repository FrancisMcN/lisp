;; functions and macros to perform iteration

;; recursive-dotimes is a recursive function which evaluates 
;; 'body' the associated number of times
(func recursive-dotimes (body times)
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
(func recursive-dowhile (condition body)
    (do
    (if (= (eval condition) true)
        (do (eval body)
            (recursive-dowhile condition body)))))

;; dowhile is a macro which just wraps around the recursive-dowhile
;; function to make the interface better
(defmacro dowhile (condition body)
    (do
    `(recursive-dowhile (quote ,condition) (quote ,body))))
