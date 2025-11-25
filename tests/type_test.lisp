;; Some unit tests for the builtin type function

(deftest test_number_is_number
	(assert (= (type 1) "number")))

(deftest test_string_is_string
    (assert (= (type "abc") "string")))

(deftest test_true_is_bool
    (assert (= (type true) "bool")))

(deftest test_false_is_bool
    (assert (= (type false) "bool")))

(deftest test_symbol_is_symbol
    (assert (= (type (quote abc)) "symbol")))

(deftest test_keyword_is_symbol
    (assert (= (type :my-keyword) "symbol")))

(deftest test_error_is_error
    (assert (= (type (error "my error")) "error")))

(deftest test_list_is_cons
    (assert (= (type (list 1 2 3 4)) "cons")))

(deftest test_nil_is_nil
    (assert (= (type nil) "nil")))
