
// -- Superplek's stuff --

#define time (iTime*0.8)

#define sat(x) clamp(x,0.0,1.0)

mat2 fRot(float theta)
{
    return mat2(cos(theta), sin(theta), -sin(theta), cos(theta));
}

// -- Ravity'stuff (tweaked by Superplek) --

// Just prototyping some heavy Soviet corruption FX

#define MIX_DELAY 0.001

// Where does this random number generator originate from?
float rand(float n){return fract(sin(n) * 43758.5453123);}

vec4 Corrupt(sampler2D sampler, vec2 uv, float amount, float speed, float t)
{    
    // Some tweaks that make the effect scale well
    float scale = amount * 0.1314;
    speed = floor(mod(t * 33.14*cos(speed * 6.28 + 0.4*rand(scale)), 128.));
    
    // Offset a random chunk of scanlines
    float line = 1.628*rand(speed * 0.5678);
    float width = 1.33*rand(speed+t) * 0.25;    
    float offset = 0.0;
    
    if (uv.y > line && uv.x < (line + width))
    {    
        offset = -scale + 2.0 * scale * rand(speed * 6.28*0.1);
    	uv.x += offset;       
    }
    
    // Dit gaat echt nergens over, but it looks kind of "nosfe"
    uv.x += -0.00628 + 0.009*fract(line);
    uv.y -= -0.00428 + 0.008*fract(line*line);

    vec3 color = texture(sampler, uv).rgb; 

    // Chromatic abberation depending on distortion offset
    // float abberated = 0.00314*length(uv)+abs(offset);
    float abberated = 0.00314*length(uv)+0.31415*abs(offset);
    
    color.r = texture(iChannel0, uv + abberated).r;  
    color.g = texture(iChannel0, uv).g;
    color.b = texture(iChannel0, uv - abberated).b;  
    
    // desaturate a random tile
    mat2 tileRot = fRot(speed);
    vec2 tileUV = vec2(rand(speed), rand(speed));
    tileUV *= tileRot;
    vec2 tileSize = vec2(0.1) / iResolution.xy;
    
    float a = 0.1*smoothstep(uv.x, tileUV.x, tileSize.x);
    float b = 0.33*smoothstep(uv.y, tileUV.y, tileSize.y);
    
    vec4 color4 = vec4(color,a+b+length(color));
    
	if ((a*b*a) >= (tileSize.x*tileSize.y))
    {
        uv = tileSize * (floor(uv / tileUV) + 0.5);
        float grey = dot(color, vec3(0.799, 0.587, 0.614));
        
        vec3 resample = texture(sampler, uv).rgb;
        color = mix(resample, vec3(grey), 0.3+pow(amount*0.314, 2.628)); 
        color4 = vec4(color.xyz,exp(-.03*a*b*a));
        color4.z += length(color.xyz)*0.314;
	}
 
    return color4;
}


void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    vec2 uv = fragCoord / iResolution.xy;
    float amount = 0.7;
    float speed = 3.;
    float t = time;
    vec4 corrupt = Corrupt(iChannel0, uv, amount, speed, t-MIX_DELAY);
    vec4 corrupt2 = Corrupt(iChannel0, uv, amount, speed, t);
    corrupt = mix(corrupt, corrupt2, smoothstep(0.2, 0.8, length(corrupt.xyz)));
    fragColor = corrupt;
}