/**
 * Reactive application store powered by Petite-Vue.
 * Supports two distinct modes:
 *   1. "realtime": Live SimBackend execution where packets are loaded from PCAP and stepped forward into handlers.
 *   2. "trace": Standalone execution trace replay (export.trace.json) supporting forward and backward stepping without a running backend.
 */
import { CanvasRenderer } from "./canvas.js?v=2";

export function createSimulationApp() {
  return {
    mode: "realtime", // "realtime" | "trace"
    realtimeAvailable: false,

    state: {
      chain: { backend: "sim", handlers: [] },
      pcap: "Connecting...",
      packetCount: 0,
      packets: [],
      events: [],
      currentPacketIdx: 0,
      inFlight: false,
      inFlightStage: null,
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

    traceAllEvents: [],
    traceStepIdx: 0,

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
          if (this.mode === "realtime" && !this.state.inFlight) {
            this.loadNextPacket();
          } else {
            this.stepForward();
          }
        } else if (e.code === "ArrowLeft") {
          if (this.mode === "trace") {
            e.preventDefault();
            this.stepBack();
          }
        } else if (e.code === "KeyR") {
          e.preventDefault();
          this.resetSim();
        }
      });

      // Check if a standalone trace document was pre-injected into the page (--view-trace)
      if (window.__INITIAL_TRACE_DOC__) {
        this.loadStandaloneTrace(
          window.__INITIAL_TRACE_DOC__,
          window.__INITIAL_TRACE_NAME__ || "export.trace.json"
        );
      } else {
        await this.fetchState();
      }
    },

    async fetchState() {
      if (this.mode === "trace" && !this.realtimeAvailable) return;
      try {
        const resp = await fetch("/api/simulation");
        const data = await resp.json();

        if (data.mode === "trace" && !this.realtimeAvailable) {
          this.loadStandaloneTrace(
            {
              topology: data.chain,
              packets: data.packets || [],
              events: data.events || []
            },
            data.pcap || "export.trace.json"
          );
          return;
        }

        this.realtimeAvailable = true;
        this.mode = "realtime";
        this.state.chain = data.chain || { backend: "sim", handlers: [] };
        this.state.pcap = data.pcap || "Loaded";
        this.state.packetCount = data.packetCount || 0;
        this.state.packets = data.packets || [];
        this.state.events = data.events || [];
        this.state.currentPacketIdx = data.currentPacketIdx || 0;
        this.state.inFlight = !!data.inFlight;
        this.state.inFlightStage = data.inFlightStage || null;
        this.state.inFlightSummary = data.inFlightSummary || null;
        this.state.batchGroups = data.batchGroups || [];

        this.updateStats();
        if (this.canvasRenderer) {
          this.canvasRenderer.layoutPipeline();
        }

        if (!this.state.selectedPacket && this.state.packets.length > 0) {
          const nextIdx = Math.min(this.state.currentPacketIdx, this.state.packets.length - 1);
          this.state.selectedPacket = this.state.packets[Math.max(0, nextIdx)];
        }
      } catch (err) {
        console.warn("Server fetch idle / disconnected:", err);
      }
    },

    updateStats() {
      let delivered = 0;
      let dropped = 0;
      this.state.events.forEach(ev => {
        if (ev.type === "egress" || ev.result === "Consumed") delivered++;
        if (ev.type === "drop" || ev.result === "Dropped") dropped++;
      });
      this.state.stats.delivered = delivered;
      this.state.stats.dropped = dropped;

      if (this.mode === "trace") {
        if (this.traceAllEvents.length === 0) {
          this.state.stats.currentStatus = "Empty Trace";
        } else if (this.traceStepIdx <= 0) {
          this.state.stats.currentStatus = "Ready (Step 0)";
        } else if (this.traceStepIdx >= this.traceAllEvents.length) {
          this.state.stats.currentStatus = "Trace Complete";
        } else {
          const curEv = this.traceAllEvents[this.traceStepIdx - 1];
          this.state.stats.currentStatus = `Pkt #${curEv.msgId} (${curEv.result || curEv.type})`;
        }
        return;
      }

      if (this.state.inFlight) {
        if (this.state.inFlightStage === "loaded") {
          this.state.stats.currentStatus = "Staged @ Ingress";
        } else if (this.state.inFlightSummary && this.state.inFlightSummary.channel) {
          this.state.stats.currentStatus = `Held @ ${this.state.inFlightSummary.channel}`;
        } else {
          this.state.stats.currentStatus = "In-Flight";
        }
      } else if (this.state.currentPacketIdx >= this.state.packetCount && this.state.packetCount > 0) {
        this.state.stats.currentStatus = "All Completed";
      } else {
        this.state.stats.currentStatus = `Ready (Next #${this.state.currentPacketIdx + 1})`;
      }
    },

    inspectNode(node) {
      this.state.selectedNode = node;
    },

    inspectPacket(pkt) {
      this.state.selectedPacket = pkt;
    },

    syncSelectedPacketForEvent(ev) {
      if (!ev || !ev.msgId || !this.state.packets) return;
      const matched = this.state.packets.find(p => p.id === ev.msgId);
      if (matched) {
        this.state.selectedPacket = matched;
      }
    },

    inspectTraceEvent(ev) {
      if (!ev) return;
      this.state.selectedTraceEventId = ev.eventId;
      this.state.lastEvent = ev;
      this.syncSelectedPacketForEvent(ev);
      if (this.canvasRenderer) {
        this.canvasRenderer.triggerTransitionAnimation(ev);
      }
    },

    selectTraceItem(ev, idx) {
      if (this.isPlaying) this.togglePlay();
      this.state.selectedTraceEventId = ev.eventId;
      this.state.lastEvent = ev;

      if (this.mode === "trace") {
        this.traceStepIdx = idx + 1;
        this.state.events = this.traceAllEvents.slice(0, this.traceStepIdx);
        this.updateStats();
      }

      this.inspectTraceEvent(ev);
    },

    async loadNextPacket() {
      if (this.mode !== "realtime") return;
      try {
        const resp = await fetch("/api/simulation/load_packet", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({})
        });
        const res = await resp.json();
        if (res.status === "finished") {
          this.state.stats.currentStatus = "All Completed";
          if (this.isPlaying) this.togglePlay();
          return;
        }
        await this.fetchState();
        if (res.event) {
          this.inspectTraceEvent(res.event);
          this.scrollTraceIntoView(this.state.events.length - 1);
        }
      } catch (err) {
        console.error("Load packet error:", err);
      }
    },

    async loadSpecificPacket(pktId) {
      if (this.mode !== "realtime") return;
      try {
        const resp = await fetch("/api/simulation/load_packet", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ id: pktId })
        });
        const res = await resp.json();
        await this.fetchState();
        if (res.event) {
          this.inspectTraceEvent(res.event);
          this.scrollTraceIntoView(this.state.events.length - 1);
        }
      } catch (err) {
        console.error("Load specific packet error:", err);
      }
    },

    async stepForward() {
      // TRACE SIMULATION MODE: Step forward in recorded trace
      if (this.mode === "trace") {
        if (this.traceStepIdx >= this.traceAllEvents.length) {
          this.updateStats();
          if (this.isPlaying) this.togglePlay();
          return;
        }
        this.traceStepIdx++;
        this.state.events = this.traceAllEvents.slice(0, this.traceStepIdx);
        const ev = this.traceAllEvents[this.traceStepIdx - 1];
        this.inspectTraceEvent(ev);
        this.updateStats();
        this.scrollTraceIntoView(this.traceStepIdx - 1);
        return;
      }

      // REAL-TIME SIMULATION MODE: Step in-flight packet into next handler
      try {
        const resp = await fetch("/api/simulation/step", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ autoLoad: false })
        });
        const res = await resp.json();
        if (res.status === "finished") {
          this.state.stats.currentStatus = "All Completed";
          if (this.isPlaying) this.togglePlay();
          return;
        }
        await this.fetchState();
        if (res.event) {
          this.inspectTraceEvent(res.event);
          this.scrollTraceIntoView(this.state.events.length - 1);
        }
      } catch (err) {
        console.error("Step error:", err);
      }
    },

    async stepBack() {
      // Step Back is exclusively supported in Trace Simulation Mode
      if (this.mode !== "trace") return;
      if (this.isPlaying) this.togglePlay();
      if (this.traceStepIdx <= 0) return;

      this.traceStepIdx--;
      this.state.events = this.traceAllEvents.slice(0, this.traceStepIdx);
      this.updateStats();

      if (this.traceStepIdx > 0) {
        const ev = this.traceAllEvents[this.traceStepIdx - 1];
        this.inspectTraceEvent(ev);
        this.scrollTraceIntoView(this.traceStepIdx - 1);
      } else {
        this.state.lastEvent = null;
        this.state.selectedTraceEventId = null;
        if (this.canvasRenderer) this.canvasRenderer.activeParticles = [];
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

    async playLoop() {
      if (!this.isPlaying) return;
      if (this.mode === "realtime") {
        if (!this.state.inFlight) {
          if (this.state.currentPacketIdx >= this.state.packetCount) {
            this.isPlaying = false;
            return;
          }
          await this.loadNextPacket();
        } else {
          await this.stepForward();
        }
      } else {
        await this.stepForward();
      }
      if (this.isPlaying) {
        this.playTimer = setTimeout(() => this.playLoop(), this.stepInterval);
      }
    },

    updateSpeed(val) {
      this.stepInterval = Math.round(parseFloat(val) * 1000);
    },

    async resetSim() {
      if (this.isPlaying) this.togglePlay();
      if (this.mode === "trace") {
        this.traceStepIdx = 0;
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
      if (this.mode !== "realtime") return;
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

    async exportTrace() {
      try {
        const resp = await fetch("/api/simulation/export_trace");
        const doc = await resp.json();
        const blob = new Blob([JSON.stringify(doc, null, 2)], { type: "application/json" });
        const url = URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;
        a.download = "export.trace.json";
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
      } catch (err) {
        console.error("Export trace error:", err);
      }
    },

    async switchToTraceModeFromLive() {
      try {
        const resp = await fetch("/api/simulation/export_trace");
        const doc = await resp.json();
        this.loadStandaloneTrace(doc, "export.trace.json");
      } catch (err) {
        console.error("Failed to switch to Trace Mode:", err);
      }
    },

    async returnToRealtimeMode() {
      if (this.isPlaying) this.togglePlay();
      this.mode = "realtime";
      await this.fetchState();
      if (this.state.events.length > 0) {
        this.inspectTraceEvent(this.state.events[this.state.events.length - 1]);
      } else {
        this.state.lastEvent = null;
        this.state.selectedTraceEventId = null;
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
      event.target.value = "";
    },

    loadStandaloneTrace(doc, fileName) {
      if (this.isPlaying) this.togglePlay();

      let rawEvents = [];
      let topology = null;
      let packets = [];

      if (Array.isArray(doc)) {
        rawEvents = doc;
      } else if (typeof doc === "object" && doc !== null) {
        rawEvents = doc.events || [];
        topology = doc.topology || null;
        packets = doc.packets || [];
      }

      const events = rawEvents.map((ev, idx) => ({
        ...ev,
        eventId: ev.eventId !== undefined ? ev.eventId : idx + 1
      }));

      // Synthesize packet list from trace events if not explicitly embedded in trace JSON
      if (!packets || packets.length === 0) {
        const pktMap = new Map();
        events.forEach(ev => {
          if (ev.msgId !== undefined && !pktMap.has(ev.msgId)) {
            let srcIp = "127.0.0.1";
            let srcPort = 0;
            if (ev.source && ev.source.includes(":")) {
              const parts = ev.source.split(":");
              srcIp = parts[0];
              srcPort = parseInt(parts[1], 10) || 0;
            }
            const payloadFull = ev.payloadFull || ev.payloadSummary || "";
            pktMap.set(ev.msgId, {
              id: ev.msgId,
              timestampNs: ev.timestampNs || 0,
              srcIp,
              srcPort,
              dstIp: "127.0.0.1",
              dstPort: 0,
              size: payloadFull.length,
              payloadSummary: ev.payloadSummary || payloadFull.slice(0, 60),
              payloadFull
            });
          }
        });
        packets = Array.from(pktMap.values());
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
              input_channel: ev.type === "ingress" ? ev.channel : null,
              output_channel: ev.type === "hop" ? ev.channel : null
            });
          } else if (ev.target && nodeMap.has(ev.target)) {
            const existing = nodeMap.get(ev.target);
            if (ev.type === "hop" && ev.channel && !existing.output_channel) {
              existing.output_channel = ev.channel;
            }
            if (ev.type !== "hop" && ev.channel && !existing.input_channel) {
              existing.input_channel = ev.channel;
            }
          }
        });
        handlers = Array.from(nodeMap.values());
      }

      this.mode = "trace";
      this.traceAllEvents = events;
      this.traceStepIdx = events.length > 0 ? 1 : 0;

      this.state.chain = { backend: backendName, handlers };
      this.state.pcap = fileName || "export.trace.json";
      this.state.packetCount = packets.length;
      this.state.packets = packets;
      this.state.events = this.traceAllEvents.slice(0, this.traceStepIdx);
      this.state.currentPacketIdx = 0;
      this.state.inFlight = false;
      this.state.inFlightStage = null;
      this.state.inFlightSummary = null;

      this.updateStats();
      if (this.canvasRenderer) {
        this.canvasRenderer.layoutPipeline();
      }

      if (this.state.events.length > 0) {
        const firstEv = this.state.events[0];
        this.inspectTraceEvent(firstEv);
      } else {
        this.state.lastEvent = null;
        this.state.selectedTraceEventId = null;
      }
    }
  };
}
