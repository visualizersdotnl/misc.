float perlin(vec2 uv, float wrap)
{
    return perlin(uv, vec2(wrap), 17, 2.0, 0.5);
}

void mainCubemap( out vec4 fragColor, in vec2 fragCoord, in vec3 rayOri, in vec3 rayDir )
{
//    if(iFrame==0)
 //   {
        vec3 r = abs(rayDir);
        float axis = max(r.x, max(r.y, r.z));

        // fragColor = vec4(0.0);

        vec2 uv = fragCoord/iResolution.y;
        // +X face
        if(axis == r.x && rayDir.x > 0.0)
            fragColor.x = perlin(uv, 4.0);
 //   }
  //  else
   //     fragColor = texture(iChannel0, rayDir);
}
