// Procedural reptile skin for the Noctis creature renderer (three.js 0.169, WebGL2, UV-free).
//
//   import { createSkinMaterial } from './creature/skin_material.js';
//   skinnedMesh.material = createSkinMaterial(THREE, { dorsalColor: '#463d36' });
//   skinnedMesh.material.userData.skinUniforms.uBumpStrength.value = 0.8;   // live tweak
//
// Everything is evaluated in REST object space (the `position` / `normal` attributes, i.e. before
// skinning), so the scales stay glued to the skin while the skeleton animates. Instead of a
// triplanar projection of 2D cells we evaluate 3D cellular noise directly on the rest surface:
// cheaper than 3 projections, and there is no blend seam or stretching anywhere on the body.
//
// Layers (all procedural, combined into one height field whose analytic gradient bumps the normal):
//   1. pebble scales: smooth-bordered 3D Voronoi (soft-min over the cell borders gives rounded,
//      polygonal "pebbles"), size per body region, snapped to octaves of uBaseScale so that the
//      transition between two sizes costs a second evaluation only along region borders;
//   2. feature scales: sparse larger round tubercles (denser on the head, along the dorsal midline
//      and inside optional hotspots such as the brow above the orbit);
//   3. scutes: transverse plates on the front of the metatarsus and the top of the toes
//      (anisotropic Voronoi in a limb-aligned frame);
//   4. skin folds: warped crease bands driven by the aFold attribute (+ a weak global level);
//   5. low-frequency fleshy lumps.
// Shading: countershaded albedo + mottling + per-region tint, per-scale colour jitter, darker
// mortar/cracks, aCavity occlusion, roughness breakup, dust on the lower legs, and a cheap
// subsurface approximation (per-channel wrap lighting + the red channel lit with a smoother
// normal, the classic "pre-integrated skin" trick) injected into three's direct-light loop.
// Fine detail fades with the pixel footprint (screen-space derivatives of the rest position) and
// the lost normal variance is put back as roughness, so medium/far shots do not shimmer.
//
// Science note: Tyrannosaurus skin impressions show small, non-imbricate, pebbly scales
// (Bell et al. 2017). Their real size is smaller than what is rendered by default here; the
// default sizes are a legibility choice for close-up renders (uncertain, tweak uBaseScale).
// Colour is UNKNOWN for any non-avian dinosaur: the palette is a neutral countershaded guess and
// must be labelled speculative wherever it is shown.
//
// ------------------------------------------------------------------------------------------
// Geometry attributes read (all optional; a missing attribute reads as 0):
//   aRegion  float  index into SKIN_REGIONS (head, jaw, neck, torso, belly, tail, thigh, shin, foot, arm)
//   aCavity  float  0..1 cavity / occlusion estimate (darkens albedo, occludes ambient light)
//   aFold    float  0..1 skin-fold intensity (neck / joint wrinkles)
//   position, normal: REST pose (model space, metres; +x forward, +y up, +z animal's right)
//
// Options (createSkinMaterial(THREE, opts)) -> uniform of the same role; every uniform lives in
// material.userData.skinUniforms and can be changed at any time without recompiling:
//   physical        false   use MeshPhysicalMaterial (+ sheen for a dusty grazing sheen) instead of Standard
//   sheen           0.25    (physical only) sheen amount; sheenColor / sheenRoughness also accepted
//   envMapIntensity 1       standard three.js material property
//   seed            0       uSeed: shifts every cellular pattern (variants of the same skin)
//   baseScale       0.022   uBaseScale (m): reference pebble size; region sizes snap to its octaves (x0.5, x1, x2)
//   jitter          0.85    uJitter: 0 = regular grid of cells, 1 = fully random cells
//   sizeVar         0.35    uSizeVar: power-diagram weight range (0 = equal cells; ~0.5 = big scales among small ones)
//   mortar          0.055   uMortar: crack half-width between scales (fraction of a cell)
//   domeRadius      0.32    uDomeRadius: width of the rounded scale edge (fraction of a cell)
//   scaleHeight     0.13    uScaleHeight: scale relief height (fraction of the cell size)
//   softness        0.06    uSoftness: soft-min radius of the cell borders (fraction of a cell; rounds corners)
//   bumpStrength    1.0     uBumpStrength: global multiplier on all relief
//   featureScale    0.075   uFeatureScale (m): cell size of the sparse tubercle layer
//   featureHeight   0.16    uFeatureHeight: tubercle height (fraction of its cell)
//   hotspots        []      uHotspots[6]: [[x, y, z, radius], ...] rest-space spheres that densify tubercles (brow, nasal)
//   hotspotBoost    0.6     uHotspotBoost: tubercle density added at a hotspot centre
//   midlineBoost    1.6     uMidlineBoost: tubercle density multiplier along the dorsal midline (z ~ 0, normal up)
//   midlineWidth    0.12    uMidlineWidth (m): half-width of that dorsal row
//   scuteSize       [0.045, 0.16, 0.13]  uScuteSize (m): plate length along the limb, width across the front, depth sideways
//   scuteHeight     0.16    uScuteHeight: plate relief (fraction of the plate length)
//   metaAxis        [0.32,-0.95,0]       uMetaAxis: distal direction of the metatarsus in rest space (see configureSkinForBones)
//   toeAxis         [1,0,0]              uToeAxis: distal direction of the toes in rest space
//   toeHeight       0.22    uToeHeight (m): rest-space height below which the toe frame is used
//   foldDepth       0.014   uFoldDepth (m): depth of a full-strength skin fold
//   foldBase        0.15    uFoldBase: fold intensity everywhere (added to aFold, times the region weights)
//   foldDirs        [[1,0,0,9],[0,1,0,10],[0.8,0.6,0,14]]  uFoldDirs[3]: rest-space direction ACROSS each fold field + folds per metre
//                           (x: tail / belly transverse folds, y: limb and flank wrinkles, diagonal: neck)
//   lumpDepth       0.006   uLumpDepth (m): amplitude of the low-frequency fleshy undulation
//   lumpScale       3.0     uLumpScale (1/m): frequency of that undulation
//   grainScale      0.045   uGrainScale (m): size of the scale-cluster grain kept at medium distance
//   grainDepth      0.0018  uGrainDepth (m): its relief
//   grainTint       0.1     uGrainTint: its brightness variation
//   dorsalColor     '#4a4038'  uDorsalColor (sRGB): back / upper flanks (speculative)
//   ventralColor    '#a2947f'  uVentralColor (sRGB): belly / underside (speculative)
//   counterShade    [-0.45, 0.35] uCounterShade: rest-normal-y range of the ventral -> dorsal transition
//   mottle          0.28    uMottle: low-frequency blotch contrast
//   mottleScale     1.6     uMottleScale (1/m): blotch frequency
//   scaleTint       0.16    uScaleTint: per-scale brightness jitter
//   topLight        0.12    uTopLight: lightening of the worn scale tops
//   crackDark       0.45    uCrackDark: darkening inside the mortar / cracks
//   cavityDark      0.45    uCavityDark: albedo darkening by aCavity
//   cavityAO        0.75    uCavityAO: ambient occlusion by aCavity
//   roughness       0.6     uRoughness: base roughness
//   roughVar        0.08    uRoughVar: low-frequency roughness breakup
//   crackRough      0.18    uCrackRough: extra roughness in the cracks
//   topRough        -0.1    uTopRough: roughness change on polished scale tops
//   fadeRough       0.12    uFadeRough: roughness added where detail is faded (keeps the normal variance)
//   scatterColor    [1.0, 0.42, 0.28] uScatterColor: per-channel wrap (red scatters furthest)
//   sss             0.55    uSSS: subsurface strength (wrap amount)
//   sssNormalBlur   0.45    uSSSNormalBlur: how much the red channel uses the un-bumped normal
//   translucency    0.0     uTranslucency: back-light glow at thin silhouettes (keep ~0 for a 7 t animal)
//   dust            0.35    uDust: dust / dried mud on the lower legs and in the cracks there
//   dustHeight      0.7     uDustHeight (m): height up to which dust is present
//   dustColor       '#8d8270'  uDustColor (sRGB)
//   detailFade      2.0     uDetailFade: pixels per scale under which detail is fully faded (fully shown at 2.5x)
//   worldScale      1.0     uWorldScale: object -> world scale, keeps the relief consistent if the mesh is scaled
//   regions         {}      per-region overrides, e.g. { foot: { tint: [0.8, 0.8, 0.82], rough: 0.1 } } with fields
//                           size (m), feature (density 0..1), scute (0..1), bump (x), tint ([r,g,b] linear multiplier),
//                           rough (offset), folds ([wx, wy, wd] weights of the three fold fields of foldDirs)
//   -> uRegionParams[10] (x log2(size/baseScale), y feature density, z scute, w bump),
//      uRegionTint[10] (rgb tint, a roughness offset), uRegionFold[10] (xyz fold-field weights, w unused)
// ------------------------------------------------------------------------------------------

export const SKIN_REGIONS = ['head', 'jaw', 'neck', 'torso', 'belly', 'tail', 'thigh', 'shin', 'foot', 'arm'];

// Region defaults. Sizes are octaves of the 2.2 cm body scale: face/lips and the small forelimb
// get half-size scales, as in extant archosaurs where facial and manual scales are finer.
export const DEFAULT_REGION_PARAMS = {
  head:  { size: 0.009, feature: 0.30, scute: 0, bump: 1.0, tint: [1.00, 0.98, 0.95], rough: -0.03, folds: [0, 0, 0] },
  jaw:   { size: 0.009, feature: 0.12, scute: 0, bump: 0.9, tint: [1.12, 1.08, 1.02], rough: -0.07, folds: [0, 0, 0] },
  neck:  { size: 0.018, feature: 0.10, scute: 0, bump: 1.0, tint: [1.00, 1.00, 1.00], rough: 0.0, folds: [0, 0, 1] },
  torso: { size: 0.018, feature: 0.05, scute: 0, bump: 1.0, tint: [1.00, 1.00, 1.00], rough: 0.0, folds: [0, 0.8, 0] },
  belly: { size: 0.018, feature: 0.00, scute: 0, bump: 0.8, tint: [1.04, 1.03, 1.01], rough: 0.03, folds: [0.8, 0, 0] },
  tail:  { size: 0.018, feature: 0.05, scute: 0, bump: 1.0, tint: [0.98, 0.98, 0.98], rough: 0.02, folds: [1, 0, 0] },
  thigh: { size: 0.018, feature: 0.03, scute: 0, bump: 1.0, tint: [0.98, 0.97, 0.96], rough: 0.0, folds: [0, 1, 0] },
  shin:  { size: 0.018, feature: 0.02, scute: 0, bump: 1.0, tint: [0.86, 0.85, 0.85], rough: 0.04, folds: [0, 1, 0] },
  foot:  { size: 0.018, feature: 0.00, scute: 1, bump: 1.15, tint: [0.64, 0.64, 0.66], rough: 0.08, folds: [0, 0, 0] },
  arm:   { size: 0.009, feature: 0.00, scute: 0, bump: 1.0, tint: [0.97, 0.96, 0.95], rough: 0.0, folds: [0, 1, 0] },
};

const DEFAULTS = {
  physical: false, sheen: 0.25, sheenColor: '#b9ab98', sheenRoughness: 0.8, envMapIntensity: 1,
  seed: 0, baseScale: 0.018, jitter: 0.85, sizeVar: 0.35, mortar: 0.035, domeRadius: 0.45, scaleHeight: 0.11, softness: 0.06,
  bumpStrength: 1.0, featureScale: 0.075, featureHeight: 0.085, hotspots: [], hotspotBoost: 0.6,
  midlineBoost: 1.6, midlineWidth: 0.12, scuteSize: [0.05, 0.22, 0.3], scuteHeight: 0.16,
  metaAxis: [0.32, -0.95, 0], toeAxis: [1, 0, 0], toeHeight: 0.22,
  foldDepth: 0.016, foldBase: 0.15, foldDirs: [[1, 0, 0, 9], [0, 1, 0, 10], [0.8, 0.6, 0, 14]], lumpDepth: 0.006, lumpScale: 3.0, grainScale: 0.045, grainDepth: 0.0018, grainTint: 0.1,
  dorsalColor: '#463e37', ventralColor: '#968e80', counterShade: [-0.45, 0.35], mottle: 0.32, mottleScale: 1.6,
  scaleTint: 0.16, topLight: 0.12, crackDark: 0.32, cavityDark: 0.45, cavityAO: 0.75,
  roughness: 0.64, roughVar: 0.08, crackRough: 0.18, topRough: -0.08, fadeRough: 0.12,
  scatterColor: [1.0, 0.5, 0.35], sss: 0.45, sssNormalBlur: 0.45, translucency: 0.0,
  dust: 0.25, dustHeight: 0.7, dustColor: '#8d8270', detailFade: 1.6, worldScale: 1.0,
};

const MAX_HOTSPOTS = 6;

// ---------------------------------------------------------------------------------------------
// GLSL

const VERT_PARS = /* glsl */`
attribute float aRegion;
attribute float aCavity;
attribute float aFold;
#ifdef SKIN_BAKED_REGIONS
attribute vec4 aSkinA;   // smoothed region params (see bakeSkinRegions)
attribute vec4 aSkinB;
attribute vec4 aSkinC;
#endif
uniform vec4 uRegionParams[ 10 ];
uniform vec4 uRegionTint[ 10 ];
uniform vec4 uRegionFold[ 10 ];   // xyz weights of the 3 fold fields
uniform vec4 uHotspots[ ${MAX_HOTSPOTS} ];
uniform float uHotspotBoost;
uniform float uMidlineBoost;
uniform float uMidlineWidth;
uniform float uFoldBase;
varying vec3 vRestPos;
varying vec3 vRestNormal;
varying vec4 vSkinA;    // x log2(size / base), y tubercle density, z scute weight, w bump
varying vec4 vSkinB;    // rgb region tint, a roughness offset
varying vec4 vSkinFold; // xyz weights of the 3 fold fields, w fold amount
varying float vSkinCavity;
`;

// Region data is looked up per vertex and interpolated, so the borders between regions blend
// smoothly instead of interpolating a meaningless region index.
const VERT_MAIN = /* glsl */`
#include <begin_vertex>
{
	vRestPos = position;
	vRestNormal = normal;
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
	vSkinA = vec4( rp.x, rp.y * ( 1.0 + midline * uMidlineBoost ) + hot * uHotspotBoost, rp.z, rp.w );
	vSkinFold = vec4( foldW, clamp( aFold + uFoldBase, 0.0, 1.0 ) );
	vSkinCavity = clamp( aCavity, 0.0, 1.0 );
}
`;

const FRAG_PARS = /* glsl */`
varying vec3 vRestPos;
varying vec3 vRestNormal;
varying vec4 vSkinA;
varying vec4 vSkinB;
varying vec4 vSkinFold;
varying float vSkinCavity;

uniform float uSeed, uBaseScale, uJitter, uSizeVar, uMortar, uDomeRadius, uScaleHeight, uSoftness, uBumpStrength;
uniform float uFeatureScale, uFeatureHeight;
uniform vec3 uScuteSize, uMetaAxis, uToeAxis;
uniform float uScuteHeight, uToeHeight;
uniform float uFoldDepth, uLumpDepth, uLumpScale, uGrainScale, uGrainDepth, uGrainTint;
uniform vec4 uFoldDirs[ 3 ];
uniform vec3 uDorsalColor, uVentralColor;
uniform vec2 uCounterShade;
uniform float uMottle, uMottleScale, uScaleTint, uTopLight, uCrackDark, uCavityDark, uCavityAO;
uniform float uRoughness, uRoughVar, uCrackRough, uTopRough, uFadeRough;
uniform vec3 uScatterColor;
uniform float uSSS, uSSSNormalBlur, uTranslucency;
uniform vec3 uDustColor;
uniform float uDust, uDustHeight, uDetailFade, uWorldScale;

// Globals shared between the pattern evaluation and the custom light function.
vec3 skSmoothN;     // view-space normal with the macro relief only (folds / lumps): used by the SSS red channel
float skAO;

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

// Cellular layer in q = J * p space (J maps rest metres to cell units; anisotropic for scutes).
// It is a power diagram (each cell has a weight w, "distance" = |r|^2 - w): the borders stay
// planar, but cells grow or shrink, which gives the natural mix of larger scales surrounded by
// small ones instead of a uniform cobblestone. wAmp = 0 gives a plain Voronoi diagram.
// Returns x = distance to the cell border in METRES (soft-min over every neighbour's border
// plane, which rounds the polygon corners), y = squared q-distance to the feature point.
// grad = gradient of x w.r.t. p (rest space); cellRnd = 3 random numbers of the cell.
vec2 skVoronoi( vec3 p, mat3 J, float jitter, float wAmp, float soft, out vec3 grad, out vec3 cellRnd ) {
	vec3 q = J * p;
	vec3 iq = floor( q ), fq = q - iq;
	vec4 r[ 27 ];                                            // xyz = feature - q, w = |r|^2 - weight
	float dmin = 1e9;
	int imin = 13;
	vec4 r1 = vec4( 0.0 );
	vec3 g1 = vec3( 0.0 );
	for ( int k = 0; k < 27; k ++ ) {
		vec3 g = vec3( float( k % 3 ) - 1.0, float( ( k / 3 ) % 3 ) - 1.0, float( k / 9 ) - 1.0 );
		vec3 o = skHash33( iq + g + uSeed );
		vec3 rr = g + 0.5 + jitter * ( o - 0.5 ) - fq;
		float wt = fract( dot( o, vec3( 13.17, 7.71, 3.37 ) ) );
		float d = dot( rr, rr ) - wAmp * wt * wt;
		r[ k ] = vec4( rr, d );
		if ( d < dmin ) { dmin = d; imin = k; r1 = r[ k ]; g1 = g; }
	}
	mat3 JT = transpose( J );
	float s = 0.0;
	vec3 gs = vec3( 0.0 );
	float inv = 1.0 / soft;
	for ( int k = 0; k < 27; k ++ ) {
		if ( k == imin ) continue;
		vec3 dr = r[ k ].xyz - r1.xyz;
		float il2 = inversesqrt( dot( dr, dr ) );
		vec3 nq = dr * il2;
		vec3 np = JT * nq;                                   // p-space gradient of the border plane function
		float il = inversesqrt( dot( np, np ) );
		float e = 0.5 * ( r[ k ].w - r1.w ) * il2 * il;     // metric distance to that border (>= 0)
		float wgt = exp( - e * inv );
		s += wgt;
		gs += wgt * il * np;
	}
	grad = - gs / s;
	cellRnd = skHash33( iq + g1 + uSeed + 71.31 );
	return vec2( - soft * log( s ), dmin );
}

// Rounded polygonal "pebble" scales of a given size (m). Outputs relief h (m), its gradient g,
// crack mask (1 in the mortar), top mask (1 on the scale crown) and a per-scale random value.
void skPebbles( vec3 p, float size, out float h, out vec3 g, out float crack, out float top, out float rnd ) {
	vec3 eg, cr;
	vec2 v = skVoronoi( p, mat3( 1.0 / size ), uJitter, uSizeVar, uSoftness * size, eg, cr );
	float e = v.x / size;                                   // border distance in cell units
	float m = uMortar * ( 0.7 + 0.6 * cr.y );
	float x = clamp( ( e - m ) / uDomeRadius, 0.0, 1.0 );
	float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
	float dprof = ( e > m && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / uDomeRadius : 0.0;
	float amp = uScaleHeight * ( 0.6 + 0.8 * cr.x );
	h = amp * prof * size;
	g = amp * dprof * eg;                                   // dh/dp = amp*size*dprof * (eg/size)
	g += ( cr - 0.5 ) * ( 0.35 * uScaleHeight / 0.13 ) * prof; // per-scale tilt breaks up the specular
	crack = 1.0 - smoothstep( 0.0, m * 1.8, e );
	top = prof;
	rnd = cr.z;
}

// Transverse scutes in a limb frame: plates short along the limb axis, spanning its front.
// Profile is asymmetric like overlapping (imbricate) scutes: a raised step at the distal edge
// of each plate and a gentle ramp at its proximal edge.
void skScutes( vec3 p, vec3 axis, out float h, out vec3 g, out float crack, out float top, out float rnd ) {
	vec3 F = normalize( cross( vec3( 0.0, 0.0, 1.0 ), axis ) );
	vec3 B = cross( axis, F );
	mat3 J = transpose( mat3( axis / uScuteSize.x, F / uScuteSize.y, B / uScuteSize.z ) );
	vec3 eg, cr;
	vec2 v = skVoronoi( p, J, 0.5, 0.0, 0.06 * uScuteSize.x, eg, cr );
	float e = v.x / uScuteSize.x;
	float m = 0.06;
	// eg points away from the nearest border: dot < 0 means that border is the distal one
	float R = mix( 0.22, 0.85, smoothstep( - 0.5, 0.5, dot( eg, axis ) ) );
	float x = clamp( ( e - m ) / R, 0.0, 1.0 );
	float prof = 1.0 - ( 1.0 - x ) * ( 1.0 - x );
	float dprof = ( e > m && x < 1.0 ) ? 2.0 * ( 1.0 - x ) / R : 0.0;
	float amp = uScuteHeight * ( 0.75 + 0.5 * cr.x );
	h = amp * prof * uScuteSize.x;
	g = amp * dprof * eg + ( cr - 0.5 ) * 0.1 * prof;
	crack = 1.0 - smoothstep( 0.0, m * 1.8, e );
	top = prof;
	rnd = cr.z;
}

// Sparse tubercles (feature scales): low, flat-topped, slightly irregular domes that replace the
// pebbles where a cell of a coarser lattice is "selected" (probability = density). The pebble
// relief is kept faintly on top so a tubercle reads as a rugose large scale, not a bead.
void skTubercles( vec3 p, float size, float density, inout float h, inout vec3 g, inout float crack, inout float top, inout float rnd, out float boss ) {
	boss = 0.0;
	vec3 q = p / size;
	vec3 iq = floor( q ), fq = q - iq;
	float dmin = 1e9;
	vec3 r1 = vec3( 0.0 ), c1 = vec3( 0.0 );
	for ( int k = 0; k < 27; k ++ ) {
		vec3 gg = vec3( float( k % 3 ) - 1.0, float( ( k / 3 ) % 3 ) - 1.0, float( k / 9 ) - 1.0 );
		vec3 rr = gg + 0.5 + 0.7 * ( skHash33( iq + gg + uSeed + 19.7 ) - 0.5 ) - fq;
		float d = dot( rr, rr );
		if ( d < dmin ) { dmin = d; r1 = rr; c1 = iq + gg; }
	}
	vec3 cr = skHash33( c1 + uSeed + 5.13 );
	if ( cr.x > density ) return;
	float d = sqrt( dmin );
	vec3 dir = r1 / max( d, 1e-4 );                         // towards the tubercle centre
	// irregular outline: radius depends on the direction (cheap angular wobble)
	float wob = 0.09 * sin( 4.0 * dir.x + 3.0 * dir.y + 6.28 * cr.z ) + 0.06 * sin( 7.0 * dir.z - 5.0 * dir.x + 6.28 * cr.y );
	float radius = ( 0.3 + 0.15 * cr.y ) * ( 1.0 + wob );
	float x = d / radius;
	if ( x > 1.2 ) return;
	float amp = uFeatureHeight * ( 0.6 + 0.6 * cr.z ) * size;
	// bell profile (1 - x^2)^2: a boss that swells out of the pebbles, no hard rim
	float b1 = max( 1.0 - x * x, 0.0 );
	float hf = amp * b1 * b1;
	vec3 gf = amp * 4.0 * x * b1 / ( radius * size ) * dir;
	float mask = 1.0 - smoothstep( 0.55, 1.0, x );      // the pebbles flatten on the boss
	h = h * ( 1.0 - 0.65 * mask ) + hf;
	g = g * ( 1.0 - 0.65 * mask ) + gf;
	float ring = smoothstep( 0.85, 0.97, x ) * ( 1.0 - smoothstep( 1.0, 1.12, x ) );
	crack = max( crack * ( 1.0 - 0.6 * mask ), 0.6 * ring );
	top = mix( top, 0.25 + 0.25 * b1, mask );
	rnd = mix( rnd, cr.y, mask );
	boss = mask;
}

// Smoothly fades a layer whose cells cover fewer than uDetailFade pixels.
float skVisible( float size, float pix ) {
	return smoothstep( uDetailFade, uDetailFade * 2.5, size / max( pix, 1e-6 ) );
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
vec3 skGradDetail = vec3( 0.0 );
vec3 skGradMacro = vec3( 0.0 );
float skRoughness;
{
	vec3 p = vRestPos;
	vec3 nR = normalize( vRestNormal );
	float pix = max( length( skRestDx ), length( skRestDy ) );   // metres per pixel on the rest surface

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

	// ---- skin folds: three warped crease-band fields with FIXED rest-space directions, weighted
	// per region. (Interpolating a direction between regions would compress the band phase into
	// a hatched seam along every region border.)
	float foldCrease = 0.0;
	if ( vSkinFold.w > 0.005 && dot( vSkinFold.xyz, vec3( 1.0 ) ) > 0.01 ) {
		vec3 w1 = vec3( 3.1, 5.3, 4.2 ), w2 = vec3( - 6.7, 2.9, 7.7 ), w3 = vec3( 9.3, - 7.1, 3.3 );
		float warp = 0.5 * sin( dot( p, w1 ) ) + 0.3 * sin( dot( p, w2 ) + 1.7 ) + 0.12 * sin( dot( p, w3 ) + 0.4 );
		vec3 dwarp = 0.5 * cos( dot( p, w1 ) ) * w1 + 0.3 * cos( dot( p, w2 ) + 1.7 ) * w2 + 0.12 * cos( dot( p, w3 ) + 0.4 ) * w3;
		// fold lines start and stop along their length instead of ringing the whole body
		float brk = smoothstep( - 0.35, 0.35, n2.x + 0.35 * n1.x );
		for ( int i = 0; i < 3; i ++ ) {
			float wi = vSkinFold[ i ];
			if ( wi < 0.01 ) continue;
			vec3 K = uFoldDirs[ i ].xyz * uFoldDirs[ i ].w;
			float t = dot( p, K ) + warp;
			vec3 dt = K + dwarp;
			float u = fract( t );
			float sn = sin( PI * u );
			float dh = 2.0 * ( 1.0 - sn ) * PI * cos( PI * u );   // profile sn * (2 - sn): rounded roll, sharp crease
			float band = 0.45 + 0.9 * skHash13( vec3( floor( t ), float( i ), uSeed ) ); // each fold its own depth
			float fvis = skVisible( 1.0 / uFoldDirs[ i ].w, pix );
			float a = wi * vSkinFold.w * brk * fvis * band;
			skGradMacro += uFoldDepth * a * dh * dt;
			foldCrease = max( foldCrease, ( 1.0 - smoothstep( 0.0, 0.35, sn ) ) * a );
		}
	}

	// ---- scale layers
	float h = 0.0, crack = 0.0, top = 0.0, rnd = 0.5;
	vec3 g = vec3( 0.0 );
	float L = clamp( vSkinA.x, - 3.0, 3.0 );
	float lo = floor( L );
	// dithered (|d| < 0.25 keeps octave crossings continuous): scale sizes interlock at region borders
	float tB = smoothstep( 0.25, 0.75, L - lo + 0.2 * n2.x );
	float sizeA = uBaseScale * exp2( lo ), sizeB = sizeA * 2.0;
	float visA = skVisible( sizeA, pix ), visB = skVisible( sizeB, pix );
	float vis = mix( visA, visB, tB );
	if ( tB < 0.999 && visA > 0.0 ) {
		float h0, c0, t0, r0; vec3 g0;
		skPebbles( p, sizeA, h0, g0, c0, t0, r0 );
		float w = ( 1.0 - tB ) * visA;
		h += h0 * w; g += g0 * w; crack += c0 * ( 1.0 - tB ); top += t0 * ( 1.0 - tB ); rnd = r0;
	}
	if ( tB > 0.001 && visB > 0.0 ) {
		float h0, c0, t0, r0; vec3 g0;
		skPebbles( p, sizeB, h0, g0, c0, t0, r0 );
		float w = tB * visB;
		h += h0 * w; g += g0 * w; crack += c0 * tB; top += t0 * tB; rnd = mix( rnd, r0, step( 0.5, tB ) );
	}

	// scutes on the front of the metatarsus / top of the toes
	float scuteW = 0.0, boss = 0.0;
	if ( vSkinA.z > 0.01 ) {
		// two frames (metatarsus, toes) evaluated separately and cross-faded: blending the axis
		// itself would squeeze the plates into thin stripes at the joint
		float toeW = 1.0 - smoothstep( uToeHeight - 0.06, uToeHeight + 0.06, p.y );
		vec3 axM = normalize( uMetaAxis ), axT = normalize( uToeAxis );
		vec3 fM = normalize( cross( vec3( 0.0, 0.0, 1.0 ), axM ) ), fT = normalize( cross( vec3( 0.0, 0.0, 1.0 ), axT ) );
		float faceW = smoothstep( - 0.05, 0.45, dot( nR, normalize( mix( fM, fT, toeW ) ) ) );
		scuteW = vSkinA.z * faceW;
		float sVis = skVisible( uScuteSize.x, pix );
		if ( scuteW > 0.01 && sVis > 0.0 ) {
			float hs = 0.0, cs = 0.0, ts = 0.0, rs = 0.0; vec3 gs = vec3( 0.0 );
			float h0, c0, t0, r0; vec3 g0;
			if ( toeW < 0.999 ) {
				skScutes( p, axM, h0, g0, c0, t0, r0 );
				hs += h0 * ( 1.0 - toeW ); gs += g0 * ( 1.0 - toeW ); cs += c0 * ( 1.0 - toeW ); ts += t0 * ( 1.0 - toeW ); rs = r0;
			}
			if ( toeW > 0.001 ) {
				skScutes( p + 1.37, axT, h0, g0, c0, t0, r0 );
				hs += h0 * toeW; gs += g0 * toeW; cs += c0 * toeW; ts += t0 * toeW; rs = mix( rs, r0, step( 0.5, toeW ) );
			}
			h = mix( h, hs * sVis, scuteW ); g = mix( g, gs * sVis, scuteW );
			crack = mix( crack, cs, scuteW ); top = mix( top, ts, scuteW ); rnd = mix( rnd, rs, step( 0.5, scuteW ) );
			vis = mix( vis, sVis, scuteW );
		}
	}

	// sparse tubercles (feature scales)
	float fVis = skVisible( uFeatureScale * 0.5, pix );
	if ( vSkinA.y > 0.005 && fVis > 0.0 ) {
		float hb = h; vec3 gb = g; float cb = crack, tb = top, rb = rnd;
		skTubercles( p + 7.7, uFeatureScale, min( vSkinA.y, 0.85 ), h, g, crack, top, rnd, boss );
		boss *= fVis;
		h = mix( hb, h, fVis ); g = mix( gb, g, fVis ); crack = mix( cb, crack, fVis ); top = mix( tb, top, fVis ); rnd = mix( rb, rnd, fVis );
		vis = max( vis, fVis * step( 0.001, abs( h - hb ) ) );
	}

	float bump = uBumpStrength * vSkinA.w;
	skGradDetail = g * bump;
	skGradMacro *= uBumpStrength;

	// ---- albedo
	float cs = smoothstep( uCounterShade.x, uCounterShade.y, nR.y + 0.3 * mottle );
	vec3 col = mix( uVentralColor, uDorsalColor, cs ) * vSkinB.rgb;
	col *= 1.0 + uMottle * mottle;
	col *= 1.0 + uGrainTint * n4.x * grainVis;
	col *= 1.0 + uScaleTint * ( rnd - 0.5 ) * 2.0 * vis;
	col *= 1.0 + uTopLight * top * vis;
	col *= 1.0 - 0.12 * boss;                                       // bosses: slightly darker, read by relief
	float crackAmt = max( crack * vis, foldCrease * 0.6 );
	col *= 1.0 - uCrackDark * mix( 0.18, crackAmt, vis );           // faded: keep the mean darkening of the mortar
	col *= 1.0 - uCavityDark * vSkinCavity;
	float dustW = uDust * ( 1.0 - smoothstep( uDustHeight * 0.25, uDustHeight, p.y ) ) * ( 0.55 + 0.45 * n2.x );
	dustW *= mix( 0.45, 1.0, max( crack * vis, 0.25 ) );
	col = mix( col, uDustColor, clamp( dustW, 0.0, 1.0 ) );
	diffuseColor.rgb *= max( col, 0.0 );

	// ---- roughness
	float rough = uRoughness + vSkinB.a + uRoughVar * n3;
	rough += uCrackRough * crack * vis + uTopRough * top * vis + 0.14 * ( fract( rnd * 7.31 ) - 0.5 ) * vis;
	rough += uFadeRough * ( 1.0 - vis ) * step( 0.001, bump );
	rough += 0.25 * dustW;
	skRoughness = clamp( rough, 0.08, 1.0 );

	// ---- occlusion: aCavity + micro occlusion in cracks and fold creases
	skAO = ( 1.0 - uCavityAO * vSkinCavity ) * ( 1.0 - 0.35 * crack * vis ) * ( 1.0 - 0.3 * foldCrease );
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
	vec3 ndl = mix( vec3( ndlD ), vec3( ndlS ), blur );
	vec3 wrap = uSSS * uScatterColor;
	vec3 diff = clamp( ( ndl + wrap ) / ( 1.0 + wrap ), 0.0, 1.0 );
	vec3 irrD = directLight.color * diff;
	reflectedLight.directDiffuse += irrD * BRDF_Lambert( material.diffuseColor );
	float dotNL = saturate( ndlD );
	reflectedLight.directSpecular += dotNL * directLight.color * BRDF_GGX( L, geometryViewDir, geometryNormal, material );
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
    uHotspots: { value: hot }, uHotspotBoost: { value: o.hotspotBoost },
    uMidlineBoost: { value: o.midlineBoost }, uMidlineWidth: { value: o.midlineWidth }, uFoldBase: { value: o.foldBase },
    uSeed: { value: o.seed * 17.123 }, uBaseScale: { value: o.baseScale }, uJitter: { value: o.jitter }, uSizeVar: { value: o.sizeVar },
    uMortar: { value: o.mortar }, uDomeRadius: { value: o.domeRadius }, uScaleHeight: { value: o.scaleHeight },
    uSoftness: { value: o.softness }, uBumpStrength: { value: o.bumpStrength },
    uFeatureScale: { value: o.featureScale }, uFeatureHeight: { value: o.featureHeight },
    uScuteSize: { value: v3(THREE, o.scuteSize) }, uScuteHeight: { value: o.scuteHeight },
    uMetaAxis: { value: v3(THREE, o.metaAxis).normalize() }, uToeAxis: { value: v3(THREE, o.toeAxis).normalize() },
    uToeHeight: { value: o.toeHeight },
    uFoldDepth: { value: o.foldDepth }, uFoldDirs: { value: o.foldDirs.map((d) => { const a = v3(THREE, d).normalize(); return new THREE.Vector4(a.x, a.y, a.z, d[3]); }) }, uLumpDepth: { value: o.lumpDepth }, uLumpScale: { value: o.lumpScale },
    uGrainScale: { value: o.grainScale }, uGrainDepth: { value: o.grainDepth }, uGrainTint: { value: o.grainTint },
    uDorsalColor: { value: col(o.dorsalColor) }, uVentralColor: { value: col(o.ventralColor) },
    uCounterShade: { value: new THREE.Vector2(o.counterShade[0], o.counterShade[1]) },
    uMottle: { value: o.mottle }, uMottleScale: { value: o.mottleScale }, uScaleTint: { value: o.scaleTint },
    uTopLight: { value: o.topLight }, uCrackDark: { value: o.crackDark }, uCavityDark: { value: o.cavityDark },
    uCavityAO: { value: o.cavityAO }, uRoughness: { value: o.roughness }, uRoughVar: { value: o.roughVar },
    uCrackRough: { value: o.crackRough }, uTopRough: { value: o.topRough }, uFadeRough: { value: o.fadeRough },
    uScatterColor: { value: v3(THREE, o.scatterColor) }, uSSS: { value: o.sss }, uSSSNormalBlur: { value: o.sssNormalBlur },
    uTranslucency: { value: o.translucency },
    uDustColor: { value: col(o.dustColor) }, uDust: { value: o.dust }, uDustHeight: { value: o.dustHeight },
    uDetailFade: { value: o.detailFade }, uWorldScale: { value: o.worldScale },
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
      .replace('#include <aomap_fragment>', FRAG_AO);
  };
  // One compiled program is shared by every skin material of the same kind.
  mat.customProgramCacheKey = () => 'noctis-reptile-skin-v1' + (o.physical ? '-phys' : '');
  mat.userData.skinUniforms = U;
  mat.userData.skinRegions = regions;
  // Re-upload the region tables after editing material.userData.skinRegions[...].
  mat.userData.updateRegions = () => {
    const r = regionUniforms(THREE, regions, U.uBaseScale.value);
    U.uRegionParams.value = r.params; U.uRegionTint.value = r.tint; U.uRegionFold.value = r.fold;
  };
  return mat;
}

// Points the scute frames at the real foot bones (rest pose): distal metatarsus direction from
// metaL -> toesL, toe frame below the metatarsophalangeal joint. bones = TREX_BONES-like array.
export function configureSkinForBones(material, bones) {
  const U = material.userData.skinUniforms;
  const get = (n) => bones.find((b) => b.name === n);
  const meta = get('metaL') || get('metaR'), toes = get('toesL') || get('toesR');
  if (!meta || !toes) return;
  const d = [toes.head[0] - meta.head[0], toes.head[1] - meta.head[1], 0];
  const l = Math.hypot(d[0], d[1]) || 1;
  U.uMetaAxis.value.set(d[0] / l, d[1] / l, 0);
  // The toe frame applies below roughly the top of the toe pads (joint height + a toe radius).
  U.uToeHeight.value = toes.head[1] + 0.06;
}

// Optional, recommended for meshes whose aRegion changes abruptly from one vertex to the next
// (e.g. SDF meshes): bakes the region tables into per-vertex attributes (aSkinA/B/C) and blurs
// them over the mesh surface, so scale size, tint, roughness and fold weights cross-fade over
// ~radius metres instead of switching inside one triangle (iterations: override the count). It enables the
// SKIN_BAKED_REGIONS define on the material: every mesh drawn with that material must then be
// baked. Re-run it after editing material.userData.skinRegions. Vertices that share a position
// (seams) are welded for the blur, so no crack opens along duplicated vertices.
export function bakeSkinRegions(THREE, geometry, material, { radius = 0.08, iterations } = {}) {
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
  material.defines = { ...(material.defines || {}), SKIN_BAKED_REGIONS: '' };
  material.needsUpdate = true;
  return geometry;
}
