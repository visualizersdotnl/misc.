
/*
    TPB-07 effect

	Credits:
	- Superplek
    - Mercury (HG_SDF) + various Shadertoy users
    - Trevor/PB
	- IQ
*/

// -- Defines --

#define sat(x) clamp(x,0.0,1.0)

// Always set these up!
#define MARCH_ITER 128
#define MARCH_EPS 0.0001
#define MARCH_FAR 24.

#define SHADOW_ITER 24
#define SHADOW_FAR 8.

#define AO_ITER 6

// Shadertoy-specific (FIXME: add iResolution!)
#define time (iTime*.5)

// -- Function(s) taken from HG_SDF --

// Changed numbers so they're explicitly floats
#define PI 3.14159265
#define TAU (2.*PI)
#define PHI (sqrt(5.)*0.5 + 0.5)

float vmax(vec3 v) {
    return max(max(v.x, v.y), v.z);
}

// Box: correct distance to corners
float fBox(vec3 p, vec3 b) {
    vec3 d = abs(p) - b;
    return length(max(d, vec3(0.))) + vmax(min(d, vec3(0.)));
}

// Plane with normal n (n is normalized) at some distance from the origin
float fPlane(vec3 p, vec3 n, float distanceFromOrigin) {
    return dot(p, n) + distanceFromOrigin;
}

// Similar to fOpUnionRound, but more lipschitz-y at acute angles
// (and less so at 90 degrees). Useful when fudging around too much
// by MediaMolecule, from Alex Evans' siggraph slides
float fOpUnionSoft(float a, float b, float r) {
    float e = max(r - abs(a - b), 0.);
    return min(a, b) - e*e*0.25/r;
}

// Rotate around a coordinate axis (i.e. in a plane perpendicular to that axis) by angle <a>.
// Read like this: R(p.xz, a) rotates "x towards z".
// This is fast if <a> is a compile-time constant and slower (but still practical) if not.
void pR(inout vec2 p, float a) {
    p = cos(a)*p + sin(a)*vec2(p.y, -p.x);
}

// -- Tone/Colorization --

// Thanks, Trevor!
// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return sat((x*(a*x+b))/(x*(c*x+d)+e));
}

// One of my own; I derived it from NTSC weights as far as I can remember
vec3 fDesaturate(vec3 color, float amount)
{
    return mix(color, vec3(dot(color, vec3((1.-(0.21+0.71)), 0.21, 0.71))), amount);
}

// Trevor's function
vec3 fVignette(vec3 color, in vec2 fragCoord)
{
    vec2 uv = fragCoord/iResolution.xy;
    uv *=  1. - uv.yx;
    float vignetting = uv.x*uv.y * 25.;
    vignetting =(pow(vignetting, .25));
    return color*vignetting;
}

// -- Misc. --

mat2 fRot(float theta)
{
    return mat2(cos(theta), sin(theta), -sin(theta), cos(theta));
}

vec2 fFragCoord2Pos2D(vec2 fragCoord)
{
    vec2 uv = fragCoord/iResolution.xy;
    vec2 p = -1. + 2. * fragCoord.xy/iResolution.xy;
    return p;       
}

// -- Scene --

#define MAT_FLOOR_PLANE 0
#define MAT_CEILING_PLANE 1
#define MAT_CUBES 2
#define MAT_NONE 4

float g_floorPlane;
float g_ceilingPlane;
float g_cubes;

int g_matID;

#define CUBE_SIZE (0.41803398875)
#define CUBE_GRID_X (4.314*0.247)
#define CUBE_GRID_Y (4.278*0.5)

//  Cube unrolling attempt
void fSceneCube(vec3 p, inout float d, inout float x, float y, float yOffs)
{
    float startX = (CUBE_GRID_X-1.)*0.5;
    float offsX = -startX + x;

    vec3 cubeOffs = vec3(offsX, yOffs, 0.);
    float offsLen = 0.9 + 0.2*length(cubeOffs); // Scale a little

    mat2 mRot = fRot(time + 2.*(x + 1.3*y));
    
    vec3 curPos = p+cubeOffs;
    curPos.xz *= mRot;
    curPos.yx *= mRot;

    float box = fBox(curPos, vec3(CUBE_SIZE*offsLen));
    d = fOpUnionSoft(d, box, 0.5);
    
    // Next cube please!
    x += 1.;
}

float fScene(vec3 p)
{
    float d = MARCH_FAR;
    
    mat2 mRot = fRot(iTime*0.2);
    p.xz *= mRot;
    
    g_cubes = MARCH_FAR;
    float startY = (CUBE_GRID_Y-1.)*0.5;
    for (float y = 0.; y < CUBE_GRID_Y; y += 1.)
    {
        float offsY = -startY + y;
        offsY -= 1.33; // Lift cubes up a little
        
        float x = 0.;
        fSceneCube(p, g_cubes, x, y, offsY);
        fSceneCube(p, g_cubes, x, y, offsY);
        fSceneCube(p, g_cubes, x, y, offsY);
        fSceneCube(p, g_cubes, x, y, offsY);
    }
    
    d = g_cubes;
    
    // Floor plane
    g_floorPlane = fPlane(p, vec3(0., 1., 0.), 2.75);
    d = min(g_floorPlane, d);
    
    // Ceiling plane
    g_ceilingPlane = fPlane(p, vec3(0., -1., 0.), 7.5);
    d = min(g_ceilingPlane, d);
    
    return d;
}

void fSetSceneMatID(vec3 hit)
{
    g_matID = MAT_NONE;
    
    float d = fScene(hit);
    if (g_floorPlane == d)
        g_matID = MAT_FLOOR_PLANE;
    else if (g_ceilingPlane == d)
        g_matID = MAT_CEILING_PLANE;
    else if (g_cubes == d)
        g_matID = MAT_CUBES;
}

// -- Lighting --

// Function uses 'march' (last trace dist.), saves 1 fScene() call
// Simple function, might need some more love (FIXME)
vec3 fNormal(vec3 p, float march, float eps)
{                    
    float center = march;
    vec3 normal;
    normal.x = fScene(vec3(p.x+eps, p.y, p.z))-center;
    normal.y = fScene(vec3(p.x, p.y+eps, p.z))-center;
    normal.z = fScene(vec3(p.x, p.y, p.z+eps))-center;
    return normalize(normal);
}

float fDiffuse(vec3 p, vec3 normal, vec3 ldir)
{
    float diffuse = max(0., dot(normal, ldir));
    return diffuse;
}

float fSpecular(vec3 origin, vec3 p, vec3 normal, vec3 ldir, float power)
{
    vec3 v = normalize(origin-p);
    vec3 h = normalize(ldir+v);
    return pow(max(dot(normal, h), 0.), power);
}

// by IQ
// Just another march, this time to accumulate diff. between points along the normal
// Requires a fair bit of f*cking around to get just right depending on geometry
float fAO(in vec3 pos, in vec3 normal, in float strength)
{
    float occ = .0;
    float sca = 1.;
    for(int i = 0; i < AO_ITER; i++)
    {
        float h = .001 + strength*float(i)/4.;
        float d = fScene(pos + h*normal);
        occ += (h-d)*sca;
        sca *= .95;
    }

    return clamp(1. - 1.5*occ /* was 1.5 */, .0, 1.);    
}

// First attempt (adapted from IQs examples)
float fShadow(vec3 pos, vec3 ldir, float minTotal, float maxTotal)
{
    float shadow = 1.;

    // Big, such that Y is zero on the first iteration
    float ph = 1e10; 

    float total = minTotal;
    float march;
    
    for (int i = 0; i < SHADOW_ITER; ++i)
    {
        march = fScene(pos + ldir*total);
        total += march;

        // FIXME: figure out what exactly IQ is doing here
        float y = march*march/(2.*ph);
        float d = sqrt(march*march-y*y);
        shadow = min(shadow, 20.*d/max(0., total-y)); // Multiplier (20.) varies among geometry for good results (FIXME: parameter?)
        ph = march;

        if (shadow < MARCH_EPS || total > maxTotal)
            break;
    }
    
    return clamp(shadow, 0., 1.);
}

// -- Main --

// For checkers pattern on ground plane
#define GROUNDSPACING 0.4314
#define GROUNDGRID 0.05

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p2D = fFragCoord2Pos2D(fragCoord);
    
    vec3 origin = vec3(0.624, 1., -3.75); // Bit to the left..

    vec3 direction = vec3(p2D.x, p2D.y, 1.);
    mat2 mDirRot = fRot(0.214*sin(time*0.1));
    direction.xy *= mDirRot;
    direction = normalize(direction);

    // March pass 1: camera to geometry
    float march;
    float total = 0.;
    vec3 hit = origin;
    for (int i = 0; i < MARCH_ITER; ++i)
    {
        march = fScene(hit);
        total += march;

        if (march < MARCH_EPS || total > MARCH_FAR)
            break;

        hit = origin + direction*total;
    }
    
    // Figure out which material, if any, we hit
    fSetSceneMatID(hit);
    
    // Shade!
    float shadow = 1.;
    vec3 bgColor = vec3(0.);
    vec3 color = vec3(bgColor);
    float alpha = 0.;
    
    if (MAT_NONE != g_matID)
    {
        // Textbook (single) lighting
        vec3 lpos = vec3(2., 4, -6.);
        vec3 ldir = normalize(lpos-hit);
        vec3 normal = fNormal(hit, march, 0.1);
        float diffuse = fDiffuse(hit, normal, ldir);
        float specular = fSpecular(origin, hit, normal, ldir, 6.);
        
        if (MAT_FLOOR_PLANE == g_matID)
        {
            // Cubes cast shadow on floor plane
            shadow = fShadow(hit, ldir, 0.001, SHADOW_FAR);

            vec2 checkersUV = vec2(hit.x*0.25, hit.z*0.25 + iTime*1.87);
            
            // https://www.shadertoy.com/view/3sVXWz
            vec2 checkers = smoothstep(vec2(GROUNDGRID*0.75), vec2(GROUNDGRID), abs(mod(checkersUV, vec2(GROUNDSPACING))*2.-GROUNDSPACING));

            // Shade along Z
            checkers *= clamp(0., 1.4, 0.6 + hit.z);

            vec3 albedo = vec3(1., 0.9, 0.77)*checkers.x*checkers.y;
            color = vec3(0.4, 0.5, 0.) + diffuse*albedo + pow(specular, 4.);
            alpha = 1.;
        }
        else if (MAT_CEILING_PLANE == g_matID)
        {
            // Just a simple dull ceiling
            vec3 albedo = vec3(1., 0.5, 0.3);
            color = albedo*0.8 + albedo*diffuse;
        }
        else if (MAT_CUBES == g_matID)
        {
            shadow = fShadow(hit, ldir, 0.001, SHADOW_FAR);

            float AO = fAO(hit, normal, .01);
            
            vec3 albedo = vec3(.4, .5, 1.);
            color = 0.4*albedo + diffuse*albedo + specular;
            
            color *= AO;
            color -= AO*0.4314;

            alpha = diffuse; // Use blur!
        }
        
        // Apply shadow
        float ssShadow = smoothstep(0., 1., shadow);
        color = color*0.4 + 0.6*color*vec3(ssShadow);
                
        // Fog scene
        float fog = 1.-(exp(-0.0008*total*total*total));
        vec3 fogColor = vec3(0.7, 0.5, 0.3)*0.37;
        color = mix(color, fogColor, fog);
        
        // Tone mapping
        color = fDesaturate(color, 1.414); // Overdo it to get a nice gold and blue-ish hue
        color = .44*ACESFilm(color) + .63*color;
    }
    
    // Vignette
    color = fVignette(color, fragCoord);

    fragColor = vec4(color, alpha);
}
