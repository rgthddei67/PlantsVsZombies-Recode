// 仅显式 ShaderPreview 使用；宿主提供 previewSource()，输入/输出均为预乘 alpha。
// 7=能量护盾，8=冰晶蔓延，9=燃烧溶解。正式角色与玩法不自动启用这些效果。
float previewHash(vec2 p) {
    return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);
}

float previewNoise(vec2 p) {
    vec2 i=floor(p), f=fract(p);
    f=f*f*(3.0-2.0*f);
    return mix(mix(previewHash(i),previewHash(i+vec2(1,0)),f.x),
        mix(previewHash(i+vec2(0,1)),previewHash(i+vec2(1)),f.x),f.y);
}

float previewFbm(vec2 p) {
    float sum=0.0, amplitude=.55;
    for (int i=0;i<4;++i) {
        sum+=previewNoise(p)*amplitude;
        p=mat2(1.6,1.2,-1.2,1.6)*p+vec2(4.3,7.1);
        amplitude*=.48;
    }
    return sum;
}

vec4 previewOver(vec4 under,vec3 tint,float opacity) {
    float a=clamp(opacity,0.0,1.0);
    return vec4(tint*a+under.rgb*(1.0-a),a+under.a*(1.0-a));
}

float previewHex(vec2 p) {
    const vec2 span=vec2(1.7320508,1.0);
    vec2 a=mod(p,span)-span*.5;
    vec2 b=mod(p-span*.5,span)-span*.5;
    vec2 cell=abs(dot(a,a)<dot(b,b)?a:b);
    float d=max(dot(cell,vec2(.8660254,.5)),cell.y);
    return 1.0-smoothstep(.018,.018+max(fwidth(d),.008),abs(d-.49));
}

vec4 shaderPreview(vec2 uv,vec4 data,float seconds,int effect) {
    vec4 source=previewSource(uv);
    vec2 p=(uv-vec2(.5,.53))*2.0;
    float t=seconds;
    if (effect==7) {
        float r=length(p*vec2(1.0,.88));
        float aa=max(fwidth(r),.002);
        float inside=1.0-smoothstep(.755-aa,.755+aa,r);
        float angle=atan(p.y,p.x);
        float rim=exp(-abs(r-.755)*120.0);
        float glow=exp(-abs(r-.755)*24.0)*.30;
        float grid=previewHex(p*8.5+vec2(0,t*.12))*inside;
        float scan=pow(max(0.0,cos(angle-t*1.2)),18.0);
        float shell=(.035+pow(clamp(r/.755,0.0,1.0),4.0)*.10)*inside;
        vec4 result=previewOver(source,vec3(.07,.45,.73),shell*data.x);
        result=previewOver(result,vec3(.12,.85,1.0),(glow+grid*.14+rim*.65)*data.x);
        result=previewOver(result,vec3(.75,1.0,1.0),rim*scan*.95*data.x);
        // 局部命中涟漪随冲击年龄扩散；由演示层传入命中 UV，不推断玩法伤害。
        float distanceToHit=length((uv-data.yz)*vec2(1.0,.88));
        float age=max(data.w,0.0);
        float wave=exp(-pow((distanceToHit-age*.40)/.014,2.0))*exp(-age*1.3);
        float core=exp(-distanceToHit*32.0)*exp(-age*4.0);
        result=previewOver(result,vec3(.72,.98,1.0),(wave*.85+core)*inside*data.x);
        return result;
    }
    if (effect==8) {
        float amount=clamp(data.x,0.0,1.0);
        float front=1.08-amount*1.22+(previewFbm(uv*12.0)-.5)*.11;
        float ice=smoothstep(front-.025,front+.025,uv.y);
        vec2 crystal=uv*vec2(22,25);
        crystal.x+=floor(crystal.y)*.5;
        vec2 cell=fract(crystal);
        float facet=previewHash(floor(crystal));
        float edge=min(min(cell.x,1.0-cell.x),min(cell.y,1.0-cell.y));
        edge=min(edge,abs(cell.x-cell.y)*.7);
        float crack=1.0-smoothstep(.012,.012+max(fwidth(edge),.008),edge);
        float light=dot(source.rgb,vec3(.25,.55,.20));
        vec3 icy=vec3(.18,.56,.80)*(.65+light*.7)+vec3(.30,.36,.38)*facet;
        icy=mix(icy,vec3(.76,.96,1.0),crack*.78);
        float sweep=pow(max(0.0,1.0-abs(uv.x+uv.y*.4-fract(t*.16)*1.8)/.065),3.0);
        icy+=sweep*.40;
        vec4 result=vec4(mix(source.rgb,clamp(icy,0.0,1.0)*source.a,ice*.88),source.a);
        float nearby=max(max(previewSource(uv+vec2(.012,0)).a,previewSource(uv-vec2(.012,0)).a),
            max(previewSource(uv+vec2(0,.012)).a,previewSource(uv-vec2(0,.012)).a));
        result=previewOver(result,vec3(.45,.85,1.0),max(nearby-source.a,0.0)*ice*.65);
        // 少量程序化冰屑，属于片元装饰，不创建 ParticleSystem 或游戏实体。
        for (int i=0;i<10;++i) {
            float seed=float(i);
            vec2 center=vec2(.18+previewHash(vec2(seed,3))*.64,
                fract(previewHash(vec2(seed,9))+.075*t));
            vec2 d=uv-center;
            float sparkle=exp(-length(d)*260.0)+exp(-abs(d.x)*650.0-abs(d.y)*90.0)*.4;
            result=previewOver(result,vec3(.72,.94,1.0),sparkle*amount*.7);
        }
        return result;
    }
    float amount=clamp(data.x,0.0,1.0);
    float field=(1.0-uv.y)*.65+previewFbm(uv*10.0)*.35;
    float aa=max(fwidth(field),.003);
    float keep=smoothstep(amount-aa,amount+aa,field);
    float hot=exp(-abs(field-amount)*58.0)*source.a;
    float charred=1.0-smoothstep(.0,.10,field-amount);
    vec4 result=vec4(source.rgb*mix(1.0,.16,charred)*keep,source.a*keep);
    result=previewOver(result,vec3(1.0,.19,.015),hot*.95);
    result=previewOver(result,vec3(1.0,.90,.32),pow(hot,2.0)*.98);
    float activity=smoothstep(.05,.18,amount)*(1.0-smoothstep(.82,1.0,amount));
    for (int i=0;i<16;++i) {
        float seed=float(i);
        float phase=fract(t*.45+previewHash(vec2(seed,13)));
        float x=.32+previewHash(vec2(seed,6))*.36+sin(t*2.0+seed)*phase*.055;
        float y=clamp(1.0-(amount-.16)/.65,.18,.88)-phase*.55;
        vec2 d=(uv-vec2(x,y))*vec2(1.0,.35);
        float spark=exp(-length(d)*420.0)*(1.0-phase)*activity;
        result=previewOver(result,vec3(1.0,.43,.045),spark*.95);
    }
    return result;
}
