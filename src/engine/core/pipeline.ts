import type { Device } from './device'
import type { Shader } from './shader'

export interface VertexAttribute {
  location: number
  size: number
  offset: number
}

export interface VertexLayout {
  stride: number
  attributes: VertexAttribute[]
}

export type CullMode = 'none' | 'back' | 'front'
export type BlendMode = 'none' | 'alpha' | 'additive'
export type DepthCompare = 'less' | 'lequal' | 'always'

export interface PipelineDesc {
  label?: string
  shader: Shader
  layout: VertexLayout
  depthTest?: boolean
  depthWrite?: boolean
  depthCompare?: DepthCompare
  cull?: CullMode
  blend?: BlendMode
}

/**
 * Пайплайн — неизменяемый набор состояния отрисовки: шейдер, формат вершин,
 * тест/запись глубины, отсечение граней и смешивание. Как в Vulkan, смена
 * пайплайна — единственный способ поменять всё состояние разом.
 */
export class Pipeline {
  readonly shader: Shader
  readonly layout: VertexLayout
  private readonly depthTest: boolean
  private readonly depthWrite: boolean
  private readonly depthCompare: DepthCompare
  private readonly cull: CullMode
  private readonly blend: BlendMode

  constructor(private readonly device: Device, desc: PipelineDesc) {
    this.shader = desc.shader
    this.layout = desc.layout
    this.depthTest = desc.depthTest ?? true
    this.depthWrite = desc.depthWrite ?? true
    this.depthCompare = desc.depthCompare ?? 'less'
    this.cull = desc.cull ?? 'back'
    this.blend = desc.blend ?? 'none'
  }

  bind(): void {
    const gl = this.device.gl
    this.shader.use()

    if (this.depthTest) {
      gl.enable(gl.DEPTH_TEST)
      gl.depthFunc(
        this.depthCompare === 'always' ? gl.ALWAYS : this.depthCompare === 'lequal' ? gl.LEQUAL : gl.LESS,
      )
    } else {
      gl.disable(gl.DEPTH_TEST)
    }
    gl.depthMask(this.depthWrite)

    if (this.cull === 'none') {
      gl.disable(gl.CULL_FACE)
    } else {
      gl.enable(gl.CULL_FACE)
      gl.cullFace(this.cull === 'front' ? gl.FRONT : gl.BACK)
    }

    if (this.blend === 'alpha') {
      gl.enable(gl.BLEND)
      gl.blendFuncSeparate(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA, gl.ONE, gl.ONE_MINUS_SRC_ALPHA)
    } else if (this.blend === 'additive') {
      gl.enable(gl.BLEND)
      gl.blendFunc(gl.SRC_ALPHA, gl.ONE)
    } else {
      gl.disable(gl.BLEND)
    }
  }
}
