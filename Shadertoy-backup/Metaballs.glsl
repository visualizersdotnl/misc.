
/*
	TPB-07 effect

    A radial blur stolen from Shadertoy, see EDITs for my own tweaks.
	Comments left largely intact.

	FIXME: write a proper one!
*/

// -- Misc. --

#define time iTime

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


/*
	Full Scene Radial Blur
	----------------------

	Radial blur - as a postprocessing effect - is one of the first things I considered doing 
	when the multipass system came out. I've always loved this effect. Reminds me of the early 
	demos from Aardbei et al. 

	Anyway, Shadertoy user, Passion, did a really cool radial blur on a field of spheres that
	inspired me to do my own. Radial blurs are pretty straight forward, but it was still
    helpful to have Passion's version as a guide. 

    As for the radial blur process, there's not much to it. Start off at the pixel position, 
    then radiate outwards gathering up pixels with decreased weighting. The result is a
	blurring of the image in a radial fashion, strangely enough. :)

	Inspired by:

	Blue Dream - Passion
	https://www.shadertoy.com/view/MdG3RD

	Radial Blur - IQ
	https://www.shadertoy.com/view/4sfGRn

	Rays of Blinding Light - mu6k
	https://www.shadertoy.com/view/lsf3Dn

*/

// The radial blur section. Shadertoy user, Passion, did a good enough job, so I've used a
// slightly trimmed down version of that. By the way, there are accumulative weighting 
// methods that do a slightly better job, but this method is good enough for this example.


// Radial blur samples. More is always better, but there's frame rate to consider.
const float SAMPLES = 24.; 


// 2x1 hash. Used to jitter the samples.
float hash( vec2 p ){ return fract(sin(dot(p, vec2(41, 289)))*45758.5453); }


// Light offset.
//
// I realized, after a while, that determining the correct light position doesn't help, since 
// radial blur doesn't really look right unless its focus point is within the screen boundaries, 
// whereas the light is often out of frame. Therefore, I decided to go for something that at 
// least gives the feel of following the light. In this case, I normalized the light position 
// and rotated it in unison with the camera rotation. Hacky, for sure, but who's checking? :)
vec3 lOff()
{    
    vec2 u = sin(vec2(0.96, 0.) - time*0.314); // EDIT: made this range smaller & movement slower
    mat2 a = mat2(u, -u.y, u.x);
    
    vec3 l = normalize(vec3(1.5, 1., -2.5 /* EDIT: was -0.5 */));
    l.xz = a * l.xz;
    l.xy = a * l.xy;
    
    return l;
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    // Screen coordinates.
    vec2 uv = fragCoord.xy / iResolution.xy;

    // EDIT
    float decay = 0.94;
    float density = 0.314;
    float weight = 0.1;  

    // Light offset. Kind of fake. See above.
    vec3 l = lOff();
    
    // Offset texture position (uv - .5), offset again by the fake light movement.
    // It's used to set the blur direction (a direction vector of sorts), and is used 
    // later to center the spotlight.
    //
    // The range is centered on zero, which allows the accumulation to spread out in
    // all directions. Ie; It's radial.
    vec2 tuv =  uv - .5 - l.xy*0.55;
    
    // Dividing the direction vector above by the sample number and a density factor
    // which controls how far the blur spreads out. Higher density means a greater 
    // blur radius.
    vec2 dTuv = tuv*density/SAMPLES;
    
    // Grabbing a portion of the initial texture sample. Higher numbers will make the
    // scene a little clearer, but I'm going for a bit of abstraction.
    vec4 color = texture(iChannel0, uv.xy)*0.33; // EDIT: 0.25 was a bit too little
    
    // Jittering, to get rid of banding. Vitally important when accumulating discontinuous 
    // samples, especially when only a few layers are being used.
    uv += dTuv*(hash(uv.xy + fract(time))*2. - 1.);
    
    // The radial blur loop. Take a texture sample, move a little in the direction of
    // the radial direction vector (dTuv) then take another, slightly less weighted,
    // sample, add it to the total, then repeat the process until done.
    for(float i = 0.; i < SAMPLES; i++)
    {
        uv -= dTuv;
        
        // EDIT: ripped this from a comment in original src. thread
        vec4 s = texture(iChannel0, uv);
        s *= s.w; // EDIT: premultiply with alpha
		color += smoothstep(0., 1., length(s.xyz)) * s * weight;
        
        weight *= decay;
    }
    
    // Multiplying the final color with a spotlight centered on the focal point of the radial
    // blur. It's a nice finishing touch... that Passion came up with. If it's a good idea,
    // it didn't come from me. :)
    // EDIT: needs a little love (max + less impact)
    color *= (1. - max(0., dot(tuv, tuv))*0.5);

    // EDIT: blend in original, make things a bit more subtle
    vec4 origColor = (texture(iChannel0, fragCoord.xy / iResolution.xy));
    color = origColor*0.7 + color*0.4;

    // Add Unity logo, deal with 'em!
    vec4 unity = fUnityLogo(fragCoord.xy);
    color = mix(color, unity, unity.w);
    color.z += unity.w*0.328;

	fragColor = color;
}
