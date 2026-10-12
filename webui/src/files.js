"use strict";
// SD card / flash file access through FluidNC's WebDAV endpoints
// (/sd/... and /flash/...): PROPFIND to list, PUT to upload, GET, DELETE.
const Files = (() => {
  const roots = { sd: "/sd", flash: "/flash" };

  const encPath = (p) => p.split("/").map(encodeURIComponent).join("/");
  const url = (fs, path) => HTTP_BASE + roots[fs] + encPath(path);

  // At most MAX_PARALLEL requests to the controller at once. A browser opens
  // up to 6 connections; every one costs the controller network buffers and
  // shares one SPI Ethernet chip with the SD card - a burst of them (reload:
  // file list, tools, macros, preview, G-code text) is what overloaded it.
  // A request that gets no answer in time is aborted, so one stuck request
  // never blocks the others.
  const MAX_PARALLEL = 2;
  let running = 0;
  const waiting = [];
  function limited(fn) {
    return new Promise((resolve, reject) => {
      waiting.push({ fn, resolve, reject });
      next();
    });
  }
  function next() {
    while (running < MAX_PARALLEL && waiting.length) {
      const { fn, resolve, reject } = waiting.shift();
      running++;
      Promise.resolve().then(fn).then(resolve, reject).finally(() => { running--; next(); });
    }
  }
  async function req(u, opts = {}, timeoutMs = 30000) {
    const ctl = new AbortController();
    const t = setTimeout(() => ctl.abort(), timeoutMs);
    try {
      return await fetch(u, { ...opts, signal: ctl.signal });
    } catch (e) {
      throw new Error(e.name === "AbortError" ? "no answer from the controller" : "network error");
    } finally {
      clearTimeout(t);
    }
  }

  async function list(fs, dir) {
    const d = dir.endsWith("/") ? dir : dir + "/";
    const body = await limited(async () => {
      const r = await req(url(fs, d), { method: "PROPFIND", headers: { Depth: "1" } }, 20000);
      if (!r.ok) throw new Error(`List failed (${r.status})`);
      return r.text();
    });
    const xml = new DOMParser().parseFromString(body, "application/xml");
    const out = [];
    for (const resp of xml.getElementsByTagNameNS("*", "response")) {
      const href = (resp.getElementsByTagNameNS("*", "href")[0] || {}).textContent || "";
      let p = decodeURIComponent(href);
      if (p.startsWith(roots[fs])) p = p.slice(roots[fs].length);
      if (!p.startsWith("/")) p = "/" + p;
      const isDir = resp.getElementsByTagNameNS("*", "collection").length > 0;
      const clean = p.replace(/\/+$/, "") || "/";
      if (clean === (d.replace(/\/+$/, "") || "/")) continue; // the directory itself
      const name = clean.slice(clean.lastIndexOf("/") + 1);
      if (!name || name.startsWith(".")) continue;
      const len = resp.getElementsByTagNameNS("*", "getcontentlength")[0];
      out.push({ name, path: clean, dir: isDir, size: len ? Number(len.textContent) : 0 });
    }
    out.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name, undefined, { numeric: true }));
    return out;
  }

  function upload(fs, dir, file, onProgress) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest();
      const target = (dir.endsWith("/") ? dir : dir + "/") + file.name;
      xhr.open("PUT", url(fs, target));
      xhr.upload.onprogress = (e) => e.lengthComputable && onProgress && onProgress(e.loaded / e.total);
      xhr.onload = () => (xhr.status >= 200 && xhr.status < 300 ? resolve() : reject(new Error(`Upload failed (${xhr.status})`)));
      xhr.onerror = () => reject(new Error("Upload failed (network)"));
      xhr.send(file);
    });
  }

  // Reads of the same file at the same time share one request; a read that
  // fails on the network (not a 404) is tried once more.
  const reading = new Map();
  function getText(fs, path) {
    const k = fs + ":" + path;
    if (reading.has(k)) return reading.get(k);
    const once = () => limited(async () => {
      const r = await req(url(fs, path), { cache: "no-store" }, 60000);
      if (!r.ok) { const e = new Error(`Read failed (${r.status})`); e.status = r.status; throw e; }
      return r.text();
    });
    const p = once()
      .catch((e) => (e.status ? Promise.reject(e) : new Promise((ok) => setTimeout(ok, 1500)).then(once)))
      .finally(() => reading.delete(k));
    reading.set(k, p);
    return p;
  }

  function putText(fs, path, text) {
    return limited(async () => {
      const r = await req(url(fs, path), { method: "PUT", body: new Blob([text], { type: "text/plain" }) });
      if (!r.ok) throw new Error(`Save failed (${r.status})`);
    });
  }

  function remove(fs, path) {
    return limited(async () => {
      const r = await req(url(fs, path), { method: "DELETE" }, 20000);
      if (!r.ok) throw new Error(`Delete failed (${r.status})`);
    });
  }

  // FluidNC command that runs a file from the given filesystem.
  function runCommand(fs, path) {
    return (fs === "sd" ? "$SD/Run=" : "$LocalFS/Run=") + path;
  }

  return { list, upload, getText, putText, remove, runCommand };
})();
