import { Pipeline, type PipelineDesc } from './pipeline'
import { Shader, type ShaderDesc } from './shader'
import { Mesh, type Geometry } from './mesh'
import { RenderTarget, type RenderTargetDesc } from './framebuffer'
import { Texture, type TextureDesc } from './texture'
import { RenderPass, type PassDescriptor } from './pass'

export interface DeviceCapabilities {
  readonly renderer: string
  readonly vendor: string
  readonly maxTextureSize: number
  readonly maxSamples: number
  readonly floatTargets: boolean
  readonly anisotropicFiltering: boolean
}

export interface FrameStats {
  drawCalls: number
  triangles: number
}

export interface DeviceOptions {
  antialias?: boolean
  alpha?: boolean
}

function readCapabilities(gl: WebGL2RenderingContext): DeviceCapabilities {
  const debugInfo = gl.getExtension('WEBGL_debug_renderer_info')
  const renderer = debugInfo
    ? String(gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL))
    : String(gl.getParameter(gl.RENDERER))
  const vendor = debugInfo
    ? String(gl.getParameter(debugInfo.UNMASKED_VENDOR_WEBGL))
    : String(gl.getParameter(gl.VENDOR))

  return {
    renderer,
    vendor,
    maxTextureSize: gl.getParameter(gl.MAX_TEXTURE_SIZE) as number,
    maxSamples: gl.getParameter(gl.MAX_SAMPLES) as number,
    floatTargets: Boolean(gl.getExtension('EXT_color_buffer_float')),
    anisotropicFiltering: Boolean(gl.getExtension('EXT_texture_filter_anisotropic')),
  }
}

/**
 * Устройство (device) — владелец GL-контекста и фабрика всех ресурсов:
 * шейдеров, пайплайнов, мешей, текстур и целей рендеринга.
 */
export class Device {
  readonly gl: WebGL2RenderingContext
  readonly canvas: HTMLCanvasElement
  readonly capabilities: DeviceCapabilities
  readonly stats: FrameStats = { drawCalls: 0, triangles: 0 }
  private readonly fullscreenVao: WebGLVertexArrayObject

  private constructor(canvas: HTMLCanvasElement, gl: WebGL2RenderingContext) {
    this.canvas = canvas
    this.gl = gl
    this.capabilities = readCapabilities(gl)

    const vao = gl.createVertexArray()
    if (!vao) throw new Error('Не удалось создать VAO для полноэкранных проходов')
    this.fullscreenVao = vao

    gl.enable(gl.DEPTH_TEST)
    gl.disable(gl.CULL_FACE)
  }

  static create(canvas: HTMLCanvasElement, options: DeviceOptions = {}): Device {
    const gl = canvas.getContext('webgl2', {
      antialias: options.antialias ?? false,
      alpha: options.alpha ?? false,
      depth: true,
      stencil: false,
      premultipliedAlpha: false,
      powerPreference: 'high-performance',
      preserveDrawingBuffer: false,
    })
    if (!gl) {
      throw new Error('Браузер не предоставил контекст WebGL2. Включите аппаратное ускорение.')
    }
    return new Device(canvas, gl)
  }

  get width(): number {
    return this.canvas.width
  }

  get height(): number {
    return this.canvas.height
  }

  /** Подгоняет размер буфера под CSS-размер с учётом devicePixelRatio. */
  resizeToDisplaySize(maxPixelRatio = 2): boolean {
    const ratio = Math.min(window.devicePixelRatio || 1, maxPixelRatio)
    const width = Math.max(1, Math.round(this.canvas.clientWidth * ratio))
    const height = Math.max(1, Math.round(this.canvas.clientHeight * ratio))
    if (this.canvas.width === width && this.canvas.height === height) return false
    this.canvas.width = width
    this.canvas.height = height
    return true
  }

  createShader(desc: ShaderDesc): Shader {
    return new Shader(this, desc)
  }

  createPipeline(desc: PipelineDesc): Pipeline {
    return new Pipeline(this, desc)
  }

  createMesh(geometry: Geometry, label?: string): Mesh {
    return new Mesh(this, geometry, label)
  }

  createTexture(desc: TextureDesc): Texture {
    return new Texture(this, desc)
  }

  createRenderTarget(desc: RenderTargetDesc): RenderTarget {
    return new RenderTarget(this, desc)
  }

  beginPass(desc: PassDescriptor = {}): RenderPass {
    const gl = this.gl
    gl.bindVertexArray(null)
    gl.colorMask(true, true, true, true)
    gl.depthMask(true)

    if (desc.target) {
      desc.target.bind()
    } else {
      gl.bindFramebuffer(gl.FRAMEBUFFER, null)
      gl.viewport(0, 0, this.width, this.height)
    }

    if (desc.clearColor !== null) {
      const [r, g, b, a] = desc.clearColor ?? [0, 0, 0, 1]
      gl.clearColor(r, g, b, a)
      gl.clear(gl.COLOR_BUFFER_BIT)
    }
    if (desc.clearDepth !== null) {
      gl.clearDepth(desc.clearDepth ?? 1)
      gl.clear(gl.DEPTH_BUFFER_BIT)
    }

    return new RenderPass(this, desc.target ?? null)
  }

  /** @internal используется RenderPass для полноэкранного треугольника */
  bindFullscreenGeometry(): void {
    this.gl.bindVertexArray(this.fullscreenVao)
  }

  dispose(): void {
    this.gl.deleteVertexArray(this.fullscreenVao)
  }
}
