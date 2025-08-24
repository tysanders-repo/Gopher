const { invoke } = window.__TAURI__.core;

let peers = [];
let refreshInterval;

// DOM elements
let statusIndicator;
let statusText;
let refreshBtn;
let shutdownBtn;
let peersList;
let notificationArea;
let gopherSetup;
let statusSection;
let gopherNameInput;
let initializeBtn;

// Gopher state
let isGopherInitialized = false;

async function initializeGopher() {
  try {
    const name = gopherNameInput.value.trim();
    if (!name) {
      showNotification("Error", "Please enter a name for your Gopher", "error");
      return;
    }

    initializeBtn.disabled = true;
    initializeBtn.textContent = "Initializing...";

    const result = await invoke("initialize_gopher", { name });
    
    isGopherInitialized = true;
    showGopherStatus();
    showNotification("Success", result, "success");
    
    // Start loading peers
    loadNetworkPeers();
    
  } catch (error) {
    console.error("Failed to initialize Gopher:", error);
    showNotification("Error", error, "error");
  } finally {
    initializeBtn.disabled = false;
    initializeBtn.textContent = "Initialize Gopher";
  }
}

async function shutdownGopher() {
  try {
    await invoke("shutdown_gopher");
    isGopherInitialized = false;
    showGopherSetup();
    showNotification("Success", "Gopher shut down successfully", "success");
    
    // Stop auto-refresh
    if (refreshInterval) {
      clearInterval(refreshInterval);
      refreshInterval = null;
    }
    
  } catch (error) {
    console.error("Failed to shutdown Gopher:", error);
    showNotification("Error", error, "error");
  }
}

function showGopherSetup() {
  gopherSetup.style.display = "block";
  statusSection.style.display = "none";
}

function showGopherStatus() {
  gopherSetup.style.display = "none";
  statusSection.style.display = "flex";
}

async function loadNetworkPeers() {
  if (!isGopherInitialized) return;
  
  try {
    statusText.textContent = "Loading...";
    statusIndicator.className = "indicator";
    
    const networkPeers = await invoke("get_network_peers");
    peers = networkPeers;
    
    updatePeersDisplay();
    updateConnectionStatus(true);
    
  } catch (error) {
    console.error("Failed to load network peers:", error);
    updateConnectionStatus(false);
    showEmptyState("Failed to connect to daemon. Make sure gopherd is running.");
  }
}

function updatePeersDisplay() {
  if (peers.length === 0) {
    showEmptyState("No peers found on the network.");
    return;
  }

  peersList.innerHTML = peers.map(peer => `
    <div class="peer-card">
      <div class="peer-info">
        <h3 class="peer-name">${escapeHtml(peer.name)}</h3>
        <p class="peer-address">${escapeHtml(peer.ip)}:${peer.port}</p>
      </div>
      <div class="peer-actions">
        <button class="btn btn-primary" onclick="startCall('${escapeHtml(peer.ip)}', ${peer.port})">
          📞 Call
        </button>
      </div>
    </div>
  `).join('');
}

function showEmptyState(message) {
  peersList.innerHTML = `<div class="empty-state">${escapeHtml(message)}</div>`;
}

function updateConnectionStatus(connected) {
  if (connected) {
    statusIndicator.className = "indicator connected";
    statusText.textContent = `Connected - ${peers.length} peer${peers.length !== 1 ? 's' : ''} found`;
  } else {
    statusIndicator.className = "indicator disconnected";
    statusText.textContent = "Disconnected from daemon";
  }
}

async function startCall(ip, port) {
  try {
    const result = await invoke("start_call", { targetIp: ip, targetPort: port });
    showNotification("Call Started", result, "success");
  } catch (error) {
    console.error("Failed to start call:", error);
    showNotification("Call Failed", error, "error");
  }
}

function showNotification(title, message, type = "info") {
  const notification = document.createElement('div');
  notification.className = `notification ${type === "incoming-call" ? "incoming-call" : ""}`;
  
  notification.innerHTML = `
    <div class="notification-header">
      <h4 class="notification-title">${escapeHtml(title)}</h4>
    </div>
    <p class="notification-body">${escapeHtml(message)}</p>
    <div class="notification-actions">
      <button class="btn btn-secondary" onclick="closeNotification(this)">Close</button>
    </div>
  `;
  
  notificationArea.appendChild(notification);
  
  // Auto-remove after 5 seconds
  setTimeout(() => {
    if (notification.parentNode) {
      closeNotification(notification.querySelector('.btn-secondary'));
    }
  }, 5000);
}

function showIncomingCallNotification(callerName, callerIp, callerPort) {
  const notification = document.createElement('div');
  notification.className = 'notification incoming-call';
  
  notification.innerHTML = `
    <div class="notification-header">
      <h4 class="notification-title">📞 Incoming Call</h4>
    </div>
    <p class="notification-body">Call from ${escapeHtml(callerName)} (${escapeHtml(callerIp)}:${callerPort})</p>
    <div class="notification-actions">
      <button class="btn btn-success" onclick="acceptCall('${escapeHtml(callerIp)}', ${callerPort}); closeNotification(this);">Accept</button>
      <button class="btn btn-secondary" onclick="closeNotification(this)">Decline</button>
    </div>
  `;
  
  notificationArea.appendChild(notification);
}

async function acceptCall(ip, port) {
  try {
    const result = await invoke("start_call", { targetIp: ip, targetPort: port });
    showNotification("Call Accepted", result, "success");
  } catch (error) {
    console.error("Failed to accept call:", error);
    showNotification("Call Failed", error, "error");
  }
}

function closeNotification(button) {
  const notification = button.closest('.notification');
  if (notification) {
    notification.remove();
  }
}

function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

// Make functions globally available
window.startCall = startCall;
window.acceptCall = acceptCall;
window.closeNotification = closeNotification;

// Initialize app
window.addEventListener("DOMContentLoaded", () => {
  // Get DOM elements
  statusIndicator = document.getElementById("connection-status");
  statusText = document.getElementById("status-text");
  refreshBtn = document.getElementById("refresh-btn");
  shutdownBtn = document.getElementById("shutdown-btn");
  peersList = document.getElementById("peers-list");
  notificationArea = document.getElementById("notification-area");
  gopherSetup = document.getElementById("gopher-setup");
  statusSection = document.getElementById("status-section");
  gopherNameInput = document.getElementById("gopher-name");
  initializeBtn = document.getElementById("initialize-btn");
  
  // Add event listeners
  initializeBtn.addEventListener("click", initializeGopher);
  refreshBtn.addEventListener("click", loadNetworkPeers);
  shutdownBtn.addEventListener("click", shutdownGopher);
  
  // Allow Enter key to initialize Gopher
  gopherNameInput.addEventListener("keypress", (e) => {
    if (e.key === "Enter") {
      initializeGopher();
    }
  });
  
  // Start with setup view
  showGopherSetup();
});

// Cleanup on page unload
window.addEventListener("beforeunload", () => {
  if (refreshInterval) {
    clearInterval(refreshInterval);
  }
});