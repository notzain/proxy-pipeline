/**
 * Reactive application store powered by Petite-Vue.
 * Supports two distinct modes:
 *   1. "realtime": Live SimBackend execution where packets are loaded from PCAP and stepped forward into handlers.
 *   2. "trace": Standalone execution trace replay (export.trace.json) supporting forward and backward stepping without a running backend.
 */
import { CanvasRenderer } from "./canvas.js?v=7";

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
          this.stepForward();
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
      if (!pkt) {
        this.state.selectedPacket = null;
        return;
      }
      // Check if this packet is part of a batch group
      const allGroups = (this.state.batchGroups || []);
      const foundGroup = allGroups.find(grp => grp.includes(pkt.id));
      if (foundGroup && foundGroup.length > 1) {
        const batchPkts = this.state.packets.filter(p => foundGroup.includes(p.id));
        const totalSize = batchPkts.reduce((sum, p) => sum + (p.size || 0), 0);
        const fullPayload = batchPkts.map(p => `[Message #${p.id} (${p.size}B)]:\n${p.payloadFull || p.payloadSummary}`).join("\n\n---\n\n");
        this.state.selectedPacket = {
          ...pkt,
          batchCount: foundGroup.length,
          batchIds: foundGroup,
          size: totalSize,
          payloadFull: fullPayload,
        };
      } else {
        this.state.selectedPacket = pkt;
      }
    },

    getEventHandler(ev) {
      if (!ev) return "";
      return ev.handler || ev.target || "";
    },

    getPacketBatchTag(pktId) {
      const allGroups = (this.state.batchGroups || []);
      const found = allGroups.find(grp => grp.includes(pktId));
      if (found && found.length > 1) {
        return `📦 BATCH [${found.join(', ')}]`;
      }
      return null;
    },

    getEventHandlerType(ev) {
      if (!ev) return "handler";
      return ev.handlerType || ev.targetType || "handler";
    },

    getEventIncomingChannel(ev) {
      if (!ev) return "";
      if (ev.inputChannel) return ev.inputChannel;
      if (ev.type === "ingress") {
        return ev.channel || "udp_network";
      }
      const handlerName = this.getEventHandler(ev);
      if (ev.source === "ingress") {
        if (ev.outputChannel) return ev.channel || "udp_network";
        const handlers = (this.state.chain && this.state.chain.handlers) || [];
        const targetH = handlers.find(h => h.name === handlerName);
        if (targetH && targetH.input_channel) return targetH.input_channel;
        return "udp_network";
      }
      if (ev.outputChannel || ev.type !== "hop") {
        return ev.channel || "";
      }
      const handlers = (this.state.chain && this.state.chain.handlers) || [];
      const dstH = handlers.find(h => h.name === handlerName);
      if (dstH && dstH.input_channel) return dstH.input_channel;
      return ev.channel || "";
    },

    getEventOutgoingChannel(ev) {
      if (!ev) return "";
      if (ev.outputChannel) return ev.outputChannel;
      if (ev.type === "hop") return ev.channel || "";
      return "";
    },

    formatCurrentLocation(ev) {
      if (!ev) return "Idle";
      if (ev.type === "ingress") {
        return `UDP IN (${this.getEventIncomingChannel(ev)})`;
      }
      return this.getEventHandler(ev) || "Unknown";
    },

    formatStepSummary(ev) {
      if (!ev) return "";
      const handler = this.getEventHandler(ev);
      const inCh = this.getEventIncomingChannel(ev);
      const outCh = this.getEventOutgoingChannel(ev);
      if (ev.type === "ingress") {
        return `UDP IN (${inCh}) — Staged from ${ev.source || "PCAP"}`;
      }
      if (ev.type === "drop" || ev.result === "Dropped") {
        return `${handler} — Dropped${inCh ? " (in: " + inCh + ")" : ""}`;
      }
      if (ev.type === "egress" || ev.result === "Consumed") {
        return `${handler} — Consumed${inCh ? " (in: " + inCh + ")" : ""}`;
      }
      return `${handler} ➔ ${outCh || "next"}${inCh ? " (in: " + inCh + ")" : ""}`;
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

      // REAL-TIME SIMULATION MODE: Step in-flight packet into next handler or auto-load next packet
      try {
        const resp = await fetch("/api/simulation/step", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ autoLoad: true })
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
        if (!this.state.inFlight && this.state.currentPacketIdx >= this.state.packetCount) {
          this.isPlaying = false;
          return;
        }
        await this.stepForward();
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
          const hName = ev.handler || ev.target;
          const hType = ev.handlerType || ev.targetType || "handler";
          const inCh = ev.inputChannel || (ev.type !== "hop" ? ev.channel : null);
          const outCh = ev.outputChannel || (ev.type === "hop" ? ev.channel : null);
          if (hName && !nodeMap.has(hName)) {
            nodeMap.set(hName, {
              name: hName,
              type: hType,
              input_channel: inCh || null,
              output_channel: outCh || null
            });
          } else if (hName && nodeMap.has(hName)) {
            const existing = nodeMap.get(hName);
            if (outCh && !existing.output_channel) {
              existing.output_channel = outCh;
            }
            if (inCh && !existing.input_channel) {
              existing.input_channel = inCh;
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
