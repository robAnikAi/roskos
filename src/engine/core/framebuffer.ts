import type { Device } from './device'
import { Texture } from './texture'

export interface RenderTargetDesc {
  label?: string
  width: number
  height: number
  /** Количество сэмплов MSAA (1 = без сглаживания). */
  samples?: number
  /** Плавающий формат цвета (HDR) — если поддерживается. */
  hdr?: boolean
}

/**
 * Цель рендеринга: цветовая текстура + буфер глубины, опционально
 * мультисэмплированная с автоматическим resolve (blit) в текстуру.
 */
export class RenderTarget {
  readonly label: string
  color!: Texture
  private widthValue: number
  private heightValue: number
  private samplesValue: number
  private hdrValue: boolean

  private framebuffer: WebGLFramebuffer | null = null
  private resolveFramebuffer: WebGLFramebuffer | null = null
  private colorRenderbuffer: WebGLRenderbuffer | null = null
  private depthRenderbuffer: WebGLRenderbuffer | null = null

  constructor(private readonly device: Device, desc: RenderTargetDesc) {
    this.label = desc.label ?? 'render-target'
    this.widthValue = Math.max(1, Math.floor(desc.width))
    this.heightValue = Math.max(1, Math.floor(desc.height))
    this.samplesValue = Math.max(1, Math.floor(desc.samples ?? 1))
    this.hdrValue = desc.hdr ?? device.capabilities.floatTargets
    this.allocate()
  }

  get width(): number {
    return this.widthValue
  }

  get height(): number {
    return this.heightValue
  }

  get samples(): number {
    return this.samplesValue
  }

  get isHdr(): boolean {
    return this.hdrValue
  }

  bind(): void {
    const gl = this.device.gl
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.framebuffer)
    gl.viewport(0, 0, this.widthValue, this.heightValue)
  }

  /** Завершает проход: если включён MSAA — разрешает буфер в текстуру. */
  resolve(): void {
    const gl = this.device.gl
    if (this.resolveFramebuffer) {
      gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.framebuffer)
      gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.resolveFramebuffer)
      gl.blitFramebuffer(
        0,
        0,
        this.widthValue,
        this.heightValue,
        0,
        0,
        this.widthValue,
        this.heightValue,
        gl.COLOR_BUFFER_BIT,
        gl.NEAREST,
      )
    }
    gl.bindFramebuffer(gl.FRAMEBUFFER, null)
  }

  setSamples(samples: number): void {
    const next = Math.max(
      1,
      Math.min(Math.floor(samples), this.device.capabilities.maxSamples),
    )
    if (next === this.samplesValue) return
    this.samplesValue = next
    this.recreate()
  }

  resize(width: number, height: number): void {
    const w = Math.max(1, Math.floor(width))
    const h = Math.max(1, Math.floor(height))
    if (w === this.widthValue && h === this.heightValue) return
    this.widthValue = w
    this.heightValue = h
    this.recreate()
  }

  dispose(): void {
    this.destroyResources()
    this.color?.dispose()
  }

  private recreate(): void {
    this.destroyResources()
    this.color?.dispose()
    this.allocate()
  }

  private destroyResources(): void {
    const gl = this.device.gl
    if (this.framebuffer) gl.deleteFramebuffer(this.framebuffer)
    if (this.resolveFramebuffer) gl.deleteFramebuffer(this.resolveFramebuffer)
    if (this.colorRenderbuffer) gl.deleteRenderbuffer(this.colorRenderbuffer)
    if (this.depthRenderbuffer) gl.deleteRenderbuffer(this.depthRenderbuffer)
    this.framebuffer = null
    this.resolveFramebuffer = null
    this.colorRenderbuffer = null
    this.depthRenderbuffer = null
  }

  private allocate(): void {
    const requestedHdr = this.hdrValue
    const requestedSamples = this.samplesValue
    const plans: Array<[boolean, number]> = [
      [requestedHdr, requestedSamples],
      [false, requestedSamples],
      [false, 1],
    ]

    for (const [hdr, samples] of plans) {
      const color = new Texture(this.device, {
        label: `${this.label}.color`,
        width: this.widthValue,
        height: this.heightValue,
        format: hdr ? 'rgba16f' : 'rgba8',
        filter: 'linear',
        wrap: 'clamp',
      })
      if (this.build(color, samples, hdr)) {
        this.color = color
        this.samplesValue = samples
        this.hdrValue = hdr
        return
      }
      color.dispose()
      this.destroyResources()
    }

    throw new Error(`RenderTarget "${this.label}": не удалось создать полный фреймбуфер`)
  }

  private build(color: Texture, samples: number, hdr: boolean): boolean {
    const gl = this.device.gl
    const width = this.widthValue
    const height = this.heightValue
    const internalFormat = hdr ? gl.RGBA16F : gl.RGBA8

    const framebuffer = gl.createFramebuffer()
    if (!framebuffer) return false

    gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer)

    if (samples > 1) {
      const colorBuffer = gl.createRenderbuffer()
      const depthBuffer = gl.createRenderbuffer()
      if (!colorBuffer || !depthBuffer) return false

      gl.bindRenderbuffer(gl.RENDERBUFFER, colorBuffer)
      gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, internalFormat, width, height)
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.RENDERBUFFER, colorBuffer)

      gl.bindRenderbuffer(gl.RENDERBUFFER, depthBuffer)
      gl.renderbufferStorageMultisample(gl.RENDERBUFFER, samples, gl.DEPTH_COMPONENT24, width, height)
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthBuffer)

      this.colorRenderbuffer = colorBuffer
      this.depthRenderbuffer = depthBuffer
    } else {
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color.handle, 0)
      const depthBuffer = gl.createRenderbuffer()
      if (!depthBuffer) return false
      gl.bindRenderbuffer(gl.RENDERBUFFER, depthBuffer)
      gl.renderbufferStorage(gl.RENDERBUFFER, gl.DEPTH_COMPONENT24, width, height)
      gl.framebufferRenderbuffer(gl.FRAMEBUFFER, gl.DEPTH_ATTACHMENT, gl.RENDERBUFFER, depthBuffer)
      this.depthRenderbuffer = depthBuffer
    }

    const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER)
    gl.bindFramebuffer(gl.FRAMEBUFFER, null)
    if (status !== gl.FRAMEBUFFER_COMPLETE) return false

    if (samples > 1) {
      const resolve = gl.createFramebuffer()
      if (!resolve) return false
      gl.bindFramebuffer(gl.FRAMEBUFFER, resolve)
      gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, color.handle, 0)
      const resolveStatus = gl.checkFramebufferStatus(gl.FRAMEBUFFER)
      gl.bindFramebuffer(gl.FRAMEBUFFER, null)
      if (resolveStatus !== gl.FRAMEBUFFER_COMPLETE) {
        gl.deleteFramebuffer(resolve)
        return false
      }
      this.resolveFramebuffer = resolve
    }

    this.framebuffer = framebuffer
    return true
  }
}
