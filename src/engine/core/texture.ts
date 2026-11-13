import type { Device } from './device'

export type TextureFilter = 'linear' | 'nearest' | 'mipmap'
export type TextureWrap = 'repeat' | 'clamp' | 'mirror'
export type TextureFormat = 'rgba8' | 'rgba16f'

export interface TextureDesc {
  label?: string
  width: number
  height: number
  data?: Uint8Array | null
  format?: TextureFormat
  filter?: TextureFilter
  wrap?: TextureWrap
}

function wrapToGl(gl: WebGL2RenderingContext, wrap: TextureWrap): number {
  if (wrap === 'clamp') return gl.CLAMP_TO_EDGE
  if (wrap === 'mirror') return gl.MIRRORED_REPEAT
  return gl.REPEAT
}

export class Texture {
  readonly handle: WebGLTexture
  readonly width: number
  readonly height: number

  constructor(private readonly device: Device, desc: TextureDesc) {
    const gl = device.gl
    const handle = gl.createTexture()
    if (!handle) throw new Error('Не удалось создать текстуру')

    this.handle = handle
    this.width = desc.width
    this.height = desc.height

    const filter = desc.filter ?? 'mipmap'
    gl.bindTexture(gl.TEXTURE_2D, handle)
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, wrapToGl(gl, desc.wrap ?? 'repeat'))
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, wrapToGl(gl, desc.wrap ?? 'repeat'))

    if (filter === 'nearest') {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST)
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST)
    } else if (filter === 'linear') {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR)
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR)
    } else {
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR_MIPMAP_LINEAR)
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR)
    }

    const internalFormat = desc.format === 'rgba16f' ? gl.RGBA16F : gl.RGBA8
    const type = desc.format === 'rgba16f' ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE
    gl.texImage2D(
      gl.TEXTURE_2D,
      0,
      internalFormat,
      desc.width,
      desc.height,
      0,
      gl.RGBA,
      type,
      desc.data ?? null,
    )
    if (filter === 'mipmap') gl.generateMipmap(gl.TEXTURE_2D)
    gl.bindTexture(gl.TEXTURE_2D, null)
  }

  bind(unit: number): void {
    const gl = this.device.gl
    gl.activeTexture(gl.TEXTURE0 + unit)
    gl.bindTexture(gl.TEXTURE_2D, this.handle)
  }

  dispose(): void {
    this.device.gl.deleteTexture(this.handle)
  }
}
