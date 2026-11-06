//! Machine fingerprint: sha256("dkk1|" + MachineGuid + "|" + C: serial + "|" + hostname).
//! The recipe is frozen by the licensing standards document; never change it
//! without bumping the prefix, or every machine already activated in the
//! panel stops matching its key.

use windows_sys::Win32::Foundation::ERROR_SUCCESS;
use windows_sys::Win32::Storage::FileSystem::GetVolumeInformationW;
use windows_sys::Win32::System::Registry::{
    RegGetValueW, HKEY_LOCAL_MACHINE, RRF_RT_REG_SZ, RRF_SUBKEY_WOW6464KEY,
};
use windows_sys::Win32::System::WindowsProgramming::GetComputerNameW;

use crate::config;
use crate::util::{from_wide, sha256_hex, wide};

fn machine_guid() -> String {
    let subkey = wide("SOFTWARE\\Microsoft\\Cryptography");
    let value = wide("MachineGuid");
    let mut buffer = [0u16; 256];
    let mut size = (buffer.len() * 2) as u32;

    let status = unsafe {
        RegGetValueW(
            HKEY_LOCAL_MACHINE,
            subkey.as_ptr(),
            value.as_ptr(),
            RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
            std::ptr::null_mut(),
            buffer.as_mut_ptr() as *mut std::ffi::c_void,
            &mut size,
        )
    };
    if status != ERROR_SUCCESS {
        return String::new();
    }
    from_wide(&buffer)
}

fn system_volume_serial() -> u32 {
    let root = wide("C:\\");
    let mut serial: u32 = 0;
    let ok = unsafe {
        GetVolumeInformationW(
            root.as_ptr(),
            std::ptr::null_mut(),
            0,
            &mut serial,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            0,
        )
    };
    if ok == 0 {
        0
    } else {
        serial
    }
}

/// Machine name as registered on the licence; the panel shows it per machine.
pub fn computer_name() -> String {
    if let Ok(name) = std::env::var("COMPUTERNAME") {
        if !name.is_empty() {
            return name;
        }
    }
    let mut buffer = [0u16; 256];
    let mut size = buffer.len() as u32;
    let ok = unsafe { GetComputerNameW(buffer.as_mut_ptr(), &mut size) };
    if ok == 0 {
        String::new()
    } else {
        from_wide(&buffer)
    }
}

/// Lowercase 64-character SHA-256 fingerprint, or empty when a component
/// could not be read.
pub fn hwid() -> String {
    let guid = machine_guid();
    if guid.is_empty() {
        return String::new();
    }
    let source = format!(
        "{}{}|{:08x}|{}",
        config::HWID_PREFIX,
        guid,
        system_volume_serial(),
        computer_name()
    );
    sha256_hex(source.as_bytes())
}
