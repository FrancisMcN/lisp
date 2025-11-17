;; Some unit tests for the builtin types and functions

(deftest test_nil_equals_nil
    (assert (= nil nil)))

(deftest test_string_comparison_with_different_strings
    (assert (= (= "abc" "xyz") false)))
    
(deftest test_string_comparison_with_identical_strings
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
    
(deftest test_cdr_returns_tail_of_list
    (assert (= (cdr (quote (1 2 3 4 5))) (list 2 3 4 5))))
    
(deftest test_cdr_of_empty_list_is_nil
    (assert (= (cdr ()) nil)))
    
(deftest test_logical_equals_of_two_lists
    (assert (= '(a b c) '(a b c))))

(deftest test_append_joins_multiple_lists_together
    (assert (= (append '(a b c) '(d e f)) '(a b c d e f))))
