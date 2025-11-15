;; Some unit tests for special forms

(deftest test_local_value_is_accessible_using_let
    (let (x "hello")
        (assert (= x "hello"))))

(deftest test_multiple_values_can_be_set_using_let
    (let (a 5 b 7)
        (assert (= (+ a b) 12))))

(deftest test_shorthand_quote_special_form
    (assert (= 'x (quote x))))

(deftest test_shorthand_quote_special_form2
    (assert (= 'a (quote a))))
