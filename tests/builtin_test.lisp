;; Some unit tests for the builtin types and functions

(deftest test_nil_equals_nil
    (assert (= nil nil)))

(deftest test_string_comparison_with_different_strings_is_false
    (assert (= (= "abc" "xyz") false)))
    
(deftest test_string_comparison_with_identical_strings_is_true
    (assert (= (= "abc" "abc") true)))

(deftest test_string_comparison_with_different_length_strings
    (assert (= (= "abc123" "abc1234") false)))
    
(deftest test_plus_adds_correctly
    (assert (= (+ 1 2 3 4 5) 15)))
    
(deftest test_minus_subtracts_correctly
    (assert (= (- 1 2 3 4 5) -13)))

(deftest test_car_returns_first_item
    (assert (= (car (quote (1 2 3))) 1)))

(deftest test_car_returns_nil_for_empty_list
    (assert (= (car ()) nil)))

(deftest test_setcar_replaces_element_of_list
    (let (l '(1 2 3 4 5))
        (do (setcar l 'a)
            (assert (= l '(a 2 3 4 5))))))
    
(deftest test_cdr_returns_tail_of_list
    (assert (= (cdr (quote (1 2 3 4 5))) (list 2 3 4 5))))
    
(deftest test_cdr_of_empty_list_is_nil
    (assert (= (cdr ()) nil)))
    
(deftest test_setcdr_replaces_cdr_of_list
    (let (l '(1 2 3 4 5))
        (do (setcdr l '(4 6 8 10))
            (assert (= l '(1 4 6 8 10))))))

(deftest test_logical_equals_of_two_lists
    (assert (= '(a b c) '(a b c))))

(deftest test_append_joins_multiple_lists_together
    (assert (= (append '(a b c) '(d e f)) '(a b c d e f))))
    
(deftest test_appending_non_lists_returns_error
    (assert (= (type (append '(a b c) 1 2 3)) "error")))

(deftest test_macroexpand-1_only_expands_macro_once
    (let (m2 (macro (y) `(+ ,y ,y)) m1 (macro (x) `(m2 ,x)))
        (assert (= (macroexpand-1 '(m1 6)) '(m2 6)))))

(deftest test_macroexpand_keeps_expanding_until_result_is_not_macro
    (let (m2 (macro (y) `(+ ,y ,y)) m1 (macro (x) `(m2 ,x)))
        (assert (= (macroexpand '(m1 6)) '(+ 6 6)))))

(deftest test_dotimes_runs_code_multiple_times
    (do (define x 0)
        (dotimes (define x (+ x 1)) 5)
        (assert (= x 5))))

(deftest test_dowhile_only_runs_code_when_condition_is_true
    (do (define y 0)
        (dowhile false (define y (+ y 1)))
        (assert (= y 0))))

(deftest test_dowhile_runs_code_while_condition_is_true
    (do (define x 10)
        (dowhile (> x 5) (do
            (define x (- x 1))))
        (assert (= x 5))))
