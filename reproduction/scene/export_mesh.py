"""Export the exact evaluated frozen mesh on CPU; never render or save .blend."""
import hashlib
import json
from pathlib import Path
import struct
import time
import argparse,sys

import bpy
import numpy as np

HERE = Path(__file__).resolve().parent
SOURCE = HERE / "indirect_alcove.blend"
SCENE_SHA = "c844dfc2401c5fb3b8e30d9f4f375dbeca4fbb8d5aeb21ff66e6876bc2237f93"
ACCEPTED_FILE_SHAS = {SCENE_SHA, "8e98ac44c4dab9e52821b3bfff9a510b418f460fb99a9c3223e7829a1296c45c"}
CURVES = {"bronze low sphere": 1, "blue tall ellipsoid": 1,
          "ivory upper sphere": 1, "rough bronze standing ring": 3}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalized(value):
    return value / np.maximum(np.linalg.norm(value, axis=-1, keepdims=True), 1e-30)


def main():
    global HERE,SOURCE
    parser=argparse.ArgumentParser()
    parser.add_argument('--scene',type=Path,default=SOURCE)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--'in sys.argv else [])
    if args.out.exists():raise FileExistsError('Use a fresh output directory')
    args.out.mkdir(parents=True);HERE=args.out.resolve();SOURCE=args.scene.resolve()
    started = time.perf_counter()
    assert not (HERE / "mesh_v1.npz").exists(), "Immutable geometry export already exists"
    assert sha(SOURCE) in ACCEPTED_FILE_SHAS
    bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
    bpy.context.scene.cycles.device = "CPU"
    bpy.context.scene.render.threads_mode = "FIXED"
    bpy.context.scene.render.threads = 2
    graph = bpy.context.evaluated_depsgraph_get()
    positions, normals, uvs, metadata = [], [], [], []
    objects, materials, charts = [], [], []
    material_ids = {}
    original_mesh_data = set()
    seam_unwraps = 0
    for obj in bpy.context.scene.objects:
        if obj.type != "MESH":
            continue
        assert obj.data.name not in original_mesh_data and obj.data.users == 1
        original_mesh_data.add(obj.data.name)
        assert not any(m.type in ("SUBSURF", "DISPLACE") for m in obj.modifiers)
        evaluated = obj.evaluated_get(graph)
        mesh = evaluated.to_mesh(preserve_all_data_layers=True, depsgraph=graph)
        mesh.calc_loop_triangles()
        mesh.calc_normals_split()
        object_id = len(objects)
        chart_base = len(charts)
        wrap = CURVES.get(obj.name, 0)
        for side in range(1 if wrap else 6):
            charts.append({"id": len(charts), "object_id": object_id, "object_name": obj.name,
                "kind": "periodic_existing_uv" if wrap else "dominant_geometric_face",
                "face_axis": None if wrap else side // 2, "face_negative": None if wrap else bool(side % 2),
                "wrap_flags": wrap, "triangle_count": 0, "physical_area": 0.0,
                "metric_u_area_sum": 0.0, "metric_v_area_sum": 0.0})
        material_map = []
        for mat in mesh.materials:
            bsdf = mat.node_tree.nodes.get("Principled BSDF")
            assert bsdf and not bsdf.inputs["Normal"].is_linked
            assert bsdf.inputs["Subsurface"].default_value == 0
            assert bsdf.inputs["Transmission"].default_value == 0
            output = next(n for n in mat.node_tree.nodes if n.type == "OUTPUT_MATERIAL" and n.is_active_output)
            assert not output.inputs["Displacement"].is_linked and not output.inputs["Volume"].is_linked
            if mat.name not in material_ids:
                material_ids[mat.name] = len(materials)
                materials.append({"id": len(materials), "name": mat.name,
                    "base_color": list(bsdf.inputs["Base Color"].default_value)[:3],
                    "roughness": float(bsdf.inputs["Roughness"].default_value),
                    "metallic": float(bsdf.inputs["Metallic"].default_value)})
            material_map.append(material_ids[mat.name])
        local = np.asarray([tuple(v.co) for v in mesh.vertices], np.float64)
        lo, hi = local.min(axis=0), local.max(axis=0)
        matrix = np.asarray(obj.matrix_world, np.float64)
        normal_matrix = np.linalg.inv(matrix[:3, :3]).T
        world = ((local @ matrix[:3, :3].T + matrix[:3, 3]) * 128).astype(np.float32)
        start = len(positions)
        for tri in mesh.loop_triangles:
            corner_ids = list(tri.vertices)
            p = world[corner_ids]
            n = np.asarray([tuple(mesh.loops[i].normal) for i in tri.loops], np.float64)
            n = normalized(n @ normal_matrix.T).astype(np.float32)
            if wrap:
                uv = np.asarray([tuple(mesh.uv_layers.active.data[i].uv) for i in tri.loops], np.float64)
                for axis in range(2):
                    if wrap & (1 << axis) and np.ptp(uv[:, axis]) > .5:
                        uv[uv[:, axis] < .5, axis] += 1
                        seam_unwraps += 1
                chart_id = chart_base
            else:
                geometric = np.asarray(tri.normal, np.float64)
                axis = int(np.argmax(np.abs(geometric)))
                side = axis * 2 + int(geometric[axis] < 0)
                chart_id = chart_base + side
                axes = [a for a in range(3) if a != axis]
                uv = (local[corner_ids][:, axes] - lo[axes]) / (hi[axes] - lo[axes])
            uv = uv.astype(np.float32)
            material_id = material_map[tri.material_index]
            roughness = materials[material_id]["roughness"]
            positions.append(p); normals.append(n); uvs.append(uv)
            metadata.append([chart_id, material_id, object_id, struct.unpack("<I", struct.pack("<f", roughness))[0]])
            chart = charts[chart_id]
            area = .5 * np.linalg.norm(np.cross((p[1] - p[0]).astype(np.float64), (p[2] - p[0]).astype(np.float64)))
            duv = (uv[1:] - uv[0]).astype(np.float64)
            determinant = np.linalg.det(duv)
            assert abs(determinant) > 1e-12, (obj.name, tri.index, "degenerate chart UV")
            # Columns are world-space dP/du,dP/dv.
            dpduv = (p[1:] - p[0]).astype(np.float64).T @ np.linalg.inv(duv.T)
            chart["triangle_count"] += 1
            chart["physical_area"] += float(area)
            chart["metric_u_area_sum"] += float(np.linalg.norm(dpduv[:, 0]) * area)
            chart["metric_v_area_sum"] += float(np.linalg.norm(dpduv[:, 1]) * area)
        objects.append({"id": object_id, "name": obj.name, "triangle_offset": start,
                        "triangle_count": len(positions) - start, "chart_offset": chart_base,
                        "chart_count": 1 if wrap else 6, "original_mesh_users": obj.data.users,
                        "auto_smooth": mesh.use_auto_smooth, "has_custom_normals": mesh.has_custom_normals,
                        "material_ids": material_map, "matrix_world": matrix.tolist()})
        evaluated.to_mesh_clear()
    for chart in charts:
        assert chart["physical_area"] > 0 and chart["triangle_count"] > 0
        chart["metric_u"] = chart.pop("metric_u_area_sum") / chart["physical_area"]
        chart["metric_v"] = chart.pop("metric_v_area_sum") / chart["physical_area"]
        chart["metric_aspect"] = chart["metric_u"] / chart["metric_v"]
    arrays = {"positions": np.asarray(positions, np.float32), "normals": np.asarray(normals, np.float32),
              "uv": np.asarray(uvs, np.float32), "metadata": np.asarray(metadata, np.uint32)}
    assert len(positions) == 10936 and len(charts) == 88 and len(objects) == 18
    for value in arrays.values():
        assert np.isfinite(value).all()
    np.savez(HERE / "mesh_v1.npz", **arrays)
    # Exact portable runtime payload: six vec4 corner blocks + finaluvec4.
    packed = np.empty((len(positions), 28), np.float32)
    for corner in range(3):
        packed[:, corner * 8:corner * 8 + 3] = arrays["positions"][:, corner]
        packed[:, corner * 8 + 3:corner * 8 + 6] = arrays["normals"][:, corner]
        packed[:, corner * 8 + 6:corner * 8 + 8] = arrays["uv"][:, corner]
    packed.view(np.uint32)[:, 24:28] = arrays["metadata"]
    packed.tofile(HERE / "triangles_v1.bin")
    result = {"scene_path": str(SOURCE), "scene_sha256": SCENE_SHA,
        "exporter_sha256": sha(Path(__file__)), "schema_sha256": sha(HERE / "SCHEMA_V1.txt"),
        "mesh_npz_sha256": sha(HERE / "mesh_v1.npz"), "triangle_binary_sha256": sha(HERE / "triangles_v1.bin"),
        "triangles": len(positions), "objects": objects, "materials": materials, "charts": charts,
        "triangle_stride_bytes": 112, "triangle_payload_bytes": packed.nbytes,
        "curve_triangle_axis_unwrap_count": seam_unwraps,
        "normal_contract": "Cycles3.6 static-transform path: per-corner inverse-transpose then normalize; interpolate+normalize at hit; auto-smooth loop normals override flat polygon flags",
        "static_transform_criteria_verified": "Each mesh data has one user; no subdivision/displacement modifier, BSSRDF, transmission, linked displacement, volume or normal map",
        "normal_sources": ["https://raw.githubusercontent.com/blender/blender/v3.6.0/intern/cycles/blender/mesh.cpp",
                           "https://raw.githubusercontent.com/blender/blender/v3.6.0/intern/cycles/scene/mesh.cpp"],
        "render_invoked": False, "export_seconds": time.perf_counter() - started,
        "source_unchanged": sha(SOURCE) in ACCEPTED_FILE_SHAS}
    (HERE / "geometry_manifest.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("EXPORT_COMPLETE " + json.dumps({k: result[k] for k in ("triangles", "mesh_npz_sha256", "triangle_binary_sha256", "triangle_payload_bytes", "export_seconds", "source_unchanged")}))


if __name__ == "__main__":
    main()
