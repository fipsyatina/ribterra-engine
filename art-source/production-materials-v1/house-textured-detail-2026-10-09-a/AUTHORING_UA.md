# House texture detail: авторська SOURCE-чернетка

DRAFT / SOURCE ONLY / NOT_ACCEPTED. Генерація не є export/native/reference/FPS acceptance.
Parent запускає генератор лише після прийнятого authored-UV CPU checkpoint1.43.

Оригінальний ready House: дерев'яні trim/door/frames/fence/props, slate roof і кам'яна
основа/chimney/steps. Переглянуто original sheet і три надані user references. Reference
images мають baked освітлення, сніг і warm emissive вигляд; їхній RGB не sampled.
Синій навіс відсутній на цьому ready House й не доданий. 19649 old-paint unsupported
samples та приховані/відсутні деталі не закриваються новою геометрією.

master.json — exact byte copy frozen flat master;114XYZ triangles,14material meanings,
winding/AO/10coarse sky triangles/empty local occluders/20probes/eppp4/pivot незмінні.
Три constant semantics plaster/glass/metal зберігають exact RGB. Решта11materials
мають texture XOR constant; їхній master RGB лишається inactive bound constant.
Roughness/wet/caster scalars та emission0 не змінюються, не є фізичними вимірами.

RGB8 sources: timber192x256, slate192x192, fieldstone192x256;405504tight bytes total.
Timber base[123,88,55], slate[112,118,129], stone[142,138,124] — provisional authored
sRGB pigments, chosen from visual material meaning, not sampled reflectance. Integer
hash_noise supplies fixed modest pigment variation; source contains no directional
gradient, highlights, cavity shadow, AO or glow. Dark seam texels encode authored
material/joint pigment only. Roof has16px staggered courses/32px tiles; stone has8px
courses/24..32px blocks; timber has32px planks and vertical veins,128px butt joints.

UV3 follows ORIGINAL vertex order, top-left V-down nearest-clamp-v1. Roof uses shared
physical ridge/down-slope axes for roof/ridge facets; stone fronts/sides use global
house horizontal axes and global Z-down course extent, tops use global house XY axes.
Vertical wood/post/stave/door/frame/leg faces use whole-face width and world Z-down.
Trim, eave, rails and bench use the semantic member's declared long edge; cap boards
use the house front axis. Each entire semantic face/group has ONE affine projector
shared by its triangulated halves/fans; UV does not restart at a triangle diagonal.
Disconnected timber faces fit the same atlas separately; thus texel density varies
with member size. This is a documented draft choice, not a measured physical scale.
Proxy corners differing below1e-12 share UV identity only; XYZ bytes are never rounded.
uv-layout.json records actual bases/extents/group IDs and shared-corner checks.

Meaningful self-check recomputes SAME-prefix payload bytes twice, compares exact master,
all14scalars/assignments,3WH/UV bounds/nondegenerate areas and shared corners. Across
fresh roots RGB3/master/NOTICE/UV/document stay exact; only declared generated-source
paths in palette/collection plus their derived palette digest differ. Source-report
enumerates these fields; no broad JSON/provenance/source_record exemption is allowed.
Future exports must independently bind each source and compare all SIX channel payloads.

Outputs are exclusive fresh source files,<=128MiB root/free>=384MiB; failures preserved.
The32MiB arithmetic is logical buffer planning, not actual RSS/working-set measurement.
Full silhouette/model refinement, warm window/lantern emitters, contact metadata,
physical coefficients/artist acceptance, all other masters/FullFX/refs/FPS stay OPEN.
