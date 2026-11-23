/*
 * gl_psh.c -- NV2A texture shader + register combiners as a GLSL fragment
 * shader.
 *
 * The register values are the XDK's pixel-shader tokens, which D3D writes to
 * the combiner registers unchanged, so the encoding is the XDK's:
 *
 *   input byte (ICW, final CW0/CW1): reg[3:0] | ALPHA 0x10 | mapping[7:5]
 *   colour/alpha OCW:  CD dst[3:0] AB dst[7:4] SUM dst[11:8]
 *                      CD_DOT 12, AB_DOT 13, MUX 14, scale/bias [17:15],
 *                      CD_BLUE_TO_ALPHA 18, AB_BLUE_TO_ALPHA 19
 *   CONTROL:           stage count [7:0], MUX_MSB 8, UNIQUE_C0 12, UNIQUE_C1 16
 *   final CW1:         E, F, G bytes; CLAMP_SUM 0x80, COMPLEMENT_V1 0x40,
 *                      COMPLEMENT_R0 0x20 in the low byte
 *   SHADER_STAGE_PROGRAM: 5 bits per texture stage (PS_TEXTUREMODES)
 *
 * Registers: 0 zero, 1 C0, 2 C1, 3 fog, 4 V0 (diffuse), 5 V1 (specular),
 * 8-11 T0-T3, 12 R0, 13 R1, and for the final combiner only 14 V1R0_SUM and
 * 15 EF_PROD. R0.a starts as T0.a.
 *
 * ponytail: the bump-map and dot-product texture modes (DOT_ST and friends)
 * sample as plain 2D, and cube maps read black on GL (Vulkan samples them);
 * the dependent AR/GB lookups are done. They are the
 * next thing to add when a title's surfaces need them.
 */
#include "gl_psh.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char  *p;
    size_t left;
    int    overflow;
} Out;

static void emit(Out *o, const char *fmt, ...)
{
    va_list ap;
    int n;

    if (o->overflow)
        return;
    va_start(ap, fmt);
    n = vsnprintf(o->p, o->left, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= o->left) {
        o->overflow = 1;
        return;
    }
    o->p += n;
    o->left -= (size_t)n;
}

static const char *reg_name(uint32_t reg, int stage, const Nv2aPshKey *k,
                            char *buf, size_t n)
{
    switch (reg) {
    case 1:
        snprintf(buf, n, "u_c0[%d]", (k->control & (1u << 12)) ? stage : 0);
        return buf;
    case 2:
        snprintf(buf, n, "u_c1[%d]", (k->control & (1u << 16)) ? stage : 0);
        return buf;
    case 3:  return "FOG";
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    case 14: return "V1R0";
    case 15: return "EF";
    default: return "ZERO";
    }
}

/* One general-combiner input, mapped. `alpha_portion` selects the
 * float-valued (alpha) path, where the RGB channel bit means blue. */
static void input(Out *o, uint32_t b, int stage, int alpha_portion,
                  const Nv2aPshKey *k)
{
    char nb[16];
    /* V1R0_SUM and EF_PROD exist only in the final combiner. */
    const char *r = (b & 0xF) >= 14 ? "ZERO"
                                    : reg_name(b & 0xF, stage, k, nb, sizeof nb);
    const char *x;
    char val[40];
    int use_alpha = (b >> 4) & 1;
    uint32_t map = (b >> 5) & 7;

    if (alpha_portion)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    x = val;

    switch (map) {
    case 0: emit(o, "max(%s, 0.0)", x); break;                       /* unsigned */
    case 1: emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", x); break;        /* unsigned invert */
    case 2: emit(o, "(2.0 * max(%s, 0.0) - 1.0)", x); break;         /* expand normal */
    case 3: emit(o, "(1.0 - 2.0 * max(%s, 0.0))", x); break;         /* expand negate */
    case 4: emit(o, "(max(%s, 0.0) - 0.5)", x); break;               /* half-bias normal */
    case 5: emit(o, "(0.5 - max(%s, 0.0))", x); break;               /* half-bias negate */
    case 6: emit(o, "(%s)", x); break;                               /* signed identity */
    default: emit(o, "(-(%s))", x); break;                           /* signed negate */
    }
}

static const char *dst_name(uint32_t reg)
{
    switch (reg) {
    case 4:  return "V0";
    case 5:  return "V1";
    case 8:  return "T0";
    case 9:  return "T1";
    case 10: return "T2";
    case 11: return "T3";
    case 12: return "R0";
    case 13: return "R1";
    default: return NULL;           /* 0 discards; others are not writable */
    }
}

static const char *scale_bias(uint32_t op)
{
    switch (op) {
    case 1:  return "(%s - 0.5)";
    case 2:  return "(%s * 2.0)";
    case 3:  return "((%s - 0.5) * 2.0)";
    case 4:  return "(%s * 4.0)";
    case 6:  return "(%s * 0.5)";
    default: return "(%s)";
    }
}

static void stage(Out *o, int i, const Nv2aPshKey *k)
{
    uint32_t cicw = k->color_icw[i], cocw = k->color_ocw[i];
    uint32_t aicw = k->alpha_icw[i], aocw = k->alpha_ocw[i];
    const char *d;
    char e[64];

    emit(o, "    { /* stage %d */\n", i);
    /* Colour inputs */
    emit(o, "        vec3 cA = "); input(o, cicw >> 24, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cB = "); input(o, cicw >> 16, i, 0, k); emit(o, ";\n");
    emit(o, "        vec3 cC = "); input(o, cicw >> 8, i, 0, k);  emit(o, ";\n");
    emit(o, "        vec3 cD = "); input(o, cicw, i, 0, k);       emit(o, ";\n");
    /* Alpha inputs */
    emit(o, "        float aA = "); input(o, aicw >> 24, i, 1, k); emit(o, ";\n");
    emit(o, "        float aB = "); input(o, aicw >> 16, i, 1, k); emit(o, ";\n");
    emit(o, "        float aC = "); input(o, aicw >> 8, i, 1, k);  emit(o, ";\n");
    emit(o, "        float aD = "); input(o, aicw, i, 1, k);       emit(o, ";\n");

    emit(o, "        vec3 cAB = %s;\n", (cocw & (1u << 13)) ? "vec3(dot(cA, cB))" : "cA * cB");
    emit(o, "        vec3 cCD = %s;\n", (cocw & (1u << 12)) ? "vec3(dot(cC, cD))" : "cC * cD");
    if (cocw & (1u << 14))
        emit(o, "        vec3 cS = %s ? cCD : cAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        vec3 cS = cAB + cCD;\n");
    emit(o, "        float aAB = aA * aB, aCD = aC * aD;\n");
    if (aocw & (1u << 14))
        emit(o, "        float aS = %s ? aCD : aAB;\n",
             (k->control & (1u << 8)) ? "(R0.a >= 0.5)"
                                      : "((int(R0.a * 255.0 + 0.5) & 1) != 0)");
    else
        emit(o, "        float aS = aAB + aCD;\n");

    /* Scale/bias, clamp to the signed range, write. */
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cAB");
    emit(o, "        cAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cCD");
    emit(o, "        cCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((cocw >> 15) & 7), "cS");
    emit(o, "        cS = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aAB");
    emit(o, "        aAB = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aCD");
    emit(o, "        aCD = clamp(%s, -1.0, 1.0);\n", e);
    snprintf(e, sizeof e, scale_bias((aocw >> 15) & 7), "aS");
    emit(o, "        aS = clamp(%s, -1.0, 1.0);\n", e);

    if ((d = dst_name((cocw >> 4) & 0xF)) != NULL) emit(o, "        %s.rgb = cAB;\n", d);
    if ((d = dst_name(cocw & 0xF)) != NULL)        emit(o, "        %s.rgb = cCD;\n", d);
    if ((d = dst_name((cocw >> 8) & 0xF)) != NULL) emit(o, "        %s.rgb = cS;\n", d);
    if ((d = dst_name((aocw >> 4) & 0xF)) != NULL) emit(o, "        %s.a = aAB;\n", d);
    if ((d = dst_name(aocw & 0xF)) != NULL)        emit(o, "        %s.a = aCD;\n", d);
    if ((d = dst_name((aocw >> 8) & 0xF)) != NULL) emit(o, "        %s.a = aS;\n", d);
    /* Blue-to-alpha replaces the alpha written to the AB / CD destination. */
    if ((cocw & (1u << 19)) && (d = dst_name((cocw >> 4) & 0xF)) != NULL)
        emit(o, "        %s.a = cAB.b;\n", d);
    if ((cocw & (1u << 18)) && (d = dst_name(cocw & 0xF)) != NULL)
        emit(o, "        %s.a = cCD.b;\n", d);
    emit(o, "    }\n");
}

/* A final-combiner input: clamped to [0,1], optionally inverted. Its C0 and
 * C1 are its own (SPECULAR_FOG_FACTOR0/1), not a general stage's: read as
 * stage 0's, NFSU2's colour grading added its red channel mask to the whole
 * picture. */
static void final_input(Out *o, uint32_t b, int alpha_only, const Nv2aPshKey *k)
{
    char nb[16];
    const char *r = (b & 0xF) == 1 ? "u_fc0"
                  : (b & 0xF) == 2 ? "u_fc1"
                  : reg_name(b & 0xF, 0, k, nb, sizeof nb);
    int use_alpha = (b >> 4) & 1;
    int invert = (b >> 5) & 1;
    char val[40];

    if (alpha_only)
        snprintf(val, sizeof val, "%s.%s", r, use_alpha ? "a" : "b");
    else if (use_alpha)
        snprintf(val, sizeof val, "vec3(%s.a)", r);
    else
        snprintf(val, sizeof val, "%s.rgb", r);
    if (invert)
        emit(o, "(1.0 - clamp(%s, 0.0, 1.0))", val);
    else
        emit(o, "clamp(%s, 0.0, 1.0)", val);
}

/* The earlier stage a dependent mode reads (SHADER_OTHER_STAGE_INPUT):
 * stage 1 always reads T0; stage 2 selects with bits [19:16], stage 3 with
 * [23:20]. */
static int input_stage(const Nv2aPshKey *k, int t)
{
    int s = t <= 1 ? 0 : (int)((k->other_input >> (t == 2 ? 16 : 20)) & 0xF);
    return s < t ? s : t - 1;
}

static void tex_fetch(Out *o, int t, uint32_t mode, const Nv2aPshKey *k)
{
    /* A shadow map (nv2a_vk binds the depth buffer drawn at the texture's address): the stored depth, in the
     * texture format's integer range, compared with the texcoord's r / q - 1 where the comparison holds, else 0,
     * in every channel. A 2D-projective stage compares with 0. The hardware's behaviour as xemu documents it
     * (pgraph/glsl/psh.c, psh_append_shadowmap); written here, not copied. Float depth formats are taken as
     * fixed. */
    if ((k->shadow >> t) & 1u) {
        static const char *const op[8] = { "", "<", "==", "<=", ">", "!=", ">=", "" };
        uint32_t func = (k->shadow >> 8) & 7u;
        if (func == 0 || func == 7) {
            emit(o, "    vec4 T%d = vec4(%s);\n", t, func == 7 ? "1.0" : "0.0");
            return;
        }
        emit(o, "    vec4 T%d;\n"
                "    {\n"
                "        float q%d = abs(vT%d.w) > 1e-8 ? vT%d.w : 1.0;\n"
                "        float d%d = texture(t%d, vT%d.xy / q%d * u_tscale[%d]).r;\n",
             t, t, t, t, t, t, t, t, t);
        if (mode == 2) {
            const char *mx = (k->shadow >> (4 + t)) & 1u ? "65535.0" : "16777215.0";
            emit(o, "        float r%d = clamp(vT%d.z / q%d, 0.0, %s);\n"
                    "        T%d = vec4(d%d * %s %s r%d ? 1.0 : 0.0);\n",
                 t, t, t, mx, t, t, mx, op[func], t);
        } else {
            emit(o, "        T%d = vec4(d%d %s 0.0 ? 1.0 : 0.0);\n", t, t, op[func]);
        }
        emit(o, "    }\n");
        return;
    }
    switch (mode) {
    case 15:                                         /* DEPENDENT_AR */
    case 16:                                         /* DEPENDENT_GB */
        /* A lookup indexed by an earlier stage's colour -- NFSU2's colour
         * grading does red through one and green/blue through the other.
         * The coordinates are already normalised. Sampled at the stage's own
         * texcoords, the green/blue lookup read black and races were red. */
        if (t == 0) {
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
            break;
        }
        emit(o, "    vec4 T%d = texture(t%d, T%d.%s);\n", t, t, input_stage(k, t),
             mode == 15 ? "ar" : "gb");
        break;
    case 6:                                          /* BUMPENVMAP */
    case 7:                                          /* BUMPENVMAP_LUM */
        /* An earlier stage's texture holds signed offsets (du in blue, dv in green, luminance in red: X8L8V8U8),
         * turned by the stage's 2x2 bump matrix and added to this stage's coordinates before its lookup; _LUM also
         * scales the result by the luminance (bump scale / offset). The bytes are signed: they are converted per
         * texel and only then filtered (bilinear by hand, repeat wrap) - converting after the sampler's filter turns
         * every +127 / -128 edge into a wrong step, coarse blots instead of running water (Panzer Dragoon Orta's
         * Pandora's Box background and river; xemu notes the same loss as a FIXME). Behaviour after xemu
         * (pgraph/glsl/psh.c BUMPENVMAP, the register order in pgraph.c); written here. option-c, 2026-11-08. */
        if (t == 0) {
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
            break;
        }
        {
            /* RECOMP_PSH_NO_BUMP=1: the old plain 2D lookup - for side-by-side comparisons of the same frame */
            static int nob = -1;
            if (nob < 0) { const char *e = getenv("RECOMP_PSH_NO_BUMP"); nob = e && *e == '1'; }
            if (nob) {
                emit(o, "    vec4 T%d = texture(t%d, vT%d.xy * u_tscale[%d]);\n", t, t, t, t);
                break;
            }
        }
        {
            int s = input_stage(k, t);
            emit(o, "    vec4 T%d;\n"
                    "    {\n"
                    "        vec3 b3 = nv2a_bump_fetch(t%d, vT%d.xy * u_tscale[%d]);\n"
                    "        vec2 dsdt = mat2(u_bumpmat[%d]) * b3.xy;\n"
                    "        T%d = texture(t%d, (vT%d.xy + dsdt) * u_tscale[%d]);\n",
                 t, s, s, s, t, t, t, t, t);
            if (mode == 7)
                emit(o, "        T%d *= u_bumpls[%d].x * b3.z + u_bumpls[%d].y;\n", t, t, t);
            emit(o, "    }\n");
        }
        break;
    case 0:                                          /* NONE */
        emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    case 4:                                          /* PASS_THROUGH */
        emit(o, "    vec4 T%d = clamp(vT%d, 0.0, 1.0);\n", t, t);
        break;
    case 5:                                          /* CLIP_PLANE */
        emit(o, "    if (any(lessThan(vT%d, vec4(0.0)))) discard;\n"
                "    vec4 T%d = vec4(0.0);\n", t, t);
        break;
    case 3:                                          /* CUBE_MAP */
        /* Car reflections. The direction is (s, t, r), not divided by q
         * (xemu does the same). Vulkan only: nv2a_vk binds cube views,
         * nv2a_gl does not, so GL still reads black. */
        if (nv2a_shader_vk)
            emit(o, "    vec4 T%d = texture(t%d, vT%d.xyz);\n", t, t, t);
        else
            emit(o, "    vec4 T%d = vec4(0.0);\n", t);
        break;
    default: {                                       /* 2D (+ the rest, for now) */
        if (mode != 1 && mode < 32) {
            /* bump-map / dot-product modes (6..14, 17, 18) sampled as plain 2D: say so once per mode, so a play
             * log shows WHERE a title needs them (option-c, 2026-11-08: missing-effects watch) */
            static uint32_t seen;
            static const char *const nm[32] = { [6] = "BUMPENVMAP", [7] = "BUMPENVMAP_LUM", [8] = "BRDF",
                [9] = "DOT_ST", [10] = "DOT_ZW", [11] = "DOT_RFLCT_DIFF", [12] = "DOT_RFLCT_SPEC",
                [13] = "DOT_STR_3D", [14] = "DOT_STR_CUBE", [17] = "DOT_PRODUCT", [18] = "DOT_RFLCT_SPEC_CONST" };
            if (!(seen & (1u << mode))) {
                seen |= 1u << mode;
                fprintf(stderr, "  [PSH] texture mode %u (%s) on stage %d drawn as plain 2D - first seen\n", mode,
                        nm[mode] ? nm[mode] : "?", t);
            }
        }
        emit(o, "    vec2 tc%d = (abs(vT%d.w) > 1e-8 && %d == 1) ? vT%d.xy / vT%d.w : vT%d.xy;\n",
             t, t, mode == 1 ? 1 : 0, t, t, t);
        emit(o, "    vec4 T%d = texture(t%d, tc%d * u_tscale[%d]);\n", t, t, t, t);
        break;
    }
    }
}

/* The fog unit (xemu's vsh.c fog modes), here per pixel on the interpolated
 * fog coordinate. u_fogp: see nv2a_fog_params. */
#define NV2A_FOG_GLSL \
    "float nv2a_fog(float d) {\n" \
    "    if (u_fogp.w < 0.5) return clamp(d, 0.0, 1.0);\n" \
    "    if (u_fogp.w < 1.5) return 1.0;\n" \
    "    int m = int(u_fogp.z + 0.5);\n" \
    "    float f;\n" \
    "    if (isinf(d)) d = 0.0;\n" \
    "    if (m == 0 || m == 3) f = u_fogp.x + d * u_fogp.y - 1.0;\n" \
    "    else if (m == 1 || m == 4) f = u_fogp.x + exp2(d * u_fogp.y * 16.0) - 1.5;\n" \
    "    else f = u_fogp.x + exp2(-d * d * u_fogp.y * u_fogp.y * 32.0) - 1.5;\n" \
    "    if (m >= 3) f = abs(f);\n" \
    "    return clamp(f, 0.0, 1.0);\n" \
    "}\n"

/* BUMPENVMAP's source (tex_fetch case 6 / 7): a signed byte per channel -> -1..1; the bilinear filter by hand after
 * the conversion. Returns (du, dv, luminance 0..1). */
#define NV2A_BUMP_GLSL \
    "float nv2a_s8(float x) {\n" \
    "    x = floor(x * 255.0 + 0.5);\n" \
    "    return x >= 128.0 ? (x - 256.0) / 127.0 : x / 127.0;\n" \
    "}\n" \
    "vec3 nv2a_bump_texel(sampler2D s, ivec2 p, ivec2 sz) {\n" \
    "    vec4 c = texelFetch(s, ((p %% sz) + sz) %% sz, 0);\n" /* %%: this text goes through emit's printf */ \
    "    return vec3(nv2a_s8(c.b), nv2a_s8(c.g), c.r);\n" \
    "}\n" \
    "vec3 nv2a_bump_fetch(sampler2D s, vec2 uv) {\n" \
    "    ivec2 sz = textureSize(s, 0);\n" \
    "    vec2 p = uv * vec2(sz) - 0.5;\n" \
    "    ivec2 i = ivec2(floor(p));\n" \
    "    vec2 f = p - floor(p);\n" \
    "    vec3 a = mix(nv2a_bump_texel(s, i, sz), nv2a_bump_texel(s, i + ivec2(1, 0), sz), f.x);\n" \
    "    vec3 b = mix(nv2a_bump_texel(s, i + ivec2(0, 1), sz), nv2a_bump_texel(s, i + ivec2(1, 1), sz), f.x);\n" \
    "    return mix(a, b, f.y);\n" \
    "}\n"

void nv2a_fog_params(const uint32_t *regs, uint32_t xform, float out[4])
{
    union { uint32_t u; float f; } p0, p1;

    p0.u = regs[0x09C0 / 4];                    /* SET_FOG_PARAMS */
    p1.u = regs[0x09C4 / 4];
    out[0] = p0.f;
    out[1] = p1.f;
    switch (regs[0x029C / 4]) {                 /* SET_FOG_MODE */
    case 0x0800: out[2] = 1.0f; break;          /* EXP */
    case 0x0801: out[2] = 2.0f; break;          /* EXP2 */
    case 0x0802: out[2] = 4.0f; break;          /* EXP_ABS */
    case 0x0803: out[2] = 5.0f; break;          /* EXP2_ABS */
    case 0x0804: out[2] = 3.0f; break;          /* LINEAR_ABS */
    default:     out[2] = 0.0f; break;          /* LINEAR (0x2601) */
    }
    /* Fog off: the factor is 1 whatever the transform mode -- the hardware
     * writes oFog = 1.0 then (xemu hw/xbox/nv2a/pgraph/glsl/vsh.c, behaviour
     * only). The fixed-function path used to get the raw read of vFog, which
     * it never sets (0): every fixed-function draw whose final combiner mixes
     * by FOG.a (D3D's default A = fog.a, B = spare0+specular, C = fog.rgb)
     * came out in the fog colour -- THPS 2X's front end: a grey veil over the
     * menus and button text gone in it.
     * Fog on: the distance is computed per vertex -- a vertex program's
     * oFog.x, or the fixed-function fog source (gl_vsh.c, u_foggen) -- and
     * goes through the fog mode here. */
    (void)xform;
    out[3] = regs[0x02A4 / 4] ? 2.0f : 1.0f;     /* SET_FOG_ENABLE */
}

/* ── Uber fragment shader (SHADER COMPILE: HYBRID, docs/hybrid_ubershader.md) ──────────────────────────────────
 * The same rules as nv2a_gl_psh above, interpreted at run time from the draw's Nv2aPshKey, which nv2a_vk appends to
 * the fragment block as u_pk (38 words in the struct's order: colour ICW 0-7, colour OCW 8-15, alpha ICW 16-23,
 * alpha OCW 24-31, control 32, final0 33, final1 34, shader_program 35, other_input 36, shadow 37). One source, built
 * once. Not covered (the caller keeps such draws off it): CUBE_MAP and shadow-map stages, point sprites. A change to a
 * rule above must be made here too - tests/uber_psh compares both. Vulkan GLSL only. */
#define UBER_PSH_GLSL \
    "#version 450\n" \
    "layout(location = 0) in vec4 vD0; layout(location = 1) in vec4 vD1;\n" \
    "layout(location = 2) in vec4 vT0; layout(location = 3) in vec4 vT1;\n" \
    "layout(location = 4) in vec4 vT2; layout(location = 5) in vec4 vT3in;\n" \
    "layout(location = 6) in float vFog;\n" \
    "layout(set = 0, binding = 2) uniform sampler2D t0;\n" \
    "layout(set = 0, binding = 3) uniform sampler2D t1;\n" \
    "layout(set = 0, binding = 4) uniform sampler2D t2;\n" \
    "layout(set = 0, binding = 5) uniform sampler2D t3;\n" \
    "layout(std140, set = 0, binding = 1) uniform FsU {\n" \
    "    vec2 u_tscale[4];\n" \
    "    vec4 u_c0[8]; vec4 u_c1[8];\n" \
    "    vec4 u_fc0; vec4 u_fc1;\n" \
    "    vec4 u_fogcolor;\n" \
    "    int u_alpha_func; float u_alpha_ref;\n" \
    "    int u_psprite;\n" \
    "    int u_mark;\n" \
    "    vec4 u_fogp;\n" \
    "    vec4 u_bumpmat[4];\n" \
    "    vec4 u_bumpls[4];\n" \
    "    uvec4 u_pk[10];\n" \
    "};\n" \
    "layout(location = 0) out vec4 fragColor;\n" \
    NV2A_FOG_GLSL \
    NV2A_BUMP_GLSL \
    "uint pkw(int w) { return u_pk[w >> 2][w & 3]; }\n" \
    /* The combiner registers as named variables, read / written through switches (2026-11-22): a dynamically   \
     * indexed vec4 R[16] went to local memory on the drivers (forced-uber GPU time ~7x a specialised shader).     \
     * Register numbers: 0 zero, 1 C0, 2 C1, 3 fog, 4 V0, 5 V1, 8-11 T0-T3, 12 R0, 13 R1, 14 V1R0, 15 EF.          \
     * Tf0-Tf2: the stages' fetched values (the dependent fetches read these, not the registers). */               \
    "vec4 Rc0, Rc1, Rfog, Rv0, Rv1, Rt0, Rt1, Rt2, Rt3, Rr0, Rr1, Rv1r0, Ref;\n" \
    "vec4 Tf0, Tf1, Tf2;\n" \
    "vec4 rreg(uint r) {\n" \
    "    switch (r) {\n" \
    "    case 1u: return Rc0; case 2u: return Rc1; case 3u: return Rfog; case 4u: return Rv0; case 5u: return Rv1;\n" \
    "    case 8u: return Rt0; case 9u: return Rt1; case 10u: return Rt2; case 11u: return Rt3;\n" \
    "    case 12u: return Rr0; case 13u: return Rr1; case 14u: return Rv1r0; case 15u: return Ref;\n" \
    "    default: return vec4(0.0);\n" \
    "    }\n" \
    "}\n" \
    /* a general-combiner input byte, mapped (input()) */ \
    "vec3 gin3(uint b) {\n" \
    "    uint r = b & 15u; vec4 s = rreg(r); vec3 x = r >= 14u ? vec3(0.0) : (((b >> 4) & 1u) != 0u ? vec3(s.a) : s.rgb);\n" \
    "    uint m = (b >> 5) & 7u;\n" \
    "    if (m == 0u) return max(x, 0.0);\n" \
    "    if (m == 1u) return 1.0 - clamp(x, 0.0, 1.0);\n" \
    "    if (m == 2u) return 2.0 * max(x, 0.0) - 1.0;\n" \
    "    if (m == 3u) return 1.0 - 2.0 * max(x, 0.0);\n" \
    "    if (m == 4u) return max(x, 0.0) - 0.5;\n" \
    "    if (m == 5u) return 0.5 - max(x, 0.0);\n" \
    "    if (m == 6u) return x;\n" \
    "    return -x;\n" \
    "}\n" \
    "float gin1(uint b) {\n" \
    "    uint r = b & 15u; vec4 s = rreg(r); float x = r >= 14u ? 0.0 : (((b >> 4) & 1u) != 0u ? s.a : s.b);\n" \
    "    uint m = (b >> 5) & 7u;\n" \
    "    if (m == 0u) return max(x, 0.0);\n" \
    "    if (m == 1u) return 1.0 - clamp(x, 0.0, 1.0);\n" \
    "    if (m == 2u) return 2.0 * max(x, 0.0) - 1.0;\n" \
    "    if (m == 3u) return 1.0 - 2.0 * max(x, 0.0);\n" \
    "    if (m == 4u) return max(x, 0.0) - 0.5;\n" \
    "    if (m == 5u) return 0.5 - max(x, 0.0);\n" \
    "    if (m == 6u) return x;\n" \
    "    return -x;\n" \
    "}\n" \
    "vec3 sb3(vec3 v, uint op) {\n" \
    "    if (op == 1u) v = v - 0.5; else if (op == 2u) v = v * 2.0; else if (op == 3u) v = (v - 0.5) * 2.0;\n" \
    "    else if (op == 4u) v = v * 4.0; else if (op == 6u) v = v * 0.5;\n" \
    "    return clamp(v, -1.0, 1.0);\n" \
    "}\n" \
    "float sb1(float v, uint op) {\n" \
    "    if (op == 1u) v = v - 0.5; else if (op == 2u) v = v * 2.0; else if (op == 3u) v = (v - 0.5) * 2.0;\n" \
    "    else if (op == 4u) v = v * 4.0; else if (op == 6u) v = v * 0.5;\n" \
    "    return clamp(v, -1.0, 1.0);\n" \
    "}\n" \
    "void wr3(uint d, vec3 v) {\n" \
    "    switch (d) {\n" \
    "    case 4u: Rv0.rgb = v; break; case 5u: Rv1.rgb = v; break;\n" \
    "    case 8u: Rt0.rgb = v; break; case 9u: Rt1.rgb = v; break; case 10u: Rt2.rgb = v; break; case 11u: Rt3.rgb = v; break;\n" \
    "    case 12u: Rr0.rgb = v; break; case 13u: Rr1.rgb = v; break;\n" \
    "    default: break;\n" \
    "    }\n" \
    "}\n" \
    "void wr1(uint d, float v) {\n" \
    "    switch (d) {\n" \
    "    case 4u: Rv0.a = v; break; case 5u: Rv1.a = v; break;\n" \
    "    case 8u: Rt0.a = v; break; case 9u: Rt1.a = v; break; case 10u: Rt2.a = v; break; case 11u: Rt3.a = v; break;\n" \
    "    case 12u: Rr0.a = v; break; case 13u: Rr1.a = v; break;\n" \
    "    default: break;\n" \
    "    }\n" \
    "}\n" \
    "vec3 fin3(uint b) {\n" \
    "    uint r = b & 15u; vec4 s = r == 1u ? u_fc0 : r == 2u ? u_fc1 : rreg(r);\n" \
    "    vec3 x = clamp(((b >> 4) & 1u) != 0u ? vec3(s.a) : s.rgb, 0.0, 1.0);\n" \
    "    return ((b >> 5) & 1u) != 0u ? 1.0 - x : x;\n" \
    "}\n" \
    "float fin1(uint b) {\n" \
    "    uint r = b & 15u; vec4 s = r == 1u ? u_fc0 : r == 2u ? u_fc1 : rreg(r);\n" \
    "    float x = clamp(((b >> 4) & 1u) != 0u ? s.a : s.b, 0.0, 1.0);\n" \
    "    return ((b >> 5) & 1u) != 0u ? 1.0 - x : x;\n" \
    "}\n" \
    "int in_stage(int t) {\n" \
    "    int s = t <= 1 ? 0 : int((pkw(36) >> (t == 2 ? 16u : 20u)) & 15u);\n" \
    "    return s < t ? s : t - 1;\n" \
    "}\n" \
    "vec4 tex2d(int t, vec2 uv) {\n" \
    "    if (t == 0) return texture(t0, uv); if (t == 1) return texture(t1, uv);\n" \
    "    if (t == 2) return texture(t2, uv); return texture(t3, uv);\n" \
    "}\n" \
    "vec3 bumpf(int s, vec2 uv) {\n" \
    "    if (s == 0) return nv2a_bump_fetch(t0, uv); if (s == 1) return nv2a_bump_fetch(t1, uv);\n" \
    "    return nv2a_bump_fetch(t2, uv);\n" \
    "}\n" \
    "vec4 fetch(int t, vec4 tc) {\n" \
    "    uint mode = (pkw(35) >> uint(5 * t)) & 31u;\n" \
    "    if (mode == 0u) return vec4(0.0);\n" \
    "    if (mode == 4u) return clamp(tc, 0.0, 1.0);\n" \
    "    if (mode == 5u) { if (any(lessThan(tc, vec4(0.0)))) discard; return vec4(0.0); }\n" \
    "    if (mode == 15u || mode == 16u) {\n" \
    "        if (t == 0) return vec4(0.0);\n" \
    "        int s = in_stage(t);\n" \
    "        vec4 p = s == 0 ? Tf0 : s == 1 ? Tf1 : Tf2;\n" \
    "        return tex2d(t, mode == 15u ? p.ar : p.gb);\n" \
    "    }\n" \
    "    if (mode == 6u || mode == 7u) {\n" \
    "        if (t == 0) return vec4(0.0);\n" \
    "        int s = in_stage(t);\n" \
    "        vec4 ts = s == 0 ? vT0 : s == 1 ? vT1 : vT2;\n" \
    "        vec3 b3 = bumpf(s, ts.xy * u_tscale[s]);\n" \
    "        vec2 dsdt = mat2(u_bumpmat[t]) * b3.xy;\n" \
    "        vec4 c = tex2d(t, (tc.xy + dsdt) * u_tscale[t]);\n" \
    "        if (mode == 7u) c *= u_bumpls[t].x * b3.z + u_bumpls[t].y;\n" \
    "        return c;\n" \
    "    }\n" \
    "    vec2 uv = (abs(tc.w) > 1e-8 && mode == 1u) ? tc.xy / tc.w : tc.xy;\n" \
    "    return tex2d(t, uv * u_tscale[t]);\n" \
    "}\n" \
    "void main() {\n" \
    "    vec4 vT3 = u_psprite != 0 ? vec4(gl_PointCoord, 1.0, 1.0) : vT3in;\n" \
    "    Tf0 = fetch(0, vT0); Tf1 = fetch(1, vT1); Tf2 = fetch(2, vT2);\n" \
    "    vec4 Tf3 = fetch(3, vT3);\n" \
    "    uint ctl = pkw(32);\n" \
    "    Rc0 = vec4(0.0); Rc1 = vec4(0.0); Rr1 = vec4(0.0); Rv1r0 = vec4(0.0); Ref = vec4(0.0);\n" \
    "    Rfog = vec4(u_fogcolor.rgb, nv2a_fog(vFog));\n" \
    "    Rv0 = vD0; Rv1 = vD1;\n" \
    "    Rt0 = Tf0; Rt1 = Tf1; Rt2 = Tf2; Rt3 = Tf3;\n" \
    "    Rr0 = vec4(0.0, 0.0, 0.0, Tf0.a);\n" \
    "    int count = min(int(ctl & 255u), 8);\n" \
    "    for (int i = 0; i < count; i++) {\n" \
    "        uint cicw = pkw(i), cocw = pkw(8 + i), aicw = pkw(16 + i), aocw = pkw(24 + i);\n" \
    "        Rc0 = u_c0[(ctl & (1u << 12)) != 0u ? i : 0];\n" \
    "        Rc1 = u_c1[(ctl & (1u << 16)) != 0u ? i : 0];\n" \
    "        vec3 cA = gin3(cicw >> 24), cB = gin3(cicw >> 16), cC = gin3(cicw >> 8), cD = gin3(cicw);\n" \
    "        float aA = gin1(aicw >> 24), aB = gin1(aicw >> 16), aC = gin1(aicw >> 8), aD = gin1(aicw);\n" \
    "        bool mux = (ctl & (1u << 8)) != 0u ? Rr0.a >= 0.5 : (int(Rr0.a * 255.0 + 0.5) & 1) != 0;\n" \
    "        vec3 cAB = (cocw & (1u << 13)) != 0u ? vec3(dot(cA, cB)) : cA * cB;\n" \
    "        vec3 cCD = (cocw & (1u << 12)) != 0u ? vec3(dot(cC, cD)) : cC * cD;\n" \
    "        vec3 cS = (cocw & (1u << 14)) != 0u ? (mux ? cCD : cAB) : cAB + cCD;\n" \
    "        float aAB = aA * aB, aCD = aC * aD;\n" \
    "        float aS = (aocw & (1u << 14)) != 0u ? (mux ? aCD : aAB) : aAB + aCD;\n" \
    "        uint cop = (cocw >> 15) & 7u, aop = (aocw >> 15) & 7u;\n" \
    "        cAB = sb3(cAB, cop); cCD = sb3(cCD, cop); cS = sb3(cS, cop);\n" \
    "        aAB = sb1(aAB, aop); aCD = sb1(aCD, aop); aS = sb1(aS, aop);\n" \
    "        wr3((cocw >> 4) & 15u, cAB); wr3(cocw & 15u, cCD); wr3((cocw >> 8) & 15u, cS);\n" \
    "        wr1((aocw >> 4) & 15u, aAB); wr1(aocw & 15u, aCD); wr1((aocw >> 8) & 15u, aS);\n" \
    "        if ((cocw & (1u << 19)) != 0u) wr1((cocw >> 4) & 15u, cAB.b);\n" \
    "        if ((cocw & (1u << 18)) != 0u) wr1(cocw & 15u, cCD.b);\n" \
    "    }\n" \
    "    uint cw0 = pkw(33), cw1 = pkw(34);\n" \
    "    Rc0 = u_c0[0]; Rc1 = u_c1[0];\n" /* reg_name(.., stage 0): a final input never reads these (fin*) */ \
    "    vec3 v1 = clamp(Rv1.rgb, 0.0, 1.0), r0 = clamp(Rr0.rgb, 0.0, 1.0);\n" \
    "    Rv1r0 = vec4(((cw1 & 0x40u) != 0u ? 1.0 - v1 : v1) + ((cw1 & 0x20u) != 0u ? 1.0 - r0 : r0), 0.0);\n" \
    "    if ((cw1 & 0x80u) != 0u) Rv1r0 = clamp(Rv1r0, 0.0, 1.0);\n" \
    "    Ref = vec4(fin3(cw1 >> 24) * fin3(cw1 >> 16), 0.0);\n" \
    "    vec3 fA = fin3(cw0 >> 24), fB = fin3(cw0 >> 16), fC = fin3(cw0 >> 8), fD = fin3(cw0);\n" \
    "    fragColor = vec4(fA * fB + (1.0 - fA) * fC + fD, fin1(cw1 >> 8));\n" \
    "    if (u_mark != 0) { fragColor = vec4(1.0, 0.0, 1.0, 1.0); return; }\n" \
    "    fragColor = clamp(fragColor, 0.0, 1.0);\n" \
    "    float ar = fragColor.a * 255.0;\n" \
    "    bool pass = true;\n" \
    "    if (u_alpha_func == 0x200) pass = false;\n" \
    "    else if (u_alpha_func == 0x201) pass = ar <  u_alpha_ref;\n" \
    "    else if (u_alpha_func == 0x202) pass = ar == u_alpha_ref;\n" \
    "    else if (u_alpha_func == 0x203) pass = ar <= u_alpha_ref;\n" \
    "    else if (u_alpha_func == 0x204) pass = ar >  u_alpha_ref;\n" \
    "    else if (u_alpha_func == 0x205) pass = ar != u_alpha_ref;\n" \
    "    else if (u_alpha_func == 0x206) pass = ar >= u_alpha_ref;\n" \
    "    if (!pass) discard;\n" \
    "}\n"

int nv2a_uber_psh(char *buf, size_t cap)
{
    /* through printf like the rest: NV2A_BUMP_GLSL carries %% */
    Out o = { buf, cap, 0 };
    emit(&o, UBER_PSH_GLSL);
    return o.overflow ? -1 : (int)(cap - o.left);
}

int nv2a_uber_psh_covers(const Nv2aPshKey *k)
{
    int t;
    if (k->shadow & 0xFu)
        return 0;                                    /* shadow-map stages */
    for (t = 0; t < 4; t++)
        if (((k->shader_program >> (5 * t)) & 0x1Fu) == 3u)
            return 0;                                /* CUBE_MAP: a samplerCube binding */
    return 1;
}

int nv2a_gl_psh(const Nv2aPshKey *k, char *buf, size_t cap)
{
    Out o = { buf, cap, 0 };
    uint32_t count = k->control & 0xFF;
    uint32_t i;

    if (count > 8)
        count = 8;
    if (nv2a_shader_vk) {
        /* Vulkan (nv2a_vk): std140 block at binding 1 -- vec2 array
         * elements take 16 bytes -- and samplers at bindings 2..5,
         * samplerCube for a CUBE_MAP stage. */
        emit(&o,
            "#version 450\n"
            "layout(location = 0) in vec4 vD0; layout(location = 1) in vec4 vD1;\n"
            "layout(location = 2) in vec4 vT0; layout(location = 3) in vec4 vT1;\n"
            "layout(location = 4) in vec4 vT2; layout(location = 5) in vec4 vT3in;\n"
            "layout(location = 6) in float vFog;\n"
            "vec4 vT3;\n");
        for (i = 0; i < 4; i++)
            emit(&o, "layout(set = 0, binding = %u) uniform %s t%u;\n", i + 2,
                 ((k->shader_program >> (5 * i)) & 0x1F) == 3 ? "samplerCube" : "sampler2D",
                 i);
        emit(&o,
            "layout(std140, set = 0, binding = 1) uniform FsU {\n"
            "    vec2 u_tscale[4];\n"
            "    vec4 u_c0[8]; vec4 u_c1[8];\n"
            "    vec4 u_fc0; vec4 u_fc1;\n"
            "    vec4 u_fogcolor;\n"
            "    int u_alpha_func; float u_alpha_ref;\n"
            "    int u_psprite;\n"   /* point sprite (SET_POINT_SMOOTH_ENABLE on a point draw): stage 3 reads the point */
            "    int u_mark;\n"      /* RECOMP_VK_DRAW_MARK: this draw solid magenta (nv2a_vk) */
            "    vec4 u_fogp;\n"
            "    vec4 u_bumpmat[4];\n"   /* BUMPENVMAP: m00 m01 m10 m11 per stage (column-major mat2) */
            "    vec4 u_bumpls[4];\n"    /* BUMPENVMAP_LUM: x scale, y offset */
            "};\n"
            "layout(location = 0) out vec4 fragColor;\n"
            NV2A_FOG_GLSL
            NV2A_BUMP_GLSL
            "void main() {\n"
            "    vT3 = u_psprite != 0 ? vec4(gl_PointCoord, 1.0, 1.0) : vT3in;\n");
    } else
    emit(&o,
        "#version 330 core\n"
        "in vec4 vD0; in vec4 vD1; in vec4 vT0; in vec4 vT1;\n"
        "in vec4 vT2; in vec4 vT3; in float vFog;\n"
        "uniform sampler2D t0; uniform sampler2D t1;\n"
        "uniform sampler2D t2; uniform sampler2D t3;\n"
        "uniform vec2 u_tscale[4];\n"
        "uniform vec4 u_c0[8]; uniform vec4 u_c1[8];\n"
        "uniform vec4 u_fc0; uniform vec4 u_fc1;\n"
        "uniform vec4 u_fogcolor;\n"
        "uniform int u_alpha_func; uniform float u_alpha_ref;\n"
        "uniform vec4 u_fogp;\n"
        "uniform vec4 u_bumpmat[4]; uniform vec4 u_bumpls[4];\n"
        "out vec4 fragColor;\n"
        NV2A_FOG_GLSL
        NV2A_BUMP_GLSL
        "void main() {\n");
    for (i = 0; i < 4; i++)
        tex_fetch(&o, (int)i, (k->shader_program >> (5 * i)) & 0x1F, k);
    emit(&o,
        "    vec4 ZERO = vec4(0.0);\n"
        "    vec4 V0 = vD0, V1 = vD1;\n"
        "    vec4 R0 = vec4(0.0, 0.0, 0.0, T0.a), R1 = vec4(0.0);\n"
        "    vec4 FOG = vec4(u_fogcolor.rgb, nv2a_fog(vFog));\n");
    for (i = 0; i < count; i++)
        stage(&o, (int)i, k);

    /* Final combiner. */
    {
        uint32_t cw0 = k->final0, cw1 = k->final1;
        emit(&o, "    vec4 V1R0 = vec4(%s + %s, 0.0);\n",
             (cw1 & 0x40) ? "(1.0 - clamp(V1.rgb, 0.0, 1.0))" : "clamp(V1.rgb, 0.0, 1.0)",
             (cw1 & 0x20) ? "(1.0 - clamp(R0.rgb, 0.0, 1.0))" : "clamp(R0.rgb, 0.0, 1.0)");
        if (cw1 & 0x80)
            emit(&o, "    V1R0 = clamp(V1R0, 0.0, 1.0);\n");
        emit(&o, "    vec4 EF = vec4(");
        final_input(&o, cw1 >> 24, 0, k);
        emit(&o, " * ");
        final_input(&o, cw1 >> 16, 0, k);
        emit(&o, ", 0.0);\n");
        emit(&o, "    vec3 fA = "); final_input(&o, cw0 >> 24, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fB = "); final_input(&o, cw0 >> 16, 0, k); emit(&o, ";\n");
        emit(&o, "    vec3 fC = "); final_input(&o, cw0 >> 8, 0, k);  emit(&o, ";\n");
        emit(&o, "    vec3 fD = "); final_input(&o, cw0, 0, k);       emit(&o, ";\n");
        emit(&o, "    float fG = "); final_input(&o, cw1 >> 8, 1, k); emit(&o, ";\n");
        emit(&o, "    fragColor = vec4(fA * fB + (1.0 - fA) * fC + fD, fG);\n");
    }
    if (nv2a_shader_vk)
        emit(&o, "    if (u_mark != 0) { fragColor = vec4(1.0, 0.0, 1.0, 1.0); return; }\n");
    emit(&o,
        "    fragColor = clamp(fragColor, 0.0, 1.0);\n"
        "    float ar = fragColor.a * 255.0;\n"
        "    bool pass = true;\n"
        "    if (u_alpha_func == 0x200) pass = false;\n"
        "    else if (u_alpha_func == 0x201) pass = ar <  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x202) pass = ar == u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x203) pass = ar <= u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x204) pass = ar >  u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x205) pass = ar != u_alpha_ref;\n"
        "    else if (u_alpha_func == 0x206) pass = ar >= u_alpha_ref;\n"
        "    if (!pass) discard;\n"
        "}\n");
    return o.overflow ? -1 : (int)(cap - o.left);
}
