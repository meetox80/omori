const ERRNO = { SUCCESS: 0, BADF: 8, EXIST: 20, INVAL: 28, ISDIR: 31, NOENT: 44, NOSYS: 52, NOTDIR: 54, NOTEMPTY: 55 };
const FILETYPE = { UNKNOWN: 0, CHARACTER_DEVICE: 2, DIRECTORY: 3, REGULAR_FILE: 4 };
const OPEN = { CREATE: 1, DIRECTORY: 2, EXCLUSIVE: 4, TRUNCATE: 8 };
const FD_APPEND = 1;
const ALL_RIGHTS = 0xffffffffffffffffn;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

let nextInode = 1;

export class MemoryFile {
  constructor(blob = null) {
    this.inode = nextInode++;
    this.blob = blob;
    this.bytes = null;
    this.size = blob ? blob.size : 0;
    this.openCount = 0;
    this.modified = false;
  }

  get isDirectory() {
    return false;
  }

  load() {
    if (!this.bytes) {
      this.bytes = this.blob && this.blob.size ? new Uint8Array(new FileReaderSync().readAsArrayBuffer(this.blob)) : new Uint8Array(0);
      this.size = this.bytes.length;
    }
    return this.bytes;
  }

  reserve(length) {
    const current = this.load();
    if (length <= current.length) return current;
    let capacity = Math.max(4096, current.length);
    while (capacity < length) capacity *= 2;
    const grown = new Uint8Array(capacity);
    grown.set(current.subarray(0, this.size));
    this.bytes = grown;
    this.blob = null;
    this.modified = true;
    return grown;
  }

  truncate() {
    this.blob = null;
    this.bytes = new Uint8Array(0);
    this.size = 0;
    this.modified = true;
  }

  release() {
    this.openCount--;
    if (this.openCount <= 0 && !this.modified && this.blob) this.bytes = null;
  }

  contents() {
    return this.load().slice(0, this.size);
  }

  readInto(target, position) {
    const bytes = this.load();
    const take = Math.max(0, Math.min(target.length, this.size - position));
    if (take > 0) target.set(bytes.subarray(position, position + take));
    return take;
  }

  writeFrom(source, position) {
    const target = this.reserve(position + source.length);
    target.set(source, position);
    this.size = Math.max(this.size, position + source.length);
    return source.length;
  }

  setSize(size) {
    this.reserve(size);
    this.size = size;
  }
}

export class SyncAccessFile {
  constructor(handle) {
    this.inode = nextInode++;
    this.handle = handle;
    this.size = handle.getSize();
    this.openCount = 0;
  }

  get isDirectory() {
    return false;
  }

  truncate() {
    this.handle.truncate(0);
    this.size = 0;
  }

  release() {
    this.openCount--;
    if (this.openCount <= 0) this.handle.flush();
  }

  readInto(target, position) {
    if (position >= this.size) return 0;
    return this.handle.read(target, { at: position });
  }

  writeFrom(source, position) {
    const written = this.handle.write(source, { at: position });
    this.size = Math.max(this.size, position + written);
    return written;
  }

  setSize(size) {
    this.handle.truncate(size);
    this.size = size;
  }
}

export class MemoryDirectory {
  constructor() {
    this.inode = nextInode++;
    this.children = new Map();
  }

  get isDirectory() {
    return true;
  }

  find(parts) {
    let node = this;
    for (const part of parts) {
      if (!node.isDirectory || part === "..") return null;
      node = node.children.get(part);
      if (!node) return null;
    }
    return node;
  }

  makeDirectories(parts) {
    let node = this;
    for (const part of parts) {
      if (!node.children.has(part)) node.children.set(part, new MemoryDirectory());
      node = node.children.get(part);
    }
    return node;
  }

  addFile(path, blob) {
    this.addNode(path, new MemoryFile(blob));
  }

  addNode(path, node) {
    const parts = splitPath(path);
    const parent = this.makeDirectories(parts.slice(0, -1));
    parent.children.set(parts[parts.length - 1], node);
  }
}

export function splitPath(path) {
  return path.split("/").filter((part) => part && part !== ".");
}

class ExitSignal extends Error {
  constructor(code) {
    super(`exit ${code}`);
    this.code = code;
  }
}

export class WasiProcess {
  constructor({ args, env, root, onLine }) {
    this.args = args.map((arg) => encoder.encode(`${arg}\0`));
    this.env = Object.entries(env).map(([key, value]) => encoder.encode(`${key}=${value}\0`));
    this.onLine = onLine;
    this.memory = null;
    this.descriptors = new Map([
      [0, { kind: "stdin" }],
      [1, { kind: "output", stream: 1, pending: "" }],
      [2, { kind: "output", stream: 2, pending: "" }],
      [3, { kind: "node", node: root, position: 0, preopen: "/" }],
    ]);
    this.nextDescriptor = 4;
  }

  run(module) {
    const instance = new WebAssembly.Instance(module, { wasi_snapshot_preview1: this.imports() });
    this.memory = instance.exports.memory;
    let code = 0;
    try {
      instance.exports._start();
    } catch (error) {
      if (!(error instanceof ExitSignal)) throw error;
      code = error.code;
    }
    this.flushOutput(this.descriptors.get(1), true);
    this.flushOutput(this.descriptors.get(2), true);
    return code;
  }

  imports() {
    const calls = {};
    for (const name of Object.getOwnPropertyNames(WasiProcess.prototype)) {
      if (name.startsWith("sys_")) calls[name.slice(4)] = (...params) => this[name](...params);
    }
    return new Proxy(calls, { get: (target, name) => target[name] ?? (() => ERRNO.NOSYS) });
  }

  get view() {
    return new DataView(this.memory.buffer);
  }

  get heap() {
    return new Uint8Array(this.memory.buffer);
  }

  readString(pointer, length) {
    return decoder.decode(this.heap.subarray(pointer, pointer + length));
  }

  ioVectors(pointer, count) {
    const vectors = [];
    for (let i = 0; i < count; i++) {
      vectors.push({ pointer: this.view.getUint32(pointer + i * 8, true), length: this.view.getUint32(pointer + i * 8 + 4, true) });
    }
    return vectors;
  }

  resolve(descriptor, pointer, length) {
    const base = this.descriptors.get(descriptor);
    if (!base || base.kind !== "node" || !base.node.isDirectory) return { error: ERRNO.BADF };
    return { directory: base.node, parts: splitPath(this.readString(pointer, length)) };
  }

  openFile(descriptor) {
    const entry = this.descriptors.get(descriptor);
    if (!entry || entry.kind !== "node" || entry.node.isDirectory) return null;
    return entry;
  }

  flushOutput(entry, final) {
    const lines = entry.pending.split("\n");
    entry.pending = final ? "" : lines.pop();
    for (const line of lines) {
      if (line.length > 0 || !final) this.onLine(line, entry.stream);
    }
  }

  writeFileStat(pointer, node) {
    const view = this.view;
    for (let offset = 0; offset < 64; offset += 8) view.setBigUint64(pointer + offset, 0n, true);
    view.setBigUint64(pointer + 8, BigInt(node.inode), true);
    view.setUint8(pointer + 16, node.isDirectory ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE);
    view.setBigUint64(pointer + 24, 1n, true);
    view.setBigUint64(pointer + 32, BigInt(node.isDirectory ? 0 : node.size), true);
  }

  writeList(list, countPointer, sizePointer) {
    this.view.setUint32(countPointer, list.length, true);
    this.view.setUint32(sizePointer, list.reduce((sum, bytes) => sum + bytes.length, 0), true);
    return ERRNO.SUCCESS;
  }

  copyList(list, pointersPointer, bufferPointer) {
    let cursor = bufferPointer;
    list.forEach((bytes, i) => {
      this.view.setUint32(pointersPointer + i * 4, cursor, true);
      this.heap.set(bytes, cursor);
      cursor += bytes.length;
    });
    return ERRNO.SUCCESS;
  }

  readAt(entry, vectorsPointer, count, position) {
    let cursor = position;
    let total = 0;
    for (const { pointer, length } of this.ioVectors(vectorsPointer, count)) {
      const take = length > 0 ? entry.node.readInto(this.heap.subarray(pointer, pointer + length), cursor) : 0;
      cursor += take;
      total += take;
      if (take < length) break;
    }
    return total;
  }

  writeAt(entry, vectorsPointer, count, position) {
    let cursor = position;
    let total = 0;
    for (const { pointer, length } of this.ioVectors(vectorsPointer, count)) {
      const written = length > 0 ? entry.node.writeFrom(this.heap.subarray(pointer, pointer + length), cursor) : 0;
      cursor += written;
      total += written;
      if (written < length) break;
    }
    return total;
  }

  sys_args_sizes_get(countPointer, sizePointer) {
    return this.writeList(this.args, countPointer, sizePointer);
  }

  sys_args_get(pointers, buffer) {
    return this.copyList(this.args, pointers, buffer);
  }

  sys_environ_sizes_get(countPointer, sizePointer) {
    return this.writeList(this.env, countPointer, sizePointer);
  }

  sys_environ_get(pointers, buffer) {
    return this.copyList(this.env, pointers, buffer);
  }

  sys_clock_res_get(id, resultPointer) {
    this.view.setBigUint64(resultPointer, 1000n, true);
    return ERRNO.SUCCESS;
  }

  sys_clock_time_get(id, precision, resultPointer) {
    const nanoseconds = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
    this.view.setBigUint64(resultPointer, nanoseconds, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_prestat_get(descriptor, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (!entry?.preopen) return ERRNO.BADF;
    this.view.setUint8(resultPointer, 0);
    this.view.setUint32(resultPointer + 4, encoder.encode(entry.preopen).length, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_prestat_dir_name(descriptor, pointer, length) {
    const entry = this.descriptors.get(descriptor);
    if (!entry?.preopen) return ERRNO.BADF;
    this.heap.set(encoder.encode(entry.preopen).subarray(0, length), pointer);
    return ERRNO.SUCCESS;
  }

  sys_fd_fdstat_get(descriptor, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (!entry) return ERRNO.BADF;
    let type = FILETYPE.UNKNOWN;
    if (entry.kind === "node") type = entry.node.isDirectory ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE;
    this.view.setUint8(resultPointer, type);
    this.view.setUint16(resultPointer + 2, 0, true);
    this.view.setBigUint64(resultPointer + 8, ALL_RIGHTS, true);
    this.view.setBigUint64(resultPointer + 16, ALL_RIGHTS, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_fdstat_set_flags() {
    return ERRNO.SUCCESS;
  }

  sys_fd_filestat_get(descriptor, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (!entry) return ERRNO.BADF;
    if (entry.kind === "node") {
      this.writeFileStat(resultPointer, entry.node);
    } else {
      for (let offset = 0; offset < 64; offset += 8) this.view.setBigUint64(resultPointer + offset, 0n, true);
      this.view.setUint8(resultPointer + 16, FILETYPE.CHARACTER_DEVICE);
    }
    return ERRNO.SUCCESS;
  }

  sys_fd_filestat_set_size(descriptor, size) {
    const entry = this.openFile(descriptor);
    if (!entry) return ERRNO.BADF;
    entry.node.setSize(Number(size));
    return ERRNO.SUCCESS;
  }

  sys_path_filestat_get(descriptor, flags, pathPointer, pathLength, resultPointer) {
    const { error, directory, parts } = this.resolve(descriptor, pathPointer, pathLength);
    if (error) return error;
    const node = directory.find(parts);
    if (!node) return ERRNO.NOENT;
    this.writeFileStat(resultPointer, node);
    return ERRNO.SUCCESS;
  }

  sys_path_open(descriptor, lookupFlags, pathPointer, pathLength, openFlags, rightsBase, rightsInheriting, fdFlags, resultPointer) {
    const { error, directory, parts } = this.resolve(descriptor, pathPointer, pathLength);
    if (error) return error;

    let node = directory.find(parts);
    if (node && openFlags & OPEN.CREATE && openFlags & OPEN.EXCLUSIVE) return ERRNO.EXIST;
    if (!node) {
      if (!(openFlags & OPEN.CREATE) || parts.length === 0) return ERRNO.NOENT;
      const parent = directory.find(parts.slice(0, -1));
      if (!parent?.isDirectory) return ERRNO.NOENT;
      node = new MemoryFile();
      node.truncate();
      parent.children.set(parts[parts.length - 1], node);
    }
    if (openFlags & OPEN.DIRECTORY && !node.isDirectory) return ERRNO.NOTDIR;
    if (!node.isDirectory) {
      if (openFlags & OPEN.TRUNCATE) node.truncate();
      node.openCount++;
    }

    const id = this.nextDescriptor++;
    this.descriptors.set(id, { kind: "node", node, position: 0, append: Boolean(fdFlags & FD_APPEND) });
    this.view.setUint32(resultPointer, id, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_close(descriptor) {
    const entry = this.descriptors.get(descriptor);
    if (!entry) return ERRNO.BADF;
    if (entry.kind === "node" && !entry.node.isDirectory) entry.node.release();
    if (entry.kind === "output") this.flushOutput(entry, true);
    this.descriptors.delete(descriptor);
    return ERRNO.SUCCESS;
  }

  sys_fd_read(descriptor, vectorsPointer, count, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (entry?.kind === "stdin") {
      this.view.setUint32(resultPointer, 0, true);
      return ERRNO.SUCCESS;
    }
    if (!this.openFile(descriptor)) return ERRNO.BADF;
    const read = this.readAt(entry, vectorsPointer, count, entry.position);
    entry.position += read;
    this.view.setUint32(resultPointer, read, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_pread(descriptor, vectorsPointer, count, offset, resultPointer) {
    const entry = this.openFile(descriptor);
    if (!entry) return ERRNO.BADF;
    this.view.setUint32(resultPointer, this.readAt(entry, vectorsPointer, count, Number(offset)), true);
    return ERRNO.SUCCESS;
  }

  sys_fd_write(descriptor, vectorsPointer, count, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (entry?.kind === "output") {
      let total = 0;
      for (const { pointer, length } of this.ioVectors(vectorsPointer, count)) {
        entry.pending += decoder.decode(this.heap.subarray(pointer, pointer + length));
        total += length;
      }
      this.flushOutput(entry, false);
      this.view.setUint32(resultPointer, total, true);
      return ERRNO.SUCCESS;
    }
    if (!this.openFile(descriptor)) return ERRNO.BADF;
    if (entry.append) entry.position = entry.node.size;
    const written = this.writeAt(entry, vectorsPointer, count, entry.position);
    entry.position += written;
    this.view.setUint32(resultPointer, written, true);
    return ERRNO.SUCCESS;
  }

  sys_fd_pwrite(descriptor, vectorsPointer, count, offset, resultPointer) {
    const entry = this.openFile(descriptor);
    if (!entry) return ERRNO.BADF;
    this.view.setUint32(resultPointer, this.writeAt(entry, vectorsPointer, count, Number(offset)), true);
    return ERRNO.SUCCESS;
  }

  sys_fd_seek(descriptor, offset, whence, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (entry?.kind !== "node") return ERRNO.BADF;
    const origins = [0, entry.position, entry.node.size ?? 0];
    const position = origins[whence] + Number(offset);
    if (position < 0) return ERRNO.INVAL;
    entry.position = position;
    this.view.setBigUint64(resultPointer, BigInt(position), true);
    return ERRNO.SUCCESS;
  }

  sys_fd_tell(descriptor, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (entry?.kind !== "node") return ERRNO.BADF;
    this.view.setBigUint64(resultPointer, BigInt(entry.position), true);
    return ERRNO.SUCCESS;
  }

  sys_fd_readdir(descriptor, bufferPointer, bufferLength, cookie, resultPointer) {
    const entry = this.descriptors.get(descriptor);
    if (entry?.kind !== "node" || !entry.node.isDirectory) return ERRNO.BADF;

    const names = [...entry.node.children.keys()];
    const end = bufferPointer + bufferLength;
    let cursor = bufferPointer;

    for (let i = Number(cookie); i < names.length && cursor < end; i++) {
      const name = encoder.encode(names[i]);
      const child = entry.node.children.get(names[i]);
      const record = new Uint8Array(24 + name.length);
      const header = new DataView(record.buffer);
      header.setBigUint64(0, BigInt(i + 1), true);
      header.setBigUint64(8, BigInt(child.inode), true);
      header.setUint32(16, name.length, true);
      header.setUint8(20, child.isDirectory ? FILETYPE.DIRECTORY : FILETYPE.REGULAR_FILE);
      record.set(name, 24);

      const take = Math.min(record.length, end - cursor);
      this.heap.set(record.subarray(0, take), cursor);
      cursor += take;
    }

    this.view.setUint32(resultPointer, cursor - bufferPointer, true);
    return ERRNO.SUCCESS;
  }

  sys_path_create_directory(descriptor, pathPointer, pathLength) {
    const { error, directory, parts } = this.resolve(descriptor, pathPointer, pathLength);
    if (error) return error;
    if (directory.find(parts)) return ERRNO.EXIST;
    const parent = directory.find(parts.slice(0, -1));
    if (!parent?.isDirectory) return ERRNO.NOENT;
    parent.children.set(parts[parts.length - 1], new MemoryDirectory());
    return ERRNO.SUCCESS;
  }

  sys_path_rename(fromDescriptor, fromPointer, fromLength, toDescriptor, toPointer, toLength) {
    const from = this.resolve(fromDescriptor, fromPointer, fromLength);
    const to = this.resolve(toDescriptor, toPointer, toLength);
    if (from.error || to.error) return from.error || to.error;

    const fromParent = from.directory.find(from.parts.slice(0, -1));
    const toParent = to.directory.find(to.parts.slice(0, -1));
    const fromName = from.parts[from.parts.length - 1];
    if (!fromParent?.children.has(fromName) || !toParent?.isDirectory) return ERRNO.NOENT;

    const node = fromParent.children.get(fromName);
    fromParent.children.delete(fromName);
    toParent.children.set(to.parts[to.parts.length - 1], node);
    return ERRNO.SUCCESS;
  }

  sys_path_unlink_file(descriptor, pathPointer, pathLength) {
    const { error, directory, parts } = this.resolve(descriptor, pathPointer, pathLength);
    if (error) return error;
    const parent = directory.find(parts.slice(0, -1));
    const name = parts[parts.length - 1];
    const node = parent?.children.get(name);
    if (!node) return ERRNO.NOENT;
    if (node.isDirectory) return ERRNO.ISDIR;
    parent.children.delete(name);
    return ERRNO.SUCCESS;
  }

  sys_path_remove_directory(descriptor, pathPointer, pathLength) {
    const { error, directory, parts } = this.resolve(descriptor, pathPointer, pathLength);
    if (error) return error;
    const parent = directory.find(parts.slice(0, -1));
    const name = parts[parts.length - 1];
    const node = parent?.children.get(name);
    if (!node) return ERRNO.NOENT;
    if (!node.isDirectory) return ERRNO.NOTDIR;
    if (node.children.size > 0) return ERRNO.NOTEMPTY;
    parent.children.delete(name);
    return ERRNO.SUCCESS;
  }

  sys_random_get(pointer, length) {
    for (let offset = 0; offset < length; offset += 65536) {
      crypto.getRandomValues(this.heap.subarray(pointer + offset, pointer + Math.min(length, offset + 65536)));
    }
    return ERRNO.SUCCESS;
  }

  sys_proc_exit(code) {
    throw new ExitSignal(code);
  }

  sys_sched_yield() {
    return ERRNO.SUCCESS;
  }

  sys_fd_advise() {
    return ERRNO.SUCCESS;
  }

  sys_fd_allocate() {
    return ERRNO.SUCCESS;
  }

  sys_fd_datasync() {
    return ERRNO.SUCCESS;
  }

  sys_fd_sync() {
    return ERRNO.SUCCESS;
  }
}
