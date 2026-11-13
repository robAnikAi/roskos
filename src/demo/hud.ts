export interface HudFrame {
  fps: number
  frameMs: number
  width: number
  height: number
  samples: number
  drawCalls: number
  triangles: number
}

function element(id: string): HTMLElement {
  const node = document.getElementById(id)
  if (!node) throw new Error(`HUD: не найден элемент #${id}`)
  return node
}

export class Hud {
  private readonly fps = element('stat-fps')
  private readonly frame = element('stat-frame')
  private readonly resolution = element('stat-res')
  private readonly msaa = element('stat-msaa')
  private readonly draws = element('stat-draws')
  private readonly triangles = element('stat-tris')
  private readonly device = element('stat-gpu')

  setDevice(name: string, coreBytes: number): void {
    this.device.textContent = `rust core · wasm ${(coreBytes / 1024).toFixed(0)} КБ · ${name}`
  }

  update(frame: HudFrame): void {
    this.fps.textContent = frame.fps.toFixed(0)
    this.frame.textContent = `${frame.frameMs.toFixed(1)} мс`
    this.resolution.textContent = `${frame.width}×${frame.height}`
    this.msaa.textContent = frame.samples > 1 ? `${frame.samples}×` : 'выкл'
    this.draws.textContent = String(frame.drawCalls)
    this.triangles.textContent = frame.triangles.toLocaleString('ru-RU')
  }
}
