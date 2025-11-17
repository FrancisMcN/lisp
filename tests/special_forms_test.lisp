;; Some unit tests for special forms

(deftest test_local_value_is_accessible_using_let
    (let (x "hello")
        (assert (= x "hello"))))

(deftest test_multiple_values_can_be_set_using_let
    (let (a 5 b 7)
        (assert (= (+ a b) 12))))

(deftest test_shorthand_quote_special_form
    (assert (= (quote a) 'a)))
    
(deftest test_quasiquote_shorthand_equals_long_form
    (assert (= `(a b c) (quasiquote (a b c)))))

(deftest test_quasiquote_doesnt_evaluate_symbols
    (assert (= `(a b c) '(a b c))))

(deftest test_quasiquote_symbols_can_be_unquoted
    (let (c 5)
        (assert (= `(a b ,c) '(a b 5)))))
