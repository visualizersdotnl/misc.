
/*
    TPB-07 effect: caged metaballs

	Credits:
	- Superplek
	- Mercury (HG_SF) + various Shadertoy users
	- TropicalTrevor
	- IQ
*/

// -- Defines --

#define sat(x) clamp(x,0.0,1.0)

// Always set these up!
#define MARCH_ITER 128
#define MARCH_EPS 0.0001
#define MARCH_FAR 24.

#define SHADOW_ITER 64
#define SHADOW_FAR 8.

#define AO_ITER 8

// Shadertoy-specific (FIXME: add iResolution!)
#define time (iTime*1.1)

// -- Function(s) taken from HG_SDF --

// Changed numbers so they're explicitly floats
#define PI 3.14159265
#define TAU (2.*PI)
#define PHI (sqrt(5.)*0.5 + 0.5)

float vmax(vec3 v) {
    return max(max(v.x, v.y), v.z);
}

float fSphere(vec3 p, float r) {
	return length(p) - r;
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

float fOpPipe(float a, float b, float r) {
	return length(vec2(a, b)) - r;
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
    vignetting = sat(pow(vignetting, .25));
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

#define MAT_BALLS 0
#define MAT_RIBBONS 1
#define MAT_NONE 3

float g_balls;
float g_ribbons;

int g_matID;

float fBlobSphere(vec3 p, float scale)
{
    return fSphere(vec3(p.x, p.y, p.z-.5 /* Push fwd. a bit */), scale);
}

float fBalls(vec3 p, float scale)
{
    vec3 offset1 = vec3(cos(time*1.3), sin(time*0.4), cos(time*0.6));
    vec3 offset2 = vec3(sin(time*0.5), sin(time*0.6), sin(time*0.93));
    float sphere1 = fBlobSphere(p+offset1, scale);
    float sphere2 = fBlobSphere(p+offset2, scale);

    vec3 offset3 = vec3(1.2*sin(time*0.3 + 0.4), sin(time*0.7), 1.3*sin(time*0.4));
    vec3 offset4 = vec3(cos(time*0.6 + 0.2), 1.4*cos(time*0.3), sin(time*0.63));
    float sphere3 = fBlobSphere(p+offset3, scale);
    float sphere4 = fBlobSphere(p+offset4, scale);

    vec3 offset5 = vec3(0.4*sin(time*0.6 + 0.54), sin(time*0.32), 1.3*sin(time*1.6));
    vec3 offset6 = vec3(1.3*cos(time*0.9 + 0.12), 0.8*cos(time*0.3), sin(time*0.43));
    float sphere5 = fBlobSphere(p+offset5, scale);
    float sphere6 = fBlobSphere(p+offset6, scale);
    
    float sphere12 = fOpUnionSoft(sphere1, sphere2, 0.13);
    float sphere34 = fOpUnionSoft(sphere3, sphere4, 0.26);
    float sphere56 = fOpUnionSoft(sphere5, sphere6, 0.4);
    float sphere1234 = fOpUnionSoft(sphere12, sphere34, 0.5);
    
    float result = fOpUnionSoft(sphere1234, sphere56, 0.5);
    
    return result;
}

float fScene(vec3 p)
{
    float d = MARCH_FAR;
    
    g_balls = fBalls(p, 0.4);
    g_balls = fOpPipe(g_balls, g_balls, 0.4);
    d = min(g_balls, d);

    float ballsRibbon1 = fBalls(p, .95);
    float ballsRibbon2 = fBalls(p, 1.1);
    float ballsRibbon  = max(-ballsRibbon1, ballsRibbon2);
    
    vec3 boxPos = p + time*vec3(.0, .75, .0);
    float boxMod = 0.314;
    boxPos = mod(boxPos, boxMod)-boxMod*0.5;
    float box = fBox(boxPos, vec3(16., 0.075 /* FIXME: parametrize! */, 16.));
    g_ribbons = max(box, ballsRibbon);
    
    // Works well to hide intersecting geometry
    d = fOpUnionSoft(g_balls, g_ribbons, 0.45);
    
    return d;
}

void fSetSceneMatID(vec3 hit)
{
    g_matID = MAT_NONE;
    
    float d = fScene(hit);
    if (d >= g_ribbons)
        g_matID = MAT_RIBBONS;
    else
        g_matID = MAT_BALLS;
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

void fTextbookLighting(vec3 lpos, vec3 hit, vec3 origin, vec3 normal, out float diffuse, out float specular)
{
    // Textbook (single) lighting
    vec3 ldir = normalize(lpos-hit);
    diffuse = fDiffuse(hit, normal, ldir);
    specular = fSpecular(origin, hit, normal, ldir, 24.);
}        

// -- Main --

// For checkers pattern on ground plane
#define GROUNDSPACING 0.5
#define GROUNDGRID 0.05

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 p2D = fFragCoord2Pos2D(fragCoord);
    
    vec3 origin = vec3(0., 0., -3.); // Bit to the left..

    vec3 direction = vec3(p2D.x, p2D.y, 1.);
    mat2 mDirRot = fRot(0.214*sin(time*0.1));
    direction.xy *= mDirRot;
    direction.zy *= mDirRot;
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
        float diffuse, specular;
        
        if (MAT_BALLS == g_matID)
        {
            vec3 normal = fNormal(hit, march, 0.1);
            vec3 lpos = vec3(direction.x, direction.y, -4.);
            fTextbookLighting(lpos, hit, origin, normal, diffuse, specular);

            shadow = fShadow(hit, normalize(lpos-hit), 0.001, SHADOW_FAR);

            vec3 albedo = 2.*vec3(.2, 0.67, 1.); // vec3(.1, .5, 1.);

            vec2 UV = vec2(hit.x, hit.y);
            vec3 map = texture(iChannel0, UV).xyz;
            albedo *= map;

            color = 0.4*albedo + albedo*diffuse + specular;
            
            alpha = 0.4 + diffuse + 2.5*specular;
        }
        else if (MAT_RIBBONS == g_matID)
        {
            vec3 normal = fNormal(hit, march, 0.01);
            float land_of = fAO(hit, normal, 0.15);
            
            vec3 lpos = vec3(direction.x, direction.y + 2., -6.);
            fTextbookLighting(lpos, hit, origin, normal, diffuse, specular);

            vec3 albedo = vec3(0.7, 0.3, 0.2);
            color = 1.4*albedo * (diffuse+specular);
            color += specular;
            color = 0.5*color + color*land_of;

			// Somehow I liked this?>                
            alpha = .4 + .6*land_of*(diffuse+specular);
        }
        
        // Apply shadow
        float ssShadow = smoothstep(0., 1., shadow);
        color = color*0.4 + 0.6*color*vec3(ssShadow);
                
        // Fog scene
        float fog = 1.-(exp(-0.01224*total*total*total));
        vec3 fogColor = vec3(0.2, 0.1, 0.25)*0.4;
        color = mix(color, fogColor, fog);
        
        // Tone mapping
        color = fDesaturate(color, 0.314); // Overdo it to get a nice gold and blue-ish hue
        color = .46*ACESFilm(color) + .53*color;
    }
    
    // Vignette
    color = fVignette(color, fragCoord);

    fragColor = vec4(color, alpha);
}
