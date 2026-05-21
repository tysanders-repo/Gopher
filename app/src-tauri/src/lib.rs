use anyhow::Result;
use serde::{Deserialize, Serialize};
use std::io::{Read, Write};
use std::net::{TcpStream, UdpSocket};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

#[derive(Debug, Serialize, Deserialize, Clone)]
pub struct Gopher {
    pub name: String,
    pub ip: String,
    pub port: u16,
}

const DAEMON_HOST: &str = "127.0.0.1";
const DAEMON_PORT: u16 = 43823;
const BROADCAST_PORT: u16 = 43753;

struct GopherBroadcaster {
    name: String,
    ip: String,
    port: u16,
    running: Arc<Mutex<bool>>,
}

impl GopherBroadcaster {
    fn new(name: String, ip: String, port: u16) -> Self {
        Self {
            name,
            ip,
            port,
            running: Arc::new(Mutex::new(false)),
        }
    }

    fn start(&self) -> Result<()> {
        let mut running = self.running.lock().unwrap();
        if *running {
            return Ok(());
        }
        *running = true;
        drop(running);

        let socket = UdpSocket::bind("0.0.0.0:0")?;
        socket.set_broadcast(true)?;

        let message = format!("name:{};ip:{};port:{}", self.name, self.ip, self.port);
        let running_clone = Arc::clone(&self.running);

        thread::spawn(move || {
            let broadcast_addr = format!("255.255.255.255:{}", BROADCAST_PORT);

            while *running_clone.lock().unwrap() {
                if let Err(e) = socket.send_to(message.as_bytes(), &broadcast_addr) {
                    eprintln!("Failed to send broadcast: {}", e);
                }

                thread::sleep(Duration::from_secs(5)); // Broadcast every 5 seconds
            }
        });

        Ok(())
    }

    fn stop(&self) {
        let mut running = self.running.lock().unwrap();
        *running = false;
    }
}

// Global broadcaster instance
static mut BROADCASTER: Option<GopherBroadcaster> = None;

#[tauri::command]
async fn initialize_gopher(name: String) -> Result<String, String> {
    unsafe {
        if BROADCASTER.is_some() {
            return Err("Gopher already initialized".to_string());
        }

        // Get local IP address
        let local_ip = get_local_ip().unwrap_or_else(|| "127.0.0.1".to_string());

        // For now, use a random port. In a full implementation, this would
        // create a listening socket and get the actual port
        let port = 12345; // TODO: Integrate with C++ engine to get actual listening port

        let broadcaster = GopherBroadcaster::new(name.clone(), local_ip.clone(), port);

        if let Err(e) = broadcaster.start() {
            return Err(format!("Failed to start broadcaster: {}", e));
        }

        BROADCASTER = Some(broadcaster);

        Ok(format!(
            "Initialized Gopher '{}' at {}:{}",
            name, local_ip, port
        ))
    }
}

#[tauri::command]
async fn shutdown_gopher() -> Result<String, String> {
    unsafe {
        if let Some(broadcaster) = &BROADCASTER {
            broadcaster.stop();
            BROADCASTER = None;
            Ok("Gopher shut down".to_string())
        } else {
            Err("No Gopher instance running".to_string())
        }
    }
}

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

fn get_local_ip() -> Option<String> {
    use std::net::{ToSocketAddrs, UdpSocket};

    // Try to connect to a remote address to determine local IP
    let socket = UdpSocket::bind("0.0.0.0:0").ok()?;
    socket.connect("8.8.8.8:80").ok()?;
    let local_addr = socket.local_addr().ok()?;
    Some(local_addr.ip().to_string())
}

async fn get_gophers_from_daemon() -> Result<Vec<Gopher>> {
    use std::io::BufRead;
    use std::io::BufReader;

    println!(
        "Attempting to connect to daemon at {}:{}",
        DAEMON_HOST, DAEMON_PORT
    );

    let mut stream = TcpStream::connect(format!("{}:{}", DAEMON_HOST, DAEMON_PORT))?;
    println!("Connected to daemon successfully");

    // Send empty request to get all gophers
    stream.write_all(b"\n")?;
    println!("Sent request to daemon");

    // Use BufReader to read line by line instead of waiting for connection close
    let mut reader = BufReader::new(&mut stream);
    let mut response = String::new();
    let mut gophers = Vec::new();

    // Read lines until we get an empty line or connection closes
    loop {
        let mut line = String::new();
        match reader.read_line(&mut line) {
            Ok(0) => break, // EOF
            Ok(_) => {
                let line = line.trim();
                if line.is_empty() {
                    break; // Empty line signals end
                }

                response.push_str(line);
                response.push('\n');

                // Parse the line immediately
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
            Err(e) => {
                println!("Error reading from daemon: {}", e);
                break;
            }
        }
    }

    println!("Received response from daemon: '{}'", response);
    println!("Parsed {} gophers from daemon response", gophers.len());
    Ok(gophers)
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        // .plugin(tauri_plugin_notification::init()) // Temporarily disabled
        .invoke_handler(tauri::generate_handler![
            initialize_gopher,
            shutdown_gopher,
            get_network_peers,
            start_call,
            send_notification
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}
