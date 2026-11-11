/* xr_glslang_link_test: a C program against libxr_glslang.so - compiles one Vulkan fragment shader to SPIR-V.
 * Proves the C-only link (no C++ runtime on the consumer side); runs on a device or an arm64 Android system. */
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <stdio.h>

int main(void)
{
    const char *src = "#version 450\nlayout(location=0) out vec4 o;\nvoid main(){ o = vec4(1.0); }\n";
    glslang_input_t in = {0};
    glslang_shader_t *sh;
    glslang_program_t *prog;
    int ok;

    glslang_initialize_process();
    in.language = GLSLANG_SOURCE_GLSL; in.stage = GLSLANG_STAGE_FRAGMENT; in.client = GLSLANG_CLIENT_VULKAN;
    in.client_version = GLSLANG_TARGET_VULKAN_1_3; in.target_language = GLSLANG_TARGET_SPV;
    in.target_language_version = GLSLANG_TARGET_SPV_1_6; in.code = src; in.default_version = 450;
    in.default_profile = GLSLANG_NO_PROFILE; in.messages = GLSLANG_MSG_DEFAULT_BIT;
    in.resource = glslang_default_resource();
    sh = glslang_shader_create(&in);
    ok = glslang_shader_preprocess(sh, &in) && glslang_shader_parse(sh, &in);
    if (ok) {
        prog = glslang_program_create();
        glslang_program_add_shader(prog, sh);
        ok = glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT);
        if (ok) {
            glslang_program_SPIRV_generate(prog, GLSLANG_STAGE_FRAGMENT);
            ok = glslang_program_SPIRV_get_size(prog) > 5;
            printf("SPIR-V: %zu words\n", glslang_program_SPIRV_get_size(prog));
        }
        glslang_program_delete(prog);
    }
    printf("libxr_glslang: %s\n", ok ? "compiles a shader to SPIR-V" : glslang_shader_get_info_log(sh));
    glslang_shader_delete(sh);
    glslang_finalize_process();
    return ok ? 0 : 1;
}
