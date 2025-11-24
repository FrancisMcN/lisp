;; The recursive-dotimes function is a simple first attempt at 
;; implementing iteration in my Lisp. This implementation is 
;; currently susceptible to stack overflow but it's ok to start with.
(func recursive-dotimes (body times)
    (do (eval body)
        (if (= times 1) nil
            (dotimes body (- times 1)))))

;; The dotimes macro is just a wrapper around the recursive-dotimes 
;; function. The macro just tidies up the interface by removing the need 
;; to quote the body
(defmacro dotimes (body times) 
	`(dotimes-function (quote ,body) ,times))
