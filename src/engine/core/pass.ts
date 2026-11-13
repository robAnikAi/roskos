import type { Device } from './device'
import type { Mesh } from './mesh'
import type { Pipeline } from './pipeline'
import type { RenderTarget } from './framebuffer'
import type { Texture } from './texture'
import type { UniformValue } from './shader'

export interface PassDescriptor {
  label?: string
  /** null / не задано — рисовать в экранный фреймбуфер. */
  target?: RenderTarget | null
  clearColor?: [number, number, number, number] | null
  clearDepth?: number | null
}

/**
 * Записанный проход рендеринга: набор вызовов между beginPass() и end().
 * Движок сам следит за привязкой цели, вьюпортом и счётчиками статистики.
 */
export class RenderPass {
  private pipeline: Pipeline | null = null
  private samplerUnit = 0
  private finished = false

  constructor(
    private readonly device: Device,
    private readonly target: RenderTarget | null,
  ) {}

  usePipeline(pipeline: Pipeline): this {
    this.assertOpen()
    if (pipeline !== this.pipeline) {
      pipeline.bind()
      this.pipeline = pipeline
      this.samplerUnit = 0
    }
    return this
  }

  setUniform(name: string, value: UniformValue): this {
    this.shader().setUniform(name, value)
    return this
  }

  setUniforms(values: Record<string, UniformValue>): this {
    const shader = this.shader()
    for (const name of Object.keys(values)) shader.setUniform(name, values[name])
    return this
  }

  setTexture(name: string, texture: Texture, unit?: number): this {
    const shader = this.shader()
    const slot = unit ?? this.samplerUnit++
    texture.bind(slot)
    shader.setUniform(name, slot)
    return this
  }

  draw(mesh: Mesh, instanceCount = 1): this {
    this.assertOpen()
    if (!this.pipeline) throw new Error('RenderPass.draw: сначала вызовите usePipeline()')
    const gl = this.device.gl
    mesh.bind()
    if (instanceCount > 1) {
      gl.drawElementsInstanced(gl.TRIANGLES, mesh.indexCount, gl.UNSIGNED_INT, 0, instanceCount)
    } else {
      gl.drawElements(gl.TRIANGLES, mesh.indexCount, gl.UNSIGNED_INT, 0)
    }
    this.device.stats.drawCalls += 1
    this.device.stats.triangles += (mesh.indexCount / 3) * instanceCount
    return this
  }

  /** Полноэкранный треугольник (постобработка, небо). */
  drawFullscreen(): this {
    this.assertOpen()
    if (!this.pipeline) throw new Error('RenderPass.drawFullscreen: сначала вызовите usePipeline()')
    const gl = this.device.gl
    this.device.bindFullscreenGeometry()
    gl.drawArrays(gl.TRIANGLES, 0, 3)
    this.device.stats.drawCalls += 1
    return this
  }

  end(): void {
    if (this.finished) return
    this.finished = true
    this.target?.resolve()
    this.device.gl.bindVertexArray(null)
  }

  private assertOpen(): void {
    if (this.finished) throw new Error('RenderPass: проход уже завершён')
  }

  private shader() {
    if (!this.pipeline) throw new Error('RenderPass: сначала вызовите usePipeline()')
    return this.pipeline.shader
  }
}
