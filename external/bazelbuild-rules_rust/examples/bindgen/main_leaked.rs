//! rust_bindgen_library example consumer

fn simple_function() -> i64 {
    unsafe { simple_leaked_bindgen::simple_function() }
}

fn main() {
    println!(
        "The values are {} and {}!",
        simple_leaked_bindgen::SIMPLE_VALUE,
        simple_function()
    );
}

#[cfg(test)]
mod test {
    #[test]
    fn do_the_test() {
        assert_eq!(42, simple_leaked_bindgen::SIMPLE_VALUE);
        assert_eq!(1337, super::simple_function());
    }
}
