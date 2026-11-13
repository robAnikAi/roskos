import type { Core } from '../engine/renderer/wasm-core'

export interface ControlCallbacks {
  onTogglePause(): void
  onTogglePost(): void
  onCycleSamples(): void
}

export interface OrbitControls {
  /** Время без ввода — используется для мягкого авто-вращения. */
  idleTime: number
}

/** Управление камерой живёт в ядре: здесь только события → вызовы ABI. */
export function attachOrbitControls(
  canvas: HTMLCanvasElement,
  core: Core,
  callbacks: ControlCallbacks,
): OrbitControls {
  let pointerId: number | null = null
  let lastX = 0
  let lastY = 0
  const state: OrbitControls = { idleTime: 0 }

  const onPointerDown = (event: PointerEvent) => {
    if (pointerId !== null) return
    pointerId = event.pointerId
    lastX = event.clientX
    lastY = event.clientY
    canvas.setPointerCapture(event.pointerId)
    state.idleTime = 0
  }

  const onPointerMove = (event: PointerEvent) => {
    if (event.pointerId !== pointerId) return
    const dx = event.clientX - lastX
    const dy = event.clientY - lastY
    lastX = event.clientX
    lastY = event.clientY
    core.orbit(-dx * 0.005, -dy * 0.004)
    state.idleTime = 0
  }

  const onPointerUp = (event: PointerEvent) => {
    if (event.pointerId !== pointerId) return
    pointerId = null
    if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId)
  }

  const onWheel = (event: WheelEvent) => {
    event.preventDefault()
    core.zoom(event.deltaY)
    state.idleTime = 0
  }

  const onKeyDown = (event: KeyboardEvent) => {
    if (event.repeat) return
    if (event.code === 'Space') {
      event.preventDefault()
      callbacks.onTogglePause()
    } else if (event.code === 'KeyP') {
      callbacks.onTogglePost()
    } else if (event.code === 'KeyM') {
      callbacks.onCycleSamples()
    }
  }

  canvas.addEventListener('pointerdown', onPointerDown)
  canvas.addEventListener('pointermove', onPointerMove)
  canvas.addEventListener('pointerup', onPointerUp)
  canvas.addEventListener('pointercancel', onPointerUp)
  canvas.addEventListener('wheel', onWheel, { passive: false })
  canvas.addEventListener('contextmenu', (event) => event.preventDefault())
  window.addEventListener('keydown', onKeyDown)

  return state
}
