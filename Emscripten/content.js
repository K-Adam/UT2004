// The engine side of the content kept in IndexedDB (Emscripten/index.html), linked with --js-library.
// index.html creates each content file as an empty node with its size and contentPath. Packages stay open
// for as long as their objects are loaded, so the engine reads them in chunks instead of holding whole files.
// Each call returns a Promise, and so suspends the engine, only when the data has to come from IndexedDB.

addToLibrary({
  $UTContent: {
    chunk: 1 << 20,
    // Chunks up to this many bytes stay in memory, least recently used first.
    budget: 256 << 20,
    chunks: new Map(),
    // Whole files read for fopen, which bypasses ut_content_read.
    whole: new Set(),
    async load(node, index) {
      node.contentBlob ??= await Module.readContent(node.contentPath);
      const start = index * UTContent.chunk;
      return new Uint8Array(await node.contentBlob.slice(start, start + UTContent.chunk).arrayBuffer());
    },
  },

  // read() for FArchiveFileReader.
  ut_content_read__deps: ['$UTContent', '$FS'],
  ut_content_read__sig: 'iipi',
  ut_content_read__async: true,
  ut_content_read: (fd, data, count) => {
    const stream = FS.getStreamChecked(fd);
    const node = stream.node;
    if (!node.contentPath || node.contents) return FS.read(stream, HEAP8, data, count);
    const start = stream.position;
    const end = Math.min(start + count, node.usedBytes);
    if (end <= start) return 0;
    const { chunk, chunks } = UTContent;
    // Inode numbers are unique and files stay below 4 GB, so this names one chunk of one file.
    const key = (index) => node.id * 4096 + index;
    const first = Math.floor(start / chunk), last = Math.floor((end - 1) / chunk);
    const copy = () => {
      for (let index = first; index <= last; index++) {
        const bytes = chunks.get(key(index));
        chunks.delete(key(index));
        chunks.set(key(index), bytes);
        const from = Math.max(start, index * chunk), to = Math.min(end, (index + 1) * chunk);
        HEAPU8.set(bytes.subarray(from - index * chunk, to - index * chunk), data + from - start);
      }
      stream.position = end;
      let bytes = chunks.size * chunk;
      for (const old of chunks.keys()) {
        if (bytes <= UTContent.budget) break;
        chunks.delete(old);
        bytes -= chunk;
      }
      return end - start;
    };
    const missing = [];
    for (let index = first; index <= last; index++) if (!chunks.has(key(index))) missing.push(index);
    if (!missing.length) return copy();
    // A failed read reaches FArchiveFileReader as one, for example when the browser cleared the storage.
    return Promise.all(missing.map(async (index) => chunks.set(key(index), await UTContent.load(node, index)))).then(copy, (e) => {
      err(e);
      return -1;
    });
  },

  // Reads the whole file before fopen, for the music streams.
  ut_content_open__deps: ['$UTContent', '$FS', '$UTF8ToString'],
  ut_content_open__sig: 'vp',
  ut_content_open__async: true,
  ut_content_open: (path) => {
    const node = FS.analyzePath(UTF8ToString(path)).object;
    if (!node?.contentPath || node.contents) return;
    return (async () => {
      node.contents = new Uint8Array(await (await Module.readContent(node.contentPath)).arrayBuffer());
      for (const old of UTContent.whole) {
        if (!FS.streams.some((stream) => stream?.node === old)) {
          old.contents = null;
          UTContent.whole.delete(old);
        }
      }
      UTContent.whole.add(node);
    })();
  },

  ut_next_frame__sig: 'i',
  ut_next_frame__async: true,
  ut_next_frame: () => new Promise((resolve) => requestAnimationFrame(() => resolve(Module.stopRequested ? 1 : 0))),

  // The page reloads or closes after this, which must wait until the inis appExit wrote are in IndexedDB.
  ut_exit__deps: ['$FS', '$UTF8ToString'],
  ut_exit__sig: 'vp',
  ut_exit: (cmd) => {
    const server = UTF8ToString(cmd);
    FS.syncfs(false, () => Module.gameExit(server));
  },
});
