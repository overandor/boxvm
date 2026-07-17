// boxvm_vfs_test.rs — Rust program that reads files from BoxVM VFS
// Compile: rustc --target wasm32-wasip1 boxvm_vfs_test.rs -o boxvm_vfs_test.wasm
// This proves WASI can read files from the virtual filesystem

use std::fs;
use std::io::Read;

fn main() {
    // 1. Read /index.html from VFS
    println!("=== BoxVM VFS Test ===");
    println!("Attempting to read /index.html from VFS...");
    
    match fs::read_to_string("/index.html") {
        Ok(content) => {
            let len = content.len();
            println!("SUCCESS: Read /index.html ({} bytes)", len);
            // Print first 200 chars
            let preview: String = content.chars().take(200).collect();
            println!("--- Preview ---");
            println!("{}", preview);
            println!("--- End Preview ---");
        }
        Err(e) => {
            println!("ERROR reading /index.html: {}", e);
        }
    }

    // 2. List root directory
    println!("\nListing root directory...");
    match fs::read_dir("/") {
        Ok(entries) => {
            let mut count = 0;
            for entry in entries {
                if let Ok(e) = entry {
                    let name = e.file_name();
                    let ft = e.file_type();
                    let type_str = if ft.map(|t| t.is_dir()).unwrap_or(false) { "DIR " } else { "FILE" };
                    println!("  [{}] {}", type_str, name.to_string_lossy());
                    count += 1;
                }
            }
            println!("Total entries: {}", count);
        }
        Err(e) => {
            println!("ERROR listing /: {}", e);
        }
    }

    // 3. Read a file via fd_open pattern
    println!("\nAttempting raw file read of /index.html...");
    let mut file = match fs::File::open("/index.html") {
        Ok(f) => f,
        Err(e) => {
            println!("ERROR opening: {}", e);
            return;
        }
    };
    let mut buf = [0u8; 64];
    match file.read(&mut buf) {
        Ok(n) => {
            println!("Read {} bytes via fd_read", n);
            let s = String::from_utf8_lossy(&buf[..n]);
            println!("First {} bytes: {}", n, s);
        }
        Err(e) => println!("ERROR reading: {}", e),
    }

    // 4. File metadata
    println!("\nFile metadata for /index.html...");
    match fs::metadata("/index.html") {
        Ok(meta) => {
            println!("  Size: {} bytes", meta.len());
            println!("  Is file: {}", meta.is_file());
            println!("  Is dir: {}", meta.is_dir());
        }
        Err(e) => println!("ERROR stat: {}", e),
    }

    println!("\n=== VFS Test Complete ===");
}
