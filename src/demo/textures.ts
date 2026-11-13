import type { Device } from '../engine/core/device'
import { Texture } from '../engine/core/texture'

/** Процедурная шахматка с сеткой — деталка пола. */
export function createCheckerTexture(device: Device, size = 256, cells = 8): Texture {
  const data = new Uint8Array(size * size * 4)
  const cell = size / cells

  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const isLight = ((Math.floor(x / cell) + Math.floor(y / cell)) & 1) === 0
      const onGridLine = x % cell < 2 || y % cell < 2
      let value = isLight ? 236 : 148
      if (onGridLine) value = Math.min(255, value + 18)
      const index = (y * size + x) * 4
      data[index + 0] = value
      data[index + 1] = value
      data[index + 2] = value
      data[index + 3] = 255
    }
  }

  return device.createTexture({
    label: 'checker',
    width: size,
    height: size,
    data,
    filter: 'mipmap',
    wrap: 'repeat',
  })
}
