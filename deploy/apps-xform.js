/* Turns a page installer script (Studio / Motion card) into one that can also install on the SD
 * card (opts.sd) and exports itself as window.DPC<Name>Installer for the setup flow. */
function dpcXform(src, n, Name) {
  const must = (s, a) => { if (s.split(a).length !== 2) throw new Error(Name + ": anchor not found once: " + a.slice(0, 60)); };
  if (src.includes("DPC" + Name + "Installer")) return src;   // done already
  const a1 = "async function install(W, port, fetchFile, ui) {";
  must(src, a1);
  src = src.replace(a1, "async function install(W, port, fetchFile, ui, opts) {\n    var SD = !!(opts && opts.sd), FS = SD ? \"$SD\" : \"$LocalFS\", TMP = SD ? \"/sd/" + n + ".tmp\" : \"/localfs/" + n + ".tmp\", PAGE = SD ? \"/sd/" + n + ".html.gz\" : \"/localfs/" + n + ".html.gz\";");
  src = src.split('"$LocalFS/List"').join('FS + "/List"').split('"$LocalFS/Delete="').join('FS + "/Delete="').split('"$LocalFS/Rename="').join('FS + "/Rename="');
  const a2 = 'path: board ? "/' + n + '.html" : "/flash/' + n + '.html"';
  must(src, a2);
  src = src.replace(a2, 'path: SD ? "/sd/' + n + '.html" : board ? "/' + n + '.html" : "/flash/' + n + '.html"');
  const a3 = "  function result(ok, html) {";
  must(src, a3);
  src = src.replace(a3, "  root.DPC" + Name + "Installer = { install: install, shop: shop };\n\n" + a3);
  return src;
}
if (typeof module !== "undefined") module.exports = dpcXform;
