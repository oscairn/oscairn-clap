// SPDX-License-Identifier: MIT
//! Raw FFI bindings for every header in the repository's `include/oscairn-clap/`, generated
//! at build time with bindgen against the vendored CLAP 1.2.10 headers (`rust/clap-headers`,
//! the version Oscairn pins). bindgen needs C headers to parse, which `clap-sys` does not
//! ship, so the CLAP types the extensions use are generated here too; `tests/bindings.rs`
//! checks they agree with `clap-sys`'s.
//!
//! bindgen keeps each header's ID strings as NUL-terminated byte strings (`OSCAIRN_EXT_UI`);
//! [`ids`] has each of them as a `&CStr`, generated from those.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(clippy::all)]

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

/// The generated [`ids`] constants: a header's `static const char[]` as a `&CStr`.
const fn id(bytes: &'static [u8]) -> &'static core::ffi::CStr {
    match core::ffi::CStr::from_bytes_with_nul(bytes) {
        Ok(id) => id,
        Err(_) => panic!("an ID string without its NUL"),
    }
}
