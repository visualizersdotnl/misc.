#define RAYMARCH_ITERATIONS 128
#define EPSILON 0.001
#define SPIKE_LENGTH 1.628
#define PI 3.1415926539

// To see Keetels' shape
// #define OCTOPUSSY

// -- Superplek's crap --

#define time iTime

#define sat(x) clamp(x,0.0,1.0)

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

mat2 fRot(float theta)
{
    return mat2(cos(theta), sin(theta), -sin(theta), cos(theta));
}

// -- Unity logo --

// Source: https://www.shadertoy.com/view/ld3XDse 
// Edited it to rotate and scale to a corner for this specific shader.

float a(vec2 uv)
{
    uv.y = abs(uv.y);
    vec4 ss = vec4(uv.xy, .11, .79) - uv.yxyy + vec4(1.5,-1.2,0,0);
    return min(min(min(ss.x,ss.w),-uv.x),max(ss.y,ss.z));
}

vec4 fUnityLogo(in vec2 fragCoord)
{
    mat2 mrot = fRot(time*0.3);
    vec2 uv = (fragCoord - iResolution.xy*0.5) * 3.14 / iResolution.y,
         sup = vec2(-.5, .866),
         supo = vec2(.866, .5);
            
    // improved s by Fabrice! Thanx!
    // EDIT: added rotation
    float s = max(a(mrot*uv),max(a(mrot*uv*mat2(-.5,.866,.866,.5)),a(-mrot*uv*mat2(.5,.866,.866,-.5))));

    float logo = smoothstep(-fwidth(uv.x)*1.9,.0,s)*0.7;
    vec4 col = vec4(.13,.17,.22,logo) + logo; 

    float i = smoothstep(.5,1.,sin(time*2.7 + uv.x + uv.y));
   
    return clamp(col,.0,1.);
}

// -- Keetels' octopussy --

float smin(float a, float b, float k)
{
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

vec2 rotate2D(vec2 plane, float angle) 
{
    return cos(angle) * plane + sin(angle) * vec2(plane.y,-plane.x);
}

vec3 rotate3D(vec3 p, vec3 axis, float angle) 
{
    vec3 a = cross(axis, p);        
    vec3 b = cross(a, axis);
    
    return b * cos(angle) + a * sin(angle) + axis * dot(p, axis);   
}

vec3 fold(vec3 p, vec3 dir) 
{
    return p + max(0.0, -2. * dot(p, dir)) * dir;
}

float spikeball(vec3 p, float angle)
{   
    vec3 p0 = p; 
    vec3 p1 = p0;

    float d0 = 0.0;
    float d1 = sqrt(1.0 / 3.0);
    float d2 = sqrt(2.0 / 3.0);
    float d3 = sqrt(1.0 / 4.0);
    float d4 = sqrt(3.0 / 4.0);
        
    vec3 a = vec3(d0, d1, d2);
    vec3 b = vec3(d2 * d4, d1, -d2 * d3); 
    vec3 c = vec3(-d2 * d4, d1, -d2 * d3);
    
    p = normalize(p);
    p0 = normalize(p0);
    p1 = normalize(p1);
    
    float l = 2.6;
        
    p = fold(p, a);
    p = fold(p, b);
    p = fold(p, c);
    
    float spike = -smin(-(p.x + p.y + p.z)/l, -(angle - SPIKE_LENGTH), 1.00);           
    
    p0 = rotate3D(p0, vec3(0,1,0), -90.0);

    p0 = fold(p0, a);
    p0 = fold(p0, b);
    p0 = fold(p0, c);    
    
    spike = smin(spike, -smin(-(p0.x + p0.y + p0.z)/l, -(angle - SPIKE_LENGTH), 1.0), 0.24);
    
    p1 = rotate3D(p1, vec3(0,0,1), 90.0);

    p1 = fold(p1, a);
    p1 = fold(p1, b);
    p1 = fold(p1, c);    
    
    spike = smin(spike, -smin(-(p1.x + p1.y + p1.z)/l, -(angle - SPIKE_LENGTH), 1.0), 0.24);
     
    return spike * 0.9;  
}

float map(vec3 p)
{
    vec3 p0 = p;
    
    float angle = length(p);
   
    p.xy = rotate2D(p.xy, angle - iTime * 0.6);
    p.yz = rotate2D(p.yz, angle - iTime * 0.5);
    p.xz = rotate2D(p.xz, angle - iTime * 0.7);
       
    float d = spikeball(p, angle);   
    
    float sphere = length(p0) - 0.3;    
   
    d = smin(d, sphere, 0.3);    
    
    return d;
}

vec3 calcNormal(vec3 p)
{
    float dist = map(p);
    return normalize(vec3(map(p + vec3(EPSILON, 0.0, 0.0)) - dist,
                          map(p + vec3(0.0, EPSILON, 0.0)) - dist,
                          map(p + vec3(0.0, 0.0, EPSILON)) - dist));
}

float Raymarch(vec3 origin, vec3 direction)
{
    float t = 0.0;
    
    for (int i = 0; i < RAYMARCH_ITERATIONS; i++) 
    {
        vec3 pos = origin + t * direction;
        float dist = map(pos);
        t += dist;
        
        if(dist < EPSILON)
            return t - EPSILON;  
    }
    return 0.0;
}

// taken from Heavyweight Grand Prix shader #1
float lauraspec(vec3 eye, vec3 hit, vec3 normal, vec3 ldir)
{
    vec3 v = normalize(eye-hit);
	vec3 h = normalize(ldir+v);
    float spec = pow(max(dot(normal, h), 0.), 4.);
    return spec;
}

// -- 

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 color = vec3(.0);
#ifdef OCTOPUSSY
    vec2 uv = (2.0 * fragCoord - iResolution.xy) / iResolution.y;
    
    vec3 ro = vec3(0.1 * sin(iTime * 0.4), 0.1 * cos(iTime * 0.6), -7.0);
    vec3 rd = normalize(vec3(uv, 0.0) - ro);

    rd.xy = rotate2D(rd.xy, 0.1 * iTime);
    
    float dist = Raymarch(ro, rd);
    
    if (dist > 0.0)
    {
        vec3 p = ro + dist * rd;
        vec3 N = calcNormal(ro + dist * rd);
        vec3 L = -rd;
        
        float speccy = lauraspec(ro, p, N, L);
        color += speccy;

        vec3 c0 = vec3(1,0.8,0.6) * N.y;
        color = c0;
        
        vec3 c1 = vec3(0.4,1,1) * -N.z;
        color += .5 * c1;
        
        vec3 c2 = vec3(0.5,0.5,1);
        
        float ambient = mod(0.5 + 0.45 * cos(dist * 7.0), 1.0);
        color *= 0.7 + ambient * c2;
    }
#else        
    color = fUnityLogo(fragCoord).xyz;
#endif

    // FIXME: perhaps do this post radial blur?
    color = fDesaturate(color, 0.1);
    color = fVignette(color, fragCoord);
        
    fragColor = vec4(color, 1);
}