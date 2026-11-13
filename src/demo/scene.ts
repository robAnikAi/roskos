import { MeshId, type Core, type Vec3 } from '../engine/renderer/wasm-core'

export interface Demo {
  /** Анимирует сцену: трансформации узлов и движение источников света. */
  update(time: number): void
}

const PALETTE: Vec3[] = [
  [0.24, 0.84, 1.0],
  [0.66, 0.42, 1.0],
  [0.36, 1.0, 0.75],
]

/**
 * Демо-сцена целиком описывается через API ядра: ни одной матрицы и ни одного
 * GL-вызова на стороне демо. Всё остальное считает Rust.
 */
export function buildDemoScene(core: Core): Demo {
  core.setSky([0.02, 0.035, 0.09], [0.11, 0.14, 0.21], [0.01, 0.012, 0.02], 8)
  core.setLighting({
    ambientSky: [0.1, 0.13, 0.2],
    ambientGround: [0.015, 0.02, 0.03],
    fogColor: [0.05, 0.06, 0.09],
    fogDensity: 0.03,
    sunDirection: [0.4, 0.8, 0.3],
    sunColor: [0.55, 0.6, 0.8],
  })

  const floor = core.addNode(MeshId.Floor)
  core.setTransform(floor, [0, -1.4, 0], [0, 0, 0], [1, 1, 1])
  core.setMaterial(floor, {
    baseColor: [0.2, 0.22, 0.26],
    roughness: 0.85,
    metallic: 0.05,
    detail: true,
    detailMix: 0.45,
    uvScale: [40, 40],
  })

  const knot = core.addNode(MeshId.TorusKnot)
  core.setTransform(knot, [0, 0.15, 0], [0, 0, 0], [1.05, 1.05, 1.05])
  core.setMaterial(knot, {
    baseColor: [1.0, 0.55, 0.22],
    roughness: 0.22,
    metallic: 0.85,
    emissive: [0.02, 0.008, 0.0],
  })

  const orbs = PALETTE.map((color, index) => {
    const node = core.addNode(MeshId.Sphere)
    core.setMaterial(node, { baseColor: color, roughness: 0.28, metallic: 0.55 })
    return { node, phase: index * 2.0944 }
  })

  const cube = core.addNode(MeshId.Box)
  core.setTransform(cube, [-2.6, 1.5, -1.4], [0, 0, 0], [1, 1, 1])
  core.setMaterial(cube, { baseColor: [0.9, 0.92, 1.0], roughness: 0.18, metallic: 0.9 })

  const warm = core.addLight([3.6, 1.6, 0.55])
  const cyan = core.addLight([0.35, 1.8, 3.0])
  const violet = core.addLight([2.0, 0.8, 3.2])

  return {
    update(time: number): void {
      core.setTransform(
        knot,
        [0, 0.15 + Math.sin(time * 0.9) * 0.12, 0],
        [time * 0.35, time * 0.55, 0],
        [1.05, 1.05, 1.05],
      )

      for (const orb of orbs) {
        const angle = time * 0.45 + orb.phase
        core.setTransform(
          orb.node,
          [Math.cos(angle) * 3.2, 0.45 + Math.sin(time * 0.7 + orb.phase) * 0.55, Math.sin(angle) * 3.2],
          [time * 0.6 + orb.phase, angle, 0],
          [0.62, 0.62, 0.62],
        )
      }

      core.setTransform(
        cube,
        [-2.6, 1.5 + Math.sin(time * 0.8) * 0.18, -1.4],
        [time * 0.4, time * 0.7, 0.2],
        [1, 1, 1],
      )

      core.setLight(warm, [Math.cos(time * 0.5) * 3.6, 2.4, Math.sin(time * 0.5) * 3.6], [3.6, 1.6, 0.55])
      core.setLight(
        cyan,
        [Math.cos(time * 0.42 + 2.1) * 4.2, 0.8, Math.sin(time * 0.42 + 2.1) * 4.2],
        [0.35, 1.8, 3.0],
      )
      core.setLight(
        violet,
        [Math.cos(time * 0.6 + 4.2) * 2.8, 3.2, Math.sin(time * 0.6 + 4.2) * 2.8],
        [2.0, 0.8, 3.2],
      )
    },
  }
}
