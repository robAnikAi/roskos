/**
 * Типизированная обёртка над C-ABI ядра roSkos (wasm32).
 *
 * Ядро не трогает GPU: оно собирает сцену и командный поток кадра, а бэкенд
 * исполняет его через WebGL2. Обмен идёт через линейную память wasm.
 */

export type Vec3 = [number, number, number]

export const MeshId = {
  Sphere: 0,
  TorusKnot: 1,
  Box: 2,
  Floor: 3,
} as const

export type MeshId = (typeof MeshId)[keyof typeof MeshId]

export interface CoreMaterial {
  baseColor: Vec3
  roughness: number
  metallic: number
  emissive?: Vec3
  detail?: boolean
  detailMix?: number
  uvScale?: [number, number]
}

interface CoreExports extends WebAssembly.Exports {
  memory: WebAssembly.Memory
  roskos_add_node: (meshId: number) => number
  roskos_set_transform: (
    node: number,
    px: number,
    py: number,
    pz: number,
    rx: number,
    ry: number,
    rz: number,
    sx: number,
    sy: number,
    sz: number,
  ) => void
  roskos_set_material: (
    node: number,
    r: number,
    g: number,
    b: number,
    roughness: number,
    metallic: number,
    er: number,
    eg: number,
    eb: number,
    detail: number,
    detailMix: number,
    uvX: number,
    uvY: number,
  ) => void
  roskos_set_visible: (node: number, visible: number) => void
  roskos_add_light: (r: number, g: number, b: number) => number
  roskos_set_light: (index: number, x: number, y: number, z: number, r: number, g: number, b: number) => void
  roskos_set_sky: (
    zr: number, zg: number, zb: number,
    hr: number, hg: number, hb: number,
    gr: number, gg: number, gb: number,
    sunIntensity: number,
  ) => void
  roskos_set_lighting: (
    ambientSkyR: number, ambientSkyG: number, ambientSkyB: number,
    ambientGroundR: number, ambientGroundG: number, ambientGroundB: number,
    fogR: number, fogG: number, fogB: number, fogDensity: number,
    sunDirX: number, sunDirY: number, sunDirZ: number,
    sunR: number, sunG: number, sunB: number,
  ) => void
  roskos_camera_orbit: (deltaYaw: number, deltaPitch: number) => void
  roskos_camera_zoom: (delta: number) => void
  roskos_camera_update: (dt: number) => void
  roskos_set_viewport: (width: number, height: number) => void
  roskos_build_frame: (time: number) => number
  roskos_frame_ptr: () => number
  roskos_command_count: () => number
}

export class Core {
  private constructor(private readonly exports: CoreExports) {}

  static async load(url = '/roskos_core.wasm'): Promise<Core> {
    const response = await fetch(url)
    if (!response.ok) {
      throw new Error(`roSkos: не удалось загрузить ядро ${url} (HTTP ${response.status})`)
    }

    let instance: WebAssembly.Instance
    try {
      instance = (await WebAssembly.instantiateStreaming(response.clone(), {})).instance
    } catch {
      instance = (await WebAssembly.instantiate(await response.arrayBuffer(), {})).instance
    }

    return new Core(instance.exports as CoreExports)
  }

  get memoryBytes(): number {
    return this.exports.memory.buffer.byteLength
  }

  addNode(meshId: MeshId | number): number {
    return this.exports.roskos_add_node(meshId)
  }

  setTransform(node: number, position: Vec3, rotation: Vec3, scale: Vec3): void {
    this.exports.roskos_set_transform(
      node,
      position[0], position[1], position[2],
      rotation[0], rotation[1], rotation[2],
      scale[0], scale[1], scale[2],
    )
  }

  setMaterial(node: number, material: CoreMaterial): void {
    const emissive = material.emissive ?? [0, 0, 0]
    const uvScale = material.uvScale ?? [1, 1]
    this.exports.roskos_set_material(
      node,
      material.baseColor[0], material.baseColor[1], material.baseColor[2],
      material.roughness,
      material.metallic,
      emissive[0], emissive[1], emissive[2],
      material.detail ? 1 : 0,
      material.detailMix ?? 0,
      uvScale[0], uvScale[1],
    )
  }

  setVisible(node: number, visible: boolean): void {
    this.exports.roskos_set_visible(node, visible ? 1 : 0)
  }

  addLight(color: Vec3): number {
    return this.exports.roskos_add_light(color[0], color[1], color[2])
  }

  setLight(index: number, position: Vec3, color: Vec3): void {
    this.exports.roskos_set_light(
      index,
      position[0], position[1], position[2],
      color[0], color[1], color[2],
    )
  }

  setSky(zenith: Vec3, horizon: Vec3, ground: Vec3, sunIntensity: number): void {
    this.exports.roskos_set_sky(
      zenith[0], zenith[1], zenith[2],
      horizon[0], horizon[1], horizon[2],
      ground[0], ground[1], ground[2],
      sunIntensity,
    )
  }

  setLighting(options: {
    ambientSky: Vec3
    ambientGround: Vec3
    fogColor: Vec3
    fogDensity: number
    sunDirection: Vec3
    sunColor: Vec3
  }): void {
    this.exports.roskos_set_lighting(
      options.ambientSky[0], options.ambientSky[1], options.ambientSky[2],
      options.ambientGround[0], options.ambientGround[1], options.ambientGround[2],
      options.fogColor[0], options.fogColor[1], options.fogColor[2],
      options.fogDensity,
      options.sunDirection[0], options.sunDirection[1], options.sunDirection[2],
      options.sunColor[0], options.sunColor[1], options.sunColor[2],
    )
  }

  orbit(deltaYaw: number, deltaPitch: number): void {
    this.exports.roskos_camera_orbit(deltaYaw, deltaPitch)
  }

  zoom(delta: number): void {
    this.exports.roskos_camera_zoom(delta)
  }

  updateCamera(dt: number): void {
    this.exports.roskos_camera_update(dt)
  }

  setViewport(width: number, height: number): void {
    this.exports.roskos_set_viewport(width, height)
  }

  /**
   * Пересобирает кадр и возвращает представление командного потока.
   * Массив указывает прямо в память wasm и живёт до следующего вызова.
   */
  buildFrame(time: number): Float32Array {
    const count = this.exports.roskos_build_frame(time)
    const pointer = this.exports.roskos_frame_ptr()
    return new Float32Array(this.exports.memory.buffer, pointer, count)
  }
}
