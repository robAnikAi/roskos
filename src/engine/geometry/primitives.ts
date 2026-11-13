import type { Geometry } from '../core/mesh'

/** Коллектор геометрии для сборки примитивов. */
class GeometryBuilder {
  private readonly positions: number[] = []
  private readonly normals: number[] = []
  private readonly uvs: number[] = []
  private readonly indices: number[] = []

  vertex(
    px: number,
    py: number,
    pz: number,
    nx: number,
    ny: number,
    nz: number,
    u: number,
    v: number,
  ): number {
    const index = this.positions.length / 3
    this.positions.push(px, py, pz)
    this.normals.push(nx, ny, nz)
    this.uvs.push(u, v)
    return index
  }

  triangle(a: number, b: number, c: number): void {
    this.indices.push(a, b, c)
  }

  quad(a: number, b: number, c: number, d: number): void {
    this.indices.push(a, b, c, a, c, d)
  }

  build(): Geometry {
    return {
      positions: new Float32Array(this.positions),
      normals: new Float32Array(this.normals),
      uvs: new Float32Array(this.uvs),
      indices: new Uint32Array(this.indices),
    }
  }
}

export function box(width = 1, height = 1, depth = 1): Geometry {
  const builder = new GeometryBuilder()
  const hw = width / 2
  const hh = height / 2
  const hd = depth / 2

  const faces: Array<{ normal: [number, number, number]; corners: Array<[number, number, number]> }> = [
    { normal: [0, 0, 1], corners: [[-hw, -hh, hd], [hw, -hh, hd], [hw, hh, hd], [-hw, hh, hd]] },
    { normal: [0, 0, -1], corners: [[hw, -hh, -hd], [-hw, -hh, -hd], [-hw, hh, -hd], [hw, hh, -hd]] },
    { normal: [1, 0, 0], corners: [[hw, -hh, hd], [hw, -hh, -hd], [hw, hh, -hd], [hw, hh, hd]] },
    { normal: [-1, 0, 0], corners: [[-hw, -hh, -hd], [-hw, -hh, hd], [-hw, hh, hd], [-hw, hh, -hd]] },
    { normal: [0, 1, 0], corners: [[-hw, hh, hd], [hw, hh, hd], [hw, hh, -hd], [-hw, hh, -hd]] },
    { normal: [0, -1, 0], corners: [[-hw, -hh, -hd], [hw, -hh, -hd], [hw, -hh, hd], [-hw, -hh, hd]] },
  ]

  const cornerUvs: Array<[number, number]> = [[0, 0], [1, 0], [1, 1], [0, 1]]

  for (const face of faces) {
    const start = builder.vertex(
      face.corners[0][0], face.corners[0][1], face.corners[0][2],
      face.normal[0], face.normal[1], face.normal[2],
      cornerUvs[0][0], cornerUvs[0][1],
    )
    for (let i = 1; i < 4; i++) {
      const corner = face.corners[i]
      builder.vertex(
        corner[0], corner[1], corner[2],
        face.normal[0], face.normal[1], face.normal[2],
        cornerUvs[i][0], cornerUvs[i][1],
      )
    }
    builder.quad(start, start + 1, start + 2, start + 3)
  }

  return builder.build()
}

export function sphere(radius = 1, widthSegments = 56, heightSegments = 36): Geometry {
  const builder = new GeometryBuilder()
  const grid: number[][] = []

  for (let y = 0; y <= heightSegments; y++) {
    const row: number[] = []
    const v = y / heightSegments
    const phi = v * Math.PI
    for (let x = 0; x <= widthSegments; x++) {
      const u = x / widthSegments
      const theta = u * Math.PI * 2
      const nx = Math.sin(phi) * Math.cos(theta)
      const ny = Math.cos(phi)
      const nz = Math.sin(phi) * Math.sin(theta)
      row.push(builder.vertex(nx * radius, ny * radius, nz * radius, nx, ny, nz, u, 1 - v))
    }
    grid.push(row)
  }

  for (let y = 0; y < heightSegments; y++) {
    for (let x = 0; x < widthSegments; x++) {
      const a = grid[y][x]
      const b = grid[y + 1][x]
      const aNext = grid[y][x + 1]
      const bNext = grid[y + 1][x + 1]
      builder.triangle(a, aNext, b)
      builder.triangle(b, aNext, bNext)
    }
  }

  return builder.build()
}

export function plane(size = 1, segments = 1): Geometry {
  const builder = new GeometryBuilder()
  const half = size / 2
  const step = size / segments
  const grid: number[][] = []

  for (let z = 0; z <= segments; z++) {
    const row: number[] = []
    for (let x = 0; x <= segments; x++) {
      row.push(
        builder.vertex(-half + x * step, 0, -half + z * step, 0, 1, 0, x / segments, z / segments),
      )
    }
    grid.push(row)
  }

  for (let z = 0; z < segments; z++) {
    for (let x = 0; x < segments; x++) {
      const a = grid[z][x]
      const b = grid[z + 1][x]
      const aNext = grid[z][x + 1]
      const bNext = grid[z + 1][x + 1]
      builder.triangle(a, b, aNext)
      builder.triangle(b, bNext, aNext)
    }
  }

  return builder.build()
}

function knotPoint(u: number, radius: number, p: number, q: number): [number, number, number] {
  const quOverP = (q / p) * u
  const cs = Math.cos(quOverP)
  return [
    radius * (2 + cs) * 0.5 * Math.cos(u),
    radius * (2 + cs) * 0.5 * Math.sin(u),
    radius * Math.sin(quOverP) * 0.5,
  ]
}

/** Тор-узел (p, q) — выразительный тестовый меш. */
export function torusKnot(
  radius = 1,
  tube = 0.3,
  tubularSegments = 240,
  radialSegments = 24,
  p = 2,
  q = 3,
): Geometry {
  const builder = new GeometryBuilder()
  const grid: number[][] = []
  const eps = 0.0001

  for (let i = 0; i <= tubularSegments; i++) {
    const u = (i / tubularSegments) * Math.PI * 2 * p
    const current = knotPoint(u, radius, p, q)
    const next = knotPoint(u + eps, radius, p, q)
    const previous = knotPoint(u - eps, radius, p, q)

    let tx = next[0] - previous[0]
    let ty = next[1] - previous[1]
    let tz = next[2] - previous[2]
    const tangentLength = Math.hypot(tx, ty, tz) || 1
    tx /= tangentLength
    ty /= tangentLength
    tz /= tangentLength

    // нормаль — направление кривизны, очищенное от касательной
    let ax = next[0] - 2 * current[0] + previous[0]
    let ay = next[1] - 2 * current[1] + previous[1]
    let az = next[2] - 2 * current[2] + previous[2]
    const projection = ax * tx + ay * ty + az * tz
    ax -= tx * projection
    ay -= ty * projection
    az -= tz * projection
    const normalLength = Math.hypot(ax, ay, az) || 1
    ax /= normalLength
    ay /= normalLength
    az /= normalLength

    const bx = ty * az - tz * ay
    const by = tz * ax - tx * az
    const bz = tx * ay - ty * ax

    const row: number[] = []
    for (let j = 0; j <= radialSegments; j++) {
      const v = (j / radialSegments) * Math.PI * 2
      const cv = Math.cos(v)
      const sv = Math.sin(v)
      const nx = ax * cv + bx * sv
      const ny = ay * cv + by * sv
      const nz = az * cv + bz * sv
      row.push(
        builder.vertex(
          current[0] + nx * tube,
          current[1] + ny * tube,
          current[2] + nz * tube,
          nx,
          ny,
          nz,
          i / tubularSegments,
          j / radialSegments,
        ),
      )
    }
    grid.push(row)
  }

  for (let i = 0; i < tubularSegments; i++) {
    for (let j = 0; j < radialSegments; j++) {
      const a = grid[i][j]
      const b = grid[i + 1][j]
      const aNext = grid[i][j + 1]
      const bNext = grid[i + 1][j + 1]
      builder.triangle(a, aNext, b)
      builder.triangle(b, aNext, bNext)
    }
  }

  return builder.build()
}
