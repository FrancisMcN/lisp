;; Some unit tests for the builtin types and functions

(deftest test_nil_equals_nil
    (assert (= nil nil)))

(deftest test_string_comparison_with_different_strings
    (assert (= (= "abc" "xyz") false)))
    
(deftest test_string_comparison_with_identical_strings
    (assert (= (= "abc" "abc") true)))

(deftest test_string_comparison_with_different_length_strings
    (assert (= (= "abc123" "abc1234") false)))
    
(deftest test_builtin_plus_adds_correctly
    (assert (= (+ 1 2 3 4 5) 15)))
    
(deftest test_builtin_minus_subtracts_correctly
    (assert (= (- 1 2 3 4 5) -13)))
