;; Some unit tests for special forms

(deftest test_local_value_is_accessible_using_let
    (let (x "hello")
        (assert (= x "hello"))))

(deftest test_let_special_form_evaluates_values
    (do (let (x (+ 1 2 3))
        (assert (= x 6)))))

(deftest test_define_inside_let_is_accessible_outside_let
    (do (let (x 10) (define y 20))
        (assert (= y 20))))

(deftest test_multiple_values_can_be_set_using_let
    (let (a 5 b 7)
        (assert (= (+ a b) 12))))

;; start of unit tests for the quote special form

(deftest test_shorthand_quote_special_form
    (assert (= (quote a) 'a)))
    
(deftest test_quote_with_multiple_arguments_returns_error
    (let (x (quote a b c)) (do
        (assert (= (type x) "error")))))

;; end of unit tests for the quote special form

(deftest test_quasiquote_shorthand_equals_long_form
    (assert (= `(a b c) (quasiquote (a b c)))))

(deftest test_quasiquote_doesnt_evaluate_symbols
    (assert (= `(a b c) '(a b c))))

(deftest test_quasiquote_symbols_can_be_unquoted
    (let (c 5)
        (assert (= `(a b ,c) '(a b 5)))))

(deftest test_recursive_unquoting_for_nested_forms
    (let (x 5)
        (assert (= `(do (print (+ ,x ,x x ,x))) '(do (print (+ 5 5 x 5)))))))
