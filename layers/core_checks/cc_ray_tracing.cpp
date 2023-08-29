/* Copyright (c) 2015-2023 The Khronos Group Inc.
 * Copyright (c) 2015-2023 Valve Corporation
 * Copyright (c) 2015-2023 LunarG, Inc.
 * Copyright (C) 2015-2023 Google Inc.
 * Modifications Copyright (C) 2020-2022 Advanced Micro Devices, Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <algorithm>
#include <assert.h>
#include <sstream>
#include <string>

#include <vulkan/vk_enum_string_helper.h>
#include "generated/chassis.h"
#include "core_validation.h"
#include "cc_buffer_address.h"
#include "utils/ray_tracing_utils.h"

bool CoreChecks::ValidateInsertAccelerationStructureMemoryRange(VkAccelerationStructureNV as, const DEVICE_MEMORY_STATE *mem_info,
                                                                VkDeviceSize mem_offset, const Location &loc) const {
    return ValidateInsertMemoryRange(VulkanTypedHandle(as, kVulkanObjectTypeAccelerationStructureNV), mem_info, mem_offset, loc);
}

bool CoreChecks::PreCallValidateCreateAccelerationStructureNV(VkDevice device,
                                                              const VkAccelerationStructureCreateInfoNV *pCreateInfo,
                                                              const VkAllocationCallbacks *pAllocator,
                                                              VkAccelerationStructureNV *pAccelerationStructure,
                                                              const ErrorObject &error_obj) const {
    bool skip = false;
    if (pCreateInfo != nullptr && pCreateInfo->info.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV) {
        for (uint32_t i = 0; i < pCreateInfo->info.geometryCount; i++) {
            skip |= ValidateGeometryNV(pCreateInfo->info.pGeometries[i], "vkCreateAccelerationStructureNV():");
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCreateAccelerationStructureKHR(VkDevice device,
                                                               const VkAccelerationStructureCreateInfoKHR *pCreateInfo,
                                                               const VkAllocationCallbacks *pAllocator,
                                                               VkAccelerationStructureKHR *pAccelerationStructure,
                                                               const ErrorObject &error_obj) const {
    bool skip = false;
    if (pCreateInfo) {
        auto buffer_state = Get<BUFFER_STATE>(pCreateInfo->buffer);
        if (buffer_state) {
            if (!(buffer_state->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR)) {
                skip |=
                    LogError(device, "VUID-VkAccelerationStructureCreateInfoKHR-buffer-03614",
                             "VkAccelerationStructureCreateInfoKHR(): buffer must have been created with a usage value containing "
                             "VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR.");
            }
            if (buffer_state->createInfo.flags & VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT) {
                skip |= LogError(device, "VUID-VkAccelerationStructureCreateInfoKHR-buffer-03615",
                                 "VkAccelerationStructureCreateInfoKHR(): buffer must not have been created with "
                                 "VK_BUFFER_CREATE_SPARSE_RESIDENCY_BIT.");
            }
            if (pCreateInfo->offset + pCreateInfo->size > buffer_state->createInfo.size) {
                skip |= LogError(
                    device, "VUID-VkAccelerationStructureCreateInfoKHR-offset-03616",
                    "VkAccelerationStructureCreateInfoKHR(): The sum of offset and size must be less than the size of buffer.");
            }
        }
    }
    return skip;
}

bool CoreChecks::ValidateBindAccelerationStructureMemory(VkDevice device,
                                                         const VkBindAccelerationStructureMemoryInfoNV &info) const {
    bool skip = false;

    auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(info.accelerationStructure);
    if (!as_state) {
        return skip;
    }
    if (as_state->HasFullRangeBound()) {
        skip |=
            LogError(info.accelerationStructure, "VUID-VkBindAccelerationStructureMemoryInfoNV-accelerationStructure-03620",
                     "vkBindAccelerationStructureMemoryNV(): accelerationStructure must not already be backed by a memory object.");
    }

    // Validate bound memory range information
    auto mem_info = Get<DEVICE_MEMORY_STATE>(info.memory);
    if (mem_info) {
        const Location loc(Func::vkBindAccelerationStructureMemoryNV);
        skip |= ValidateInsertAccelerationStructureMemoryRange(info.accelerationStructure, mem_info.get(), info.memoryOffset, loc);
        skip |= ValidateMemoryTypes(mem_info.get(), as_state->memory_requirements.memoryTypeBits, loc,
                                    "VUID-VkBindAccelerationStructureMemoryInfoNV-memory-03622");
    }

    // Validate memory requirements alignment
    if (SafeModulo(info.memoryOffset, as_state->memory_requirements.alignment) != 0) {
        skip |= LogError(info.accelerationStructure, "VUID-VkBindAccelerationStructureMemoryInfoNV-memoryOffset-03623",
                         "vkBindAccelerationStructureMemoryNV(): memoryOffset  0x%" PRIxLEAST64
                         " must be an integer multiple of the alignment 0x%" PRIxLEAST64
                         " member of the VkMemoryRequirements structure returned from "
                         "a call to vkGetAccelerationStructureMemoryRequirementsNV with accelerationStructure and type of "
                         "VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV",
                         info.memoryOffset, as_state->memory_requirements.alignment);
    }

    if (mem_info) {
        // Validate memory requirements size
        if (as_state->memory_requirements.size > (mem_info->alloc_info.allocationSize - info.memoryOffset)) {
            skip |= LogError(info.accelerationStructure, "VUID-VkBindAccelerationStructureMemoryInfoNV-size-03624",
                             "vkBindAccelerationStructureMemoryNV(): The size 0x%" PRIxLEAST64
                             " member of the VkMemoryRequirements structure returned from a call to "
                             "vkGetAccelerationStructureMemoryRequirementsNV with accelerationStructure and type of "
                             "VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV must be less than or equal to the size "
                             "of memory minus memoryOffset 0x%" PRIxLEAST64 ".",
                             as_state->memory_requirements.size, mem_info->alloc_info.allocationSize - info.memoryOffset);
        }
    }

    return skip;
}
bool CoreChecks::PreCallValidateBindAccelerationStructureMemoryNV(VkDevice device, uint32_t bindInfoCount,
                                                                  const VkBindAccelerationStructureMemoryInfoNV *pBindInfos,
                                                                  const ErrorObject &error_obj) const {
    bool skip = false;
    for (uint32_t i = 0; i < bindInfoCount; i++) {
        skip |= ValidateBindAccelerationStructureMemory(device, pBindInfos[i]);
    }
    return skip;
}

bool CoreChecks::PreCallValidateGetAccelerationStructureHandleNV(VkDevice device, VkAccelerationStructureNV accelerationStructure,
                                                                 size_t dataSize, void *pData, const ErrorObject &error_obj) const {
    bool skip = false;

    auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(accelerationStructure);
    if (as_state != nullptr) {
        skip = ValidateMemoryIsBoundToAccelerationStructure(device, *as_state, "vkGetAccelerationStructureHandleNV",
                                                            "VUID-vkGetAccelerationStructureHandleNV-accelerationStructure-02787");
    }

    return skip;
}

bool CoreChecks::PreCallValidateCmdBuildAccelerationStructuresKHR(
    VkCommandBuffer commandBuffer, uint32_t infoCount, const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos, const ErrorObject &error_obj) const {
    using sparse_container::range;
    bool skip = false;
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    skip |= ValidateCmd(*cb_state, error_obj.location);

    if (!pInfos || !ppBuildRangeInfos) {
        return skip;
    }

    for (uint32_t info_i = 0; info_i < infoCount; ++info_i) {
        const Location loc = error_obj.location.dot(Field::pInfos, info_i);
        const auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[info_i].srcAccelerationStructure);
        const auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[info_i].dstAccelerationStructure);

        if (dst_as_state != nullptr) {
            skip |= ValidateMemoryIsBoundToBuffer(commandBuffer, *dst_as_state->buffer_state, "vkCmdBuildAccelerationStructuresKHR",
                                                  "VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03707");
        }

        if (pInfos[info_i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) {
            if (pInfos[info_i].srcAccelerationStructure == VK_NULL_HANDLE) {
                const LogObjectList objlist(device, commandBuffer);
                skip |=
                    LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-04630", objlist, loc.dot(Field::mode),
                             "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR but srcAccelerationStructure is VK_NULL_HANDLE.");
            } else if (src_as_state == nullptr || !src_as_state->built ||
                       !(src_as_state->build_info_khr.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)) {
                const LogObjectList objlist(device, commandBuffer);
                skip |= LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03667", objlist, loc.dot(Field::mode),
                                 "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, srcAccelerationStructure has been previously "
                                 "constructed with flags %s.",
                                 string_VkBuildAccelerationStructureFlagsKHR(src_as_state->build_info_khr.flags).c_str());
            }
            if (src_as_state != nullptr) {
                if (!src_as_state->buffer_state) {
                    const LogObjectList objlist(device, commandBuffer, src_as_state->Handle());
                    skip |= LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03708", objlist, loc.dot(Field::mode),
                                     "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR but the buffer associated with "
                                     "srcAccelerationStructure is not valid.");
                } else {
                    skip |= ValidateMemoryIsBoundToBuffer(commandBuffer, *src_as_state->buffer_state,
                                                          "vkCmdBuildAccelerationStructuresKHR",
                                                          "VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03708");
                }
                if (pInfos[info_i].geometryCount != src_as_state->build_info_khr.geometryCount) {
                    const LogObjectList objlist(device, commandBuffer);
                    skip |= LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03758", objlist, loc.dot(Field::mode),
                                     "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,"
                                     " but geometryCount (%" PRIu32
                                     ")  must have the same value which was specified when "
                                     "srcAccelerationStructure was last built (%" PRIu32 ").",
                                     pInfos[info_i].geometryCount, src_as_state->build_info_khr.geometryCount);
                }
                if (pInfos[info_i].flags != src_as_state->build_info_khr.flags) {
                    const LogObjectList objlist(device, commandBuffer);
                    skip |=
                        LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03759", objlist, loc.dot(Field::mode),
                                 "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, but flags (%s) must have the same value which"
                                 " was specified when srcAccelerationStructure was last built (%s).",
                                 string_VkBuildAccelerationStructureFlagsKHR(pInfos[info_i].flags).c_str(),
                                 string_VkBuildAccelerationStructureFlagsKHR(src_as_state->build_info_khr.flags).c_str());
                }
                if (pInfos[info_i].type != src_as_state->build_info_khr.type) {
                    const LogObjectList objlist(device, commandBuffer);
                    skip |=
                        LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03760", objlist, loc.dot(Field::mode),
                                 "is VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, but type (%s) must have the same value which"
                                 " was specified when srcAccelerationStructure was last built (%s).",
                                 string_VkAccelerationStructureTypeKHR(pInfos[info_i].type),
                                 string_VkAccelerationStructureTypeKHR(src_as_state->build_info_khr.type));
                }
            }
        }
        if (pInfos[info_i].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                const LogObjectList objlist(device, commandBuffer);
                skip |= LogError(
                    "VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03700", objlist, loc.dot(Field::type),
                    "is VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, but its dstAccelerationStructure was created with %s.",
                    string_VkAccelerationStructureTypeKHR(dst_as_state->create_infoKHR.type));
            }
        }
        if (pInfos[info_i].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                const LogObjectList objlist(device, commandBuffer);
                skip |= LogError(
                    "VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03699", objlist, loc.dot(Field::type),
                    "is VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, but its dstAccelerationStructure was created with %s.",
                    string_VkAccelerationStructureTypeKHR(dst_as_state->create_infoKHR.type));
            }
        }

        skip |= ValidateAccelerationBuffers(info_i, pInfos[info_i], loc);
    }

    auto no_as_buffer_memory_overlap_msg =
        [this](const char *variable_name_a, VkBuffer buffer_handle_a, const char *variable_name_b, VkBuffer buffer_handle_b,
               VkDeviceMemory memory_handle, const range<VkDeviceSize> &overlap_range) -> std::string {
        std::stringstream error_msg_ss;
        error_msg_ss << "vkCmdBuildAccelerationStructuresKHR(): memory backing buffer (" << FormatHandle(buffer_handle_a)
                     << ") used as storage for " << variable_name_a << " overlaps memory backing buffer ("
                     << FormatHandle(buffer_handle_b) << ") used as storage for " << variable_name_b << ". Overlapped memory is "
                     << FormatHandle(memory_handle) << " on range " << string_range(overlap_range) << '.';

        return error_msg_ss.str();
    };

    auto validate_no_as_buffer_memory_overlap =
        [this, commandBuffer, &no_as_buffer_memory_overlap_msg](
            const ACCELERATION_STRUCTURE_STATE_KHR &accel_struct_a, const char *variable_name_a, const BUFFER_STATE &buffer_a,
            const sparse_container::range<VkDeviceSize> &range_a,

            const ACCELERATION_STRUCTURE_STATE_KHR &accel_struct_b, const char *variable_name_b, const BUFFER_STATE &buffer_b,
            const sparse_container::range<VkDeviceSize> &range_b,

            const char *vuid) {
            bool skip = false;

            if (const auto [memory, overlap_range] = buffer_a.GetResourceMemoryOverlap(range_a, &buffer_b, range_b);
                memory != VK_NULL_HANDLE) {
                const LogObjectList objlist(commandBuffer, accel_struct_a.Handle(), buffer_a.Handle(), accel_struct_b.Handle(),
                                            buffer_b.Handle());
                const std::string error_msg = no_as_buffer_memory_overlap_msg(variable_name_a, buffer_a.buffer(), variable_name_b,
                                                                              buffer_b.buffer(), memory, overlap_range);
                skip |= LogError(objlist, vuid, "%s", error_msg.c_str());
            }

            return skip;
        };

    for (uint32_t info_i = 0; info_i < infoCount; ++info_i) {
        const auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[info_i].srcAccelerationStructure);
        const auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[info_i].dstAccelerationStructure);

        // loop over the others VkAccelerationStructureBuildGeometryInfoKHR from pInfos
        for (uint32_t other_info_j = info_i; other_info_j < infoCount; ++other_info_j) {
            // Validate that scratch buffer's memory does not overlap destination acceleration structure's memory, or source
            // acceleration structure's memory if build mode is update, or other scratch buffers' memory.
            // Here validation is pessimistic: if one buffer associated to pInfos[other_info_j].scratchData.deviceAddress has an
            // overlap, an error will be logged.
// https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/6040
#if 0
            if (auto other_scratches = GetBuffersByAddress(pInfos[other_info_j].scratchData.deviceAddress);
                !other_scratches.empty()) {
                using BUFFER_STATE_PTR = ValidationStateTracker::BUFFER_STATE_PTR;
                BufferAddressValidation<3> other_scratches_validator;

                // Validate that scratch buffer's memory does not overlap destination acceleration structure's memory
                if (dst_as_state && dst_as_state->buffer_state) {
                    const BUFFER_STATE &dst_as_buffer = *dst_as_state->buffer_state;
                    const range<VkDeviceSize> dst_as_buffer_range(
                        dst_as_state->create_infoKHR.offset,
                        dst_as_state->create_infoKHR.offset + dst_as_state->create_infoKHR.size);

                    other_scratches_validator.AddVuidValidation(
                        {"VUID-vkCmdBuildAccelerationStructuresKHR-dstAccelerationStructure-03703",
                         LogObjectList(commandBuffer, dst_as_state->Handle(), dst_as_buffer.Handle()),
                         // clang-format off
                         [info_i,
                         other_info_j,
                         &no_as_buffer_memory_overlap_msg,
                         &dst_as_buffer,
                         dst_as_buffer_range,
                         // Since scratch buffer size is unknown, compute an assumed scratch buffer size the idiomatic way
                         assumed_other_scratch_size = rt::ComputeScratchSize(device, pInfos[other_info_j], ppBuildRangeInfos[other_info_j]),
                         other_scratch_address = pInfos[other_info_j].scratchData.deviceAddress]
                         // clang-format on
                         (const BUFFER_STATE_PTR &other_scratch, std::string *out_error_msg) -> bool {
                             assert(other_scratch->DeviceAddressRange().includes(other_scratch_address));

                             const VkDeviceSize other_scratch_offset = other_scratch_address - other_scratch->deviceAddress;
                             const range<VkDeviceSize> other_scratch_range(
                                 other_scratch_offset,
                                 std::min(other_scratch_offset + assumed_other_scratch_size, other_scratch->createInfo.size));
                             if (other_scratch_range.invalid()) {
                                 // Do not validate this VU if range is invalid
                                 return true;
                             }

                             if (const auto [memory, overlap_range] = dst_as_buffer.GetResourceMemoryOverlap(
                                     dst_as_buffer_range, other_scratch, other_scratch_range);
                                 memory != VK_NULL_HANDLE) {
                                 if (out_error_msg) {
                                     std::stringstream dst_as_var_name_ss;
                                     dst_as_var_name_ss << "pInfos[" << info_i << "].dstAccelerationStructure";
                                     std::stringstream other_scratch_var_name_ss;
                                     other_scratch_var_name_ss << "pInfos[" << other_info_j << "].scratchData";

                                     *out_error_msg += no_as_buffer_memory_overlap_msg(
                                         dst_as_var_name_ss.str().c_str(), dst_as_buffer.buffer(),
                                         other_scratch_var_name_ss.str().c_str(), other_scratch->buffer(), memory, overlap_range);
                                 }
                                 return false;
                             }
                             return true;
                         },
                         [info_i, other_info_j]() {
                             return std::string("Some buffers associated to pInfos[") + std::to_string(other_info_j) +
                                    "].scratchData.deviceAddress have their underlying memory overlapping with the memory "
                                    "backing pInfos[" +
                                    std::to_string(info_i) + "].dstAccelerationStructure.\n";
                         }});
                }

                // Validate that scratch buffer's memory does not overlap source acceleration structure's memory if build mode is
                // update
                if (pInfos[info_i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR && src_as_state &&
                    src_as_state->buffer_state) {
                    const BUFFER_STATE &src_as_buffer = *src_as_state->buffer_state;
                    const range<VkDeviceSize> src_as_buffer_range(
                        src_as_state->create_infoKHR.offset,
                        src_as_state->create_infoKHR.offset + src_as_state->create_infoKHR.size);

                    other_scratches_validator.AddVuidValidation(
                        {"VUID-vkCmdBuildAccelerationStructuresKHR-scratchData-03705",
                         LogObjectList(commandBuffer, src_as_state->Handle(), src_as_buffer.Handle()),
                         // clang-format off
                         [info_i,
                         other_info_j,
                         &no_as_buffer_memory_overlap_msg,
                         &src_as_buffer,
                         src_as_buffer_range,
                         // Since scratch buffer size is unknown, compute an assumed scratch buffer size the idiomatic way
                         assumed_other_scratch_size = rt::ComputeScratchSize(device, pInfos[other_info_j], ppBuildRangeInfos[other_info_j]),
                         other_scratch_address = pInfos[other_info_j].scratchData.deviceAddress]
                         // clang-format on
                         (const BUFFER_STATE_PTR &other_scratch, std::string *out_error_msg) -> bool {
                             assert(other_scratch->DeviceAddressRange().includes(other_scratch_address));

                             const VkDeviceSize other_scratch_offset = other_scratch_address - other_scratch->deviceAddress;
                             const range<VkDeviceSize> other_scratch_range(
                                 other_scratch_offset,
                                 std::min(other_scratch_offset + assumed_other_scratch_size, other_scratch->createInfo.size));
                             if (other_scratch_range.invalid()) {
                                 // Do not validate this VU if range is invalid
                                 return true;
                             }

                             if (const auto [memory, overlap_range] = src_as_buffer.GetResourceMemoryOverlap(
                                     src_as_buffer_range, other_scratch, other_scratch_range);
                                 memory != VK_NULL_HANDLE) {
                                 if (out_error_msg) {
                                     std::stringstream src_as_var_name_ss;
                                     src_as_var_name_ss << "pInfos[" << info_i << "].srcAccelerationStructure";
                                     std::stringstream other_scratch_var_name_ss;
                                     other_scratch_var_name_ss << "pInfos[" << other_info_j << "].scratchData";

                                     *out_error_msg += no_as_buffer_memory_overlap_msg(
                                         src_as_var_name_ss.str().c_str(), src_as_buffer.buffer(),
                                         other_scratch_var_name_ss.str().c_str(), other_scratch->buffer(), memory, overlap_range);
                                 }
                                 return false;
                             }
                             return true;
                         },
                         [info_i, other_info_j]() {
                             return std::string("Some buffers associated to pInfos[") + std::to_string(other_info_j) +
                                    "].scratchData.deviceAddress have their underlying memory overlapping with the memory "
                                    "backing pInfos[" +
                                    std::to_string(info_i) + "].srcAccelerationStructure:\n";
                         }});
                }

                // Validate that scratch buffers' memory do not overlap.
                // Since pInfos[info_i].scratchData.deviceAddress can point to multiple buffers,
                // `other_scratch` needs to be validated against all of these buffers: if one pair has their respective memory
                // overlapping, validation failed
                auto scratches = GetBuffersByAddress(pInfos[info_i].scratchData.deviceAddress);
                if (info_i != other_info_j) {
                    if (!scratches.empty()) {
                        other_scratches_validator.AddVuidValidation(
                            {"VUID-vkCmdBuildAccelerationStructuresKHR-scratchData-03704", LogObjectList(commandBuffer),
                             // clang-format off
                             [this,
                             commandBuffer,
                             info_i,
                             scratch_address = pInfos[info_i].scratchData.deviceAddress,
                             assumed_scratch_size = rt::ComputeScratchSize(device, pInfos[info_i], ppBuildRangeInfos[info_i]),
                             &scratches,
                             other_scratch_address = pInfos[other_info_j].scratchData.deviceAddress,
                             assumed_other_scratch_size = rt::ComputeScratchSize(device, pInfos[other_info_j], ppBuildRangeInfos[other_info_j])]
                             // clang-format on
                             (const BUFFER_STATE_PTR &other_scratch, std::string *out_error_msg) -> bool {
                                 assert(other_scratch->DeviceAddressRange().includes(other_scratch_address));

                                 const VkDeviceSize other_scratch_offset = other_scratch_address - other_scratch->deviceAddress;
                                 const range<VkDeviceSize> other_scratch_range(
                                     other_scratch_offset,
                                     std::min(other_scratch_offset + assumed_other_scratch_size, other_scratch->createInfo.size));
                                 if (other_scratch_range.invalid()) {
                                     // Do not validate this VU if range is invalid
                                     return true;
                                 }

                                 // Create a nested BufferAddressValidation object, this time to loop over buffers associated to
                                 // pInfos[info_i].scratchData.deviceAddress
                                 // If one does overlap "other_scratch", then validation of 03704 failed
                                 BufferAddressValidation<1> scratch_and_other_scratch_overlap_validator = {
                                     {{{"No-VUID", LogObjectList(commandBuffer),
                                        // clang-format off
                                        [this,
                                        scratch_address,
                                        assumed_scratch_size,
                                        &other_scratch,
                                        other_scratch_range,
                                        parent_out_error_msg = out_error_msg]
                                        // clang-format on
                                        (const BUFFER_STATE_PTR &scratch, std::string *) -> bool {
                                            const VkDeviceSize scratch_offset = scratch_address - scratch->deviceAddress;
                                            const range<VkDeviceSize> scratch_range(
                                                scratch_offset,
                                                std::min(scratch_offset + assumed_scratch_size, scratch->createInfo.size));

                                            // Do not validate this VU if range is invalid
                                            if (scratch_range.invalid()) {
                                                return true;
                                            }

                                            if (const auto [memory, overlap_range] = scratch->GetResourceMemoryOverlap(
                                                    scratch_range, other_scratch, other_scratch_range);
                                                memory != VK_NULL_HANDLE) {
                                                if (parent_out_error_msg) {
                                                    std::stringstream scratch_error_msg_ss;
                                                    scratch_error_msg_ss << " {" << FormatHandle(scratch->buffer())
                                                                         << ", backed by " << FormatHandle(memory)
                                                                         << " - overlap on VkDeviceMemory space range "
                                                                         << string_range(overlap_range) << "}";
                                                    *parent_out_error_msg += scratch_error_msg_ss.str();
                                                }
                                                return false;
                                            }
                                            return true;
                                        }}}}};

                                 if (!out_error_msg) {
                                     return !scratch_and_other_scratch_overlap_validator.HasInvalidBuffer(scratches);
                                 } else {
                                     const std::string address_name = [&]() {
                                         std::stringstream address_name_ss;
                                         address_name_ss << "pInfos[" << info_i << "].scratchData.deviceAddress";
                                         return address_name_ss.str();
                                     }();
                                     *out_error_msg +=
                                         "Memory backing this buffer is overlapped by memory backing the following buffer(s) "
                                         "associated to ";
                                     *out_error_msg += address_name;
                                     *out_error_msg += ':';
                                     // Buffer from the `scratches` list overlapping `other_scratch` will be
                                     // appended in out_error_msg in the LogInvalidBuffers call
                                     return scratch_and_other_scratch_overlap_validator.LogInvalidBuffers(
                                         *this, scratches, "vkCmdBuildAccelerationStructures", address_name.c_str(),
                                         scratch_address);
                                 }
                             },
                             [info_i]() {
                                 return std::string(
                                            "The following buffers have their underlying memory overlapping buffers "
                                            "associated to pInfos[") +
                                        std::to_string(info_i) + "].scratchData.deviceAddress:\n";
                             }});
                    }
                }

                std::stringstream address_name_ss;
                address_name_ss << "pInfos[" << other_info_j << "].scratchData.deviceAddress";

                skip |= other_scratches_validator.LogErrorsIfInvalidBufferFound(
                    *this, other_scratches, "vkCmdBuildAccelerationStructuresKHR()", address_name_ss.str(),
                    pInfos[other_info_j].scratchData.deviceAddress);
            }
#endif
            // skip comparing to self pInfos[info_i]
            if (other_info_j != info_i) {
                const auto other_dst_as_state =
                    Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[other_info_j].dstAccelerationStructure);
                const auto other_src_as_state =
                    Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[other_info_j].srcAccelerationStructure);

                // Validate destination acceleration structure's memory is not overlapped by another source acceleration structure's
                // memory that is going to be updated by this cmd
                if (dst_as_state && dst_as_state->buffer_state && other_src_as_state && other_src_as_state->buffer_state) {
                    if (pInfos[other_info_j].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) {
                        std::stringstream dst_as_ss;
                        dst_as_ss << "pInfos[" << info_i << "].dstAccelerationStructure ("
                                  << FormatHandle(pInfos[info_i].dstAccelerationStructure) << ')';
                        std::stringstream other_src_as_ss;
                        other_src_as_ss << "pInfos[" << other_info_j << "].srcAccelerationStructure ("
                                        << FormatHandle(pInfos[other_info_j].srcAccelerationStructure) << ')';

                        const BUFFER_STATE &buffer_a = *dst_as_state->buffer_state;
                        const range<VkDeviceSize> range_a(dst_as_state->create_infoKHR.offset,
                                                          dst_as_state->create_infoKHR.offset + dst_as_state->create_infoKHR.size);

                        const BUFFER_STATE &buffer_b = *other_src_as_state->buffer_state;
                        const range<VkDeviceSize> range_b(
                            other_src_as_state->create_infoKHR.offset,
                            other_src_as_state->create_infoKHR.offset + other_src_as_state->create_infoKHR.size);

                        skip |= validate_no_as_buffer_memory_overlap(
                            *dst_as_state, dst_as_ss.str().c_str(), buffer_a, range_a, *other_src_as_state,
                            other_src_as_ss.str().c_str(), buffer_b, range_b,
                            "VUID-vkCmdBuildAccelerationStructuresKHR-dstAccelerationStructure-03701");
                    }
                }

                // Validate that there is no destination acceleration structures' memory overlaps
                if (dst_as_state && dst_as_state->buffer_state && other_dst_as_state && other_dst_as_state->buffer_state) {
                    std::stringstream dst_as_ss;
                    dst_as_ss << "pInfos[" << info_i << "].dstAccelerationStructure ("
                              << FormatHandle(pInfos[info_i].dstAccelerationStructure) << ')';
                    std::stringstream other_dst_as_ss;
                    other_dst_as_ss << "pInfos[" << other_info_j << "].dstAccelerationStructure ("
                                    << FormatHandle(pInfos[other_info_j].dstAccelerationStructure) << ')';

                    const BUFFER_STATE &buffer_a = *dst_as_state->buffer_state;
                    const range<VkDeviceSize> range_a(dst_as_state->create_infoKHR.offset,
                                                      dst_as_state->create_infoKHR.offset + dst_as_state->create_infoKHR.size);

                    const BUFFER_STATE &buffer_b = *other_dst_as_state->buffer_state;
                    const range<VkDeviceSize> range_b(
                        other_dst_as_state->create_infoKHR.offset,
                        other_dst_as_state->create_infoKHR.offset + other_dst_as_state->create_infoKHR.size);

                    skip |= validate_no_as_buffer_memory_overlap(
                        *dst_as_state, dst_as_ss.str().c_str(), buffer_a, range_a, *other_dst_as_state,
                        other_dst_as_ss.str().c_str(), buffer_b, range_b,
                        "VUID-vkCmdBuildAccelerationStructuresKHR-dstAccelerationStructure-03702");
                }
            }
        }
    }

    return skip;
}

bool CoreChecks::ValidateAccelerationBuffers(uint32_t info_index, const VkAccelerationStructureBuildGeometryInfoKHR &info,
                                             const Location &loc) const {
    bool skip = false;
    const auto geometry_count = info.geometryCount;
    const auto *p_geometries = info.pGeometries;
    const auto *const *const pp_geometries = info.ppGeometries;

    auto buffer_check = [this](uint32_t gi, const VkDeviceOrHostAddressConstKHR address, const Location &geom_loc) -> bool {
        const auto buffer_states = GetBuffersByAddress(address.deviceAddress);
        const bool no_valid_buffer_found =
            !buffer_states.empty() &&
            std::none_of(buffer_states.begin(), buffer_states.end(),
                         [](const ValidationStateTracker::BUFFER_STATE_PTR &buffer_state) {
                             return buffer_state->usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
                         });
        if (no_valid_buffer_found) {
            LogObjectList objlist(device);
            for (const auto &buffer_state : buffer_states) {
                objlist.add(buffer_state->Handle());
            }
            return LogError(
                "VUID-vkCmdBuildAccelerationStructuresKHR-geometry-03673", objlist, geom_loc,
                "has no buffer which created with VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");
        }

        return false;
    };

    // Parameter validation has already checked VUID-VkAccelerationStructureBuildGeometryInfoKHR-pGeometries-03788
    // !(pGeometries && ppGeometries)
    std::function<const VkAccelerationStructureGeometryKHR &(uint32_t)> geom_accessor;
    if (p_geometries) {
        geom_accessor = [p_geometries](uint32_t i) -> const VkAccelerationStructureGeometryKHR & { return p_geometries[i]; };
    } else if (pp_geometries) {
        geom_accessor = [pp_geometries](uint32_t i) -> const VkAccelerationStructureGeometryKHR & {
            // pp_geometries[i] is assumed to be a valid pointer
            return *pp_geometries[i];
        };
    }

    if (geom_accessor) {
        for (uint32_t geom_index = 0; geom_index < geometry_count; ++geom_index) {
            const Location geom_loc = loc.dot(Field::pGeometries, geom_index);
            const auto &geom_data = geom_accessor(geom_index);
            switch (geom_data.geometryType) {
                case VK_GEOMETRY_TYPE_TRIANGLES_KHR:  // == VK_GEOMETRY_TYPE_TRIANGLES_NV
                    skip |= buffer_check(geom_index, geom_data.geometry.triangles.vertexData,
                                         geom_loc.dot(Field::geometry).dot(Field::triangles).dot(Field::vertexData));
                    skip |= buffer_check(geom_index, geom_data.geometry.triangles.indexData,
                                         geom_loc.dot(Field::geometry).dot(Field::triangles).dot(Field::indexData));
                    skip |= buffer_check(geom_index, geom_data.geometry.triangles.transformData,
                                         geom_loc.dot(Field::geometry).dot(Field::triangles).dot(Field::transformData));
                    break;
                case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                    skip |= buffer_check(geom_index, geom_data.geometry.instances.data,
                                         geom_loc.dot(Field::geometry).dot(Field::instances).dot(Field::data));
                    break;
                case VK_GEOMETRY_TYPE_AABBS_KHR:  // == VK_GEOMETRY_TYPE_AABBS_NV
                    skip |= buffer_check(geom_index, geom_data.geometry.aabbs.data,
                                         geom_loc.dot(Field::geometry).dot(Field::aabbs).dot(Field::data));
                    break;
                default:
                    // no-op
                    break;
            }
        }
    }

    const auto buffer_states = GetBuffersByAddress(info.scratchData.deviceAddress);
    if (buffer_states.empty()) {
        skip |= LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03802", device,
                         loc.dot(Field::scratchData).dot(Field::deviceAddress),
                         "(0x%" PRIx64 ") has no buffer is associated with it.", info.scratchData.deviceAddress);
    } else {
        const bool no_valid_buffer_found = std::none_of(buffer_states.begin(), buffer_states.end(),
                                                        [](const ValidationStateTracker::BUFFER_STATE_PTR &buffer_state) {
                                                            return buffer_state->usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
                                                        });
        if (no_valid_buffer_found) {
            LogObjectList objlist(device);
            for (const auto &buffer_state : buffer_states) {
                objlist.add(buffer_state->Handle());
            }
            skip |= LogError("VUID-vkCmdBuildAccelerationStructuresKHR-pInfos-03674", objlist,
                             loc.dot(Field::scratchData).dot(Field::deviceAddress),
                             "(0x%" PRIx64
                             ") has no buffer is associated with it that was created with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT bit.",
                             info.scratchData.deviceAddress);
        }
    }

    return skip;
}

bool CoreChecks::PreCallValidateBuildAccelerationStructuresKHR(
    VkDevice device, VkDeferredOperationKHR deferredOperation, uint32_t infoCount,
    const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
    const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos, const ErrorObject &error_obj) const {
    bool skip = false;
    for (uint32_t i = 0; i < infoCount; ++i) {
        auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[i].srcAccelerationStructure);
        auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[i].dstAccelerationStructure);
        if (dst_as_state) {
            skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*dst_as_state->buffer_state, "vkBuildAccelerationStructuresKHR",
                                                             "VUID-vkBuildAccelerationStructuresKHR-pInfos-03722");
        }
        if (pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) {
            if (src_as_state == nullptr || !src_as_state->built ||
                !(src_as_state->build_info_khr.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)) {
                skip |= LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03667",
                                 "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its mode member is "
                                 "VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its srcAccelerationStructure member must have "
                                 "been built before with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR set in "
                                 "VkAccelerationStructureBuildGeometryInfoKHR::flags.");
            }
            if (src_as_state) {
                skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*src_as_state->buffer_state, "vkBuildAccelerationStructuresKHR",
                                                                 "VUID-vkBuildAccelerationStructuresKHR-pInfos-03723");
                if (pInfos[i].geometryCount != src_as_state->build_info_khr.geometryCount) {
                    skip |= LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03758",
                                     "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its mode member is "
                                     "VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,"
                                     " its geometryCount member must have the same value which was specified when "
                                     "srcAccelerationStructure was last built.");
                }
                if (pInfos[i].flags != src_as_state->build_info_khr.flags) {
                    skip |=
                        LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03759",
                                 "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its mode member is"
                                 " VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its flags member must have the same value which"
                                 " was specified when srcAccelerationStructure was last built.");
                }
                if (pInfos[i].type != src_as_state->build_info_khr.type) {
                    skip |=
                        LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03760",
                                 "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its mode member is"
                                 " VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its type member must have the same value which"
                                 " was specified when srcAccelerationStructure was last built.");
                }
            }
        }
        if (pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                skip |= LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03700",
                                 "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its type member is "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, its dstAccelerationStructure member must have "
                                 "been created with a value of VkAccelerationStructureCreateInfoKHR::type equal to either "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR or VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR.");
            }
        }
        if (pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                skip |= LogError(device, "VUID-vkBuildAccelerationStructuresKHR-pInfos-03699",
                                 "vkBuildAccelerationStructuresKHR(): For each element of pInfos, if its type member is "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, its dstAccelerationStructure member must have been "
                                 "created with a value of VkAccelerationStructureCreateInfoKHR::type equal to either "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR or VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR.");
            }
        }
    }
    return skip;
}
bool CoreChecks::PreCallValidateCmdBuildAccelerationStructureNV(VkCommandBuffer commandBuffer,
                                                                const VkAccelerationStructureInfoNV *pInfo, VkBuffer instanceData,
                                                                VkDeviceSize instanceOffset, VkBool32 update,
                                                                VkAccelerationStructureNV dst, VkAccelerationStructureNV src,
                                                                VkBuffer scratch, VkDeviceSize scratchOffset,
                                                                const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;

    skip |= ValidateCmd(*cb_state, error_obj.location);

    if (pInfo != nullptr && pInfo->type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV) {
        for (uint32_t i = 0; i < pInfo->geometryCount; i++) {
            skip |= ValidateGeometryNV(pInfo->pGeometries[i], "vkCmdBuildAccelerationStructureNV():");
        }
    }

    if (pInfo != nullptr && pInfo->geometryCount > phys_dev_ext_props.ray_tracing_props_nv.maxGeometryCount) {
        skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-geometryCount-02241",
                         "vkCmdBuildAccelerationStructureNV(): geometryCount [%d] must be less than or equal to "
                         "VkPhysicalDeviceRayTracingPropertiesNV::maxGeometryCount.",
                         pInfo->geometryCount);
    }

    auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE>(dst);
    auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE>(src);
    auto scratch_buffer_state = Get<BUFFER_STATE>(scratch);

    if (dst_as_state != nullptr && pInfo != nullptr) {
        if (dst_as_state->create_infoNV.info.type != pInfo->type) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                             "vkCmdBuildAccelerationStructureNV(): create info VkAccelerationStructureInfoNV::type"
                             "[%s] must be identical to build info VkAccelerationStructureInfoNV::type [%s].",
                             string_VkAccelerationStructureTypeKHR(dst_as_state->create_infoNV.info.type),
                             string_VkAccelerationStructureTypeKHR(pInfo->type));
        }
        if (dst_as_state->create_infoNV.info.flags != pInfo->flags) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                             "vkCmdBuildAccelerationStructureNV(): create info VkAccelerationStructureInfoNV::flags"
                             "[0x%x] must be identical to build info VkAccelerationStructureInfoNV::flags [0x%x].",
                             dst_as_state->create_infoNV.info.flags, pInfo->flags);
        }
        if (dst_as_state->create_infoNV.info.instanceCount < pInfo->instanceCount) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                             "vkCmdBuildAccelerationStructureNV(): create info VkAccelerationStructureInfoNV::instanceCount "
                             "[%d] must be greater than or equal to build info VkAccelerationStructureInfoNV::instanceCount [%d].",
                             dst_as_state->create_infoNV.info.instanceCount, pInfo->instanceCount);
        }
        if (dst_as_state->create_infoNV.info.geometryCount < pInfo->geometryCount) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                             "vkCmdBuildAccelerationStructureNV(): create info VkAccelerationStructureInfoNV::geometryCount"
                             "[%d] must be greater than or equal to build info VkAccelerationStructureInfoNV::geometryCount [%d].",
                             dst_as_state->create_infoNV.info.geometryCount, pInfo->geometryCount);
        } else {
            for (uint32_t i = 0; i < pInfo->geometryCount; i++) {
                const VkGeometryDataNV &create_geometry_data = dst_as_state->create_infoNV.info.pGeometries[i].geometry;
                const VkGeometryDataNV &build_geometry_data = pInfo->pGeometries[i].geometry;
                if (create_geometry_data.triangles.vertexCount < build_geometry_data.triangles.vertexCount) {
                    skip |= LogError(
                        commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                        "vkCmdBuildAccelerationStructureNV(): create info pGeometries[%d].geometry.triangles.vertexCount [%d]"
                        "must be greater than or equal to build info pGeometries[%d].geometry.triangles.vertexCount [%d].",
                        i, create_geometry_data.triangles.vertexCount, i, build_geometry_data.triangles.vertexCount);
                    break;
                }
                if (create_geometry_data.triangles.indexCount < build_geometry_data.triangles.indexCount) {
                    skip |= LogError(
                        commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                        "vkCmdBuildAccelerationStructureNV(): create info pGeometries[%d].geometry.triangles.indexCount [%d]"
                        "must be greater than or equal to build info pGeometries[%d].geometry.triangles.indexCount [%d].",
                        i, create_geometry_data.triangles.indexCount, i, build_geometry_data.triangles.indexCount);
                    break;
                }
                if (create_geometry_data.aabbs.numAABBs < build_geometry_data.aabbs.numAABBs) {
                    skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-dst-02488",
                                     "vkCmdBuildAccelerationStructureNV(): create info pGeometries[%d].geometry.aabbs.numAABBs [%d]"
                                     "must be greater than or equal to build info pGeometries[%d].geometry.aabbs.numAABBs [%d].",
                                     i, create_geometry_data.aabbs.numAABBs, i, build_geometry_data.aabbs.numAABBs);
                    break;
                }
            }
        }
    }

    if (dst_as_state != nullptr) {
        skip |= ValidateMemoryIsBoundToAccelerationStructure(commandBuffer, *dst_as_state, "vkCmdBuildAccelerationStructureNV()",
                                                             "VUID-vkCmdBuildAccelerationStructureNV-dst-07787");
    }

    if (update == VK_TRUE) {
        if (src == VK_NULL_HANDLE) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-update-02489",
                             "vkCmdBuildAccelerationStructureNV(): If update is VK_TRUE, src must not be VK_NULL_HANDLE.");
        } else {
            if (src_as_state == nullptr || !src_as_state->built ||
                !(src_as_state->build_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV)) {
                skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-update-02490",
                                 "vkCmdBuildAccelerationStructureNV(): If update is VK_TRUE, src must have been built before "
                                 "with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_NV set in "
                                 "VkAccelerationStructureInfoNV::flags.");
            }
        }
        if (dst_as_state != nullptr && !dst_as_state->update_scratch_memory_requirements_checked) {
            skip |=
                LogWarning(dst, kVUID_Core_CmdBuildAccelNV_NoUpdateMemReqQuery,
                           "vkCmdBuildAccelerationStructureNV(): Updating %s but vkGetAccelerationStructureMemoryRequirementsNV() "
                           "has not been called for update scratch memory.",
                           FormatHandle(dst_as_state->acceleration_structure()).c_str());
            // Use requirements fetched at create time
        }
        if (scratch_buffer_state != nullptr && dst_as_state != nullptr &&
            dst_as_state->update_scratch_memory_requirements.size > (scratch_buffer_state->createInfo.size - scratchOffset)) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-update-02492",
                             "vkCmdBuildAccelerationStructureNV(): If update is VK_TRUE, The size member of the "
                             "VkMemoryRequirements structure returned from a call to "
                             "vkGetAccelerationStructureMemoryRequirementsNV with "
                             "VkAccelerationStructureMemoryRequirementsInfoNV::accelerationStructure set to dst and "
                             "VkAccelerationStructureMemoryRequirementsInfoNV::type set to "
                             "VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_UPDATE_SCRATCH_NV must be less than "
                             "or equal to the size of scratch minus scratchOffset");
        }
    } else {
        if (dst_as_state != nullptr && !dst_as_state->build_scratch_memory_requirements_checked) {
            skip |= LogWarning(dst, kVUID_Core_CmdBuildAccelNV_NoScratchMemReqQuery,
                               "vkCmdBuildAccelerationStructureNV(): Assigning scratch buffer to %s but "
                               "vkGetAccelerationStructureMemoryRequirementsNV() has not been called for scratch memory.",
                               FormatHandle(dst_as_state->acceleration_structure()).c_str());
            // Use requirements fetched at create time
        }
        if (scratch_buffer_state != nullptr && dst_as_state != nullptr &&
            dst_as_state->build_scratch_memory_requirements.size > (scratch_buffer_state->createInfo.size - scratchOffset)) {
            skip |= LogError(commandBuffer, "VUID-vkCmdBuildAccelerationStructureNV-update-02491",
                             "vkCmdBuildAccelerationStructureNV(): If update is VK_FALSE, The size member of the "
                             "VkMemoryRequirements structure returned from a call to "
                             "vkGetAccelerationStructureMemoryRequirementsNV with "
                             "VkAccelerationStructureMemoryRequirementsInfoNV::accelerationStructure set to dst and "
                             "VkAccelerationStructureMemoryRequirementsInfoNV::type set to "
                             "VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV must be less than "
                             "or equal to the size of scratch minus scratchOffset");
        }
    }
    if (instanceData != VK_NULL_HANDLE) {
        auto buffer_state = Get<BUFFER_STATE>(instanceData);
        if (buffer_state) {
            skip |= ValidateBufferUsageFlags(
                LogObjectList(commandBuffer, instanceData), *buffer_state, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, true,
                "VUID-VkAccelerationStructureInfoNV-instanceData-02782", error_obj.location.dot(Field::instanceData));
        }
    }
    if (scratch_buffer_state) {
        skip |= ValidateBufferUsageFlags(
            LogObjectList(commandBuffer, scratch), *scratch_buffer_state, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, true,
            "VUID-VkAccelerationStructureInfoNV-scratch-02781", error_obj.location.dot(Field::scratch));
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdCopyAccelerationStructureNV(VkCommandBuffer commandBuffer, VkAccelerationStructureNV dst,
                                                               VkAccelerationStructureNV src,
                                                               VkCopyAccelerationStructureModeNV mode,
                                                               const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;

    skip |= ValidateCmd(*cb_state, error_obj.location);
    auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE>(dst);
    auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE>(src);

    if (dst_as_state != nullptr) {
        skip |= ValidateMemoryIsBoundToAccelerationStructure(commandBuffer, *dst_as_state, "vkCmdCopyAccelerationStructureNV()",
                                                             "VUID-vkCmdCopyAccelerationStructureNV-dst-07792");
    }

    if (mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_NV) {
        if (src_as_state != nullptr &&
            (!src_as_state->built || !(src_as_state->build_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_NV))) {
            skip |= LogError(commandBuffer, "VUID-vkCmdCopyAccelerationStructureNV-src-03411",
                             "vkCmdCopyAccelerationStructureNV(): src must have been built with "
                             "VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_NV if mode is "
                             "VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_NV.");
        }
    }
    if (!(mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_NV || mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR)) {
        skip |= LogError(commandBuffer, "VUID-vkCmdCopyAccelerationStructureNV-mode-03410",
                         "vkCmdCopyAccelerationStructureNV():mode must be VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR"
                         "or VK_COPY_ACCELERATION_STRUCTURE_MODE_CLONE_KHR.");
    }
    return skip;
}

bool CoreChecks::PreCallValidateDestroyAccelerationStructureNV(VkDevice device, VkAccelerationStructureNV accelerationStructure,
                                                               const VkAllocationCallbacks *pAllocator,
                                                               const ErrorObject &error_obj) const {
    auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(accelerationStructure);
    bool skip = false;
    if (as_state) {
        skip |= ValidateObjectNotInUse(as_state.get(), error_obj.location,
                                       "VUID-vkDestroyAccelerationStructureNV-accelerationStructure-03752");
    }
    return skip;
}

bool CoreChecks::PreCallValidateDestroyAccelerationStructureKHR(VkDevice device, VkAccelerationStructureKHR accelerationStructure,
                                                                const VkAllocationCallbacks *pAllocator,
                                                                const ErrorObject &error_obj) const {
    auto as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(accelerationStructure);
    bool skip = false;
    if (as_state) {
        skip |= ValidateObjectNotInUse(as_state.get(), error_obj.location,
                                       "VUID-vkDestroyAccelerationStructureKHR-accelerationStructure-02442");
    }
    return skip;
}

void CoreChecks::PreCallRecordCmdWriteAccelerationStructuresPropertiesKHR(VkCommandBuffer commandBuffer,
                                                                          uint32_t accelerationStructureCount,
                                                                          const VkAccelerationStructureKHR *pAccelerationStructures,
                                                                          VkQueryType queryType, VkQueryPool queryPool,
                                                                          uint32_t firstQuery) {
    if (disabled[query_validation]) return;
    // Enqueue the submit time validation check here, before the submit time state update in StateTracker::PostCall...
    auto cb_state = GetWrite<CMD_BUFFER_STATE>(commandBuffer);
    cb_state->queryUpdates.emplace_back([accelerationStructureCount, firstQuery, queryPool](
                                            CMD_BUFFER_STATE &cb_state_arg, bool do_validate, VkQueryPool &firstPerfQueryPool,
                                            uint32_t perfPass, QueryMap *localQueryToStateMap) {
        if (!do_validate) return false;
        bool skip = false;
        for (uint32_t i = 0; i < accelerationStructureCount; i++) {
            QueryObject query_obj = {queryPool, firstQuery + i, perfPass};
            skip |= VerifyQueryIsReset(cb_state_arg, query_obj, Func::vkCmdWriteAccelerationStructuresPropertiesKHR,
                                       firstPerfQueryPool, perfPass, localQueryToStateMap);
        }
        return skip;
    });
}

bool CoreChecks::PreCallValidateWriteAccelerationStructuresPropertiesKHR(VkDevice device, uint32_t accelerationStructureCount,
                                                                         const VkAccelerationStructureKHR *pAccelerationStructures,
                                                                         VkQueryType queryType, size_t dataSize, void *pData,
                                                                         size_t stride, const ErrorObject &error_obj) const {
    bool skip = false;
    for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
        auto as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pAccelerationStructures[i]);
        const auto &as_info = as_state->build_info_khr;
        if (queryType == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR) {
            if (!(as_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
                const LogObjectList objlist(device, pAccelerationStructures[i]);
                skip |= LogError(objlist, "VUID-vkWriteAccelerationStructuresPropertiesKHR-accelerationStructures-03431",
                                 "vkWriteAccelerationStructuresPropertiesKHR(): pAccelerationStructures[%" PRIu32 "] has flags %s.",
                                 i, string_VkBuildAccelerationStructureFlagsKHR(as_info.flags).c_str());
            }
        }
        if (as_state) {
            skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*as_state->buffer_state, "vkWriteAccelerationStructuresPropertiesKHR",
                                                             "VUID-vkWriteAccelerationStructuresPropertiesKHR-buffer-03733");
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdWriteAccelerationStructuresPropertiesKHR(
    VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureKHR *pAccelerationStructures,
    VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery, const ErrorObject &error_obj) const {
    bool skip = false;
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    skip |= ValidateCmd(*cb_state, error_obj.location);
    auto query_pool_state = Get<QUERY_POOL_STATE>(queryPool);
    const auto &query_pool_ci = query_pool_state->createInfo;
    if (query_pool_ci.queryType != queryType) {
        skip |= LogError(
            device, "VUID-vkCmdWriteAccelerationStructuresPropertiesKHR-queryPool-02493",
            "vkCmdWriteAccelerationStructuresPropertiesKHR: queryPool must have been created with a queryType matching queryType.");
    }
    for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
        if (queryType == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR) {
            auto as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pAccelerationStructures[i]);
            if (!(as_state->build_info_khr.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
                skip |= LogError(
                    device, "VUID-vkCmdWriteAccelerationStructuresPropertiesKHR-accelerationStructures-03431",
                    "vkCmdWriteAccelerationStructuresPropertiesKHR: All acceleration structures in pAccelerationStructures "
                    "must have been built with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR if queryType is "
                    "VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR.");
            }
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdWriteAccelerationStructuresPropertiesNV(
    VkCommandBuffer commandBuffer, uint32_t accelerationStructureCount, const VkAccelerationStructureNV *pAccelerationStructures,
    VkQueryType queryType, VkQueryPool queryPool, uint32_t firstQuery, const ErrorObject &error_obj) const {
    bool skip = false;
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    skip |= ValidateCmd(*cb_state, error_obj.location);
    auto query_pool_state = Get<QUERY_POOL_STATE>(queryPool);
    const auto &query_pool_ci = query_pool_state->createInfo;
    if (query_pool_ci.queryType != queryType) {
        skip |= LogError(
            device, "VUID-vkCmdWriteAccelerationStructuresPropertiesNV-queryPool-03755",
            "vkCmdWriteAccelerationStructuresPropertiesNV: queryPool must have been created with a queryType matching queryType.");
    }
    for (uint32_t i = 0; i < accelerationStructureCount; ++i) {
        if (queryType == VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_NV) {
            auto as_state = Get<ACCELERATION_STRUCTURE_STATE>(pAccelerationStructures[i]);
            if (!(as_state->build_info.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
                skip |=
                    LogError(device, "VUID-vkCmdWriteAccelerationStructuresPropertiesNV-pAccelerationStructures-06215",
                             "vkCmdWriteAccelerationStructuresPropertiesNV: All acceleration structures in pAccelerationStructures "
                             "must have been built with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR if queryType is "
                             "VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_NV.");
            }
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdBuildAccelerationStructuresIndirectKHR(VkCommandBuffer commandBuffer, uint32_t infoCount,
                                                                          const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
                                                                          const VkDeviceAddress *pIndirectDeviceAddresses,
                                                                          const uint32_t *pIndirectStrides,
                                                                          const uint32_t *const *ppMaxPrimitiveCounts,
                                                                          const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, error_obj.location);
    // TODO - This is not called in vkCmdBuildAccelerationStructuresKHR and only seems used for ValidateActionState
    skip |= ValidateCmdRayQueryState(*cb_state, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, error_obj.location);
    for (uint32_t i = 0; i < infoCount; ++i) {
        auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[i].srcAccelerationStructure);
        auto dst_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfos[i].dstAccelerationStructure);
        skip |=
            ValidateMemoryIsBoundToBuffer(commandBuffer, *dst_as_state->buffer_state, "vkCmdBuildAccelerationStructuresIndirectKHR",
                                          "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03707");
        if (pInfos[i].mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR) {
            skip |= ValidateMemoryIsBoundToBuffer(commandBuffer, *src_as_state->buffer_state,
                                                  "vkCmdBuildAccelerationStructuresIndirectKHR",
                                                  "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03708");
            if (src_as_state == nullptr || !src_as_state->built ||
                !(src_as_state->build_info_khr.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR)) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03667",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR(): For each element of pInfos, if its mode member is "
                                 "VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its srcAccelerationStructure member must have "
                                 "been built before with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR set in "
                                 "VkAccelerationStructureBuildGeometryInfoKHR::flags.");
            }
            if (pInfos[i].geometryCount != src_as_state->build_info_khr.geometryCount) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03758",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR(): For each element of pInfos, if its mode member is "
                                 "VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR,"
                                 " its geometryCount member must have the same value which was specified when "
                                 "srcAccelerationStructure was last built.");
            }
            if (pInfos[i].flags != src_as_state->build_info_khr.flags) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03759",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR(): For each element of pInfos, if its mode member is"
                                 " VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its flags member must have the same value which"
                                 " was specified when srcAccelerationStructure was last built.");
            }
            if (pInfos[i].type != src_as_state->build_info_khr.type) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03760",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR(): For each element of pInfos, if its mode member is"
                                 " VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR, its type member must have the same value which"
                                 " was specified when srcAccelerationStructure was last built.");
            }
        }
        if (pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03700",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR(): For each element of pInfos, if its type member is "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, its dstAccelerationStructure member must have "
                                 "been created with a value of VkAccelerationStructureCreateInfoKHR::type equal to either "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR or VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR.");
            }
        }
        if (pInfos[i].type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR) {
            if (!dst_as_state ||
                (dst_as_state && dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR &&
                 dst_as_state->create_infoKHR.type != VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR)) {
                skip |= LogError(device, "VUID-vkCmdBuildAccelerationStructuresIndirectKHR-pInfos-03699",
                                 "vkCmdBuildAccelerationStructuresIndirectKHR():For each element of pInfos, if its type member is "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, its dstAccelerationStructure member must have been "
                                 "created with a value of VkAccelerationStructureCreateInfoKHR::type equal to either "
                                 "VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR or VK_ACCELERATION_STRUCTURE_TYPE_GENERIC_KHR.");
            }
        }
    }
    return skip;
}

bool CoreChecks::ValidateCopyAccelerationStructureInfoKHR(const VkCopyAccelerationStructureInfoKHR *pInfo,
                                                          const VulkanTypedHandle &handle, const Location &loc) const {
    bool skip = false;
    if (pInfo->mode == VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR) {
        auto src_as_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->src);
        if (!(src_as_state->build_info_khr.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)) {
            const LogObjectList objlist(handle, pInfo->src);
            skip |= LogError("VUID-VkCopyAccelerationStructureInfoKHR-src-03411", objlist, loc.dot(Field::src),
                             "(%s) must have been built with VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR"
                             "if mode is VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR.",
                             FormatHandle(pInfo->src).c_str());
        }
    }
    auto src_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->src);
    if (src_accel_state) {
        auto buffer_state = Get<BUFFER_STATE>(src_accel_state->create_infoKHR.buffer);
        skip |= ValidateMemoryIsBoundToBuffer(device, *buffer_state, loc.StringFunc(),
                                              "VUID-VkCopyAccelerationStructureInfoKHR-buffer-03718");
    }
    auto dst_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->dst);
    if (dst_accel_state) {
        auto buffer_state = Get<BUFFER_STATE>(dst_accel_state->create_infoKHR.buffer);
        skip |= ValidateMemoryIsBoundToBuffer(device, *buffer_state, loc.StringFunc(),
                                              "VUID-VkCopyAccelerationStructureInfoKHR-buffer-03719");
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdCopyAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                                const VkCopyAccelerationStructureInfoKHR *pInfo,
                                                                const ErrorObject &error_obj) const {
    bool skip = false;
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    skip |= ValidateCmd(*cb_state, error_obj.location);
    if (pInfo) {
        skip |= ValidateCopyAccelerationStructureInfoKHR(pInfo, error_obj.handle, error_obj.location.dot(Field::pInfo));
        auto src_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->src);
        if (src_accel_state) {
            skip |=
                ValidateMemoryIsBoundToBuffer(commandBuffer, *src_accel_state->buffer_state, "vkCmdCopyAccelerationStructureKHR",
                                              "VUID-vkCmdCopyAccelerationStructureKHR-buffer-03737");
        }
        auto dst_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->dst);
        if (dst_accel_state) {
            skip |=
                ValidateMemoryIsBoundToBuffer(commandBuffer, *dst_accel_state->buffer_state, "vkCmdCopyAccelerationStructureKHR",
                                              "VUID-vkCmdCopyAccelerationStructureKHR-buffer-03738");
        }
    }
    return skip;
}

bool CoreChecks::PreCallValidateCopyAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation,
                                                             const VkCopyAccelerationStructureInfoKHR *pInfo,
                                                             const ErrorObject &error_obj) const {
    bool skip = false;
    if (pInfo) {
        skip |= ValidateCopyAccelerationStructureInfoKHR(pInfo, error_obj.handle, error_obj.location.dot(Field::pInfo));
        auto src_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->src);
        if (src_accel_state) {
            skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*src_accel_state->buffer_state, "vkCopyAccelerationStructureKHR",
                                                             "VUID-vkCopyAccelerationStructureKHR-buffer-03727");
        }
        auto dst_accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->dst);
        if (dst_accel_state) {
            skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*dst_accel_state->buffer_state, "vkCopyAccelerationStructureKHR",
                                                             "VUID-vkCopyAccelerationStructureKHR-buffer-03728");
        }
    }
    return skip;
}
bool CoreChecks::PreCallValidateCmdCopyAccelerationStructureToMemoryKHR(VkCommandBuffer commandBuffer,
                                                                        const VkCopyAccelerationStructureToMemoryInfoKHR *pInfo,
                                                                        const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, error_obj.location);

    auto accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->src);
    if (accel_state) {
        auto buffer_state = Get<BUFFER_STATE>(accel_state->create_infoKHR.buffer);
        skip |= ValidateMemoryIsBoundToBuffer(commandBuffer, *buffer_state, "vkCmdCopyAccelerationStructureToMemoryKHR",
                                              "VUID-vkCmdCopyAccelerationStructureToMemoryKHR-None-03559");
    }
    return skip;
}

bool CoreChecks::PreCallValidateCopyMemoryToAccelerationStructureKHR(VkDevice device, VkDeferredOperationKHR deferredOperation,
                                                                     const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo,
                                                                     const ErrorObject &error_obj) const {
    bool skip = false;

    auto accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->dst);
    if (accel_state) {
        skip |= ValidateHostVisibleMemoryIsBoundToBuffer(*accel_state->buffer_state, "vkCopyMemoryToAccelerationStructureKHR",
                                                         "VUID-vkCopyMemoryToAccelerationStructureKHR-buffer-03730");
    }

    return skip;
}

bool CoreChecks::PreCallValidateCmdCopyMemoryToAccelerationStructureKHR(VkCommandBuffer commandBuffer,
                                                                        const VkCopyMemoryToAccelerationStructureInfoKHR *pInfo,
                                                                        const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    assert(cb_state);
    bool skip = false;
    skip |= ValidateCmd(*cb_state, error_obj.location);

    auto accel_state = Get<ACCELERATION_STRUCTURE_STATE_KHR>(pInfo->dst);
    if (accel_state) {
        skip |=
            ValidateMemoryIsBoundToBuffer(commandBuffer, *accel_state->buffer_state, "vkCmdCopyAccelerationStructureToMemoryKHR",
                                          "VUID-vkCmdCopyMemoryToAccelerationStructureKHR-buffer-03745");
    }
    return skip;
}

bool CoreChecks::ValidateCmdRayQueryState(const CMD_BUFFER_STATE &cb_state, const VkPipelineBindPoint bind_point,
                                          const Location &loc) const {
    bool skip = false;
    const DrawDispatchVuid &vuid = GetDrawDispatchVuid(loc.function);
    const auto lv_bind_point = ConvertToLvlBindPoint(bind_point);
    const auto &last_bound = cb_state.lastBound[lv_bind_point];
    const auto *pipe = last_bound.pipeline_state;

    bool ray_query_shader = false;
    if (nullptr != pipe) {
        if (bind_point == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
            ray_query_shader = true;
        } else {
            // TODO - Loop through shader for RayQueryKHR for draw/dispatch commands
        }
    }

    if (cb_state.unprotected == false && ray_query_shader) {
        skip |= LogError(vuid.ray_query_protected_cb_03635, cb_state.commandBuffer(), loc,
                         "can't use in protected command buffers for RayQuery operations.");
    }

    return skip;
}

uint32_t CoreChecks::CalcTotalShaderGroupCount(const PIPELINE_STATE &pipeline) const {
    uint32_t total = 0;
    if (pipeline.GetCreateInfoSType() == VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR) {
        const auto &create_info = pipeline.GetCreateInfo<VkRayTracingPipelineCreateInfoKHR>();
        total = create_info.groupCount;

        if (create_info.pLibraryInfo) {
            for (uint32_t i = 0; i < create_info.pLibraryInfo->libraryCount; ++i) {
                auto library_pipeline_state = Get<PIPELINE_STATE>(create_info.pLibraryInfo->pLibraries[i]);
                total += CalcTotalShaderGroupCount(*library_pipeline_state.get());
            }
        }
    } else if (pipeline.GetCreateInfoSType() == VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV) {
        const auto &create_info = pipeline.GetCreateInfo<VkRayTracingPipelineCreateInfoNV>();
        total = create_info.groupCount;

        if (create_info.pLibraryInfo) {
            for (uint32_t i = 0; i < create_info.pLibraryInfo->libraryCount; ++i) {
                auto library_pipeline_state = Get<PIPELINE_STATE>(create_info.pLibraryInfo->pLibraries[i]);
                total += CalcTotalShaderGroupCount(*library_pipeline_state.get());
            }
        }
    }

    return total;
}

bool CoreChecks::PreCallValidateGetRayTracingShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline, uint32_t firstGroup,
                                                                   uint32_t groupCount, size_t dataSize, void *pData,
                                                                   const ErrorObject &error_obj) const {
    bool skip = false;
    auto pPipeline = Get<PIPELINE_STATE>(pipeline);
    if (!pPipeline) {
        return skip;
    }
    const PIPELINE_STATE &pipeline_state = *pPipeline;
    if (pipeline_state.create_flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
        if (!enabled_features.pipeline_library_group_handles_features.pipelineLibraryGroupHandles) {
            skip |= LogError(device, "VUID-vkGetRayTracingShaderGroupHandlesKHR-pipeline-07828",
                             "vkGetRayTracingShaderGroupHandlesKHR: If the pipelineLibraryGroupHandles feature is not enabled, "
                             "pipeline must have not been created with "
                             "VK_PIPELINE_CREATE_LIBRARY_BIT_KHR.");
        }
    }
    if (dataSize < (phys_dev_ext_props.ray_tracing_props_khr.shaderGroupHandleSize * groupCount)) {
        skip |= LogError(device, "VUID-vkGetRayTracingShaderGroupHandlesKHR-dataSize-02420",
                         "vkGetRayTracingShaderGroupHandlesKHR: dataSize (%zu) must be at least "
                         "VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleSize * groupCount.",
                         dataSize);
    }

    uint32_t total_group_count = CalcTotalShaderGroupCount(pipeline_state);

    if (firstGroup >= total_group_count) {
        skip |=
            LogError(device, "VUID-vkGetRayTracingShaderGroupHandlesKHR-firstGroup-04050",
                     "vkGetRayTracingShaderGroupHandlesKHR: firstGroup must be less than the number of shader groups in pipeline.");
    }
    if ((firstGroup + groupCount) > total_group_count) {
        skip |= LogError(
            device, "VUID-vkGetRayTracingShaderGroupHandlesKHR-firstGroup-02419",
            "vkGetRayTracingShaderGroupHandlesKHR: The sum of firstGroup and groupCount must be less than or equal the number "
            "of shader groups in pipeline.");
    }
    return skip;
}

bool CoreChecks::PreCallValidateGetRayTracingCaptureReplayShaderGroupHandlesKHR(VkDevice device, VkPipeline pipeline,
                                                                                uint32_t firstGroup, uint32_t groupCount,
                                                                                size_t dataSize, void *pData,
                                                                                const ErrorObject &error_obj) const {
    bool skip = false;
    if (dataSize < (phys_dev_ext_props.ray_tracing_props_khr.shaderGroupHandleCaptureReplaySize * groupCount)) {
        skip |= LogError(device, "VUID-vkGetRayTracingCaptureReplayShaderGroupHandlesKHR-dataSize-03484",
                         "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR: dataSize (%zu) must be at least "
                         "VkPhysicalDeviceRayTracingPipelinePropertiesKHR::shaderGroupHandleCaptureReplaySize * groupCount.",
                         dataSize);
    }
    auto pipeline_state = Get<PIPELINE_STATE>(pipeline);
    if (!pipeline_state) {
        return skip;
    }
    const auto &create_info = pipeline_state->GetCreateInfo<VkRayTracingPipelineCreateInfoKHR>();
    if (create_info.flags & VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
        if (!enabled_features.pipeline_library_group_handles_features.pipelineLibraryGroupHandles) {
            skip |= LogError(
                device, "VUID-vkGetRayTracingCaptureReplayShaderGroupHandlesKHR-pipeline-07829",
                "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR: If the pipelineLibraryGroupHandles feature is not enabled, "
                "pipeline must have not been created with "
                "VK_PIPELINE_CREATE_LIBRARY_BIT_KHR.");
        }
    }
    if (firstGroup >= create_info.groupCount) {
        skip |= LogError(device, "VUID-vkGetRayTracingCaptureReplayShaderGroupHandlesKHR-firstGroup-04051",
                         "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR: firstGroup (%" PRIu32
                         ") must be less than the number of shader "
                         "groups in pipeline (%" PRIu32 ").",
                         firstGroup, create_info.groupCount);
    }
    if ((firstGroup + groupCount) > create_info.groupCount) {
        skip |= LogError(device, "VUID-vkGetRayTracingCaptureReplayShaderGroupHandlesKHR-firstGroup-03483",
                         "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR: The sum of firstGroup (%" PRIu32
                         ") and groupCount (%" PRIu32
                         ") must be less than or equal to the number of shader groups in pipeline (%" PRIu32 ").",
                         firstGroup, groupCount, create_info.groupCount);
    }
    if (!(create_info.flags & VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR)) {
        skip |= LogError(device, "VUID-vkGetRayTracingCaptureReplayShaderGroupHandlesKHR-pipeline-03607",
                         "pipeline must have been created with a flags that included "
                         "VK_PIPELINE_CREATE_RAY_TRACING_SHADER_GROUP_HANDLE_CAPTURE_REPLAY_BIT_KHR.");
    }
    return skip;
}

bool CoreChecks::PreCallValidateCmdSetRayTracingPipelineStackSizeKHR(VkCommandBuffer commandBuffer, uint32_t pipelineStackSize,
                                                                     const ErrorObject &error_obj) const {
    auto cb_state = GetRead<CMD_BUFFER_STATE>(commandBuffer);
    return ValidateExtendedDynamicState(*cb_state, error_obj.location, VK_TRUE, nullptr, nullptr);
}

bool CoreChecks::PreCallValidateGetRayTracingShaderGroupStackSizeKHR(VkDevice device, VkPipeline pipeline, uint32_t group,
                                                                     VkShaderGroupShaderKHR groupShader,
                                                                     const ErrorObject &error_obj) const {
    bool skip = false;
    auto pipeline_state = Get<PIPELINE_STATE>(pipeline);
    if (pipeline_state) {
        if (pipeline_state->pipeline_type != VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR) {
            skip |=
                LogError(device, "VUID-vkGetRayTracingShaderGroupStackSizeKHR-pipeline-04622",
                         "vkGetRayTracingShaderGroupStackSizeKHR: Pipeline must be a ray-tracing pipeline, but is a %s pipeline.",
                         string_VkPipelineBindPoint(pipeline_state->pipeline_type));
        } else if (group >= pipeline_state->GetCreateInfo<VkRayTracingPipelineCreateInfoKHR>().groupCount) {
            skip |=
                LogError(device, "VUID-vkGetRayTracingShaderGroupStackSizeKHR-group-03608",
                         "vkGetRayTracingShaderGroupStackSizeKHR: The value of group must be less than the number of shader groups "
                         "in pipeline.");
        }
    }
    return skip;
}

bool CoreChecks::ValidateGeometryTrianglesNV(const VkGeometryTrianglesNV &triangles, const char *func_name) const {
    bool skip = false;

    auto vb_state = Get<BUFFER_STATE>(triangles.vertexData);
    if (vb_state != nullptr && vb_state->createInfo.size <= triangles.vertexOffset) {
        skip |= LogError(device, "VUID-VkGeometryTrianglesNV-vertexOffset-02428", "%s", func_name);
    }

    auto ib_state = Get<BUFFER_STATE>(triangles.indexData);
    if (ib_state != nullptr && ib_state->createInfo.size <= triangles.indexOffset) {
        skip |= LogError(device, "VUID-VkGeometryTrianglesNV-indexOffset-02431", "%s", func_name);
    }

    auto td_state = Get<BUFFER_STATE>(triangles.transformData);
    if (td_state != nullptr && td_state->createInfo.size <= triangles.transformOffset) {
        skip |= LogError(device, "VUID-VkGeometryTrianglesNV-transformOffset-02437", "%s", func_name);
    }

    return skip;
}

bool CoreChecks::ValidateGeometryAABBNV(const VkGeometryAABBNV &aabbs, const char *func_name) const {
    bool skip = false;

    auto aabb_state = Get<BUFFER_STATE>(aabbs.aabbData);
    if (aabb_state != nullptr && aabb_state->createInfo.size > 0 && aabb_state->createInfo.size <= aabbs.offset) {
        skip |= LogError(device, "VUID-VkGeometryAABBNV-offset-02439", "%s", func_name);
    }

    return skip;
}

bool CoreChecks::ValidateGeometryNV(const VkGeometryNV &geometry, const char *func_name) const {
    bool skip = false;
    if (geometry.geometryType == VK_GEOMETRY_TYPE_TRIANGLES_NV) {
        skip = ValidateGeometryTrianglesNV(geometry.geometry.triangles, func_name);
    } else if (geometry.geometryType == VK_GEOMETRY_TYPE_AABBS_NV) {
        skip = ValidateGeometryAABBNV(geometry.geometry.aabbs, func_name);
    }
    return skip;
}

bool CoreChecks::ValidateRaytracingShaderBindingTable(VkCommandBuffer commandBuffer, const Location &table_loc,
                                                      const char *vuid_single_device_memory, const char *vuid_binding_table_flag,
                                                      const VkStridedDeviceAddressRegionKHR &binding_table) const {
    bool skip = false;

    if (binding_table.deviceAddress == 0 || binding_table.size == 0) {
        return skip;
    }

    const auto buffer_states = GetBuffersByAddress(binding_table.deviceAddress);
    if (buffer_states.empty()) {
        skip |= LogError("VUID-VkStridedDeviceAddressRegionKHR-size-04631", commandBuffer, table_loc.dot(Field::deviceAddress),
                         "(0x%" PRIx64 ") has no buffer associated with it.", binding_table.deviceAddress);
    } else {
        const sparse_container::range<VkDeviceSize> requested_range(binding_table.deviceAddress,
                                                                    binding_table.deviceAddress + binding_table.size - 1);
        using BUFFER_STATE_PTR = ValidationStateTracker::BUFFER_STATE_PTR;
        BufferAddressValidation<4> buffer_address_validator = {{{
            {vuid_single_device_memory, LogObjectList(commandBuffer),
             [this, commandBuffer, table_loc, vuid_single_device_memory](const BUFFER_STATE_PTR &buffer_state,
                                                                         std::string *out_error_msg) {
                 if (!out_error_msg) {
                     return !buffer_state->sparse && buffer_state->IsMemoryBound();
                 } else {
                     return ValidateMemoryIsBoundToBuffer(commandBuffer, *buffer_state, table_loc.StringFunc(),
                                                          vuid_single_device_memory);
                 }
             }},

            {vuid_binding_table_flag, LogObjectList(commandBuffer),
             [](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                 if (!(static_cast<uint32_t>(buffer_state->usage) & VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR)) {
                     if (out_error_msg) {
                         *out_error_msg += "buffer has usage " + string_VkBufferUsageFlags2KHR(buffer_state->usage) + '\n';
                     }
                     return false;
                 }
                 return true;
             },
             []() {
                 return "The following buffers have not been created with the VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR "
                        "usage flag:\n";
             }},

            {"VUID-VkStridedDeviceAddressRegionKHR-size-04631", LogObjectList(commandBuffer),
             [&requested_range](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                 const auto buffer_address_range = buffer_state->DeviceAddressRange();
                 if (!buffer_address_range.includes(requested_range)) {
                     if (out_error_msg) {
                         const std::string buffer_address_range_string = string_range_hex(buffer_address_range);
                         *out_error_msg += "buffer device address range is " + buffer_address_range_string + '\n';
                     }
                     return false;
                 }

                 return true;
             },
             [table_loc, requested_range_string = string_range_hex(requested_range)]() {
                 return "The following buffers do not include " + table_loc.Fields() + " buffer device address range " +
                        requested_range_string + ":\n";
             }},

            {"VUID-VkStridedDeviceAddressRegionKHR-size-04632", LogObjectList(commandBuffer),
             [&binding_table](const BUFFER_STATE_PTR &buffer_state, std::string *out_error_msg) {
                 if (binding_table.stride > buffer_state->createInfo.size) {
                     if (out_error_msg) {
                         *out_error_msg += "buffer size is " + std::to_string(buffer_state->createInfo.size) + '\n';
                     }
                     return false;
                 }
                 return true;
             },
             [table_loc, &binding_table]() {
                 return "The following buffers have a size inferior to " + table_loc.Fields() + "->stride (" +
                        std::to_string(binding_table.stride) + "):\n";
                 ;
             }},
        }}};

        skip |= buffer_address_validator.LogErrorsIfNoValidBuffer(*this, buffer_states, table_loc.StringFunc(),
                                                                  table_loc.dot(Field::deviceAddress).Fields(),
                                                                  binding_table.deviceAddress);
    }

    return skip;
}
