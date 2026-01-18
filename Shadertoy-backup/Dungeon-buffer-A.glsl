
/*
	TPB-07 effect: Trevor's dungeon (shape)

	This code is "a bit of a mess", but no effort will be made to clean it up.
	It does however beg for some speed optimization down the line.
*/

// -- <Misc. Superplek stuff> --

#define GULDEN_SNEDE 1.618

#define time (iTime*0.55)

// One of my own; I derived it from NTSC weights as far as I can remember
vec3 fDesaturate(vec3 color, float amount)
{
    return mix(color, vec3(dot(color, vec3((1.-(0.21+0.71)), 0.21, 0.71))), amount);
}

// -- </Misc. Superplek stuff> --

#define REFLECTIONS
#define FAR (5.14*2.278)
#define STEPS 128
#define REFL_FAR 4
#define REFL_STEPS 2
#define AO_ITER 8
#define NORMAL_EPS 0.0001

#define sat(x) clamp(x,0.0,1.0)
void pR(inout vec2 p, float a){p=cos(a)*p+sin(a)*vec2(p.y,-p.x);}
float Min(float a, float b, inout vec4 am, vec4 bm){if(b<a){am=bm;return b;} return a;}
float SDF(vec3 p, out vec4 m)
{
    vec3 op = p;
    
    // gold chains
    // this twists the chains over distance, with an extra sine wobble over distance, makes it look more random than regular twists
    pR(p.xy, sin(p.z * 0.5) + p.z * 0.1 - cos(p.z * 0.3));
    // TODO: I suspect ss and I are completely redundant, just I = 0 and let's go
    // vec2 ss = sign(p.xy); // identify each chain uniquely
    // float I = 0.; // <SUPERPLEK> (max(0.0, ss.x) + max(0.0, ss.y) * 2.0) ; // based on the chain create a single unique float 0,1,2,3
    float T = time*1.928; // <SUPERPLEK> iTime * 0.5 + I * 0.25; // animate the chain over time, with offsets per index, TODO: hard to see because of camera movement, maybe just delete?
    
    // <SUPERPLEK> remember this trick! + SYNC!
    //T = smoothstep(0.3, 0.9, fract(T)) + floor(T);
    
    
    p.z += T; // animate the chain over time
    p.xy = abs(p.xy); // mirror the chain in X and Y to get 4 chains (in 1 place, moved apart on the next line)
    
    // <SUPERPLEK> I don't want sh*t intersecting with my beautiful marble tiling
    //p.xy -= 1.1 + sin(p.z * 0.25) * 0.1; // move the chain 1.1 out of the center with a bit mover or a bt less over Z (the sin part)
    p.xy -= 0.85 + 0.1*sin(p.z*0.25);
    
    pR(p.xy, p.z * 0.1); // rotate based on Z = twist everything into the depth
    p.z = fract(p.z) - 0.5; // repeat in Z axis
    float r = length(vec2(length(p.xz) - 0.3, p.y)) - 0.1; // draw a torus
    m = vec4(p, 0.0); // set a default material
    p.z = abs(p.z); // our torus fits within 1 grid cell, we're gonna draw another one half a grid cell away and mirrored
    p.z -= 0.5; // this is moving it half a grid cell, mirroring above with abs creates links that connect perfectly
    float ir = length(vec2(length(p.yz) - 0.3, p.x)) - 0.1; // draw a second torus to make a chain
    r = Min(r, ir, m, vec4(p, 0.0));
    r *= 0.8; // bias against glithces, remove this for more perf but shittier visuals
    
    // purple ribbons
    p = op;
    p.z += 0.5;
    float c = floor(p.z * 0.5);
    float c2 = floor(p.x / 2.0 + 0.5);
    p.x = (fract(p .x / 2.0 + 0.5) - 0.5) * 2.0;
    p.y += 0.75;
    p.y -= (c2 == 0.0) ? 1.5 : 0.0;
    pR(p.xy, sin(c * 101.19) * 0.15);
    p.z = (fract(p.z * 0.5) - 0.5) * 2.0;
    p.y -= 4.0;
    pR(p.xz, p.y * 0.7 + sin(c * 33.19 / 6.28 + time) * 0.5 + sin(p.x * 64.0 + time) * 0.01 + cos(p.x * 43.0 + time) * 0.05);
    p.x += pow(sat(sin(c * 0.4 + time) * p.y * 0.2), 4.0) * -1.5 + p.y * 0.03;
    vec3 q = abs(p)-vec3(0.3,3.0,0.01);
    ir = max(q.x, max(q.y,q.z));
    r = Min(r, ir * ((ir > 0.5) ? 0.8 : 0.25), m, vec4(p, 1.0));
    
    // columns
    p = op;
    const float columnSpacing = 1.5;
    p.z = (fract(p.z / columnSpacing) - 0.5) * columnSpacing;
    p.x = abs(p.x) - 2.5;
    float core = length(p.xz);
    ir = max(core - 0.2, 1.45 - abs(p.y));
    ir = min(ir, max(core - 0.22, 1.5 - abs(p.y)));
    ir = min(ir, max(core - 0.18, 1.4 - abs(p.y)));
    ir = min(core - 0.15, ir);
   // r = Min(r, ir, m, vec4(p, 2.0));
    
    // walls
    p = op;
  //  r = Min(r, 3.0 - abs(p.x), m, vec4(p, 3.0));
    
    // ceiling
    ir = length(vec2(p.x, p.y - 2.0)) - 3.0;
    ir = max(-ir, 2.0 - p.y);
    r = Min(r, ir, m, vec4(p, 4.0));// 4.0 - p.y);
    
    // floor
    p = op;
    p.y += 1.8;
    vec2 c3 = floor(p.xz * 0.37 + 0.5);
    c3 = fract(sin(c3) * 19.9);
    p.xz = (fract(p.xz * 0.37 + 0.5) - 0.5) / 0.37;
    pR(p.xy, (c3.x - 0.5) * 0.025);
    pR(p.zy, (c3.y - 0.5) * -0.025);
    q = abs(p)-vec3(0.5/0.37, 0.1, 0.5/0.37)+0.05;
    ir = min(op.y+1.8,max(q.x,max(q.y,q.z)));
    r = Min(r, ir, m, vec4(op, 5.0));
    
    
    return r;
}
float SDF(vec3 p){vec4 m;return SDF(p,m);}
float Trace(vec3 ro, vec3 rd, int steps, float far)
{
    float s=0.0, t=0.0;
    for(int i = 0 ; i < steps; ++i)
    {
        t += s;
        if(t>far)break;
        vec3  p = ro+rd*t;
        s = SDF(p);
        //if((s<0.244*0.1274)||(t>far))break;
    }
    return t;
}

float sampleCube(vec2 uv, int axis, int sgn)
{
    vec2 f = fract(uv * 1024.0);
    vec2 t = floor(uv * 1024.0) / 1024.0;
    vec3 smp = vec3(float(sgn), fract(t));
    if(axis==1)
        smp=smp.yxz;
    if(axis==2)
        smp=smp.yzx;
    vec4 tx = texture(iChannel0, smp);
    float tmpt = smp[(axis==0)?1:0];
    smp[(axis==0)?1:0] = fract(smp[(axis==0)?1:0] + 1.0 / 1024.0);
    tx.y = texture(iChannel0, smp).x;
    smp[(axis==2)?1:2] = fract(smp[(axis==2)?1:2] + 1.0 / 1024.0);
    tx.w = texture(iChannel0, smp).x;
    smp[(axis==0)?1:0] = tmpt;
    tx.z = texture(iChannel0, smp).x;
    vec2 tmp = mix(tx.xz, tx.yw, f.x);
    return mix(tmp.x, tmp.y, f.y);
}

float perlin(vec2 uv)
{
    return sampleCube(uv, 0, 1);
}

float SDFBumped(vec3 p)
{
	vec4 m;
    float r = SDF(p, m);
    if(m.w == 3.0 || m.w == 4.0)
    {
    	vec2 uv = p.yz + perlin(floor(p.yz)*0.01)*19.19;
    	r += pow(perlin( perlin(uv * 0.3 + 0.11) * vec2(8.0) ), 6.0) * 0.125;
    }
    return r;
}
vec3 Gradient(vec3 p)
{
    const vec2 e = vec2(NORMAL_EPS, 0.); 
    return normalize(vec3(SDFBumped(p + e.xyy) - SDFBumped(p - e.xyy),
    SDFBumped(p + e.yxy) - SDFBumped(p - e.yxy),
    SDFBumped(p + e.yyx) * SDFBumped(p - e.yyx)));
}
struct Material
{
    vec3 albedo;
    vec3 emissive;
    float specular;
    float roughness;
    float metallicity;
    float reflectivity; // <TT> Reflection gating
};
// ShadowArgs struct & default constructor helper.
struct ShadowArgs
{
    float near;
    float far;
    float hardness;
    int steps;
};
// ShadowArgs shadowArgs(){return ShadowArgs(0.1,100.0,128.0,32);}
ShadowArgs shadowArgs(){return ShadowArgs(0.1,FAR,128.0,64);} // <SUPERPLEK>

// Basic soft shadow function from https://iquilezles.org/articles/rmshadows
float Shadow(vec3 ro, vec3 rd, ShadowArgs shadow)
{
    float dist = shadow.near;
	float atten = 1.0;
	for(int i = 0; i < shadow.steps; ++i)
	{
        #ifdef SHADOW_CASTER
		float sampl = fShadowCaster(ray.origin + ray.direction * dist);
		#else
		float sampl = SDF(ro + rd * dist);
        #endif
		if(sampl<0.001)
			return 0.0;
		if(abs(dist) > shadow.far)
			return atten;
		atten = min(atten, shadow.hardness * sampl / dist);
		dist += sampl;
	}
	return atten;
}

#define PI 3.14159265359
float sqr(float x){return x*x;}
float cub(float x){return x*x*x;}
struct LightData
{
    vec3 rayDirection;
    vec3 normal;
    vec3 worldPos;
    Material material;
    float atten;
};
float G1V(float dotNV, float k){return 1.0 / (dotNV * (1.0 - k)+k);}
float ggxSpecular(float NdotV, float NdotL, vec3 N, vec3 L, vec3 V, float roughness)
{
    float F0 = 0.5;
    // http://filmicworlds.com/blog/optimizing-ggx-shaders-with-dotlh/
    vec3 H = normalize(V + L);
    float NdotH = sat(dot(N, H));
    float LdotH = sat(dot(L, H));
    float a2 = roughness * roughness;

    float D = a2 / (PI * sqr(sqr(NdotH) * (a2 - 1.0) + 1.0));

    LdotH = 1.0 - LdotH;
    float F = F0 + (1.0 - F0) * cub(LdotH) * LdotH;

    float vis = G1V(NdotL, a2 * 0.5) * G1V(NdotV, a2 * 0.5);
    return NdotL * D * F * vis;
}
vec3 AmbientLight(LightData data, vec3 color)
{
    return data.material.albedo * color * (1.0 - data.material.specular) * data.atten;
}

vec3 RimLight(LightData data, vec3 color, float power)
{
	return pow(min(1.0 + dot(data.normal, data.rayDirection), 1.0), power) * AmbientLight(data, color);
}

// Core lighting function.
vec3 _DirectionalLight(LightData data, vec3 direction, vec3 color, float atten)
{
    float satNdotV = sat(dot(data.normal, -data.rayDirection));
    // poor man's sub surface scattering
    float satNdotL = sat(dot(data.normal, normalize(direction)));
    return color * mix(
        // diffuse
        atten * data.material.albedo * satNdotL,
        // specular
        atten * // mix(atten, 1.0, data.material.roughness) * // apply shadow on rough specular
        mix(vec3(1.0), data.material.albedo, data.material.metallicity) * // metallicity
        ggxSpecular(satNdotV, satNdotL, data.normal, normalize(direction), -data.rayDirection, max(0.001, data.material.roughness)),
        data.material.specular);
}

// Directional light.
vec3 DirectionalLight(LightData data, vec3 direction, vec3 color)
{
    return _DirectionalLight(data, direction, color, data.atten);
}

// Directional light with arg.
vec3 DirectionalLight(LightData data, vec3 direction, vec3 color, ShadowArgs shadow)
{
    return _DirectionalLight(data, direction, color, Shadow(data.worldPos, normalize(direction), shadow));
}

// Core point light function, attenuation argument can be used to apply shadows.
vec3 _PointLight(LightData data, vec3 point, vec3 color, float lightRadius, float atten)
{
    vec3 direction = point - data.worldPos;
    float d = length(direction);
    float dd = sqr(d);
    if(lightRadius>0.0)
        atten /= (1.0 + (d + d) / lightRadius + dd / sqr(lightRadius));
    else // infinitesimal light radius
        atten /= dd;
    return _DirectionalLight(data, direction, color, atten);
}

// Infinitesimal point light.
vec3 PointLight(LightData data, vec3 point, vec3 color)
{
    return _PointLight(data, point, color, 0.0, data.atten);
}

// Point light.
vec3 PointLight(LightData data, vec3 point, vec3 color, float lightRadius)
{
    return _PointLight(data, point, color, lightRadius, data.atten);
}

// Point light with radius & shadow, light radius is not used by shadowing function, only for falloff.
vec3 PointLight(LightData data, vec3 point, vec3 color, float lightRadius, ShadowArgs shadow)
{
    vec3 line = point - data.worldPos;
    
    shadow.far = length(line);
    line /= shadow.far;
    
    return _PointLight(data, point, color, lightRadius, Shadow(data.worldPos, line, shadow));
}
Material marble_black(vec2 uv)
{
    uv += perlin(floor(uv)*0.1)*19.19;
   
    Material result;
    result.albedo = 0.314 * sat(vec3(smoothstep(0.55, 0.9, perlin(perlin(uv * 0.4 + 0.11)*vec2(3.2)+uv*0.002-0.44)) + perlin(uv+0.32+perlin(uv-0.19)*0.25) * 0.25));
    result.specular = 0.1; // <SUPERPLEK>
    result.roughness = 0.5; // <SUPERPLEK>
    result.metallicity = .314;
    result.emissive = vec3(0.0);
    result.reflectivity = 0.0; // <TT> Reflection gating
    return result;
}
 
Material marble_white(vec2 uv)
{
    uv += perlin(floor(uv)*0.1)*19.19;
   
    Material result;
    result.albedo = mix(vec3(1.0), vec3(0.4, 0.45, 0.45), perlin(perlin(uv * 0.4 - 0.11)*vec2(3.2)+uv*0.02));
    result.albedo -= 0.4 * smoothstep(0.5, 0.8, perlin(perlin(uv*0.1+0.3)*vec2(3.2)+0.1));
    result.specular = 0.5; // <SUPERPLEK>
    result.roughness = 0.01; // <SUPERPLEK>
    result.metallicity = 0.314;
    result.emissive = vec3(0.);
    result.reflectivity = 0.0; // <TT> Reflection gating
    return result;
}

Material marble_checker(vec2 uv)
{
    ivec2 cl = ivec2(fract(uv * 0.37) * 2.0);
    Material result;
    if(cl.x == cl.y)
        result = marble_white(uv);
    else
        result = marble_black(uv);
    
    //<SUPERPLEK>
    result.metallicity = 1.;
    result.reflectivity = 0.25;  // <TT> Reflection gating
    
    return result;
}

Material gold(vec3 m)
{
    // <SUPERPLEK>
//    return Material(vec3(0.44, 0.32, 0.31), vec3(0.0), 0.9, 0.1, 1.0, 
//                    0.1 // <TT> Reflection gating
//                   );
    
    // vec3 tex = texture(iChannel2, m.xz).xyz;
    

    return Material(vec3(0.8, 0.4, 0.3), vec3(0.), 1., 0.1, 1.0, 
                    0.1 // <TT> Reflection gating
                   );
}

Material purpleCloth(vec3 m)
{
    float threading = sin(m.x * 128.0) * 0.5 + 0.5;
    return Material(vec3(0.4, 0.04, 0.1), vec3(0.0), threading * 0.3 + 0.05, 0.09, 1.0, 
                    0.0 // <TT> Reflection gating
                   ); }
	
Material GetMaterial(vec4 m, vec3 p)
{
    Material mtl;
    if(m.w==0.0)
    	mtl = gold(m.xyz);
    else if(m.w == 1.0)
    	mtl = purpleCloth(m.xyz);
    else if(m.w == 2.0)
    {
    	mtl = marble_white(m.yz * 2.0);
    }
    else if(m.w == 3.0  || m.w == 4.0)
    	mtl = marble_black(m.yz);
    else
    	mtl = marble_checker(m.xz);
	return mtl;
}

// <SUPERPLEK>
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
        float d = SDF(pos + h*normal);
        occ += (h-d)*sca;
        sca *= .95;
    }

    return clamp(1. - 1.5*occ /* was 1.5 */, .0, 1.);    
}

vec3 Lighting(LightData data, vec3 ro)
{
	vec3 cd = vec3(0.0);
    
    cd += AmbientLight(data, vec3(0.015, 0.015, 0.01));
	cd += RimLight(data, vec3(0.02, 0.02, 0.04), 3.0);
    
    // <SUPERPLEK> here's your lighting snafu, je kunt niet zomaar door die geometry heen gaan casten en doen, wordt 1 kolerezooi
    // the best solution here would be to take the rotation into account! FIXME!!
    cd += PointLight(data, ro + vec3(1.0, 1.0, 8.0), vec3(20.0, 17.0, 10.0), 0.0, shadowArgs());
   // cd += PointLight(data, vec3(.0, .0, 8.0)+ro, vec3(10.0, 13.0, 14.0), 0.0, shadowArgs());
  //  cd += PointLight(data, vec3(.0, .0, -8.0)+ro, vec3(10.0, 13.0, 14.0), 0.0, shadowArgs());
    
    float spacing = 5.0; // <SUPERPLEK> makes the chain a bit more Quinton 'Rampage' Jackson
    float tmp = fract(data.worldPos.z / spacing) - 0.5;
    data.worldPos.z = tmp * spacing;
    float mask = 1.0 - abs(tmp + tmp);
    cd += PointLight(data, vec3(0.0), vec3(1.0)) * mask;
    
    // <SUPERPLEK> seems unnecessary as well, deleted in _refl() func.
    data.worldPos.z = 0.0;
    cd += PointLight(data, vec3(0.0, 1.0, 0.0), 4.*vec3(0.3, 0.3, 0.2));
    
    // add a touch of AO
    float land_of = fAO(data.worldPos, data.normal, 0.05);
    cd = 0.66*cd + 0.33*cd*land_of;
    
    return cd;
}

// <SUPERPLEK> just copying to simplify modifying reflections
vec3 Lighting_refl(LightData data, vec3 ro)
{
	vec3 cd = vec3(0.0);

    // <SUPERPLEK> not really necessary
    // cd += AmbientLight(data, vec3(0.015, 0.015, 0.01));
	// cd += RimLight(data, vec3(0.02, 0.02, 0.04), 3.0);
    
    // <SUPERPLEK> same as above, but no shadows
    cd += PointLight(data, vec3(.0, .0, 8.0)+ro, vec3(20.0, 17.0, 10.0), .0);
    cd += PointLight(data, vec3(.0, .0, -8.0)+ro, vec3(20.0, 17.0, 10.0), .0);
    
    // float spacing = 5.0; // <SUPERPLEK> makes the chain a bit more Quinton 'Rampage' Jackson
    // float tmp = fract(data.worldPos.z / spacing) - 0.5;
    // data.worldPos.z = tmp * spacing;
    // float mask = 1.0 - abs(tmp + tmp);
    // cd += PointLight(data, vec3(0.0), vec3(1.0)) * mask;

    data.worldPos.z = 0.0;
    cd += PointLight(data, vec3(0.0, 4.0, 0.0), 2.*vec3(0.3, 0.3, 0.2));
    
    // skip AO too <SUPERPLEK>
    
    return cd;
}

#define FOG_CD vec3(0.15, 0.08, 0.14)

vec3 TraceAndShade(vec3 ro, vec3 rd, int steps, float far, out vec3 p, out vec3 n, out float fog, out vec3 s, out vec3 bg)
{
    float d = Trace(ro, rd, steps, far);
	bg = mix(FOG_CD, vec3(0.0), sqrt(abs(rd.y))); // <SUPERPLEK>
    fog = 1.-(exp(-0.00314*d*d*d)); // sat(d/(FAR*0.6)); // <SUPERPLEK>
    
    fog = fog*fog;
    p = ro + rd * d;
    n = Gradient(p);
    vec4 m;
    SDF(p,m);
    Material mtl = GetMaterial(m, p);
    s = mtl.reflectivity *  // <TT> Reflection gating
        	mtl.specular * mix(vec3(1.0), mtl.albedo, mtl.metallicity);
    LightData data = LightData(rd, n, p, mtl, 1.0);
	return Lighting(data, ro) + mtl.emissive;
}

// <SUPERPLEK> just copying to simplify modifying reflections
vec3 TraceAndShade_refl(vec3 ro, vec3 rd, int steps, float far, out vec3 p, out vec3 n, out float fog, out vec3 s, out vec3 bg)
{
    float d = Trace(ro, rd, steps, far);
	bg = vec3(0.0);
    fog = pow(sat(d/far), 0.25);
    
    p = ro + rd * d;
    n = Gradient(p);
    vec4 m;
    SDF(p,m);
    Material mtl = GetMaterial(m, p);
    s = mtl.reflectivity *  // <TT> Reflection gating
        	mtl.specular * mix(vec3(1.0), mtl.albedo, mtl.metallicity);
    LightData data = LightData(rd, n, p, mtl, 1.0);
	return Lighting_refl(data, ro) + mtl.emissive;
}

vec3 TraceAndShade(vec3 ro, vec3 rd, int steps, float far)
{
    vec3 p,n,bg,s;
    float fog;
   	vec3 fg = TraceAndShade(ro,rd,steps,far,p,n,fog,s,bg);
    return mix(fg, bg, fog);
}

// <SUPERPLEK> just copying to simplify modifying reflections
vec3 TraceAndShade_refl(vec3 ro, vec3 rd, int steps, float far)
{
    vec3 p,n,bg,s;
    float fog;
   	vec3 fg = TraceAndShade_refl(ro,rd,steps,far,p,n,fog,s,bg);
    return mix(fg, bg, fog);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = (fragCoord * 2.0 - iResolution.xy) / iResolution.y;
	vec3 ro = vec3(0. /* SUPERPLEK */, .35 /* SUPERPLEK: camera up! */, time * 3.6);
    vec3 rd = normalize(vec3(uv, /* 1.5 <SUPERPLEK> */ 1.) + vec3(sin(time), cos(time * 0.411), 0.0) * 0.1);
	
    // <SUPERPLEK> camera sync.(TODO)
    pR(rd.xz, PI*smoothstep(-1., 1., sin(0.414*time)));
  //  pR(rd.zy, -0.23); // <SUPERPLEK>
    pR(rd.xy, sin(time*0.628) * 3.14*0.24); // <SUPERPLEK>

    
    vec3 bg, p, n, specularColor;
    float fog;
    vec3 fg = TraceAndShade(ro, rd, STEPS, FAR, p, n, fog, specularColor, bg);
    
   // float base_fog = fog;
#ifdef REFLECTIONS
    if(dot(specularColor,specularColor)>0.01) // <TT> Reflection gating
    {
    // TODO: I may want to do reflections in a half-res pass for perf
    
    float lenpminro = length(p-ro);
    float mask = pow(1.0 / (0.5+lenpminro), 0.01);
    vec3 refldir = normalize(reflect(rd, n));
    //vec3 fg_refl = TraceAndShade_refl(p + refldir * 0.01, refldir, REFL_STEPS, REFL_FAR);
  	//fg += specularColor*fg_refl*mask;
    }
#else
    // I think this sample needs to be ^2.2 due to the image being srgb?
    // <SUPERPLEK> if you want to be fully correct, yes
    vec3 tex = texture(iChannel1, reflect(rd, n)).xyz;
    fg += specularColor * tex * 0.3; // texture(iChannel1, reflect(rd, n)).xyz * 0.1;
#endif
        
    // tone mapping
    vec3 cd = mix(fg, bg, pow(fog /*base_fog*/,3.14)); // <SUPERPLEK> refl. fog looks better than base fog
    
    fragColor = vec4(cd,1.0);
}
