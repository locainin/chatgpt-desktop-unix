(() => {
  // Make long chats cheaper by lowering work on old offscreen turns
  const host = window.location.hostname || "";
  const trusted = /(^|\.)chatgpt\.com$/i.test(host)
    || /(^|\.)openai\.com$/i.test(host)
    || /(^|\.)oaistatic\.com$/i.test(host);
  if (!trusted) {
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
  const minMutationUpdateMs = 650;
  const minViewportUpdateMs = 120;
  const managedNodes = new Set();
  let nearViewportNodes = new WeakSet();
  let updateScheduled = false;
  let scheduledReason = "initial";
  let lastUpdateAt = 0;

  const reasonPriority = {
    mutation: 1,
    scroll: 2,
    resize: 2,
    initial: 3
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
    for (const node of managedNodes) {
      if (!(node instanceof HTMLElement)) {
        continue;
      }
      node.classList.remove(optimizedClass);
      node.classList.remove(recentClass);
    }
    managedNodes.clear();
  };

  const isNearViewport = (node) => {
    const rect = node.getBoundingClientRect();
    return rect.bottom >= -viewportMargin
      && rect.top <= (window.innerHeight + viewportMargin);
  };

  const updateOptimization = (reason) => {
    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !node.isConnected) {
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

    const selectedNodes = new Set(messages);
    for (const node of Array.from(managedNodes)) {
      if (!(node instanceof HTMLElement) || !selectedNodes.has(node)) {
        if (node instanceof HTMLElement) {
          node.classList.remove(optimizedClass);
          node.classList.remove(recentClass);
        }
        managedNodes.delete(node);
      }
    }

    const updateViewportMap = reason === "scroll" || reason === "resize" || reason === "initial";
    const nextNearViewportNodes = updateViewportMap ? new WeakSet() : nearViewportNodes;
    const recentStart = Math.max(0, messages.length - keepRecentCount);
    for (let index = 0; index < messages.length; ++index) {
      const message = messages[index];
      managedNodes.add(message);
      const isRecent = index >= recentStart;
      const nearViewport = updateViewportMap
        ? isNearViewport(message)
        : nearViewportNodes.has(message);
      if (updateViewportMap && nearViewport) {
        nextNearViewportNodes.add(message);
      }
      const shouldOptimize = !isRecent && !nearViewport;
      message.classList.toggle(optimizedClass, shouldOptimize);
      message.classList.toggle(recentClass, !shouldOptimize);
    }

    if (updateViewportMap) {
      nearViewportNodes = nextNearViewportNodes;
    }
  };

  const scheduleUpdate = (reason = "mutation") => {
    if (reasonPriority[reason] > reasonPriority[scheduledReason]) {
      scheduledReason = reason;
    }

    if (updateScheduled) {
      return;
    }
    updateScheduled = true;

    const run = () => {
      updateScheduled = false;
      const reasonForRun = scheduledReason;
      scheduledReason = "mutation";
      lastUpdateAt = performance.now();
      updateOptimization(reasonForRun);
    };

    const now = performance.now();
    const minGap = reason === "scroll" || reason === "resize"
      ? minViewportUpdateMs
      : minMutationUpdateMs;
    const delay = Math.max(0, minGap - (now - lastUpdateAt));

    window.setTimeout(() => {
      if (typeof window.requestIdleCallback === "function") {
        window.requestIdleCallback(run, { timeout: 250 });
        return;
      }
      run();
    }, delay);
  };

  ensureStyle();
  scheduleUpdate("initial");

  const observer = new MutationObserver(() => {
    scheduleUpdate("mutation");
  });
  const observerRoot = document.querySelector("main")
    || document.body
    || document.documentElement;
  observer.observe(observerRoot, { childList: true, subtree: true });

  window.addEventListener("scroll", () => {
    scheduleUpdate("scroll");
  }, { passive: true });
  window.addEventListener("resize", () => {
    scheduleUpdate("resize");
  }, { passive: true });
})();
