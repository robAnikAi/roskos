/** Публичный вход в движок roSkos (TS-слой: GPU-абстракция + бэкенд). */

export { Device, type DeviceCapabilities, type DeviceOptions, type FrameStats } from './core/device'
export { Pipeline, type PipelineDesc, type VertexLayout, type CullMode, type BlendMode } from './core/pipeline'
export { Shader, type ShaderDesc, type UniformValue } from './core/shader'
export { Mesh, MESH_VERTEX_LAYOUT, type Geometry } from './core/mesh'
export { Texture, type TextureDesc } from './core/texture'
export { RenderTarget, type RenderTargetDesc } from './core/framebuffer'
export { RenderPass, type PassDescriptor } from './core/pass'
export { Renderer, type RendererOptions } from './renderer/renderer'
export { Core, MeshId, type CoreMaterial, type Vec3 } from './renderer/wasm-core'
export { box, plane, sphere, torusKnot } from './geometry/primitives'
