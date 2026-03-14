(() => {
  // Keep the browser-side allowlist in one injected helper
  const trustedHostPattern = /(^|\.)chatgpt\.com$|(^|\.)openai\.com$|(^|\.)oaistatic\.com$/i;

  const isTrustedHost = (host) => {
    if (typeof host !== "string" || host.length === 0) {
      return false;
    }
    return trustedHostPattern.test(host);
  };

  const isTrustedLocation = (locationLike) => {
    if (!locationLike || typeof locationLike.protocol !== "string") {
      return false;
    }
    if (locationLike.protocol !== "https:") {
      return false;
    }
    return isTrustedHost(locationLike.hostname || "");
  };

  globalThis.__chatgptDesktopTrustedOrigins = {
    isTrustedHost,
    isTrustedLocation
  };
})();
