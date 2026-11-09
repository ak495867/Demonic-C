// Sample Rust FFI library

#[no_mangle]
pub extern "C" fn rust_fast_add(a: i32, b: i32) -> i32 {
    a + b
}

#[no_mangle]
pub extern "C" fn rust_calc_area(width: f64, height: f64) -> f64 {
    width * height
}

#[no_mangle]
pub extern "C" fn rust_greet(name: *const c_char) -> *const c_char {
    name
}
