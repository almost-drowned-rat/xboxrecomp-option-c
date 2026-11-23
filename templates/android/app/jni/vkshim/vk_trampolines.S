/* SPDX-License-Identifier: MIT
 * vk_trampolines.S - generated once (2026-11-11; same order as vk_names.h) from the NDK's API-29 libvulkan.so
 * exports (vk_exports.txt): one jump per exported Vulkan entry point through g_vk_slots[]. An empty slot means the
 * shim is not set up yet: the arguments are saved, vk_shim_init_once() (vulkan_port.c) fills the table, then the jump.
 * Lazy on purpose: a load-time constructor ran before libadrenotools' own (Tab S8: linkernsbypass not loaded). */
    .text
    .p2align 2
    .type vk_shim_lazy,%function
vk_shim_lazy:            /* x17 = the slot's address */
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    stp x8, x17, [sp, #80]
    bl vk_shim_init_once
    ldp x8, x17, [sp, #80]
    ldp x6, x7, [sp, #64]
    ldp x4, x5, [sp, #48]
    ldp x2, x3, [sp, #32]
    ldp x0, x1, [sp, #16]
    ldp x29, x30, [sp], #96
    ldr x16, [x17]
    br x16
    .size vk_shim_lazy, .-vk_shim_lazy
    .globl vkAcquireNextImage2KHR
    .type vkAcquireNextImage2KHR,%function
    .p2align 2
vkAcquireNextImage2KHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #0
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkAcquireNextImage2KHR, .-vkAcquireNextImage2KHR
    .globl vkAcquireNextImageKHR
    .type vkAcquireNextImageKHR,%function
    .p2align 2
vkAcquireNextImageKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #8
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkAcquireNextImageKHR, .-vkAcquireNextImageKHR
    .globl vkAllocateCommandBuffers
    .type vkAllocateCommandBuffers,%function
    .p2align 2
vkAllocateCommandBuffers:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #16
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkAllocateCommandBuffers, .-vkAllocateCommandBuffers
    .globl vkAllocateDescriptorSets
    .type vkAllocateDescriptorSets,%function
    .p2align 2
vkAllocateDescriptorSets:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #24
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkAllocateDescriptorSets, .-vkAllocateDescriptorSets
    .globl vkAllocateMemory
    .type vkAllocateMemory,%function
    .p2align 2
vkAllocateMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #32
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkAllocateMemory, .-vkAllocateMemory
    .globl vkBeginCommandBuffer
    .type vkBeginCommandBuffer,%function
    .p2align 2
vkBeginCommandBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #40
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkBeginCommandBuffer, .-vkBeginCommandBuffer
    .globl vkBindBufferMemory
    .type vkBindBufferMemory,%function
    .p2align 2
vkBindBufferMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #48
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkBindBufferMemory, .-vkBindBufferMemory
    .globl vkBindBufferMemory2
    .type vkBindBufferMemory2,%function
    .p2align 2
vkBindBufferMemory2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #56
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkBindBufferMemory2, .-vkBindBufferMemory2
    .globl vkBindImageMemory
    .type vkBindImageMemory,%function
    .p2align 2
vkBindImageMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #64
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkBindImageMemory, .-vkBindImageMemory
    .globl vkBindImageMemory2
    .type vkBindImageMemory2,%function
    .p2align 2
vkBindImageMemory2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #72
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkBindImageMemory2, .-vkBindImageMemory2
    .globl vkCmdBeginQuery
    .type vkCmdBeginQuery,%function
    .p2align 2
vkCmdBeginQuery:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #80
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBeginQuery, .-vkCmdBeginQuery
    .globl vkCmdBeginRenderPass
    .type vkCmdBeginRenderPass,%function
    .p2align 2
vkCmdBeginRenderPass:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #88
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBeginRenderPass, .-vkCmdBeginRenderPass
    .globl vkCmdBindDescriptorSets
    .type vkCmdBindDescriptorSets,%function
    .p2align 2
vkCmdBindDescriptorSets:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #96
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBindDescriptorSets, .-vkCmdBindDescriptorSets
    .globl vkCmdBindIndexBuffer
    .type vkCmdBindIndexBuffer,%function
    .p2align 2
vkCmdBindIndexBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #104
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBindIndexBuffer, .-vkCmdBindIndexBuffer
    .globl vkCmdBindPipeline
    .type vkCmdBindPipeline,%function
    .p2align 2
vkCmdBindPipeline:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #112
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBindPipeline, .-vkCmdBindPipeline
    .globl vkCmdBindVertexBuffers
    .type vkCmdBindVertexBuffers,%function
    .p2align 2
vkCmdBindVertexBuffers:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #120
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBindVertexBuffers, .-vkCmdBindVertexBuffers
    .globl vkCmdBlitImage
    .type vkCmdBlitImage,%function
    .p2align 2
vkCmdBlitImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #128
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdBlitImage, .-vkCmdBlitImage
    .globl vkCmdClearAttachments
    .type vkCmdClearAttachments,%function
    .p2align 2
vkCmdClearAttachments:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #136
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdClearAttachments, .-vkCmdClearAttachments
    .globl vkCmdClearColorImage
    .type vkCmdClearColorImage,%function
    .p2align 2
vkCmdClearColorImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #144
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdClearColorImage, .-vkCmdClearColorImage
    .globl vkCmdClearDepthStencilImage
    .type vkCmdClearDepthStencilImage,%function
    .p2align 2
vkCmdClearDepthStencilImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #152
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdClearDepthStencilImage, .-vkCmdClearDepthStencilImage
    .globl vkCmdCopyBuffer
    .type vkCmdCopyBuffer,%function
    .p2align 2
vkCmdCopyBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #160
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdCopyBuffer, .-vkCmdCopyBuffer
    .globl vkCmdCopyBufferToImage
    .type vkCmdCopyBufferToImage,%function
    .p2align 2
vkCmdCopyBufferToImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #168
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdCopyBufferToImage, .-vkCmdCopyBufferToImage
    .globl vkCmdCopyImage
    .type vkCmdCopyImage,%function
    .p2align 2
vkCmdCopyImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #176
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdCopyImage, .-vkCmdCopyImage
    .globl vkCmdCopyImageToBuffer
    .type vkCmdCopyImageToBuffer,%function
    .p2align 2
vkCmdCopyImageToBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #184
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdCopyImageToBuffer, .-vkCmdCopyImageToBuffer
    .globl vkCmdCopyQueryPoolResults
    .type vkCmdCopyQueryPoolResults,%function
    .p2align 2
vkCmdCopyQueryPoolResults:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #192
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdCopyQueryPoolResults, .-vkCmdCopyQueryPoolResults
    .globl vkCmdDispatch
    .type vkCmdDispatch,%function
    .p2align 2
vkCmdDispatch:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #200
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDispatch, .-vkCmdDispatch
    .globl vkCmdDispatchBase
    .type vkCmdDispatchBase,%function
    .p2align 2
vkCmdDispatchBase:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #208
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDispatchBase, .-vkCmdDispatchBase
    .globl vkCmdDispatchIndirect
    .type vkCmdDispatchIndirect,%function
    .p2align 2
vkCmdDispatchIndirect:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #216
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDispatchIndirect, .-vkCmdDispatchIndirect
    .globl vkCmdDraw
    .type vkCmdDraw,%function
    .p2align 2
vkCmdDraw:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #224
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDraw, .-vkCmdDraw
    .globl vkCmdDrawIndexed
    .type vkCmdDrawIndexed,%function
    .p2align 2
vkCmdDrawIndexed:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #232
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDrawIndexed, .-vkCmdDrawIndexed
    .globl vkCmdDrawIndexedIndirect
    .type vkCmdDrawIndexedIndirect,%function
    .p2align 2
vkCmdDrawIndexedIndirect:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #240
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDrawIndexedIndirect, .-vkCmdDrawIndexedIndirect
    .globl vkCmdDrawIndirect
    .type vkCmdDrawIndirect,%function
    .p2align 2
vkCmdDrawIndirect:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #248
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdDrawIndirect, .-vkCmdDrawIndirect
    .globl vkCmdEndQuery
    .type vkCmdEndQuery,%function
    .p2align 2
vkCmdEndQuery:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #256
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdEndQuery, .-vkCmdEndQuery
    .globl vkCmdEndRenderPass
    .type vkCmdEndRenderPass,%function
    .p2align 2
vkCmdEndRenderPass:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #264
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdEndRenderPass, .-vkCmdEndRenderPass
    .globl vkCmdExecuteCommands
    .type vkCmdExecuteCommands,%function
    .p2align 2
vkCmdExecuteCommands:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #272
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdExecuteCommands, .-vkCmdExecuteCommands
    .globl vkCmdFillBuffer
    .type vkCmdFillBuffer,%function
    .p2align 2
vkCmdFillBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #280
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdFillBuffer, .-vkCmdFillBuffer
    .globl vkCmdNextSubpass
    .type vkCmdNextSubpass,%function
    .p2align 2
vkCmdNextSubpass:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #288
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdNextSubpass, .-vkCmdNextSubpass
    .globl vkCmdPipelineBarrier
    .type vkCmdPipelineBarrier,%function
    .p2align 2
vkCmdPipelineBarrier:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #296
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdPipelineBarrier, .-vkCmdPipelineBarrier
    .globl vkCmdPushConstants
    .type vkCmdPushConstants,%function
    .p2align 2
vkCmdPushConstants:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #304
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdPushConstants, .-vkCmdPushConstants
    .globl vkCmdResetEvent
    .type vkCmdResetEvent,%function
    .p2align 2
vkCmdResetEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #312
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdResetEvent, .-vkCmdResetEvent
    .globl vkCmdResetQueryPool
    .type vkCmdResetQueryPool,%function
    .p2align 2
vkCmdResetQueryPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #320
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdResetQueryPool, .-vkCmdResetQueryPool
    .globl vkCmdResolveImage
    .type vkCmdResolveImage,%function
    .p2align 2
vkCmdResolveImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #328
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdResolveImage, .-vkCmdResolveImage
    .globl vkCmdSetBlendConstants
    .type vkCmdSetBlendConstants,%function
    .p2align 2
vkCmdSetBlendConstants:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #336
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetBlendConstants, .-vkCmdSetBlendConstants
    .globl vkCmdSetDepthBias
    .type vkCmdSetDepthBias,%function
    .p2align 2
vkCmdSetDepthBias:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #344
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetDepthBias, .-vkCmdSetDepthBias
    .globl vkCmdSetDepthBounds
    .type vkCmdSetDepthBounds,%function
    .p2align 2
vkCmdSetDepthBounds:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #352
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetDepthBounds, .-vkCmdSetDepthBounds
    .globl vkCmdSetDeviceMask
    .type vkCmdSetDeviceMask,%function
    .p2align 2
vkCmdSetDeviceMask:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #360
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetDeviceMask, .-vkCmdSetDeviceMask
    .globl vkCmdSetEvent
    .type vkCmdSetEvent,%function
    .p2align 2
vkCmdSetEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #368
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetEvent, .-vkCmdSetEvent
    .globl vkCmdSetLineWidth
    .type vkCmdSetLineWidth,%function
    .p2align 2
vkCmdSetLineWidth:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #376
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetLineWidth, .-vkCmdSetLineWidth
    .globl vkCmdSetScissor
    .type vkCmdSetScissor,%function
    .p2align 2
vkCmdSetScissor:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #384
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetScissor, .-vkCmdSetScissor
    .globl vkCmdSetStencilCompareMask
    .type vkCmdSetStencilCompareMask,%function
    .p2align 2
vkCmdSetStencilCompareMask:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #392
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetStencilCompareMask, .-vkCmdSetStencilCompareMask
    .globl vkCmdSetStencilReference
    .type vkCmdSetStencilReference,%function
    .p2align 2
vkCmdSetStencilReference:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #400
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetStencilReference, .-vkCmdSetStencilReference
    .globl vkCmdSetStencilWriteMask
    .type vkCmdSetStencilWriteMask,%function
    .p2align 2
vkCmdSetStencilWriteMask:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #408
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetStencilWriteMask, .-vkCmdSetStencilWriteMask
    .globl vkCmdSetViewport
    .type vkCmdSetViewport,%function
    .p2align 2
vkCmdSetViewport:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #416
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdSetViewport, .-vkCmdSetViewport
    .globl vkCmdUpdateBuffer
    .type vkCmdUpdateBuffer,%function
    .p2align 2
vkCmdUpdateBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #424
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdUpdateBuffer, .-vkCmdUpdateBuffer
    .globl vkCmdWaitEvents
    .type vkCmdWaitEvents,%function
    .p2align 2
vkCmdWaitEvents:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #432
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdWaitEvents, .-vkCmdWaitEvents
    .globl vkCmdWriteTimestamp
    .type vkCmdWriteTimestamp,%function
    .p2align 2
vkCmdWriteTimestamp:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #440
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCmdWriteTimestamp, .-vkCmdWriteTimestamp
    .globl vkCreateAndroidSurfaceKHR
    .type vkCreateAndroidSurfaceKHR,%function
    .p2align 2
vkCreateAndroidSurfaceKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #448
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateAndroidSurfaceKHR, .-vkCreateAndroidSurfaceKHR
    .globl vkCreateBuffer
    .type vkCreateBuffer,%function
    .p2align 2
vkCreateBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #456
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateBuffer, .-vkCreateBuffer
    .globl vkCreateBufferView
    .type vkCreateBufferView,%function
    .p2align 2
vkCreateBufferView:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #464
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateBufferView, .-vkCreateBufferView
    .globl vkCreateCommandPool
    .type vkCreateCommandPool,%function
    .p2align 2
vkCreateCommandPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #472
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateCommandPool, .-vkCreateCommandPool
    .globl vkCreateComputePipelines
    .type vkCreateComputePipelines,%function
    .p2align 2
vkCreateComputePipelines:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #480
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateComputePipelines, .-vkCreateComputePipelines
    .globl vkCreateDescriptorPool
    .type vkCreateDescriptorPool,%function
    .p2align 2
vkCreateDescriptorPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #488
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateDescriptorPool, .-vkCreateDescriptorPool
    .globl vkCreateDescriptorSetLayout
    .type vkCreateDescriptorSetLayout,%function
    .p2align 2
vkCreateDescriptorSetLayout:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #496
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateDescriptorSetLayout, .-vkCreateDescriptorSetLayout
    .globl vkCreateDescriptorUpdateTemplate
    .type vkCreateDescriptorUpdateTemplate,%function
    .p2align 2
vkCreateDescriptorUpdateTemplate:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #504
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateDescriptorUpdateTemplate, .-vkCreateDescriptorUpdateTemplate
    .globl vkCreateDevice
    .type vkCreateDevice,%function
    .p2align 2
vkCreateDevice:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #512
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateDevice, .-vkCreateDevice
    .globl vkCreateEvent
    .type vkCreateEvent,%function
    .p2align 2
vkCreateEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #520
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateEvent, .-vkCreateEvent
    .globl vkCreateFence
    .type vkCreateFence,%function
    .p2align 2
vkCreateFence:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #528
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateFence, .-vkCreateFence
    .globl vkCreateFramebuffer
    .type vkCreateFramebuffer,%function
    .p2align 2
vkCreateFramebuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #536
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateFramebuffer, .-vkCreateFramebuffer
    .globl vkCreateGraphicsPipelines
    .type vkCreateGraphicsPipelines,%function
    .p2align 2
vkCreateGraphicsPipelines:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #544
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateGraphicsPipelines, .-vkCreateGraphicsPipelines
    .globl vkCreateImage
    .type vkCreateImage,%function
    .p2align 2
vkCreateImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #552
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateImage, .-vkCreateImage
    .globl vkCreateImageView
    .type vkCreateImageView,%function
    .p2align 2
vkCreateImageView:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #560
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateImageView, .-vkCreateImageView
    .globl vkCreateInstance
    .type vkCreateInstance,%function
    .p2align 2
vkCreateInstance:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #568
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateInstance, .-vkCreateInstance
    .globl vkCreatePipelineCache
    .type vkCreatePipelineCache,%function
    .p2align 2
vkCreatePipelineCache:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #576
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreatePipelineCache, .-vkCreatePipelineCache
    .globl vkCreatePipelineLayout
    .type vkCreatePipelineLayout,%function
    .p2align 2
vkCreatePipelineLayout:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #584
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreatePipelineLayout, .-vkCreatePipelineLayout
    .globl vkCreateQueryPool
    .type vkCreateQueryPool,%function
    .p2align 2
vkCreateQueryPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #592
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateQueryPool, .-vkCreateQueryPool
    .globl vkCreateRenderPass
    .type vkCreateRenderPass,%function
    .p2align 2
vkCreateRenderPass:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #600
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateRenderPass, .-vkCreateRenderPass
    .globl vkCreateSampler
    .type vkCreateSampler,%function
    .p2align 2
vkCreateSampler:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #608
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateSampler, .-vkCreateSampler
    .globl vkCreateSamplerYcbcrConversion
    .type vkCreateSamplerYcbcrConversion,%function
    .p2align 2
vkCreateSamplerYcbcrConversion:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #616
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateSamplerYcbcrConversion, .-vkCreateSamplerYcbcrConversion
    .globl vkCreateSemaphore
    .type vkCreateSemaphore,%function
    .p2align 2
vkCreateSemaphore:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #624
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateSemaphore, .-vkCreateSemaphore
    .globl vkCreateShaderModule
    .type vkCreateShaderModule,%function
    .p2align 2
vkCreateShaderModule:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #632
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateShaderModule, .-vkCreateShaderModule
    .globl vkCreateSwapchainKHR
    .type vkCreateSwapchainKHR,%function
    .p2align 2
vkCreateSwapchainKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #640
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkCreateSwapchainKHR, .-vkCreateSwapchainKHR
    .globl vkDestroyBuffer
    .type vkDestroyBuffer,%function
    .p2align 2
vkDestroyBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #648
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyBuffer, .-vkDestroyBuffer
    .globl vkDestroyBufferView
    .type vkDestroyBufferView,%function
    .p2align 2
vkDestroyBufferView:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #656
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyBufferView, .-vkDestroyBufferView
    .globl vkDestroyCommandPool
    .type vkDestroyCommandPool,%function
    .p2align 2
vkDestroyCommandPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #664
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyCommandPool, .-vkDestroyCommandPool
    .globl vkDestroyDescriptorPool
    .type vkDestroyDescriptorPool,%function
    .p2align 2
vkDestroyDescriptorPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #672
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyDescriptorPool, .-vkDestroyDescriptorPool
    .globl vkDestroyDescriptorSetLayout
    .type vkDestroyDescriptorSetLayout,%function
    .p2align 2
vkDestroyDescriptorSetLayout:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #680
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyDescriptorSetLayout, .-vkDestroyDescriptorSetLayout
    .globl vkDestroyDescriptorUpdateTemplate
    .type vkDestroyDescriptorUpdateTemplate,%function
    .p2align 2
vkDestroyDescriptorUpdateTemplate:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #688
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyDescriptorUpdateTemplate, .-vkDestroyDescriptorUpdateTemplate
    .globl vkDestroyDevice
    .type vkDestroyDevice,%function
    .p2align 2
vkDestroyDevice:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #696
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyDevice, .-vkDestroyDevice
    .globl vkDestroyEvent
    .type vkDestroyEvent,%function
    .p2align 2
vkDestroyEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #704
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyEvent, .-vkDestroyEvent
    .globl vkDestroyFence
    .type vkDestroyFence,%function
    .p2align 2
vkDestroyFence:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #712
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyFence, .-vkDestroyFence
    .globl vkDestroyFramebuffer
    .type vkDestroyFramebuffer,%function
    .p2align 2
vkDestroyFramebuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #720
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyFramebuffer, .-vkDestroyFramebuffer
    .globl vkDestroyImage
    .type vkDestroyImage,%function
    .p2align 2
vkDestroyImage:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #728
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyImage, .-vkDestroyImage
    .globl vkDestroyImageView
    .type vkDestroyImageView,%function
    .p2align 2
vkDestroyImageView:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #736
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyImageView, .-vkDestroyImageView
    .globl vkDestroyInstance
    .type vkDestroyInstance,%function
    .p2align 2
vkDestroyInstance:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #744
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyInstance, .-vkDestroyInstance
    .globl vkDestroyPipeline
    .type vkDestroyPipeline,%function
    .p2align 2
vkDestroyPipeline:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #752
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyPipeline, .-vkDestroyPipeline
    .globl vkDestroyPipelineCache
    .type vkDestroyPipelineCache,%function
    .p2align 2
vkDestroyPipelineCache:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #760
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyPipelineCache, .-vkDestroyPipelineCache
    .globl vkDestroyPipelineLayout
    .type vkDestroyPipelineLayout,%function
    .p2align 2
vkDestroyPipelineLayout:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #768
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyPipelineLayout, .-vkDestroyPipelineLayout
    .globl vkDestroyQueryPool
    .type vkDestroyQueryPool,%function
    .p2align 2
vkDestroyQueryPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #776
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyQueryPool, .-vkDestroyQueryPool
    .globl vkDestroyRenderPass
    .type vkDestroyRenderPass,%function
    .p2align 2
vkDestroyRenderPass:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #784
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyRenderPass, .-vkDestroyRenderPass
    .globl vkDestroySampler
    .type vkDestroySampler,%function
    .p2align 2
vkDestroySampler:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #792
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroySampler, .-vkDestroySampler
    .globl vkDestroySamplerYcbcrConversion
    .type vkDestroySamplerYcbcrConversion,%function
    .p2align 2
vkDestroySamplerYcbcrConversion:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #800
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroySamplerYcbcrConversion, .-vkDestroySamplerYcbcrConversion
    .globl vkDestroySemaphore
    .type vkDestroySemaphore,%function
    .p2align 2
vkDestroySemaphore:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #808
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroySemaphore, .-vkDestroySemaphore
    .globl vkDestroyShaderModule
    .type vkDestroyShaderModule,%function
    .p2align 2
vkDestroyShaderModule:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #816
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroyShaderModule, .-vkDestroyShaderModule
    .globl vkDestroySurfaceKHR
    .type vkDestroySurfaceKHR,%function
    .p2align 2
vkDestroySurfaceKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #824
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroySurfaceKHR, .-vkDestroySurfaceKHR
    .globl vkDestroySwapchainKHR
    .type vkDestroySwapchainKHR,%function
    .p2align 2
vkDestroySwapchainKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #832
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDestroySwapchainKHR, .-vkDestroySwapchainKHR
    .globl vkDeviceWaitIdle
    .type vkDeviceWaitIdle,%function
    .p2align 2
vkDeviceWaitIdle:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #840
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkDeviceWaitIdle, .-vkDeviceWaitIdle
    .globl vkEndCommandBuffer
    .type vkEndCommandBuffer,%function
    .p2align 2
vkEndCommandBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #848
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEndCommandBuffer, .-vkEndCommandBuffer
    .globl vkEnumerateDeviceExtensionProperties
    .type vkEnumerateDeviceExtensionProperties,%function
    .p2align 2
vkEnumerateDeviceExtensionProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #856
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumerateDeviceExtensionProperties, .-vkEnumerateDeviceExtensionProperties
    .globl vkEnumerateDeviceLayerProperties
    .type vkEnumerateDeviceLayerProperties,%function
    .p2align 2
vkEnumerateDeviceLayerProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #864
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumerateDeviceLayerProperties, .-vkEnumerateDeviceLayerProperties
    .globl vkEnumerateInstanceExtensionProperties
    .type vkEnumerateInstanceExtensionProperties,%function
    .p2align 2
vkEnumerateInstanceExtensionProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #872
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumerateInstanceExtensionProperties, .-vkEnumerateInstanceExtensionProperties
    .globl vkEnumerateInstanceLayerProperties
    .type vkEnumerateInstanceLayerProperties,%function
    .p2align 2
vkEnumerateInstanceLayerProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #880
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumerateInstanceLayerProperties, .-vkEnumerateInstanceLayerProperties
    .globl vkEnumerateInstanceVersion
    .type vkEnumerateInstanceVersion,%function
    .p2align 2
vkEnumerateInstanceVersion:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #888
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumerateInstanceVersion, .-vkEnumerateInstanceVersion
    .globl vkEnumeratePhysicalDeviceGroups
    .type vkEnumeratePhysicalDeviceGroups,%function
    .p2align 2
vkEnumeratePhysicalDeviceGroups:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #896
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumeratePhysicalDeviceGroups, .-vkEnumeratePhysicalDeviceGroups
    .globl vkEnumeratePhysicalDevices
    .type vkEnumeratePhysicalDevices,%function
    .p2align 2
vkEnumeratePhysicalDevices:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #904
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkEnumeratePhysicalDevices, .-vkEnumeratePhysicalDevices
    .globl vkFlushMappedMemoryRanges
    .type vkFlushMappedMemoryRanges,%function
    .p2align 2
vkFlushMappedMemoryRanges:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #912
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkFlushMappedMemoryRanges, .-vkFlushMappedMemoryRanges
    .globl vkFreeCommandBuffers
    .type vkFreeCommandBuffers,%function
    .p2align 2
vkFreeCommandBuffers:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #920
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkFreeCommandBuffers, .-vkFreeCommandBuffers
    .globl vkFreeDescriptorSets
    .type vkFreeDescriptorSets,%function
    .p2align 2
vkFreeDescriptorSets:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #928
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkFreeDescriptorSets, .-vkFreeDescriptorSets
    .globl vkFreeMemory
    .type vkFreeMemory,%function
    .p2align 2
vkFreeMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #936
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkFreeMemory, .-vkFreeMemory
    .globl vkGetAndroidHardwareBufferPropertiesANDROID
    .type vkGetAndroidHardwareBufferPropertiesANDROID,%function
    .p2align 2
vkGetAndroidHardwareBufferPropertiesANDROID:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #944
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetAndroidHardwareBufferPropertiesANDROID, .-vkGetAndroidHardwareBufferPropertiesANDROID
    .globl vkGetBufferMemoryRequirements
    .type vkGetBufferMemoryRequirements,%function
    .p2align 2
vkGetBufferMemoryRequirements:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #952
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetBufferMemoryRequirements, .-vkGetBufferMemoryRequirements
    .globl vkGetBufferMemoryRequirements2
    .type vkGetBufferMemoryRequirements2,%function
    .p2align 2
vkGetBufferMemoryRequirements2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #960
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetBufferMemoryRequirements2, .-vkGetBufferMemoryRequirements2
    .globl vkGetDescriptorSetLayoutSupport
    .type vkGetDescriptorSetLayoutSupport,%function
    .p2align 2
vkGetDescriptorSetLayoutSupport:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #968
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDescriptorSetLayoutSupport, .-vkGetDescriptorSetLayoutSupport
    .globl vkGetDeviceGroupPeerMemoryFeatures
    .type vkGetDeviceGroupPeerMemoryFeatures,%function
    .p2align 2
vkGetDeviceGroupPeerMemoryFeatures:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #976
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceGroupPeerMemoryFeatures, .-vkGetDeviceGroupPeerMemoryFeatures
    .globl vkGetDeviceGroupPresentCapabilitiesKHR
    .type vkGetDeviceGroupPresentCapabilitiesKHR,%function
    .p2align 2
vkGetDeviceGroupPresentCapabilitiesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #984
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceGroupPresentCapabilitiesKHR, .-vkGetDeviceGroupPresentCapabilitiesKHR
    .globl vkGetDeviceGroupSurfacePresentModesKHR
    .type vkGetDeviceGroupSurfacePresentModesKHR,%function
    .p2align 2
vkGetDeviceGroupSurfacePresentModesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #992
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceGroupSurfacePresentModesKHR, .-vkGetDeviceGroupSurfacePresentModesKHR
    .globl vkGetDeviceMemoryCommitment
    .type vkGetDeviceMemoryCommitment,%function
    .p2align 2
vkGetDeviceMemoryCommitment:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1000
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceMemoryCommitment, .-vkGetDeviceMemoryCommitment
    .globl vkGetDeviceProcAddr
    .type vkGetDeviceProcAddr,%function
    .p2align 2
vkGetDeviceProcAddr:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1008
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceProcAddr, .-vkGetDeviceProcAddr
    .globl vkGetDeviceQueue
    .type vkGetDeviceQueue,%function
    .p2align 2
vkGetDeviceQueue:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1016
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceQueue, .-vkGetDeviceQueue
    .globl vkGetDeviceQueue2
    .type vkGetDeviceQueue2,%function
    .p2align 2
vkGetDeviceQueue2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1024
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetDeviceQueue2, .-vkGetDeviceQueue2
    .globl vkGetEventStatus
    .type vkGetEventStatus,%function
    .p2align 2
vkGetEventStatus:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1032
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetEventStatus, .-vkGetEventStatus
    .globl vkGetFenceStatus
    .type vkGetFenceStatus,%function
    .p2align 2
vkGetFenceStatus:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1040
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetFenceStatus, .-vkGetFenceStatus
    .globl vkGetImageMemoryRequirements
    .type vkGetImageMemoryRequirements,%function
    .p2align 2
vkGetImageMemoryRequirements:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1048
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetImageMemoryRequirements, .-vkGetImageMemoryRequirements
    .globl vkGetImageMemoryRequirements2
    .type vkGetImageMemoryRequirements2,%function
    .p2align 2
vkGetImageMemoryRequirements2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1056
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetImageMemoryRequirements2, .-vkGetImageMemoryRequirements2
    .globl vkGetImageSparseMemoryRequirements
    .type vkGetImageSparseMemoryRequirements,%function
    .p2align 2
vkGetImageSparseMemoryRequirements:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1064
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetImageSparseMemoryRequirements, .-vkGetImageSparseMemoryRequirements
    .globl vkGetImageSparseMemoryRequirements2
    .type vkGetImageSparseMemoryRequirements2,%function
    .p2align 2
vkGetImageSparseMemoryRequirements2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1072
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetImageSparseMemoryRequirements2, .-vkGetImageSparseMemoryRequirements2
    .globl vkGetImageSubresourceLayout
    .type vkGetImageSubresourceLayout,%function
    .p2align 2
vkGetImageSubresourceLayout:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1080
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetImageSubresourceLayout, .-vkGetImageSubresourceLayout
    .globl vkGetInstanceProcAddr
    .type vkGetInstanceProcAddr,%function
    .p2align 2
vkGetInstanceProcAddr:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1088
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetInstanceProcAddr, .-vkGetInstanceProcAddr
    .globl vkGetMemoryAndroidHardwareBufferANDROID
    .type vkGetMemoryAndroidHardwareBufferANDROID,%function
    .p2align 2
vkGetMemoryAndroidHardwareBufferANDROID:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1096
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetMemoryAndroidHardwareBufferANDROID, .-vkGetMemoryAndroidHardwareBufferANDROID
    .globl vkGetPhysicalDeviceExternalBufferProperties
    .type vkGetPhysicalDeviceExternalBufferProperties,%function
    .p2align 2
vkGetPhysicalDeviceExternalBufferProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1104
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceExternalBufferProperties, .-vkGetPhysicalDeviceExternalBufferProperties
    .globl vkGetPhysicalDeviceExternalFenceProperties
    .type vkGetPhysicalDeviceExternalFenceProperties,%function
    .p2align 2
vkGetPhysicalDeviceExternalFenceProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1112
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceExternalFenceProperties, .-vkGetPhysicalDeviceExternalFenceProperties
    .globl vkGetPhysicalDeviceExternalSemaphoreProperties
    .type vkGetPhysicalDeviceExternalSemaphoreProperties,%function
    .p2align 2
vkGetPhysicalDeviceExternalSemaphoreProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1120
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceExternalSemaphoreProperties, .-vkGetPhysicalDeviceExternalSemaphoreProperties
    .globl vkGetPhysicalDeviceFeatures
    .type vkGetPhysicalDeviceFeatures,%function
    .p2align 2
vkGetPhysicalDeviceFeatures:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1128
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceFeatures, .-vkGetPhysicalDeviceFeatures
    .globl vkGetPhysicalDeviceFeatures2
    .type vkGetPhysicalDeviceFeatures2,%function
    .p2align 2
vkGetPhysicalDeviceFeatures2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1136
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceFeatures2, .-vkGetPhysicalDeviceFeatures2
    .globl vkGetPhysicalDeviceFormatProperties
    .type vkGetPhysicalDeviceFormatProperties,%function
    .p2align 2
vkGetPhysicalDeviceFormatProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1144
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceFormatProperties, .-vkGetPhysicalDeviceFormatProperties
    .globl vkGetPhysicalDeviceFormatProperties2
    .type vkGetPhysicalDeviceFormatProperties2,%function
    .p2align 2
vkGetPhysicalDeviceFormatProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1152
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceFormatProperties2, .-vkGetPhysicalDeviceFormatProperties2
    .globl vkGetPhysicalDeviceImageFormatProperties
    .type vkGetPhysicalDeviceImageFormatProperties,%function
    .p2align 2
vkGetPhysicalDeviceImageFormatProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1160
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceImageFormatProperties, .-vkGetPhysicalDeviceImageFormatProperties
    .globl vkGetPhysicalDeviceImageFormatProperties2
    .type vkGetPhysicalDeviceImageFormatProperties2,%function
    .p2align 2
vkGetPhysicalDeviceImageFormatProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1168
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceImageFormatProperties2, .-vkGetPhysicalDeviceImageFormatProperties2
    .globl vkGetPhysicalDeviceMemoryProperties
    .type vkGetPhysicalDeviceMemoryProperties,%function
    .p2align 2
vkGetPhysicalDeviceMemoryProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1176
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceMemoryProperties, .-vkGetPhysicalDeviceMemoryProperties
    .globl vkGetPhysicalDeviceMemoryProperties2
    .type vkGetPhysicalDeviceMemoryProperties2,%function
    .p2align 2
vkGetPhysicalDeviceMemoryProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1184
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceMemoryProperties2, .-vkGetPhysicalDeviceMemoryProperties2
    .globl vkGetPhysicalDevicePresentRectanglesKHR
    .type vkGetPhysicalDevicePresentRectanglesKHR,%function
    .p2align 2
vkGetPhysicalDevicePresentRectanglesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1192
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDevicePresentRectanglesKHR, .-vkGetPhysicalDevicePresentRectanglesKHR
    .globl vkGetPhysicalDeviceProperties
    .type vkGetPhysicalDeviceProperties,%function
    .p2align 2
vkGetPhysicalDeviceProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1200
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceProperties, .-vkGetPhysicalDeviceProperties
    .globl vkGetPhysicalDeviceProperties2
    .type vkGetPhysicalDeviceProperties2,%function
    .p2align 2
vkGetPhysicalDeviceProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1208
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceProperties2, .-vkGetPhysicalDeviceProperties2
    .globl vkGetPhysicalDeviceQueueFamilyProperties
    .type vkGetPhysicalDeviceQueueFamilyProperties,%function
    .p2align 2
vkGetPhysicalDeviceQueueFamilyProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1216
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceQueueFamilyProperties, .-vkGetPhysicalDeviceQueueFamilyProperties
    .globl vkGetPhysicalDeviceQueueFamilyProperties2
    .type vkGetPhysicalDeviceQueueFamilyProperties2,%function
    .p2align 2
vkGetPhysicalDeviceQueueFamilyProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1224
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceQueueFamilyProperties2, .-vkGetPhysicalDeviceQueueFamilyProperties2
    .globl vkGetPhysicalDeviceSparseImageFormatProperties
    .type vkGetPhysicalDeviceSparseImageFormatProperties,%function
    .p2align 2
vkGetPhysicalDeviceSparseImageFormatProperties:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1232
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSparseImageFormatProperties, .-vkGetPhysicalDeviceSparseImageFormatProperties
    .globl vkGetPhysicalDeviceSparseImageFormatProperties2
    .type vkGetPhysicalDeviceSparseImageFormatProperties2,%function
    .p2align 2
vkGetPhysicalDeviceSparseImageFormatProperties2:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1240
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSparseImageFormatProperties2, .-vkGetPhysicalDeviceSparseImageFormatProperties2
    .globl vkGetPhysicalDeviceSurfaceCapabilitiesKHR
    .type vkGetPhysicalDeviceSurfaceCapabilitiesKHR,%function
    .p2align 2
vkGetPhysicalDeviceSurfaceCapabilitiesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1248
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSurfaceCapabilitiesKHR, .-vkGetPhysicalDeviceSurfaceCapabilitiesKHR
    .globl vkGetPhysicalDeviceSurfaceFormatsKHR
    .type vkGetPhysicalDeviceSurfaceFormatsKHR,%function
    .p2align 2
vkGetPhysicalDeviceSurfaceFormatsKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1256
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSurfaceFormatsKHR, .-vkGetPhysicalDeviceSurfaceFormatsKHR
    .globl vkGetPhysicalDeviceSurfacePresentModesKHR
    .type vkGetPhysicalDeviceSurfacePresentModesKHR,%function
    .p2align 2
vkGetPhysicalDeviceSurfacePresentModesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1264
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSurfacePresentModesKHR, .-vkGetPhysicalDeviceSurfacePresentModesKHR
    .globl vkGetPhysicalDeviceSurfaceSupportKHR
    .type vkGetPhysicalDeviceSurfaceSupportKHR,%function
    .p2align 2
vkGetPhysicalDeviceSurfaceSupportKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1272
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPhysicalDeviceSurfaceSupportKHR, .-vkGetPhysicalDeviceSurfaceSupportKHR
    .globl vkGetPipelineCacheData
    .type vkGetPipelineCacheData,%function
    .p2align 2
vkGetPipelineCacheData:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1280
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetPipelineCacheData, .-vkGetPipelineCacheData
    .globl vkGetQueryPoolResults
    .type vkGetQueryPoolResults,%function
    .p2align 2
vkGetQueryPoolResults:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1288
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetQueryPoolResults, .-vkGetQueryPoolResults
    .globl vkGetRenderAreaGranularity
    .type vkGetRenderAreaGranularity,%function
    .p2align 2
vkGetRenderAreaGranularity:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1296
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetRenderAreaGranularity, .-vkGetRenderAreaGranularity
    .globl vkGetSwapchainImagesKHR
    .type vkGetSwapchainImagesKHR,%function
    .p2align 2
vkGetSwapchainImagesKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1304
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkGetSwapchainImagesKHR, .-vkGetSwapchainImagesKHR
    .globl vkInvalidateMappedMemoryRanges
    .type vkInvalidateMappedMemoryRanges,%function
    .p2align 2
vkInvalidateMappedMemoryRanges:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1312
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkInvalidateMappedMemoryRanges, .-vkInvalidateMappedMemoryRanges
    .globl vkMapMemory
    .type vkMapMemory,%function
    .p2align 2
vkMapMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1320
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkMapMemory, .-vkMapMemory
    .globl vkMergePipelineCaches
    .type vkMergePipelineCaches,%function
    .p2align 2
vkMergePipelineCaches:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1328
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkMergePipelineCaches, .-vkMergePipelineCaches
    .globl vkQueueBindSparse
    .type vkQueueBindSparse,%function
    .p2align 2
vkQueueBindSparse:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1336
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkQueueBindSparse, .-vkQueueBindSparse
    .globl vkQueuePresentKHR
    .type vkQueuePresentKHR,%function
    .p2align 2
vkQueuePresentKHR:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1344
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkQueuePresentKHR, .-vkQueuePresentKHR
    .globl vkQueueSubmit
    .type vkQueueSubmit,%function
    .p2align 2
vkQueueSubmit:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1352
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkQueueSubmit, .-vkQueueSubmit
    .globl vkQueueWaitIdle
    .type vkQueueWaitIdle,%function
    .p2align 2
vkQueueWaitIdle:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1360
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkQueueWaitIdle, .-vkQueueWaitIdle
    .globl vkResetCommandBuffer
    .type vkResetCommandBuffer,%function
    .p2align 2
vkResetCommandBuffer:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1368
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkResetCommandBuffer, .-vkResetCommandBuffer
    .globl vkResetCommandPool
    .type vkResetCommandPool,%function
    .p2align 2
vkResetCommandPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1376
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkResetCommandPool, .-vkResetCommandPool
    .globl vkResetDescriptorPool
    .type vkResetDescriptorPool,%function
    .p2align 2
vkResetDescriptorPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1384
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkResetDescriptorPool, .-vkResetDescriptorPool
    .globl vkResetEvent
    .type vkResetEvent,%function
    .p2align 2
vkResetEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1392
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkResetEvent, .-vkResetEvent
    .globl vkResetFences
    .type vkResetFences,%function
    .p2align 2
vkResetFences:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1400
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkResetFences, .-vkResetFences
    .globl vkSetEvent
    .type vkSetEvent,%function
    .p2align 2
vkSetEvent:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1408
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkSetEvent, .-vkSetEvent
    .globl vkTrimCommandPool
    .type vkTrimCommandPool,%function
    .p2align 2
vkTrimCommandPool:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1416
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkTrimCommandPool, .-vkTrimCommandPool
    .globl vkUnmapMemory
    .type vkUnmapMemory,%function
    .p2align 2
vkUnmapMemory:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1424
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkUnmapMemory, .-vkUnmapMemory
    .globl vkUpdateDescriptorSets
    .type vkUpdateDescriptorSets,%function
    .p2align 2
vkUpdateDescriptorSets:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1432
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkUpdateDescriptorSets, .-vkUpdateDescriptorSets
    .globl vkUpdateDescriptorSetWithTemplate
    .type vkUpdateDescriptorSetWithTemplate,%function
    .p2align 2
vkUpdateDescriptorSetWithTemplate:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1440
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkUpdateDescriptorSetWithTemplate, .-vkUpdateDescriptorSetWithTemplate
    .globl vkWaitForFences
    .type vkWaitForFences,%function
    .p2align 2
vkWaitForFences:
    adrp x17, g_vk_slots
    add x17, x17, :lo12:g_vk_slots
    add x17, x17, #1448
    ldr x16, [x17]
    cbz x16, vk_shim_lazy
    br x16
    .size vkWaitForFences, .-vkWaitForFences
