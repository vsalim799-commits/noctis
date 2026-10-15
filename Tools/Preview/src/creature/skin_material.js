// Procedural reptile skin for the Noctis creature renderer (three.js 0.169, WebGL2, UV-free).
//
//   import { createSkinMaterial, configureSkinForBones, bakeSkinRegions } from './creature/skin_material.js';
//   const mat = createSkinMaterial(THREE, { dorsalColor: '#3a342f' });
//   configureSkinForBones(mat, TREX_BONES, TREX_SHAPES);       // optional: foot frames, brow / cheek hotspots
//   bakeSkinRegions(THREE, skinnedMesh.geometry, mat);         // optional, recommended on SDF meshes
//                                                              // (smooth region borders + labial scale rows)
//   skinnedMesh.material = mat;
//   mat.userData.skinUniforms.uBumpStrength.value = 0.8;       // live tweak, no recompile
//
// Everything is evaluated in REST object space (the `position` / `normal` attributes, i.e. before
// skinning), so the scales stay glued to the skin while the skeleton animates. Instead of a
// triplanar projection of 2D cells we evaluate 3D cellular noise directly on the rest surface:
// cheaper than 3 projections, and there is no blend seam or stretching anywhere on the body.
//
// Layers (all procedural, combined into one height field whose analytic gradient bumps the normal):
//   1. pebble scales: smooth-bordered 3D power diagram (soft-min over the cell borders gives rounded,
//      polygonal "pebbles" of mixed sizes), size per body region plus low-frequency patches of larger /
//      smaller scales, snapped to octaves of uBaseScale (a second evaluation only where two octaves meet);
//   2. feature scales: sparse, larger, low round scales set INTO the pebble field (the pebbles around
//      one are cut along its rim and slightly enlarged, a rosette), denser on the head, along the dorsal
//      midline and inside optional hotspots (brow, cheek, nasal);
//   3. scutes: transverse rows across the front of the metatarsus and the top of the toes, with
//      staggered ends, occasional split rows and an imbricate step at the distal edge;
//   4. labial scales: rows of enlarged bead-like scales parallel to the closed mouth line on both lips,
//      shrinking row by row (needs the lip coordinates written by bakeSkinRegions);
//   5. skin folds: "billow" relief |n| of anisotropic, domain-warped gradient noise: sharp meandering
//      creases between fleshy rolls of uneven height, long along the fold lines, driven by the aFold
//      attribute (+ a global level) in three fixed rest-space directions weighted per region; plus a
//      web of fine wrinkles (zero lines of gradient noise) for the large smooth areas;
//   6. low-frequency fleshy lumps and a scale-cluster grain that survives at medium distance.
// Shading: countershaded albedo (perceptual blend, warm lateral band) + mottling + per-region tint,
// per-scale colour jitter, darker mortar/cracks, aCavity occlusion, roughness breakup, dust on the
// lower legs, eye / claw tissue from aFeature, and a cheap subsurface approximation (per-channel wrap
// lighting + the red channel lit with a smoother normal, the "pre-integrated skin" trick) injected
// into three's direct-light loop; reduced grazing Fresnel and crease-occluded direct specular (studio
// rim lights cast no shadows).
// Antialiasing: every layer fades out as its cells shrink below a few pixels (pixel footprint from
// screen-space derivatives of the rest position, geometric mean of the two axes, uDetailFade..
// uDetailFull), crease widths grow with the footprint, and the lost normal variance is put back as
// roughness, so medium / far shots do not shimmer.
//
// Science note: Tyrannosaurus skin impressions show small, non-imbricate, pebbly scales
// (Bell et al. 2017). Their real size is smaller than what is rendered by default here; the default
// sizes are a legibility choice for close-up renders (uncertain, tweak baseScale). Feature-scale
// placement and the foot scutes are inferred from extant archosaurs (birds, crocodilians), not from
// preserved tyrannosaur skin; the labial rows assume lips (Cullen et al. 2023, inferred) dressed like
// those of lipped extant reptiles (e.g. varanids). Colour is UNKNOWN for any non-avian dinosaur: the palette is a neutral
// countershaded guess and must be labelled speculative wherever it is shown.
//
// ------------------------------------------------------------------------------------------
// Geometry attributes read (all optional; a missing attribute reads as 0):
//   aRegion    float  index into SKIN_REGIONS (head, jaw, neck, torso, belly, tail, thigh, shin, foot, arm)
//   aCavity    float  0..1 cavity / occlusion estimate (darkens albedo, occludes ambient light)
//   aFold      float  0..1 skin-fold intensity (neck / joint wrinkles)
//   aFeature   float  0 skin, 1 eye, 2 claw (sdf_mesher FEATURES): glossy eye, keratin claw
//   aOcclusion float  0..1 occlusion by other body parts (ambient only)
//   aSkinA/B/C vec4   written by bakeSkinRegions (smoothed region tables)
//   aLip       vec2   written by bakeSkinRegions where head and jaw regions meet: (arc length along the
//                     mouth line from the snout tip, signed distance to it, + upper lip), metres
//   position, normal: REST pose (model space, metres; +x forward, +y up, +z animal's right)
//
// Options (createSkinMaterial(THREE, opts)) -> uniform of the same role; every uniform lives in
// material.userData.skinUniforms and can be changed at any time without recompiling. Lengths in
// metres of rest space (scaled by worldScale); "cell" = the local scale size.
//   physical        false    use MeshPhysicalMaterial (+ sheen for a dusty grazing sheen) instead of Standard
//   sheen           0.25     (physical only) sheen amount; sheenColor '#b9ab98', sheenRoughness 0.8
//   envMapIntensity 1        standard three.js material property
//   debug           0        debug view (see setDebug below)
//   -- pebble scales
//   seed            0        uSeed: shifts every cellular pattern (variants of the same skin)
//   baseScale       0.018    uBaseScale (m): reference pebble size; region sizes snap to its octaves (x0.5, x1, x2)
//   sizeNoise       0.35     uSizeNoise: octaves of low-frequency variation of the scale size (patches)
//   jitter          0.85     uJitter: 0 = regular grid of cells, 1 = fully random cells
//   sizeVar         0.35     uSizeVar: power-diagram weight range (0 = equal cells; ~0.5 = big scales among small ones)
//   mortar          0.035    uMortar: crack half-width between scales (fraction of a cell)
//   domeRadius      0.45     uDomeRadius: width of the rounded scale edge (fraction of a cell)
//   scaleHeight     0.11     uScaleHeight: scale relief height (fraction of the cell size)
//   softness        0.06     uSoftness: soft-min radius of the cell borders (fraction of a cell; rounds corners)
//   bumpStrength    1.0      uBumpStrength: global multiplier on all relief
//   -- feature scales
//   featureScale    0.075    uFeatureScale (m): lattice spacing of the feature-scale sites
//   featureSize     [0.2, 0.36]  uFeatureSize: radius range (fraction of featureScale; x0.7 on the face, x1 on the body)
//   featureHeight   0.1      uFeatureHeight: relief (fraction of the radius)
//   featureTint     -0.04    uFeatureTint: brightness change of the feature scales
//   rosette         0.22     uRosette: enlargement of the pebbles ringing a feature scale (power weight, cell^2)
//   hotspots        []       uHotspots[6]: [[x, y, z, radius], ...] rest-space spheres that densify feature scales
//                            and enlarge the pebbles (configureSkinForBones places brow / cheek / nasal ones)
//   hotspotBoost    0.45     uHotspotBoost: feature density added at a hotspot centre
//   hotspotSize     0.8      uHotspotSize: scale-size increase at a hotspot centre (octaves): a patch of larger scales
//   midlineBoost    1.6      uMidlineBoost: feature density multiplier along the dorsal midline (z ~ 0, normal up)
//   midlineWidth    0.12     uMidlineWidth (m): half-width of that dorsal row
//   -- foot scutes ([metatarsus, toes] pairs)
//   scuteLength     [0.055, 0.04]  uScuteLength (m): row length along the limb
//   scuteSpan       [1.45, 1.5]    uScuteSpan (rad): half-angle around the limb covered by a row, from the front
//   scuteRadius     [0.125, 0.07]  uScuteRadius (m): limb radius (converts angles to lengths; fitted by configureSkinForBones)
//   scuteHeight     0.12     uScuteHeight: plate relief (fraction of the row length)
//   scuteCurve      0.08     uScuteCurve: bow of the joints towards the sides (rows per rad^2)
//   scuteSplit      0.3      uScuteSplit: probability that a row is split in two
//   metaAxis        [0.32,-0.95,0]  uMetaAxis: distal direction of the metatarsus in rest space (configureSkinForBones)
//   toeAxis         [1,0,0]  uToeAxis: distal direction of the toes in rest space
//   toeHeight       0.22     uToeHeight (m): rest-space height below which the toe frame is used
//   -- folds and macro relief
//   foldDepth       0.06     uFoldDepth (m): relief of a full-strength fold (peak noise lobe; typical rolls ~40 %)
//   foldBase        0.2      uFoldBase: fold intensity everywhere (added to foldAttr * aFold, times the region weights)
//   foldAttr        0.8      uFoldAttr: weight of the aFold attribute
//   foldAniso       5.0      uFoldAniso: fold length / spacing ratio (noise stretched along the fold lines)
//   foldOctave      0.35     uFoldOctave: amplitude of the second, 2.13x finer fold octave (branching wrinkles)
//   foldWarp        0.06     uFoldWarp (m): low-frequency domain warp, makes the folds curve
//   foldDirs        [[1,0,0,6],[0,1,0,7],[0.8,0.6,0,6]]  uFoldDirs[3]: rest-space direction ACROSS each fold field +
//                            noise frequency (1/m; crease spacing ~0.7 / frequency)
//                            (x: tail / belly / flank transverse folds, y: limb wrinkles, diagonal: neck)
//   netScale        0.12     uNetScale (m): wavelength of the wrinkle web (gradient-noise zero lines, 2 octaves)
//   netDepth        0.0025   uNetDepth (m): depth of its grooves
//   netWidth        0.12     uNetWidth: groove half-width (in noise units, |n| < netWidth)
//   lumpDepth       0.006    uLumpDepth (m): amplitude of the low-frequency fleshy undulation
//   lumpScale       3.0      uLumpScale (1/m): frequency of that undulation
//   grainScale      0.045    uGrainScale (m): size of the scale-cluster grain kept at medium distance
//   grainDepth      0.0018   uGrainDepth (m): its relief
//   grainTint       0.1      uGrainTint: its brightness variation
//   -- labial scales (need aLip from bakeSkinRegions; uLip = (lipRow, lipShrink, lipRows, lipLength))
//   lipRow          0.03     uLip.x (m): height of the first row, at the lip margin
//   lipShrink       0.8      uLip.y: height ratio of each next row
//   lipRows         3        uLip.z: number of rows before the pebbles take over
//   lipLength       1.05     uLip.w (m): extent along the mouth line from the snout tip (about the tooth row of a
//                            ~1.4 m skull, plausible; rows fade out over the last 12 cm, at the mouth corner)
//   lipHeight       0.12     uLipHeight: relief (fraction of the row height); 0 disables the rows
//   -- colour (all speculative)
//   dorsalColor     '#322e2b'  uDorsalColor (sRGB): back / upper flanks
//   ventralColor    '#9c907d'  uVentralColor (sRGB): belly / underside
//   lateralTint     [1.06, 1.0, 0.94]  uLateralTint: multiplier on the band where the two tones meet
//   counterShade    [-0.7, 0.45]  uCounterShade: rest-normal-y range of the ventral -> dorsal transition
//   mottle          0.32     uMottle: low-frequency blotch contrast
//   mottleScale     1.6      uMottleScale (1/m): blotch frequency
//   scaleTint       0.08     uScaleTint: per-scale brightness jitter
//   topLight        0.07     uTopLight: lightening of the worn scale tops
//   crackDark       0.22     uCrackDark: darkening inside the mortar / cracks
//   cavityDark      0.35     uCavityDark: albedo darkening by aCavity
//   cavityAO        0.7      uCavityAO: ambient occlusion by aCavity
//   occlusionAO     0.35     uOcclusionAO: ambient occlusion by aOcclusion
//   dust            0.25     uDust: dust / dried mud on the lower legs and in the cracks there
//   dustHeight      0.7      uDustHeight (m): height up to which dust is present
//   dustColor       '#8d8270'  uDustColor (sRGB)
//   eyeColor        '#2b1a0b'  uEyeColor (sRGB): aFeature = 1
//   clawColor       '#2e2925'  uClawColor (sRGB): aFeature = 2
//   -- reflectance and scattering
//   roughness       0.68     uRoughness: base roughness
//   roughVar        0.1      uRoughVar: low-frequency roughness breakup
//   crackRough      0.18     uCrackRough: extra roughness in the cracks
//   topRough        -0.08    uTopRough: roughness change on polished scale tops
//   fadeRough       0.22     uFadeRough: roughness added where detail is faded (keeps the normal variance)
//   specular        0.65     uSpecular: direct-light specular multiplier (dry skin)
//   specF90         0.55     uSpecF90: Fresnel reflectance at grazing angles (1 = glassy rim; dry, dusty skin < 1)
//   specOcclusion   0.9      uSpecOcclusion: how much aCavity / aOcclusion / cracks / fold creases occlude DIRECT
//                            specular (studio rim and fill lights cast no shadows and would glint inside creases)
//   scatterColor    [1.0, 0.7, 0.55]  uScatterColor: per-channel wrap (red scatters furthest)
//   sss             0.22     uSSS: subsurface strength (wrap amount)
//   sssNormalBlur   0.45     uSSSNormalBlur: how much the red channel uses the un-bumped normal
//   translucency    0.0      uTranslucency: back-light glow at thin silhouettes (keep ~0 for a 7 t animal)
//   -- level of detail
//   detailFade      1.6      uDetailFade: pixels per cell at which a layer is fully faded
//   detailFull      8.0      uDetailFull: pixels per cell at which a layer is fully shown
//   worldScale      1.0      uWorldScale: object -> world scale, keeps the relief consistent if the mesh is scaled
//   -- regions
//   regions         {}       per-region overrides, e.g. { foot: { tint: [0.8, 0.8, 0.82], rough: 0.1 } } with fields
//                            size (m), feature (density 0..1), scute (0..1), bump (x), tint ([r,g,b] linear multiplier),
//                            rough (offset), folds ([wx, wy, wd] weights of the three fold fields of foldDirs)
//   -> uRegionParams[10] (x log2(size/baseScale), y feature density, z scute, w bump),
//      uRegionTint[10] (rgb tint, a roughness offset), uRegionFold[10] (xyz fold-field weights, w unused)
//
// material.userData: skinUniforms (all uniforms), skinRegions (editable region table; call
// updateRegions() or re-run bakeSkinRegions after editing), setDebug(mode): 0 off, 1 albedo,
// 2 normal, 3 roughness, 4 AO, 5 detail visibility, 6 region params, 7 crack / feature scales,
// 8 lip coordinates (r: fract(s / 5 cm), g: |d| / 10 cm, b: labial-row weight).
// ------------------------------------------------------------------------------------------

export const SKIN_REGIONS = ['head', 'jaw', 'neck', 'torso', 'belly', 'tail', 'thigh', 'shin', 'foot', 'arm'];

// Region defaults. Sizes relative to the 1.8 cm body scale: the face and jaw get finer scales
// (between the half and the full octave, so patches of both sizes mix) and the small forelimb
// half-size scales, as in extant archosaurs where facial and manual scales are finer (sizes are a
// legibility choice, see the science note). Feature densities: highest on the face (art direction
// from extant archosaurs, uncertain; the brow / cheek clusters come from the hotspots), none on the belly.
export const DEFAULT_REGION_PARAMS = {
  head:  { size: 0.013, feature: 0.3, scute: 0, bump: 1.0, tint: [1.00, 0.98, 0.95], rough: -0.04, folds: [0, 0, 0] },
  jaw:   { size: 0.012, feature: 0.2, scute: 0, bump: 0.9, tint: [1.12, 1.08, 1.02], rough: -0.05, folds: [0, 0, 0] },
  neck:  { size: 0.018, feature: 0.12, scute: 0, bump: 1.0, tint: [1.00, 1.00, 1.00], rough: 0.0, folds: [0, 0, 1] },
  torso: { size: 0.018, feature: 0.07, scute: 0, bump: 1.0, tint: [1.00, 1.00, 1.00], rough: 0.0, folds: [0.7, 0, 0] },
  belly: { size: 0.018, feature: 0.00, scute: 0, bump: 0.8, tint: [1.04, 1.03, 1.01], rough: 0.03, folds: [0.8, 0, 0] },
  tail:  { size: 0.018, feature: 0.06, scute: 0, bump: 1.0, tint: [0.98, 0.98, 0.98], rough: 0.02, folds: [1, 0, 0] },
  thigh: { size: 0.018, feature: 0.06, scute: 0, bump: 1.0, tint: [0.98, 0.97, 0.96], rough: 0.0, folds: [0, 0.35, 0] },
  shin:  { size: 0.018, feature: 0.05, scute: 0, bump: 1.0, tint: [0.86, 0.85, 0.85], rough: 0.04, folds: [0, 0.6, 0] },
  foot:  { size: 0.026, feature: 0.00, scute: 1, bump: 1.15, tint: [0.64, 0.64, 0.66], rough: 0.08, folds: [0, 0, 0] },
  arm:   { size: 0.009, feature: 0.05, scute: 0, bump: 1.0, tint: [0.97, 0.96, 0.95], rough: 0.0, folds: [0, 1, 0] },
};

const DEFAULTS = {
  physical: false, debug: 0, sheen: 0.25, sheenColor: '#b9ab98', sheenRoughness: 0.8, envMapIntensity: 1,
  seed: 0, baseScale: 0.018, sizeNoise: 0.35, jitter: 0.85, sizeVar: 0.35, mortar: 0.035, domeRadius: 0.45, scaleHeight: 0.11, softness: 0.06,
  bumpStrength: 1.0, featureScale: 0.075, featureHeight: 0.1, featureSize: [0.2, 0.36], rosette: 0.22, featureTint: -0.04,
  hotspots: [], hotspotBoost: 0.45, hotspotSize: 0.8, midlineBoost: 1.6, midlineWidth: 0.12,
  scuteLength: [0.055, 0.04], scuteSpan: [1.45, 1.5], scuteRadius: [0.125, 0.07], scuteHeight: 0.12, scuteCurve: 0.08, scuteSplit: 0.3,
  metaAxis: [0.32, -0.95, 0], toeAxis: [1, 0, 0], toeHeight: 0.22,
  foldDepth: 0.06, foldBase: 0.2, foldAniso: 5.0, foldOctave: 0.35, foldWarp: 0.06, foldDirs: [[1, 0, 0, 6], [0, 1, 0, 7], [0.8, 0.6, 0, 6]], lumpDepth: 0.006, lumpScale: 3.0, grainScale: 0.045, grainDepth: 0.0018, grainTint: 0.1, netScale: 0.12, netDepth: 0.0025, netWidth: 0.12,
  dorsalColor: '#322e2b', ventralColor: '#9c907d', lateralTint: [1.06, 1.0, 0.94], counterShade: [-0.7, 0.45], mottle: 0.32, mottleScale: 1.6,
  scaleTint: 0.08, topLight: 0.07, crackDark: 0.22, cavityDark: 0.35, cavityAO: 0.7,
  roughness: 0.68, roughVar: 0.1, crackRough: 0.18, topRough: -0.08, fadeRough: 0.22,
  scatterColor: [1.0, 0.7, 0.55], sss: 0.22, specular: 0.65, specF90: 0.55, specOcclusion: 0.9, sssNormalBlur: 0.45, translucency: 0.0,
  dust: 0.25, dustHeight: 0.7, dustColor: '#8d8270', detailFade: 1.6, detailFull: 8.0, worldScale: 1.0,
  eyeColor: '#2b1a0b', clawColor: '#2e2925', occlusionAO: 0.35, foldAttr: 0.8,
  lipRow: 0.03, lipShrink: 0.8, lipRows: 3, lipLength: 1.05, lipHeight: 0.12,
};

const MAX_HOTSPOTS = 6;

// ---------------------------------------------------------------------------------------------
// GLSL

const VERT_PARS = /* glsl */`
attribute float aRegion;
attribute float aCavity;
attribute float aFold;
attribute float aFeature;    // optional: 0 skin, 1 eye, 2 claw (sdf_mesher FEATURES)
attribute float aOcclusion;  // optional: 0..1 occlusion by other body parts
#ifdef SKIN_BAKED_REGIONS
attribute vec4 aSkinA;   // smoothed region params (see bakeSkinRegions)
attribute vec4 aSkinB;
attribute vec4 aSkinC;
#endif
#ifdef SKIN_LIP
attribute vec2 aLip;     // (along-lip arc length from the snout tip, signed distance to the lip line), m
varying vec2 vLip;
#endif
uniform vec4 uRegionParams[ 10 ];
uniform vec4 uRegionTint[ 10 ];
uniform vec4 uRegionFold[ 10 ];   // xyz weights of the 3 fold fields
uniform vec4 uHotspots[ ${MAX_HOTSPOTS} ];
uniform float uHotspotBoost;
uniform float uHotspotSize;
uniform float uMidlineBoost;
uniform float uMidlineWidth;
uniform float uFoldBase;
uniform float uFoldAttr;
varying vec3 vRestPos;
varying vec3 vRestNormal;
varying vec4 vSkinA;    // x log2(size / base), y tubercle density, z scute weight, w bump
varying vec4 vSkinB;    // rgb region tint, a roughness offset
varying vec4 vSkinFold; // xyz weights of the 3 fold fields, w fold amount
varying float vSkinCavity;
varying vec3 vSkinFeat;   // x eye, y claw, z occlusion
`;

// Region data is looked up per vertex and interpolated, so the borders between regions blend
// smoothly instead of interpolating a meaningless region index.
const VERT_MAIN = /* glsl */`
#include <begin_vertex>
{
	vRestPos = position;
	vRestNormal = normal;
#ifdef SKIN_LIP
	vLip = aLip;
#endif
#ifdef SKIN_BAKED_REGIONS
	vec4 rp = aSkinA;
	vSkinB = aSkinB;
	vec3 foldW = aSkinC.xyz;
#else
	int ri = clamp( int( aRegion + 0.5 ), 0, 9 );
	vec4 rp = uRegionParams[ ri ];
	vSkinB = uRegionTint[ ri ];
	vec3 foldW = uRegionFold[ ri ].xyz;
#endif
	float hot = 0.0;
	for ( int i = 0; i < ${MAX_HOTSPOTS}; i ++ ) {
		vec4 hs = uHotspots[ i ];
		if ( hs.w > 0.0 ) hot = max( hot, 1.0 - smoothstep( 0.35, 1.0, length( position - hs.xyz ) / hs.w ) );
	}
	float zz = position.z / uMidlineWidth;
	float midline = exp( - zz * zz ) * smoothstep( 0.5, 0.9, normal.y );
	// hotspots: denser feature scales AND a patch of larger scales (the cheek / brow clusters)
	vSkinA = vec4( rp.x + hot * uHotspotSize, rp.y * ( 1.0 + midline * uMidlineBoost ) + hot * uHotspotBoost, rp.z, rp.w * ( 1.0 + 0.25 * hot ) );
	vSkinFold = vec4( foldW, clamp( uFoldAttr * aFold + uFoldBase, 0.0, 1.0 ) );
	vSkinCavity = clamp( aCavity, 0.0, 1.0 );
	vSkinFeat = vec3( 1.0 - clamp( abs( aFeature - 1.0 ), 0.0, 1.0 ), 1.0 - clamp( abs( aFeature - 2.0 ), 0.0, 1.0 ), clamp( aOcclusion, 0.0, 1.0 ) );
}
`;

const FRAG_PARS = /* glsl */`
varying vec3 vRestPos;
varying vec3 vRestNormal;
varying vec4 vSkinA;
varying vec4 vSkinB;
varying vec4 vSkinFold;
varying float vSkinCavity;
varying vec3 vSkinFeat;
#ifdef SKIN_LIP
varying vec2 vLip;
#endif
uniform vec4 uLip;
uniform float uLipHeight;

uniform float uSeed, uBaseScale, uSizeNoise, uJitter, uSizeVar, uMortar, uDomeRadius, uScaleHeight, uSoftness, uBumpStrength;
uniform float uFeatureScale, uFeatureHeight, uRosette, uFeatureTint;
uniform vec2 uFeatureSize;
uniform vec3 uMetaAxis, uToeAxis;
uniform vec2 uScuteLength, uScuteSpan, uScuteRadius;
uniform float uScuteHeight, uScuteCurve, uScuteSplit, uToeHeight;
uniform float uFoldDepth, uFoldAniso, uFoldOctave, uFoldWarp, uLumpDepth, uLumpScale, uGrainScale, uGrainDepth, uGrainTint, uNetScale, uNetDepth, uNetWidth;
uniform vec4 uFoldDirs[ 3 ];
uniform vec3 uDorsalColor, uVentralColor, uLateralTint;
uniform vec2 uCounterShade;
uniform float uMottle, uMottleScale, uScaleTint, uTopLight, uCrackDark, uCavityDark, uCavityAO;
uniform float uRoughness, uRoughVar, uCrackRough, uTopRough, uFadeRough;
uniform vec3 uScatterColor;
uniform float uSSS, uSSSNormalBlur, uTranslucency, uSpecular, uSpecF90, uSpecOcclusion;
uniform vec3 uDustColor;
uniform float uDust, uDustHeight, uDetailFade, uDetailFull, uWorldScale;
uniform vec3 uEyeColor, uClawColor;
uniform float uOcclusionAO;

// Globals shared between the pattern evaluation and the custom light function.
vec3 skSmoothN;     // view-space normal with the macro relief only (folds / lumps): used by the SSS red channel
float skAO;
float skSpecOcc;    // direct-specular occlusion (creases, body-part occlusion): lights without shadows leak there
float skSkinW;      // 1 on skin, 0 on eyes / claws (no scattering there)
float skDebugVis;   // detail visibility, for the debug view
vec3 skDebugFeat;   // crack, feature-scale mask, nearest feature site (debug view 7)
vec3 skDebugLip = vec3( 0.0 );   // lip coordinates: fract(s / 5 cm), |d| / 10 cm, lip-row weight (debug view 8)

// Hash without sine (D. Hoskins): stable in fp32 for the cell indices we use (|i| < 10^4).
vec3 skHash33( vec3 p ) {
	p = fract( p * vec3( 0.1031, 0.1030, 0.0973 ) );
	p += dot( p, p.yxz + 33.33 );
	return fract( ( p.xxy + p.yxx ) * p.zyx );
}
float skHash13( vec3 p ) {
	p = fract( p * 0.1031 );
	p += dot( p, p.zyx + 31.32 );
	return fract( ( p.x + p.y ) * p.z );
}

// Value noise with analytic derivatives (I. Quilez): returns (value in -1..1, d/dx, d/dy, d/dz).
vec4 skNoised( vec3 x ) {
	vec3 i = floor( x ), w = fract( x );
	vec3 u = w * w * w * ( w * ( w * 6.0 - 15.0 ) + 10.0 );
	vec3 du = 30.0 * w * w * ( w * ( w - 2.0 ) + 1.0 );
	float a = skHash13( i ), b = skHash13( i + vec3( 1, 0, 0 ) ), c = skHash13( i + vec3( 0, 1, 0 ) ), d = skHash13( i + vec3( 1, 1, 0 ) );
	float e = skHash13( i + vec3( 0, 0, 1 ) ), f = skHash13( i + vec3( 1, 0, 1 ) ), g = skHash13( i + vec3( 0, 1, 1 ) ), h = skHash13( i + vec3( 1, 1, 1 ) );
	float k1 = b - a, k2 = c - a, k3 = e - a, k4 = a - b - c + d, k5 = a - c - e + g, k6 = a - b - e + f, k7 = - a + b + c - d + e - f - g + h;
	float v = a + k1 * u.x + k2 * u.y + k3 * u.z + k4 * u.x * u.y + k5 * u.y * u.z + k6 * u.z * u.x + k7 * u.x * u.y * u.z;
	vec3 dv = du * vec3( k1 + k4 * u.y + k6 * u.z + k7 * u.y * u.z, k2 + k5 * u.z + k4 * u.x + k7 * u.z * u.x, k3 + k6 * u.x + k5 * u.y + k7 * u.x * u.y );
	return vec4( 2.0 * v - 1.0, 2.0 * dv );
}

// Gradient noise with analytic derivatives (I. Quilez, MIT): returns (value ~ -1..1, d/dx, d/dy, d/dz).
// Used for the skin folds: its zero lines meander without the axis-aligned streaks of value noise.
vec4 skGnoised( vec3 x ) {
	vec3 i = floor( x ), f = fract( x );
	vec3 u = f * f * f * ( f * ( f * 6.0 - 15.0 ) + 10.0 );
	vec3 du = 30.0 * f * f * ( f * ( f - 2.0 ) + 1.0 );
	vec3 ga = skHash33( i ) * 2.0 - 1.0, gb = skHash33( i + vec3( 1, 0, 0 ) ) * 2.0 - 1.0;
	vec3 gc = skHash33( i + vec3( 0, 1, 0 ) ) * 2.0 - 1.0, gd = skHash33( i + vec3( 1, 1, 0 ) ) * 2.0 - 1.0;
	vec3 ge = skHash33( i + vec3( 0, 0, 1 ) ) * 2.0 - 1.0, gf = skHash33( i + vec3( 1, 0, 1 ) ) * 2.0 - 1.0;
	vec3 gg = skHash33( i + vec3( 0, 1, 1 ) ) * 2.0 - 1.0, gh = skHash33( i + vec3( 1, 1, 1 ) ) * 2.0 - 1.0;
	float va = dot( ga, f ), vb = dot( gb, f - vec3( 1, 0, 0 ) ), vc = dot( gc, f - vec3( 0, 1, 0 ) ), vd = dot( gd, f - vec3( 1, 1, 0 ) );
	float ve = dot( ge, f - vec3( 0, 0, 1 ) ), vf = dot( gf, f - vec3( 1, 0, 1 ) ), vg = dot( gg, f - vec3( 0, 1, 1 ) ), vh = dot( gh, f - vec3( 1, 1, 1 ) );
	float k7 = - va + vb + vc - vd + ve - vf - vg + vh;
	float v = va + u.x * ( vb - va ) + u.y * ( vc - va ) + u.z * ( ve - va ) + u.x * u.y * ( va - vb - vc + vd ) + u.y * u.z * ( va - vc - ve + vg ) + u.z * u.x * ( va - vb - ve + vf ) + k7 * u.x * u.y * u.z;
	vec3 d = ga + u.x * ( gb - ga ) + u.y * ( gc - ga ) + u.z * ( ge - ga ) + u.x * u.y * ( ga - gb - gc + gd ) + u.y * u.z * ( ga - gc - ge + gg ) + u.z * u.x * ( ga - gb - ge + gf ) + ( - ga + gb + gc - gd + ge - gf - gg + gh ) * u.x * u.y * u.z
		+ du * ( vec3( vb - va, vc - va, ve - va ) + u.yzx * vec3( va - vb - vc + vd, va - vc - ve + vg, va - vb - ve + vf ) + u.zxy * vec3( va - vb - ve + vf, va - vb - vc + vd, va - vc - ve + vg ) + u.yzx * u.zxy * k7 );
	return vec4( 2.0 * v, 2.0 * d );
}

// Feature-scale sites: the two nearest "selected" points of a coarse jittered lattice (cell
// uFeatureScale). A lattice point is selected when its random number is below the local density;
// its radius grows from 0 as the density rises past that number (full size at ~1.3x), so a feature
// scale never appears cut in half where the density varies across the body. s = (centre in rest metres, radius in m;
// radius 0 = no site), rnd = 3 random numbers of the site.
void skFeatureSites( vec3 p, float density, float rScale, out vec4 s1, out vec4 s2, out vec3 rnd1, out vec3 rnd2 ) {
	vec3 q = p / uFeatureScale;
	vec3 iq = floor( q ), fq = q - iq;
	float d1 = 1e9, d2 = 1e9;
	s1 = vec4( 0.0 ); s2 = vec4( 0.0 ); rnd1 = vec3( 0.5 ); rnd2 = vec3( 0.5 );
	for ( int k = 0; k < 27; k ++ ) {
		vec3 gg = vec3( float( k % 3 ) - 1.0, float( ( k / 3 ) % 3 ) - 1.0, float( k / 9 ) - 1.0 );
		vec3 c = iq + gg;
		vec3 hc = skHash33( c + uSeed + 5.13 );
		float grow = smoothstep( 0.0, 0.25, ( density - hc.x ) / max( density, 0.02 ) );
		if ( grow <= 0.0 ) continue;
		vec3 fp = gg + 0.5 + 0.5 * ( skHash33( c + uSeed + 19.7 ) - 0.5 );
		float R = mix( uFeatureSize.x, uFeatureSize.y, hc.y ) * grow * rScale;
		float d = length( fp - fq ) - R;
		vec4 s = vec4( ( iq + fp ) * uFeatureScale, R * uFeatureScale );
		if ( d < d1 ) { d2 = d1; s2 = s1; rnd2 = rnd1; d1 = d; s1 = s; rnd1 = hc; }
		else if ( d < d2 ) { d2 = d; s2 = s; rnd2 = hc; }
	}
}

// Irregular outline of a feature scale: radius factor as a function of the direction from its centre.
float skWobble( vec3 dir, vec3 fr ) {
	return 1.0 + 0.06 * sin( 5.0 * dir.x + 3.0 * dir.y + 6.28 * fr.z ) + 0.04 * sin( 7.0 * dir.z - 4.0 * dir.x + 6.28 * fr.y );
}

// Rounded polygonal "pebble" scales of a given size (m), as a smooth-bordered power diagram (each
// cell has a weight w, "distance" = |r|^2 - w: the borders stay planar but cells grow or shrink,
// giving the natural mix of larger scales among small ones instead of a uniform cobblestone), with up to two round feature scales (sites f1, f2) set into the pebble field: pebble
// cells whose feature point falls inside a feature scale are removed, so the surrounding pebbles
// grow up to its rim and are cut along it, the way small scales fit around an enlarged scale in
// archosaur skin, and pebbles in a ring around it are enlarged (uRosette, rosette arrangement).
// Outputs relief h (m) and its rest-space gradient g, crack mask (1 in the mortar), top mask
// (1 on the crown), a per-scale random value, and feat (1 on a feature scale).
void skPebbles( vec3 p, float size, vec4 f1, vec4 f2, vec3 fr1, vec3 fr2, out float h, out vec3 g, out float crack, out float top, out float rnd, out float feat ) {
	// signed distance to the feature-scale outlines (m, < 0 inside)
	float sd = 1e3;
	vec3 sn = vec3( 0.0 );
	int sid = 0;
	if ( f1.w > 0.0 ) {
		vec3 dc = p - f1.xyz; float l = max( length( dc ), 1e-6 );
		float d = l - f1.w * skWobble( dc / l, fr1 );
		if ( d < sd ) { sd = d; sn = dc / l; sid = 1; }
	}
	if ( f2.w > 0.0 ) {
		vec3 dc = p - f2.xyz; float l = max( length( dc ), 1e-6 );
		float d = l - f2.w * skWobble( dc / l, fr2 );
		if ( d < sd ) { sd = d; sn = dc / l; sid = 2; }
	}
	if ( sd < 0.0 ) {
		// on a feature scale: low convex crown with a rounded rim
		vec4 f = sid == 1 ? f1 : f2;
		vec3 fr = sid == 1 ? fr1 : fr2;
		float R = f.w;
		vec3 dc = p - f.xyz;
		float c2 = min( dot( dc, dc ) / ( R * R ), 1.0 );
		float crown = 1.0 - 0.4 * c2;
		vec3 dcrown = c2 < 1.0 ? - 0.8 * dc / ( R * R ) : vec3( 0.0 );
		float e = - sd;
		float m = uMortar * size * 1.2;
		float rim = 0.3 * R + 0.3 * size;
		float x = clamp( ( e - m ) / rim, 0.0, 1.0 );
		float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
		float dprof = ( e > m && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / rim : 0.0;
		float amp = uFeatureHeight * R * ( 0.6 + 0.8 * fr.y );
		h = amp * prof * crown;
		g = amp * ( - dprof * sn * crown + prof * dcrown );
		g += ( fr - 0.5 ) * 0.25 * uFeatureHeight * prof;     // each feature scale tilted a little
		// fine pitting / wrinkling of the crown: large scales are not polished beads
		vec4 nm = skNoised( p / ( 0.5 * size ) + fr * 17.0 );
		h += 0.04 * size * nm.x * prof;
		g += 0.08 * nm.yzw * prof;
		crack = 1.0 - smoothstep( 0.0, m * 1.8, e );
		top = prof * crown;
		rnd = fr.z;
		feat = 1.0;
		return;
	}
	vec3 q = p / size;
	vec3 iq = floor( q ), fq = q - iq;
	vec4 r[ 27 ];                                            // xyz = feature - q, w = power distance
	float dmin = 1e9;
	int imin = 13;
	bool any = f1.w > 0.0;
	for ( int k = 0; k < 27; k ++ ) {
		vec3 gc = vec3( float( k % 3 ) - 1.0, float( ( k / 3 ) % 3 ) - 1.0, float( k / 9 ) - 1.0 );
		vec3 o = skHash33( iq + gc + uSeed );
		vec3 rr = gc + 0.5 + uJitter * ( o - 0.5 ) - fq;
		float wt = fract( dot( o, vec3( 13.17, 7.71, 3.37 ) ) );
		float d = dot( rr, rr ) - uSizeVar * wt * wt;
		if ( any ) {
			vec3 fp = p + rr * size;                         // the cell's feature point, rest metres
			float m1 = f1.w > 0.0 ? length( fp - f1.xyz ) - 0.92 * f1.w : 1e3;
			float m2 = f2.w > 0.0 ? length( fp - f2.xyz ) - 0.92 * f2.w : 1e3;
			float md = min( m1, m2 );
			d = md < 0.0 ? 1e6 : d - uRosette * ( 1.0 - smoothstep( 0.0, 1.6 * size, md ) );
		}
		r[ k ] = vec4( rr, d );
		if ( d < dmin ) { dmin = d; imin = k; }
	}
	vec3 r1 = r[ imin ].xyz;
	float w1 = r[ imin ].w;
	float soft = uSoftness * size, inv = 1.0 / soft;
	// the rim of a nearby feature scale is one more border of this cell
	float s = sd < 1e2 ? exp( - sd * inv ) : 0.0;
	vec3 gs = - s * sn;
	for ( int k = 0; k < 27; k ++ ) {
		if ( k == imin || r[ k ].w > 1e5 ) continue;
		vec3 dr = r[ k ].xyz - r1;
		float il = inversesqrt( dot( dr, dr ) );
		float e = 0.5 * ( r[ k ].w - w1 ) * il * size;      // metric distance to that border plane
		float wgt = exp( - e * inv );
		s += wgt;
		gs += wgt * il * dr;
	}
	float e = s > 1e-25 ? - soft * log( s ) : 0.0;
	vec3 eg = s > 1e-25 ? - gs / s : vec3( 0.0 );
	vec3 g1 = vec3( float( imin % 3 ) - 1.0, float( ( imin / 3 ) % 3 ) - 1.0, float( imin / 9 ) - 1.0 );
	vec3 cr = skHash33( iq + g1 + uSeed + 71.31 );
	float ec = e / size;                                     // border distance in cell units
	float m = uMortar * ( 0.7 + 0.6 * cr.y );
	float x = clamp( ( ec - m ) / uDomeRadius, 0.0, 1.0 );
	float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
	float dprof = ( ec > m && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / uDomeRadius : 0.0;
	float amp = uScaleHeight * ( 0.6 + 0.8 * cr.x );
	h = amp * prof * size;
	g = amp * dprof * eg;                                    // dh/dp = amp*size*dprof * (eg/size)
	g += ( cr - 0.5 ) * ( 0.35 * uScaleHeight / 0.13 ) * prof; // per-scale tilt breaks up the specular
	crack = 1.0 - smoothstep( 0.0, m * 1.8, ec );
	top = prof;
	rnd = cr.z;
	feat = 0.0;
}

// Smooth stand-in for a feature scale once the pebbles are below pixel size (keeps the bumps
// visible at medium distance without evaluating the pebble cells).
void skFeatureDisc( vec3 p, vec4 f, vec3 fr, inout float h, inout vec3 g, inout float m ) {
	if ( f.w <= 0.0 ) return;
	vec3 dc = p - f.xyz;
	float R = f.w * 1.1;
	float x2 = dot( dc, dc ) / ( R * R );
	if ( x2 >= 1.0 ) return;
	float b = 1.0 - x2;
	float amp = uFeatureHeight * f.w * ( 0.6 + 0.8 * fr.y );
	h += amp * b * b;
	g += amp * 2.0 * b * ( - 2.0 * dc / ( R * R ) );
	m = max( m, smoothstep( 0.0, 0.5, b ) );
}

// Transverse scutes (scutate scales) across the front of a limb segment, as on the metatarsus and
// toes of birds and crocodilians (extant phylogenetic bracket; tyrannosaurid foot scutes are
// inferred, not preserved in detail). Rows are a jittered 1-D cell sequence along the limb axis;
// the joints curve slightly, every row has its own lateral extent (staggered ends) and some rows
// are split in two. The profile is asymmetric like overlapping scutes: each plate rises towards
// its free distal edge, which ends in a sharp step.
//   axis: distal direction; R: limb radius (m); len: row length along the axis (m);
//   span: half-angle around the limb covered by a full row (rad); key: decorrelates feet / toes;
//   aa: antialiasing width (m). on = 1 where the pixel lies on a scute (else pebbles);
//   eLat = signed lateral distance to the end of the row (m, > 0 on the scute).
void skScutes( vec3 p, vec3 nR, vec3 axis, float R, float len, float span, float key, float aa, out float h, out vec3 g, out float crack, out float top, out float rnd, out float on, out float eLat ) {
	vec3 F = normalize( cross( vec3( 0.0, 0.0, 1.0 ), axis ) );
	vec3 S = cross( axis, F );
	vec3 nP = nR - axis * dot( nR, axis );
	float nl = length( nP );
	nP = nl > 1e-4 ? nP / nl : F;
	float phi = atan( dot( nP, S ), dot( nP, F ) );         // angle around the limb from the front
	vec3 Tl = cross( axis, nP );                              // surface direction of increasing phi
	vec3 dphi = Tl / R;
	float ax = dot( p, axis ) / len;
	float wob = 0.06 * sin( 3.1 * phi + 0.7 * ax + key );
	float t = ax - uScuteCurve * phi * phi + wob;
	vec3 dt = axis / len - 2.0 * uScuteCurve * phi * dphi + 0.06 * cos( 3.1 * phi + 0.7 * ax + key ) * ( 3.1 * dphi + 0.7 * axis / len );
	// jittered cut points c(i) = i + j * (hash - 0.5): rows of unequal length
	float it = floor( t );
	float j = 0.35;
	float cA = it + j * ( skHash13( vec3( it, key, uSeed + 3.7 ) ) - 0.5 );
	float cB = it + 1.0 + j * ( skHash13( vec3( it + 1.0, key, uSeed + 3.7 ) ) - 0.5 );
	float row = it, a = cA, b = cB;
	if ( t < cA ) { row = it - 1.0; b = cA; a = it - 1.0 + j * ( skHash13( vec3( it - 1.0, key, uSeed + 3.7 ) ) - 0.5 ); }
	else if ( t >= cB ) { row = it + 1.0; a = cB; b = it + 2.0 + j * ( skHash13( vec3( it + 2.0, key, uSeed + 3.7 ) ) - 0.5 ); }
	vec3 hr = skHash33( vec3( row, key, uSeed + 9.1 ) );
	// lateral ends of this row (staggered from row to row)
	float pL = - span * ( 0.72 + 0.45 * hr.x ), pR = span * ( 0.72 + 0.45 * hr.y );
	float eL = ( phi - pL ) * R, eR = ( pR - phi ) * R;
	eLat = min( eL, eR );
	vec3 gLat = eL < eR ? Tl : - Tl;
	// edges: proximal / distal joint, lateral end, optional split; soft-min rounds the corners
	float eP = ( t - a ) * len, eD = ( b - t ) * len;
	float k = 0.1 * len, ik = 1.0 / k;
	float w0 = exp( - eP * ik ), w1 = exp( - eD * ik ), w2 = exp( - max( eLat, 0.0 ) * ik ), w3 = 0.0;
	vec3 gsum = w0 * len * dt - w1 * len * dt + w2 * gLat;
	if ( hr.z < uScuteSplit ) {
		float ps = ( fract( hr.x * 7.13 + hr.y * 3.1 ) - 0.5 ) * span * 0.9;
		float eS = abs( phi - ps ) * R;
		w3 = exp( - eS * ik );
		gsum += w3 * sign( phi - ps ) * Tl;
	}
	float ws = w0 + w1 + w2 + w3;
	float e = - k * log( ws );
	vec3 eg = gsum / ws;
	float u = clamp( ( t - a ) / ( b - a ), 0.0, 1.0 );
	float mort = 0.045 * len;
	float Rr = len * mix( 0.5, 0.16, smoothstep( 0.35, 0.75, u ) ); // gentle proximal ramp, sharp distal step
	float x = clamp( ( e - mort ) / Rr, 0.0, 1.0 );
	float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
	float dprof = ( e > mort && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / Rr : 0.0;
	float slope = 0.45 + 0.55 * u;
	float pc = phi / max( span, 0.2 );
	float conv = 1.0 - 0.3 * pc * pc;                         // plates are convex across the limb
	float amp = uScuteHeight * len * ( 0.8 + 0.4 * hr.y );
	h = amp * prof * slope * conv;
	g = amp * ( dprof * eg * slope * conv + prof * 0.55 / ( b - a ) * dt * conv - prof * slope * 0.6 * pc / max( span, 0.2 ) * dphi );
	g += ( hr - 0.5 ) * 0.12 * uScuteHeight * prof;          // each plate slightly tilted
	// fine wear / wrinkling on the plate surface
	vec4 nm = skNoised( p / ( 0.12 * len ) + hr * 13.0 );
	h += 0.01 * len * nm.x * prof;
	g += 0.083 * nm.yzw * prof;
	crack = 1.0 - smoothstep( 0.0, mort * 1.8 + aa, e );
	top = prof;
	rnd = hr.z;
	on = smoothstep( - aa, aa, eLat );
}

// Fades a layer out as its cells shrink from uDetailFull to uDetailFade pixels across.
float skVisible( float size, float pix ) {
	return smoothstep( uDetailFade, uDetailFull, size / max( pix, 1e-6 ) );
}

// Labial scales: rows of enlarged, rounded-rectangular scales running parallel to the closed mouth
// line on the upper and lower lips, the largest at the lip margin and shrinking row by row until
// they merge into the pebbles. Lipped extant reptiles (e.g. varanid lizards) carry such rows; with
// lips covering the teeth (Cullen et al. 2023, inferred for Tyrannosaurus) they are a plausible
// art-direction inference, not preserved tyrannosaur skin.
//   sd = (s: arc length along the lip from the snout tip, d: signed distance to the lip line, + upper
//   lip), metres; gS / gD = their rest-space surface gradients. Row k is uLip.x * uLip.y^k high.
//   Outputs: wl = weight of the lip layer (0 outside the band), relief gradient g, crack, top, rnd, vis.
void skLipRows( vec2 sd, vec3 gS, vec3 gD, float pix, out float wl, out vec3 g, out float crack, out float top, out float rnd, out float vis ) {
	float L = uLip.x, q = uLip.y;
	float ad = abs( sd.y );
	float sideK = sd.y < 0.0 ? 17.0 : 0.0;                    // upper / lower lip: different rows
	vec3 gA = sd.y < 0.0 ? - gD : gD;                          // gradient of |d|
	float W = L * ( 1.0 - pow( q, uLip.z ) ) / ( 1.0 - q );      // width of the whole band
	float a = max( 1.0 - ad * ( 1.0 - q ) / L, 0.02 );
	float kap = log( a ) / log( q );                           // continuous row coordinate
	float dkap = ( 1.0 - q ) / ( L * a * abs( log( q ) ) );    // d kap / d|d|
	float row = floor( kap );
	float rowH = L * pow( q, row );
	wl = ( 1.0 - smoothstep( W - 0.5 * rowH, W, ad ) ) * ( 1.0 - smoothstep( uLip.w - 0.12, uLip.w, abs( sd.x ) ) );
	vis = skVisible( rowH, pix );
	g = vec3( 0.0 ); crack = 0.0; top = 0.0; rnd = 0.5;
	if ( wl <= 0.0 ) return;
	vec3 hr = skHash33( vec3( row, sideK, uSeed + 41.7 ) );
	float cl = rowH * ( 1.0 + 0.3 * hr.x );                    // scale length along the lip
	float sg = sd.x / cl + hr.y;                               // staggered from row to row
	// jittered cut points along the row: scales of unequal length
	float it = floor( sg );
	float j = 0.3;
	float cA = it + j * ( skHash13( vec3( it, row + sideK, uSeed + 5.9 ) ) - 0.5 );
	float cB = it + 1.0 + j * ( skHash13( vec3( it + 1.0, row + sideK, uSeed + 5.9 ) ) - 0.5 );
	float ci = it;
	if ( sg < cA ) { ci = it - 1.0; cB = cA; cA = it - 1.0 + j * ( skHash13( vec3( it - 1.0, row + sideK, uSeed + 5.9 ) ) - 0.5 ); }
	else if ( sg >= cB ) { ci = it + 1.0; cA = cB; cB = it + 2.0 + j * ( skHash13( vec3( it + 2.0, row + sideK, uSeed + 5.9 ) ) - 0.5 ); }
	vec3 hc = skHash33( vec3( ci, row + sideK, uSeed + 3.3 ) );
	// metric distances to the four edges and their gradients; soft-min rounds the corners
	float fr = kap - row;
	float e0 = fr * rowH, e1 = ( 1.0 - fr ) * rowH;
	float e2 = ( sg - cA ) * cl, e3 = ( cB - sg ) * cl;
	vec3 g01 = rowH * dkap * gA;
	float sz = min( rowH, cl );
	float k = 0.2 * sz, ik = 1.0 / k;                          // generous corner rounding: beads, not bricks
	float w0 = exp( - e0 * ik ), w1 = exp( - e1 * ik ), w2 = exp( - e2 * ik ), w3 = exp( - e3 * ik );
	float ws = w0 + w1 + w2 + w3;
	float e = - k * log( ws );
	vec3 eg = ( ( w0 - w1 ) * g01 + ( w2 - w3 ) * gS ) / ws;
	float mort = uMortar * sz * 1.3;
	float dome = 0.5 * sz;                                     // domed right to the centre
	float x = clamp( ( e - mort ) / dome, 0.0, 1.0 );
	float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
	float dprof = ( e > mort && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / dome : 0.0;
	float amp = uLipHeight * rowH * ( 0.75 + 0.5 * hc.x );
	g = amp * dprof * eg;
	g += ( hc - 0.5 ) * 0.3 * uLipHeight * prof;               // each scale a little tilted
	vec4 nm = skNoised( vRestPos / ( 0.3 * rowH ) + hc * 11.0 ); // fine pitting of the crown
	g += 0.05 * nm.yzw * prof;
	crack = 1.0 - smoothstep( 0.0, mort * 1.8, e );
	top = prof;
	rnd = hc.z;
}


// Unnormalised Mikkelsen bump: dHdxy = screen derivatives of the height (same units as surf_pos).
vec3 skPerturb( vec3 sigX, vec3 sigY, vec3 n, vec2 dHdxy, float faceDir ) {
	vec3 R1 = cross( sigY, n );
	vec3 R2 = cross( n, sigX );
	float det = dot( sigX, R1 ) * faceDir;
	vec3 grad = sign( det ) * ( dHdxy.x * R1 + dHdxy.y * R2 );
	return normalize( abs( det ) * n - grad );
}
`;

// Evaluated right after <color_fragment>: produces albedo, roughness, AO and the relief gradients.
const FRAG_EVAL = /* glsl */`
#include <color_fragment>
vec3 skRestDx = dFdx( vRestPos );
vec3 skRestDy = dFdy( vRestPos );
vec3 skSigX = dFdx( - vViewPosition );
vec3 skSigY = dFdy( - vViewPosition );
#ifdef SKIN_LIP
vec2 skLipDx = dFdx( vLip ), skLipDy = dFdy( vLip );   // outside any branch: derivatives need uniform flow
#endif
vec3 skGradDetail = vec3( 0.0 );
vec3 skGradMacro = vec3( 0.0 );
float skRoughness;
{
	vec3 p = vRestPos;
	vec3 nR = normalize( vRestNormal );
	// metres per pixel on the rest surface. The geometric mean of the two footprint axes (not the
	// max) keeps detail on surfaces seen at grazing angles, where the long axis alone would fade
	// every scale and leave a smooth, glassy rim highlight; bounded to 1/4 of the long axis.
	float pxA = length( skRestDx ), pxB = length( skRestDy );
	float pix = max( sqrt( pxA * pxB ), 0.25 * max( pxA, pxB ) );

	// ---- low-frequency lumps + mottling (shared noise)
	vec4 n1 = skNoised( p * uLumpScale + uSeed * 1.7 );
	vec4 n2 = skNoised( p * uLumpScale * 2.7 + 11.3 );
	float lumpVis = skVisible( 1.0 / uLumpScale, pix );
	skGradMacro += uLumpDepth * ( uLumpScale * n1.yzw + 0.45 * uLumpScale * 2.7 * n2.yzw ) * lumpVis;
	float n3 = skNoised( p * uMottleScale + 3.7 ).x;
	float mottle = 0.55 * n3 + 0.3 * n1.x + 0.15 * n2.x;
	// skin grain: clusters of scales a few cm across. Survives when the individual scales have
	// faded, so medium / far shots keep a fleshy, non-plastic surface.
	vec4 n4 = skNoised( p / uGrainScale + 23.1 + uSeed );
	float grainVis = skVisible( uGrainScale, pix );
	skGradMacro += uGrainDepth / uGrainScale * n4.yzw * grainVis;

	// ---- skin folds: "billow" relief |n| of anisotropic gradient noise. Its zero lines become sharp
	// creases and the noise lobes between them rounded rolls of uneven height; stretching the noise
	// along the fold lines (uFoldAniso) makes long, meandering, roughly parallel folds that end and
	// branch on their own, like the rolls of a flexed neck or tail. Three fields with FIXED rest-space
	// directions are weighted per region (interpolating a direction between regions would shear the
	// pattern into a seam along every region border).
	float foldCrease = 0.0;
	if ( vSkinFold.w > 0.005 && dot( vSkinFold.xyz, vec3( 1.0 ) ) > 0.01 ) {
		// low-frequency domain warp: folds curve instead of running straight (Jacobian ignored, small)
		vec3 pw = p + uFoldWarp * vec3( n1.x, n3, 0.8 * n1.x - 0.6 * n3 );
		for ( int i = 0; i < 3; i ++ ) {
			float wi = vSkinFold[ i ];
			if ( wi < 0.01 ) continue;
			vec3 Kd = uFoldDirs[ i ].xyz;
			float fq = uFoldDirs[ i ].w;
			float fvis = skVisible( 0.6 / fq, pix );
			if ( fvis <= 0.0 ) continue;
			// J: fq across the folds (along Kd), fq / aniso along them
			float fl = fq / uFoldAniso;
			mat3 J = mat3( fl ) + ( fq - fl ) * outerProduct( Kd, Kd );
			float a = wi * vSkinFold.w * fvis;
			for ( int o = 0; o < 2; o ++ ) {
				float oa = o == 0 ? 1.0 : uFoldOctave;
				float of = o == 0 ? 1.0 : 2.13;
				if ( oa <= 0.0 ) continue;
				vec4 nz = skGnoised( of * ( J * pw ) + vec3( 7.3, 1.9, 4.4 ) * float( i + 3 * o ) + uSeed );
				vec3 dn = of * ( J * nz.yzw );                     // J is symmetric
				// crease softness grows with the pixel footprint: soft at distance, no aliasing
				float e = clamp( 1.5 * pix * length( dn ), 0.04, 0.6 );
				float sa = sqrt( nz.x * nz.x + e * e );
				skGradMacro += uFoldDepth * a * oa * ( nz.x / sa ) * dn;
				foldCrease = max( foldCrease, a * oa * ( 1.0 - smoothstep( 0.0, 0.18 + e, abs( nz.x ) ) ) );
			}
		}
	}

	// ---- scale layers
	float h = 0.0, crack = 0.0, top = 0.0, rnd = 0.5, feat = 0.0;
	vec3 g = vec3( 0.0 );
	// feature-scale sites (shared by both pebble octaves so their outlines agree)
	vec4 f1 = vec4( 0.0 ), f2 = vec4( 0.0 );
	vec3 fr1 = vec3( 0.5 ), fr2 = vec3( 0.5 );
	float visF = skVisible( uFeatureScale * uFeatureSize.y * 2.0, pix ) * ( 1.0 - smoothstep( 0.2, 0.6, vSkinA.z ) );
	// feature scales scale (softly) with the local scale size: finer on the face than on the body
	if ( vSkinA.y > 0.005 && visF > 0.0 ) skFeatureSites( p, min( vSkinA.y, 0.9 ), exp2( 0.5 * clamp( vSkinA.x, - 2.0, 1.0 ) ), f1, f2, fr1, fr2 );
	// patches of larger and smaller scales (uSizeNoise octaves of low-frequency variation)
	float L = clamp( vSkinA.x + uSizeNoise * ( 0.7 * n1.x + 0.3 * n3 ), - 3.0, 3.0 );
	float lo = floor( L );
	// dithered (|d| < 0.15 keeps octave crossings continuous): scale sizes interlock at region borders
	float tB = smoothstep( 0.38, 0.62, L - lo + 0.12 * n2.x );
	float sizeA = uBaseScale * exp2( lo ), sizeB = sizeA * 2.0;
	float visA = skVisible( sizeA, pix ), visB = skVisible( sizeB, pix );
	float vis = mix( visA, visB, tB );
	for ( int oct = 0; oct < 2; oct ++ ) {
		float wo = oct == 0 ? 1.0 - tB : tB;
		float vo = oct == 0 ? visA : visB;
		if ( wo < 0.001 || vo <= 0.0 ) continue;
		float h0, c0, t0, r0, ft0; vec3 g0;
		skPebbles( p, oct == 0 ? sizeA : sizeB, f1, f2, fr1, fr2, h0, g0, c0, t0, r0, ft0 );
		// feature scales are large: their relief survives longer than the pebbles' (visF)
		float wv = wo * vo * ( ft0 > 0.5 ? visF : 1.0 );
		h += h0 * wv; g += g0 * wv;
		crack += c0 * wo; top += t0 * wo; feat += ft0 * wo;
		rnd = oct == 0 ? r0 : mix( rnd, r0, step( 0.5, tB ) );
	}
	// below pebble resolution the feature scales become smooth bumps
	if ( visF > 0.0 && vis < 1.0 && f1.w > 0.0 ) {
		float hd = 0.0, md = 0.0; vec3 gd = vec3( 0.0 );
		skFeatureDisc( p, f1, fr1, hd, gd, md );
		skFeatureDisc( p, f2, fr2, hd, gd, md );
		float wd = visF * ( 1.0 - vis );
		h += hd * wd; g += gd * wd;
		feat = mix( feat, md, 1.0 - vis );
	}

#ifdef SKIN_LIP
	// labial scale rows replace the pebbles along the lip line (see skLipRows)
	if ( abs( vLip.y ) < uLip.x * 6.0 && uLipHeight > 0.0 ) {
		// rest-space surface gradients of s and d from their screen derivatives: 2x2 solve in the
		// basis of the rest-position derivatives
		float a11 = dot( skRestDx, skRestDx ), a12 = dot( skRestDx, skRestDy ), a22 = dot( skRestDy, skRestDy );
		float det = a11 * a22 - a12 * a12;
		if ( det > 1e-28 ) {
			vec2 cs = vec2( a22 * skLipDx.x - a12 * skLipDy.x, a11 * skLipDy.x - a12 * skLipDx.x ) / det;
			vec2 cd = vec2( a22 * skLipDx.y - a12 * skLipDy.y, a11 * skLipDy.y - a12 * skLipDx.y ) / det;
			vec3 gS = cs.x * skRestDx + cs.y * skRestDy;
			vec3 gD = cd.x * skRestDx + cd.y * skRestDy;
			float wl, cl, tl, rl, vl; vec3 gl;
			skLipRows( vLip, gS, gD, pix, wl, gl, cl, tl, rl, vl );
			skDebugLip = vec3( fract( vLip.x / 0.05 ), min( abs( vLip.y ) / 0.1, 1.0 ), wl );
			if ( wl > 0.0 ) {
				g = mix( g, gl * vl, wl );
				crack = mix( crack, cl, wl ); top = mix( top, tl, wl );
				rnd = mix( rnd, rl, step( 0.5, wl ) );
				feat *= 1.0 - wl;
				vis = mix( vis, vl, wl );
			}
		}
	}
#endif

	// scutes on the front of the metatarsus / top of the toes. Rows narrow and stop where the foot
	// region fades out (instead of cross-fading into a smudge); metatarsus and toe frames switch along
	// a wavy line just above the toe joint, with a crease there.
	float scuteW = 0.0;
	if ( vSkinA.z > 0.01 ) {
		float aa = 1.2 * pix;
		float spanK = smoothstep( 0.2, 0.85, vSkinA.z );
		float sVis = skVisible( min( uScuteLength.x, uScuteLength.y ), pix );
		float side = step( 0.0, p.z ) * 7.0;                  // left and right feet get different rows
		float toeLine = uToeHeight + 0.02 * n2.x;
		float toeW = 1.0 - smoothstep( - aa, aa, p.y - toeLine );
		float hs = 0.0, cs = 0.0, ts = 0.0, rs = 0.0, os = 0.0, es = 0.0; vec3 gs = vec3( 0.0 );
		for ( int fr = 0; fr < 2; fr ++ ) {
			float wf = fr == 0 ? 1.0 - toeW : toeW;
			if ( wf < 0.001 ) continue;
			float h0, c0, t0, r0, o0, e0; vec3 g0;
			if ( fr == 0 ) skScutes( p, nR, normalize( uMetaAxis ), uScuteRadius.x, uScuteLength.x, uScuteSpan.x * spanK, side, aa, h0, g0, c0, t0, r0, o0, e0 );
			else skScutes( p, nR, normalize( uToeAxis ), uScuteRadius.y, uScuteLength.y, uScuteSpan.y * spanK, side + 3.0, aa, h0, g0, c0, t0, r0, o0, e0 );
			hs += h0 * wf; gs += g0 * wf; cs += c0 * wf; ts += t0 * wf; os += o0 * wf; es += e0 * wf;
			rs = fr == 0 ? r0 : mix( rs, r0, step( 0.5, toeW ) );
		}
		cs = max( cs, ( 1.0 - smoothstep( 0.0, 0.006 + aa, abs( p.y - toeLine ) ) ) * os );
		scuteW = os * sVis;
		h = mix( h, hs * sVis, scuteW ); g = mix( g, gs * sVis, scuteW );
		// pebbles meeting the end of a row are cut by a crack too
		crack = max( mix( crack, cs, os ), ( 1.0 - smoothstep( 0.0, 0.004 + aa, abs( es ) ) ) * spanK );
		top = mix( top, ts, os ); rnd = mix( rnd, rs, step( 0.5, os ) );
		feat *= 1.0 - os;
		vis = mix( vis, sVis, os );
	}

	// wrinkle web: narrow grooves along the zero lines of gradient noise (two octaves), the
	// meandering "elephant skin" creases that break up large smooth areas (flank, thigh, tail) at
	// medium distance. Weighted like the folds: faint on the face, absent on the scutes.
	float netVis = skVisible( uNetScale * 0.35, pix ) * ( 1.0 - scuteW ) * clamp( vSkinFold.x + vSkinFold.y + vSkinFold.z + 0.3, 0.0, 1.0 ) * ( 1.0 - feat );
	if ( uNetDepth > 0.0 && netVis > 0.0 ) {
		// grooves come and go along their length: a web of wrinkles, not a crackle glaze
		float on = smoothstep( - 0.35, 0.35, n4.x + 0.6 * n2.x );
		for ( int o = 0; o < 2; o ++ ) {
			float of = o == 0 ? 1.0 : 2.31;
			float oa = ( o == 0 ? 1.0 : 0.5 ) * netVis * on * ( o == 0 ? 1.0 : skVisible( uNetScale * 0.35 / of, pix ) );
			if ( oa <= 0.0 ) continue;
			vec4 nz = skGnoised( p * ( of / uNetScale ) + vec3( 4.41, 9.2, 1.3 ) * float( o + 1 ) + uSeed );
			vec3 dn = nz.yzw * ( of / uNetScale );
			float w = uNetWidth + 1.2 * pix * length( dn );         // wider (and fainter) when small on screen
			float t = clamp( abs( nz.x ) / w, 0.0, 1.0 );
			float prof = 1.0 - t * t * ( 3.0 - 2.0 * t );
			float dprof = t < 1.0 ? - 6.0 * t * ( 1.0 - t ) / w * sign( nz.x ) : 0.0;
			float amt = oa * uNetWidth / w;
			h -= uNetDepth * amt * prof;
			g -= uNetDepth * amt * dprof * dn;
			foldCrease = max( foldCrease, prof * amt * 0.45 );
		}
	}
	float boss = feat * visF;


	float eyeW = smoothstep( 0.3, 0.7, vSkinFeat.x ), clawW = smoothstep( 0.3, 0.7, vSkinFeat.y );
	skSkinW = 1.0 - max( eyeW, clawW );
	skDebugVis = vis;
	skDebugFeat = vec3( crack, feat, f1.w > 0.0 ? 0.5 + 0.5 * step( length( p - f1.xyz ), f1.w ) : 0.0 );
	float bump = uBumpStrength * vSkinA.w * skSkinW;
	skGradDetail = g * bump;
	skGradMacro *= uBumpStrength * skSkinW;
	// keratin claws: faint growth striations instead of scales
	skGradDetail += clawW * 0.0006 * uLumpScale * 9.0 * n2.yzw;

	// ---- albedo
	float cs = smoothstep( uCounterShade.x, uCounterShade.y, nR.y + 0.3 * mottle );
	// blend in a perceptual (sqrt) space: a linear blend lets the pale belly tone dominate the flanks
	vec3 col = mix( sqrt( uVentralColor ), sqrt( uDorsalColor ), cs );
	col = col * col * vSkinB.rgb;
	col *= mix( vec3( 1.0 ), uLateralTint, 4.0 * cs * ( 1.0 - cs ) );   // warmer band where back and belly tones meet
	col *= 1.0 + uMottle * mottle;
	col *= 1.0 + uGrainTint * n4.x * grainVis;
	col *= 1.0 + uScaleTint * ( rnd - 0.5 ) * 2.0 * vis;
	col *= 1.0 + uTopLight * top * vis;
	col *= 1.0 + uFeatureTint * boss * ( 0.6 + 0.8 * fract( rnd * 3.7 ) ); // feature scales: own tone, read mostly by relief
	float crackAmt = max( crack * vis, foldCrease * 0.6 );
	col *= 1.0 - uCrackDark * mix( 0.18, crackAmt, vis );           // faded: keep the mean darkening of the mortar
	col *= 1.0 - uCavityDark * vSkinCavity * vSkinCavity;          // squared: only true creases darken
	float dustW = uDust * ( 1.0 - smoothstep( uDustHeight * 0.25, uDustHeight, p.y ) ) * ( 0.55 + 0.45 * n2.x );
	dustW *= mix( 0.45, 1.0, max( crack * vis, 0.25 ) );
	col = mix( col, uDustColor, clamp( dustW, 0.0, 1.0 ) );
	col = mix( col, uClawColor * ( 0.85 + 0.3 * n2.x ), clawW );
	col = mix( col, uEyeColor * ( 0.8 + 0.4 * n4.x ), eyeW );
	diffuseColor.rgb *= max( col, 0.0 );

	// ---- roughness
	float rough = uRoughness + vSkinB.a + uRoughVar * n3;
	rough += uCrackRough * crack * vis + uTopRough * top * vis + 0.14 * ( fract( rnd * 7.31 ) - 0.5 ) * vis;
	rough += uFadeRough * ( 1.0 - vis ) * step( 0.001, bump );
	rough += 0.25 * dustW;
	rough = mix( rough, 0.36 + 0.08 * n2.x, clawW );
	rough = mix( rough, 0.07, eyeW );
	skRoughness = clamp( rough, 0.05, 1.0 );

	// ---- occlusion: aCavity + micro occlusion in cracks and fold creases
	skAO = ( 1.0 - uCavityAO * vSkinCavity ) * ( 1.0 - uOcclusionAO * vSkinFeat.z ) * ( 1.0 - 0.35 * crack * vis ) * ( 1.0 - 0.3 * foldCrease );
	// rim / fill lights usually cast no shadow: without this their highlights glow inside creases
	// and where a limb meets the body
	skSpecOcc = 1.0 - uSpecOcclusion * ( 1.0 - ( 1.0 - vSkinCavity ) * ( 1.0 - vSkinFeat.z ) * ( 1.0 - 0.5 * crack * vis ) * ( 1.0 - 0.5 * foldCrease ) );
}
`;

const FRAG_NORMAL = /* glsl */`
{
	float skFace = gl_FrontFacing ? 1.0 : - 1.0;
	vec3 skN0 = normal;
	vec2 dMacro = vec2( dot( skGradMacro, skRestDx ), dot( skGradMacro, skRestDy ) ) * uWorldScale;
	vec2 dDetail = vec2( dot( skGradDetail, skRestDx ), dot( skGradDetail, skRestDy ) ) * uWorldScale;
	skSmoothN = skPerturb( skSigX, skSigY, skN0, dMacro, skFace );
	normal = skPerturb( skSigX, skSigY, skN0, dMacro + dDetail, skFace );
}
`;

// Direct light: GGX specular on the detailed normal; diffuse with per-channel wrap and the
// red channel partly lit by the macro normal (light diffusing under the scales).
const FRAG_LIGHT = /* glsl */`
#include <lights_physical_pars_fragment>
void RE_Direct_Skin( const in IncidentLight directLight, const in vec3 geometryPosition, const in vec3 geometryNormal, const in vec3 geometryViewDir, const in vec3 geometryClearcoatNormal, const in PhysicalMaterial material, inout ReflectedLight reflectedLight ) {
	vec3 L = directLight.direction;
	float ndlD = dot( geometryNormal, L );
	float ndlS = dot( skSmoothN, L );
	vec3 blur = clamp( uSSSNormalBlur * uScatterColor, 0.0, 1.0 );
	vec3 ndl = mix( vec3( ndlD ), vec3( ndlS ), blur * skSkinW );
	vec3 wrap = uSSS * uScatterColor * skSkinW;
	vec3 diff = clamp( ( ndl + wrap ) / ( 1.0 + wrap ), 0.0, 1.0 );
	vec3 irrD = directLight.color * diff;
	reflectedLight.directDiffuse += irrD * BRDF_Lambert( material.diffuseColor );
	float dotNL = saturate( ndlD );
	reflectedLight.directSpecular += uSpecular * skSpecOcc * dotNL * directLight.color * BRDF_GGX( L, geometryViewDir, geometryNormal, material );
	#ifdef USE_SHEEN
		sheenSpecularDirect += dotNL * directLight.color * BRDF_Sheen( L, geometryViewDir, geometryNormal, material.sheenColor, material.sheenRoughness );
	#endif
	if ( uTranslucency > 0.0 ) {
		float back = pow( saturate( dot( geometryViewDir, - L ) ), 6.0 ) * pow( 1.0 - saturate( dot( skSmoothN, geometryViewDir ) ), 2.0 );
		reflectedLight.directDiffuse += directLight.color * back * uTranslucency * uScatterColor * material.diffuseColor;
	}
}
#undef RE_Direct
#define RE_Direct RE_Direct_Skin
`;

// Debug views (material.userData.setDebug(mode)): 1 albedo, 2 normal, 3 roughness, 4 ambient
// occlusion, 5 detail visibility (red = scales shown, black = faded), 6 region params (r: scale octave
// 0.5 + 0.5 * log2(size / baseScale), g: feature density, b: scute weight), 7 crack / feature scale / site,
// 8 lip coordinates (r: fract(s / 5 cm), g: |d| / 10 cm, b: labial-row weight).
const FRAG_DEBUG = /* glsl */`
#include <dithering_fragment>
#ifdef SKIN_DEBUG
	#if SKIN_DEBUG == 1
		gl_FragColor = vec4( pow( diffuseColor.rgb, vec3( 1.0 / 2.2 ) ), 1.0 );
	#elif SKIN_DEBUG == 2
		gl_FragColor = vec4( normal * 0.5 + 0.5, 1.0 );
	#elif SKIN_DEBUG == 3
		gl_FragColor = vec4( vec3( skRoughness ), 1.0 );
	#elif SKIN_DEBUG == 4
		gl_FragColor = vec4( vec3( skAO ), 1.0 );
	#elif SKIN_DEBUG == 5
		gl_FragColor = vec4( skDebugVis, 0.0, 0.0, 1.0 );
	#elif SKIN_DEBUG == 7
		gl_FragColor = vec4( skDebugFeat, 1.0 );
	#elif SKIN_DEBUG == 8
		gl_FragColor = vec4( skDebugLip, 1.0 );
	#else
		gl_FragColor = vec4( 0.5 + 0.5 * vSkinA.x, vSkinA.y, vSkinA.z, 1.0 );
	#endif
#endif
`;

const FRAG_AO = /* glsl */`
reflectedLight.indirectDiffuse *= skAO;
#if defined( USE_ENVMAP ) && defined( STANDARD )
{
	float skNV = saturate( dot( geometryNormal, geometryViewDir ) );
	reflectedLight.indirectSpecular *= computeSpecularOcclusion( skNV, skAO, material.roughness );
}
#endif
#include <aomap_fragment>
`;

// ---------------------------------------------------------------------------------------------

function v3(THREE, a) { return new THREE.Vector3(a[0], a[1], a[2]); }
function v2(THREE, a) { return new THREE.Vector2(a[0], a[1]); }

function regionUniforms(THREE, regions, baseScale) {
  const params = [], tint = [], fold = [];
  for (const name of SKIN_REGIONS) {
    const r = regions[name];
    params.push(new THREE.Vector4(Math.log2(r.size / baseScale), r.feature, r.scute, r.bump));
    tint.push(new THREE.Vector4(r.tint[0], r.tint[1], r.tint[2], r.rough));
    fold.push(new THREE.Vector4(r.folds[0], r.folds[1], r.folds[2], 0));
  }
  return { params, tint, fold };
}

export function createSkinMaterial(THREE, opts = {}) {
  const o = { ...DEFAULTS, ...opts };
  const regions = {};
  for (const name of SKIN_REGIONS) regions[name] = { ...DEFAULT_REGION_PARAMS[name], ...(opts.regions?.[name] || {}) };

  const Mat = o.physical ? THREE.MeshPhysicalMaterial : THREE.MeshStandardMaterial;
  const mat = new Mat({ color: 0xffffff, roughness: 1, metalness: 0, envMapIntensity: o.envMapIntensity });
  if (o.physical) {
    mat.sheen = o.sheen;
    mat.sheenRoughness = o.sheenRoughness;
    mat.sheenColor = new THREE.Color(o.sheenColor);
  }
  const col = (c) => new THREE.Color(c);
  const ru = regionUniforms(THREE, regions, o.baseScale);
  const hot = [];
  for (let i = 0; i < MAX_HOTSPOTS; i++) {
    const h = o.hotspots[i];
    hot.push(h ? new THREE.Vector4(h[0], h[1], h[2], h[3]) : new THREE.Vector4(0, 0, 0, 0));
  }
  const U = {
    uRegionParams: { value: ru.params }, uRegionTint: { value: ru.tint }, uRegionFold: { value: ru.fold },
    uHotspots: { value: hot }, uHotspotBoost: { value: o.hotspotBoost }, uHotspotSize: { value: o.hotspotSize },
    uMidlineBoost: { value: o.midlineBoost }, uMidlineWidth: { value: o.midlineWidth }, uFoldBase: { value: o.foldBase },
    uSeed: { value: o.seed * 17.123 }, uBaseScale: { value: o.baseScale }, uSizeNoise: { value: o.sizeNoise }, uJitter: { value: o.jitter }, uSizeVar: { value: o.sizeVar },
    uMortar: { value: o.mortar }, uDomeRadius: { value: o.domeRadius }, uScaleHeight: { value: o.scaleHeight },
    uSoftness: { value: o.softness }, uBumpStrength: { value: o.bumpStrength },
    uFeatureScale: { value: o.featureScale }, uFeatureHeight: { value: o.featureHeight },
    uFeatureSize: { value: v2(THREE, o.featureSize) }, uRosette: { value: o.rosette }, uFeatureTint: { value: o.featureTint },
    uScuteLength: { value: v2(THREE, o.scuteLength) }, uScuteSpan: { value: v2(THREE, o.scuteSpan) },
    uScuteRadius: { value: v2(THREE, o.scuteRadius) }, uScuteHeight: { value: o.scuteHeight },
    uScuteCurve: { value: o.scuteCurve }, uScuteSplit: { value: o.scuteSplit },
    uMetaAxis: { value: v3(THREE, o.metaAxis).normalize() }, uToeAxis: { value: v3(THREE, o.toeAxis).normalize() },
    uToeHeight: { value: o.toeHeight },
    uFoldDepth: { value: o.foldDepth }, uFoldAniso: { value: o.foldAniso }, uFoldOctave: { value: o.foldOctave }, uFoldWarp: { value: o.foldWarp }, uFoldDirs: { value: o.foldDirs.map((d) => { const a = v3(THREE, d).normalize(); return new THREE.Vector4(a.x, a.y, a.z, d[3]); }) }, uLumpDepth: { value: o.lumpDepth }, uLumpScale: { value: o.lumpScale },
    uGrainScale: { value: o.grainScale }, uGrainDepth: { value: o.grainDepth }, uGrainTint: { value: o.grainTint },
    uNetScale: { value: o.netScale }, uNetDepth: { value: o.netDepth }, uNetWidth: { value: o.netWidth },
    uDorsalColor: { value: col(o.dorsalColor) }, uVentralColor: { value: col(o.ventralColor) }, uLateralTint: { value: v3(THREE, o.lateralTint) },
    uCounterShade: { value: new THREE.Vector2(o.counterShade[0], o.counterShade[1]) },
    uMottle: { value: o.mottle }, uMottleScale: { value: o.mottleScale }, uScaleTint: { value: o.scaleTint },
    uTopLight: { value: o.topLight }, uCrackDark: { value: o.crackDark }, uCavityDark: { value: o.cavityDark },
    uCavityAO: { value: o.cavityAO }, uRoughness: { value: o.roughness }, uRoughVar: { value: o.roughVar },
    uCrackRough: { value: o.crackRough }, uTopRough: { value: o.topRough }, uFadeRough: { value: o.fadeRough },
    uScatterColor: { value: v3(THREE, o.scatterColor) }, uSSS: { value: o.sss }, uSSSNormalBlur: { value: o.sssNormalBlur },
    uTranslucency: { value: o.translucency }, uSpecular: { value: o.specular }, uSpecF90: { value: o.specF90 }, uSpecOcclusion: { value: o.specOcclusion },
    uDustColor: { value: col(o.dustColor) }, uDust: { value: o.dust }, uDustHeight: { value: o.dustHeight },
    uDetailFade: { value: o.detailFade }, uDetailFull: { value: o.detailFull }, uWorldScale: { value: o.worldScale },
    uLip: { value: new THREE.Vector4(o.lipRow, o.lipShrink, o.lipRows, o.lipLength) }, uLipHeight: { value: o.lipHeight },
    uEyeColor: { value: col(o.eyeColor) }, uClawColor: { value: col(o.clawColor) }, uOcclusionAO: { value: o.occlusionAO }, uFoldAttr: { value: o.foldAttr },
  };

  mat.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, U);
    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\n' + VERT_PARS)
      .replace('#include <begin_vertex>', VERT_MAIN);
    shader.fragmentShader = shader.fragmentShader
      .replace('#include <common>', '#include <common>\n' + FRAG_PARS)
      .replace('#include <color_fragment>', FRAG_EVAL)
      .replace('#include <roughnessmap_fragment>', 'float roughnessFactor = skRoughness;')
      .replace('#include <normal_fragment_maps>', FRAG_NORMAL)
      .replace('#include <lights_physical_pars_fragment>', FRAG_LIGHT)
      .replace('#include <lights_physical_fragment>', '#include <lights_physical_fragment>\nmaterial.specularF90 = mix( material.specularF90, uSpecF90, skSkinW );')
      .replace('#include <aomap_fragment>', FRAG_AO)
      .replace('#include <dithering_fragment>', FRAG_DEBUG);
  };
  // One compiled program is shared by every skin material of the same kind.
  mat.customProgramCacheKey = () => 'noctis-reptile-skin-v3' + (o.physical ? '-phys' : '');
  mat.userData.skinUniforms = U;
  // Debug view: 0 off, 1 albedo, 2 normal, 3 roughness, 4 AO, 5 detail visibility, 6 region params, 7 crack / feature scales, 8 lip coordinates.
  mat.userData.setDebug = (mode) => {
    const d = { ...(mat.defines || {}) };
    if (mode) d.SKIN_DEBUG = mode | 0; else delete d.SKIN_DEBUG;
    mat.defines = d;
    mat.needsUpdate = true;
  };
  if (o.debug) mat.userData.setDebug(o.debug);
  mat.userData.skinRegions = regions;
  // Re-upload the region tables after editing material.userData.skinRegions[...].
  mat.userData.updateRegions = () => {
    const r = regionUniforms(THREE, regions, U.uBaseScale.value);
    U.uRegionParams.value = r.params; U.uRegionTint.value = r.tint; U.uRegionFold.value = r.fold;
  };
  return mat;
}

// Fits the material to a skeleton / sculpt (rest pose, TREX_BONES / TREX_SHAPES-like arrays):
//  - scute frames: distal metatarsus direction from metaL -> toesL, toe frame below the
//    metatarsophalangeal joint;
//  - tubercle hotspots (if shapes are given): for every shape tagged feature 'eye', one above the
//    orbit (brow bosses) and one below-behind it (cheek / jugal cluster), plus one on the nasal ridge.
//    The exact placement of feature scales on Tyrannosaurus is not preserved: this is an
//    art-direction choice modelled on extant archosaurs, keep it subtle.
export function configureSkinForBones(material, bones, shapes = null) {
  const U = material.userData.skinUniforms;
  const get = (n) => bones.find((b) => b.name === n);
  const meta = get('metaL') || get('metaR'), toes = get('toesL') || get('toesR');
  if (meta && toes) {
    const d = [toes.head[0] - meta.head[0], toes.head[1] - meta.head[1], 0];
    const l = Math.hypot(d[0], d[1]) || 1;
    U.uMetaAxis.value.set(d[0] / l, d[1] / l, 0);
    // The toe frame applies below roughly the top of the toe pads (joint height + a toe radius).
    U.uToeHeight.value = toes.head[1] + 0.06;
  }
  // Scute frame radii from the sculpt: the metatarsus and toe volumes (rest-space round cones).
  const radiusOf = (bone) => {
    const cones = (shapes || []).filter((s) => s.bone === bone && s.kind === 'roundCone' && s.feature !== 'claw' && s.ra);
    if (!cones.length) return 0;
    const big = cones.reduce((a, s) => (s.ra + s.rb > a.ra + a.rb ? s : a));
    return 0.5 * (big.ra + big.rb);
  };
  const rMeta = radiusOf('metaL') || radiusOf('metaR'), rToe = radiusOf('toesL') || radiusOf('toesR');
  if (rMeta > 0) U.uScuteRadius.value.x = rMeta;
  if (rToe > 0) U.uScuteRadius.value.y = rToe;
  const eyes = (shapes || []).filter((s) => s.feature === 'eye' && s.center);
  if (eyes.length) {
    const spots = [];
    for (const e of eyes.slice(0, 2)) {
      const [x, y, z] = e.center;
      spots.push([x - 0.02, y + 0.1, z * 0.92, 0.17]);          // brow, above the orbit
      spots.push([x - 0.18, y - 0.2, z * 1.05, 0.2]);           // cheek, below-behind the orbit
    }
    const ex = eyes.reduce((a, e) => a + e.center[0], 0) / eyes.length;
    const ey = eyes.reduce((a, e) => a + e.center[1], 0) / eyes.length;
    spots.push([ex + 0.45, ey - 0.02, 0, 0.22]);                 // nasal ridge
    U.uHotspots.value.forEach((v, i) => (spots[i] ? v.set(...spots[i]) : v.set(0, 0, 0, 0)));
  }
}

// Optional, recommended for meshes whose aRegion changes abruptly from one vertex to the next
// (e.g. SDF meshes): bakes the region tables into per-vertex attributes (aSkinA/B/C) and blurs
// them over the mesh surface, so scale size, tint, roughness and fold weights cross-fade over
// ~radius metres instead of switching inside one triangle (iterations: override the count). Where head and
// jaw regions meet it also writes the lip coordinates aLip (labial scale rows; lip: false skips them,
// lipMaxDist bounds the search) and enables SKIN_LIP. It enables the
// SKIN_BAKED_REGIONS define on the material: every mesh drawn with that material must then be
// baked. Re-run it after editing material.userData.skinRegions. Vertices that share a position
// (seams) are welded for the blur, so no crack opens along duplicated vertices.
export function bakeSkinRegions(THREE, geometry, material, { radius = 0.08, iterations, lip = true, lipMaxDist = 0.14 } = {}) {
  const pos = geometry.attributes.position, n = pos.count;
  const reg = geometry.attributes.aRegion;
  const regions = material.userData.skinRegions, base = material.userData.skinUniforms.uBaseScale.value;
  const K = 12;
  const rows = SKIN_REGIONS.map((name) => {
    const r = regions[name];
    return [Math.log2(r.size / base), r.feature, r.scute, r.bump, r.tint[0], r.tint[1], r.tint[2], r.rough, r.folds[0], r.folds[1], r.folds[2], 0];
  });
  // weld coincident vertices
  const canon = new Int32Array(n), keyMap = new Map();
  let m = 0;
  for (let i = 0; i < n; i++) {
    const key = `${Math.round(pos.getX(i) * 1e4)},${Math.round(pos.getY(i) * 1e4)},${Math.round(pos.getZ(i) * 1e4)}`;
    let c = keyMap.get(key);
    if (c === undefined) { c = m++; keyMap.set(key, c); }
    canon[i] = c;
  }
  let Q = new Float32Array(m * K);
  for (let i = 0; i < n; i++) {
    const ri = reg ? Math.min(SKIN_REGIONS.length - 1, Math.max(0, Math.round(reg.getX(i)))) : 0;
    Q.set(rows[ri], canon[i] * K);
  }
  // adjacency (CSR) from the triangles
  const index = geometry.index ? geometry.index.array : null;
  const triCount = index ? index.length / 3 : n / 3;
  const corner = (t, k) => canon[index ? index[t * 3 + k] : t * 3 + k];
  const deg = new Int32Array(m + 1);
  for (let t = 0; t < triCount; t++) for (let k = 0; k < 3; k++) deg[corner(t, k)] += 2;
  const start = new Int32Array(m + 1);
  for (let i = 0; i < m; i++) start[i + 1] = start[i] + deg[i];
  const adj = new Int32Array(start[m]), fill = start.slice(0, m);
  for (let t = 0; t < triCount; t++) {
    const a = corner(t, 0), b = corner(t, 1), c = corner(t, 2);
    adj[fill[a]++] = b; adj[fill[a]++] = c;
    adj[fill[b]++] = c; adj[fill[b]++] = a;
    adj[fill[c]++] = a; adj[fill[c]++] = b;
  }
  // A lazy Laplacian step spreads by ~edge/sqrt(2): pick the iteration count from the blur radius.
  if (iterations === undefined) {
    let sum = 0, cnt = 0;
    const step = Math.max(1, Math.floor(triCount / 20000));
    for (let t = 0; t < triCount; t += step) {
      const ia = index ? index[t * 3] : t * 3, ib = index ? index[t * 3 + 1] : t * 3 + 1;
      sum += Math.hypot(pos.getX(ia) - pos.getX(ib), pos.getY(ia) - pos.getY(ib), pos.getZ(ia) - pos.getZ(ib)); cnt++;
    }
    const edge = Math.max(1e-4, sum / Math.max(1, cnt));
    iterations = Math.min(400, Math.ceil(2 * (radius / edge) ** 2));
  }
  let Qn = new Float32Array(m * K);
  for (let it = 0; it < iterations; it++) {
    for (let i = 0; i < m; i++) {
      const s0 = start[i], s1 = start[i + 1], o = i * K;
      if (s1 === s0) { for (let k = 0; k < K; k++) Qn[o + k] = Q[o + k]; continue; }
      const inv = 0.5 / (s1 - s0);
      for (let k = 0; k < K; k++) {
        let acc = 0;
        for (let e = s0; e < s1; e++) acc += Q[adj[e] * K + k];
        Qn[o + k] = 0.5 * Q[o + k] + acc * inv;
      }
    }
    [Q, Qn] = [Qn, Q];
  }
  const A = new Float32Array(n * 4), B = new Float32Array(n * 4), C = new Float32Array(n * 4);
  for (let i = 0; i < n; i++) {
    const o = canon[i] * K;
    A.set(Q.subarray(o, o + 4), i * 4); B.set(Q.subarray(o + 4, o + 8), i * 4); C.set(Q.subarray(o + 8, o + 12), i * 4);
  }
  geometry.setAttribute('aSkinA', new THREE.BufferAttribute(A, 4));
  geometry.setAttribute('aSkinB', new THREE.BufferAttribute(B, 4));
  geometry.setAttribute('aSkinC', new THREE.BufferAttribute(C, 4));
  const defines = { ...(material.defines || {}), SKIN_BAKED_REGIONS: '' };
  // Lip coordinates for the labial scale rows (only where head and jaw regions meet).
  const lipData = lip && reg ? lipCoordinates(pos, reg, canon, m, start, adj, lipMaxDist) : null;
  if (lipData) {
    const L = new Float32Array(n * 2);
    for (let i = 0; i < n; i++) { L[i * 2] = lipData.s[canon[i]]; L[i * 2 + 1] = lipData.d[canon[i]]; }
    geometry.setAttribute('aLip', new THREE.BufferAttribute(L, 2));
    defines.SKIN_LIP = '';
  } else delete defines.SKIN_LIP;
  material.defines = defines;
  material.needsUpdate = true;
  return geometry;
}

// Lip coordinates on a welded mesh graph (canonical vertices, CSR adjacency):
//  d = signed geodesic distance to the head / jaw region border, i.e. the closed mouth line
//      (+ on the upper head, - on the jaw), clamped to +-maxDist;
//  s = arc length along that border from its most anterior point (the snout tip), signed by the
//      side (+z right, -z left), carried from the nearest border vertex.
// Dijkstra over mesh edges (graph distances overestimate true geodesics by a few %, fine here).
// Returns null when the mesh has no head / jaw border.
function lipCoordinates(pos, reg, canon, m, start, adj, maxDist) {
  const HEAD = SKIN_REGIONS.indexOf('head'), JAW = SKIN_REGIONS.indexOf('jaw');
  const n = pos.count;
  const P = new Float32Array(m * 3), R = new Int8Array(m).fill(-1);
  for (let i = 0; i < n; i++) {
    const c = canon[i];
    if (R[c] >= 0) continue;
    P[c * 3] = pos.getX(i); P[c * 3 + 1] = pos.getY(i); P[c * 3 + 2] = pos.getZ(i);
    R[c] = Math.round(reg.getX(i));
  }
  const dist = (a, b) => Math.hypot(P[a * 3] - P[b * 3], P[a * 3 + 1] - P[b * 3 + 1], P[a * 3 + 2] - P[b * 3 + 2]);
  // border vertices: head next to jaw or jaw next to head; start value = half the shortest edge
  // to the other side, so the signed distance crosses zero mid-edge
  const d0 = new Float32Array(m).fill(Infinity);
  const border = [];
  for (let v = 0; v < m; v++) {
    if (R[v] !== HEAD && R[v] !== JAW) continue;
    const other = R[v] === HEAD ? JAW : HEAD;
    for (let e = start[v]; e < start[v + 1]; e++) {
      const u = adj[e];
      if (R[u] === other) d0[v] = Math.min(d0[v], 0.5 * dist(u, v));
    }
    if (d0[v] < Infinity) border.push(v);
  }
  if (border.length < 8) return null;
  // arc length along the border band from its most anterior vertex
  let tip = border[0];
  for (const v of border) if (P[v * 3] > P[tip * 3]) tip = v;
  const isBorder = new Uint8Array(m);
  for (const v of border) isBorder[v] = 1;
  const arc = dijkstra(m, start, adj, dist, [[tip, 0]], Infinity, isBorder);
  // geodesic distance from the border, carrying the source's arc length
  const srcArc = new Float32Array(m);
  const seeds = border.map((v) => { srcArc[v] = arc.d[v] < Infinity ? Math.sign(P[v * 3 + 2] || 1) * arc.d[v] : 1e3; return [v, d0[v]]; });
  const geo = dijkstra(m, start, adj, dist, seeds, maxDist, null, srcArc);
  const s = new Float32Array(m), d = new Float32Array(m);
  for (let v = 0; v < m; v++) {
    const sign = R[v] === JAW ? -1 : 1;
    if (geo.d[v] <= maxDist) { d[v] = sign * geo.d[v]; s[v] = geo.carry[v]; } else { d[v] = sign * maxDist; s[v] = 1e3; }
  }
  return { s, d };
}

// Multi-source Dijkstra on a CSR graph (binary heap, lazy deletion). seeds: [[vertex, startDist]];
// stops at maxDist; `allowed` (optional) restricts the vertices; `carry` (optional) is propagated
// from the seed that reaches each vertex first.
function dijkstra(m, start, adj, dist, seeds, maxDist, allowed = null, carry = null) {
  const D = new Float64Array(m).fill(Infinity);   // float64: the heap keys must compare exactly
  const C = carry ? Float32Array.from(carry) : null;
  const hv = [], hd = [];
  const push = (v, dv) => {
    let i = hv.length; hv.push(v); hd.push(dv);
    while (i > 0) { const p = (i - 1) >> 1; if (hd[p] <= dv) break; hv[i] = hv[p]; hd[i] = hd[p]; i = p; }
    hv[i] = v; hd[i] = dv;
  };
  const pop = () => {
    const v = hv[0], last = hv.length - 1, lv = hv[last], ld = hd[last];
    hv.pop(); hd.pop();
    if (last > 0) {
      let i = 0;
      for (;;) {
        let c = 2 * i + 1;
        if (c >= last) break;
        if (c + 1 < last && hd[c + 1] < hd[c]) c++;
        if (hd[c] >= ld) break;
        hv[i] = hv[c]; hd[i] = hd[c]; i = c;
      }
      hv[i] = lv; hd[i] = ld;
    }
    return v;
  };
  for (const [v, dv] of seeds) if (dv < D[v]) { D[v] = dv; push(v, dv); }
  while (hv.length) {
    const dv = hd[0], v = pop();
    if (dv > D[v]) continue;
    for (let e = start[v]; e < start[v + 1]; e++) {
      const u = adj[e];
      if (allowed && !allowed[u]) continue;
      const du = dv + dist(u, v);
      if (du < D[u] && du <= maxDist) { D[u] = du; if (C) C[u] = C[v]; push(u, du); }
    }
  }
  return { d: D, carry: C };
}
