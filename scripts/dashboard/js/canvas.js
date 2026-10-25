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
      const outList = h.output_channels || (h.output_channel ? String(h.output_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
      outList.forEach(outCh => {
        handlers.forEach(consumer => {
          const inList = consumer.input_channels || (consumer.input_channel ? String(consumer.input_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
          if (inList.includes(outCh)) {
            outgoing.get(h.name).push(consumer.name);
            inDegrees.set(consumer.name, (inDegrees.get(consumer.name) || 0) + 1);
          }
        });
      });
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
        const role = handler.role || (
          (!handler.input_channel && (!handler.input_channels || handler.input_channels.length === 0) && !!handler.output_channel)
            ? (handler.type === "udp_ingress_handler" ? "external_ingress" : "generator")
            : "processor"
        );
        const isGenerator = role === "generator";
        const isIngress = role === "external_ingress";
        const protocol = handler.protocol || (handler.type === "udp_ingress_handler" ? "udp" : "none");

        this.handlersLayout.push({
          name: handler.name,
          type: handler.type,
          role: role,
          protocol: protocol,
          isGenerator: isGenerator,
          isIngress: isIngress,
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
      const outList = h.output_channels || (h.output_channel ? String(h.output_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
      const fromNode = this.handlersLayout.find(item => item.name === h.name);
      outList.forEach(outCh => {
        handlers.forEach(consumer => {
          const inList = consumer.input_channels || (consumer.input_channel ? String(consumer.input_channel).split(",").map(s => s.trim()).filter(Boolean) : []);
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
      });
    });
  }

  findIncomingChannelEdge(event) {
    if (!event || event.type === "ingress") return null;
    const handlerName = event.handler || event.target;
    const inCh = event.inputChannel || event.channel;
    if (!handlerName) return null;

    if (inCh) {
      const exact = this.channelsLayout.find(c => c.to.name === handlerName && c.name === inCh);
      if (exact) return exact;
    }
    if (event.source && event.source !== "ingress") {
      const bySource = this.channelsLayout.find(c => c.from.name === event.source && c.to.name === handlerName);
      if (bySource) return bySource;
    }
    return null;
  }

  findOutgoingChannelEdge(event) {
    if (!event) return null;
    const handlerName = event.handler || event.source;
    const outCh = event.outputChannel || event.channel;
    if (!handlerName || !outCh) return null;

    return this.channelsLayout.find(c => c.from.name === handlerName && c.name === outCh) || null;
  }

  triggerTransitionAnimation(event) {
    if (!event) return;
    const handlerName = event.handler || event.target;
    if (!handlerName) return;
    const handlerNode = this.handlersLayout.find(h => h.name === handlerName);
    if (!handlerNode) return;

    const isIngress = event.type === "ingress";
    const isForwarded = event.result === "Forwarded" && !!(event.outputChannel || event.channel);
    const isConsumed = event.type === "egress" || event.result === "Consumed";
    const edge = isForwarded
      ? this.findOutgoingChannelEdge(event)
      : this.findIncomingChannelEdge(event);

    const cloudW = 54;
    const cloudCenter = handlerNode.x - handlerNode.width / 2 - 14 - cloudW / 2;
    let startX;
    let startY;
    let endX;
    let endY;

    if (isIngress) {
      startX = cloudCenter - 22;
      startY = handlerNode.y;
      endX = cloudCenter;
      endY = handlerNode.y;
    } else if (isForwarded) {
      startX = handlerNode.x + handlerNode.width / 2;
      startY = handlerNode.y;
      endX = edge ? edge.to.x - edge.to.width / 2 : startX + 76;
      endY = edge ? edge.to.y : startY;
    } else if (isConsumed) {
      startX = edge ? edge.to.x - edge.to.width / 2 : cloudCenter;
      startY = edge ? edge.to.y : handlerNode.y;
      endX = handlerNode.x + handlerNode.width / 2;
      endY = handlerNode.y;
    } else if (edge) {
      startX = edge.from.x + edge.from.width / 2;
      startY = edge.from.y;
      endX = handlerNode.x - handlerNode.width / 2;
      endY = handlerNode.y;
    } else if (!handlerNode.isGenerator) {
      startX = cloudCenter;
      startY = handlerNode.y;
      endX = handlerNode.x - handlerNode.width / 2;
      endY = handlerNode.y;
    } else {
      startX = handlerNode.x;
      startY = handlerNode.y;
      endX = handlerNode.x + handlerNode.width / 2 + 76;
      endY = handlerNode.y;
    }

    const transitColor = isIngress ? "#38bdf8" : (isForwarded ? "#c084fc" : "#38bdf8");
    const arrivalColor = isIngress
      ? "#38bdf8"
      : (event.type === "drop" ? "#f87171" : (event.type === "egress" ? "#34d399" : "#c084fc"));

    const distance = Math.hypot(endX - startX, endY - startY);
    const durationMs = Math.min(700, Math.max(350, distance / 0.7));

    this.activeParticles = [{
      startX,
      startY,
      endX,
      endY,
      progress: 0.0,
      startTime: performance.now(),
      durationMs,
      color: transitColor,
      arrivalColor: arrivalColor,
      dropColor: "#f87171",
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
    const lastEventHandler = lastEvent ? (lastEvent.handler || lastEvent.target) : null;
    const activeIncomingEdge = this.findIncomingChannelEdge(lastEvent);
    const activeOutgoingEdge = this.findOutgoingChannelEdge(lastEvent);

    this.handlersLayout.forEach((h) => {
      const x = h.x - h.width/2;
      const y = h.y - h.height/2;
      // Only highlight the handler node when the packet is currently AT this handler (not when staged in UDP IN queue)
      const isTarget = lastEvent && lastEvent.type !== "ingress" && lastEventHandler === h.name;
      const isSelected = selectedNodeName === h.name;

      const isIngressNode = h.isIngress || h.type === "udp_ingress_handler" || h.inCh === "udp_network" || (h.config && h.config["ingress.port"]);
      if (isIngressNode) {
        const cloudW = 54;
        const cloudH = h.height - 6;
        const cloudX = x - cloudW - 14;
        const cloudY = y + 3;

        ctx.save();
        const isIngressStaged = lastEvent && lastEvent.type === "ingress" && lastEventHandler === h.name;
        const isIngressEnteringNode = lastEvent && lastEvent.type !== "ingress" && lastEventHandler === h.name && !activeIncomingEdge;
        ctx.shadowColor = isIngressStaged ? "rgba(56, 189, 248, 0.8)" : (isIngressEnteringNode ? "rgba(192, 132, 252, 0.6)" : "rgba(56, 189, 248, 0.2)");
        ctx.shadowBlur = isIngressStaged ? 18 : (isIngressEnteringNode ? 12 : 6);

        ctx.fillStyle = isIngressStaged ? "rgba(12, 74, 110, 0.65)" : "rgba(15, 23, 42, 0.75)";
        ctx.beginPath();
        ctx.roundRect(cloudX, cloudY, cloudW, cloudH, [10, 4, 4, 10]);
        ctx.fill();

        ctx.strokeStyle = isIngressStaged ? "#38bdf8" : (isIngressEnteringNode ? "#c084fc" : "rgba(148, 163, 184, 0.45)");
        ctx.lineWidth = isIngressStaged ? 2.5 : 1.5;
        ctx.setLineDash([4, 3]);
        ctx.stroke();
        ctx.setLineDash([]);

        ctx.shadowBlur = 0;
        ctx.fillStyle = isIngressStaged ? "#7dd3fc" : "rgba(148, 163, 184, 0.85)";
        ctx.font = "bold 9px monospace";
        ctx.textAlign = "center";
        ctx.fillText("UDP IN", cloudX + cloudW/2, cloudY + 16);
        ctx.font = "8px sans-serif";
        ctx.fillText("queue", cloudX + cloudW/2, cloudY + 28);

        // Highlight the connector between UDP IN queue and handler when stepping into first handler
        ctx.strokeStyle = isIngressEnteringNode ? "#c084fc" : (isIngressStaged ? "#38bdf8" : "rgba(148, 163, 184, 0.35)");
        ctx.lineWidth = isIngressEnteringNode ? 3.5 : 1.5;
        if (isIngressEnteringNode) {
          ctx.shadowColor = "#c084fc";
          ctx.shadowBlur = 12;
          ctx.setLineDash([]);
        } else {
          ctx.setLineDash([2, 3]);
        }
        ctx.beginPath();
        ctx.moveTo(cloudX + cloudW, h.y);
        ctx.lineTo(x, h.y);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.restore();
      }

      const activeParticle = this.activeParticles[0];
      const reachedNode = !activeParticle || activeParticle.progress >= 0.85;
      const dropReachedNode = lastEvent && lastEvent.type === "drop" && reachedNode;
      const isForwardedTarget = isTarget && lastEvent.type === "hop";
      const isConsumedTarget = isTarget && lastEvent.type === "egress";

      if (isSelected) {
        ctx.shadowColor = "rgba(56, 189, 248, 0.9)";
        ctx.shadowBlur = 26;
      } else if (isTarget) {
        ctx.shadowColor = dropReachedNode
          ? "rgba(248, 113, 113, 0.8)"
          : (isConsumedTarget ? "rgba(52, 211, 153, 0.8)" : (isForwardedTarget ? "rgba(192, 132, 252, 0.8)" : "rgba(56, 189, 248, 0.75)"));
        ctx.shadowBlur = 20;
      } else {
        ctx.shadowColor = "rgba(56, 189, 248, 0.15)";
        ctx.shadowBlur = 8;
      }

      ctx.fillStyle = isSelected
        ? "#0c4a6e"
        : (isTarget ? (dropReachedNode ? "#3b1824" : (isConsumedTarget ? "#064e3b" : (isForwardedTarget ? "#3b1d5e" : "#1e3a5f"))) : "#1e293b");
      ctx.beginPath();
      ctx.roundRect(x, y, h.width, h.height, 10);
      ctx.fill();

      ctx.shadowBlur = 0;
      ctx.strokeStyle = isSelected
        ? "#38bdf8"
        : (isTarget ? (dropReachedNode ? "#f87171" : (isConsumedTarget ? "#34d399" : (isForwardedTarget ? "#c084fc" : "#38bdf8"))) : "rgba(56, 189, 248, 0.35)");
      ctx.lineWidth = isSelected ? 3 : (isTarget ? 2.5 : 1.5);
      ctx.stroke();

      ctx.fillStyle = "#f8fafc";
      ctx.font = "bold 12px sans-serif";
      ctx.textAlign = "center";
      const titleY = h.isGenerator ? h.y - 12 : h.y - 5;
      ctx.fillText(h.name, h.x, titleY);

      ctx.fillStyle = isSelected
        ? "#7dd3fc"
        : (isTarget ? (dropReachedNode ? "#fca5a5" : (isConsumedTarget ? "#6ee7b7" : (isForwardedTarget ? "#e9d5ff" : "#7dd3fc"))) : "#94a3b8");
      ctx.font = "10px monospace";
      let subtitle = h.type.replace("_handler", "");
      if (h.isGenerator) {
        subtitle = "⚡ generator";
      } else if (h.isIngress) {
        subtitle = `🌐 ingress [${h.protocol.toUpperCase()}]`;
      }
      ctx.fillText(subtitle, h.x, titleY + 16);

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

    // Highlight the same direction shown by the active event.
    const activeEventEdge = lastEvent?.result === "Forwarded" ? activeOutgoingEdge : activeIncomingEdge;
    if (activeEventEdge) {
      ctx.strokeStyle = "#c084fc";
      ctx.lineWidth = 4;
      ctx.shadowColor = ctx.strokeStyle;
      ctx.shadowBlur = 14;
      ctx.beginPath();
      ctx.moveTo(activeEventEdge.from.x + activeEventEdge.from.width/2, activeEventEdge.from.y);
      ctx.lineTo(activeEventEdge.to.x - activeEventEdge.to.width/2, activeEventEdge.to.y);
      ctx.stroke();
      ctx.shadowBlur = 0;
    } else if (lastEvent?.result === "Forwarded" && lastEvent.outputChannel) {
      const sourceNode = this.handlersLayout.find(h => h.name === lastEventHandler);
      if (sourceNode) {
        const startX = sourceNode.x + sourceNode.width / 2;
        const endX = startX + 76;
        ctx.strokeStyle = "#c084fc";
        ctx.lineWidth = 3;
        ctx.shadowColor = ctx.strokeStyle;
        ctx.shadowBlur = 12;
        ctx.beginPath();
        ctx.moveTo(startX, sourceNode.y);
        ctx.lineTo(endX, sourceNode.y);
        ctx.stroke();
        ctx.shadowBlur = 0;

        ctx.fillStyle = "#c084fc";
        ctx.font = "10px monospace";
        ctx.textAlign = "left";
        ctx.fillText(lastEvent.outputChannel, startX + 5, sourceNode.y - 10);
      }
    }

    // Animate Particles
    for (let i = this.activeParticles.length - 1; i >= 0; i--) {
      const p = this.activeParticles[i];
      if (p.progress < 1.0) {
        const elapsed = performance.now() - p.startTime;
        const linearProgress = Math.min(elapsed / p.durationMs, 1);
        p.progress = linearProgress * linearProgress * (3 - 2 * linearProgress);
      }

      const curX = p.startX + (p.endX - p.startX) * p.progress;
      const curY = p.startY + (p.endY - p.startY) * p.progress;
      const reachedNode = p.progress >= 0.85;
      const reachedDropNode = p.isDrop && reachedNode;
      const activeColor = reachedNode ? (p.arrivalColor || p.color) : p.color;

      if (p.progress < 1.0) {
        ctx.save();
        ctx.strokeStyle = activeColor;
        ctx.lineWidth = 3;
        ctx.shadowColor = activeColor;
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

      ctx.save();
      const pulse = p.progress >= 1.0 ? Math.sin(Date.now() / 200) * 1.5 : 0;
      ctx.shadowColor = activeColor;
      ctx.shadowBlur = p.isIngress ? 24 : 18 + pulse * 2;
      ctx.fillStyle = reachedDropNode ? "rgba(248, 113, 113, 0.95)" : "#ffffff";
      ctx.beginPath();
      ctx.arc(curX, curY, headRadius + pulse, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = activeColor;
      ctx.beginPath();
      ctx.arc(curX, curY, (headRadius * 0.65) + pulse * 0.5, 0, Math.PI * 2);
      ctx.fill();

      if (p.isIngress && p.progress < 1.0) {
        ctx.strokeStyle = activeColor;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(curX, curY, headRadius + 5, 0, Math.PI * 2);
        ctx.stroke();
      }
      ctx.restore();

      // Once the particle reaches the node on a drop event, turn red and draw a clear cross-out (X) over it
      if (reachedDropNode) {
        ctx.save();
        if (p.progress < 1.0) {
          const ringProgress = (p.progress - 0.85) / 0.15;
          ctx.globalAlpha = Math.max(0, 1.0 - ringProgress);
          ctx.strokeStyle = "#f87171";
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.arc(curX, curY, (headRadius + 2) + ringProgress * 12, 0, Math.PI * 2);
          ctx.stroke();
        }

        ctx.globalAlpha = 1.0;
        ctx.strokeStyle = "#ffffff";
        ctx.lineWidth = 2.5;
        ctx.lineCap = "round";
        const crossSize = 5.5;
        ctx.beginPath();
        ctx.moveTo(curX - crossSize, curY - crossSize);
        ctx.lineTo(curX + crossSize, curY + crossSize);
        ctx.moveTo(curX + crossSize, curY - crossSize);
        ctx.lineTo(curX - crossSize, curY + crossSize);
        ctx.stroke();

        ctx.strokeStyle = "#ef4444";
        ctx.lineWidth = 2;
        const outerRingRadius = headRadius + 3.5;
        ctx.beginPath();
        ctx.arc(curX, curY, outerRingRadius, 0, Math.PI * 2);
        ctx.stroke();
        ctx.restore();
      }
    }

    // Persistent Envelope Badge on the current packet location (UDP IN queue on Step 1, or target handler node on Steps 2+)
    if (lastEvent) {
      const targetNode = this.handlersLayout.find(h => h.name === lastEventHandler);
      const activeParticle = this.activeParticles[0];
      const showDropState = lastEvent.type === "drop" && (!activeParticle || activeParticle.progress >= 0.85);
      if (targetNode && (lastEvent.type !== "drop" || showDropState)) {
        const cloudW = 54;
        const cloudCenter = targetNode.x - targetNode.width / 2 - 14 - cloudW / 2;
        const badgeX = lastEvent.type === "ingress" ? cloudCenter : targetNode.x;
        const badgeY = targetNode.y - targetNode.height/2 - 16;

        ctx.shadowColor = showDropState
          ? "rgba(248, 113, 113, 0.75)"
          : (lastEvent.type === "egress" ? "rgba(52, 211, 153, 0.75)" : (lastEvent.type === "hop" ? "rgba(192, 132, 252, 0.75)" : "rgba(56, 189, 248, 0.7)"));
        ctx.shadowBlur = 12;
        ctx.fillStyle = showDropState
          ? "#991b1b"
          : (lastEvent.type === "egress" ? "#065f46" : (lastEvent.type === "hop" ? "#6b21a8" : "#0369a1"));
        ctx.font = "bold 10px monospace";
        const batchIds = Array.isArray(lastEvent.batchIds) ? lastEvent.batchIds : [];
        const packetLabel = batchIds.length > 1
          ? `P[${batchIds.join(",")}]`
          : `P${lastEvent.msgId}`;
        const actionLabel = lastEvent.result
          ? lastEvent.result.toUpperCase()
          : (lastEvent.type === "drop" ? "DROPPED" : (lastEvent.type === "egress" ? "CONSUMED" : (lastEvent.type === "ingress" ? "LOADED" : "HELD")));
        const statusText = `${packetLabel} ${actionLabel}`;
        const badgeWidth = Math.max(108, ctx.measureText(statusText).width + 16);
        ctx.beginPath();
        ctx.roundRect(badgeX - badgeWidth / 2, badgeY - 10, badgeWidth, 20, 6);
        ctx.fill();

        ctx.shadowBlur = 0;
        ctx.fillStyle = "#f8fafc";
        ctx.textAlign = "center";
        ctx.fillText(statusText, badgeX, badgeY + 3.5);
      }
    }
  }
}
