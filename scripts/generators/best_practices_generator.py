#!/usr/bin/python3 -i
#
# Copyright (c) 2015-2023 The Khronos Group Inc.
# Copyright (c) 2015-2023 Valve Corporation
# Copyright (c) 2015-2023 LunarG, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import os
from generators.base_generator import BaseGenerator

# If there is another success code other than VK_SUCCESS
def hasNonVkSuccess(successCodes: list[str]) -> bool:
    if successCodes is None or len(successCodes) == 0:
        return False
    return len(successCodes) > 1 or 'VK_SUCCESS' not in successCodes

class BestPracticesOutputGenerator(BaseGenerator):
    def __init__(self):
        BaseGenerator.__init__(self)

        # Commands which are not autogenerated but still intercepted
        # More are added from a first pass over the functions
        self.no_autogen_list = [
            'vkEnumerateInstanceVersion',
            'vkCreateValidationCacheEXT',
            'vkDestroyValidationCacheEXT',
            'vkMergeValidationCachesEXT',
            'vkGetValidationCacheDataEXT',
        ]

        # Commands that require an extra parameter for state sharing between validate/record steps
        self.extra_parameter_list = [
            "vkCreateShaderModule",
            "vkCreateShadersEXT",
            "vkCreateGraphicsPipelines",
            "vkCreateComputePipelines",
            "vkAllocateDescriptorSets",
            "vkCreateRayTracingPipelinesNV",
            "vkCreateRayTracingPipelinesKHR",
        ]
        # Commands that have a manually written post-call-record step which needs to be called from the autogen'd fcn
        self.manual_postcallrecord_list = [
            'vkAllocateDescriptorSets',
            'vkQueuePresentKHR',
            'vkQueueBindSparse',
            'vkCreateGraphicsPipelines',
            'vkGetPhysicalDeviceSurfaceCapabilitiesKHR',
            'vkGetPhysicalDeviceSurfaceCapabilities2KHR',
            'vkGetPhysicalDeviceSurfaceCapabilities2EXT',
            'vkGetPhysicalDeviceSurfacePresentModesKHR',
            'vkGetPhysicalDeviceSurfaceFormatsKHR',
            'vkGetPhysicalDeviceSurfaceFormats2KHR',
            'vkGetPhysicalDeviceDisplayPlanePropertiesKHR',
            'vkGetSwapchainImagesKHR',
            # AMD tracked
            'vkCreateComputePipelines',
            'vkCmdPipelineBarrier',
            'vkQueueSubmit',
        ]

        self.extension_info = dict()

    def generate(self):
        self.write(f'''// *** THIS FILE IS GENERATED - DO NOT EDIT ***
// See {os.path.basename(__file__)} for modifications

/***************************************************************************
*
* Copyright (c) 2015-2023 The Khronos Group Inc.
* Copyright (c) 2015-2023 Valve Corporation
* Copyright (c) 2015-2023 LunarG, Inc.
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
****************************************************************************/\n''')
        self.write('// NOLINTBEGIN') # Wrap for clang-tidy to ignore

        # Build additional pass of functions to ignore
        for name, command in self.vk.commands.items():
            if command.returnType != 'VkResult':
                self.no_autogen_list.append(name)
            # This is just to remove un-used commands from be generated
            # This can be removed if another use for these commands are needed
            if (command.errorCodes is None and not hasNonVkSuccess(command.successCodes)):
                self.no_autogen_list.append(name)

        if self.filename == 'best_practices.h':
            self.generateHeader()
        elif self.filename == 'best_practices.cpp':
            self.generateSource()
        else:
            self.write(f'\nFile name {self.filename} has no code to generate\n')

        self.write('// NOLINTEND') # Wrap for clang-tidy to ignore

    def generateHeader(self):
        out = []
        out.append('''
#pragma once
#include <vulkan/vulkan_core.h>
#include "containers/custom_containers.h"
#include "error_message/record_object.h"
''')
        # List all Function declarations
        for command in [x for x in self.vk.commands.values() if x.name not in self.no_autogen_list]:
            out.extend([f'#ifdef {command.protect}\n'] if command.protect else [])
            prototype = command.cPrototype.split("VKAPI_CALL ")[1]
            prototype = f'void PostCallRecord{prototype[2:]}'
            prototype = prototype.replace(');', ',\n    const RecordObject&                         record_obj) {\n')
            prototype = prototype.replace(') {', ') override;\n')
            if command.name in self.extra_parameter_list:
                prototype = prototype.replace(')', ',\n    void*                                       state_data)')
            out.append(prototype)
            out.extend([f'#endif // {command.protect}\n'] if command.protect else [])

        # Create deprecated extension map
        out.append('const vvl::unordered_map<std::string, DeprecationData>  deprecated_extensions = {\n')
        for extension in self.vk.extensions.values():
            target = None
            reason = None
            if extension.promotedTo is not None:
                reason = 'kExtPromoted'
                target = extension.promotedTo
            elif extension.obsoletedBy is not None:
                reason = 'kExtObsoleted'
                target = extension.obsoletedBy
            elif extension.deprecatedBy is not None:
                reason = 'kExtDeprecated'
                target = extension.deprecatedBy
            else:
                continue
            out.append(f'    {{"{extension.name}", {{{reason}, "{target}"}}}},\n')
        out.append('};\n')

        out.append('const vvl::unordered_map<std::string, std::string> special_use_extensions = {\n')
        for extension in self.vk.extensions.values():
            if extension.specialUse is not None:
                out.append(f'    {{"{extension.name}", "{", ".join(extension.specialUse)}"}},\n')
        out.append('};\n')
        self.write("".join(out))

    def generateSource(self):
        out = []
        out.append('''
#include "chassis.h"
#include "best_practices/best_practices_validation.h"
''')
        for command in [x for x in self.vk.commands.values() if x.name not in self.no_autogen_list]:
            paramList = [param.name for param in command.params]
            paramList.append('record_obj')
            if command.name in self.extra_parameter_list:
                paramList.append('state_data')
            params = ', '.join(paramList)

            out.append('\n')
            out.extend([f'#ifdef {command.protect}\n'] if command.protect else [])
            prototype = command.cPrototype.split("VKAPI_CALL ")[1]
            prototype = f'void BestPractices::PostCallRecord{prototype[2:]}'
            prototype = prototype.replace(');', ',\n    const RecordObject&                         record_obj) {\n')
            if command.name in self.extra_parameter_list:
                prototype = prototype.replace(')', ',\n    void*                                       state_data)')
            out.append(prototype)

            out.append(f'    ValidationStateTracker::PostCallRecord{command.name[2:]}({params});\n')
            if command.name in self.manual_postcallrecord_list:
                out.append(f'    ManualPostCallRecord{command.name[2:]}({params});\n')

            if hasNonVkSuccess(command.successCodes):
                out.append('    if (record_obj.result > VK_SUCCESS) {\n')
                out.append('        LogPositiveSuccessCode(record_obj);\n')
                out.append('        return;\n')
                out.append('    }\n')

            if command.errorCodes is not None:
                out.append('    if (record_obj.result < VK_SUCCESS) {\n')
                out.append('        LogErrorCode(record_obj);\n')
                out.append('    }\n')

            out.append('}\n')
            out.extend([f'#endif // {command.protect}\n'] if command.protect else [])
        self.write(''.join(out))
