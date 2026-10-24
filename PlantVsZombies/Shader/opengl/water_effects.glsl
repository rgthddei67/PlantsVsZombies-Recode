// Vulkan 与 OpenGL 共用的水岸片元函数；不依赖纹理或后端专有接口。
// 所有输出为预乘 alpha，圆弧/指针/岸线以像素导数抗锯齿。
float waterNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    vec4 h = fract(sin(vec4(dot(i, vec2(127.1,311.7)), dot(i+vec2(1,0),vec2(127.1,311.7)),
        dot(i+vec2(0,1),vec2(127.1,311.7)), dot(i+vec2(1),vec2(127.1,311.7)))) * 43758.5453);
    return mix(mix(h.x,h.y,f.x),mix(h.z,h.w,f.x),f.y);
}

float floodMask(vec2 uv, vec4 depths, vec2 world, float counter) {
    vec2 f = uv*uv*(3.0-2.0*uv);
    float depth=mix(mix(depths.x,depths.y,f.x),mix(depths.z,depths.w,f.x),f.y);
    // 波缘在格中心处归零，保留原玩法阈值；中间岸线连续弯曲并轻微起伏。
    float edgeWave=sin(world.x*.07+world.y*.05+counter*.023)*.5;
    depth+=edgeWave*sin(uv.x*3.14159265)*sin(uv.y*3.14159265);
    float aa=max(fwidth(depth)*1.2,.12);
    return smoothstep(-aa,aa,depth);
}

vec2 floodTextureUV(vec2 world, float counter) {
    vec2 p=world*.0125;
    float t=counter*.018;
    vec2 wave=vec2(sin(p.y*2.1+t)*.003+sin(p.y*.6+t*.7)*.004,
        sin(p.x*2.4+t*.8)*.005+sin(p.x*1.2-t*.6)*.007);
    // 全场统一水纹坐标；只取原泳池底图的水色内区，避免资源石岸重复平铺。
    // 留出场外负坐标的采样余量，四周扩展后也不会卷回纹理边上的石岸。
    return vec2(.16,.08)+world/vec2(2200.0,1000.0)*.78+wave;
}

vec2 floodCausticUV(vec2 world,float counter) {
    vec2 p=world*.0125;
    float t=counter*.014;
    return world/vec2(360.0,170.0)+vec2(sin(p.y*2.0+t)*.009,sin(p.x*2.5+t)*.025);
}

float dialBand(float r, float inner, float outer, float aa) {
    return smoothstep(inner-aa,inner+aa,r)*(1.0-smoothstep(outer-aa,outer+aa,r));
}

vec4 waterGaugeFace(vec2 uv, float value) {
    const float pi = 3.141592653589793;
    vec2 p = uv*2.0-1.0;
    float r = length(p), aa = max(fwidth(r),.002);
    float grain = waterNoise(uv*190.0)*.012;
    vec3 ink = vec3(.17,.23,.18);
    vec3 metal = vec3(.52,.45,.27) + (.12-p.y*.12+grain);
    metal += vec3(.09,.075,.045)*sin(r*170.0)*.18;
    vec3 paper = vec3(.92,.89,.75)*(1.0-.11*r*r)+grain;
    vec3 color = mix(paper,metal,smoothstep(.83-aa,.83+aa,r));
    color = mix(color,ink,dialBand(r,.964,1.0,aa));
    color = mix(color,vec3(.84,.76,.48),dialBand(r,.925,.94,aa)*.7);
    color = mix(color,ink,dialBand(r,.825,.846,aa)*.65);
    float angle = r > .0001 ? atan(p.x,-p.y) : 0.0;
    float scale = mod(angle+pi*.75+2.0*pi,2.0*pi)/(pi*1.5);
    float arc = dialBand(r,.746,.777,aa)*(1.0-step(1.0,scale));
    vec3 arcColor = scale>.75 ? vec3(.91,.12,.10) : scale>.45 ? vec3(.73,.47,.15) : vec3(.32,.48,.32);
    color = mix(color,arcColor,arc);
    float tick = abs(fract(scale*20.0+.5)-.5);
    float tickAA = max(fwidth(scale)*20.0,.002);
    float major = 1.0-step(.1,abs(mod(floor(scale*20.0+.5),5.0)));
    float tickMask = (1.0-smoothstep(.035,.035+tickAA,tick))*dialBand(r,.66-major*.045,.705,aa)*(1.0-step(1.001,scale));
    color = mix(color,ink,tickMask*.85);
    // 细长锥形指针，以有向距离覆盖像素；指针保持连续角度，不做整数取整。
    float needleAngle = (-.75+clamp(value,0.0,1.0)*1.5)*pi;
    vec2 axis = vec2(sin(needleAngle),-cos(needleAngle));
    float along = dot(p,axis), side = dot(p,vec2(-axis.y,axis.x));
    float width = mix(.026,.006,clamp((along+.12)/.80,0.0,1.0));
    float sd = max(abs(side)-width,max(-.14-along,along-.70));
    float na = max(fwidth(sd),.002);
    float needle = 1.0-smoothstep(-na,na,sd);
    color = mix(color,mix(vec3(.12,.20,.19),vec3(.35,.43,.36),smoothstep(-width,width,side)),needle);
    color = mix(color,metal,1.0-smoothstep(.062-aa,.062+aa,r));
    color = mix(color,ink,1.0-smoothstep(.027-aa,.027+aa,r));
    // 四颗嵌入式铆钉提供少量材质层次，不使用分段圆点拼轮廓。
    vec2 screwP = abs(p);
    float screw = min(length(screwP-vec2(.88,0)),length(screwP-vec2(0,.88)));
    color = mix(color,ink,(1.0-smoothstep(.023-aa,.023+aa,screw))*.65);
    float alpha = 1.0-smoothstep(.996-aa,.996+aa,r);
    return vec4(color*alpha,alpha);
}
