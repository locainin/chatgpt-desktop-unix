(() => {
  // Make long chats cheaper by lowering work on old offscreen turns
  const trustedOrigins = globalThis.__chatgptDesktopTrustedOrigins;
  if (!trustedOrigins?.isTrustedLocation(window.location)) {
    return;
  }

  if (window.__chatgptDesktopLongChatPerfInstalled) {
    return;
  }
  // Install once per page so observers do not stack up
  window.__chatgptDesktopLongChatPerfInstalled = true;

  const styleId = "chatgpt-desktop-long-chat-perf-style";
  const optimizedClass = "__chatgptDesktopPerfOptimized";
  const recentClass = "__chatgptDesktopPerfRecent";
  const minMessageCount = 24;
  const keepRecentCount = 18;
  const viewportMargin = 1600;
  const maxNodes = 1000;
  const minMutationUpdateMs = 450;
  const minIntersectionUpdateMs = 90;
  const minResizeUpdateMs = 120;
  const managedNodes = new Set();
  const nearViewportNodes = new Set();
  let updateScheduled = false;
  let observersConnected = false;
  let scheduledReason = "initial";
  let lastUpdateAt = 0;
  let updateTimerId = 0;
  let scheduledRunAt = 0;

  const reasonPriority = {
    // Lower cost reasons can be replaced by stronger layout changes
    mutation: 1,
    intersection: 2,
    resize: 3,
    initial: 4
  };

  const ensureStyle = () => {
    if (document.getElementById(styleId)) {
      return;
    }

    // Keep the CSS here so the script can self manage its own rules
    const style = document.createElement("style");
    style.id = styleId;
    style.textContent = `
      html.__chatgptDesktopReducedMotion {
        scroll-behavior: auto !important;
      }
      html.__chatgptDesktopReducedMotion article,
      html.__chatgptDesktopReducedMotion li[data-message-author-role],
      html.__chatgptDesktopReducedMotion div[data-message-author-role] {
        animation: none !important;
        transition: none !important;
      }
      .${optimizedClass} {
        content-visibility: auto !important;
        contain: layout style paint !important;
        contain-intrinsic-size: 1px 900px !important;
      }
      .${recentClass} {
        content-visibility: visible !important;
        contain: none !important;
      }
    `;
    document.head.appendChild(style);
    document.documentElement.classList.add("__chatgptDesktopReducedMotion");
  };

  const getChatRoot = () => document.querySelector("main")
    || document.body
    || document.documentElement;

  const collectMessageNodes = (root) => {
    // Prefer the real chat turn markers before falling back to generic articles
    const selectors = [
      "article[data-testid*='conversation-turn']",
      "li[data-message-author-role]",
      "div[data-message-author-role]"
    ];
    const seen = new Set();
    const nodes = [];

    for (const selector of selectors) {
      const results = root.querySelectorAll(selector);
      for (const node of results) {
        if (!(node instanceof HTMLElement) || seen.has(node)) {
          continue;
        }
        seen.add(node);
        nodes.push(node);
      }
    }

    // Fallback selector only when structured selectors fail
    if (nodes.length === 0) {
      const results = root.querySelectorAll("article");
      for (const node of results) {
        if (!(node instanceof HTMLElement) || seen.has(node)) {
          continue;
        }
        seen.add(node);
        nodes.push(node);
      }
    }

    if (nodes.length <= maxNodes) {
      return nodes;
    }

    // Keep some old and new turns so both ends of the chat still get covered
    const tailCount = Math.floor(maxNodes * 0.35);
    const headCount = maxNodes - tailCount;
    return nodes.slice(0, headCount).concat(nodes.slice(nodes.length - tailCount));
  };

  const clearManagedNodes = () => {
    // Remove every class and observer so old pages do not leak work
    for (const node of managedNodes) {
      if (!(node instanceof HTMLElement)) {
        continue;
      }
      intersectionObserver.unobserve(node);
      node.classList.remove(optimizedClass);
      node.classList.remove(recentClass);
    }
    managedNodes.clear();
    nearViewportNodes.clear();
  };

  const cancelPendingUpdate = () => {
    if (updateTimerId !== 0) {
      window.clearTimeout(updateTimerId);
      updateTimerId = 0;
    }
    // Reset scheduler state so the next reason starts fresh
    scheduledRunAt = 0;
    updateScheduled = false;
    scheduledReason = "mutation";
  };

  const isNearViewportEntry = (entry) => entry.isIntersecting
    || (entry.boundingClientRect.bottom >= -viewportMargin
      && entry.boundingClientRect.top <= (window.innerHeight + viewportMargin));

  const syncManagedNodes = (messages) => {
    // Keep observers only on the nodes that still matter for this chat
    const selectedNodes = new Set(messages);

    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !selectedNodes.has(node)) {
        if (node instanceof HTMLElement) {
          node.classList.remove(optimizedClass);
          node.classList.remove(recentClass);
          intersectionObserver.unobserve(node);
        }
        nearViewportNodes.delete(node);
        managedNodes.delete(node);
      }
    }

    for (const node of messages) {
      if (managedNodes.has(node)) {
        continue;
      }
      // New nodes start as near viewport until the observer says otherwise
      managedNodes.add(node);
      nearViewportNodes.add(node);
      intersectionObserver.observe(node);
    }
  };

  const updateOptimization = (reason) => {
    if (document.visibilityState === "hidden") {
      // Hidden pages should not keep observer state or layout classes around
      clearManagedNodes();
      return;
    }

    // Drop nodes that vanished before touching layout classes again
    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !node.isConnected) {
        intersectionObserver.unobserve(node);
        nearViewportNodes.delete(node);
        managedNodes.delete(node);
      }
    }

    const chatRoot = getChatRoot();
    const hasChatTurns = !!chatRoot.querySelector(
      "article[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role]"
    );
    if (!hasChatTurns) {
      clearManagedNodes();
      return;
    }

    const messages = collectMessageNodes(chatRoot);
    if (messages.length < minMessageCount) {
      // Small chats do not need extra containment rules
      clearManagedNodes();
      return;
    }

    syncManagedNodes(messages);
    const recentStart = Math.max(0, messages.length - keepRecentCount);
    for (let index = 0; index < messages.length; ++index) {
      const message = messages[index];
      const isRecent = index >= recentStart;
      // The first pass keeps everything visible until the viewport map settles
      const nearViewport = reason === "initial"
        ? true
        : nearViewportNodes.has(message);
      const shouldOptimize = !isRecent && !nearViewport;
      message.classList.toggle(optimizedClass, shouldOptimize);
      message.classList.toggle(recentClass, !shouldOptimize);
    }
  };

  const scheduleUpdate = (reason = "mutation") => {
    if (document.visibilityState === "hidden") {
      cancelPendingUpdate();
      return;
    }

    // Batch expensive updates so token streaming does not thrash the page
    if (reasonPriority[reason] > reasonPriority[scheduledReason]) {
      scheduledReason = reason;
    }

    const run = () => {
      updateTimerId = 0;
      scheduledRunAt = 0;
      updateScheduled = false;
      const reasonForRun = scheduledReason;
      scheduledReason = "mutation";
      lastUpdateAt = performance.now();
      // Use the strongest queued reason for the final run
      updateOptimization(reasonForRun);
    };

    const scheduleTimer = (delayMs) => {
      updateTimerId = window.setTimeout(() => {
        // Idle time is preferred, but a timeout still forces progress
        if (typeof window.requestIdleCallback === "function") {
          window.requestIdleCallback(run, { timeout: 250 });
          return;
        }
        run();
      }, delayMs);
    };

    const now = performance.now();
    let minGap = minMutationUpdateMs;
    if (scheduledReason === "intersection") {
      minGap = minIntersectionUpdateMs;
    } else if (scheduledReason === "resize" || scheduledReason === "initial") {
      minGap = minResizeUpdateMs;
    }
    const delay = Math.max(0, minGap - (now - lastUpdateAt));
    const nextRunAt = now + delay;

    if (updateScheduled) {
      if (updateTimerId !== 0 && nextRunAt + 1 < scheduledRunAt) {
        // Pull the timer forward when a stronger reason needs an earlier pass
        window.clearTimeout(updateTimerId);
        scheduledRunAt = nextRunAt;
        scheduleTimer(delay);
      }
      return;
    }

    updateScheduled = true;
    scheduledRunAt = nextRunAt;
    scheduleTimer(delay);
  };

  ensureStyle();

  const intersectionObserver = new IntersectionObserver((entries) => {
    // Near viewport turns stay fully visible so scrolling feels normal
    let changed = false;
    for (const entry of entries) {
      const target = entry.target;
      if (!(target instanceof HTMLElement) || !managedNodes.has(target)) {
        continue;
      }

      if (isNearViewportEntry(entry)) {
        if (!nearViewportNodes.has(target)) {
          nearViewportNodes.add(target);
          changed = true;
        }
        continue;
      }

      if (nearViewportNodes.delete(target)) {
        changed = true;
      }
    }

    if (changed) {
      scheduleUpdate("intersection");
    }
  }, {
    root: null,
    rootMargin: `${viewportMargin}px 0px ${viewportMargin}px 0px`,
    threshold: 0
  });

  const observer = new MutationObserver(() => {
    scheduleUpdate("mutation");
  });

  const connectObservers = () => {
    if (observersConnected) {
      return;
    }

    // Watch only the chat root instead of the whole document tree
    const observerRoot = getChatRoot();
    observer.observe(observerRoot, { childList: true, subtree: true });
    observersConnected = true;
  };

  const disconnectObservers = () => {
    if (!observersConnected) {
      return;
    }

    // Disconnect both observers so hidden pages stop doing work
    observer.disconnect();
    intersectionObserver.disconnect();
    observersConnected = false;
  };

  const handleVisibilityChange = () => {
    if (document.visibilityState === "hidden") {
      // A hidden page should drop timers and observer work right away
      cancelPendingUpdate();
      clearManagedNodes();
      disconnectObservers();
      return;
    }

    // Reconnect on return and let the first pass rebuild the viewport map
    connectObservers();
    scheduleUpdate("initial");
  };

  connectObservers();
  scheduleUpdate("initial");

  window.addEventListener("resize", () => {
    scheduleUpdate("resize");
  }, { passive: true });
  document.addEventListener("visibilitychange", handleVisibilityChange, { passive: true });
  window.addEventListener("pagehide", () => {
    // Tear down work before the page leaves the history stack
    cancelPendingUpdate();
    clearManagedNodes();
    disconnectObservers();
  }, { passive: true });
})();
