import type { Device } from './device'
import type { VertexLayout } from './pipeline'

export interface Geometry {
  positions: Float32Array
  normals: Float32Array
  uvs: Float32Array
  indices: Uint32Array
}

/** position(3) + normal(3) + uv(2) — 32 байта на вершину. */
export const MESH_VERTEX_LAYOUT: VertexLayout = {
  stride: 8 * 4,
  attributes: [
    { location: 0, size: 3, offset: 0 },
    { location: 1, size: 3, offset: 12 },
    { location: 2, size: 2, offset: 24 },
  ],
}

export class Mesh {
  readonly indexCount: number
  readonly vertexCount: number
  private readonly vao: WebGLVertexArrayObject
  private readonly vertexBuffer: WebGLBuffer
  private readonly indexBuffer: WebGLBuffer

  constructor(private readonly device: Device, geometry: Geometry, readonly label = 'mesh') {
    const gl = device.gl
    this.vertexCount = geometry.positions.length / 3
    this.indexCount = geometry.indices.length

    const interleaved = new Float32Array(this.vertexCount * 8)
    for (let i = 0; i < this.vertexCount; i++) {
      const target = i * 8
      const source = i * 3
      interleaved[target + 0] = geometry.positions[source + 0]
      interleaved[target + 1] = geometry.positions[source + 1]
      interleaved[target + 2] = geometry.positions[source + 2]
      interleaved[target + 3] = geometry.normals[source + 0]
      interleaved[target + 4] = geometry.normals[source + 1]
      interleaved[target + 5] = geometry.normals[source + 2]
      interleaved[target + 6] = geometry.uvs[i * 2 + 0]
      interleaved[target + 7] = geometry.uvs[i * 2 + 1]
    }

    const vao = gl.createVertexArray()
    const vertexBuffer = gl.createBuffer()
    const indexBuffer = gl.createBuffer()
    if (!vao || !vertexBuffer || !indexBuffer) throw new Error('Не удалось создать буферы меша')

    this.vao = vao
    this.vertexBuffer = vertexBuffer
    this.indexBuffer = indexBuffer

    gl.bindVertexArray(vao)
    gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer)
    gl.bufferData(gl.ARRAY_BUFFER, interleaved, gl.STATIC_DRAW)
    for (const attribute of MESH_VERTEX_LAYOUT.attributes) {
      gl.enableVertexAttribArray(attribute.location)
      gl.vertexAttribPointer(
        attribute.location,
        attribute.size,
        gl.FLOAT,
        false,
        MESH_VERTEX_LAYOUT.stride,
        attribute.offset,
      )
    }
    gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, indexBuffer)
    gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, geometry.indices, gl.STATIC_DRAW)
    gl.bindVertexArray(null)
    gl.bindBuffer(gl.ARRAY_BUFFER, null)
  }

  bind(): void {
    this.device.gl.bindVertexArray(this.vao)
  }

  dispose(): void {
    const gl = this.device.gl
    gl.deleteVertexArray(this.vao)
    gl.deleteBuffer(this.vertexBuffer)
    gl.deleteBuffer(this.indexBuffer)
  }
}
