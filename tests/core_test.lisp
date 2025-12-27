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
