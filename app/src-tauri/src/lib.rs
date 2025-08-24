use serde::{Deserialize, Serialize};
use std::net::TcpStream;
use std::io::{Read, Write};
use anyhow::Result;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Gopher {
    pub name: String,
    pub ip: String,
    pub port: u16,
}

const DAEMON_HOST: &str = "127.0.0.1";
const DAEMON_PORT: u16 = 43823;

#[tauri::command]
async fn get_network_peers() -> Result<Vec<Gopher>, String> {
    match get_gophers_from_daemon().await {
        Ok(gophers) => Ok(gophers),
        Err(e) => Err(format!("Failed to get network peers: {}", e)),
    }
}

#[tauri::command]
async fn start_call(target_ip: String, target_port: u16) -> Result<String, String> {
    // For now, just return success message
    // This will be integrated with the engine later
    Ok(format!("Starting call to {}:{}", target_ip, target_port))
}

#[tauri::command]
async fn send_notification(title: String, body: String) -> Result<(), String> {
    // This will use Tauri's notification API
    println!("Notification: {} - {}", title, body);
    Ok(())
}

async fn get_gophers_from_daemon() -> Result<Vec<Gopher>> {
    let mut stream = TcpStream::connect(format!("{}:{}", DAEMON_HOST, DAEMON_PORT))?;
    
    // Send empty request to get all gophers
    stream.write_all(b"\n")?;
    
    let mut response = String::new();
    stream.read_to_string(&mut response)?;
    
    let mut gophers = Vec::new();
    for line in response.lines() {
        let parts: Vec<&str> = line.split(',').collect();
        if parts.len() == 3 {
            if let Ok(port) = parts[2].parse::<u16>() {
                gophers.push(Gopher {
                    name: parts[0].to_string(),
                    ip: parts[1].to_string(),
                    port,
                });
            }
        }
    }
    
    Ok(gophers)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        // .plugin(tauri_plugin_notification::init()) // Temporarily disabled
        .invoke_handler(tauri::generate_handler![
            get_network_peers,
            start_call,
            send_notification
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
