#!/usr/bin/env python3
"""Brings PrusaSlicer's resources/shaders/ES to one dialect: GLSL ES 3.00.

Upstream mixes '#version 100' and '#version 300 es' files (programs pairing both fail to
link) and several ES 1.0 fragment shaders lack a default float precision.
Usage: fix-es-shaders.py <resources/shaders/ES directory>
"""
import re
import sys
from pathlib import Path

OUT_COLOR = "out_color"


def convert_100(src: str, is_fragment: bool) -> str:
    lines = src.splitlines()
    body = [l for l in lines[1:] if not l.strip().startswith("#extension GL_OES_standard_derivatives")]
    text = "\n".join(body)

    if is_fragment:
        text = re.sub(r"\bvarying\b", "in", text)
        text = text.replace("gl_FragColor", OUT_COLOR)
    else:
        text = re.sub(r"\battribute\b", "in", text)
        text = re.sub(r"\bvarying\b", "out", text)
    text = re.sub(r"\btexture2DLod\b", "textureLod", text)
    text = re.sub(r"\btexture2D\b", "texture", text)
    text = re.sub(r"\btextureCube\b", "texture", text)

    header = ["#version 300 es"]
    if is_fragment:
        # Before the output declaration; repeating a default precision later on is legal.
        header.append("precision highp float;")
        if OUT_COLOR in text:
            header.append(f"out vec4 {OUT_COLOR};")
    return "\n".join(header) + "\n" + text + "\n"


def add_precision_300(src: str) -> str:
    if re.search(r"precision\s+\w+\s+float\s*;", src):
        return src
    first, _, rest = src.partition("\n")
    return first + "\nprecision highp float;\n" + rest


def fix_float_int_compares(src: str) -> str:
    # GLSL ES has no implicit int -> float conversion. Only the float vectors known to be compared
    # with int literals upstream (integer vectors must keep int literals).
    return re.sub(r"(\bheight_width_angle\.[xyzw]\s*[=!<>]=?\s*)(\d+)(?![\d.])", r"\g<1>\2.0", src)


def fix_es_strictness(src: str, is_fragment: bool) -> str:
    """Implicit conversions desktop GLSL accepts and GLSL ES rejects (shadow / AO shaders)."""
    # textureSize() returns ivec2
    src = re.sub(r"(1\.0\s*/\s*)textureSize\((\w+),\s*0\)", r"\1vec2(textureSize(\2, 0))", src)
    src = re.sub(r"(\bvec2\s+\w+\s*=\s*)textureSize\((\w+),\s*0\)", r"\1vec2(textureSize(\2, 0))", src)
    # 'noperspective' needs GL_NV_shader_noperspective_interpolation in ES (wireframe overlay only)
    src = re.sub(r"\bnoperspective\s+", "", src)
    # float / int uniform, float * int define
    src = re.sub(r"/\s*(kernel_size|filter_size)\b", r"/ float(\1)", src)
    src = re.sub(r"\*\s*MAX_MATERIALS\b", "* float(MAX_MATERIALS)", src)
    if is_fragment:
        # 3.00 shaders that still write gl_FragColor
        if "gl_FragColor" in src:
            src = src.replace("gl_FragColor", OUT_COLOR)
            if not re.search(rf"\bout\s+vec4\s+{OUT_COLOR}\s*;", src):
                src = re.sub(r"(precision\s+\w+\s+float\s*;\n)", rf"\1out vec4 {OUT_COLOR};\n", src, count=1)
        # AO passes write a single channel: scalar -> vec4
        src = re.sub(rf"({OUT_COLOR}\s*=\s*)(pow\(1\.0 - \(occlusion / float\(kernel_size\)\), intensity\));",
                     r"\1vec4(\2);", src)
        src = re.sub(rf"({OUT_COLOR}\s*=\s*)(result / float\(filter_size\));", r"\1vec4(\2);", src)
    return src


def main() -> None:
    directory = Path(sys.argv[1])
    changed = 0
    for path in sorted(directory.iterdir()):
        if path.suffix not in (".vs", ".fs"):
            continue
        src = path.read_text()
        is_fragment = path.suffix == ".fs"
        first = src.lstrip().splitlines()[0].strip() if src.strip() else ""
        if first == "#version 100":
            out = convert_100(src, is_fragment)
        elif is_fragment:
            out = add_precision_300(src)
        else:
            out = src
        out = fix_float_int_compares(out)
        out = fix_es_strictness(out, is_fragment)
        if out != src:
            path.write_text(out)
            changed += 1
    print(f"ES shaders adjusted: {changed}")


if __name__ == "__main__":
    main()
