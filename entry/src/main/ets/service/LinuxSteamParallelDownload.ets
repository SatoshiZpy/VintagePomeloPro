import { http } from '@kit.NetworkKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { LinuxSteamDownloadArchive } from '../model/LinuxSteamDownloadModels';

export class LinuxSteamDownloadError extends Error {
  code: string;
  constructor(code: string, message: string) { super(message); this.code = code; }
}

const SOURCES: string[] = ['https://client-update.steamstatic.com/',
  'https://media.steampowered.com/client/', 'https://cdn.akamai.steamstatic.com/client/'];
const BLOCK = 1048576;
const PROBE = 262144;
const CONNECTIONS = 4;

interface RangeResult {
  source: number;
  bytes: ArrayBuffer | null;
  error: LinuxSteamDownloadError | null;
}

// A group owns its requests, so selecting a mirror or failing one block never
// leaves background transfers running. Only a contiguous, validated prefix is
// appended to the existing .part file; no OHOS positional writes are used.
class RangeGroup {
  cancelled: boolean = false;
  stops: Set<() => void> = new Set<() => void>();
  cancel(): void {
    this.cancelled = true;
    this.stops.forEach((stop: () => void): void => { stop(); });
  }
}

export class LinuxSteamParallelDownload {
  private stopped: boolean = false;
  private group: RangeGroup | null = null;
  pause(): void { this.stopped = true; this.group?.cancel(); }
  private check(): void {
    if (this.stopped) throw new LinuxSteamDownloadError('PAUSED', '下载已暂停，可继续');
  }

  private async range(archive: LinuxSteamDownloadArchive, source: number, start: number, end: number,
    group: RangeGroup, progress: (received: number) => void, probe: boolean, allowSlow: boolean = false): Promise<ArrayBuffer> {
    this.check();
    if (group.cancelled) throw new LinuxSteamDownloadError('CANCELLED', '下载连接已取消');
    const request = http.createHttp();
    const output = new Uint8Array(end - start + 1);
    let received = 0;
    let headerReady = false;
    let ended = false;
    let failure: LinuxSteamDownloadError | null = null;
    let finish: (() => void) | null = null;
    let interrupt: ((code: number) => void) | null = null;
    const completion = new Promise<void>((resolve: () => void): void => { finish = resolve; });
    const interruption = new Promise<number>((resolve: (code: number) => void): void => { interrupt = resolve; });
    const fail = (code: string, message: string): void => {
      if (!failure) failure = new LinuxSteamDownloadError(code, message);
      request.destroy(); finish?.(); interrupt?.(-1);
    };
    const stop = (): void => { fail('CANCELLED', '下载连接已取消'); };
    group.stops.add(stop);
    const headers = (value: Object): void => {
      const record = value as Record<string, string>;
      let contentRange = ''; let statusLine = '';
      Object.keys(record).forEach((key: string): void => {
        if (key.toLowerCase() === 'content-range') contentRange = String(record[key]);
        if (key.toLowerCase() === 'status-line') statusLine = String(record[key]);
      });
      const status = /\s([0-9]{3})(?:\s|$)/.exec(statusLine);
      const code = status ? Number(status[1]) : contentRange ? 206 : 0;
      headerReady = false;
      if (code >= 300 && code < 400) return;
      if (code === 200 || code === 0) {
        fail('RANGE_UNSUPPORTED', '当前线路不支持分段下载，将使用普通下载'); return;
      }
      if (code !== 206) { fail(`HTTP_${code}`, `下载服务返回 HTTP ${code}`); return; }
      if (contentRange !== `bytes ${start}-${end}/${archive.bytes}`) {
        fail('RANGE', '下载服务器分段响应不正确'); return;
      }
      headerReady = true;
    };
    const data = (bytes: ArrayBuffer): void => {
      if (failure || this.stopped || group.cancelled) return;
      if (!headerReady) { fail('RANGE', '下载响应缺少分段信息'); return; }
      if (received + bytes.byteLength > output.byteLength) { fail('SIZE', '下载分段大小不正确'); return; }
      output.set(new Uint8Array(bytes), received); received += bytes.byteLength; progress(received);
    };
    const endData = (): void => { ended = true; finish?.(); };
    let sample = 0;
    let slowTimer = -1;
    const sampleSpeed = (): void => {
      if (ended || failure || group.cancelled) return;
      // Keep measuring, including stalls after a fast start. 64 KiB/s is a
      // reason to compare the other official sources, not a success condition.
      if (received - sample < 524288) { fail('SLOW', '当前官方线路较慢，正在重新选择线路'); return; }
      sample = received; slowTimer = setTimeout(sampleSpeed, 8000);
    };
    const deadline = setTimeout((): void => { fail('TIMEOUT', '下载连接超时，将尝试其他官方线路'); }, probe ? 15000 : 120000);
    if (!probe && !allowSlow) slowTimer = setTimeout(sampleSpeed, 8000);
    try {
      request.on('headersReceive', headers); request.on('dataReceive', data); request.on('dataEnd', endData);
      const url = SOURCES[source] + archive.url.substring(archive.url.lastIndexOf('/') + 1);
      const code = await Promise.race([interruption, request.requestInStream(url, {
        method: http.RequestMethod.GET,
        header: { 'accept-encoding': 'identity', 'range': `bytes=${start}-${end}` },
        connectTimeout: 10000, readTimeout: 120000, usingCache: false
      })]);
      if (!ended && !failure) await completion;
      this.check();
      if (failure) throw failure as LinuxSteamDownloadError;
      if (code !== 206 || !headerReady || !ended || received !== output.byteLength) {
        throw new LinuxSteamDownloadError('INCOMPLETE', '组件分段未下载完整');
      }
      return output.buffer;
    } finally {
      clearTimeout(deadline); if (slowTimer >= 0) clearTimeout(slowTimer);
      group.stops.delete(stop);
      request.off('headersReceive', headers); request.off('dataReceive', data); request.off('dataEnd', endData);
      request.destroy();
    }
  }

  private async choose(archive: LinuxSteamDownloadArchive, offset: number, excluded: Set<number>): Promise<RangeResult> {
    const group = new RangeGroup(); this.group = group;
    const sources = [0, 1, 2].filter((source: number): boolean => !excluded.has(source));
    let remaining = sources.length;
    let selected = false;
    let win: ((result: RangeResult) => void) | null = null;
    const winner = new Promise<RangeResult>((resolve: (result: RangeResult) => void): void => { win = resolve; });
    const tasks = sources.map(async (source: number): Promise<void> => {
      let result: RangeResult;
      try {
        const bytes = await this.range(archive, source, offset, Math.min(offset + PROBE, archive.bytes) - 1,
          group, (): void => {}, true);
        result = { source, bytes, error: null };
      } catch (error) {
        result = { source, bytes: null, error: new LinuxSteamDownloadError('SOURCE', (error as Error).message) };
      }
      remaining--;
      if (!selected && (result.bytes !== null || remaining === 0)) {
        selected = true; win?.(result); group.cancel();
      }
    });
    const result = await winner;
    await Promise.all(tasks); this.group = null; this.check();
    if (!result.bytes) throw result.error as LinuxSteamDownloadError;
    return result;
  }

  async fetch(archive: LinuxSteamDownloadArchive, partial: string, offset: number,
    progress: (received: number) => void): Promise<void> {
    const output = fs.openSync(partial, fs.OpenMode.CREATE | fs.OpenMode.WRITE_ONLY | fs.OpenMode.APPEND | fs.OpenMode.NOFOLLOW);
    const excluded = new Set<number>();
    let source = -1;
    const append = (bytes: ArrayBuffer): void => {
      try {
        let sent = 0;
        while (sent < bytes.byteLength) {
          const written = fs.writeSync(output.fd, bytes.slice(sent));
          if (written <= 0) throw new Error('short write');
          sent += written; offset += written;
        }
      } catch (_) { throw new LinuxSteamDownloadError('DISK', '无法写入下载文件，请检查剩余存储空间'); }
    };
    try {
      while (offset < archive.bytes) {
        this.check();
        if (source < 0) {
          const chosen = await this.choose(archive, offset, excluded);
          source = chosen.source; append(chosen.bytes as ArrayBuffer); progress(offset);
          if (offset === archive.bytes) break;
        }
        const group = new RangeGroup(); this.group = group;
        const base = offset;
        const counts: number[] = [0, 0, 0, 0];
        const tasks: Promise<RangeResult>[] = [];
        for (let slot = 0; slot < CONNECTIONS && base + slot * BLOCK < archive.bytes; slot++) {
          const index = slot;
          const start = base + slot * BLOCK;
          const transfer = async (): Promise<RangeResult> => {
            try {
              const bytes = await this.range(archive, source, start, Math.min(start + BLOCK, archive.bytes) - 1, group,
                (received: number): void => {
                  counts[index] = received;
                  progress(base + counts.reduce((sum: number, count: number): number => sum + count, 0));
                // If every route is slow, keep the last one parallel instead
                // of repeatedly discarding work and reverting to one stream.
                }, false, excluded.size === SOURCES.length - 1);
              return { source, bytes, error: null };
            } catch (error) {
              group.cancel();
              return { source, bytes: null, error: new LinuxSteamDownloadError('RANGE', (error as Error).message) };
            }
          };
          tasks.push(transfer());
        }
        const results = await Promise.all(tasks); this.group = null;
        // Even on pause/failure, save completed blocks up to the first gap.
        for (const result of results) {
          if (!result.bytes) break;
          append(result.bytes);
        }
        try { fs.fsyncSync(output.fd); }
        catch (_) { throw new LinuxSteamDownloadError('DISK', '无法保存下载文件，请检查剩余存储空间'); }
        this.check(); progress(offset);
        if (results.some((result: RangeResult): boolean => result.error !== null)) {
          excluded.add(source); source = -1;
          if (excluded.size === SOURCES.length) throw new LinuxSteamDownloadError('SOURCES', '官方线路暂时较慢，将尝试普通下载');
        }
      }
    } finally { this.group?.cancel(); this.group = null; fs.closeSync(output); }
  }
}
