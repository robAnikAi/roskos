import './style.css'
import { Device } from './engine/core/device'
import { box, plane, sphere, torusKnot } from './engine/geometry/primitives'
import { Renderer } from './engine/renderer/renderer'
import { Core, MeshId } from './engine/renderer/wasm-core'
import { Hud } from './demo/hud'
import { attachOrbitControls } from './demo/input'
import { buildDemoScene } from './demo/scene'
import { createCheckerTexture } from './demo/textures'

const canvas = document.querySelector<HTMLCanvasElement>('#viewport')
const fatal = document.querySelector<HTMLElement>('#fatal')
const fatalTitle = document.querySelector<HTMLElement>('#fatal-title')
const fatalText = document.querySelector<HTMLElement>('#fatal-text')

const SAMPLE_STEPS = [1, 2, 4]

function showFatal(title: string, text: string): void {
  if (fatalTitle) fatalTitle.textContent = title
  if (fatalText) fatalText.textContent = text
  if (fatal) fatal.hidden = false
}

function describe(error: unknown): string {
  return error instanceof Error ? error.message : String(error)
}

async function main(): Promise<void> {
  if (!canvas) throw new Error('roSkos: не найден canvas #viewport')

  let device: Device
  try {
    device = Device.create(canvas)
  } catch (error) {
    showFatal('WebGL2 недоступен', describe(error))
    return
  }

  let core: Core
  try {
    core = await Core.load()
  } catch (error) {
    showFatal('Не удалось загрузить ядро roSkos', describe(error))
    return
  }

  const hud = new Hud()
  device.resizeToDisplaySize()

  const renderer = new Renderer(device, core, { samples: 4 })
  renderer.resize(device.width, device.height)
  core.setViewport(device.width, device.height)

  renderer.registerMesh(MeshId.Sphere, device.createMesh(sphere(1, 56, 36), 'sphere'))
  renderer.registerMesh(
    MeshId.TorusKnot,
    device.createMesh(torusKnot(1.05, 0.34, 260, 28), 'torus-knot'),
  )
  renderer.registerMesh(MeshId.Box, device.createMesh(box(1.15, 1.15, 1.15), 'box'))
  renderer.registerMesh(MeshId.Floor, device.createMesh(plane(70), 'floor'))
  renderer.setDetailTexture(createCheckerTexture(device))

  const demo = buildDemoScene(core)
  hud.setDevice(device.capabilities.renderer, core.memoryBytes)

  let paused = false
  const controls = attachOrbitControls(canvas, core, {
    onTogglePause: () => {
      paused = !paused
    },
    onTogglePost: () => renderer.setPostProcessing(!renderer.isPostProcessingEnabled),
    onCycleSamples: () => {
      const allowed = SAMPLE_STEPS.filter((samples) => samples <= device.capabilities.maxSamples)
      const steps = allowed.length > 0 ? allowed : [1]
      const current = steps.indexOf(renderer.samples)
      renderer.setSamples(steps[(current + 1) % steps.length])
    },
  })

  let elapsed = 0
  let last = performance.now()
  let frames = 0
  let fpsTimer = 0
  let frameMs = 16.7

  function frame(now: number): void {
    const dt = Math.min(Math.max((now - last) / 1000, 0), 0.05)
    last = now
    if (!paused) elapsed += dt

    if (device.resizeToDisplaySize()) {
      renderer.resize(device.width, device.height)
      core.setViewport(device.width, device.height)
    }

    controls.idleTime += dt
    if (controls.idleTime > 2.5) core.orbit(dt * 0.045, 0)

    core.updateCamera(dt)
    demo.update(elapsed)
    renderer.render(elapsed)

    frames += 1
    fpsTimer += dt
    frameMs = frameMs * 0.9 + dt * 1000 * 0.1

    if (fpsTimer >= 0.3) {
      hud.update({
        fps: frames / fpsTimer,
        frameMs,
        width: device.width,
        height: device.height,
        samples: renderer.samples,
        drawCalls: device.stats.drawCalls,
        triangles: device.stats.triangles,
      })
      frames = 0
      fpsTimer = 0
    }

    requestAnimationFrame(frame)
  }

  canvas.addEventListener('webglcontextlost', (event) => {
    event.preventDefault()
    showFatal('Контекст WebGL2 потерян', 'Драйвер сбросил контекст GPU. Перезагрузите страницу.')
  })

  requestAnimationFrame(frame)
}

main().catch((error: unknown) => {
  console.error(error)
  showFatal('Ошибка запуска roSkos', describe(error))
})
