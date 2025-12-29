;; Some unit tests for core functionality

;; This test creates a lambda function with a variable
;; number of parameters. The first two parameters are named
;; 'a' and 'b' and the remaining parameters are passed to the
;; function as a list called '&'
(deftest test_rest_parameter_is_a_list
    (do ((lambda (a b &)
        (assert (= & '(3 4 5)))) 1 2 3 4 5)))

(deftest test_params_after_rest_param_are_nil
    (do ((lambda (a & c)
        (assert (= c nil))) 1 2 3 4)))
        
(deftest test_params_after_rest_param_are_nil
    (do ((lambda (&)
        (assert (= & '(1 2 3 4)))) 1 2 3 4)))
        
(deftest test_cond_macro_with_a_single_condition
        (assert (= '(if (< 1 2) (print "a"))
                    (macroexpand '(cond (< 1 2) (print "a"))))))

(deftest test_cond_macro_with_multiple_conditions
        (assert (= '(if (< 1 2)
                        (print "a")
                        (if (< 2 3)
                            (print "b")))
                    (macroexpand '(cond (< 1 2) (print "a")
                                        (< 2 3) (print "b"))))))
