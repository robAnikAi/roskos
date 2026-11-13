import type { Device } from './device'

export type UniformValue = number | boolean | ArrayLike<number>

export interface ShaderDesc {
  label?: string
  vertex: string
  fragment: string
}

interface UniformInfo {
  location: WebGLUniformLocation
  type: number
}

function compile(gl: WebGL2RenderingContext, type: number, source: string, label?: string): WebGLShader {
  const shader = gl.createShader(type)
  if (!shader) throw new Error('Не удалось создать шейдер')
  gl.shaderSource(shader, source)
  gl.compileShader(shader)
  if (!gl.getShaderParameter(shader, gl.COMPILE_STATUS)) {
    const log = gl.getShaderInfoLog(shader) ?? 'неизвестная ошибка'
    gl.deleteShader(shader)
    const stage = type === gl.VERTEX_SHADER ? 'вершинный' : 'фрагментный'
    throw new Error(`Ошибка компиляции (${label ?? 'shader'}): ${stage} шейдер\n${log}`)
  }
  return shader
}

/**
 * Обёртка над GL-программой. При создании движок сам читает активные
 * uniform-переменные, поэтому рендереру не нужно знать их расположение.
 */
export class Shader {
  readonly program: WebGLProgram
  private readonly uniforms = new Map<string, UniformInfo>()
  private readonly reported = new Set<string>()

  constructor(private readonly device: Device, desc: ShaderDesc) {
    const gl = device.gl
    const program = gl.createProgram()
    if (!program) throw new Error('Не удалось создать GL-программу')

    const vertex = compile(gl, gl.VERTEX_SHADER, desc.vertex, desc.label)
    const fragment = compile(gl, gl.FRAGMENT_SHADER, desc.fragment, desc.label)
    gl.attachShader(program, vertex)
    gl.attachShader(program, fragment)
    gl.linkProgram(program)
    gl.deleteShader(vertex)
    gl.deleteShader(fragment)

    if (!gl.getProgramParameter(program, gl.LINK_STATUS)) {
      const log = gl.getProgramInfoLog(program) ?? 'неизвестная ошибка'
      gl.deleteProgram(program)
      throw new Error(`Ошибка линковки (${desc.label ?? 'shader'}):\n${log}`)
    }

    this.program = program
    const count = gl.getProgramParameter(program, gl.ACTIVE_UNIFORMS) as number
    for (let i = 0; i < count; i++) {
      const info = gl.getActiveUniform(program, i)
      if (!info) continue
      const location = gl.getUniformLocation(program, info.name)
      if (!location) continue
      this.uniforms.set(info.name, { location, type: info.type })
      // массив uLightPosition[0] доступен и как uLightPosition
      const base = info.name.replace(/\[0\]$/, '')
      if (base !== info.name) this.uniforms.set(base, { location, type: info.type })
    }
  }

  use(): void {
    this.device.gl.useProgram(this.program)
  }

  setUniform(name: string, value: UniformValue): void {
    const gl = this.device.gl
    const uniform = this.uniforms.get(name)
    if (!uniform) {
      if (!this.reported.has(name)) {
        this.reported.add(name)
        console.warn(`[aether] uniform "${name}" отсутствует в шейдере`)
      }
      return
    }

    switch (uniform.type) {
      case gl.FLOAT:
        if (typeof value === 'number') gl.uniform1f(uniform.location, value)
        else gl.uniform1fv(uniform.location, value as Float32List)
        break
      case gl.FLOAT_VEC2:
        gl.uniform2fv(uniform.location, value as Float32List)
        break
      case gl.FLOAT_VEC3:
        gl.uniform3fv(uniform.location, value as Float32List)
        break
      case gl.FLOAT_VEC4:
        gl.uniform4fv(uniform.location, value as Float32List)
        break
      case gl.FLOAT_MAT3:
        gl.uniformMatrix3fv(uniform.location, false, value as Float32List)
        break
      case gl.FLOAT_MAT4:
        gl.uniformMatrix4fv(uniform.location, false, value as Float32List)
        break
      case gl.INT:
      case gl.BOOL:
      case gl.SAMPLER_2D:
      case gl.SAMPLER_CUBE:
        gl.uniform1i(uniform.location, typeof value === 'number' ? value : Number(value))
        break
      default:
        break
    }
  }

  dispose(): void {
    this.device.gl.deleteProgram(this.program)
  }
}
