/**
 * Canvas pipeline visualizer and particle kinematics engine.
 */
export class CanvasRenderer {
  constructor(canvasElement, store) {
    this.canvas = canvasElement;
    this.ctx = this.canvas.getContext("2d");
    this.store = store;

    this.handlersLayout = [];
    this.channelsLayout = [];
    this.activeParticles = [];

    this.resizeCanvas();
    window.addEventListener("resize", () => this.resizeCanvas());

    const wrap = this.canvas.parentElement;
    if (wrap) {
      new ResizeObserver(() => this.resizeCanvas()).observe(wrap);
    }

    this.setupClickHandler();
    this.startLoop();
  }

  resizeCanvas() {
    const wrap = this.canvas.parentElement;
    if (!wrap) return;
    const rect = wrap.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;

    this.canvas.width = Math.floor(rect.width * dpr);
    this.canvas.height = Math.floor(rect.height * dpr);
    this.canvas.style.width = `${rect.width}px`;
    this.canvas.style.height = `${rect.height}px`;

    this.ctx.setTransform(1, 0, 0, 1, 0, 0);
    this.ctx.scale(dpr, dpr);

    this.layoutPipeline(rect.width, rect.height);
  }

  layoutPipeline(viewW, viewH) {
    const chain = this.store.state.chain;
    if (!chain || !chain.handlers) return;
    const handlers = chain.handlers;

    const w = viewW !== undefined ? viewW : (this.canvas.clientWidth || this.canvas.width);
    const h = viewH !== undefined ? viewH : (this.canvas.clientHeight || this.canvas.height);

    this.handlersLayout = [];
    this.channelsLayout = [];

    const inDegrees = new Map();
    const outgoing = new Map();
    handlers.forEach(h => {
      inDegrees.set(h.name, 0);
      outgoing.set(h.name, []);
    });

    handlers.forEach(h => {
      const outCh = h.output_channel;
      if (outCh) {
        handlers.forEach(consumer => {
          const inList = consumer.input_channels || (consumer.input_channel ? [consumer.input_channel] : []);
          if (inList.includes(outCh)) {
            outgoing.get(h.name).push(consumer.name);
            inDegrees.set(consumer.name, (inDegrees.get(consumer.name) || 0) + 1);
          }
        });
      }
    });

    const ranks = new Map();
    const queue = [];
    handlers.forEach(h => {
      if ((inDegrees.get(h.name) || 0) === 0) {
        ranks.set(h.name, 0);
        queue.push(h.name);
      }
    });

    if (queue.length === 0 && handlers.length > 0) {
      ranks.set(handlers[0].name, 0);
      queue.push(handlers[0].name);
    }

    while (queue.length > 0) {
      const u = queue.shift();
      const r = ranks.get(u) || 0;
      for (const v of outgoing.get(u) || []) {
        const nextR = Math.max(ranks.get(v) || 0, r + 1);
        ranks.set(v, nextR);
        queue.push(v);
      }
    }

    const columns = new Map();
    handlers.forEach(h => {
      const r = ranks.get(h.name) || 0;
      if (!columns.has(r)) columns.set(r, []);
      columns.get(r).push(h);
    });

    const numCols = Math.max(...ranks.values(), 0) + 1;
    const leftMargin = 150; // Comfortable margin for ingress queue
    const rightMargin = 90;
    const colSpacing = (w - leftMargin - rightMargin) / Math.max(numCols - 1, 1);

    columns.forEach((colHandlers, r) => {
      const colX = numCols === 1 ? Math.max(leftMargin + 70, w / 2) : leftMargin + r * colSpacing;
      const rowSpacing = (h - 90) / (colHandlers.length + 1);
      colHandlers.forEach((handler, rowIdx) => {
        const nodeY = 45 + (rowIdx + 1) * rowSpacing;
        const isGenerator = !handler.input_channel && (!handler.input_channels || handler.input_channels.length === 0) && !!handler.output_channel;
        this.handlersLayout.push({
          name: handler.name,
          type: handler.type,
          isGenerator: isGenerator,
          x: colX,
          y: nodeY,
          width: 140,
          height: isGenerator ? 64 : 52,
          inCh: handler.input_channel,
          outCh: handler.output_channel,
          config: handler.config || {},
        });
      });
    });

    // Construct DAG channels
    handlers.forEach(h => {
      const outCh = h.output_channel;
      if (outCh) {
        const fromNode = this.handlersLayout.find(item => item.name === h.name);
        handlers.forEach(consumer => {
          const inList = consumer.input_channels || (consumer.input_channel ? [consumer.input_channel] : []);
          if (inList.includes(outCh)) {
            const toNode = this.handlersLayout.find(item => item.name === consumer.name);
            if (fromNode && toNode) {
              this.channelsLayout.push({
                name: outCh,
                from: fromNode,
                to: toNode,
              });
            }
          }
        });
      }
    });
  }

  triggerTransitionAnimation(event) {
    if (!event || !event.target) return;
    const targetNode = this.handlersLayout.find(h => h.name === event.target);
    if (!targetNode) return;

    const isIngress = event.type === "ingress" || event.channel === "udp_network" || (event.source && event.source.includes(":"));
    const isHopAtIngress = event.type === "hop" && (event.source === "ingress" || !this.handlersLayout.some(h => h.name === event.source));

    const cloudW = 54;
    const cloudCenter = targetNode.x - targetNode.width / 2 - 14 - cloudW / 2;

    let startX = targetNode.x - targetNode.width / 2 - 80;
    let startY = targetNode.y;
    let endX = targetNode.x;
    let endY = targetNode.y;

    const srcNode = this.handlersLayout.find(h => h.name === event.source);
    if (srcNode) {
      startX = srcNode.x + srcNode.width / 2;
      startY = srcNode.y;
      endX = targetNode.x;
    } else if (isIngress) {
      // Step 1: External packet materializes inside the ingress queue
      startX = cloudCenter - 18;
      startY = targetNode.y;
      endX = cloudCenter;
    } else if (isHopAtIngress) {
      // Step 2: Ingress packet enters the handler node core
      startX = cloudCenter;
      startY = targetNode.y;
      endX = targetNode.x;
    }

    if (event.type === "egress") {
      endX = targetNode.x + targetNode.width / 2 + 60;
    }

    const particleColor = event.type === "drop" 
      ? "#f87171" 
      : (isIngress ? "#34d399" : (event.type === "egress" ? "#a78bfa" : "#38bdf8"));

    this.activeParticles = [{
      startX,
      startY,
      endX,
      endY,
      progress: 0.0,
      speed: isIngress ? 0.045 : 0.024,
      color: particleColor,
      isIngress: isIngress,
      isDrop: event.type === "drop"
    }];
  }

  setupClickHandler() {
    this.canvas.addEventListener("click", (evt) => {
      const rect = this.canvas.getBoundingClientRect();
      const clickX = evt.clientX - rect.left;
      const clickY = evt.clientY - rect.top;

      for (const h of this.handlersLayout) {
        if (clickX >= h.x - h.width/2 && clickX <= h.x + h.width/2 &&
            clickY >= h.y - h.height/2 && clickY <= h.y + h.height/2) {
          
          if (h.isGenerator) {
            const btnW = 96;
            const btnH = 18;
            const btnX = h.x - btnW / 2;
            const btnY = h.y + 11;
            if (clickX >= btnX && clickX <= btnX + btnW && clickY >= btnY && clickY <= btnY + btnH) {
              this.store.triggerGenerator(h.name);
              return;
            }
          }

          this.store.inspectNode(h);
          return;
        }
      }
    });
  }

  startLoop() {
    const render = () => {
      this.draw();
      requestAnimationFrame(render);
    };
    requestAnimationFrame(render);
  }

  draw() {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

    const lastEvent = this.store.state.lastEvent;
    const selectedNodeName = this.store.state.selectedNode ? this.store.state.selectedNode.name : null;

    // Draw Channel Connections
    this.channelsLayout.forEach(ch => {
      ctx.strokeStyle = "rgba(255, 255, 255, 0.12)";
      ctx.lineWidth = 2;
      ctx.setLineDash([5, 5]);
      ctx.beginPath();
      ctx.moveTo(ch.from.x + ch.from.width/2, ch.from.y);
      ctx.lineTo(ch.to.x - ch.to.width/2, ch.to.y);
      ctx.stroke();
      ctx.setLineDash([]);

      const midX = (ch.from.x + ch.from.width/2 + ch.to.x - ch.to.width/2) / 2;
      const midY = ch.from.y - 12;
      ctx.fillStyle = "rgba(148, 163, 184, 0.7)";
      ctx.font = "11px monospace";
      ctx.textAlign = "center";
      ctx.fillText(`channel: ${ch.name}`, midX, midY);
    });

    // Draw Handler Nodes & Queues
    this.handlersLayout.forEach((h) => {
      const x = h.x - h.width/2;
      const y = h.y - h.height/2;
      const isTarget = lastEvent && (
        lastEvent.type === "drop"
          ? lastEvent.target === h.name
          : (lastEvent.target === h.name || lastEvent.source === h.name)
      );
      const isSelected = selectedNodeName === h.name;

      const isIngressNode = h.type === "udp_ingress_handler" || h.inCh === "udp_network" || (h.config && h.config["ingress.port"]);
      if (isIngressNode) {
        const cloudW = 54;
        const cloudH = h.height - 6;
        const cloudX = x - cloudW - 14;
        const cloudY = y + 3;

        ctx.save();
        const isIngressActive = lastEvent && (lastEvent.type === "ingress" || (lastEvent.type === "hop" && lastEvent.source === "ingress"));
        ctx.shadowColor = isIngressActive ? "rgba(52, 211, 153, 0.65)" : "rgba(56, 189, 248, 0.2)";
        ctx.shadowBlur = isIngressActive ? 16 : 6;

        ctx.fillStyle = isIngressActive ? "rgba(6, 78, 59, 0.45)" : "rgba(15, 23, 42, 0.75)";
        ctx.beginPath();
        ctx.roundRect(cloudX, cloudY, cloudW, cloudH, [10, 4, 4, 10]);
        ctx.fill();

        ctx.strokeStyle = isIngressActive ? "#34d399" : "rgba(148, 163, 184, 0.45)";
        ctx.lineWidth = isIngressActive ? 2 : 1.5;
        ctx.setLineDash([4, 3]);
        ctx.stroke();
        ctx.setLineDash([]);

        ctx.shadowBlur = 0;
        ctx.fillStyle = isIngressActive ? "#6ee7b7" : "rgba(148, 163, 184, 0.85)";
        ctx.font = "bold 9px monospace";
        ctx.textAlign = "center";
        ctx.fillText("UDP IN", cloudX + cloudW/2, cloudY + 16);
        ctx.font = "8px sans-serif";
        ctx.fillText("queue", cloudX + cloudW/2, cloudY + 28);

        ctx.strokeStyle = isIngressActive ? "#34d399" : "rgba(148, 163, 184, 0.35)";
        ctx.lineWidth = 1.5;
        ctx.setLineDash([2, 3]);
        ctx.beginPath();
        ctx.moveTo(cloudX + cloudW, h.y);
        ctx.lineTo(x, h.y);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.restore();
      }

      if (isSelected) {
        ctx.shadowColor = "rgba(56, 189, 248, 0.9)";
        ctx.shadowBlur = 26;
      } else if (isTarget) {
        ctx.shadowColor = lastEvent.type === "drop" ? "rgba(248, 113, 113, 0.7)" : "rgba(56, 189, 248, 0.7)";
        ctx.shadowBlur = 20;
      } else {
        ctx.shadowColor = "rgba(56, 189, 248, 0.15)";
        ctx.shadowBlur = 8;
      }

      ctx.fillStyle = isSelected ? "#0c4a6e" : (isTarget ? "#1e3a5f" : "#1e293b");
      ctx.beginPath();
      ctx.roundRect(x, y, h.width, h.height, 10);
      ctx.fill();

      ctx.shadowBlur = 0;
      ctx.strokeStyle = isSelected ? "#38bdf8" : (isTarget ? (lastEvent.type === "drop" ? "#f87171" : "#38bdf8") : "rgba(56, 189, 248, 0.35)");
      ctx.lineWidth = isSelected ? 3 : (isTarget ? 2.5 : 1.5);
      ctx.stroke();

      ctx.fillStyle = "#f8fafc";
      ctx.font = "bold 12px sans-serif";
      ctx.textAlign = "center";
      const titleY = h.isGenerator ? h.y - 12 : h.y - 5;
      ctx.fillText(h.name, h.x, titleY);

      ctx.fillStyle = isSelected || isTarget ? "#7dd3fc" : "#94a3b8";
      ctx.font = "10px monospace";
      ctx.fillText(h.isGenerator ? "generator" : h.type.replace("_handler", ""), h.x, titleY + 16);

      if (h.isGenerator) {
        const btnW = 96;
        const btnH = 18;
        const btnX = h.x - btnW / 2;
        const btnY = h.y + 11;

        ctx.fillStyle = "rgba(56, 189, 248, 0.22)";
        ctx.beginPath();
        ctx.roundRect(btnX, btnY, btnW, btnH, 4);
        ctx.fill();

        ctx.strokeStyle = "rgba(56, 189, 248, 0.6)";
        ctx.lineWidth = 1;
        ctx.stroke();

        ctx.fillStyle = "#38bdf8";
        ctx.font = "bold 9px sans-serif";
        ctx.fillText("⚡ Trigger Emit", h.x, btnY + 12);
      }
    });

    // Highlight Active Channel
    if (lastEvent && lastEvent.channel) {
      const activeCh = this.channelsLayout.find(c => c.name === lastEvent.channel);
      if (activeCh) {
        ctx.strokeStyle = "#38bdf8";
        ctx.lineWidth = 4;
        ctx.shadowColor = ctx.strokeStyle;
        ctx.shadowBlur = 14;
        ctx.beginPath();
        ctx.moveTo(activeCh.from.x + activeCh.from.width/2, activeCh.from.y);
        ctx.lineTo(activeCh.to.x - activeCh.to.width/2, activeCh.to.y);
        ctx.stroke();
        ctx.shadowBlur = 0;
      }
    }

    // Animate Particles
    for (let i = this.activeParticles.length - 1; i >= 0; i--) {
      const p = this.activeParticles[i];
      if (p.progress < 1.0) {
        p.progress += p.speed || 0.025;
        if (p.progress > 1.0) p.progress = 1.0;
      }

      const curX = p.startX + (p.endX - p.startX) * p.progress;
      const curY = p.startY + (p.endY - p.startY) * p.progress;

      if (p.progress < 1.0) {
        ctx.save();
        ctx.strokeStyle = p.color;
        ctx.lineWidth = 3;
        ctx.shadowColor = p.color;
        ctx.shadowBlur = 12;
        ctx.beginPath();
        const trailStartX = p.startX + (p.endX - p.startX) * Math.max(0, p.progress - 0.2);
        const trailStartY = p.startY + (p.endY - p.startY) * Math.max(0, p.progress - 0.2);
        ctx.moveTo(trailStartX, trailStartY);
        ctx.lineTo(curX, curY);
        ctx.stroke();
        ctx.restore();
      }

      const headRadius = p.isIngress ? 8.5 : 7;

      // On drop, animate dissolution/disintegration into the dropping node
      if (p.isDrop && p.progress >= 0.75) {
        ctx.save();
        const fade = Math.max(0, 1.0 - (p.progress - 0.75) / 0.25);
        ctx.globalAlpha = fade;
        ctx.strokeStyle = p.color;
        ctx.lineWidth = 2;
        const disperseRadius = (headRadius + 2) + ((1.0 - fade) * 14);
        ctx.beginPath();
        ctx.arc(curX, curY, disperseRadius, 0, Math.PI * 2);
        ctx.stroke();

        // Small cross mark inside dropping node
        const crossSize = 5;
        ctx.beginPath();
        ctx.moveTo(curX - crossSize, curY - crossSize);
        ctx.lineTo(curX + crossSize, curY + crossSize);
        ctx.moveTo(curX + crossSize, curY - crossSize);
        ctx.lineTo(curX - crossSize, curY + crossSize);
        ctx.stroke();
        ctx.restore();
      }

      ctx.save();
      const pulse = p.progress >= 1.0 ? Math.sin(Date.now() / 200) * 1.5 : 0;
      ctx.shadowColor = p.color;
      ctx.shadowBlur = p.isIngress ? 24 : 18 + pulse * 2;
      ctx.fillStyle = p.isDrop ? "rgba(248, 113, 113, 0.9)" : "#ffffff";
      ctx.beginPath();
      ctx.arc(curX, curY, headRadius + pulse, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = p.color;
      ctx.beginPath();
      ctx.arc(curX, curY, (headRadius * 0.65) + pulse * 0.5, 0, Math.PI * 2);
      ctx.fill();

      if (p.isIngress && p.progress < 1.0) {
        ctx.strokeStyle = p.color;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(curX, curY, headRadius + 5, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.restore();
    }

    // Persistent Envelope Badge on target
    if (lastEvent) {
      const targetNode = this.handlersLayout.find(h => h.name === lastEvent.target);
      if (targetNode) {
        const badgeX = targetNode.x;
        const badgeY = targetNode.y - targetNode.height/2 - 16;

        ctx.shadowColor = lastEvent.type === "drop" ? "rgba(248, 113, 113, 0.7)" : "rgba(56, 189, 248, 0.6)";
        ctx.shadowBlur = 12;
        ctx.fillStyle = lastEvent.type === "drop" ? "#991b1b" : "#0369a1";
        ctx.beginPath();
        ctx.roundRect(badgeX - 44, badgeY - 10, 88, 20, 6);
        ctx.fill();

        ctx.shadowBlur = 0;
        ctx.fillStyle = "#f8fafc";
        ctx.font = "bold 10px monospace";
        ctx.textAlign = "center";
        const statusText = lastEvent.type === "drop" ? `P${lastEvent.msgId} DROPPED` : (lastEvent.type === "egress" ? `P${lastEvent.msgId} SENT` : `P${lastEvent.msgId} HELD`);
        ctx.fillText(statusText, badgeX, badgeY + 3.5);
      }
    }
  }
}

