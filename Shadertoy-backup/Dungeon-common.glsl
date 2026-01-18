/// Utilities for noise

// hash functions
vec4 h4(vec4 p4)
{
	p4 = fract(p4 * vec4(.1031, .1030, .0973, .1099));
    p4 += dot(p4, p4.wzxy + 19.19);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}
float h1(vec4 p){return h4(p).x;}
float h1(vec2 p){return h1(p.xyxy);}

#define TILED_NOISE_SEED 11.
float snoise(vec2 v,vec2 s){v*=s;vec2 f=floor(v);v-=f;v=v*v*(3.-2.*v);return mix(mix(h1(mod(f,s)+TILED_NOISE_SEED),h1(mod(f+vec2(1,0),s)+TILED_NOISE_SEED),v.x),mix(h1(mod(f+vec2(0,1),s)+TILED_NOISE_SEED),h1(mod(f+1.,s)+TILED_NOISE_SEED),v.x),v.y);}
#undef TILED_NOISE_SEED

// procedural tiled fbm noise, useable by texture functions
#define FBM_TILED(l,c,q) float l(c v,c s,int n,float f, float w){v=mod(v,s);float t=0.,a=0.,b=1.;for(int i=0;i<n;++i){t+=q(v,s)*b;a+=b;b*=w;s*=f;}return t/a;}
FBM_TILED(perlin,vec2,snoise)
