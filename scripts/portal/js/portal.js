/**
 * portal.js - Frontend application logic for the Proxy Web Studio Landing Page.
 */

PetiteVue.createApp({
  chains: [],
  pcaps: [],
  selectedChain: "",
  selectedBackend: "",
  selectedPcap: "",
  
  status: {
    state: "idle",       // "idle" | "running" | "stopped" | "error"
    pid: null,
    configPath: null,
    backend: null,
    uptimeSec: 0,
    exitCode: null,
  },
  
  logs: [],
  autoScroll: true,
  actionLoading: false,
  statusTimer: null,
  uptimeTimer: null,

  async init() {
    await this.fetchChains();
    await this.fetchPcaps();
    await this.pollStatus();
    
    // Periodically poll status & logs
    this.statusTimer = setInterval(() => {
      this.pollStatus();
    }, 1000);

    // Smooth uptime increment
    this.uptimeTimer = setInterval(() => {
      if (this.status.state === "running") {
        this.status.uptimeSec = (this.status.uptimeSec || 0) + 1;
      }
    }, 1000);
  },

  async fetchChains() {
    try {
      const resp = await fetch("/api/runner/chains");
      if (resp.ok) {
        this.chains = await resp.json();
        if (this.chains.length > 0 && !this.selectedChain) {
          this.selectedChain = this.chains[0].path;
          this.onChainSelected();
        }
      }
    } catch (e) {
      console.warn("Could not load chains:", e);
    }
  },

  async fetchPcaps() {
    try {
      const resp = await fetch("/api/runner/pcaps");
      if (resp.ok) {
        this.pcaps = await resp.json();
        if (this.pcaps.length > 0 && !this.selectedPcap) {
          this.selectedPcap = this.pcaps[0];
        }
      }
    } catch (e) {
      console.warn("Could not load pcaps:", e);
    }
  },

  onChainSelected() {
    const found = this.chains.find((c) => c.path === this.selectedChain);
    if (found && found.backend) {
      this.selectedBackend = found.backend;
    }
  },

  async pollStatus() {
    try {
      const resp = await fetch("/api/runner/status");
      if (!resp.ok) return;
      const data = await resp.json();
      
      this.status.state = data.state || "idle";
      this.status.pid = data.pid;
      this.status.configPath = data.configPath;
      this.status.backend = data.backend;
      this.status.uptimeSec = Math.round(data.uptimeSec || 0);
      this.status.exitCode = data.exitCode;
      
      if (Array.isArray(data.logs)) {
        this.logs = data.logs;
        this.$nextTick(() => {
          if (this.autoScroll) {
            this.scrollToBottom();
          }
        });
      }
    } catch (e) {
      // transient polling error
    }
  },

  async startChain() {
    if (!this.selectedChain) return;
    this.actionLoading = true;
    try {
      const resp = await fetch("/api/runner/start", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          configPath: this.selectedChain,
          backend: this.selectedBackend || null,
          pcap: this.selectedPcap || null,
        }),
      });
      const res = await resp.json();
      if (!resp.ok) {
        alert("Failed to start chain: " + (res.message || "Unknown error"));
      } else {
        await this.pollStatus();
      }
    } catch (e) {
      alert("Error contacting runner: " + e);
    } finally {
      this.actionLoading = false;
    }
  },

  async stopChain() {
    this.actionLoading = true;
    try {
      const resp = await fetch("/api/runner/stop", {
        method: "POST",
      });
      const res = await resp.json();
      if (!resp.ok) {
        alert("Failed to stop chain: " + (res.message || "Unknown error"));
      } else {
        await this.pollStatus();
      }
    } catch (e) {
      alert("Error stopping runner: " + e);
    } finally {
      this.actionLoading = false;
    }
  },

  async restartChain() {
    await this.stopChain();
    setTimeout(() => {
      this.startChain();
    }, 500);
  },

  clearLogs() {
    this.logs = [];
  },

  scrollToBottom() {
    const el = document.getElementById("terminal-output");
    if (el) {
      el.scrollTop = el.scrollHeight;
    }
  },

  formatUptime(sec) {
    if (!sec || sec < 0) return "00:00:00";
    const hrs = Math.floor(sec / 3600).toString().padStart(2, "0");
    const mins = Math.floor((sec % 3600) / 60).toString().padStart(2, "0");
    const s = Math.floor(sec % 60).toString().padStart(2, "0");
    return `${hrs}:${mins}:${s}`;
  },

  getLogClass(line) {
    if (line.includes("[ERROR]") || line.includes("Error:") || line.includes("Exception") || line.includes("Traceback")) {
      return "log-err";
    }
    if (line.includes("WARNING:") || line.includes("⚠️")) {
      return "log-warn";
    }
    if (line.includes("✅") || line.includes("ready") || line.includes("started") || line.includes("SUCCESS")) {
      return "log-ok";
    }
    return "log-info";
  },
}).mount();

window.toggleWorkspaceDropdown = function (e) {
  e.stopPropagation();
  const dd = e.currentTarget.closest(".workspace-dropdown");
  if (dd) dd.classList.toggle("open");
};

document.addEventListener("click", () => {
  document.querySelectorAll(".workspace-dropdown.open").forEach((el) => el.classList.remove("open"));
});

