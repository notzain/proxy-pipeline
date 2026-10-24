/**
 * Reactive application store powered by Petite-Vue.
 */
import { CanvasRenderer } from "./canvas.js";

export function createSimulationApp() {
  return {
    state: {
      chain: { backend: "sim", handlers: [] },
      pcap: "Connecting...",
      packetCount: 0,
      packets: [],
      events: [],
      currentPacketIdx: 0,
      inFlight: false,
      inFlightSummary: null,
      batchGroups: [],
      lastEvent: null,
      selectedNode: null,
      selectedPacket: null,
      selectedTraceEventId: null,
      stats: {
        delivered: 0,
        dropped: 0,
        currentStatus: "Waiting"
      }
    },

    offlineMode: false,
    offlineAllEvents: [],
    offlineStepIdx: 0,

    isPlaying: false,
    stepInterval: 1000,
    playTimer: null,
    canvasRenderer: null,

    async init() {
      const canvasEl = document.getElementById("sim-canvas");
      this.canvasRenderer = new CanvasRenderer(canvasEl, this);

      // Keyboard navigation
      window.addEventListener("keydown", (e) => {
        if (e.target.tagName === "INPUT" || e.target.tagName === "TEXTAREA") return;
        if (e.code === "Space") {
          e.preventDefault();
          this.togglePlay();
        } else if (e.code === "ArrowRight") {
          e.preventDefault();
          this.stepForward();
        } else if (e.code === "ArrowLeft") {
          e.preventDefault();
          this.stepBack();
        } else if (e.code === "KeyR") {
          e.preventDefault();
          this.resetSim();
        }
      });

      // Check if a standalone trace document was pre-injected into the page
      if (window.__INITIAL_TRACE_DOC__) {
        this.loadStandaloneTrace(window.__INITIAL_TRACE_DOC__, window.__INITIAL_TRACE_NAME__ || "trace.json");
      } else {
        await this.fetchState();
      }
    },

    async fetchState() {
      if (this.offlineMode) return;
      try {
        const resp = await fetch("/api/simulation");
        const data = await resp.json();
        this.state.chain = data.chain || { backend: "sim", handlers: [] };
        this.state.pcap = data.pcap || "Loaded";
        this.state.packetCount = data.packetCount || 0;
        this.state.packets = data.packets || [];
        this.state.events = data.events || [];
        this.state.currentPacketIdx = data.currentPacketIdx || 0;
        this.state.inFlight = !!data.inFlight;
        this.state.inFlightSummary = data.inFlightSummary || null;
        this.state.batchGroups = data.batchGroups || [];

        this.updateStats();
        if (this.canvasRenderer) {
          this.canvasRenderer.layoutPipeline();
        }

        if (!this.state.selectedPacket && this.state.packets.length > 0) {
          this.state.selectedPacket = this.state.packets[0];
        }
      } catch (err) {
        console.warn("Server fetch idle / disconnected:", err);
      }
    },

    updateStats() {
      let delivered = 0;
      let dropped = 0;
      this.state.events.forEach(ev => {
        if (ev.type === "egress") delivered++;
        if (ev.type === "drop") dropped++;
      });
      this.state.stats.delivered = delivered;
      this.state.stats.dropped = dropped;
      this.state.stats.currentStatus = this.state.inFlight
        ? "In-Flight"
        : (this.state.currentPacketIdx >= this.state.packetCount ? "Finished" : `Pkt #${this.state.currentPacketIdx + 1}`);
    },

    inspectNode(node) {
      this.state.selectedNode = node;
    },

    inspectPacket(pkt) {
      this.state.selectedPacket = pkt;
    },

    inspectTraceEvent(ev) {
      this.state.selectedTraceEventId = ev.eventId;
      this.state.lastEvent = ev;
      if (this.canvasRenderer) {
        this.canvasRenderer.triggerTransitionAnimation(ev);
      }
    },

    selectTraceItem(ev, idx) {
      if (this.isPlaying) this.togglePlay();
      this.state.selectedTraceEventId = ev.eventId;
      this.state.lastEvent = ev;

      if (this.offlineMode) {
        this.offlineStepIdx = idx + 1;
        this.state.events = this.offlineAllEvents.slice(0, this.offlineStepIdx);
        this.updateStats();
      }

      this.inspectTraceEvent(ev);
    },

    async stepForward() {
      if (this.offlineMode) {
        if (this.offlineStepIdx >= this.offlineAllEvents.length) {
          this.state.stats.currentStatus = "Trace End";
          if (this.isPlaying) this.togglePlay();
          return;
        }
        this.offlineStepIdx++;
        this.state.events = this.offlineAllEvents.slice(0, this.offlineStepIdx);
        const ev = this.offlineAllEvents[this.offlineStepIdx - 1];
        this.inspectTraceEvent(ev);
        this.updateStats();
        this.scrollTraceIntoView(this.offlineStepIdx - 1);
        return;
      }

      try {
        const resp = await fetch("/api/simulation/step", { method: "POST" });
        const res = await resp.json();
        if (res.status === "finished") {
          this.state.stats.currentStatus = "Completed";
          if (this.isPlaying) this.togglePlay();
          return;
        }
        await this.fetchState();
        if (res.event) {
          this.inspectTraceEvent(res.event);
        }
      } catch (err) {
        console.error("Step error:", err);
      }
    },

    async stepBack() {
      if (this.offlineMode) {
        if (this.isPlaying) this.togglePlay();
        if (this.offlineStepIdx <= 0) return;
        this.offlineStepIdx--;
        this.state.events = this.offlineAllEvents.slice(0, this.offlineStepIdx);
        this.updateStats();

        if (this.offlineStepIdx > 0) {
          const ev = this.offlineAllEvents[this.offlineStepIdx - 1];
          this.inspectTraceEvent(ev);
          this.scrollTraceIntoView(this.offlineStepIdx - 1);
        } else {
          this.state.lastEvent = null;
          this.state.selectedTraceEventId = null;
          if (this.canvasRenderer) this.canvasRenderer.activeParticles = [];
        }
        return;
      }

      try {
        if (this.isPlaying) this.togglePlay();
        const resp = await fetch("/api/simulation/step_back", { method: "POST" });
        const res = await resp.json();
        await this.fetchState();
        if (res.event) {
          this.inspectTraceEvent(res.event);
        } else {
          this.state.lastEvent = null;
          this.state.selectedTraceEventId = null;
          if (this.canvasRenderer) this.canvasRenderer.activeParticles = [];
        }
      } catch (err) {
        console.error("Step back error:", err);
      }
    },

    scrollTraceIntoView(idx) {
      const container = document.getElementById("trace-list");
      if (!container) return;
      const items = container.querySelectorAll(".list-item");
      if (items[idx]) {
        items[idx].scrollIntoView({ block: "nearest", behavior: "auto" });
      }
    },

    togglePlay() {
      this.isPlaying = !this.isPlaying;
      if (this.isPlaying) {
        this.playLoop();
      } else if (this.playTimer) {
        clearTimeout(this.playTimer);
        this.playTimer = null;
      }
    },

    playLoop() {
      if (!this.isPlaying) return;
      this.stepForward();
      this.playTimer = setTimeout(() => this.playLoop(), this.stepInterval);
    },

    updateSpeed(val) {
      this.stepInterval = Math.round(parseFloat(val) * 1000);
    },

    async resetSim() {
      if (this.isPlaying) this.togglePlay();
      if (this.offlineMode) {
        this.offlineStepIdx = 0;
        this.state.events = [];
        this.state.lastEvent = null;
        this.state.selectedTraceEventId = null;
        if (this.canvasRenderer) this.canvasRenderer.activeParticles = [];
        this.updateStats();
        return;
      }

      await fetch("/api/simulation/reset", { method: "POST" });
      this.state.lastEvent = null;
      this.state.selectedTraceEventId = null;
      this.state.selectedNode = null;
      if (this.canvasRenderer) this.canvasRenderer.activeParticles = [];
      await this.fetchState();
    },

    async triggerGenerator(nodeName) {
      try {
        const resp = await fetch("/api/simulation/trigger", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ node: nodeName })
        });
        const res = await resp.json();
        if (res.status === "ok") {
          await this.fetchState();
          if (res.event) this.inspectTraceEvent(res.event);
          if (res.packet) this.inspectPacket(res.packet);
        } else {
          alert(`Trigger failed: ${res.message}`);
        }
      } catch (err) {
        console.error("Trigger error:", err);
      }
    },

    handleTraceFile(event) {
      const file = event.target.files[0];
      if (!file) return;

      const reader = new FileReader();
      reader.onload = (e) => {
        try {
          const doc = JSON.parse(e.target.result);
          this.loadStandaloneTrace(doc, file.name);
        } catch (err) {
          alert(`Invalid trace JSON: ${err.message}`);
        }
      };
      reader.readAsText(file);
    },

    loadStandaloneTrace(doc, fileName) {
      if (this.isPlaying) this.togglePlay();

      let events = [];
      let topology = null;

      if (Array.isArray(doc)) {
        events = doc;
      } else if (typeof doc === "object") {
        events = doc.events || [];
        topology = doc.topology || null;
      }

      let handlers = [];
      let backendName = "sim";

      if (topology && topology.handlers && topology.handlers.length > 0) {
        handlers = topology.handlers;
        backendName = topology.backend || "sim";
      } else {
        const nodeMap = new Map();
        events.forEach(ev => {
          if (ev.target && !nodeMap.has(ev.target)) {
            nodeMap.set(ev.target, {
              name: ev.target,
              type: ev.targetType || "handler",
              input_channel: ev.channel,
              output_channel: null
            });
          }
          if (ev.source && !nodeMap.has(ev.source) && !ev.source.includes(":") && ev.source !== "ingress") {
            nodeMap.set(ev.source, {
              name: ev.source,
              type: "handler",
              input_channel: null,
              output_channel: ev.channel
            });
          }
        });
        handlers = Array.from(nodeMap.values());
      }

      this.offlineMode = true;
      this.offlineAllEvents = events;
      this.offlineStepIdx = events.length > 0 ? 1 : 0;

      this.state.chain = { backend: backendName, handlers };
      this.state.pcap = fileName || "trace.json";
      this.state.packetCount = 0;
      this.state.packets = [];
      this.state.events = this.offlineAllEvents.slice(0, this.offlineStepIdx);
      this.state.currentPacketIdx = 0;
      this.state.inFlight = false;
      this.state.inFlightSummary = null;

      this.updateStats();
      if (this.canvasRenderer) {
        this.canvasRenderer.layoutPipeline();
      }

      if (this.state.events.length > 0) {
        const firstEv = this.state.events[0];
        this.inspectTraceEvent(firstEv);
      }
    }
  };
}

