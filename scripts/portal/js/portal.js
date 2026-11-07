/**
 * portal.js - Frontend application logic for the Proxy Web Studio Landing Page.
 */

PetiteVue.createApp({
  chains: [],
  pcaps: [],
  selectedChain: "",
  selectedBackend: "sim",
  selectedPcap: "",
  chainsLoading: false,
  pcapsLoading: false,
  chainsError: "",
  pcapsError: "",
  runnerError: "",
  actionError: "",
  actionMessage: "",
  
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
  terminalScrollThreshold: 24,
  selectedTerminalSource: "",
  actionLoading: false,
  statusTimer: null,
  uptimeTimer: null,
  statusInitialized: false,

  get terminalTabs() {
    const sources = new Map();
    for (const line of this.logs) {
      const source = this.getLogSource(line);
      sources.set(source, (sources.get(source) || 0) + 1);
    }

    return [
      { source: "", label: "All output", count: this.logs.length },
      ...Array.from(sources, ([source, count]) => ({ source, label: source, count }))
        .sort((left, right) => left.label.localeCompare(right.label)),
    ];
  },

  get filteredLogs() {
    if (!this.selectedTerminalSource) return this.logs;
    return this.logs.filter((line) => this.getLogSource(line) === this.selectedTerminalSource);
  },

  get selectedTerminalTabIndex() {
    return this.terminalTabs.findIndex((tab) => tab.source === this.selectedTerminalSource);
  },

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
    this.chainsLoading = true;
    this.chainsError = "";
    try {
      const resp = await fetch("/api/runner/chains");
      if (!resp.ok) {
        throw new Error(`Could not load chains (HTTP ${resp.status})`);
      }
      this.chains = await resp.json();
      if (this.chains.length > 0 && !this.chains.some((chain) => chain.path === this.selectedChain)) {
        this.selectedChain = this.chains[0].path;
        this.onChainSelected();
      } else if (this.chains.length === 0) {
        this.selectedChain = "";
      }
    } catch (e) {
      this.chainsError = e.message || String(e);
    } finally {
      this.chainsLoading = false;
    }
  },

  async fetchPcaps() {
    this.pcapsLoading = true;
    this.pcapsError = "";
    try {
      const resp = await fetch("/api/runner/pcaps");
      if (!resp.ok) {
        throw new Error(`Could not load PCAP captures (HTTP ${resp.status})`);
      }
      this.pcaps = await resp.json();
    } catch (e) {
      this.pcapsError = e.message || String(e);
    } finally {
      this.pcapsLoading = false;
    }
  },

  onChainSelected() {
    const found = this.chains.find((c) => c.path === this.selectedChain);
    if (found && found.backend) {
      this.selectedBackend = found.backend;
    } else if (!this.selectedBackend) {
      this.selectedBackend = "sim";
    }
    window.ProxyStudioContext?.set(this.selectedChain, this.selectedBackend);
  },

  async pollStatus() {
    try {
      const resp = await fetch("/api/runner/status");
      if (!resp.ok) {
        throw new Error(`Runner status unavailable (HTTP ${resp.status})`);
      }
      const data = await resp.json();
      this.runnerError = "";
      
      this.status.state = data.state || "idle";
      this.status.pid = data.pid;
      this.status.configPath = data.configPath;
      this.status.backend = data.backend;
      this.status.uptimeSec = Math.round(data.uptimeSec || 0);
      this.status.exitCode = data.exitCode;
      if (!this.statusInitialized && data.configPath) {
        const activeChain = this.chains.find((chain) => chain.path === data.configPath);
        if (activeChain) {
          this.selectedChain = activeChain.path;
          this.selectedBackend = data.backend || activeChain.backend;
        }
      }
      this.statusInitialized = true;
      
      if (Array.isArray(data.logs)) {
        this.logs = data.logs;
        this.$nextTick(() => {
          if (this.autoScroll) {
            this.scrollToBottom();
          }
        });
      }
      return true;
    } catch (e) {
      this.runnerError = e.message || String(e);
      return false;
    }
  },

  async startChain() {
    if (!this.selectedChain) {
      this.actionError = "Select a chain configuration before starting.";
      return false;
    }
    this.actionLoading = true;
    this.actionError = "";
    this.actionMessage = "";
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
        throw new Error(res.message || `Start failed (HTTP ${resp.status})`);
      } else {
        await this.pollStatus();
        this.actionMessage = "Chain start requested.";
        return true;
      }
    } catch (e) {
      this.actionError = e.message || String(e);
      return false;
    } finally {
      this.actionLoading = false;
    }
  },

  async stopChain() {
    this.actionLoading = true;
    this.actionError = "";
    this.actionMessage = "";
    try {
      const resp = await fetch("/api/runner/stop", {
        method: "POST",
      });
      const res = await resp.json();
      if (!resp.ok) {
        throw new Error(res.message || `Stop failed (HTTP ${resp.status})`);
      } else {
        if (!await this.pollStatus()) return false;
        if (this.status.state === "running") {
          throw new Error("The runner is still active; restart was not attempted.");
        }
        this.actionMessage = "Chain stop requested.";
        return true;
      }
    } catch (e) {
      this.actionError = e.message || String(e);
      return false;
    } finally {
      this.actionLoading = false;
    }
  },

  async restartChain() {
    if (await this.stopChain()) await this.startChain();
  },

  editSelectedChain() {
    if (this.selectedChain) {
      window.location.href = `/builder/?chain=${encodeURIComponent(this.selectedChain)}`;
    }
  },

  clearLogs() {
    this.logs = [];
    this.selectedTerminalSource = "";
  },

  getLogSource(line) {
    const match = String(line).match(/^\[([^\]]+)\]/);
    return match ? match[1] : "System";
  },

  selectTerminalSource(source) {
    this.selectedTerminalSource = source;
    if (this.autoScroll) {
      this.$nextTick(() => this.scrollToBottom());
    }
  },

  onTerminalTabKeydown(event) {
    const tabs = Array.from(event.currentTarget.parentElement.querySelectorAll('[role="tab"]'));
    const index = tabs.indexOf(event.currentTarget);
    let nextIndex = index;

    if (event.key === "ArrowRight") nextIndex = (index + 1) % tabs.length;
    else if (event.key === "ArrowLeft") nextIndex = (index - 1 + tabs.length) % tabs.length;
    else if (event.key === "Home") nextIndex = 0;
    else if (event.key === "End") nextIndex = tabs.length - 1;
    else return;

    event.preventDefault();
    const nextTab = tabs[nextIndex];
    this.selectTerminalSource(nextTab.dataset.source || "");
    nextTab.focus();
  },

  scrollToBottom() {
    const el = document.getElementById("terminal-output");
    if (el) {
      el.scrollTop = el.scrollHeight;
    }
  },

  onTerminalScroll(event) {
    const el = event.currentTarget;
    const distanceFromBottom = el.scrollHeight - el.clientHeight - el.scrollTop;
    this.autoScroll = distanceFromBottom <= this.terminalScrollThreshold;
  },

  onAutoScrollChange() {
    if (this.autoScroll) {
      this.$nextTick(() => this.scrollToBottom());
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
