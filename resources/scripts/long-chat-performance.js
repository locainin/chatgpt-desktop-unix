(() => {
  // Make long chats cheaper by lowering work on old offscreen turns
  const trustedOrigins = globalThis.__chatgptDesktopTrustedOrigins;
  if (!trustedOrigins?.isTrustedLocation(window.location)) {
    return;
  }

  if (window.__chatgptDesktopLongChatPerfInstalled) {
    return;
  }
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

  const collectMessageNodes = () => {
    // Prefer the real chat turn markers before falling back to generic articles
    const selectors = [
      "article[data-testid*='conversation-turn']",
      "li[data-message-author-role]",
      "div[data-message-author-role]"
    ];
    const seen = new Set();
    const nodes = [];

    for (const selector of selectors) {
      const results = document.querySelectorAll(selector);
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
      const results = document.querySelectorAll("article");
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

    // Keep some old and new turns so both ends still get handled
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
      managedNodes.add(node);
      nearViewportNodes.add(node);
      intersectionObserver.observe(node);
    }
  };

  const updateOptimization = (reason) => {
    // Drop nodes that vanished before touching layout classes again
    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !node.isConnected) {
        intersectionObserver.unobserve(node);
        nearViewportNodes.delete(node);
        managedNodes.delete(node);
      }
    }

    const hasChatTurns = !!document.querySelector(
      "article[data-testid*='conversation-turn'],li[data-message-author-role],div[data-message-author-role]"
    );
    if (!hasChatTurns) {
      clearManagedNodes();
      return;
    }

    const messages = collectMessageNodes();
    if (messages.length < minMessageCount) {
      clearManagedNodes();
      return;
    }

    syncManagedNodes(messages);
    const recentStart = Math.max(0, messages.length - keepRecentCount);
    for (let index = 0; index < messages.length; ++index) {
      const message = messages[index];
      const isRecent = index >= recentStart;
      const nearViewport = reason === "initial"
        ? true
        : nearViewportNodes.has(message);
      const shouldOptimize = !isRecent && !nearViewport;
      message.classList.toggle(optimizedClass, shouldOptimize);
      message.classList.toggle(recentClass, !shouldOptimize);
    }
  };

  const scheduleUpdate = (reason = "mutation") => {
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
      updateOptimization(reasonForRun);
    };

    const scheduleTimer = (delayMs) => {
      updateTimerId = window.setTimeout(() => {
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
  scheduleUpdate("initial");

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
  const observerRoot = document.querySelector("main")
    || document.body
    || document.documentElement;
  observer.observe(observerRoot, { childList: true, subtree: true });

  window.addEventListener("resize", () => {
    scheduleUpdate("resize");
  }, { passive: true });
})();
