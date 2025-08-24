const { invoke } = window.__TAURI__.core;

let peers = [];
let refreshInterval;

// DOM elements
let statusIndicator;
let statusText;
let refreshBtn;
let peersList;
let notificationArea;

async function loadNetworkPeers() {
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
  peersList = document.getElementById("peers-list");
  notificationArea = document.getElementById("notification-area");
  
  // Add event listeners
  refreshBtn.addEventListener("click", loadNetworkPeers);
  
  // Initial load
  loadNetworkPeers();
  
  // Set up auto-refresh every 10 seconds
  refreshInterval = setInterval(loadNetworkPeers, 10000);
  
  // Simulate an incoming call notification for demo purposes
  setTimeout(() => {
    showIncomingCallNotification("Demo User", "192.168.1.100", 12345);
  }, 3000);
});

// Cleanup on page unload
window.addEventListener("beforeunload", () => {
  if (refreshInterval) {
    clearInterval(refreshInterval);
  }
});