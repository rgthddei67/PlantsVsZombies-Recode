// 极光与时间术式的共用片元实现。几何只定义连续场，亮芯/辉光均在透明边界内平滑收束。
// data.x=阶段进度（标记为剩余比例），data.y=整体透明度；所有输出为预乘 alpha。
const float POLAR_PI = 3.14159265359;

vec4 polarOver(vec4 under, vec3 color, float alpha) {
    float a=clamp(alpha,0.0,1.0);
    return vec4(color*a+under.rgb*(1.0-a),a+under.a*(1.0-a));
}

float polarStroke(float distance, float width) {
    float aa=max(fwidth(distance),.002);
    return 1.0-smoothstep(width-aa,width+aa,abs(distance));
}

float polarSegment(vec2 p,vec2 end) {
    float h=clamp(dot(p,end)/max(dot(end,end),.0001),0.0,1.0);
    return length(p-end*h);
}

/** 极光丝带绕主体流动，中央留白，不把角色盖成一个实心光球。 */
vec4 polarChannel(vec2 p,float t,float progress) {
    vec2 q=p*vec2(1.0,.90);
    float r=length(q), angle=atan(q.y,q.x);
    float gain=.65+.35*smoothstep(0.0,1.0,progress);
    vec4 result=vec4(0);
    for(int i=0;i<3;++i) {
        float phase=float(i)*2.0944;
        float radius=.56+float(i)*.075+.075*sin(angle*3.0-t*1.8+phase)
            +.026*sin(angle*7.0+t*1.2+phase);
        float d=r-radius;
        float flow=.55+.45*pow(.5+.5*sin(angle*2.0+t*2.2+phase),2.0);
        vec3 hue=mix(vec3(.22,.96,.83),vec3(.64,.28,1.0),.5+.5*sin(angle+t*.35+phase));
        result=polarOver(result,vec3(.035,.025,.13),exp(-abs(d)*20.0)*.48*gain);
        result=polarOver(result,hue,exp(-abs(d)*22.0)*.38*flow*gain);
        result=polarOver(result,hue,polarStroke(d,.020)*.90*flow*gain);
        result=polarOver(result,vec3(.79,1.0,.97),polarStroke(d,.005)*.80*flow*gain);
        // 单侧绸带尾部渐宽，柔和的纵向纹理形成极光帘，而非裸露的圆环。
        float curtain=exp(-abs(d-.027)*13.0)*pow(.5+.5*sin(angle*18.0+t+phase),5.0);
        result=polarOver(result,hue,curtain*.26*gain);
    }
    for(int i=0;i<7;++i) {
        float phase=float(i)*.8976-t*(.28+float(i)*.012);
        vec2 center=vec2(cos(phase),sin(phase)/.90)*(.61+.05*sin(t+float(i)));
        vec2 d=p-center;
        float star=exp(-length(d)*170.0)+.25*exp(-abs(d.x)*230.0-abs(d.y)*65.0);
        result=polarOver(result,vec3(.80,1.0,.96),star*gain);
    }
    return result;
}

/** 竖向裂口有暗色纵深、扭曲边缘和流动光丝；收口阶段撤掉暗芯，让到场僵尸立即可见。 */
vec4 polarRift(vec2 p,float t,float progress,bool arrival) {
    p.y*=1.5; // 相邻行裂口之间保留空隙，避免叠成贯穿多行的黑色长缝。
    float open=arrival ? 1.0-progress : smoothstep(0.0,.85,progress);
    float vertical=1.0-smoothstep(.72,.88,abs(p.y));
    float taper=pow(max(0.0,1.0-p.y*p.y/.70),.72);
    float axis=.023*sin(p.y*7.0+t*1.3)+.009*sin(p.y*19.0-t*2.0);
    float width=(.018+.45*open)*taper;
    float d=abs(p.x-axis)-width;
    float inside=(1.0-smoothstep(-.015,.015,d))*vertical;
    float fade=arrival ? pow(1.0-progress,1.2) : smoothstep(0.0,.12,progress);
    vec3 hue=mix(vec3(.19,.94,.80),vec3(.64,.30,1.0),.5+.5*sin(p.y*5.0+t*1.7));
    float depth=.5+.5*sin(p.y*13.0+sin(p.x*18.0)-t*2.0);
    vec4 result=vec4(0);
    result=polarOver(result,vec3(.025,.035,.12)+vec3(.035,.018,.07)*depth,
        inside*(arrival ? .12 : .87)*fade);
    vec2 tunnel=vec2((p.x-axis)/max(width,.025),p.y*1.3);
    float spiral=.5+.5*sin(length(tunnel)*11.0-atan(tunnel.y,tunnel.x)*2.0-t*2.3);
    result=polarOver(result,vec3(.33,.23,.67),pow(spiral,4.0)*inside*open*.23*fade);
    result=polarOver(result,hue,exp(-abs(d)*18.0)*vertical*.42*fade);
    result=polarOver(result,hue,polarStroke(d,.012)*vertical*.94*fade);
    result=polarOver(result,vec3(.83,1.0,.96),polarStroke(d,.003)*vertical*.88*fade);
    for(int i=0;i<3;++i) {
        float thread=d-(.032+float(i)*.022)*(1.0+.3*sin(p.y*9.0-t*2.0+float(i)));
        float sweep=.2+.8*pow(.5+.5*sin(p.y*12.0+t*3.0+float(i)*2.0),3.0);
        result=polarOver(result,hue,polarStroke(thread,.003)*vertical*sweep*.48*fade);
    }
    float floorGlow=exp(-p.x*p.x*9.0-pow((p.y-.72)*13.0,2.0));
    return polarOver(result,vec3(.34,.90,.88),floorGlow*.36*fade);
}

/** 双层钟环与独立逆转指针；标记外环消退严格读取 Board 的剩余时间。 */
vec4 polarClock(vec2 p,float t,float progress,int effect) {
    bool mark=effect==14, rewind=effect==15;
    float radius=rewind ? mix(.76,.30,progress) : .67;
    float r=length(p), angle=atan(p.y,p.x);
    float turn=fract((angle+POLAR_PI*.5)/(2.0*POLAR_PI)+1.0);
    float fade=rewind ? pow(1.0-progress,1.3) : (mark ? .78 : .50+.50*progress);
    vec3 gold=vec3(1.0,.72,.30), blue=vec3(.30,.84,1.0);
    vec4 result=vec4(0);
    float ring=abs(r-radius), inner=abs(r-radius*.78);
    result=polarOver(result,vec3(.025,.055,.12),exp(-ring*25.0)*.62*fade);
    result=polarOver(result,blue,exp(-ring*32.0)*.24*fade);
    result=polarOver(result,gold,polarStroke(r-radius,.008)*fade);
    result=polarOver(result,blue,polarStroke(inner,.004)*fade*.70);
    float tickAngle=abs(fract(turn*60.0+.5)-.5)*2.0*POLAR_PI/60.0*r;
    float major=1.0-smoothstep(.025,.075,abs(fract(turn*12.0+.5)-.5));
    float tickBand=smoothstep(radius-.085-major*.040,radius-.070-major*.040,r)
        *(1.0-smoothstep(radius-.025,radius-.012,r));
    result=polarOver(result,gold,polarStroke(tickAngle,.005)*tickBand*fade*.85);
    float arc=mark ? 1.0-smoothstep(progress-.012,progress+.012,turn)
        : pow(.5+.5*cos(angle+t*1.6),5.0);
    result=polarOver(result,vec3(.75,.98,1.0),polarStroke(r-radius-.032,.012)*arc*fade);
    float speed=rewind ? -9.0 : -1.8;
    vec2 hand=vec2(sin(t*speed),-cos(t*speed))*radius*.69;
    vec2 hour=vec2(sin(t*speed*.23+1.1),-cos(t*speed*.23+1.1))*radius*.45;
    float minuteLine=polarSegment(p,hand), hourLine=polarSegment(p,hour);
    result=polarOver(result,vec3(.025,.045,.10),exp(-min(minuteLine,hourLine)*55.0)*fade*.65);
    result=polarOver(result,blue,polarStroke(minuteLine,.007)*fade);
    result=polarOver(result,gold,polarStroke(hourLine,.011)*fade);
    result=polarOver(result,vec3(.94,.98,1.0),polarStroke(r,.022)*fade);
    if(!mark) {
        float orbit=r-(radius+.09+.025*sin(angle*3.0+t));
        float broken=pow(.5+.5*cos(angle*3.0-t*2.0),6.0);
        result=polarOver(result,blue,polarStroke(orbit,.004)*broken*fade*.6);
    }
    return result;
}

vec4 polarEffect(vec2 uv,vec4 data,float seconds,int effect) {
    vec2 p=uv*2.0-1.0;
    vec4 result;
    if(effect==10) result=polarChannel(p,seconds,data.x);
    else if(effect==11 || effect==12) result=polarRift(p,seconds,data.x,effect==12);
    else if(effect<16) result=polarClock(p,seconds,data.x,effect);
    else {
        // 矩形沿旧位置到恢复位置旋转，光痕向实际恢复点流动；并不插值游戏位置。
        float curve=.24*sin(uv.x*POLAR_PI)+.045*sin(uv.x*13.0-seconds*7.0);
        float d=p.y-curve;
        float ends=smoothstep(0.0,.06,uv.x)*(1.0-smoothstep(.94,1.0,uv.x));
        float head=exp(-pow((uv.x-data.x*1.22)/.16,2.0));
        float fade=pow(1.0-data.x,1.2)*ends;
        result=polarOver(vec4(0),vec3(.20,.62,1.0),exp(-abs(d)*14.0)*fade*.35);
        result=polarOver(result,vec3(.56,.90,1.0),polarStroke(d,.020)*(.24+.76*head)*fade);
        result=polarOver(result,vec3(1.0,.83,.46),polarStroke(d-.09,.012)*head*fade*.8);
    }
    // 软收边保证任何阶段的辉光都不会暴露承载 quad 的矩形边界。
    float border=(1.0-smoothstep(.92,1.0,abs(p.x)))*(1.0-smoothstep(.92,1.0,abs(p.y)));
    return result*(data.y*border);
}
