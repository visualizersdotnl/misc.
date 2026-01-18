
/*
	TPB-07 effect: castle / "Trevor's dungeon"

	Credits:
	- Trevor
	- Superplek
	- Various Shadertoy users
*/

// -- Misc. --

#define time (iTime*0.9)

mat2 fRot(float theta)
{
    return mat2(cos(theta), sin(theta), -sin(theta), cos(theta));
}

float a(vec2 uv)
{
    uv.y = abs(uv.y);
    vec4 ss = vec4(uv.xy, .11, .79) - uv.yxyy + vec4(1.5,-1.2,0,0);
    return min(min(min(ss.x,ss.w),-uv.x),max(ss.y,ss.z));
}

// -- Unity logo --

// Source: https://www.shadertoy.com/view/ld3XDse 
// Edited it to rotate and scale to a corner for this specific shader.

vec4 fUnityLogo(in vec2 fragCoord)
{
    mat2 mrot = fRot(time*0.3);
    vec2 uv = (fragCoord - iResolution.xy*0.5) * 16. / iResolution.y,
         sup = vec2(-.5, .866),
         supo = vec2(.866, .5);
    
    // EDIT: move to corner
    uv.x -= 12.;
    uv.y -= 6.;
            
    // improved s by Fabrice! Thanx!
    // EDIT: added rotation
    float s = max(a(mrot*uv),max(a(mrot*uv*mat2(-.5,.866,.866,.5)),a(-mrot*uv*mat2(.5,.866,.866,-.5))));

    float logo = smoothstep(-fwidth(uv.x)*1.9,.0,s)*0.7;
    vec4 col = vec4(.13,.17,.22,logo) + logo; 

    float i = smoothstep(.5,1.,sin(time*2.7 + uv.x + uv.y));
    
    // EDIT: made it much brighter
    col *= 0.9+(vec4(1)*s*.2+0.2*i);
   
    return clamp(col,.0,1.);
}

// -- Tone mapping --

#define sat(x) clamp(x,0.0,1.0)

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm( vec3 x )
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return sat((x*(a*x+b))/(x*(c*x+d)+e));
}

// Good & fast sRgb approximation from http://chilliant.blogspot.com.au/2012/08/srgb-approximations-for-hlsl.html
vec3 LinearToSRGB(vec3 rgb)
{
    rgb=max(rgb,vec3(0,0,0));
    return max(1.055*pow(rgb,vec3(0.416666667))-0.055,0.0);
}

// -- Main --

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec3 cd = texelFetch(iChannel0, ivec2(fragCoord), 0).xyz;
    
    // shit grading
    // make darks more purple
    cd *= mix(vec3(1.0, 0.9, 1.4), vec3(1.0), pow(sat(dot(cd, vec3(0.5, 0.7, 0.2))), 0.5));
    
    // tone mapping
    cd = LinearToSRGB(cd);
  //  cd = ACESFilm(cd); // max(cd, 0.0)); // <SUPERPLEK>
    
    // shit film grain
//    cd += (h1(fragCoord + time) - 0.5) * 0.03;
    
    // vignette
  //  vec2 uv = fragCoord/iResolution.xy;
  //  uv *=  1.0 - uv.yx;
 ///   float vig = uv.x*uv.y * 25.0;
//    vig = sat(pow(vig,0.25));
//	cd *= vig;

    // Add Unity logo, deal with 'em!
    vec4 unity = fUnityLogo(fragCoord.xy);    cd = mix(cd, unity.xyz, unity.w);
    cd.z += unity.w*0.328;
    

    fragColor = vec4(cd,1.0);
}
