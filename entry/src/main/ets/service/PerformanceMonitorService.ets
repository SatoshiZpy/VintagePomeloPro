import { common } from '@kit.AbilityKit';
import ApplicationStateChangeCallback from '@ohos.app.ability.ApplicationStateChangeCallback';
import batteryInfo from '@ohos.batteryInfo';
import thermal from '@ohos.thermal';
import testNapi from 'libentry.so';
import { PerformanceHudSettings, hasPerformanceTelemetry } from '../model/PerformanceHudSettings';
import { CpuSnapshot, calculateCpuUsage, formatCpuPercent,
  formatBatteryTemperature, formatThermalLevel } from '../model/PerformanceMetrics';

export class PerformanceReading {
  appCpu: string = '采样中';
  systemCpu: string = '采样中';
  batteryTemperature: string = '采样中';
  thermalLevel: string = '采样中';
}
export type PerformanceListener = (reading: PerformanceReading, foreground: boolean) => void;

/** Shared by desktop, WineWindow and popup HUDs; no render-loop sampling. */
export class PerformanceMonitorService {
  private static instance: PerformanceMonitorService = new PerformanceMonitorService();
  private subscribers: Map<PerformanceListener, PerformanceHudSettings> = new Map();
  private context: common.ApplicationContext | null = null;
  private foreground: boolean = true;
  private timer: number = -1;
  private epoch: number = 0;
  private busy: boolean = false;
  private lastSampleMs: number = 0;
  private previousCpu: CpuSnapshot | null = null;
  private reading: PerformanceReading = new PerformanceReading();
  private lifecycle: ApplicationStateChangeCallback = {
    onApplicationForeground: (): void => { this.setForeground(true); },
    onApplicationBackground: (): void => { this.setForeground(false); }
  };

  static getInstance(): PerformanceMonitorService { return PerformanceMonitorService.instance; }

  subscribe(context: common.UIAbilityContext, listener: PerformanceListener, metrics: PerformanceHudSettings): void {
    if (this.subscribers.size === 0) {
      this.foreground = true;
      try {
        this.context = context.getApplicationContext();
        this.context.on('applicationStateChange', this.lifecycle);
      } catch (_) { this.context = null; }
    }
    this.subscribers.set(listener, metrics);
    this.restart();
  }

  unsubscribe(listener: PerformanceListener): void {
    this.subscribers.delete(listener);
    if (this.subscribers.size === 0 && this.context) {
      try { this.context.off('applicationStateChange', this.lifecycle); } catch (_) {}
      this.context = null;
    }
    this.restart();
  }

  private setForeground(value: boolean): void {
    this.foreground = value;
    this.restart();
  }

  private restart(): void {
    if (this.timer >= 0) clearInterval(this.timer);
    this.timer = -1;
    this.epoch++;
    this.previousCpu = null;
    this.lastSampleMs = 0;
    this.reading = new PerformanceReading();
    this.notify();
    if (!this.foreground || this.subscribers.size === 0) return;
    let fps = false;
    let telemetry = false;
    this.subscribers.forEach((metrics: PerformanceHudSettings): void => {
      fps = fps || metrics.fps || metrics.resolution;
      telemetry = telemetry || hasPerformanceTelemetry(metrics);
    });
    if (telemetry) void this.sample();
    if (fps || telemetry) {
      this.timer = setInterval((): void => {
        if (telemetry && Date.now() - this.lastSampleMs >= 2000) void this.sample();
        this.notify();
      }, fps ? 500 : 2000);
    }
  }

  private notify(): void {
    this.subscribers.forEach((_: PerformanceHudSettings, listener: PerformanceListener): void => {
      listener(this.reading, this.foreground);
    });
  }

  private async sample(): Promise<void> {
    if (this.busy || !this.foreground || this.subscribers.size === 0) return;
    this.busy = true;
    const epoch = this.epoch;
    this.lastSampleMs = Date.now();
    let appCpu = false;
    let systemCpu = false;
    let battery = false;
    let heat = false;
    this.subscribers.forEach((metrics: PerformanceHudSettings): void => {
      appCpu = appCpu || metrics.appCpu;
      systemCpu = systemCpu || metrics.systemCpu;
      battery = battery || metrics.batteryTemperature;
      heat = heat || metrics.thermalLevel;
    });
    const next = new PerformanceReading();
    try {
      if (battery) {
        try { next.batteryTemperature = formatBatteryTemperature(batteryInfo.batteryTemperature); }
        catch (_) { next.batteryTemperature = '不可用'; }
      }
      if (heat) {
        try { next.thermalLevel = formatThermalLevel(thermal.getLevel()); }
        catch (_) { next.thermalLevel = '不可用'; }
      }
      if (appCpu || systemCpu) {
        const raw: string = await testNapi.readPerformanceCounters(appCpu, systemCpu) as string;
        if (epoch !== this.epoch) return;
        const current = JSON.parse(raw) as CpuSnapshot;
        const usage = calculateCpuUsage(this.previousCpu, current);
        next.appCpu = !this.previousCpu && current.appReadable ? '采样中' :
          formatCpuPercent(usage.app, usage.partial);
        next.systemCpu = !this.previousCpu && current.systemReadable ? '采样中' :
          formatCpuPercent(usage.system);
        this.previousCpu = current;
      }
    } catch (_) {
      next.appCpu = '不可用';
      next.systemCpu = '不可用';
      if (epoch === this.epoch) this.previousCpu = null;
    } finally {
      this.busy = false;
    }
    // Discard workers finishing after hide/background/reconfiguration.
    if (epoch !== this.epoch) return;
    this.reading = next;
    this.notify();
  }
}
