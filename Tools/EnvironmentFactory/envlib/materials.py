"""Blender (Cycles) materials for the environment assets, built from the generated texture sets.

Normal maps are written in the DirectX convention (Unreal's native, green = -Y); the Blender materials flip
the green channel back. Slot / material names (M_Leaf_*, M_Bark_*, M_Ground_*) are the ones the Unreal
import script maps to material instances.
"""
from __future__ import annotations

import os

import bpy

from . import common


def _img(path, colorspace):
    key = os.path.abspath(path)
    for im in bpy.data.images:
        if im.filepath and os.path.abspath(bpy.path.abspath(im.filepath)) == key:
            return im
    im = bpy.data.images.load(key, check_existing=True)
    im.colorspace_settings.name = colorspace
    return im


def _tex(nt, path, colorspace, x, y, mapping=None, interpolation="Linear"):
    n = nt.nodes.new("ShaderNodeTexImage")
    n.image = _img(path, colorspace)
    n.interpolation = interpolation
    n.location = (x, y)
    if mapping is not None:
        nt.links.new(mapping, n.inputs["Vector"])
    return n


def dx_normal(nt, tex_node, x, y, strength=1.0):
    """DirectX normal texture -> Blender Normal Map node (green flipped)."""
    sep = nt.nodes.new("ShaderNodeSeparateColor")
    sep.location = (x, y)
    nt.links.new(tex_node.outputs["Color"], sep.inputs["Color"])
    inv = nt.nodes.new("ShaderNodeMath")
    inv.operation = "SUBTRACT"
    inv.inputs[0].default_value = 1.0
    inv.location = (x + 160, y - 60)
    nt.links.new(sep.outputs["Green"], inv.inputs[1])
    comb = nt.nodes.new("ShaderNodeCombineColor")
    comb.location = (x + 320, y)
    nt.links.new(sep.outputs["Red"], comb.inputs["Red"])
    nt.links.new(inv.outputs[0], comb.inputs["Green"])
    nt.links.new(sep.outputs["Blue"], comb.inputs["Blue"])
    nm = nt.nodes.new("ShaderNodeNormalMap")
    nm.location = (x + 480, y)
    nm.inputs["Strength"].default_value = strength
    nt.links.new(comb.outputs["Color"], nm.inputs["Color"])
    return nm


def leaf_material(name, tex_prefix, translucency=0.32, tint=(1.0, 1.0, 0.8), variation=True):
    """Two-sided alpha-tested foliage: Principled + Translucent, cut out by the opacity map."""
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.blend_method = "HASHED"
    try:
        mat.shadow_method = "HASHED"
    except Exception:
        pass
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (900, 0)
    T = common.texture_path
    col = _tex(nt, T(tex_prefix, "BaseColor"), "sRGB", -800, 300)
    opa = _tex(nt, T(tex_prefix, "Opacity"), "Non-Color", -800, 0)
    nrm = _tex(nt, T(tex_prefix, "Normal"), "Non-Color", -800, -300)
    tra = _tex(nt, T(tex_prefix, "Translucency"), "Non-Color", -800, -600)
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    p.location = (100, 200)
    base = col.outputs["Color"]
    if variation:
        # per-instance colour variation (Object Info > Random): brightness 0.72-1.08, slight hue / saturation shift
        oi = nt.nodes.new("ShaderNodeObjectInfo")
        oi.location = (-800, 600)
        hs = nt.nodes.new("ShaderNodeHueSaturation")
        hs.location = (-300, 450)
        mr = nt.nodes.new("ShaderNodeMapRange")
        mr.location = (-550, 650)
        mr.inputs["To Min"].default_value = 0.485
        mr.inputs["To Max"].default_value = 0.52
        nt.links.new(oi.outputs["Random"], mr.inputs["Value"])
        nt.links.new(mr.outputs["Result"], hs.inputs["Hue"])
        mv = nt.nodes.new("ShaderNodeMapRange")
        mv.location = (-550, 450)
        mv.inputs["To Min"].default_value = 0.72
        mv.inputs["To Max"].default_value = 1.08
        nt.links.new(oi.outputs["Random"], mv.inputs["Value"])
        nt.links.new(mv.outputs["Result"], hs.inputs["Value"])
        hs.inputs["Saturation"].default_value = 0.92
        nt.links.new(col.outputs["Color"], hs.inputs["Color"])
        base = hs.outputs["Color"]
    nt.links.new(base, p.inputs["Base Color"])
    p.inputs["Roughness"].default_value = 0.5
    p.inputs["Specular IOR Level"].default_value = 0.35
    nm = dx_normal(nt, nrm, -500, -300, 0.8)
    nt.links.new(nm.outputs["Normal"], p.inputs["Normal"])
    tb = nt.nodes.new("ShaderNodeBsdfTranslucent")
    tb.location = (100, -250)
    tmul = nt.nodes.new("ShaderNodeMix")
    tmul.data_type = "RGBA"
    tmul.blend_type = "MULTIPLY"
    tmul.inputs[0].default_value = 1.0
    tmul.location = (-200, -150)
    nt.links.new(base, tmul.inputs[6])
    tmul.inputs[7].default_value = (*tint, 1.0)
    nt.links.new(tmul.outputs[2], tb.inputs["Color"])
    fac = nt.nodes.new("ShaderNodeMath")
    fac.operation = "MULTIPLY"
    fac.inputs[1].default_value = translucency
    fac.location = (100, -450)
    nt.links.new(tra.outputs["Color"], fac.inputs[0])
    mix = nt.nodes.new("ShaderNodeMixShader")
    mix.location = (400, 0)
    nt.links.new(fac.outputs[0], mix.inputs[0])
    nt.links.new(p.outputs[0], mix.inputs[1])
    nt.links.new(tb.outputs[0], mix.inputs[2])
    tr = nt.nodes.new("ShaderNodeBsdfTransparent")
    tr.location = (400, 250)
    cut = nt.nodes.new("ShaderNodeMath")
    cut.operation = "GREATER_THAN"
    cut.inputs[1].default_value = 0.45
    cut.location = (-400, 0)
    nt.links.new(opa.outputs["Color"], cut.inputs[0])
    mix2 = nt.nodes.new("ShaderNodeMixShader")
    mix2.location = (650, 0)
    nt.links.new(cut.outputs[0], mix2.inputs[0])
    nt.links.new(tr.outputs[0], mix2.inputs[1])
    nt.links.new(mix.outputs[0], mix2.inputs[2])
    nt.links.new(mix2.outputs[0], out.inputs["Surface"])
    return mat


def bark_material(name, tex_prefix, normal_strength=1.0, tint=None):
    mat = bpy.data.materials.get(name)
    if mat:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    out.location = (700, 0)
    T = common.texture_path
    col = _tex(nt, T(tex_prefix, "BaseColor"), "sRGB", -800, 300)
    rgh = _tex(nt, T(tex_prefix, "Roughness"), "Non-Color", -800, 0)
    nrm = _tex(nt, T(tex_prefix, "Normal"), "Non-Color", -800, -300)
    p = nt.nodes.new("ShaderNodeBsdfPrincipled")
    p.location = (300, 0)
    src = col.outputs["Color"]
    if tint is not None:
        mul = nt.nodes.new("ShaderNodeMix")
        mul.data_type = "RGBA"
        mul.blend_type = "MULTIPLY"
        mul.inputs[0].default_value = 1.0
        nt.links.new(src, mul.inputs[6])
        mul.inputs[7].default_value = (*tint, 1.0)
        src = mul.outputs[2]
    nt.links.new(src, p.inputs["Base Color"])
    nt.links.new(rgh.outputs["Color"], p.inputs["Roughness"])
    p.inputs["Specular IOR Level"].default_value = 0.3
    nm = dx_normal(nt, nrm, -500, -300, normal_strength)
    nt.links.new(nm.outputs["Normal"], p.inputs["Normal"])
    nt.links.new(p.outputs[0], out.inputs["Surface"])
    return mat


def flat_material(name, color, roughness=0.6):
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    p = mat.node_tree.nodes.get("Principled BSDF")
    p.inputs["Base Color"].default_value = (*color, 1.0)
    p.inputs["Roughness"].default_value = roughness
    return mat
