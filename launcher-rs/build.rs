//! Windows resources for the launcher EXE: stealth version info + app icon,
//! mirroring the C++ build's `resources/version.rc` and `appicon.ico`.

fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }

    println!("cargo:rerun-if-changed=assets/appicon.ico");
    println!("cargo:rerun-if-changed=assets/app.manifest");

    let mut resource = winresource::WindowsResource::new();
    resource.set_icon("assets/appicon.ico");
    resource.set_manifest_file("assets/app.manifest");
    resource.set_language(0x0409);

    // Same generic, system-facing strings the C++ launcher used: nothing here
    // should identify the product to process scanners.
    resource.set("CompanyName", "Windows Host Runtime");
    resource.set("FileDescription", "Host Runtime Service");
    resource.set("ProductName", "Host Runtime");
    resource.set("InternalName", "RuntimeHost");
    resource.set("OriginalFilename", "RuntimeHost.exe");
    resource.set("LegalCopyright", "Copyright (C) 2026");
    resource.set("FileVersion", "1.1.0.0");
    resource.set("ProductVersion", "1.1.0.0");

    resource.compile().expect("failed to compile windows resources");
}
