import type { Device } from '../core/device'
import type { Mesh } from '../core/mesh'
import { MESH_VERTEX_LAYOUT } from '../core/mesh'
import { Pipeline } from '../core/pipeline'
import { RenderTarget } from '../core/framebuffer'
import { Texture } from '../core/texture'
import type { Core } from './wasm-core'
import {
  fullscreenVertexShader,
  postFragmentShader,
  sceneFragmentShader,
  sceneVertexShader,
  skyFragmentShader,
} from './shaders'

/**
 * Формат командного потока ядра. Источник истины —
 * `crates/roskos-core/src/frame.rs`; менять обе стороны нужно вместе.
 */
const CMD_FRAME = 1
const CMD_LIGHT = 2
const CMD_DRAW = 3
const FRAME_STRIDE = 65
const LIGHT_STRIDE = 7
const DRAW_STRIDE = 39
const MAX_LIGHTS = 8

// Смещения внутри FRAME-команды.
const FRAME_VIEW_PROJECTION = 1
const FRAME_INVERSE_VIEW_PROJECTION = 17
const FRAME_CAMERA_POSITION = 33
const FRAME_AMBIENT_SKY = 36
const FRAME_AMBIENT_GROUND = 39
const FRAME_FOG_COLOR = 42
const FRAME_FOG_DENSITY = 45
const FRAME_SUN_DIRECTION = 46
const FRAME_SUN_COLOR = 49
const FRAME_SKY_ZENITH = 52
const FRAME_SKY_HORIZON = 55
const FRAME_SKY_GROUND = 58
const FRAME_SUN_INTENSITY = 61
const FRAME_VIEWPORT_WIDTH = 62
const FRAME_VIEWPORT_HEIGHT = 63
const FRAME_LIGHT_COUNT = 64

// Смещения внутри DRAW-команды.
const DRAW_MESH_ID = 1
const DRAW_MODEL = 2
const DRAW_NORMAL = 18
const DRAW_BASE_COLOR = 27
const DRAW_ROUGHNESS = 30
const DRAW_METALLIC = 31
const DRAW_EMISSIVE = 32
const DRAW_DETAIL_MIX = 35
const DRAW_UV_SCALE = 36
const DRAW_HAS_DETAIL = 38

const WHITE_PIXEL = new Uint8Array([255, 255, 255, 255])

export interface RendererOptions {
  samples?: number
  postProcessing?: boolean
  exposure?: number
}

/**
 * Бэкенд roSkos для WebGL2.
 *
 * Никакой логики сцены здесь нет: ядро на Rust уже посчитало матрицы,
 * материалы и свет. Бэкенд только исполняет команды и владеет GPU-ресурсами.
 */
export class Renderer {
  private readonly target: RenderTarget
  private readonly scenePipeline: Pipeline
  private readonly skyPipeline: Pipeline
  private readonly postPipeline: Pipeline
  private readonly whiteTexture: Texture
  private detailTexture: Texture
  private readonly meshes = new Map<number, Mesh>()
  private readonly lightPositions = new Float32Array(MAX_LIGHTS * 3)
  private readonly lightColors = new Float32Array(MAX_LIGHTS * 3)

  private width: number
  private height: number
  private postProcessing: boolean
  private readonly exposure: number

  constructor(
    private readonly device: Device,
    private readonly core: Core,
    options: RendererOptions = {},
  ) {
    this.width = Math.max(1, device.width)
    this.height = Math.max(1, device.height)
    this.postProcessing = options.postProcessing ?? true
    this.exposure = options.exposure ?? 1.15

    this.target = device.createRenderTarget({
      label: 'scene-target',
      width: this.width,
      height: this.height,
      samples: options.samples ?? 4,
      hdr: device.capabilities.floatTargets,
    })

    this.whiteTexture = device.createTexture({
      label: 'white',
      width: 1,
      height: 1,
      data: WHITE_PIXEL,
      filter: 'nearest',
      wrap: 'clamp',
    })
    this.detailTexture = this.whiteTexture

    const fullscreenShader = (fragment: string, label: string) =>
      device.createShader({ label, vertex: fullscreenVertexShader, fragment })

    this.scenePipeline = device.createPipeline({
      label: 'scene',
      shader: device.createShader({
        label: 'scene',
        vertex: sceneVertexShader,
        fragment: sceneFragmentShader,
      }),
      layout: MESH_VERTEX_LAYOUT,
      depthTest: true,
      depthWrite: true,
      depthCompare: 'less',
      cull: 'back',
    })

    this.skyPipeline = device.createPipeline({
      label: 'sky',
      shader: fullscreenShader(skyFragmentShader, 'sky'),
      layout: { stride: 0, attributes: [] },
      depthTest: false,
      depthWrite: false,
      cull: 'none',
    })

    this.postPipeline = device.createPipeline({
      label: 'post',
      shader: fullscreenShader(postFragmentShader, 'post'),
      layout: { stride: 0, attributes: [] },
      depthTest: false,
      depthWrite: false,
      cull: 'none',
    })
  }

  get samples(): number {
    return this.target.samples
  }

  get hdr(): boolean {
    return this.target.isHdr
  }

  get isPostProcessingEnabled(): boolean {
    return this.postProcessing
  }

  registerMesh(id: number, mesh: Mesh): void {
    this.meshes.set(id, mesh)
  }

  setDetailTexture(texture: Texture): void {
    this.detailTexture = texture
  }

  setSamples(samples: number): void {
    this.target.setSamples(samples)
  }

  setPostProcessing(enabled: boolean): void {
    this.postProcessing = enabled
  }

  resize(width: number, height: number): void {
    this.width = Math.max(1, Math.floor(width))
    this.height = Math.max(1, Math.floor(height))
    this.target.resize(this.width, this.height)
  }

  render(time: number): void {
    const commands = this.core.buildFrame(time)
    if (commands.length < FRAME_STRIDE || commands[0] !== CMD_FRAME) {
      throw new Error('roSkos: ядро вернуло некорректный командный поток')
    }

    const device = this.device
    device.stats.drawCalls = 0
    device.stats.triangles = 0

    const scenePass = device.beginPass({
      label: 'scene',
      target: this.target,
      clearColor: [0, 0, 0, 1],
      clearDepth: 1,
    })

    // небо
    scenePass.usePipeline(this.skyPipeline)
    scenePass.setUniforms({
      uInvViewProjection: commands.subarray(
        FRAME_INVERSE_VIEW_PROJECTION,
        FRAME_INVERSE_VIEW_PROJECTION + 16,
      ),
      uCameraPosition: commands.subarray(FRAME_CAMERA_POSITION, FRAME_CAMERA_POSITION + 3),
      uResolution: [
        commands[FRAME_VIEWPORT_WIDTH],
        commands[FRAME_VIEWPORT_HEIGHT],
      ],
      uZenithColor: commands.subarray(FRAME_SKY_ZENITH, FRAME_SKY_ZENITH + 3),
      uHorizonColor: commands.subarray(FRAME_SKY_HORIZON, FRAME_SKY_HORIZON + 3),
      uGroundColor: commands.subarray(FRAME_SKY_GROUND, FRAME_SKY_GROUND + 3),
      uSunDirection: commands.subarray(FRAME_SUN_DIRECTION, FRAME_SUN_DIRECTION + 3),
      uSunIntensity: commands[FRAME_SUN_INTENSITY],
    })
    scenePass.drawFullscreen()

    // геометрия
    scenePass.usePipeline(this.scenePipeline)
    scenePass.setUniforms({
      uViewProjection: commands.subarray(FRAME_VIEW_PROJECTION, FRAME_VIEW_PROJECTION + 16),
      uCameraPosition: commands.subarray(FRAME_CAMERA_POSITION, FRAME_CAMERA_POSITION + 3),
      uAmbientSky: commands.subarray(FRAME_AMBIENT_SKY, FRAME_AMBIENT_SKY + 3),
      uAmbientGround: commands.subarray(FRAME_AMBIENT_GROUND, FRAME_AMBIENT_GROUND + 3),
      uFogColor: commands.subarray(FRAME_FOG_COLOR, FRAME_FOG_COLOR + 3),
      uFogDensity: commands[FRAME_FOG_DENSITY],
      uSunDirection: commands.subarray(FRAME_SUN_DIRECTION, FRAME_SUN_DIRECTION + 3),
      uSunColor: commands.subarray(FRAME_SUN_COLOR, FRAME_SUN_COLOR + 3),
      uLightCount: commands[FRAME_LIGHT_COUNT],
      uLightPosition: this.lightPositions,
      uLightColor: this.lightColors,
    })

    let offset = FRAME_STRIDE
    this.lightPositions.fill(0)
    this.lightColors.fill(0)

    let lightIndex = 0
    while (offset + LIGHT_STRIDE <= commands.length && commands[offset] === CMD_LIGHT) {
      if (lightIndex < MAX_LIGHTS) {
        const base = offset + 1
        this.lightPositions[lightIndex * 3 + 0] = commands[base + 0]
        this.lightPositions[lightIndex * 3 + 1] = commands[base + 1]
        this.lightPositions[lightIndex * 3 + 2] = commands[base + 2]
        this.lightColors[lightIndex * 3 + 0] = commands[base + 3]
        this.lightColors[lightIndex * 3 + 1] = commands[base + 4]
        this.lightColors[lightIndex * 3 + 2] = commands[base + 5]
      }
      lightIndex += 1
      offset += LIGHT_STRIDE
    }

    while (offset < commands.length) {
      if (commands[offset] !== CMD_DRAW) {
        throw new Error(`roSkos: неизвестная команда ${commands[offset]} на смещении ${offset}`)
      }
      const meshId = commands[offset + DRAW_MESH_ID]
      const mesh = this.meshes.get(meshId)
      if (!mesh) throw new Error(`roSkos: меш ${meshId} не зарегистрирован в бэкенде`)

      const base = offset
      scenePass.setUniforms({
        uModel: commands.subarray(base + DRAW_MODEL, base + DRAW_MODEL + 16),
        uNormalMatrix: commands.subarray(base + DRAW_NORMAL, base + DRAW_NORMAL + 9),
        uBaseColor: commands.subarray(base + DRAW_BASE_COLOR, base + DRAW_BASE_COLOR + 3),
        uRoughness: commands[base + DRAW_ROUGHNESS],
        uMetallic: commands[base + DRAW_METALLIC],
        uEmissive: commands.subarray(base + DRAW_EMISSIVE, base + DRAW_EMISSIVE + 3),
        uDetailMix: commands[base + DRAW_DETAIL_MIX],
        uUvScale: commands.subarray(base + DRAW_UV_SCALE, base + DRAW_UV_SCALE + 2),
      })
      scenePass.setTexture(
        'uDetail',
        commands[base + DRAW_HAS_DETAIL] > 0.5 ? this.detailTexture : this.whiteTexture,
        0,
      )
      scenePass.draw(mesh)
      offset += DRAW_STRIDE
    }

    scenePass.end()

    // постобработка: ACES-тонмаппинг, виньетка, зерно
    const postPass = device.beginPass({ label: 'post', target: null, clearColor: null })
    postPass.usePipeline(this.postPipeline)
    postPass.setTexture('uScene', this.target.color, 0)
    postPass.setUniforms({
      uExposure: this.exposure,
      uVignette: this.postProcessing ? 0.42 : 0,
      uGrain: this.postProcessing ? 0.012 : 0,
      uTime: time,
    })
    postPass.drawFullscreen()
    postPass.end()
  }
}
