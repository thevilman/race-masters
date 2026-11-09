# Export each car of the vehicle library to tgcar files (3 levels of detail) for the 3D mod.
# Format (little-endian): "TGCR", u32 version 2, u32 levels; per level: u32 triangles, then
# per corner: 3 f32 position (x right, y up, z forward, metres), 3 f32 normal, u8 class, u8 r, g, b.
# Then the wheel (version 2): u32 triangles and corners as above, relative to its hub, as the
# left front wheel; then 4 hubs (3 f32 each: front left, front right, rear left, rear right).
import bpy, bmesh, struct, sys, os
from mathutils import Vector

args = sys.argv[sys.argv.index('--') + 1:]
out_dir = args[0]
GLB_DIR = args[1] if len(args) > 1 else None  # the cars as .glb files (else the .blend's collections)
GLB = {'Rosso': 'rosso-berlinetta', 'Glacier': 'glacier-track-special', 'Violet': 'violet-gt', 'Azure': 'azure-targa', 'Amber': 'amber-rs'}
os.makedirs(out_dir, exist_ok=True)
CARS = ['Rosso', 'Glacier', 'Violet', 'Azure', 'Amber']
RATIOS = [('C', 0.3), ('C', 0.15), ('C', 0.1)]  # collapse; then planar by angle (degrees)

def klass(mat):
    n = (mat.name if mat else '').lower()
    if 'metallic' in n: return 1      # paint
    if 'glass' in n: return 2
    if 'red laser' in n: return 3     # tail lights
    if 'white laser' in n: return 4   # headlights
    if 'rubber' in n: return 5        # tyres
    if 'stripe' in n: return 6
    return 0                          # trim, metal, carbon

def colour(mat):
    if mat is None: return (0.5, 0.5, 0.5)
    if mat.use_nodes:
        for nd in mat.node_tree.nodes:
            if nd.type == 'BSDF_PRINCIPLED':
                c = nd.inputs['Base Color'].default_value
                return (c[0], c[1], c[2])
    return tuple(mat.diffuse_color[:3])

def srgb(x):
    x = max(0.0, min(1.0, x))
    return int(round(255 * (x * 12.92 if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055)))

WHEEL = ('tire', 'tyre', 'rim', 'spoke', 'rotor', 'center cap', 'lug', 'tread')
import re
WHEEL_RE = re.compile(r'\b(tire|tyre|rim|spoke|rotor|lug)\b|center cap|tread|wheel')
def is_wheel(o): return WHEEL_RE.search(o.name.lower()) is not None
def mw(o): return o.evaluated_get(bpy.context.evaluated_depsgraph_get()).matrix_world
def centre(o):
    m = mw(o); return sum(((m @ Vector(c)) for c in o.bound_box), Vector()) / 8

def corners(m2, mats, long_x, origin=(0, 0, 0)):
    m2.calc_loop_triangles()
    buf = []
    for t in m2.loop_triangles:
        mat = mats[t.material_index] if t.material_index < len(mats) else None
        k = klass(mat); col = colour(mat)
        for li, vi in zip(t.loops, t.vertices):
            p = m2.vertices[vi].co - Vector(origin)
            n = m2.corner_normals[li].vector if t.use_smooth else t.normal
            if long_x: X, Y, Z, NX, NY, NZ = -p.y, p.z, p.x, -n.y, n.z, n.x
            else: X, Y, Z, NX, NY, NZ = p.x, p.z, p.y, n.x, n.z, n.y
            buf.append(struct.pack('<6f4B', X, Y, Z, NX, NY, NZ, k, srgb(col[0]), srgb(col[1]), srgb(col[2])))
    return len(m2.loop_triangles), buf

def gather(objs, dg):
    bm = bmesh.new(); mats = []
    for o in objs:
        if o.type not in ('MESH', 'CURVE'): continue
        ev = o.evaluated_get(dg)
        try: me = ev.to_mesh()
        except Exception: continue
        if me is None: continue
        me.transform(ev.matrix_world)
        base = len(mats)
        mats += [s.material for s in o.material_slots] or [None]
        tmp = bmesh.new(); tmp.from_mesh(me)
        for f in tmp.faces: f.material_index = base + min(f.material_index, max(0, len(o.material_slots) - 1))
        tmp.to_mesh(me); bm.from_mesh(me); tmp.free(); ev.to_mesh_clear()
    mesh = bpy.data.meshes.new('gathered'); bm.to_mesh(mesh); bm.free()
    return mesh, mats

dg = bpy.context.evaluated_depsgraph_get()
for ci, name in enumerate(CARS):
    if GLB_DIR:
        for o in list(bpy.data.objects): bpy.data.objects.remove(o, do_unlink=True)
        coll = bpy.data.collections.new('car_' + name)
        bpy.context.scene.collection.children.link(coll)
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.join(GLB_DIR, GLB[name] + '.glb'))
        for o in set(bpy.data.objects) - before:
            for c in list(o.users_collection): c.objects.unlink(o)
            coll.objects.link(o)
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get()
        print(name, 'imported', len(coll.all_objects), [o.name for o in coll.all_objects][:5])
    else:
        coll = bpy.data.collections[name]
    def is_light(o): return any(s.material and ('laser' in s.material.name.lower() or 'stripe' in s.material.name.lower()) for s in o.material_slots)
    mesh, mats = gather([o for o in coll.all_objects if not is_wheel(o) and not is_light(o)], dg)
    lmesh, lmats = gather([o for o in coll.all_objects if is_light(o)], dg)  # thin: not simplified
    tires = [o for o in coll.all_objects if re.search(r'\b(tire|tyre)\b', o.name.lower()) and 'groove' not in o.name.lower()]
    print(name, 'tires', [t.name for t in tires][:6], 'wheelparts', sum(1 for o in coll.all_objects if is_wheel(o)))
    # The car's footprint: centred, on the ground.
    xs = [v.co.x for v in mesh.vertices]; ys = [v.co.y for v in mesh.vertices]; zs = [v.co.z for v in mesh.vertices]
    cx, cy, z0 = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2, min(zs)
    for t in tires:  # the ground is under the tyres
        z0 = min(z0, min((mw(t) @ Vector(c)).z for c in t.bound_box))
    for v in mesh.vertices: v.co = Vector((v.co.x - cx, v.co.y - cy, v.co.z - z0))
    for v in lmesh.vertices: v.co = Vector((v.co.x - cx, v.co.y - cy, v.co.z - z0))
    print(name, 'size', round(max(xs) - min(xs), 2), round(max(ys) - min(ys), 2), round(max(zs) - min(zs), 2), 'faces', len(mesh.polygons))
    obj = bpy.data.objects.new(name + '_export', mesh)
    bpy.context.scene.collection.objects.link(obj)
    data = [b'TGCR', struct.pack('<II', 2, len(RATIOS))]
    long_x = (max(xs) - min(xs)) >= (max(ys) - min(ys))
    for r in RATIOS:
        mod = obj.modifiers.new('dec', 'DECIMATE')
        if r[0] == 'C': mod.ratio = r[1]
        else:
            import math
            mod.decimate_type = 'DISSOLVE'; mod.angle_limit = math.radians(r[1])
        dg2 = bpy.context.evaluated_depsgraph_get()
        ev = obj.evaluated_get(dg2)
        m2 = ev.to_mesh()
        n, buf = corners(m2, mats, long_x)
        ln, lbuf = corners(lmesh, lmats, long_x)
        data.append(struct.pack('<I', n + ln))
        data += buf + lbuf
        print(' level', r, n)
        ev.to_mesh_clear()
        obj.modifiers.remove(mod)
    # The wheel: the left front one's parts, around its hub; and the four hubs.
    hubs = [centre(t) for t in tires]
    hubs = [Vector((h.x - cx, h.y - cy, h.z - z0)) for h in hubs]
    def game(h): return (h.x, h.z, h.y) if not long_x else (-h.y, h.z, h.x)
    gh = sorted((game(h) for h in hubs), key=lambda g: (-g[2], g[0]))  # front first, left first
    fl = [t for t in tires if game(Vector((centre(t).x - cx, centre(t).y - cy, centre(t).z - z0))) == gh[0]][0]
    c0 = centre(fl)
    parts = [o for o in coll.all_objects if is_wheel(o) and (centre(o) - c0).length < 0.5]
    wm, wmats = gather(parts, dg)
    wo = bpy.data.objects.new('wheel_export', wm); bpy.context.scene.collection.objects.link(wo)
    mod = wo.modifiers.new('dec', 'DECIMATE'); mod.ratio = 0.14
    ev = wo.evaluated_get(bpy.context.evaluated_depsgraph_get()); m3 = ev.to_mesh()
    n, buf = corners(m3, wmats, long_x, origin=(c0.x, c0.y, c0.z))
    ev.to_mesh_clear()
    data.append(struct.pack('<I', n)); data += buf
    for g in gh[:4]: data.append(struct.pack('<3f', *g))
    print(' wheel', n, 'hubs', [tuple(round(x, 2) for x in g) for g in gh])
    open(os.path.join(out_dir, 'car-%s.tgcar' % name.lower()), 'wb').write(b''.join(data))
